# agentsim - tools/build.ps1 : build agentsim.exe (+ unit_tests.exe if tests exist).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { Write-Error "vcvars64.bat not found"; exit 1 }
Write-Host "vcvars: $vcvars"

New-Item -ItemType Directory -Force -Path "$root\build" | Out-Null

$cpps = Get-ChildItem -Path "$root\src" -Recurse -Filter *.cpp |
    ForEach-Object { $_.FullName.Substring($root.Length + 1) }
$libCpps = $cpps | Where-Object { $_ -notmatch "^src\\app\\" }
if (-not $cpps) { Write-Error "no .cpp under src"; exit 1 }

$flagList = @("/nologo","/std:c++17","/W4","/WX","/EHsc","/permissive-","/utf-8","/I","src","/DAGENTSIM_HAS_GENOME=1")
$clArgs = $flagList -join " "
Write-Host "flags: $clArgs"

$rspMain = Join-Path $root "build\\sources.rsp"
$exeMain = Join-Path $root "build\\agentsim.exe"
$objDir  = Join-Path $root "build\\"
$cpps | Set-Content -Path $rspMain -Encoding ascii

$cmd = "call `"$vcvars`" >nul && cl $clArgs `"@$rspMain`" /Fe:`"$exeMain`" /Fo:`"$objDir`""
Write-Host "cl agentsim.exe ..."
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { Write-Error "cl failed for agentsim.exe (exit $LASTEXITCODE)"; exit $LASTEXITCODE }

if (Test-Path "$root\tests\unit") {
    $testCpps = Get-ChildItem -Path "$root\tests\unit" -Filter *.cpp |
        ForEach-Object { $_.FullName.Substring($root.Length + 1) }
    if ($testCpps.Count -gt 0) {
        $rspTest = Join-Path $root "build\\tests.rsp"
        $exeTest = Join-Path $root "build\\unit_tests.exe"
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
    New-Item -ItemType Directory -Force -Path "$root\build\obj_launcher" | Out-Null
    $cmdL = "call `"$vcvars`" >nul && cl /nologo /std:c++17 /W4 /WX /EHsc /permissive- /utf-8 /Fe:`"$root\build\agentsimw.exe`" /Fo:`"$root\build\obj_launcher\launcher.obj`" `"$launcherSrc`" /link /SUBSYSTEM:WINDOWS"
    cmd /c $cmdL
    if ($LASTEXITCODE -ne 0) { Write-Error "cl failed for agentsimw.exe"; exit $LASTEXITCODE }
}
Write-Host "build OK"
exit 0
