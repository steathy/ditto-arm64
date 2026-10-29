param([Parameter(Mandatory=$true)][string]$PackagePath, [string]$VisualStudioPath)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$PackagePath = (Resolve-Path -LiteralPath $PackagePath).Path
if (!(Test-Path -LiteralPath (Join-Path $PackagePath 'build-info.json'))) { throw 'Use a package created by Package-Portable.ps1.' }
if (!$VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VisualStudioPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath
}
if (!(Get-CimInstance Win32_Processor | Where-Object Architecture -eq 12)) { throw 'Runtime smoke tests require Windows on ARM64.' }
$sqliteObject = Join-Path $repoRoot 'ARM64\Release\obj\sqlite3mc_amalgamation.obj'
if (!(Test-Path -LiteralPath $sqliteObject)) { throw 'Build Release ARM64 before running the database integration probe.' }
$testPath = Join-Path $repoRoot ('work\portable-test-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $testPath | Out-Null
Copy-Item -Path (Join-Path $PackagePath '*') -Destination $testPath -Recurse
$database = Join-Path $testPath 'test.db'
@('[Ditto]','ConnectedToClipboard=0','DisableRecieve=1','DittoHotKey=0','DittoHotKey2=0','DittoHotKey3=0','EnableDebugLogging=0','CheckForUpdates=0',"DBPath3=$database") | Set-Content -LiteralPath (Join-Path $testPath 'Ditto.Settings') -Encoding Unicode
$executable = Join-Path $testPath 'Ditto.exe'
$process = Start-Process -FilePath $executable -WorkingDirectory $testPath -WindowStyle Hidden -PassThru
try {
    $null = $process.WaitForInputIdle(10000)
    $requiredModules = @('ICU_Loader.dll','DittoUtil.dll','mfc140u.dll','msvcp140.dll','vcruntime140.dll')
    $deadline = (Get-Date).AddSeconds(10)
    do {
        $process.Refresh()
        if ($process.HasExited) { throw "Ditto exited during startup: $($process.ExitCode)" }
        $modules = @($process.Modules | Where-Object { $_.ModuleName -in $requiredModules })
        $ready = (Test-Path -LiteralPath $database) -and $modules.Count -eq $requiredModules.Count
        if (!$ready) { Start-Sleep -Milliseconds 100 }
    } until ($ready -or (Get-Date) -gt $deadline)
    if (!$ready) { throw 'Ditto did not create its database and load the expected modules.' }
    foreach ($module in $modules) {
        if (!$module.FileName.StartsWith($testPath + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Runtime was loaded outside the portable package: $($module.FileName)" }
    }
    $loaded = @($modules | ForEach-Object { $_.ModuleName })
    foreach ($argument in @('/Open','/Close')) {
        $command = Start-Process -FilePath $executable -ArgumentList $argument -WorkingDirectory $testPath -WindowStyle Hidden -Wait -PassThru
        if ($command.ExitCode) { throw "Ditto command $argument failed: $($command.ExitCode)" }
    }
    $process.Refresh()
    if ($process.HasExited -or !$process.Responding) { throw 'Ditto stopped responding during the CLI smoke test.' }
} finally {
    if (!$process.HasExited) {
        Start-Process -FilePath $executable -ArgumentList '/Exit' -WorkingDirectory $testPath -WindowStyle Hidden -Wait | Out-Null
        if (!$process.WaitForExit(10000)) { throw "Test instance did not shut down: PID $($process.Id)" }
    }
}
$environmentScript = Join-Path $testPath 'compiler-environment.cmd'
$vsDevCmd = Join-Path $VisualStudioPath 'Common7\Tools\VsDevCmd.bat'
@('@echo off', ('call "{0}" -no_logo -arch=arm64 -host_arch=arm64 >nul' -f $vsDevCmd), 'if errorlevel 1 exit /b %errorlevel%', 'set') | Set-Content $environmentScript -Encoding ascii
$environmentLines = & $environmentScript
if ($LASTEXITCODE) { throw 'Compiler environment initialization failed.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process') }
}
Push-Location $testPath
try {
    & cl.exe /nologo /MD /O2 /W3 "/I$repoRoot\src\sqlite" "$PSScriptRoot\probes\database_probe.c" $sqliteObject /Fe:database_probe.exe *> database-build.log
    if ($LASTEXITCODE) { throw "Database probe compilation failed. See $testPath\database-build.log" }
    & .\database_probe.exe $database (Join-Path $testPath 'ICU_Loader.dll') *> database-run.log
    if ($LASTEXITCODE) { throw "Database probe failed. See $testPath\database-run.log" }
    Get-Content database-run.log
} finally { Pop-Location }
$result = [ordered]@{passed=$true;package=$PackagePath;testDirectory=$testPath;loadedFromPackage=$loaded;startup=$true;openCloseCommands=$true;cleanShutdown=$true;databaseIntegrity=$true;capturedClipCount=0;icuRegex=$true;icuUnicodeCaseConversion=$true;visualUiInspection=$false}
$result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $testPath 'smoke-results.json') -Encoding utf8
Write-Output "PASS: isolated startup, bundled module loading, open/close commands and clean shutdown. Results: $testPath"
