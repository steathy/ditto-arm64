param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$VisualStudioPath,
    [ValidateSet('arm64', 'x64')][string]$CompilerHost,
    [switch]$Rebuild
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (!$VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 Microsoft.VisualStudio.Component.VC.MFC.ARM64 -property installationPath
}
if (!$VisualStudioPath) { throw 'Install ARM64 C++ tools and MFC/ATL using tools/arm64/buildtools.vsconfig.' }
$workPath = Join-Path $repoRoot 'work'
New-Item -ItemType Directory -Path $workPath -Force | Out-Null
$hostArch = if ([bool](Get-CimInstance Win32_Processor | Where-Object Architecture -eq 12)) { 'arm64' } else { 'x64' }
if ($CompilerHost) { $hostArch = $CompilerHost }
$environmentScript = Join-Path $workPath 'build-arm64-environment.cmd'
$vsDevCmd = Join-Path $VisualStudioPath 'Common7\Tools\VsDevCmd.bat'
@('@echo off', ('call "{0}" -no_logo -arch=arm64 -host_arch={1} >nul' -f $vsDevCmd, $hostArch), 'if errorlevel 1 exit /b %errorlevel%', 'set') | Set-Content $environmentScript -Encoding ascii
$environmentLines = & $environmentScript
if ($LASTEXITCODE) { throw 'ARM64 compiler environment initialization failed.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
function Invoke-Logged([string]$Program, [string[]]$Arguments, [string]$LogPath) {
    # Windows PowerShell wraps stderr in ErrorRecords; the process exit code is authoritative.
    $ErrorActionPreference = 'Continue'
    & $Program @Arguments *> $LogPath
    if ($LASTEXITCODE) {
        Get-Content -LiteralPath $LogPath -Tail 35 | Write-Output
        throw "$Program failed with exit code $LASTEXITCODE. Full log: $LogPath"
    }
}
$sourcePath = Join-Path $workPath 'dependencies\zlib'
$expectedCommit = 'da607da739fa6047df13e66a2af6b8bec7c2a498'
if (!(Test-Path -LiteralPath $sourcePath)) {
    Invoke-Logged 'git' @('clone','--depth','1','--branch','v1.3.2','https://github.com/madler/zlib.git',$sourcePath) (Join-Path $workPath 'zlib-checkout.log')
}
$actualCommit = & git -C $sourcePath rev-parse HEAD
if ($LASTEXITCODE -or $actualCommit -ne $expectedCommit) { throw "Unexpected zlib revision: $actualCommit; expected $expectedCommit" }
$dirtySource = & git -C $sourcePath status --porcelain --untracked-files=no
if ($LASTEXITCODE -or $dirtySource) { throw 'The pinned zlib checkout has changes; restore a clean source checkout before building.' }
$buildPath = Join-Path $workPath "dependencies\build\arm64\$Configuration"
$installPath = Join-Path $workPath "dependencies\installed\arm64\$Configuration"
$cmakeRoot = Join-Path $VisualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$cmake = Join-Path $cmakeRoot 'CMake\bin\cmake.exe'
$ninja = Join-Path $cmakeRoot 'Ninja\ninja.exe'
$crt = if ($Configuration -eq 'Debug') { 'MultiThreadedDebugDLL' } else { 'MultiThreadedDLL' }
Write-Output "Building pinned zlib 1.3.2 for ARM64 $Configuration..."
Invoke-Logged $cmake @('-S',$sourcePath,'-B',$buildPath,'-G','Ninja',"-DCMAKE_MAKE_PROGRAM=$ninja","-DCMAKE_BUILD_TYPE=$Configuration","-DCMAKE_INSTALL_PREFIX=$installPath","-DCMAKE_MSVC_RUNTIME_LIBRARY=$crt",'-DZLIB_BUILD_SHARED=OFF','-DZLIB_BUILD_STATIC=ON','-DZLIB_BUILD_TESTING=OFF','-DZLIB_INSTALL=ON') (Join-Path $workPath "zlib-$Configuration-configure.log")
Invoke-Logged $cmake @('--build',$buildPath,'--target','install') (Join-Path $workPath "zlib-$Configuration-build.log")
$msbuildHost = if ($hostArch -eq 'arm64') { 'arm64' } else { 'amd64' }
$msbuild = Join-Path $VisualStudioPath "MSBuild\Current\Bin\$msbuildHost\MSBuild.exe"
$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
$buildLog = Join-Path $workPath "ditto-arm64-$Configuration.log"
Write-Output "Building Ditto ARM64 $Configuration. Log: $buildLog"
Push-Location $repoRoot
try {
    Invoke-Logged $msbuild @('CP_Main_10.sln','/nologo','/m:4',"/t:$target","/p:Configuration=$Configuration",'/p:Platform=ARM64',"/p:PreferredToolArchitecture=$hostArch",'/v:minimal',"/bl:work\ditto-arm64-$Configuration.binlog") $buildLog
} finally { Pop-Location }
Write-Output "Built $(Join-Path $repoRoot "ARM64\$Configuration\Ditto.exe")"
