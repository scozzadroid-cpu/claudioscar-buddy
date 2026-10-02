<#
.SYNOPSIS
  USB configurator for claudioscar-buddy (Waveshare ESP32-S3-Touch-AMOLED-1.8 V1/V2).

.DESCRIPTION
  Talks to the firmware over USB serial using the same JSON protocol as the
  Bluetooth bridge (see REFERENCE.md). Run without parameters for a menu.

  No API key is needed: the buddy gets everything from Claude Desktop over BLE.

.EXAMPLE
  .\buddy-config.ps1                                   # interactive menu
  .\buddy-config.ps1 -Status
  .\buddy-config.ps1 -Owner "Alice" -PetName "Bufo" -SyncTime
  .\buddy-config.ps1 -Species cat
  .\buddy-config.ps1 -Character ..\characters\bufo
  .\buddy-config.ps1 -SdUpload .\sd-card -SdPath /     # copy a folder tree to the SD card
  .\buddy-config.ps1 -WifiSsid "MyWiFi" -WifiPass "secret"
  .\buddy-config.ps1 -Wifi                             # hotspot name/password, IP
  .\buddy-config.ps1 -CharacterFromSd clawd            # install a character stored on the SD card
  .\buddy-config.ps1 -UsageToken "sk-ant-oat01-..."    # Pro/Max usage (from `claude setup-token`)
  .\buddy-config.ps1 -OtaCheck                         # check GitHub for a newer release
  .\buddy-config.ps1 -Set '{"theme":2,"pack":"retro","volume":70}'
  .\buddy-config.ps1 -Flash .\claudioscar-buddy-v1.0.0-amoled-1.8-v2.bin
  .\buddy-config.ps1 -Backup .\backup.bin
#>
[CmdletBinding()]
param(
  [string]$Port,
  [switch]$Status,
  [string]$Owner,
  [string]$PetName,
  [string]$Species,
  [switch]$SyncTime,
  [string]$Character,
  [switch]$Unpair,
  [string]$Flash,
  [string]$Backup,
  [string]$SdUpload,
  [string]$SdPath = '/',
  [string]$WifiSsid,
  [string]$WifiPass = '',
  [switch]$Wifi,
  [string]$CharacterFromSd, # install /characters/<name> from the SD card
  [string]$UsageToken,      # token from `claude setup-token` (Pro/Max usage); '' removes it
  [switch]$OtaCheck,        # ask the buddy to check GitHub for an update now
  [string]$Set              # raw settings JSON, e.g. '{"theme":2,"pack":"retro","volume":70}'
)

$ErrorActionPreference = 'Stop'

$SPECIES_LIST = @('capybara','duck','goose','blob','cat','dragon','octopus','owl','penguin',
                  'turtle','snail','ghost','axolotl','cactus','robot','rabbit','mushroom','chonk')
$ESP_VID = 'VID_303A'
$script:sp = $null

# 216 bytes -> 288 base64 chars, under the firmware's 300-byte decode buffer.
# The firmware drains serial once per frame (~100 ms), so keep $WINDOW chunks
# in flight; 16 lines (~5 KB) fit the 8 KB RX buffer of firmware >= 1.0.0.
$CHUNK = 216
$WINDOW = 16

# ---------------------------------------------------------------- serial port

function Find-BuddyPort {
  $devs = Get-PnpDevice -PresentOnly -Class Ports -ErrorAction SilentlyContinue |
          Where-Object { $_.InstanceId -match $ESP_VID }
  foreach ($d in $devs) {
    if ($d.FriendlyName -match '\((COM\d+)\)') { return $Matches[1] }
  }
  return $null
}

function Open-Buddy {
  if ($script:sp -and $script:sp.IsOpen) { return }
  if (-not $script:Port) { $script:Port = Find-BuddyPort }
  if (-not $script:Port) { throw "Buddy not found. Plug it in over USB-C (or pass -Port COMx)." }
  $s = New-Object System.IO.Ports.SerialPort $script:Port, 115200
  $s.DtrEnable = $true
  $s.RtsEnable = $false
  $s.ReadTimeout = 200
  $s.NewLine = "`n"
  $s.Encoding = [System.Text.Encoding]::UTF8
  $s.Open()
  $script:sp = $s
  # Opening the port may reset the board: wait for boot to finish.
  $deadline = (Get-Date).AddSeconds(4)
  $boot = ''
  while ((Get-Date) -lt $deadline) {
    try { $boot += $s.ReadExisting() } catch {}
    if ($boot -match 'buddy: ') { break }
    Start-Sleep -Milliseconds 100
  }
  Start-Sleep -Milliseconds 300
  $s.DiscardInBuffer()
  Write-Host "Connected to $($script:Port)" -ForegroundColor DarkGray
}

function Close-Buddy {
  if ($script:sp) { try { $script:sp.Close() } catch {}; $script:sp = $null }
}

function Send-Json([hashtable]$obj) {
  $line = ($obj | ConvertTo-Json -Compress -Depth 5)
  $script:sp.Write($line + "`n")
}

# Send a command and wait for {"ack":"<cmd>",...}. Returns the ack object.
function Invoke-Buddy([hashtable]$obj, [int]$timeoutMs = 3000) {
  Open-Buddy
  $want = $obj.cmd
  Send-Json $obj
  $deadline = (Get-Date).AddMilliseconds($timeoutMs)
  while ((Get-Date) -lt $deadline) {
    try { $l = $script:sp.ReadLine() } catch [System.TimeoutException] { continue }
    $l = $l.Trim()
    if (-not $l.StartsWith('{')) { continue }
    try { $j = $l | ConvertFrom-Json } catch { continue }
    if ($j.ack -eq $want) { return $j }
  }
  throw "No reply from the buddy for '$want'."
}

# Stream a file's bytes as "chunk" commands, keeping $WINDOW in flight.
function Send-Chunks([byte[]]$bytes, [string]$label) {
  $o = 0; $inflight = 0
  $deadline = (Get-Date).AddSeconds(15)
  while ($o -lt $bytes.Length -or $inflight -gt 0) {
    while ($o -lt $bytes.Length -and $inflight -lt $WINDOW) {
      $n = [math]::Min($CHUNK, $bytes.Length - $o)
      $script:sp.Write('{"cmd":"chunk","d":"' + [Convert]::ToBase64String($bytes, $o, $n) + "`"}`n")
      $o += $n; $inflight++
    }
    try { $l = $script:sp.ReadLine() } catch [System.TimeoutException] { $l = '' }
    if ($l -match '"ack":"chunk","ok":(true|false),"n":(\d+)') {
      if ($Matches[1] -ne 'true') { throw "Transfer error in $label." }
      $inflight--
      $deadline = (Get-Date).AddSeconds(15)
      Write-Progress -Activity "Sending" -Status $label -PercentComplete ([math]::Min(100, 100 * [int]$Matches[2] / [math]::Max(1, $bytes.Length)))
    } elseif ((Get-Date) -gt $deadline) {
      throw "No reply from the buddy while sending $label."
    }
  }
}

# ---------------------------------------------------------------- commands

function Show-Status {
  $r = Invoke-Buddy @{ cmd = 'status' }
  $d = $r.data
  Write-Host ""
  Write-Host "  Pet          $($d.name)"
  Write-Host "  Owner        $($d.owner)"
  Write-Host "  Bluetooth    $(if ($d.sec) { 'paired (encrypted)' } else { 'not paired' })"
  Write-Host "  Battery      $($d.bat.pct)%  $($d.bat.mV) mV  $(if ($d.bat.usb) { '(USB)' })"
  Write-Host "  Uptime       $([TimeSpan]::FromSeconds($d.sys.up).ToString())"
  Write-Host "  Memory       heap $([math]::Round($d.sys.heap/1024)) KB, flash free $([math]::Round($d.sys.fsFree/1024)) / $([math]::Round($d.sys.fsTotal/1024)) KB"
  Write-Host "  Stats        approved $($d.stats.appr), denied $($d.stats.deny), level $($d.stats.lvl)"
  Write-Host ""
}

function Set-Owner([string]$name) {
  $r = Invoke-Buddy @{ cmd = 'owner'; name = $name }
  if ($r.ok) { Write-Host "Owner set: $name" -ForegroundColor Green }
}

function Set-PetName([string]$name) {
  $r = Invoke-Buddy @{ cmd = 'name'; name = $name }
  if ($r.ok) { Write-Host "Pet name set: $name" -ForegroundColor Green }
}

function Set-Species([string]$s) {
  if ($s -eq 'gif') { $idx = 255 }
  elseif ($s -match '^\d+$') { $idx = [int]$s }
  else { $idx = [array]::IndexOf($SPECIES_LIST, $s.ToLower()) }
  if ($idx -lt 0 -or ($idx -ge $SPECIES_LIST.Count -and $idx -ne 255)) {
    throw "Unknown species '$s'. Valid: $($SPECIES_LIST -join ', '), gif"
  }
  $null = Invoke-Buddy @{ cmd = 'species'; idx = $idx }
  $label = if ($idx -eq 255) { 'GIF character' } else { $SPECIES_LIST[$idx] }
  Write-Host "Species set: $label" -ForegroundColor Green
}

function Sync-Time {
  $now = [DateTimeOffset]::Now
  $epoch = $now.ToUnixTimeSeconds()
  $tz = [int]$now.Offset.TotalSeconds
  Open-Buddy
  # The time command has no ack: fire and forget.
  $script:sp.Write("{`"time`":[$epoch,$tz]}`n")
  Start-Sleep -Milliseconds 300
  Write-Host "Clock synced: $($now.ToString('yyyy-MM-dd HH:mm:ss zzz'))" -ForegroundColor Green
}

function Remove-Pairing {
  $null = Invoke-Buddy @{ cmd = 'unpair' }
  Write-Host "Bluetooth bonds cleared. Pair again from Claude Desktop." -ForegroundColor Yellow
}

function Send-Character([string]$dir) {
  $dir = (Resolve-Path $dir).Path
  $manifest = Join-Path $dir 'manifest.json'
  if (-not (Test-Path $manifest)) { throw "No manifest.json in $dir (prepare it with tools/prep_character.py)." }
  $name = (Get-Content $manifest -Raw | ConvertFrom-Json).name
  $files = Get-ChildItem $dir -File | Where-Object { $_.Extension -in '.json', '.gif' }
  $total = ($files | Measure-Object Length -Sum).Sum
  Write-Host "Sending character '$name': $($files.Count) files, $([math]::Round($total/1024)) KB"

  $r = Invoke-Buddy @{ cmd = 'char_begin'; name = $name; total = $total } 8000
  if (-not $r.ok) { throw "Buddy refused: $($r.error)" }
  foreach ($f in $files) {
    $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
    $r = Invoke-Buddy @{ cmd = 'file'; path = $f.Name; size = $bytes.Length }
    if (-not $r.ok) { throw "Could not create $($f.Name) on the buddy." }
    Send-Chunks $bytes $f.Name
    $r = Invoke-Buddy @{ cmd = 'file_end' } 5000
    if (-not $r.ok) { throw "Size check failed for $($f.Name)." }
  }
  $r = Invoke-Buddy @{ cmd = 'char_end' } 8000
  Write-Progress -Activity "Sending" -Completed
  if ($r.ok) { Write-Host "Character '$name' installed." -ForegroundColor Green }
  else { throw "The buddy could not load the character." }
}

# Copy a local folder tree to the microSD card (needs firmware >= 1.0.0).
function Send-SdTree([string]$dir, [string]$dest) {
  $dir = (Resolve-Path $dir).Path.TrimEnd('\')
  $dest = '/' + $dest.Trim('/')
  if ($dest -eq '/') { $dest = '' }
  $files = @(Get-ChildItem $dir -File -Recurse)
  $total = ($files | Measure-Object Length -Sum).Sum
  Write-Host "Copying $($files.Count) files ($([math]::Round($total/1024)) KB) to the SD card..."
  $made = @{}
  $i = 0
  foreach ($f in $files) {
    $i++
    $remote = $dest + $f.FullName.Substring($dir.Length).Replace('\', '/')
    # The firmware creates one directory level per put; create parents first
    # with an empty placeholder write.
    $parts = $remote.Trim('/').Split('/')
    for ($k = 1; $k -lt $parts.Count - 1; $k++) {
      $parent = '/' + ($parts[0..($k - 1)] -join '/')
      if ($made[$parent]) { continue }
      $null = Invoke-Buddy @{ cmd = 'put'; path = "$parent/.keep"; size = 0; sd = $true }
      $null = Invoke-Buddy @{ cmd = 'file_end' }
      $made[$parent] = $true
    }
    $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
    Write-Host ("  [{0}/{1}] {2}" -f $i, $files.Count, $remote)
    for ($try = 1; ; $try++) {
      try {
        $r = Invoke-Buddy @{ cmd = 'put'; path = $remote; size = $bytes.Length; sd = $true }
        if (-not $r.ok) { throw "SD card not available or cannot write $remote." }
        if ($bytes.Length) { Send-Chunks $bytes $remote }
        $r = Invoke-Buddy @{ cmd = 'file_end' } 5000
        if (-not $r.ok) { throw "Size check failed for $remote." }
        break
      } catch {
        if ($try -ge 3) { throw }
        Write-Host "    retrying ($($_.Exception.Message))" -ForegroundColor Yellow
        Start-Sleep -Milliseconds 500
        $script:sp.DiscardInBuffer()
      }
    }
  }
  Write-Progress -Activity "Sending" -Completed
  Write-Host "Done." -ForegroundColor Green
}

function Set-Settings([string]$json) {
  $h = @{ cmd = 'set' }
  ($json | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $h[$_.Name] = $_.Value }
  $null = Invoke-Buddy $h
  Write-Host "Settings applied." -ForegroundColor Green
}

function Set-Wifi([string]$ssid, [string]$pass) {
  $null = Invoke-Buddy @{ cmd = 'wifi'; ssid = $ssid; pass = $pass }
  if ($ssid) { Write-Host "WiFi saved: $ssid (the buddy is connecting)" -ForegroundColor Green }
  else { Write-Host "WiFi network forgotten: the buddy will run its hotspot." -ForegroundColor Yellow }
}

function Show-Wifi {
  $r = Invoke-Buddy @{ cmd = 'net' }
  Write-Host ""
  Write-Host "  Mode        $($r.mode)"
  if ($r.ssid) { Write-Host "  Network     $($r.ssid)" }
  Write-Host "  IP          $($r.ip)"
  Write-Host "  Hotspot     $($r.ap)"
  Write-Host "  Password    $($r.appw)   (also the web login, user 'buddy')"
  if ($r.mode -eq 'sta') { Write-Host "  Web page    http://claudioscar-buddy.local  or  http://$($r.ip)" }
  else { Write-Host "  Web page    join the hotspot, then open http://192.168.4.1" }
  Write-Host ""
}

# ---------------------------------------------------------------- esptool

function Get-Esptool {
  $cands = @(
    (Get-Command esptool.exe -ErrorAction SilentlyContinue).Source,
    "$env:LOCALAPPDATA\Programs\Python\Python312\Scripts\esptool.exe",
    "$env:USERPROFILE\.platformio\penv\Scripts\esptool.exe"
  ) | Where-Object { $_ -and (Test-Path $_) }
  if ($cands) { return @($cands[0]) }
  $py = Get-Command py.exe, python.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.Source -notmatch 'WindowsApps' } | Select-Object -First 1
  if ($py) {
    & $py.Source -m esptool version *> $null
    if ($LASTEXITCODE -ne 0) {
      Write-Host "Installing esptool..." -ForegroundColor DarkGray
      & $py.Source -m pip install --quiet esptool
    }
    return @($py.Source, '-m', 'esptool')
  }
  throw "esptool is required. Install Python (winget install Python.Python.3.12) and retry."
}

function Invoke-Esptool([string[]]$esptoolArgs) {
  Close-Buddy
  if (-not $script:Port) { $script:Port = Find-BuddyPort }
  if (-not $script:Port) { throw "Buddy not found." }
  $et = Get-Esptool
  $exe = $et[0]; $pre = @($et | Select-Object -Skip 1)
  & $exe @pre --port $script:Port @esptoolArgs
  if ($LASTEXITCODE -ne 0) { throw "esptool failed with exit code $LASTEXITCODE." }
}

function Install-Firmware([string]$bin) {
  $bin = (Resolve-Path $bin).Path
  Write-Host "Flashing $bin (settings and characters will be erased)." -ForegroundColor Yellow
  Invoke-Esptool @('--baud', '921600', 'write-flash', '--erase-all', '0x0', $bin)
  Write-Host "Firmware installed." -ForegroundColor Green
}

function Save-Backup([string]$out) {
  Write-Host "Full flash backup (16 MB, about 2-3 minutes)..."
  Invoke-Esptool @('--baud', '460800', 'read-flash', '0', '0x1000000', $out)
  Write-Host "Backup saved to $out" -ForegroundColor Green
}

# ---------------------------------------------------------------- menu

function Read-NonEmpty([string]$prompt) {
  do { $v = Read-Host $prompt } while (-not $v.Trim())
  return $v.Trim()
}

function Show-Menu {
  while ($true) {
    Write-Host ""
    Write-Host "=== claudioscar-buddy - configuration ===" -ForegroundColor Cyan
    Write-Host "  1) Status"
    Write-Host "  2) Set your name (owner)"
    Write-Host "  3) Set the pet name"
    Write-Host "  4) Choose the pet species"
    Write-Host "  5) Sync clock with this PC"
    Write-Host "  6) Upload a GIF character (folder)"
    Write-Host "  7) Clear Bluetooth pairing"
    Write-Host "  8) WiFi info (hotspot password, IP)"
    Write-Host "  9) Set home WiFi"
    Write-Host " 10) Copy a folder to the SD card"
    Write-Host " 11) Install/update firmware (.bin)"
    Write-Host " 12) Full flash backup"
    Write-Host "  0) Exit"
    $c = Read-Host "Choice"
    try {
      switch ($c) {
        '1'  { Show-Status }
        '2'  { Set-Owner (Read-NonEmpty "Name") }
        '3'  { Set-PetName (Read-NonEmpty "Pet name") }
        '4'  {
          for ($i = 0; $i -lt $SPECIES_LIST.Count; $i++) { Write-Host ("  {0,2}) {1}" -f $i, $SPECIES_LIST[$i]) }
          Write-Host "  gif) uploaded GIF character"
          Set-Species (Read-NonEmpty "Species")
        }
        '5'  { Sync-Time }
        '6'  { Send-Character (Read-NonEmpty "Character folder") }
        '7'  { if ((Read-Host "Are you sure? (y/N)") -eq 'y') { Remove-Pairing } }
        '8'  { Show-Wifi }
        '9'  { Set-Wifi (Read-Host "SSID (empty = forget)") (Read-Host "Password") }
        '10' {
          $d = Read-Host "Destination on the card [/]"
          if (-not $d) { $d = '/' }
          Send-SdTree (Read-NonEmpty "Local folder") $d
        }
        '11' {
          $f = Read-NonEmpty "Path to .bin file"
          if ((Read-Host "Erase everything and flash? (y/N)") -eq 'y') { Install-Firmware $f }
        }
        '12' { Save-Backup (Read-NonEmpty "Output file (e.g. backup.bin)") }
        '0'  { return }
        default { Write-Host "Invalid choice." }
      }
    } catch {
      Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    }
  }
}

# ---------------------------------------------------------------- main

try {
  $hasWifiSsid = $PSBoundParameters.ContainsKey('WifiSsid')
  $any = $Status -or $Owner -or $PetName -or $Species -or $SyncTime -or $Character -or $Unpair -or
         $Flash -or $Backup -or $SdUpload -or $hasWifiSsid -or $Wifi -or $Set -or $CharacterFromSd -or
         $PSBoundParameters.ContainsKey('UsageToken') -or $OtaCheck
  if (-not $any) { Show-Menu; return }
  if ($Backup)      { Save-Backup $Backup }
  if ($Flash)       { Install-Firmware $Flash; Start-Sleep 3 }
  if ($Owner)       { Set-Owner $Owner }
  if ($PetName)     { Set-PetName $PetName }
  if ($Species)     { Set-Species $Species }
  if ($SyncTime)    { Sync-Time }
  if ($Character)   { Send-Character $Character }
  if ($Unpair)      { Remove-Pairing }
  if ($SdUpload)    { Send-SdTree $SdUpload $SdPath }
  if ($hasWifiSsid) { Set-Wifi $WifiSsid $WifiPass }
  if ($Set)         { Set-Settings $Set }
  if ($PSBoundParameters.ContainsKey('UsageToken')) {
    $null = Invoke-Buddy @{ cmd = 'set'; usageToken = $UsageToken.Trim() }
    if ($UsageToken) { Write-Host "Usage token saved; the buddy polls it once it is on your home WiFi." -ForegroundColor Green }
    else { Write-Host "Usage token removed." -ForegroundColor Yellow }
  }
  if ($OtaCheck) {
    $null = Invoke-Buddy @{ cmd = 'ota'; do = 'check' }
    Start-Sleep -Seconds 8
    $o = (Invoke-Buddy @{ cmd = 'net' }).ota
    Write-Host "OTA: installed v$($o.current), state '$($o.state)'$(if ($o.latest) { ", latest v$($o.latest)" })$(if ($o.error) { " - $($o.error)" })"
  }
  if ($CharacterFromSd) {
    $r = Invoke-Buddy @{ cmd = 'char_sd'; name = $CharacterFromSd } 60000
    if ($r.ok) { Write-Host "Character '$CharacterFromSd' installed from the SD card." -ForegroundColor Green }
    else { throw "Could not install '$CharacterFromSd' from the SD card." }
  }
  if ($Wifi)        { Show-Wifi }
  if ($Status)      { Show-Status }
} finally {
  Close-Buddy
}
