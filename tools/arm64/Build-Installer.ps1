param(
    [Parameter(Mandatory=$true)][string]$PackagePath,
    [string]$IsccPath,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$PackagePath = (Resolve-Path -LiteralPath $PackagePath).Path
if (!$IsccPath) {
    $candidates = @(
        (Join-Path $repoRoot 'work\tools\InnoSetup6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe')
    )
    $IsccPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (!$IsccPath) { throw 'Install Inno Setup 6.7 or newer, or provide -IsccPath.' }
$manifest = Get-Content -LiteralPath (Join-Path $PackagePath 'build-info.json') -Raw | ConvertFrom-Json
$required = @('Ditto.exe','DittoUpdate.exe','ICU_Loader.dll','Addins\DittoUtil.dll','mfc140u.dll','msvcp140.dll','vcruntime140.dll')
foreach ($relative in $required) {
    $record = @($manifest.modules | Where-Object file -eq $relative)
    if ($record.Count -ne 1) { throw "Missing/duplicate package manifest entry: $relative" }
    $file = Join-Path $PackagePath $relative
    if ((Get-FileHash -LiteralPath $file).Hash -ne $record[0].sha256) { throw "Package hash mismatch: $relative" }
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($file))
    try {
        $reader.BaseStream.Position = 0x3c
        $offset = $reader.ReadInt32()
        $reader.BaseStream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0xAA64) { throw "Not a native ARM64 PE: $relative" }
    } finally { $reader.Dispose() }
}
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repoRoot ('artifacts\installer-arm64-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw "Output directory already exists: $OutputDirectory" }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$label = if ($manifest.sourceCommit) { $manifest.sourceCommit.Substring(0,7) } else { $manifest.upstreamCommit.Substring(0,7) + '-snapshot' }
$compileLog = Join-Path $OutputDirectory 'compile.log'
$savedErrorPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    & $IsccPath "/DPackagePath=$PackagePath" "/DBuildLabel=$label" "/O$OutputDirectory" (Join-Path $repoRoot 'DittoSetup\DittoSetup_ARM64.iss') *> $compileLog
    $compileExitCode = $LASTEXITCODE
} finally { $ErrorActionPreference = $savedErrorPreference }
if ($compileExitCode) { throw "Inno Setup compilation failed. See $compileLog" }
$installer = @(Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.exe' -File)
if ($installer.Count -ne 1) { throw 'Expected exactly one installer output.' }
$result = [ordered]@{
    createdAt = (Get-Date).ToString('o')
    installer = $installer[0].Name
    sha256 = (Get-FileHash -LiteralPath $installer[0].FullName).Hash
    package = Split-Path -Path $PackagePath -Leaf
    sourceCommit = $manifest.sourceCommit
    nativePayloadFiles = $required
    appId = 'Ditto'
    signed = $false
    preservesUserSettingsAndDatabase = $true
}
$result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'installer-info.json') -Encoding utf8
Write-Output "Installer: $($installer[0].FullName)"
Write-Output "SHA256: $($result.sha256)"
