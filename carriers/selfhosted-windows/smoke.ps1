<#
    Live smoke test for the self-hosted Windows carrier.

    Usage: pwsh ./smoke.ps1 -ServerRoot <server root> [-DelaySeconds 60]

    Starts through the launcher, asks the server to stop, and checks that the
    CLR came up, the core scanned plugins/, and the server stopped cleanly.
#>
param(
    [Parameter(Mandatory)][string]$ServerRoot,
    [int]$DelaySeconds = 60
)

$ErrorActionPreference = 'Stop'
$server = (Resolve-Path $ServerRoot).Path
$launcher = Join-Path $server 'hosihikari-server.exe'
if (-not (Test-Path $launcher)) {
    throw "hosihikari-server.exe is missing in $server"
}

$running = Get-Process -Name 'bedrock_server' -ErrorAction SilentlyContinue
if ($running) {
    throw 'a server is already running; stop it first'
}

$log = Join-Path $server 'smoke-selfhosted.log'

$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $launcher
$startInfo.WorkingDirectory = $server
$startInfo.UseShellExecute = $false
$startInfo.RedirectStandardInput = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.StandardOutputEncoding = [System.Text.Encoding]::UTF8
$startInfo.StandardErrorEncoding = [System.Text.Encoding]::UTF8

$process = [System.Diagnostics.Process]::new()
$process.StartInfo = $startInfo
[void]$process.Start()

$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()

Start-Sleep -Seconds $DelaySeconds
$process.StandardInput.WriteLine('stop')
$process.StandardInput.Close()

if (-not $process.WaitForExit(180000)) {
    $process.Kill()
    throw 'the server did not stop within three minutes'
}

$text = $stdout.Result + $stderr.Result
Set-Content -Path $log -Value $text -Encoding utf8

function Fail([string]$message) {
    Write-Host "FAIL  $message"
    Write-Host "see $log"
    Get-Content $log -Tail 120
    exit 1
}

if ($text -notmatch '已加载') { Fail 'core did not scan plugins/' }
if ($text -notmatch 'Starting Server') { Fail 'server did not start' }
if ($text -match '\[hosihikari\] 失败') { Fail 'the launcher or shim reported a failure' }
if ($text -match 'Unhandled exception') { Fail 'the server died' }
if ($text -notmatch 'Quit correctly') { Fail 'server did not stop cleanly' }

Write-Host "PASS  self-hosted Windows live smoke; log at $log"
exit 0
