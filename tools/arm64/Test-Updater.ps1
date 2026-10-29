param([switch]$LiveFeed)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$environmentScript = Join-Path $repoRoot 'work\build-arm64-environment.cmd'
if (!(Test-Path -LiteralPath $environmentScript)) { throw 'Run Build-Arm64.ps1 first.' }
$lines = & $environmentScript
if ($LASTEXITCODE) { throw 'Compiler environment initialization failed.' }
foreach ($line in $lines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$testPath = Join-Path $repoRoot ('work\update-test-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $testPath | Out-Null
Push-Location $testPath
try {
    & rc.exe /nologo /fo update_fixture.res "$PSScriptRoot\probes\update_fixture.rc" *> resource.log
    if ($LASTEXITCODE) { throw 'Updater test resource compilation failed.' }
    & cl.exe /nologo /std:c++17 /EHsc /MT /O2 /W4 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 "$PSScriptRoot\probes\update_probe.cpp" "$repoRoot\src\update\UpdateModel.cpp" "$repoRoot\src\update\UpdateWindows.cpp" update_fixture.res /Fe:update_probe.exe /link /MANIFEST:EMBED "/MANIFESTINPUT:$repoRoot\src\update\UpdateHelper.manifest" *> build.log
    if ($LASTEXITCODE) { Get-Content .\build.log -Tail 40; throw 'Updater test compilation failed.' }
    $arguments = @('"' + (Join-Path $testPath 'fixtures') + '"', '"' + (Join-Path $repoRoot 'ARM64\Release\Ditto.exe') + '"')
    if ($LiveFeed) { $arguments += '--live-feed' }
    $probe = Start-Process -FilePath (Join-Path $testPath 'update_probe.exe') -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $testPath 'results.log') -RedirectStandardError (Join-Path $testPath 'errors.log')
    $testExit = $probe.ExitCode
    Get-Content .\results.log
    Get-Content .\errors.log
    if ($testExit) { throw 'Updater tests failed.' }
    if (!(Select-String -LiteralPath .\results.log -SimpleMatch 'PASS:')) { throw 'Updater test did not report completion.' }
    Write-Output "Updater test evidence: $testPath"
} finally { Pop-Location }
