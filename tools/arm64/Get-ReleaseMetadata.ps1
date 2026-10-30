param([string]$Tag)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$header = Get-Content -LiteralPath (Join-Path $repoRoot 'src/Arm64BuildInfo.h') -Raw
$match = [regex]::Match($header, '(?m)^#define DITTO_ARM64_VERSION_STRING "([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)"\s*$')
if (!$match.Success) { throw 'Missing four-part ARM64 version.' }
$version = $match.Groups[1].Value
foreach ($part in $version.Split('.')) {
    if ($part -notmatch '^(0|[1-9][0-9]{0,4})$' -or [int]$part -gt 65535) { throw 'Invalid ARM64 version component.' }
}
$numbers = [regex]::Match($header, '(?m)^#define DITTO_ARM64_VERSION_NUMBERS ([0-9,]+)\s*$').Groups[1].Value
if ($numbers.Replace(',', '.') -ne $version) { throw 'String and numeric versions disagree.' }
$nightly = [regex]::Match($header, '(?m)^#define DITTO_ARM64_NIGHTLY_BUILD ([01])\s*$')
if (!$nightly.Success) { throw 'Missing explicit nightly/stable build flag.' }
$channel = if ($nightly.Groups[1].Value -eq '1') { 'beta' } else { 'stable' }
if ($Tag -and $Tag -cne "v$version") { throw "Tag must exactly match v$version." }
$notesPath = Join-Path $repoRoot "docs/releases/$version.md"
if ($Tag -and !(Test-Path -LiteralPath $notesPath)) { throw 'Add release notes before pushing a version tag.' }
[pscustomobject]@{version=$version;tag="v$version";channel=$channel;notesPath=$notesPath}
