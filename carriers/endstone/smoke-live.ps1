<#
    Live smoke test for the Endstone carrier on Windows.

    Starts an already prepared server through the Endstone CLI, asks for the
    plugin list, stops it, and checks that the CLR came up and the carrier took
    over. The Linux counterpart is smoke.sh.

    Usage: powershell -File smoke-live.ps1 -ServerRoot <folder with bedrock_server.exe>

    ASCII only: Windows PowerShell 5.1 reads a BOM-less script in the system
    codepage. The expected Chinese log lines are built from [char] codes.
#>
param(
    [Parameter(Mandatory = $true)][string]$ServerRoot
)

$ErrorActionPreference = 'Stop'
$server = (Resolve-Path $ServerRoot).Path

if (Get-Process -Name 'bedrock_server', 'bedrock_server_mod' -ErrorAction SilentlyContinue) {
    throw 'a server is already running; stop it first'
}

$endstone = $env:ENDSTONE_EXE
if (-not $endstone) {
    $candidate = Join-Path $env:TEMP 'endstone-venv\Scripts\endstone.exe'
    if (Test-Path $candidate) { $endstone = $candidate }
}
if (-not $endstone) {
    $found = Get-Command endstone -ErrorAction SilentlyContinue
    if ($found) { $endstone = $found.Source }
}
if (-not $endstone -or -not (Test-Path $endstone)) {
    throw 'endstone CLI not found; set ENDSTONE_EXE or put endstone on PATH'
}

$exe = Join-Path $server 'bedrock_server.exe'
if (-not (Test-Path $exe)) { throw "no bedrock_server.exe under $server" }

$wait = if ($env:HOSIHIKARI_SMOKE_DELAY) { [Math]::Max(1, [int]$env:HOSIHIKARI_SMOKE_DELAY) } else { 60 }
$settle = if ($env:HOSIHIKARI_SMOKE_SETTLE) { [Math]::Max(1, [int]$env:HOSIHIKARI_SMOKE_SETTLE) } else { 8 }
$timeoutSeconds = if ($env:HOSIHIKARI_SMOKE_TIMEOUT) { [Math]::Max(1, [int]$env:HOSIHIKARI_SMOKE_TIMEOUT) } else { 90 }

$log = Join-Path $server 'smoke-live.log'
$exitFile = Join-Path $server 'smoke-live.exit'
$driver = Join-Path $env:TEMP 'hosihikari-endstone-live.cmd'
Remove-Item $log, $exitFile -Force -ErrorAction SilentlyContinue

$lines = @(
    '@echo off'
    '('
    '  ping -n ' + $wait + ' 127.0.0.1 >nul & echo plugins'
    '  ping -n ' + $settle + ' 127.0.0.1 >nul & echo stop'
    ') | "' + $endstone + '" -s "' + $server + '" -y > "' + $log + '" 2>&1'
    'set SERVER_EXIT=%ERRORLEVEL%'
    'echo %SERVER_EXIT% > "' + $exitFile + '"'
    'exit /b %SERVER_EXIT%'
)
Set-Content -Path $driver -Value $lines -Encoding ASCII

$run = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', "`"$driver`"" `
    -WorkingDirectory $server -WindowStyle Hidden -PassThru
$deadline = (Get-Date).AddSeconds($timeoutSeconds)
while (-not $run.HasExited -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 250
}
$timedOut = -not $run.HasExited
if ($timedOut) {
    & taskkill /PID $run.Id /T /F 2>&1 | Out-Null
    $run.WaitForExit(5000) | Out-Null
}

$serverExit = $null
if (Test-Path $exitFile) {
    $parsed = 0
    if ([int]::TryParse((Get-Content $exitFile -Raw).Trim(), [ref]$parsed)) {
        $serverExit = $parsed
    }
}

$text = if (Test-Path $log) { Get-Content $log -Raw -Encoding UTF8 } else { '' }
$ready = -join ([char]0x5DF2, [char]0x5C31, [char]0x7EEA)
$tookOver = -join ([char]0x5DF2, [char]0x63A5, [char]0x7BA1)
$checks = @(
    @{ Name = 'server exited cleanly'; Ok = (-not $timedOut -and $serverExit -eq 0) },
    @{ Name = 'carrier loads the CLR'; Ok = ($text.Contains('CLR') -and $text.Contains('BDS')) },
    @{ Name = 'core reports ready'; Ok = $text.Contains($ready) },
    @{ Name = 'carrier takes over'; Ok = $text.Contains($tookOver) },
    @{ Name = 'parent enables carrier'; Ok = $text.Contains('Enabling hosihikari') },
    @{ Name = 'server stops'; Ok = ($text.Contains('Server stop requested') -or $text.Contains('Stopping server')) }
)
$failed = $false
foreach ($check in $checks) {
    Write-Host ("{0}  {1}" -f $(if ($check.Ok) { 'PASS' } else { 'FAIL' }), $check.Name)
    if (-not $check.Ok) { $failed = $true }
}
foreach ($bad in @('Unhandled exception', 'Segmentation fault')) {
    if ($text.Contains($bad)) {
        Write-Host "FAIL  the server died: $bad"
        $failed = $true
    }
}
if ($failed) {
    Write-Host "see $log"
    exit 1
}

Write-Host "PASS  Windows Endstone carrier live smoke; log at $log"
