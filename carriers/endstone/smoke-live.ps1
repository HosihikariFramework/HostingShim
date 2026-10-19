<#
    Live smoke test for the Endstone carrier on Windows.

    Starts an already prepared server through the Endstone CLI, asks for the
    plugin list, stops it, and checks that the CLR came up and the carrier took
    over. The Linux counterpart is smoke.sh.

    Usage: powershell -File smoke-live.ps1 -ServerRoot <folder with bedrock_server.exe>

    ASCII only: Windows PowerShell 5.1 reads a BOM-less script in the system
    codepage. The expected Chinese log lines are written as regex \u escapes.
#>
param(
    [Parameter(Mandatory = $true)][string]$ServerRoot
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
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

$log = Join-Path $server 'smoke-live.log'
$driver = Join-Path $env:TEMP 'hosihikari-endstone-live.cmd'
$lines = @(
    '@echo off'
    '('
    '  ping -n 60 127.0.0.1 >nul & echo plugins'
    '  ping -n 8 127.0.0.1 >nul & echo stop'
    ') | "' + $endstone + '" -s "' + $server + '" -y > "' + $log + '" 2>&1'
)
Set-Content -Path $driver -Value $lines -Encoding ASCII

$run = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', "`"$driver`"" `
    -WorkingDirectory $server -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 90
if (-not $run.HasExited) {
    & taskkill /PID $run.Id /T /F 2>&1 | Out-Null
}
Start-Sleep -Seconds 2

$text = Get-Content $log -Raw -Encoding UTF8
$ready = -join ([char]0x5DF2, [char]0x5C31, [char]0x7EEA)
$tookOver = -join ([char]0x5DF2, [char]0x63A5, [char]0x7BA1)
$checks = @(
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
