param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $root
New-Item -ItemType Directory -Force -Path 'build/obj','build/test-obj' | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer not found.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC x64 toolchain not found.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw 'vcvars64.bat not found.' }
$sources = Get-ChildItem -LiteralPath 'src' -Filter '*.cpp' -Recurse | Where-Object { $_.FullName -notlike '*\app\*' } | ForEach-Object { $_.FullName.Substring($root.Length + 1) }
$appSources = Get-ChildItem -LiteralPath 'src/app' -Filter '*.cpp' | ForEach-Object { $_.FullName.Substring($root.Length + 1) }
$testSources = Get-ChildItem -LiteralPath 'tests/unit' -Filter '*.cpp' | ForEach-Object { $_.FullName.Substring($root.Length + 1) }
if ($sources.Count -eq 0 -or $appSources.Count -eq 0) { throw 'src sources are incomplete.' }
$flags = @('/std:c++17','/EHsc','/W4','/WX','/permissive-','/utf-8','/nologo','/fp:precise')
if ($Configuration -eq 'Debug') { $flags += @('/Od','/Zi','/RTC1') } else { $flags += @('/O2','/DNDEBUG') }
@($flags) + @('/Isrc','/I.','/Febuild\simulator.exe','/Fobuild\obj\') + $sources + $appSources | Set-Content -LiteralPath 'build/simulator.rsp' -Encoding ascii
@($flags) + @('/Isrc','/I.','/Febuild\unit_tests.exe','/Fobuild\test-obj\') + $sources + $testSources | Set-Content -LiteralPath 'build/unit_tests.rsp' -Encoding ascii
$cmd = 'call "' + $vcvars + '" >nul && cl @build\simulator.rsp && cl @build\unit_tests.rsp'
& cmd.exe /d /s /c $cmd
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
Write-Host 'Built build/simulator.exe and build/unit_tests.exe.'
