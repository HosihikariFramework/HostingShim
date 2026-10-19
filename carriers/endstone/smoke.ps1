<#
    Live smoke test for the Endstone carrier and the out-of-process provider.

    Deploys the current shim + core, stages the provider package and two Node
    plugins (Hello, Crash), drives the server console through list / disable /
    enable / stop, and checks the log and the files the plugins wrote.

    Local only: needs an Endstone server directory and Node on PATH.
    ASCII only, like the carrier scripts: Windows PowerShell 5.1 reads a
    BOM-less script in the system codepage.
#>
param(
    [Parameter(Mandatory = $true)][string]$ServerRoot
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$framework = Split-Path (Split-Path (Split-Path $here -Parent) -Parent) -Parent
$core = Join-Path $framework 'Hosihikari.PluginManager'
$rpc = Join-Path $framework 'Hosihikari.PluginProvider.Rpc'
if (-not (Test-Path $rpc)) { $rpc = Join-Path $framework 'RpcProvider' }

# Endstone is started through its CLI: it injects the runtime DLL into the
# vanilla bedrock_server.exe. Running the exe directly gives a plain server.
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

# The CLI replaces the child's PATH with Python's sys.path, so the plugin
# command must name node by absolute path.
$node = (Get-Command node -ErrorAction Stop).Source

$running = Get-Process -Name 'bedrock_server', 'bedrock_server_mod' -ErrorAction SilentlyContinue
if ($running) {
    throw "a server is already running (PID $($running.Id -join ', ')); stop it first"
}

Write-Host '== build core and provider =='
& dotnet build (Join-Path $core 'src\Hosihikari.PluginManager.csproj') -c Release --nologo | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'the managed core failed to build' }
& dotnet build (Join-Path $rpc 'src\Hosihikari.PluginProvider.Rpc.csproj') -c Release --nologo | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'the rpc provider failed to build' }

Write-Host '== deploy the shim =='
& (Join-Path $here 'deploy.bat') $ServerRoot | Out-Null

function Write-Manifest([string]$Path, [string]$Name, [string]$Entry, [string]$Provider, [object]$Command, [object]$Dependencies) {
    $extra = [ordered]@{ api = '2'; format_version = '1'; provider = $Provider }
    if ($null -ne $Command) { $extra.command = $Command }
    $manifest = [ordered]@{
        name         = $Name
        entry        = $Entry
        type         = 'hosihikari'
        version      = '1.0.0'
        dependencies = $Dependencies
        extraInfo    = $extra
    }
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -Path $Path -Encoding UTF8
}

Write-Host '== stage the provider package =='
$rpcPackage = Join-Path $ServerRoot 'plugins\hosihikari-rpc'
if (Test-Path $rpcPackage) { Remove-Item $rpcPackage -Recurse -Force }
New-Item -ItemType Directory -Path $rpcPackage | Out-Null
$rpcOut = Join-Path $rpc 'src\bin\Release\net10.0'
Copy-Item (Join-Path $rpcOut '*.dll') $rpcPackage
Remove-Item (Join-Path $rpcPackage 'Hosihikari.Abstractions.dll') -ErrorAction SilentlyContinue
Copy-Item (Join-Path $rpcOut 'Hosihikari.PluginProvider.Rpc.deps.json') $rpcPackage -ErrorAction SilentlyContinue
Write-Manifest (Join-Path $rpcPackage 'manifest.json') 'hosihikari-rpc' 'Hosihikari.PluginProvider.Rpc.dll' 'assembly' $null @(@{ name = 'hosihikari' })

Write-Host '== stage the Node plugins =='
function Stage-RpcPlugin([string]$Name, [string]$Entry) {
    $package = Join-Path $ServerRoot "plugins\$Name"
    if (Test-Path $package) { Remove-Item $package -Recurse -Force }
    New-Item -ItemType Directory -Path $package | Out-Null
    Copy-Item (Join-Path $rpc 'bindings\node\hosihikari.mjs') $package
    Copy-Item (Join-Path $rpc "bindings\node\$Entry") $package
    $command = ConvertTo-Json -Compress -InputObject @($node, './hosihikari.mjs', "./$Entry")
    Write-Manifest (Join-Path $package 'manifest.json') $Name $Entry 'rpc' `
        $command `
        @(@{ name = 'hosihikari' }, @{ name = 'hosihikari-rpc' })
}
Stage-RpcPlugin 'Hello' 'hello.mjs'
Stage-RpcPlugin 'Crash' 'crash.mjs'
Set-Content -Path (Join-Path $ServerRoot 'plugins\Hello\Hello.conf') -Value 'greeting = "package"' -Encoding ASCII

$configDir = Join-Path $ServerRoot 'config\hosihikari'
New-Item -ItemType Directory -Force $configDir | Out-Null
Set-Content -Path (Join-Path $configDir 'Hello.conf') -Value 'greeting = "overridden"' -Encoding ASCII

foreach ($name in @('Hello', 'Crash')) {
    $data = Join-Path $ServerRoot "data\hosihikari\$name"
    if (Test-Path $data) { Remove-Item $data -Recurse -Force }
}

$serverExe = Join-Path $ServerRoot 'bedrock_server.exe'
if (-not (Test-Path $serverExe)) { throw "no bedrock_server.exe under $ServerRoot" }

$steps = @(
    @{ Wait = 30; Line = 'plugins' },
    @{ Wait = 5;  Line = 'endstone:disable hello' },
    @{ Wait = 5;  Line = 'endstone:enable hello' },
    @{ Wait = 3;  Line = 'plugins' },
    @{ Wait = 5;  Line = 'stop' }
)

$log = Join-Path $ServerRoot 'smoke-endstone.log'
$driver = Join-Path $env:TEMP 'hosihikari-endstone-smoke.cmd'
$lines = New-Object System.Text.StringBuilder
[void]$lines.AppendLine('@echo off')
[void]$lines.AppendLine('(')
foreach ($step in $steps) {
    [void]$lines.AppendLine("  ping -n $($step.Wait) 127.0.0.1 >nul & echo $($step.Line)")
}
[void]$lines.AppendLine(") | `"$endstone`" -s `"$ServerRoot`" -y > `"$log`" 2>&1")
Set-Content -Path $driver -Value $lines.ToString() -Encoding ASCII

Write-Host '== running the server through the lifecycle =='
$run = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', "`"$driver`"" `
    -WorkingDirectory $ServerRoot -WindowStyle Hidden -PassThru
$totalWait = ($steps | ForEach-Object { $_.Wait } | Measure-Object -Sum).Sum
Start-Sleep -Seconds ($totalWait + 15)
if (-not $run.HasExited) {
    & taskkill /PID $run.Id /T /F 2>&1 | Out-Null
}
Start-Sleep -Seconds 2

$text = Get-Content $log -Raw -Encoding UTF8
$notSupported = -join ([char]0x4E0D, [char]0x652F, [char]0x6301)  # "不支持"
$helloData = Join-Path $ServerRoot 'data\hosihikari\Hello'
$checks = @(
    @{ Name = 'carrier loaded';                 Expect = 'Enabling hosihikari' },
    @{ Name = 'plugin log reaches the parent';  Expect = 'hello loaded' },
    @{ Name = 'crash fault reported';           Expect = 'Crash:' },
    @{ Name = 'removal warned for Endstone';    Expect = ("Endstone " + $notSupported) }
)
foreach ($check in $checks) {
    $ok = $text.Contains($check.Expect)
    Write-Host ("{0}  {1}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $check.Name)
}

function Check-File([string]$Name, [string]$Expected) {
    $path = Join-Path $helloData $Name
    $actual = if (Test-Path $path) { (Get-Content $path -Raw).Trim() } else { '(missing)' }
    $ok = $actual -eq $Expected
    Write-Host ("{0}  {1}  ({2})" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $Name, $actual)
    return $ok
}

$fileChecks = @(
    Check-File 'body.txt' 'body:overridden'
    Check-File 'enabled.txt' 'enabled'
    Check-File 'disabled.txt' 'disabled'
    Check-File 'self.txt' 'self:"hello-publish"'
    Check-File 'syncself.txt' 'syncself:"sync-publish"'
)

foreach ($bad in @('Unhandled exception', 'Segmentation fault')) {
    if ($text.Contains($bad)) {
        Write-Host "FAIL  the server died: $bad"
        exit 1
    }
}

$logOk = -not ($checks | Where-Object { -not $text.Contains($_.Expect) })
if (-not $logOk -or $fileChecks -contains $false) {
    Write-Host "see $log"
    exit 1
}

Write-Host "all checks passed; log at $log"
