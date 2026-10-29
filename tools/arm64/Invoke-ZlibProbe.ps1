param([string]$VisualStudioPath)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (!$VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -property installationPath
}
if (!$VisualStudioPath) { throw 'Visual Studio or Build Tools installation not found.' }
$sourcePath = Join-Path $repoRoot 'work\dependencies\zlib'
$expectedCommit = 'da607da739fa6047df13e66a2af6b8bec7c2a498'
if (!(Test-Path -LiteralPath $sourcePath)) {
    & git clone --depth 1 --branch v1.3.2 https://github.com/madler/zlib.git $sourcePath
    if ($LASTEXITCODE) { throw 'zlib checkout failed.' }
}
$actualCommit = & git -C $sourcePath rev-parse HEAD
if ($actualCommit -ne $expectedCommit) { throw "Unexpected zlib checkout: $actualCommit" }
$buildPath = Join-Path $repoRoot ('work\zlib-arm64-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
$nativeHost = [bool](Get-CimInstance Win32_Processor | Where-Object Architecture -eq 12)
$hostArch = if ($nativeHost) { 'arm64' } else { 'x64' }
$vsDevCmd = Join-Path $VisualStudioPath 'Common7\Tools\VsDevCmd.bat'
$environmentScript = Join-Path $buildPath 'environment.cmd'
@('@echo off', ('call "{0}" -no_logo -arch=arm64 -host_arch={1} >nul' -f $vsDevCmd, $hostArch), 'if errorlevel 1 exit /b %errorlevel%', 'set') | Set-Content $environmentScript -Encoding ascii
$environmentLines = & $environmentScript
if ($LASTEXITCODE) { throw 'ARM64 compiler environment initialization failed.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$cmakeRoot = Join-Path $VisualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$cmake = Join-Path $cmakeRoot 'CMake\bin\cmake.exe'
$ctest = Join-Path $cmakeRoot 'CMake\bin\ctest.exe'
$ninja = Join-Path $cmakeRoot 'Ninja\ninja.exe'
# Windows PowerShell treats native stderr as ErrorRecords; use exit codes for these tools.
$ErrorActionPreference = 'Continue'
& $cmake -S $sourcePath -B $buildPath -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_STATIC=ON -DZLIB_BUILD_TESTING=ON *> (Join-Path $buildPath 'configure.log')
if ($LASTEXITCODE) { throw "Configuration failed. Logs: $buildPath" }
& $cmake --build $buildPath *> (Join-Path $buildPath 'build.log')
if ($LASTEXITCODE) { throw "Build failed. Logs: $buildPath" }
$testExe = Get-ChildItem -LiteralPath $buildPath -Recurse -Filter zlib_static_example.exe | Select-Object -First 1
if (!$testExe) { throw 'zlib test executable was not produced.' }
& dumpbin.exe /headers $testExe.FullName *> (Join-Path $buildPath 'headers.log')
& dumpbin.exe /dependents $testExe.FullName *> (Join-Path $buildPath 'dependents.log')
$machine = ((Select-String -Path (Join-Path $buildPath 'headers.log') -Pattern 'machine \(' | Select-Object -First 1).Line).Trim()
$testExit = $null
if ($nativeHost) {
    & $ctest --test-dir $buildPath --output-on-failure *> (Join-Path $buildPath 'test.log')
    $testExit = $LASTEXITCODE
}
[pscustomobject]@{name='zlib';version='1.3.2';commit=$actualCommit;buildExit=0;testExit=$testExit;machine=$machine;logs=$buildPath} | ConvertTo-Json | Set-Content (Join-Path $buildPath 'results.json') -Encoding utf8
Write-Output "zlib build=0 tests=$testExit machine=$machine"
Write-Output "Results saved to $buildPath"
if ($null -ne $testExit -and $testExit -ne 0) { exit 1 }
