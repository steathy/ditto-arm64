# Exercises publication decisions with local fixtures; every network/CLI operation is mocked.
$ErrorActionPreference = 'Stop'
$metadata = & "$PSScriptRoot/Get-ReleaseMetadata.ps1"
try { & "$PSScriptRoot/Get-ReleaseMetadata.ps1" -Tag 'v01.2.3.4' | Out-Null; throw 'Wrong tag was accepted.' }
catch { if ($_.Exception.Message -notlike 'Tag must exactly match*') { throw } }
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root = Join-Path $repoRoot ('work/release-tests-' + [guid]::NewGuid().ToString('N'))
$assets = Join-Path $root 'assets'
New-Item -ItemType Directory -Path $assets -Force | Out-Null
. "$PSScriptRoot/ReleaseText.ps1"
$encodingFixture = Join-Path $root 'encoding.txt'
foreach ($invalid in @([byte[]]@(0x97), [Text.Encoding]::UTF8.GetBytes([string][char]0xfffd))) {
    [IO.File]::WriteAllBytes($encodingFixture, $invalid)
    $rejected = $false
    try { $null = Read-ReleaseText $encodingFixture } catch { $rejected = $true }
    if (!$rejected) { throw 'Release text accepted malformed UTF-8 or a replacement character.' }
}
$unicodeFixture = 'Release ' + [char]0x2014 + ' ' + [char]0xe9
[IO.File]::WriteAllText($encodingFixture, $unicodeFixture, (New-Object Text.UTF8Encoding($false)))
if ((Read-ReleaseText $encodingFixture) -cne $unicodeFixture) { throw 'Valid UTF-8 did not round trip.' }
if ((ConvertTo-UpdateNotes '# **Title** [notes](https://example.test/release)') -cne 'Title notes (https://example.test/release)') { throw 'Updater notes still contain Markdown formatting.' }
Write-Output 'PASS: strict UTF-8, replacement-character rejection and plain-text updater notes.'
$privateMetadata = Join-Path $assets 'metadata'
New-Item -ItemType Directory -Path $privateMetadata | Out-Null
$installer = "DittoSetup_$($metadata.version)_arm64.exe"
[IO.File]::WriteAllText((Join-Path $assets $installer), 'Publication fixture, never executable.')
$hash = (Get-FileHash (Join-Path $assets $installer)).Hash.ToLowerInvariant()
$entry = @{version=$metadata.version;publishedAt='2026-10-29T12:00:00Z';minimumWindowsBuild=22000;releaseNotes='Fixture';releaseNotesUrl="https://github.com/steathy/ditto-arm64/releases/tag/$($metadata.tag)";installer=@{url="https://github.com/steathy/ditto-arm64/releases/download/$($metadata.tag)/$installer";sha256=$hash;size=(Get-Item (Join-Path $assets $installer)).Length}}
$entry | ConvertTo-Json -Depth 5 | Set-Content "$privateMetadata/update-entry.json"
@{version=$metadata.version;tag=$metadata.tag;channel=$metadata.channel;sourceCommit=('a' * 40);upstreamCommit=$metadata.upstreamCommit;releaseBranch=$metadata.releaseBranch;signed=$false} | ConvertTo-Json | Set-Content "$privateMetadata/release-info.json"
foreach ($name in @("DittoPortable_$($metadata.version)_arm64.zip","DittoSource_$($metadata.version).zip")) { [IO.File]::WriteAllText((Join-Path $assets $name), 'fixture') }
foreach ($name in @('release-body.md','build-info.json','installer-info.json')) { [IO.File]::WriteAllText((Join-Path $privateMetadata $name), 'internal fixture') }
$checksums = Get-ChildItem $assets -File | ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName).Hash.ToLowerInvariant(), $_.Name }
[IO.File]::WriteAllLines("$privateMetadata/SHA256SUMS", [string[]]$checksums)
$environmentNames = @('GITHUB_ACTIONS','GITHUB_REPOSITORY','GITHUB_EVENT_NAME','GITHUB_REF','GITHUB_SHA','RUNNER_TEMP','GITHUB_STEP_SUMMARY','GH_TOKEN')
$saved = @{}
foreach ($name in $environmentNames) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
$global:DittoReleaseTestAssets = $assets
function gh {
    $command = @($args)
    $global:DittoReleaseTestState.commands += ($command -join ' ')
    if ($command[1] -in @('create','upload')) {
        $uploadFiles = @($command | Where-Object { [IO.File]::Exists($_) -and $_ -notlike '*release-body.md' })
        if ($uploadFiles.Count -ne 3 -or @($uploadFiles | Where-Object { $_ -match 'metadata' }).Count) { throw 'Internal metadata would be publicly uploaded.' }
    }
    if ($command[1] -eq 'download') {
        $directory = $command[[array]::IndexOf($command, '--dir') + 1]
        New-Item -ItemType Directory $directory | Out-Null
        Get-ChildItem $global:DittoReleaseTestAssets -File | Copy-Item -Destination $directory
        if ($global:DittoReleaseTestState.corruptRemote) { Add-Content (Join-Path $directory $installer) 'tampered' }
        if ($global:DittoReleaseTestState.extraRemote) { Set-Content "$directory/internal-info.json" 'must not be public' }
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
    if ($Uri -like '*/releases/tags/*') { throw 'The published-tag endpoint cannot be used to discover drafts.' }
    if ($Uri -like '*/releases?per_page=100&page=*') {
        if ($global:DittoReleaseTestState.scenario -eq 'lookup-failure') { throw 'Simulated authenticated API failure.' }
        if ($global:DittoReleaseTestState.scenario -eq 'new') { Write-Output -NoEnumerate @(); return }
        if ($global:DittoReleaseTestState.scenario -eq 'draft-page-two' -and $Uri.EndsWith('page=1')) {
            Write-Output -NoEnumerate @(1..100 | ForEach-Object { [pscustomobject]@{tag_name="old-$_";draft=$false;prerelease=$false} }); return
        }
        Write-Output -NoEnumerate @([pscustomobject]@{tag_name=$metadata.tag;draft=$global:DittoReleaseTestState.draft;prerelease=($metadata.channel -eq 'beta')}); return
    }
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
    foreach ($scenario in @('new','draft','draft-page-two','lookup-failure','published','older','equal','different-hash','corrupt-remote','corrupt-public','extra-remote')) {
        $global:DittoReleaseTestState = @{scenario=$scenario;commands=@();writes=0;draft=($scenario -in @('draft','draft-page-two'));corruptRemote=($scenario -eq 'corrupt-remote');corruptPublic=($scenario -eq 'corrupt-public');extraRemote=($scenario -eq 'extra-remote')}
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
        $expectedFailure = $scenario -in @('lookup-failure','different-hash','corrupt-remote','corrupt-public','extra-remote')
        if ([bool]$caught -ne $expectedFailure) { throw "Unexpected result in ${scenario}: $caught" }
        $expectedWrites = if ($scenario -in @('new','draft','draft-page-two','published')) { 1 } else { 0 }
        if ($global:DittoReleaseTestState.writes -ne $expectedWrites) { throw "Wrong feed write count: $scenario" }
        if ($global:DittoReleaseTestState.feed.channels.$other.marker -ne 'preserve-other-channel') { throw 'Other channel was overwritten.' }
        if (!$global:DittoReleaseTestState.draft -and $scenario -ne 'new' -and ($global:DittoReleaseTestState.commands -match '^release (upload|edit|create) ')) { throw 'Published release was mutated.' }
        if ($scenario -like 'draft*' -and !($global:DittoReleaseTestState.commands -match '^release upload ')) { throw 'Draft was not repaired.' }
        if ($scenario -like 'draft*' -and ($global:DittoReleaseTestState.commands -match '^release create ')) { throw 'Draft repair tried to create a duplicate release.' }
        if ($scenario -eq 'lookup-failure' -and $global:DittoReleaseTestState.commands.Count) { throw 'API failure caused a publication attempt.' }
        if ($scenario -eq 'corrupt-remote' -and ($global:DittoReleaseTestState.commands -match '^release edit ')) { throw 'Corrupt download was published.' }
        Write-Output "PASS: publication scenario $scenario"
    }
} finally {
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
Write-Output 'PASS: release-tag validation and eleven offline publication scenarios, including paginated draft repair and private metadata exclusion. No network calls or installers executed.'
