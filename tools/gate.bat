@echo off
rem agentsim portable gate — full check.ps1, output to build\gate_out.txt
setlocal
set "ROOT=%~dp0.."
pushd "%ROOT%"
if not exist build mkdir build
powershell -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1 > build\gate_out.txt 2>&1
set "EC=%ERRORLEVEL%"
popd
echo gate exit=%EC% — see build\gate_out.txt
exit /b %EC%