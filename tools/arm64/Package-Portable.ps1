param([string]$VisualStudioPath, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$releasePath = Join-Path $repoRoot 'ARM64\Release'
if (!$VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.MFC.ARM64 -property installationPath
}
if (!$VisualStudioPath) { throw 'Visual Studio ARM64 MFC installation not found.' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repoRoot ('artifacts\DittoPortable-arm64-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw "Output directory already exists: $OutputDirectory" }
$required = @('Ditto.exe','DittoUpdate.exe','ICU_Loader.dll','Addins\DittoUtil.dll')
foreach ($file in $required) {
    if (!(Test-Path -LiteralPath (Join-Path $releasePath $file))) { throw "Build Release ARM64 first; missing $file" }
}
$redistVersionFile = Join-Path $VisualStudioPath 'VC\Auxiliary\Build\Microsoft.VCRedistVersion.default.txt'
$redistVersion = (Get-Content -LiteralPath $redistVersionFile -Raw).Trim()
$redistRoot = Join-Path $VisualStudioPath "VC\Redist\MSVC\$redistVersion\arm64"
$crtPath = Get-ChildItem -LiteralPath $redistRoot -Directory -Filter '*.CRT' | Select-Object -First 1
$mfcPath = Get-ChildItem -LiteralPath $redistRoot -Directory -Filter '*.MFC' | Select-Object -First 1
if (!$crtPath -or !$mfcPath) { throw "ARM64 redistributable files not found in $redistRoot" }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutputDirectory 'Addins') -Force | Out-Null
foreach ($file in $required) { Copy-Item -LiteralPath (Join-Path $releasePath $file) -Destination (Join-Path $OutputDirectory $file) }
# The pure ARM64 binaries import these two CRT DLLs. The redist also contains
# ARM64EC-only DLLs such as vcruntime140_1.dll; those do not belong in this package.
foreach ($runtime in @('msvcp140.dll','vcruntime140.dll')) {
    Copy-Item -LiteralPath (Join-Path $crtPath.FullName $runtime) -Destination $OutputDirectory
}
Copy-Item -LiteralPath (Join-Path $mfcPath.FullName 'mfc140u.dll') -Destination $OutputDirectory
foreach ($folder in @('Language','Themes')) {
    $destination = Join-Path $OutputDirectory $folder
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $repoRoot "Debug\$folder") -Filter '*.xml' -File | Copy-Item -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'DittoSetup\portable') -Destination (Join-Path $OutputDirectory 'portable')
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $OutputDirectory
$zlibLicense = Join-Path $repoRoot 'source-dependencies\zlib\LICENSE'
if (!(Test-Path -LiteralPath $zlibLicense)) { $zlibLicense = Join-Path $repoRoot 'work\dependencies\zlib\LICENSE' }
Copy-Item -LiteralPath $zlibLicense -Destination (Join-Path $OutputDirectory 'zlib-LICENSE.txt')
$moduleRecords = foreach ($file in Get-ChildItem -LiteralPath $OutputDirectory -Recurse -File | Where-Object Extension -in '.exe','.dll') {
    $stream = [IO.File]::OpenRead($file.FullName)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        $stream.Position = 0x3c
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x4550) { throw "Not a PE binary: $($file.Name)" }
        $machine = $reader.ReadUInt16()
        if ($machine -ne 0xAA64) { throw ('Non-ARM64 binary in package: {0} (0x{1:X4})' -f $file.FullName,$machine) }
        $bytes = [IO.File]::ReadAllBytes($file.FullName)
        $ascii = [Text.Encoding]::ASCII.GetString($bytes)
        $unicode = [Text.Encoding]::Unicode.GetString($bytes)
        $userDirectoryPattern = '(?i)[A-Z]:[/\\]+(?:Users|Documents and Settings)[/\\]+'
        if ($ascii -match $userDirectoryPattern -or $unicode -match $userDirectoryPattern) {
            throw "Local user-directory path embedded in $($file.Name). Rebuild with Directory.Build.targets before packaging."
        }
        [pscustomobject]@{file=$file.FullName.Substring($OutputDirectory.Length + 1);machine='ARM64 (AA64)';sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
    } finally { $reader.Dispose() }
}
$sourceInfo = Get-Content -LiteralPath (Join-Path $repoRoot 'build\source-info.json') -Raw | ConvertFrom-Json
$releaseMetadata = & (Join-Path $PSScriptRoot 'Get-ReleaseMetadata.ps1')
# An initial source import can be built for review before its first commit.
$sourceCommit = & git -C $repoRoot rev-parse --verify --quiet HEAD
if ($LASTEXITCODE) { $sourceCommit = $null }
$sourceChanges = @(& git -C $repoRoot status --porcelain --untracked-files=normal)
$manifest = [ordered]@{createdAt=(Get-Date).ToString('o');target='Windows ARM64';configuration='Release';version=(Get-Item -LiteralPath (Join-Path $releasePath 'Ditto.exe')).VersionInfo.ProductVersion;maintainer=$sourceInfo.maintainer;repository=$sourceInfo.repository;upstreamRepository=$sourceInfo.upstreamRepository;upstreamCommit=$sourceInfo.upstreamCommit;sourceCommit=$sourceCommit;sourceChanges=$sourceChanges;zlibCommit='da607da739fa6047df13e66a2af6b8bec7c2a498';redistVersion=$redistVersion;modules=@($moduleRecords)}
$manifest.channel = $releaseMetadata.channel
$manifest.upstreamVersion = $releaseMetadata.upstreamVersion
$manifest.upstreamTag = $releaseMetadata.upstreamTag
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'build-info.json') -Encoding utf8
@'
Ditto native Windows ARM64 community build

Extract the entire folder and run Ditto.exe on Windows 11 on Arm.
The portable marker keeps this build's settings and database beside the executable.
This is an unsigned community build, not an official Ditto release.
See build-info.json for the version, stable/beta channel and upstream baseline.
Avoid running two clipboard managers with the same global hotkey during normal use.
Built by steathy@github. See docs/arm64/STATUS.md in the source checkout for port status.
'@ | Set-Content -LiteralPath (Join-Path $OutputDirectory 'ARM64-README.txt') -Encoding utf8
$zipPath = $OutputDirectory + '.zip'
if (Test-Path -LiteralPath $zipPath) { throw "Archive already exists: $zipPath" }
Compress-Archive -LiteralPath $OutputDirectory -DestinationPath $zipPath
Write-Output "Portable directory: $OutputDirectory"
Write-Output "Portable archive: $zipPath"
