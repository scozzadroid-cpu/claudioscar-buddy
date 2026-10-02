<#
.SYNOPSIS
  USB self-test for claudioscar-buddy: drives the firmware with fake Claude
  traffic and checks the replies. Plug the buddy in and run it.

.DESCRIPTION
  1. status / net / settings round-trip
  2. every sound event (look/listen on the buddy)
  3. a fake session (sessions, transcript, tokens)
  4. a fake permission prompt — press BOOT (approve) or PWR (deny) on the
     buddy within -PromptSeconds; the decision must come back over USB
  5. angry outburst, then back to idle

  Exit code 0 = all automatic checks passed.

.EXAMPLE
  .\buddy-test.ps1
  .\buddy-test.ps1 -SkipPrompt
#>
param(
  [string]$Port,
  [int]$PromptSeconds = 30,
  [switch]$SkipPrompt
)

$ErrorActionPreference = 'Stop'
$fail = 0
function Pass($m) { Write-Host "  PASS  $m" -ForegroundColor Green }
function Fail($m) { Write-Host "  FAIL  $m" -ForegroundColor Red; $script:fail++ }

if (-not $Port) {
  $d = Get-PnpDevice -PresentOnly -Class Ports -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -match 'VID_303A' } | Select-Object -First 1
  if ($d -and $d.FriendlyName -match '\((COM\d+)\)') { $Port = $Matches[1] }
}
if (-not $Port) { throw "Buddy not found on USB." }

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.DtrEnable = $true
$sp.ReadTimeout = 200
$sp.NewLine = "`n"
$sp.Open()
Start-Sleep -Seconds 2
$sp.DiscardInBuffer()

function Send([string]$j) { $sp.Write($j + "`n") }
# Send and wait for a line matching $pattern; returns the line or $null.
function Expect([string]$j, [string]$pattern, [int]$ms = 3000) {
  if ($j) { Send $j }
  $end = (Get-Date).AddMilliseconds($ms)
  while ((Get-Date) -lt $end) {
    try { $l = $sp.ReadLine() } catch [System.TimeoutException] { continue }
    if ($l -match $pattern) { return $l.Trim() }
  }
  return $null
}

try {
  Write-Host "claudioscar-buddy self-test on $Port" -ForegroundColor Cyan

  Write-Host "`n[1] basics"
  $r = Expect '{"cmd":"status"}' '"ack":"status"'
  if ($r) { $s = ($r | ConvertFrom-Json).data; Pass "status: pet '$($s.name)', owner '$($s.owner)', heap $([math]::Round($s.sys.heap/1024)) KB" } else { Fail "status" }
  $r = Expect '{"cmd":"net"}' '"ack":"net"'
  if ($r) { $n = $r | ConvertFrom-Json; Pass "net: mode $($n.mode), ip $($n.ip), firmware v$($n.ota.current)" } else { Fail "net" }
  if (Expect '{"cmd":"set","angry":50}' '"ack":"set"') { Pass "settings write" } else { Fail "settings write" }

  Write-Host "`n[2] sounds (listen)"
  $names = 'boot','prompt','approve','deny','celebrate','dizzy','angry'
  for ($i = 0; $i -lt $names.Count; $i++) {
    if (Expect "{`"cmd`":`"sfx`",`"ev`":$i}" '"ack":"sfx"') { Pass "sound '$($names[$i])' queued" } else { Fail "sound '$($names[$i])'" }
    Start-Sleep -Milliseconds 1800
  }

  Write-Host "`n[3] fake session"
  $now = [DateTimeOffset]::Now
  Send "{`"time`":[$($now.ToUnixTimeSeconds()),$([int]$now.Offset.TotalSeconds)]}"
  Send '{"total":3,"running":2,"waiting":0,"msg":"self-test running","entries":["self-test: git status","self-test: pio run","self-test: reading main.cpp"],"tokens_today":123456}'
  Pass "session pushed (check the HUD shows 'self-test running')"
  Start-Sleep -Seconds 3

  if (-not $SkipPrompt) {
    Write-Host "`n[4] permission prompt - press BOOT (approve) or PWR (deny) on the buddy..."
    $prompt = '{"total":1,"running":0,"waiting":1,"msg":"approve?","entries":["self-test: Bash echo hi"],"prompt":{"id":"selftest-1","tool":"Bash","hint":"echo hi"}}'
    $got = $null
    $end = (Get-Date).AddSeconds($PromptSeconds)
    while (-not $got -and (Get-Date) -lt $end) {
      $got = Expect $prompt '"cmd":"permission"' 2000   # re-send like the desktop heartbeat
    }
    if ($got) {
      $d = $got | ConvertFrom-Json
      if ($d.id -eq 'selftest-1') { Pass "decision received: $($d.decision)" } else { Fail "decision for wrong id: $got" }
    } else { Fail "no decision within $PromptSeconds s" }
  }

  Write-Host "`n[5] back to idle"
  Send '{"total":1,"running":0,"waiting":0,"msg":"self-test done","entries":["self-test: done"]}'
  Pass "idle state pushed"
} finally {
  $sp.Close()
}

Write-Host ""
if ($fail) { Write-Host "$fail check(s) failed." -ForegroundColor Red; exit 1 }
Write-Host "All automatic checks passed." -ForegroundColor Green
