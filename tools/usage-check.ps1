<#
.SYNOPSIS
  Check a `claude setup-token` token against the same two requests the buddy
  makes for the Claude usage page — run it on your own PC.

.DESCRIPTION
  The token is read as a hidden prompt (or from $env:CLAUDE_USAGE_TOKEN), sent
  only to api.anthropic.com, never printed and never written to disk.

  1. GET  /api/oauth/usage          (free; may be rate limited)
  2. POST /v1/messages, max_tokens 1 (reads anthropic-ratelimit-unified-* headers)

.EXAMPLE
  .\usage-check.ps1
#>
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$tok = $env:CLAUDE_USAGE_TOKEN
if (-not $tok) {
  $sec = Read-Host "Paste the token from 'claude setup-token' (hidden)" -AsSecureString
  $tok = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec))
}
$tok = ($tok -replace '\s', '')
if (-not $tok) { throw "No token." }
Write-Host "Token: ...$($tok.Substring([math]::Max(0, $tok.Length - 4)))  ($($tok.Length) chars)"

$ua = 'claudioscar-buddy/usage-check'
Add-Type -AssemblyName System.Net.Http
$http = New-Object System.Net.Http.HttpClient
$http.Timeout = [TimeSpan]::FromSeconds(20)

function Show-Window($name, $pct, $reset) {
  if ($null -eq $pct) { Write-Host ("  {0,-7} --" -f $name); return }
  $left = ''
  if ($reset) { $ts = $reset - [DateTimeOffset]::UtcNow; if ($ts.TotalSeconds -gt 0) { $left = "  resets in {0}d {1}h {2}m" -f $ts.Days, $ts.Hours, $ts.Minutes } }
  Write-Host ("  {0,-7} {1,5:N1}%{2}" -f $name, $pct, $left) -ForegroundColor Green
}

Write-Host "`n[1] GET https://api.anthropic.com/api/oauth/usage"
$req = New-Object System.Net.Http.HttpRequestMessage 'GET', 'https://api.anthropic.com/api/oauth/usage'
$req.Headers.Authorization = New-Object System.Net.Http.Headers.AuthenticationHeaderValue 'Bearer', $tok
$null = $req.Headers.TryAddWithoutValidation('anthropic-beta', 'oauth-2025-04-20')
$null = $req.Headers.TryAddWithoutValidation('User-Agent', $ua)
$res = $http.SendAsync($req).GetAwaiter().GetResult()
$body = $res.Content.ReadAsStringAsync().GetAwaiter().GetResult()
Write-Host "  HTTP $([int]$res.StatusCode)"
if ($res.IsSuccessStatusCode) {
  $j = $body | ConvertFrom-Json
  if ($j.limits) {
    foreach ($l in $j.limits) { if ($l.id -in 'session', 'weekly_all') { Show-Window $l.id $l.percent ($(if ($l.resets_at) { [DateTimeOffset]::Parse($l.resets_at) })) } }
  } else {
    Show-Window '5-hour' $j.five_hour.utilization ($(if ($j.five_hour.resets_at) { [DateTimeOffset]::Parse($j.five_hour.resets_at) }))
    Show-Window 'weekly' $j.seven_day.utilization ($(if ($j.seven_day.resets_at) { [DateTimeOffset]::Parse($j.seven_day.resets_at) }))
  }
} else { Write-Host "  $($body.Substring(0, [math]::Min(200, $body.Length)))" -ForegroundColor Yellow }

Write-Host "`n[2] POST https://api.anthropic.com/v1/messages (max_tokens 1)"
$req = New-Object System.Net.Http.HttpRequestMessage 'POST', 'https://api.anthropic.com/v1/messages'
$req.Headers.Authorization = New-Object System.Net.Http.Headers.AuthenticationHeaderValue 'Bearer', $tok
$null = $req.Headers.TryAddWithoutValidation('anthropic-version', '2023-06-01')
$null = $req.Headers.TryAddWithoutValidation('anthropic-beta', 'oauth-2025-04-20')
$null = $req.Headers.TryAddWithoutValidation('User-Agent', $ua)
$req.Content = New-Object System.Net.Http.StringContent '{"model":"claude-haiku-4-5-20251001","max_tokens":1,"messages":[{"role":"user","content":"."}]}', ([Text.Encoding]::UTF8), 'application/json'
$res = $http.SendAsync($req).GetAwaiter().GetResult()
$body = $res.Content.ReadAsStringAsync().GetAwaiter().GetResult()
Write-Host "  HTTP $([int]$res.StatusCode)"
function Get-RlHeader($n) { $v = $null; if ($res.Headers.TryGetValues($n, [ref]$v)) { return ($v -join ',') }; return $null }
$u5 = Get-RlHeader 'anthropic-ratelimit-unified-5h-utilization'; $r5 = Get-RlHeader 'anthropic-ratelimit-unified-5h-reset'
$u7 = Get-RlHeader 'anthropic-ratelimit-unified-7d-utilization'; $r7 = Get-RlHeader 'anthropic-ratelimit-unified-7d-reset'
if ($u5 -or $u7) {
  Show-Window '5-hour' $(if ($u5) { [double]$u5 * 100 }) $(if ($r5) { [DateTimeOffset]::FromUnixTimeSeconds([long]$r5) })
  Show-Window 'weekly' $(if ($u7) { [double]$u7 * 100 }) $(if ($r7) { [DateTimeOffset]::FromUnixTimeSeconds([long]$r7) })
} else {
  Write-Host "  no unified rate-limit headers (needs a Pro/Max token)" -ForegroundColor Yellow
  if (-not $res.IsSuccessStatusCode) { Write-Host "  $($body.Substring(0, [math]::Min(200, $body.Length)))" -ForegroundColor Yellow }
}
$tok = $null
