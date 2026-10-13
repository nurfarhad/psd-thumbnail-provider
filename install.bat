@echo off
setlocal
cd /d "%~dp0"
echo ===================================================
echo   Installing PSD & AI Native Thumbnail Provider
echo ===================================================
echo.
bin\psdthumb-diag.exe /register "%~dp0bin\PsdThumbnailProvider.dll"
echo.
bin\psdthumb-diag.exe /status
echo.
echo Installation complete! Explorer thumbnails are now active.
pause
