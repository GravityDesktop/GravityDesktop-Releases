param(
    [Parameter(Mandatory=$true)]
    [ValidatePattern('^[A-Za-z0-9-]+$')]
    [string]$BuildNumber,

    [ValidateSet('LOCALTEST', 'STORE', 'DAILY TEST')]
    [string]$Channel = 'LOCALTEST'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$folder = Join-Path $root "out\validation\$BuildNumber"
if (Test-Path -LiteralPath $folder) { throw 'Build identity already exists; use a new build number.' }
New-Item -ItemType Directory -Path $folder | Out-Null
$commit = git -C $root rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source commit' }
$patch = Join-Path $folder 'source.patch'
git -C $root diff --binary HEAD "--output=$patch"
if ($LASTEXITCODE -ne 0) { throw 'Cannot preserve source diff' }
$untracked = @(git -C $root ls-files --others --exclude-standard)
foreach ($file in $untracked) {
    $destination = Join-Path (Join-Path $folder 'untracked-source') $file
    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $destination
}
$sourceFiles = @(git -C $root ls-files) + $untracked
$hashes = foreach ($file in ($sourceFiles | Sort-Object -Unique)) {
    $path = Join-Path $root $file
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        [pscustomobject]@{Path=$file;SHA256=(Get-FileHash -LiteralPath $path).Hash}
    }
}
$hashList = Join-Path $folder 'source-files.csv'
$hashes | Export-Csv -LiteralPath $hashList -NoTypeInformation -Encoding UTF8
$diffIdentity = (Get-FileHash -LiteralPath $hashList).Hash
$status = if ((git -C $root status --porcelain)) {'DIRTY'} else {'CLEAN'}
$utc = [DateTime]::UtcNow.ToString('o')
$include = Join-Path $root 'out\build-identity'
New-Item -ItemType Directory -Path $include -Force | Out-Null
$header = @"
#pragma once
#define GRAVITY_BUILD_ID "$BuildNumber"
#define GRAVITY_BUILD_COMMIT "$commit"
#define GRAVITY_BUILD_DIFF "$diffIdentity"
#define GRAVITY_BUILD_SOURCE "$status"
#define GRAVITY_BUILD_UTC "$utc"
#define GRAVITY_BUILD_CHANNEL "$Channel"
"@
[IO.File]::WriteAllText((Join-Path $include 'GravityDesktopBuildIdentity.generated.h'), $header, [Text.UTF8Encoding]::new($false))
$header | Set-Content -LiteralPath (Join-Path $folder 'build-identity.txt') -Encoding UTF8
Write-Output "Build ${BuildNumber}: $commit $status channel=$Channel source=$diffIdentity"
