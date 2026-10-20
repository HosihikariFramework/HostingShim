<#
    A fake server that prints the expected lines but exits nonzero must make
    both Windows smoke scripts fail. This guards the "PASS despite exit 42"
    regression, including the timeout-kill path.
#>
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$tmp = Join-Path $env:TEMP ("hosihikari-smoke-negative-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $tmp | Out-Null

try {
    $endstoneServer = Join-Path $tmp 'endstone'
    $leviServer = Join-Path $tmp 'levilamina'
    New-Item -ItemType Directory -Force $endstoneServer, $leviServer | Out-Null
    New-Item -ItemType File -Force (Join-Path $endstoneServer 'bedrock_server.exe') | Out-Null

    $fake = @'
$ready = -join ([char]0x5DF2, [char]0x5C31, [char]0x7EEA)
$took = -join ([char]0x5DF2, [char]0x63A5, [char]0x7BA1)
Write-Output 'CLR BDS'
Write-Output ('Hosihikari: ' + $ready)
Write-Output $took
Write-Output 'Enabling hosihikari'
Write-Output 'Server stop requested'
exit 42
'@
    $fakePs1 = Join-Path $tmp 'fake-endstone.ps1'
    Set-Content -Path $fakePs1 -Value $fake -Encoding ASCII

    $wrapper = @'
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fake-endstone.ps1"
exit /b %ERRORLEVEL%
'@
    $fakeEndstone = Join-Path $tmp 'fake-endstone.cmd'
    Set-Content -Path $fakeEndstone -Value $wrapper -Encoding ASCII

    $fakeLevi = @'
$ready = -join ([char]0x5DF2, [char]0x5C31, [char]0x7EEA)
$took = -join ([char]0x5DF2, [char]0x63A5, [char]0x7BA1)
Write-Output 'hosihikari v'
Write-Output ('Hosihikari: ' + $ready)
Write-Output $took
Write-Output 'Server started'
Write-Output 'Server stop requested'
exit 42
'@
    $fakeLeviPs1 = Join-Path $tmp 'fake-levilamina.ps1'
    Set-Content -Path $fakeLeviPs1 -Value $fakeLevi -Encoding ASCII

    $leviWrapper = @'
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fake-levilamina.ps1"
exit /b %ERRORLEVEL%
'@
    $fakeLeviCmd = Join-Path $tmp 'fake-levilamina.cmd'
    Set-Content -Path $fakeLeviCmd -Value $leviWrapper -Encoding ASCII

    $env:HOSIHIKARI_SMOKE_DELAY = '1'
    $env:HOSIHIKARI_SMOKE_SETTLE = '1'
    $env:HOSIHIKARI_SMOKE_TIMEOUT = '10'

    $env:ENDSTONE_EXE = $fakeEndstone
    & pwsh -NoProfile -File (Join-Path $here 'endstone\smoke-live.ps1') -ServerRoot $endstoneServer
    if ($LASTEXITCODE -eq 0) {
        throw 'Endstone smoke accepted a fake server exit 42'
    }

    $env:HOSIHIKARI_SMOKE_EXE = $fakeLeviCmd
    & pwsh -NoProfile -File (Join-Path $here 'levilamina\smoke-live.ps1') -ServerRoot $leviServer
    if ($LASTEXITCODE -eq 0) {
        throw 'LeviLamina smoke accepted a fake server exit 42'
    }

    Write-Host 'PASS  Windows smoke scripts reject a nonzero server exit'
}
finally {
    Remove-Item Env:\HOSIHIKARI_SMOKE_DELAY, Env:\HOSIHIKARI_SMOKE_SETTLE, Env:\HOSIHIKARI_SMOKE_TIMEOUT -ErrorAction SilentlyContinue
    Remove-Item Env:\ENDSTONE_EXE, Env:\HOSIHIKARI_SMOKE_EXE -ErrorAction SilentlyContinue
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
