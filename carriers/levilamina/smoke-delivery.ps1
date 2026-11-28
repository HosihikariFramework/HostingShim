<#
    Event-delivery smoke test for the LeviLamina carrier.

    Brings up a prepared server, loads a probe plugin that subscribes to the
    player events, connects a client so the carrier raises them, and checks that
    the plugin's handlers ran with a usable view.

    This covers what smoke-live.ps1 cannot: that script only proves registration
    and shutdown. Here the whole native path runs - the carrier's listener, the
    director, the view, the bus - and the assertions read the view's own members,
    so an empty view fails.

    Prerequisites, all on the operator's side:
      - a server root with LeviLamina and the carrier already deployed;
      - `online-mode=false` and `allow-list=false` in server.properties, because
        the client connects offline;
      - the bedrock-protocol module reachable through NODE_PATH.

    Usage: powershell -File smoke-delivery.ps1 -ServerRoot <server root>

    ASCII only: Windows PowerShell 5.1 reads a BOM-less script in the system
    codepage. The expected Chinese log lines are built from [char] codes.
#>
param(
    [Parameter(Mandatory = $true)][string]$ServerRoot,
    [string]$BotName = 'SmokeBot'
)

$ErrorActionPreference = 'Stop'
$server = (Resolve-Path $ServerRoot).Path
$here = $PSScriptRoot
$probe = Join-Path $here 'delivery-probe'

if (Get-Process -Name 'bedrock_server', 'bedrock_server_mod' -ErrorAction SilentlyContinue) {
    throw 'a server is already running; stop it first'
}

$exe = if ($env:HOSIHIKARI_SMOKE_EXE) { $env:HOSIHIKARI_SMOKE_EXE } else { Join-Path $server 'bedrock_server_mod.exe' }
if (-not (Test-Path $exe)) { throw "no bedrock_server_mod.exe under $server" }

# The client has to get in without an Xbox identity, and nothing may filter it.
$properties = Get-Content (Join-Path $server 'server.properties') -Raw
foreach ($setting in 'online-mode=false', 'allow-list=false') {
    if (-not $properties.Contains($setting)) { throw "server.properties needs $setting for the delivery smoke" }
}

Write-Host '== building the probe =='
& dotnet build (Join-Path $probe 'Probe.csproj') -c Release -v q --nologo
if ($LASTEXITCODE -ne 0) { throw 'building the probe failed' }

# mods/, not plugins/: this server generation scans ./mods (PreLoaderConfig.json).
$package = Join-Path $server "mods\Probe"
if (Test-Path $package) { Remove-Item $package -Recurse -Force }
New-Item -ItemType Directory -Path $package | Out-Null
Copy-Item (Join-Path $probe 'bin\Release\net10.0\Probe.dll') $package
Copy-Item (Join-Path $probe 'manifest.json') $package
Copy-Item (Join-Path $probe 'hosihikari.json') $package

$dataDirectory = Join-Path $server 'data\hosihikari\Probe'
$delivered = Join-Path $dataDirectory 'delivered.txt'
if (Test-Path $delivered) { Remove-Item $delivered -Force }

$log = Join-Path $server 'smoke-delivery.log'
$exitFile = Join-Path $server 'smoke-delivery.exit'
Remove-Item $log, $exitFile -Force -ErrorAction SilentlyContinue

# The server reads commands from stdin; `ping` is the portable sleep. The bot is
# a separate process, so this only has to outlast it: the driver sends `stop`
# once the client has had its run.
$bootSeconds = if ($env:HOSIHIKARI_SMOKE_DELAY) { [Math]::Max(1, [int]$env:HOSIHIKARI_SMOKE_DELAY) } else { 45 }
$runSeconds = if ($env:HOSIHIKARI_SMOKE_WATCH) { [Math]::Max(30, [int]$env:HOSIHIKARI_SMOKE_WATCH) } else { 75 }
$driver = Join-Path $env:TEMP 'hosihikari-levilamina-delivery.cmd'
$lines = @(
    '@echo off'
    '('
    '  ping -n ' + $bootSeconds + ' 127.0.0.1 >nul'
    '  ping -n ' + $runSeconds + ' 127.0.0.1 >nul & echo stop'
    ') | "' + $exe + '" > "' + $log + '" 2>&1'
    'set SERVER_EXIT=%ERRORLEVEL%'
    'echo %SERVER_EXIT% > "' + $exitFile + '"'
    'exit /b %SERVER_EXIT%'
)
Set-Content -Path $driver -Value $lines -Encoding ASCII

Write-Host '== starting the server =='
$run = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', "`"$driver`"" `
    -WorkingDirectory $server -WindowStyle Hidden -PassThru

# Wait for the probe to report the gate open, then bring the client in.
# Subscriptions made in OnLoad are dropped until OnEnable opens the scope, so
# connecting before ENABLED would raise the events into a closed gate.
$probeDeadline = (Get-Date).AddSeconds($bootSeconds + 60)
while ((Get-Date) -lt $probeDeadline) {
    if ((Test-Path $delivered) -and (Get-Content $delivered -Raw) -match 'ENABLED') { break }
    if ($run.HasExited) { throw 'the server exited before the probe was enabled' }
    Start-Sleep -Milliseconds 500
}
if (-not ((Test-Path $delivered) -and (Get-Content $delivered -Raw) -match 'ENABLED')) {
    throw 'the probe never reported ENABLED'
}
Write-Host '== probe enabled; connecting the client =='

$env:BDS_PORT = if ($properties -match 'server-port=(\d+)') { $Matches[1] } else { '19182' }
$env:BDS_BOT_NAME = $BotName
if (-not $env:HOSIHIKARI_BOT_MODULES) {
    throw 'set HOSIHIKARI_BOT_MODULES to the directory holding node_modules with bedrock-protocol'
}
$env:HOSIHIKARI_BOT_MODULES = (Resolve-Path $env:HOSIHIKARI_BOT_MODULES).Path
$botOut = Join-Path $server 'smoke-delivery.bot.log'
$botExit = Join-Path $server 'smoke-delivery.bot.exit'
Remove-Item $botOut, $botExit -Force -ErrorAction SilentlyContinue
# cmd writes the exit code itself: Start-Process.ExitCode on a redirected node
# process is unreliable with the ESM loader.
$botDriver = Join-Path $env:TEMP 'hosihikari-delivery-bot.cmd'
Set-Content -Path $botDriver -Encoding ASCII -Value @(
    '@echo off'
    '"' + (Get-Command node).Source + '" "' + (Join-Path $probe 'bot.mjs') + '" > "' + $botOut + '" 2>&1'
    'echo %ERRORLEVEL% > "' + $botExit + '"'
)
$bot = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', "`"$botDriver`"" `
    -WorkingDirectory $probe -WindowStyle Hidden -PassThru
if (-not $bot.WaitForExit(90000)) {
    & taskkill /PID $bot.Id /T /F 2>&1 | Out-Null
    throw 'the client did not finish in time'
}
$code = if (Test-Path $botExit) { [int](Get-Content $botExit -Raw).Trim() } else { -1 }
if ($code -ne 0) { throw "the client failed ($code); see $botOut" }

# Stop the server and wait for it. The driver sends `stop` on its own schedule,
# so this only has to outlast that plus the shutdown.
$deadline = (Get-Date).AddSeconds($runSeconds + 90)
while (-not $run.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
$timedOut = -not $run.HasExited
if ($timedOut) {
    & taskkill /PID $run.Id /T /F 2>&1 | Out-Null
    $run.WaitForExit(5000) | Out-Null
}

$text = if (Test-Path $delivered) { Get-Content $delivered -Raw } else { '' }
$chat = if ($env:HOSIHIKARI_SMOKE_CHAT) { $env:HOSIHIKARI_SMOKE_CHAT } else { 'hello-from-probe' }

# The driver records the exit code; without reading it a crashed server could still
# pass on the plugin's output alone.
$serverExit = $null
if (Test-Path $exitFile) {
    $parsed = 0
    if ([int]::TryParse((Get-Content $exitFile -Raw).Trim(), [ref]$parsed)) { $serverExit = $parsed }
}

# Name must equal the client's login name: an empty view or a garbage value both
# fail here. Xuid is empty under offline mode (no Xbox identity), so it is only
# required to be readable - pinned to empty so a future non-empty shows up as a
# change rather than a silent pass.
$checks = @(
    @{ Name = 'probe was enabled'; Ok = ($text -match 'ENABLED') },
    @{ Name = 'join reached the plugin'; Ok = ($text -match "JOIN name=\[$BotName\]") },
    @{ Name = 'the view carries the real player'; Ok = ($text -match 'native=\[ServerPlayer\]') },
    @{ Name = 'xuid is readable (empty offline)'; Ok = ($text -match 'xuid=\[\]') },
    @{ Name = 'chat reached the plugin'; Ok = ($text -match "CHAT name=\[$BotName\] msg=\[$chat\]") },
    @{ Name = 'leave reached the plugin'; Ok = ($text -match "LEAVE name=\[$BotName\]") },
    @{ Name = 'server stopped'; Ok = (-not $timedOut -and $serverExit -eq 0) }
)
$failed = $false
foreach ($check in $checks) {
    Write-Host ("{0}  {1}" -f $(if ($check.Ok) { 'PASS' } else { 'FAIL' }), $check.Name)
    if (-not $check.Ok) { $failed = $true }
}
foreach ($bad in @('Unhandled exception', 'Segmentation fault')) {
    if ($text -match $bad -or ((Test-Path $log) -and (Get-Content $log -Raw) -match $bad)) {
        Write-Host "FAIL  the server died: $bad"
        $failed = $true
    }
}
if ($failed) {
    Write-Host "see $delivered and $log"
    exit 1
}

Write-Host "PASS  LeviLamina event delivery smoke; probe log at $delivered"
