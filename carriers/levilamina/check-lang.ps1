# Check that the carrier's six catalogs stay in step: same keys and placeholders.
# Run with pwsh (PowerShell 7); ConvertFrom-Json -AsHashtable is not in 5.1.
$ErrorActionPreference = 'Stop'
$dir = Join-Path $PSScriptRoot 'lang'

$catalogs = [ordered]@{}
foreach ($file in Get-ChildItem $dir -Filter *.json | Sort-Object Name) {
    $catalogs[$file.Name] = Get-Content $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json -AsHashtable
}

$reference = $catalogs['en.json']
if (-not $reference) { throw 'en.json is missing' }
$referenceKeys = @($reference.Keys | Sort-Object)

foreach ($name in $catalogs.Keys) {
    $keys = @($catalogs[$name].Keys | Sort-Object)
    if (Compare-Object $referenceKeys $keys) { throw "$name has different keys" }
    foreach ($key in $keys) {
        $expected = @([regex]::Matches($key, '\{[^}]+\}') | ForEach-Object { $_.Value } | Sort-Object)
        $actual = @([regex]::Matches($catalogs[$name][$key], '\{[^}]+\}') | ForEach-Object { $_.Value } | Sort-Object)
        if (Compare-Object $expected $actual) { throw "$name has wrong placeholders for: $key" }
    }
}

Write-Host "language catalogs OK: $($catalogs.Count) files"
