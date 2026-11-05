<#
    Live smoke test for the self-hosted Windows carrier.

    Usage: pwsh ./smoke.ps1 -ServerRoot <server root> [-DelaySeconds 60]

    Starts through the launcher, asks the server to stop, and checks that the
    CLR came up, the core scanned plugins/, and the server stopped cleanly.
    ASCII only, like the carrier scripts: Windows PowerShell 5.1 reads a BOM-less
    script in the system codepage, and non-ASCII bytes corrupt the lines after them.
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

# Phrases are spelled in code points: the file is ASCII, so powershell 5.1 reads it in any codepage.
$loadedZh = -join ([char]0x5DF2, [char]0x52A0, [char]0x8F7D)
$loadedFr = -join ([char]0x0043, [char]0x0068, [char]0x0061, [char]0x0072, [char]0x0067, [char]0x00E9)
$loadedRu = -join ([char]0x0417, [char]0x0430, [char]0x0433, [char]0x0440, [char]0x0443, [char]0x0436, [char]0x0435, [char]0x043D, [char]0x043E)
$loadedAr = -join ([char]0x062A, [char]0x0645, [char]0x0020, [char]0x062A, [char]0x062D, [char]0x0645, [char]0x064A, [char]0x0644)
$failedZh = -join ([char]0x5931, [char]0x8D25)
if ($text -notmatch ($loadedZh + '|Loaded \d+ plugin|' + $loadedFr + ' \d+|' + $loadedRu + '|Cargados \d+|' + $loadedAr)) { Fail 'core did not scan plugins/' }
if ($text -notmatch 'Starting Server') { Fail 'server did not start' }
if ($text -match ('\[hosihikari\] ' + $failedZh)) { Fail 'the launcher or shim reported a failure' }
if ($text -match 'Unhandled exception') { Fail 'the server died' }
if ($text -notmatch 'Quit correctly') { Fail 'server did not stop cleanly' }

Write-Host "PASS  self-hosted Windows live smoke; log at $log"
exit 0
