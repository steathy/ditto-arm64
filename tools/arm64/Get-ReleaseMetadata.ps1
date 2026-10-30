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
$source = Get-Content (Join-Path $repoRoot 'build/source-info.json') -Raw | ConvertFrom-Json
if ($source.upstreamCommit -notmatch '^[0-9a-f]{40}$' -or
    $source.upstreamVersion -notmatch '^\d+\.\d+\.\d+\.\d+$' -or
    $source.releaseBranch -notmatch '^[A-Za-z0-9][A-Za-z0-9._/-]*$') { throw 'Missing source provenance or release branch.' }
if (($version.Split('.')[0..2] -join '.') -ne ($source.upstreamVersion.Split('.')[0..2] -join '.')) { throw 'ARM64 version does not match its upstream baseline.' }
if ($channel -eq 'stable' -and $source.upstreamTag -ne $source.upstreamVersion) { throw 'Stable builds require an upstream release tag.' }
if ($Tag -and $Tag -cne "v$version") { throw "Tag must exactly match v$version." }
$notesPath = Join-Path $repoRoot "docs/releases/$version.md"
if ($Tag -and !(Test-Path -LiteralPath $notesPath)) { throw 'Add release notes before pushing a version tag.' }
[pscustomobject]@{version=$version;tag="v$version";channel=$channel;notesPath=$notesPath;releaseBranch=$source.releaseBranch;upstreamCommit=$source.upstreamCommit;upstreamVersion=$source.upstreamVersion;upstreamTag=$source.upstreamTag}
