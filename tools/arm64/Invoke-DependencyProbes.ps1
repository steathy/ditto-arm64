param(
    [string]$VisualStudioPath,
    [switch]$SkipChaiScript,
    [ValidateSet('windows','sqlite','tinyxml','qrencode','chaiscript','mfc')]
    [string[]]$Only
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (!$VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -property installationPath
}
if (!$VisualStudioPath) { throw 'Visual Studio or Build Tools installation not found.' }
$vsDevCmd = Join-Path $VisualStudioPath 'Common7\Tools\VsDevCmd.bat'
# Capture only into this process. Do not print the complete environment.
$scratchPath = Join-Path $repoRoot 'work'
New-Item -ItemType Directory -Path $scratchPath -Force | Out-Null
$environmentScript = Join-Path $scratchPath 'capture-arm64-environment.cmd'
$canRun = [bool](Get-CimInstance Win32_Processor | Where-Object Architecture -eq 12)
$hostArch = if ($canRun) { 'arm64' } else { 'x64' }
@('@echo off', ('call "{0}" -no_logo -arch=arm64 -host_arch={1} >nul' -f $vsDevCmd, $hostArch), 'if errorlevel 1 exit /b %errorlevel%', 'set') | Set-Content $environmentScript -Encoding ascii
$environmentLines = & $environmentScript
if ($LASTEXITCODE) { throw 'ARM64 compiler environment initialization failed.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$runName = 'dependency-probes-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
$runPath = Join-Path $repoRoot "work\$runName"
New-Item -ItemType Directory -Path $runPath -Force | Out-Null
$results = [Collections.Generic.List[object]]::new()

function Invoke-Probe([string]$Name, [string[]]$Sources, [string[]]$Extra = @(), [string[]]$RunArguments = @()) {
    if ($Only -and $Name -notin $Only) { return }
    $probePath = Join-Path $runPath $Name
    New-Item -ItemType Directory -Path $probePath -Force | Out-Null
    Push-Location $probePath
    try {
        $arguments = @('/nologo', '/MD', '/O2', '/W3', '/D_CRT_SECURE_NO_WARNINGS', '/D_WIN32_WINNT=0x0A00') + $Extra + $Sources + @("/Fe:$Name.exe")
        ($arguments | ConvertTo-Json) | Set-Content command.json -Encoding utf8
        # Windows PowerShell wraps native stderr in ErrorRecords; inspect the exit code.
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & cl.exe @arguments *> build.log
        $buildExit = $LASTEXITCODE
        $ErrorActionPreference = $previousPreference
        $runExit = $null
        $machine = $null
        if ($buildExit -eq 0) {
            & dumpbin.exe /headers "$Name.exe" *> headers.log
            & dumpbin.exe /dependents "$Name.exe" *> dependents.log
            $machine = ((Select-String -Path headers.log -Pattern 'machine \(' | Select-Object -First 1).Line).Trim()
            if ($canRun) {
                & (Join-Path $probePath "$Name.exe") @RunArguments *> run.log
                $runExit = $LASTEXITCODE
            }
        }
        $results.Add([pscustomobject]@{name=$Name; buildExit=$buildExit; runExit=$runExit; machine=$machine; logs=$probePath})
        Write-Output "$Name build=$buildExit run=$runExit machine=$machine"
    } finally { Pop-Location }
}

$sourceRoot = Join-Path $repoRoot 'src'
$probes = Join-Path $PSScriptRoot 'probes'
if ($Only -contains 'mfc') {
    Invoke-Probe 'mfc' @((Join-Path $probes 'mfc_probe.cpp')) @('/EHsc', '/std:c++17', '/D_AFXDLL', '/D_UNICODE', '/DUNICODE', 'oleaut32.lib')
}
Invoke-Probe 'windows' @((Join-Path $probes 'windows_probe.cpp')) @('/EHsc', '/std:c++17', 'icu.lib')
Invoke-Probe 'sqlite' @((Join-Path $sourceRoot 'sqlite\sqlite3mc_amalgamation.c'), (Join-Path $probes 'sqlite_probe.c')) @('/TC', "/I$sourceRoot\sqlite") @((Join-Path $runPath 'sqlite-audit.db'))
$tiny = @('tinystr.cpp','tinyxml.cpp','tinyxmlerror.cpp','tinyxmlparser.cpp') | ForEach-Object { Join-Path $sourceRoot "TinyXml\$_" }
Invoke-Probe 'tinyxml' ($tiny + (Join-Path $probes 'tinyxml_probe.cpp')) @('/EHsc', '/std:c++17', "/I$sourceRoot\TinyXml")
$qr = @('bitstream.c','mask.c','mmask.c','mqrspec.c','qrencode.c','qrinput.c','qrspec.c','rscode.c','rsecc.c','split.c') | ForEach-Object { Join-Path $sourceRoot "QRCode\$_" }
Invoke-Probe 'qrencode' ($qr + (Join-Path $probes 'qr_probe.c')) @('/TC', '/DHAVE_CONFIG_H=1', "/I$sourceRoot\QRCode")
if (!$SkipChaiScript) { Invoke-Probe 'chaiscript' @((Join-Path $probes 'chaiscript_probe.cpp')) @('/EHsc', '/std:c++17', '/bigobj', "/I$sourceRoot") }
ConvertTo-Json -InputObject @($results.ToArray()) -Depth 3 | Set-Content (Join-Path $runPath 'results.json') -Encoding utf8
Write-Output "Results saved to $runPath"
if ($results | Where-Object { $_.buildExit -ne 0 -or ($null -ne $_.runExit -and $_.runExit -ne 0) }) { exit 1 }
