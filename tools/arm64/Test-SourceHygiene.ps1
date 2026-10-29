param([string]$ReportPath)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (!$ReportPath) { $ReportPath = Join-Path $repoRoot 'work\source-hygiene.json' }
$files = @(& git -C $repoRoot ls-files)
if ($LASTEXITCODE -or !$files.Count) { throw 'Stage the intended source files before running this audit.' }
$issues = [Collections.Generic.List[object]]::new()
$forbiddenExtension = '(?i)\.(exe|dll|lib|obj|pdb|pfx|p12|pem|key|cer|crt|zip|7z|db|sqlite3?|reg|dmp|user|suo|aps|plg|binlog|log)$'
$patterns = [ordered]@{
    userDirectory = '(?i)[A-Z]:[/\\]+(?:Users|Documents and Settings)[/\\]+'
    privateKey = '-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----'
    githubToken = '(?:gh[pousr]_[A-Za-z0-9]{25,}|github_pat_[A-Za-z0-9_]{30,})'
    awsAccessKey = 'AKIA[0-9A-Z]{16}'
    slackToken = 'xox[baprs]-[A-Za-z0-9-]{20,}'
}
# Specific local identifiers are checked at runtime without storing them in source.
if ($env:USERNAME -and $env:USERNAME.Length -ge 4 -and $env:USERNAME -ne 'steathy') {
    $patterns.localAccountName = '(?i)(?<![A-Za-z0-9_])' + [regex]::Escape($env:USERNAME) + '(?![A-Za-z0-9_])'
}
if ($env:COMPUTERNAME -and $env:COMPUTERNAME.Length -ge 5) {
    $patterns.localComputerName = '(?i)(?<![A-Za-z0-9_])' + [regex]::Escape($env:COMPUTERNAME) + '(?![A-Za-z0-9_])'
}
$binaryAssets = '(?i)\.(png|bmp|ico|cur|gif|avi)$'
foreach ($relative in $files) {
    if ($relative -match $forbiddenExtension -or $relative -match '^(work|artifacts|backups)/' -or $relative -match '^docs/arm64/evidence/') {
        $issues.Add([pscustomobject]@{file=$relative;category='generated-or-sensitive-file'})
        continue
    }
    if ($relative -match $binaryAssets) { continue }
    $content = [IO.File]::ReadAllText((Join-Path $repoRoot $relative))
    foreach ($pattern in $patterns.GetEnumerator()) {
        if ([regex]::IsMatch($content,$pattern.Value)) {
            $issues.Add([pscustomobject]@{file=$relative;category=$pattern.Key})
        }
    }
}
$required = @('LICENSE','ReadMe.md','UPSTREAM.md','src/About.cpp','CP_Main.vcxproj',
    'Directory.Build.props','Directory.Build.targets','build/arm64-dependencies.props','build/source-info.json',
    'tools/arm64/Build-Arm64.ps1','tools/arm64/Package-Portable.ps1','tools/arm64/Build-Installer.ps1',
    'docs/arm64/STATUS.md','Debug/Language/English.xml','Debug/Themes/Classic.xml')
foreach ($relative in $required) {
    if ($relative -notin $files) { $issues.Add([pscustomobject]@{file=$relative;category='required-file-not-staged'}) }
    & git -C $repoRoot check-ignore --no-index --quiet -- $relative
    if ($LASTEXITCODE -eq 0) { $issues.Add([pscustomobject]@{file=$relative;category='source-incorrectly-ignored'}) }
}
foreach ($relative in @('work/private.txt','artifacts/backups/history.db','example.pfx','project.vcxproj.user',
    'ARM64/Release/Ditto.exe','docs/arm64/evidence/machine.json','CP_Main_i.c','CP_Main_i.h')) {
    & git -C $repoRoot check-ignore --no-index --quiet -- $relative
    if ($LASTEXITCODE -ne 0) { $issues.Add([pscustomobject]@{file=$relative;category='ignore-rule-missing'}) }
}
$unstaged = @(& git -C $repoRoot diff --name-only)
$untracked = @(& git -C $repoRoot ls-files --others --exclude-standard)
foreach ($relative in @($unstaged) + @($untracked)) {
    $issues.Add([pscustomobject]@{file=$relative;category='working-tree-not-fully-staged'})
}
$result = [ordered]@{
    passed = $issues.Count -eq 0
    checkedAt = (Get-Date).ToString('o')
    trackedFiles = $files.Count
    languageFiles = @($files | Where-Object { $_ -like 'Debug/Language/*' }).Count
    themeFiles = @($files | Where-Object { $_ -like 'Debug/Themes/*' }).Count
    findings = @($issues.ToArray())
    scope = 'Staged source plus matching working files; excludes local .git metadata, ignored artifacts and binary image assets.'
    limitation = 'Pattern scan and manual review reduce disclosure risk but do not prove absence of every possible secret.'
}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($ReportPath))) | Out-Null
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ReportPath -Encoding utf8
$result | ConvertTo-Json -Depth 5
if (!$result.passed) { throw 'Source hygiene audit failed. See the findings above; no secret values are printed.' }
