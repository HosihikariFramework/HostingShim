<#
    Live smoke test for the LeviLamina carrier on Windows.

    Starts an already prepared server through bedrock_server_mod.exe, asks it to
    stop, and checks that the CLR came up and the carrier took over.

    Usage: powershell -File smoke-live.ps1 -ServerRoot <server root>

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

$exe = if ($env:HOSIHIKARI_SMOKE_EXE) {
    $env:HOSIHIKARI_SMOKE_EXE
} else {
    Join-Path $server 'bedrock_server_mod.exe'
}
if (-not (Test-Path $exe)) { throw "no bedrock_server_mod.exe under $server" }

$wait = if ($env:HOSIHIKARI_SMOKE_DELAY) { [Math]::Max(1, [int]$env:HOSIHIKARI_SMOKE_DELAY) } else { 60 }
$timeoutSeconds = if ($env:HOSIHIKARI_SMOKE_TIMEOUT) { [Math]::Max(1, [int]$env:HOSIHIKARI_SMOKE_TIMEOUT) } else { 90 }

$log = Join-Path $server 'smoke-live.log'
$exitFile = Join-Path $server 'smoke-live.exit'
$driver = Join-Path $env:TEMP 'hosihikari-levilamina-live.cmd'
Remove-Item $log, $exitFile -Force -ErrorAction SilentlyContinue

$lines = @(
    '@echo off'
    '('
    '  ping -n ' + $wait + ' 127.0.0.1 >nul & echo stop'
    ') | "' + $exe + '" > "' + $log + '" 2>&1'
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
    @{ Name = 'carrier loads'; Ok = $text.Contains('hosihikari v') },
    @{ Name = 'core reports ready'; Ok = $text.Contains($ready) },
    @{ Name = 'carrier takes over'; Ok = $text.Contains($tookOver) },
    @{ Name = 'server starts'; Ok = $text.Contains('Server started') },
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

Write-Host "PASS  Windows LeviLamina carrier live smoke; log at $log"
