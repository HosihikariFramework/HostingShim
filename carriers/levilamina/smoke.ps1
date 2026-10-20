<#
    Live smoke test for the LeviLamina carrier.

    Deploys the shim into a prepared server directory, stages a real plugin package,
    drives the server console through show / disable / enable / unload / load, and
    checks what the log says.

    Local only: it needs a BDS directory with LeviLamina already installed, and it
    waits for the world to come up. This is the repeatable form of what used to be
    hand-typed command sequences.

    ASCII only, like the .bat files: Windows PowerShell 5.1 reads a script without
    a BOM using the system codepage, and non-ASCII bytes corrupt the lines after them.
#>
param(
    [Parameter(Mandatory = $true)][string]$ServerRoot,
    [string]$PluginName = 'Bar'
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot

# Deploying replaces files the server holds open, and a leftover run would make the
# checks read somebody else's log. Only the process this script starts is killed, so
# an already running server is the operator's call, not ours.
$running = Get-Process -Name 'bedrock_server_mod' -ErrorAction SilentlyContinue
if ($running) {
    throw "a server is already running (PID $($running.Id -join ', ')); stop it first"
}

$harness = Join-Path $here '..\..\..\Hosihikari.PluginManager\host\Hosihikari.PluginManager.Host.csproj'
if (-not (Test-Path $harness)) {
    throw "expected the managed core as a sibling checkout: $harness"
}

Write-Host '== managed harness (it also stages the plugin packages) =='
& dotnet run --project $harness -c Release --nologo | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'the managed harness failed; fix that before the live run' }

# The staged package depends on Foo, which cannot run here (it wants the carrier
# binding). Keep only the dependency on the framework so the parent will load it.
$staged = Join-Path $env:TEMP "hosihikari-spike\plugins\$PluginName"
$package = Join-Path $ServerRoot "plugins\$PluginName"
if (Test-Path $package) { Remove-Item $package -Recurse -Force }
New-Item -ItemType Directory -Path $package | Out-Null
Copy-Item "$staged\*.dll" $package
Copy-Item "$staged\manifest.json" $package

$manifestPath = Join-Path $package 'manifest.json'
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$manifest.dependencies = @([pscustomobject]@{ name = 'hosihikari' })
$manifest | ConvertTo-Json -Depth 6 | Set-Content $manifestPath -Encoding UTF8

Write-Host '== deploying the shim =='
& (Join-Path $here 'deploy.bat') $ServerRoot | Out-Null

$exe = Join-Path $ServerRoot 'bedrock_server_mod.exe'
if (-not (Test-Path $exe)) { throw "no bedrock_server_mod.exe under $ServerRoot" }

# BDS reads commands from stdin. ping is the portable sleep here: timeout refuses
# redirected input, and this way each command lands after the previous one took effect.
$steps = @(
    @{ Wait = 30; Line = "ll show $PluginName" },
    @{ Wait = 5;  Line = "ll disable $PluginName" },
    @{ Wait = 5;  Line = "ll enable $PluginName" },
    @{ Wait = 5;  Line = "ll unload $PluginName" },
    @{ Wait = 6;  Line = "ll load $PluginName" },
    @{ Wait = 5;  Line = 'stop' }
)

$log = Join-Path $ServerRoot 'smoke.log'
$exitFile = Join-Path $ServerRoot 'smoke.exit'
$driver = Join-Path $env:TEMP 'hosihikari-smoke.cmd'
Remove-Item $log, $exitFile -Force -ErrorAction SilentlyContinue
$lines = New-Object System.Text.StringBuilder
[void]$lines.AppendLine('@echo off')
[void]$lines.AppendLine('(')
foreach ($step in $steps) {
    [void]$lines.AppendLine("  ping -n $($step.Wait) 127.0.0.1 >nul & echo $($step.Line)")
}
[void]$lines.AppendLine(") | `"$exe`" > `"$log`" 2>&1")
[void]$lines.AppendLine('set SERVER_EXIT=%ERRORLEVEL%')
[void]$lines.AppendLine("echo %SERVER_EXIT% > `"$exitFile`"")
[void]$lines.AppendLine('exit /b %SERVER_EXIT%')
Set-Content -Path $driver -Value $lines.ToString() -Encoding ASCII

Write-Host '== running the server through the lifecycle =='
$run = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', "`"$driver`"" `
    -WorkingDirectory $ServerRoot -WindowStyle Hidden -PassThru
$totalWait = ($steps | ForEach-Object { $_.Wait } | Measure-Object -Sum).Sum
Start-Sleep -Seconds ($totalWait + 15)
$timedOut = -not $run.HasExited
if ($timedOut) {
    & taskkill /PID $run.Id /T /F 2>&1 | Out-Null
    $run.WaitForExit(5000) | Out-Null
}
Start-Sleep -Seconds 2

$serverExit = $null
if (Test-Path $exitFile) {
    $parsed = 0
    if ([int]::TryParse((Get-Content $exitFile -Raw).Trim(), [ref]$parsed)) {
        $serverExit = $parsed
    }
}
$exitOk = -not $timedOut -and $serverExit -eq 0
Write-Host ("{0}  server exited cleanly  ({1})" -f $(if ($exitOk) { 'PASS' } else { 'FAIL' }), $(if ($timedOut) { 'timeout' } else { $serverExit }))

$text = Get-Content $log -Raw
$checks = @(
    @{ Name = 'plugin package loaded';    Expect = "[$PluginName] OnLoad" },
    @{ Name = 'show answers';             Expect = "Name: $PluginName" },
    @{ Name = 'disable reaches the hook'; Expect = "Disable mod $PluginName successfully" },
    @{ Name = 'enable reaches the hook';  Expect = "Enable mod $PluginName successfully" },
    @{ Name = 'unload reclaims it';       Expect = "Unload mod $PluginName successfully" },
    @{ Name = 'load brings it back';      Expect = "Load mod $PluginName successfully" }
)
foreach ($check in $checks) {
    $ok = $text.Contains($check.Expect)
    Write-Host ("{0}  {1}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $check.Name)
}

foreach ($bad in @('Unhandled exception', 'Segmentation fault')) {
    if ($text.Contains($bad)) {
        Write-Host "FAIL  the server died: $bad"
        exit 1
    }
}

if (-not $exitOk -or ($checks | Where-Object { -not $text.Contains($_.Expect) })) {
    Write-Host "see $log"
    exit 1
}

Write-Host "all checks passed; log at $log"
