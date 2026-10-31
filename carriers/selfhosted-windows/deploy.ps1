<#
    Copy the self-hosted Windows launcher and shim into a server root, and for
    source deployments the managed core as well.

    The core is taken from a sibling checkout of Hosihikari.PluginManager, or
    from HOSIHIKARI_PLUGINMANAGER. Release packaging passes -ShimOnly: the core
    then comes from its own release archive.

    Usage: pwsh ./deploy.ps1 -ServerRoot <server root> [-ShimOnly]
#>
param(
    [Parameter(Mandatory)][string]$ServerRoot,
    [switch]$ShimOnly
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$build = Join-Path $here 'build/Release'
$launcher = Join-Path $build 'hosihikari-server.exe'
$shim = Join-Path $build 'hosihikari.dll'

if (-not (Test-Path $launcher) -or -not (Test-Path $shim)) {
    throw "run build.bat first: $launcher or $shim is missing"
}

New-Item -ItemType Directory -Force $ServerRoot | Out-Null
$server = (Resolve-Path $ServerRoot).Path
$destination = Join-Path $server 'plugins/hosihikari'

if (Test-Path $destination) { Remove-Item $destination -Recurse -Force }
New-Item -ItemType Directory -Force $destination | Out-Null

Copy-Item $launcher $server -Force
Copy-Item $shim $destination -Force

if ($ShimOnly) {
    Write-Host "deployed shim-only to $destination"
    exit 0
}

$managedRoot = if ($env:HOSIHIKARI_PLUGINMANAGER) {
    $env:HOSIHIKARI_PLUGINMANAGER
} else {
    Join-Path $here '../../../PluginManager'
}
$managed = Join-Path $managedRoot 'src/bin/Release/net10.0'
if (-not (Test-Path (Join-Path $managed 'Hosihikari.PluginManager.dll'))) {
    throw "build Hosihikari.PluginManager in Release first: $managed"
}

Copy-Item (Join-Path $managed '*.dll') $destination -Force
Copy-Item (Join-Path $managed '*.json') $destination -Force
$pdbs = Get-ChildItem $managed -Filter '*.pdb' -ErrorAction SilentlyContinue
if ($pdbs) { $pdbs | Copy-Item -Destination $destination -Force }

Write-Host "deployed to $destination"
