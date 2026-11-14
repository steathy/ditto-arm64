param([string]$IsccPath)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$IsccPath) { $IsccPath = Join-Path $repoRoot 'work/tools/InnoSetup6/ISCC.exe' }
if (!(Test-Path -LiteralPath $IsccPath)) { throw 'Pass -IsccPath for the Inno Setup compiler.' }
$policy = [IO.File]::ReadAllText((Join-Path $repoRoot 'DittoSetup/DittoSetup_arm64.iss'))
$mode = [regex]::Match($policy, '(?m)^UninstallLogMode=(\w+)\s*$').Groups[1].Value
if ($mode -ne 'overwrite') { throw 'Migration must discard inherited settings-deletion records.' }
if ($policy -match '(?im)^Root: HKCU; Subkey: Software\\Ditto(?:;|\\).*uninsdelete') { throw 'ARM64 uninstall must not delete Ditto preferences.' }
if ($policy -notmatch '(?m)^Type: files; Name: \{app\}\\vcruntime140_1\.dll\s*$') { throw 'Missing explicit obsolete runtime cleanup.' }
if ($policy -notmatch "--clear-cache --uninstall") { throw 'Uninstall must use the account-checked cleanup mode.' }

# Check every installer language against the checked-out branch's payload.
$mapping = @{ German='Deutsch'; Italiano='Italian'; Polish='Polski'; Portuguese_Portugal='Portuguese'; Simplified_Chinese='Chinese Simplified'; Traditional_Chinese='Chinese Traditional' }
$mapping.Portuguese_Brazil = if (Test-Path -LiteralPath (Join-Path $repoRoot 'Debug/Language/Portuguese (Brazil).xml')) { 'Portuguese (Brazil)' } else { 'Portuguese' }
$languages = [regex]::Matches($policy, '(?m)^Name: (\w+); MessagesFile:')
foreach ($language in $languages) {
    $name = $language.Groups[1].Value
    $file = if ($mapping.ContainsKey($name)) { $mapping[$name] } else { $name }
    if (!(Test-Path -LiteralPath (Join-Path $repoRoot "Debug/Language/$file.xml"))) { throw "Installer language has no packaged translation: $name" }
}
if ($policy -notmatch 'FileExists\(PackagePath \+ "\\Language\\Portuguese \(Brazil\)\.xml"\)') { throw 'Brazilian Portuguese must depend on the packaged translation.' }
Write-Output "PASS: $($languages.Count) installer language mappings; Brazilian Portuguese selects $($mapping.Portuguese_Brazil)."

$root = Join-Path $repoRoot ('work/installer-policy-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$payload = Join-Path $root 'fixture.txt'
[IO.File]::WriteAllText($payload, 'Synthetic installer-policy fixture. Never an application or clipboard database.')
function Run-Fixture([string]$File, [string[]]$Arguments) {
    $process = Start-Process -FilePath $File -ArgumentList $Arguments -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode) { throw "Installer-policy fixture failed with exit $($process.ExitCode). Evidence: $root" }
}
# These tiny, per-user installers never register as Ditto, run an application,
# request elevation, or touch Software\Ditto / the live clipboard database.
# The append case is a negative control proving that the inherited record fires.
foreach ($scenario in @('append', $mode)) {
    $id = 'DittoARM64-PolicyTest-' + [guid]::NewGuid().ToString('N')
    $directory = Join-Path $root $scenario
    $app = Join-Path $directory 'application'
    $registry = 'Software\' + $id
    $key = 'HKCU:\' + $registry
    New-Item -ItemType Directory -Path $directory | Out-Null
    $database = Join-Path $directory 'synthetic-history.db'
    [IO.File]::WriteAllText($database, 'preserve fixture history')
    try {
        foreach ($stage in @('official', 'arm64')) {
            $logMode = if ($stage -eq 'official') { 'overwrite' } else { $scenario }
            $registryFlags = if ($stage -eq 'official') { 'uninsdeletekey' } else { 'createvalueifdoesntexist' }
            $obsoleteFile = if ($stage -eq 'official') { 'Source: "' + $payload + '"; DestDir: {app}; DestName: vcruntime140_1.dll; Flags: ignoreversion' } else { '' }
            $cleanup = if ($stage -eq 'arm64') { '[InstallDelete]' + "`r`n" + 'Type: files; Name: {app}\vcruntime140_1.dll' } else { '' }
            $script = @"
[Setup]
AppId=$id
AppName=$id
AppVersion=1.0
DefaultDirName=$app
PrivilegesRequired=lowest
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
UninstallLogMode=$logMode
OutputDir=$directory
OutputBaseFilename=$stage
DisableProgramGroupPage=yes
DisableDirPage=yes
SetupLogging=yes
[Files]
Source: "$payload"; DestDir: {app}; DestName: application-fixture.txt; Flags: ignoreversion
$obsoleteFile
[Registry]
Root: HKCU; Subkey: $registry; ValueType: string; ValueName: SavedPreference; ValueData: original; Flags: $registryFlags
$cleanup
"@
            $scriptPath = Join-Path $directory "$stage.iss"
            [IO.File]::WriteAllText($scriptPath, $script)
            & $IsccPath $scriptPath *> (Join-Path $directory "$stage-compile.log")
            if ($LASTEXITCODE) { throw "Fixture compilation failed: $scriptPath" }
            Run-Fixture (Join-Path $directory "$stage.exe") @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/LOG="{0}"' -f (Join-Path $directory "$stage-install.log")))
            if ($stage -eq 'official') {
                Set-ItemProperty -LiteralPath $key -Name SavedPreference -Value 'user fixture preference'
                [IO.File]::WriteAllText((Join-Path $app 'custom-theme.xml'), 'keep user fixture file')
            }
        }
        if ((Get-ItemProperty -LiteralPath $key).SavedPreference -cne 'user fixture preference') { throw 'Migration overwrote a saved preference.' }
        if (Test-Path -LiteralPath (Join-Path $app 'vcruntime140_1.dll')) { throw 'Obsolete fixture runtime remained.' }
        if (!(Test-Path -LiteralPath (Join-Path $app 'custom-theme.xml'))) { throw 'Cleanup removed a user fixture file.' }
        Run-Fixture (Join-Path $app 'unins000.exe') @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="{0}"' -f (Join-Path $directory 'uninstall.log')))
        if ($scenario -eq 'append') {
            if (Test-Path -LiteralPath $key) { throw 'Negative control did not reproduce inherited settings deletion.' }
        } elseif (!(Test-Path -LiteralPath $key) -or (Get-ItemProperty -LiteralPath $key).SavedPreference -cne 'user fixture preference') {
            throw 'Production overwrite policy did not preserve the saved preference on uninstall.'
        }
        if ([IO.File]::ReadAllText($database) -cne 'preserve fixture history') { throw 'Synthetic history changed.' }
        Write-Output "PASS: $scenario migration/uninstall fixture; stale runtime removed; custom file and synthetic history preserved."
    } finally {
        $uninstaller = Join-Path $app 'unins000.exe'
        if (Test-Path -LiteralPath $uninstaller) { Run-Fixture $uninstaller @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') }
        # This is the unique fixture key created above, never the application's key.
        if ($key -notmatch '^HKCU:\\Software\\DittoARM64-PolicyTest-[0-9a-f]{32}$') { throw 'Unexpected fixture registry path.' }
        if (Test-Path -LiteralPath $key) { Remove-Item -LiteralPath $key -Recurse -Force }
    }
}
Write-Output "Installer-policy evidence: $root"
