param(
    [ValidateSet('Release')]
    [string]$Configuration = 'Release',
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
    [string]$PackageVersion = '0.1.0.0',
    [string]$PackageName = 'GravityDesktop-MSIX-Prototype-0.1.0-alpha.msix',
    [switch]$PackagedBuild
)

$ErrorActionPreference = 'Stop'

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$releaseFolder = if ($PackagedBuild) { "$Configuration`MSIX" } else { $Configuration }
$releaseRoot = Join-Path $projectRoot "out\$releaseFolder"
$outputRoot = Join-Path $projectRoot 'out\msix-prototype'
$stagingRoot = Join-Path $outputRoot 'staging'
$packagePath = Join-Path $outputRoot $PackageName
$makeAppx = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\makeappx.exe'

if (-not (Test-Path $makeAppx)) {
    throw "Windows SDK MakeAppx was not found: $makeAppx"
}

@('GravityDesktop.exe', 'GravityDesktopRecovery.exe') | ForEach-Object {
    if (-not (Test-Path (Join-Path $releaseRoot $_))) {
        throw "Build the Release x64 binaries before packaging. Missing: $($_)"
    }
}

if (Test-Path $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path (Join-Path $stagingRoot 'Assets'), (Join-Path $stagingRoot 'locales') | Out-Null
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$manifestText = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Package.appxmanifest') -Raw -Encoding utf8
$manifestText = $manifestText -replace 'Version="0\.1\.0\.0"', "Version=`"$PackageVersion`""
[System.IO.File]::WriteAllText((Join-Path $stagingRoot 'AppxManifest.xml'), $manifestText, [System.Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $releaseRoot 'GravityDesktop.exe') -Destination $stagingRoot
Copy-Item -LiteralPath (Join-Path $releaseRoot 'GravityDesktopRecovery.exe') -Destination $stagingRoot
Copy-Item -LiteralPath (Join-Path $releaseRoot 'GravityDesktopLibrary.ico') -Destination $stagingRoot -ErrorAction SilentlyContinue
Copy-Item -Path (Join-Path $PSScriptRoot 'Assets\*') -Destination (Join-Path $stagingRoot 'Assets')

$localeCodes = 'zh-CN', 'en-US', 'ja-JP', 'de-DE', 'es-ES', 'fr-FR'
foreach ($localeCode in $localeCodes) {
    Copy-Item -LiteralPath (Join-Path $releaseRoot "locales\$localeCode.json") -Destination (Join-Path $stagingRoot "locales\$localeCode.json")
}

if (Test-Path $packagePath) {
    Remove-Item -LiteralPath $packagePath -Force
}
& $makeAppx pack /d $stagingRoot /p $packagePath /o
if ($LASTEXITCODE -ne 0) {
    throw "MakeAppx failed with exit code $LASTEXITCODE"
}

Write-Host "MSIX prototype: $packagePath"
