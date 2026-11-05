@echo off
rem agentsim portable rebuild — no hardcoded absolute paths.
rem Usage: tools\rebuild.bat   (from any CWD; paths derived from %~dp0)
setlocal
set "ROOT=%~dp0.."
pushd "%ROOT%"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo vcvars64.bat not found & exit /b 1)
if not exist build mkdir build
if not exist build\obj mkdir build\obj
cl /nologo /std:c++17 /W4 /WX /EHsc /permissive- /utf-8 /I src /DAGENTSIM_HAS_GENOME=1 @build\sources.rsp /Fe:build\agentsim.exe /Fo:build\obj\
if errorlevel 1 goto :fail
cl /nologo /std:c++17 /W4 /WX /EHsc /permissive- /utf-8 /I src /DAGENTSIM_HAS_GENOME=1 @build\tests.rsp /Fe:build\unit_tests.exe /Fo:build\obj\
if errorlevel 1 goto :fail
cl /nologo /std:c++17 /W4 /WX /EHsc /permissive- /utf-8 /Fe:build\agentsimw.exe /Fo:build\obj_launcher\ src\app\Launcher.cpp /link /SUBSYSTEM:WINDOWS
if errorlevel 1 goto :fail
echo build OK
popd
exit /b 0
:fail
echo build FAILED
popd
exit /b 1