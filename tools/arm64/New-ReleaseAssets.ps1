param(
    [Parameter(Mandatory=$true)][string]$PackagePath,
    [Parameter(Mandatory=$true)][string]$InstallerPath,
    [string]$OutputDirectory = 'artifacts/release'
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$metadata = & "$PSScriptRoot/Get-ReleaseMetadata.ps1"
$PackagePath = (Resolve-Path -LiteralPath $PackagePath).Path
$InstallerPath = (Resolve-Path -LiteralPath $InstallerPath).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Release output already exists; use a new directory.' }
$build = Get-Content "$PackagePath/build-info.json" -Raw | ConvertFrom-Json
$installerInfo = Get-Content "$InstallerPath/installer-info.json" -Raw | ConvertFrom-Json
$commit = & git -C $repoRoot rev-parse HEAD
if ($LASTEXITCODE) { throw 'Cannot identify source commit.' }
if ($build.sourceCommit -ne $commit -or @($build.sourceChanges).Count -ne 0) { throw 'Release packages require a clean, committed source checkout.' }
if ($installerInfo.sourceCommit -ne $commit -or $build.version.Trim() -ne $metadata.version) { throw 'Package source/version mismatch.' }
$installer = Join-Path $InstallerPath $installerInfo.installer
if ((Get-FileHash -LiteralPath $installer).Hash -ne $installerInfo.sha256) { throw 'Installer hash differs from build manifest.' }
if ((Get-Item -LiteralPath $installer).VersionInfo.FileVersion.Trim() -ne $metadata.version) { throw 'Installer version mismatch.' }
if ((Get-AuthenticodeSignature -LiteralPath $installer).Status -ne 'NotSigned') { throw 'This workflow currently publishes unsigned development builds only.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$installerName = "DittoSetup_$($metadata.version)_arm64.exe"
Copy-Item -LiteralPath $installer -Destination (Join-Path $OutputDirectory $installerName)
Copy-Item -LiteralPath "$PackagePath.zip" -Destination "$OutputDirectory/DittoPortable_$($metadata.version)_arm64.zip"
Copy-Item -LiteralPath "$PackagePath/build-info.json" -Destination $OutputDirectory
$installerInfo.installer = $installerName
$installerInfo | ConvertTo-Json -Depth 4 | Set-Content "$OutputDirectory/installer-info.json" -Encoding utf8NoBOM
# The corresponding source includes the build scripts; supply the external zlib source too.
& git -C $repoRoot archive --format=zip "--prefix=ditto-arm64-$($metadata.version)/" "--output=$OutputDirectory/DittoSource_$($metadata.version).zip" HEAD
if ($LASTEXITCODE) { throw 'Source archive failed.' }
& git -C "$repoRoot/work/dependencies/zlib" archive --format=zip --prefix=zlib-1.3.2/ "--output=$OutputDirectory/zlib-1.3.2-source.zip" HEAD
if ($LASTEXITCODE) { throw 'zlib source archive failed.' }
$notes = if (Test-Path $metadata.notesPath) { (Get-Content $metadata.notesPath -Raw).Trim() } else { "Ditto Windows ARM64 $($metadata.version) development build." }
if ([Text.Encoding]::UTF8.GetByteCount($notes) -gt 8192) { throw 'Release notes exceed the updater limit of 8 KiB.' }
[IO.File]::WriteAllText("$OutputDirectory/release-notes.md", $notes + "`n")
$entry = [ordered]@{
    version = $metadata.version
    publishedAt = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    minimumWindowsBuild = 22000
    releaseNotesUrl = "https://github.com/steathy/ditto-arm64/releases/tag/$($metadata.tag)"
    releaseNotes = $notes
    installer = [ordered]@{
        url = "https://github.com/steathy/ditto-arm64/releases/download/$($metadata.tag)/$installerName"
        sha256 = (Get-FileHash -LiteralPath $installer).Hash.ToLowerInvariant()
        size = (Get-Item -LiteralPath $installer).Length
    }
}
$entry | ConvertTo-Json -Depth 5 | Set-Content "$OutputDirectory/update-entry.json" -Encoding utf8NoBOM
[ordered]@{version=$metadata.version;tag=$metadata.tag;channel=$metadata.channel;sourceCommit=$commit;signed=$false} |
    ConvertTo-Json | Set-Content "$OutputDirectory/release-info.json" -Encoding utf8NoBOM
$checksums = Get-ChildItem -LiteralPath $OutputDirectory -File | Sort-Object Name | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant(), $_.Name
}
[IO.File]::WriteAllLines("$OutputDirectory/SHA256SUMS", [string[]]$checksums)
Write-Output "Prepared $($metadata.channel) $($metadata.version) assets in $OutputDirectory"
