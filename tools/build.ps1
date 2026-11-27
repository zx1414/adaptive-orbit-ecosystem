# agentsim - tools/build.ps1 : build agentsim.exe (+ unit_tests.exe if tests exist).
param([string]$OutputDirectory = 'build')
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$version = (Get-Content -LiteralPath VERSION -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid project version: $version" }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer was not found.' }
$installation = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Install the C++ x64 build tools for Visual Studio 2022.' }
$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw 'vcvars64.bat not found.' }
Write-Host "vcvars: $vcvars"

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).ProviderPath

$cpps = Get-ChildItem -Path "$root\src" -Recurse -Filter *.cpp |
    Where-Object { $_.Name -ne 'Launcher.cpp' } |
    ForEach-Object { $_.FullName.Substring($root.Length + 1) }
$libCpps = $cpps | Where-Object { $_ -notmatch "^src\\app\\" }
if (-not $cpps) { Write-Error "no .cpp under src"; exit 1 }

$flagList = @("/nologo","/std:c++17","/W4","/WX","/EHsc","/permissive-","/utf-8","/I","src","/DAGENTSIM_HAS_GENOME=1","/DAGENTSIM_VERSION=$version")
$clArgs = $flagList -join " "
Write-Host "flags: $clArgs"

$rspMain = Join-Path $outputRoot "sources.rsp"
$exeMain = Join-Path $outputRoot "agentsim.exe"
$objDir = "$outputRoot\\"
$cpps | Set-Content -Path $rspMain -Encoding ascii

$cmd = "call `"$vcvars`" >nul && cl $clArgs `"@$rspMain`" /Fe:`"$exeMain`" /Fo:`"$objDir`""
Write-Host "cl agentsim.exe ..."
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { Write-Error "cl failed for agentsim.exe (exit $LASTEXITCODE)"; exit $LASTEXITCODE }

if (Test-Path "$root\tests\unit") {
    $testCpps = Get-ChildItem -Path "$root\tests\unit" -Filter *.cpp |
        ForEach-Object { $_.FullName.Substring($root.Length + 1) }
    if ($testCpps.Count -gt 0) {
        $rspTest = Join-Path $outputRoot "tests.rsp"
        $exeTest = Join-Path $outputRoot "unit_tests.exe"
        ($testCpps + $libCpps) | Set-Content -Path $rspTest -Encoding ascii
        $cmd2 = "call `"$vcvars`" >nul && cl $clArgs `"@$rspTest`" /Fe:`"$exeTest`" /Fo:`"$objDir`""
        Write-Host "cl unit_tests.exe ..."
        cmd /c $cmd2
        if ($LASTEXITCODE -ne 0) { Write-Error "cl failed for unit_tests.exe"; exit $LASTEXITCODE }
    }
}

# R10: no-console launcher (WinMain shell; separate subsystem, never joins sources.rsp)
$launcherSrc = Join-Path $root "src\app\Launcher.cpp"
if (Test-Path $launcherSrc) {
    New-Item -ItemType Directory -Force -Path "$outputRoot\obj_launcher" | Out-Null
    $cmdL = "call `"$vcvars`" >nul && cl /nologo /std:c++17 /W4 /WX /EHsc /permissive- /utf-8 /DAGENTSIM_VERSION=$version /Fe:`"$outputRoot\agentsimw.exe`" /Fo:`"$outputRoot\obj_launcher\launcher.obj`" `"$launcherSrc`" /link /SUBSYSTEM:WINDOWS"
    cmd /c $cmdL
    if ($LASTEXITCODE -ne 0) { Write-Error "cl failed for agentsimw.exe"; exit $LASTEXITCODE }
}
Write-Host "build OK"
exit 0
