@echo off
chcp 65001 >nul
cd /d "%~dp0"
simulator.exe --serve %*
echo.
echo If the browser did not open automatically, visit the URL printed above.
pause
