param(
    [Parameter(Mandatory=$true)][string]$AssetPath,
    [Parameter(Mandatory=$true)][string]$Tag
)
$ErrorActionPreference = 'Stop'
# This script deliberately has no local publication mode.
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:GITHUB_REPOSITORY -ne 'steathy/ditto-arm64' -or
    $env:GITHUB_EVENT_NAME -ne 'push' -or $env:GITHUB_REF -cne "refs/tags/$Tag") {
    throw 'Publication is restricted to a version-tag push in the official fork workflow.'
}
$metadata = & "$PSScriptRoot/Get-ReleaseMetadata.ps1" -Tag $Tag
$AssetPath = (Resolve-Path -LiteralPath $AssetPath).Path
$info = Get-Content "$AssetPath/release-info.json" -Raw | ConvertFrom-Json
$entry = Get-Content "$AssetPath/update-entry.json" -Raw | ConvertFrom-Json
if ($info.tag -cne $Tag -or $info.version -ne $metadata.version -or $info.channel -ne $metadata.channel -or
    $info.sourceCommit -ne $env:GITHUB_SHA -or $entry.version -ne $metadata.version -or $info.signed) { throw 'Release metadata mismatch.' }
$repository = $env:GITHUB_REPOSITORY
function Invoke-Gh([string[]]$Arguments) {
    $result = & gh @Arguments
    if ($LASTEXITCODE) { throw "GitHub CLI failed: $($Arguments[0])" }
    return $result
}
function Test-Checksums([string]$Directory) {
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($line in Get-Content "$AssetPath/SHA256SUMS") {
        if ($line -notmatch '^([0-9a-f]{64})  ([A-Za-z0-9_.-]+)$') { throw 'Malformed checksum record.' }
        $hash = $Matches[1]; $name = $Matches[2]
        if (!$names.Add($name)) { throw 'Duplicate checksum record.' }
        if ((Get-FileHash -LiteralPath (Join-Path $Directory $name)).Hash -ne $hash) { throw "Checksum mismatch: $name" }
    }
    if ($names.Count -lt 8 -or !$names.Contains('release-info.json') -or !$names.Contains('update-entry.json')) { throw 'Incomplete release manifest.' }
    foreach ($file in Get-ChildItem -LiteralPath $Directory -File) {
        if ($file.Name -ne 'SHA256SUMS' -and !$names.Contains($file.Name)) { throw 'Unlisted release asset.' }
    }
    if ((Get-FileHash "$Directory/SHA256SUMS").Hash -ne (Get-FileHash "$AssetPath/SHA256SUMS").Hash) { throw 'Uploaded checksum manifest differs.' }
}
Test-Checksums $AssetPath
$headers = @{Authorization="Bearer $env:GH_TOKEN";Accept='application/vnd.github+json';'X-GitHub-Api-Version'='2022-11-28'}
$api = "https://api.github.com/repos/$repository"
try { $release = Invoke-RestMethod "$api/releases/tags/$Tag" -Headers $headers }
catch {
    if ([int]$_.Exception.Response.StatusCode -ne 404) { throw }
    $release = $null
}
$assets = @((Get-ChildItem -LiteralPath $AssetPath -File).FullName)
if (!$release) {
    $arguments = @('release','create',$Tag,'--repo',$repository,'--verify-tag','--target',$info.sourceCommit,
        '--draft','--latest=false','--title',"Ditto Windows ARM64 $($info.version)",'--notes-file',"$AssetPath/release-notes.md")
    if ($info.channel -eq 'beta') { $arguments += '--prerelease' }
    Invoke-Gh ($arguments + $assets)
} elseif ($release.draft) {
    # Only an unfinished draft may be repaired. Published versioned assets are immutable.
    Invoke-Gh (@('release','upload',$Tag,'--repo',$repository,'--clobber') + $assets)
} elseif ([bool]$release.prerelease -ne ($info.channel -eq 'beta')) {
    throw 'Existing release channel differs.'
}
$verifyPath = Join-Path $env:RUNNER_TEMP ('release-verify-' + [guid]::NewGuid().ToString('N'))
Invoke-Gh @('release','download',$Tag,'--repo',$repository,'--dir',$verifyPath)
Test-Checksums $verifyPath
if (!$release -or $release.draft) {
    $latest = if ($info.channel -eq 'stable') { '--latest=true' } else { '--latest=false' }
    Invoke-Gh @('release','edit',$Tag,'--repo',$repository,'--draft=false',$latest)
}
# Check the actual public download before advertising it to installed clients.
$publicInstaller = Join-Path $env:RUNNER_TEMP ('public-installer-' + [guid]::NewGuid().ToString('N') + '.exe')
$downloaded = $false
for ($attempt = 0; $attempt -lt 4; $attempt++) {
    try { Invoke-WebRequest $entry.installer.url -OutFile $publicInstaller; $downloaded = $true; break }
    catch { if ($attempt -eq 3) { throw }; Start-Sleep -Seconds 5 }
}
if (!$downloaded -or (Get-Item $publicInstaller).Length -ne $entry.installer.size -or
    (Get-FileHash $publicInstaller).Hash -ne $entry.installer.sha256) { throw 'Public installer verification failed; feed was not changed.' }
# Optimistic Contents API update preserves the other channel and concurrent main changes.
for ($attempt = 0; $attempt -lt 4; $attempt++) {
    $file = Invoke-RestMethod "$api/contents/updates/arm64.json?ref=main" -Headers $headers
    $feed = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($file.content)) | ConvertFrom-Json
    if ($feed.schemaVersion -ne 2 -or $feed.appId -ne 'steathy.ditto-arm64' -or
        $feed.platform -ne 'windows' -or $feed.architecture -ne 'arm64' -or
        !($feed.channels.PSObject.Properties.Name -contains 'stable') -or
        !($feed.channels.PSObject.Properties.Name -contains 'beta')) { throw 'Unexpected feed format.' }
    $current = $feed.channels.($info.channel)
    if ($current -and [version]$current.version -gt [version]$entry.version) {
        Write-Output 'A newer release already occupies this channel; keeping it.'
        break
    }
    if ($current -and [version]$current.version -eq [version]$entry.version) {
        if ($current.installer.sha256 -ne $entry.installer.sha256 -or $current.installer.url -ne $entry.installer.url) { throw 'Existing version has different installer content.' }
        Write-Output 'Feed already contains this verified release.'
        break
    }
    $feed.channels.($info.channel) = $entry
    $json = ($feed | ConvertTo-Json -Depth 8) + "`n"
    if ([Text.Encoding]::UTF8.GetByteCount($json) -gt 65536) { throw 'Feed exceeds client limit.' }
    $body = @{message="Publish $($info.channel) update $($info.version)";branch='main';sha=$file.sha;content=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($json))} | ConvertTo-Json
    try {
        $null = Invoke-RestMethod "$api/contents/updates/arm64.json" -Method Put -Headers $headers -ContentType 'application/json' -Body ([Text.Encoding]::UTF8.GetBytes($body))
        Write-Output "Published $($info.channel) feed entry for $($info.version)."
        break
    } catch {
        if ([int]$_.Exception.Response.StatusCode -ne 409 -or $attempt -eq 3) { throw }
        Start-Sleep -Seconds 2
    }
}
"Release: https://github.com/$repository/releases/tag/$Tag" >> $env:GITHUB_STEP_SUMMARY
"Channel: $($info.channel). Installer is unsigned; installation requires user consent." >> $env:GITHUB_STEP_SUMMARY
