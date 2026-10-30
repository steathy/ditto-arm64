# Exercises publication decisions with local fixtures; every network/CLI operation is mocked.
$ErrorActionPreference = 'Stop'
$metadata = & "$PSScriptRoot/Get-ReleaseMetadata.ps1"
try { & "$PSScriptRoot/Get-ReleaseMetadata.ps1" -Tag 'v01.2.3.4' | Out-Null; throw 'Wrong tag was accepted.' }
catch { if ($_.Exception.Message -notlike 'Tag must exactly match*') { throw } }
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root = Join-Path $repoRoot ('work/release-tests-' + [guid]::NewGuid().ToString('N'))
$assets = Join-Path $root 'assets'
New-Item -ItemType Directory -Path $assets -Force | Out-Null
$installer = "DittoSetup_$($metadata.version)_arm64.exe"
[IO.File]::WriteAllText((Join-Path $assets $installer), 'Publication fixture, never executable.')
$hash = (Get-FileHash (Join-Path $assets $installer)).Hash.ToLowerInvariant()
$entry = @{version=$metadata.version;publishedAt='2026-10-29T12:00:00Z';minimumWindowsBuild=22000;releaseNotes='Fixture';releaseNotesUrl="https://github.com/steathy/ditto-arm64/releases/tag/$($metadata.tag)";installer=@{url="https://github.com/steathy/ditto-arm64/releases/download/$($metadata.tag)/$installer";sha256=$hash;size=(Get-Item (Join-Path $assets $installer)).Length}}
$entry | ConvertTo-Json -Depth 5 | Set-Content "$assets/update-entry.json"
@{version=$metadata.version;tag=$metadata.tag;channel=$metadata.channel;sourceCommit=('a' * 40);signed=$false} | ConvertTo-Json | Set-Content "$assets/release-info.json"
foreach ($name in @('release-notes.md','build-info.json','installer-info.json','portable.zip','source.zip')) { [IO.File]::WriteAllText((Join-Path $assets $name), 'fixture') }
$checksums = Get-ChildItem $assets -File | ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName).Hash.ToLowerInvariant(), $_.Name }
[IO.File]::WriteAllLines("$assets/SHA256SUMS", [string[]]$checksums)
$environmentNames = @('GITHUB_ACTIONS','GITHUB_REPOSITORY','GITHUB_EVENT_NAME','GITHUB_REF','GITHUB_SHA','RUNNER_TEMP','GITHUB_STEP_SUMMARY','GH_TOKEN')
$saved = @{}
foreach ($name in $environmentNames) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
$global:DittoReleaseTestAssets = $assets
function gh {
    $command = @($args)
    $global:DittoReleaseTestState.commands += ($command -join ' ')
    if ($command[1] -eq 'download') {
        $directory = $command[[array]::IndexOf($command, '--dir') + 1]
        New-Item -ItemType Directory $directory | Out-Null
        Copy-Item "$global:DittoReleaseTestAssets/*" $directory
        if ($global:DittoReleaseTestState.corruptRemote) { Add-Content "$directory/release-notes.md" 'tampered' }
    }
    $global:LASTEXITCODE = 0
}
function Invoke-WebRequest {
    param($Uri, $OutFile)
    Copy-Item (Join-Path $global:DittoReleaseTestAssets $installer) $OutFile
    if ($global:DittoReleaseTestState.corruptPublic) { Add-Content $OutFile 'tampered' }
}
function Invoke-RestMethod {
    param($Uri, $Headers, $Method = 'Get', $ContentType, $Body)
    if ($Uri -like '*/releases/tags/*') { return [pscustomobject]@{draft=$global:DittoReleaseTestState.draft;prerelease=($metadata.channel -eq 'beta')} }
    if ($Uri -like '*/contents/updates/arm64.json*') {
        if ($Method -eq 'Put') {
            $request = [Text.Encoding]::UTF8.GetString($Body) | ConvertFrom-Json
            if ($request.sha -ne 'fixture-sha' -or $request.branch -ne 'main') { throw 'Missing optimistic feed update.' }
            $global:DittoReleaseTestState.feed = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($request.content)) | ConvertFrom-Json
            $global:DittoReleaseTestState.writes++
            return
        }
        $json = $global:DittoReleaseTestState.feed | ConvertTo-Json -Depth 8
        return [pscustomobject]@{sha='fixture-sha';content=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($json))}
    }
    throw "Unexpected mocked request: $Uri"
}
try {
    $env:GITHUB_ACTIONS='true'; $env:GITHUB_REPOSITORY='steathy/ditto-arm64'; $env:GITHUB_EVENT_NAME='push'
    $env:GITHUB_REF="refs/tags/$($metadata.tag)"; $env:GITHUB_SHA='a' * 40
    $env:RUNNER_TEMP=$root; $env:GITHUB_STEP_SUMMARY=Join-Path $root 'summary.md'; $env:GH_TOKEN='local-fixture'
    foreach ($scenario in @('draft','published','older','equal','different-hash','corrupt-remote','corrupt-public')) {
        $global:DittoReleaseTestState = @{commands=@();writes=0;draft=($scenario -eq 'draft');corruptRemote=($scenario -eq 'corrupt-remote');corruptPublic=($scenario -eq 'corrupt-public')}
        $global:DittoReleaseTestState.feed = '{"schemaVersion":2,"appId":"steathy.ditto-arm64","platform":"windows","architecture":"arm64","channels":{"stable":null,"beta":null}}' | ConvertFrom-Json
        $other = if ($metadata.channel -eq 'beta') { 'stable' } else { 'beta' }
        $global:DittoReleaseTestState.feed.channels.$other = [pscustomobject]@{version='0.0.0.1';marker='preserve-other-channel'}
        if ($scenario -in @('older','equal','different-hash')) {
            $current = ($entry | ConvertTo-Json -Depth 5) | ConvertFrom-Json
            if ($scenario -eq 'older') { $current.version='65535.65535.65535.65535' }
            if ($scenario -eq 'different-hash') { $current.installer.sha256='b' * 64 }
            $global:DittoReleaseTestState.feed.channels.($metadata.channel) = $current
        }
        $caught = $null
        try { & "$PSScriptRoot/Publish-GitHubRelease.ps1" -AssetPath $assets -Tag $metadata.tag | Out-Null }
        catch { $caught = $_.Exception.Message }
        $expectedFailure = $scenario -in @('different-hash','corrupt-remote','corrupt-public')
        if ([bool]$caught -ne $expectedFailure) { throw "Unexpected result in ${scenario}: $caught" }
        $expectedWrites = if ($scenario -in @('draft','published')) { 1 } else { 0 }
        if ($global:DittoReleaseTestState.writes -ne $expectedWrites) { throw "Wrong feed write count: $scenario" }
        if ($global:DittoReleaseTestState.feed.channels.$other.marker -ne 'preserve-other-channel') { throw 'Other channel was overwritten.' }
        if (!$global:DittoReleaseTestState.draft -and ($global:DittoReleaseTestState.commands -match '^release (upload|edit|create) ')) { throw 'Published release was mutated.' }
        if ($scenario -eq 'corrupt-remote' -and ($global:DittoReleaseTestState.commands -match '^release edit ')) { throw 'Corrupt download was published.' }
        Write-Output "PASS: publication scenario $scenario"
    }
} finally {
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
Write-Output 'PASS: release-tag validation and seven offline publication scenarios. No network calls or installers executed.'
