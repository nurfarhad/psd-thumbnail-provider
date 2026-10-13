@echo off
setlocal
cd /d "%~dp0"
echo ===================================================
echo   Uninstalling PSD & AI Thumbnail Provider
echo ===================================================
echo.
bin\psdthumb-diag.exe /unregister "%~dp0bin\PsdThumbnailProvider.dll"
echo.
bin\psdthumb-diag.exe /status
echo.
echo Uninstallation complete! All registry associations removed.
pause
