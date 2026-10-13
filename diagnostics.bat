@echo off
setlocal
cd /d "%~dp0"
bin\psdthumb-diag.exe /status
echo.
pause
