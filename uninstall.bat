@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo ===================================================
echo   Uninstalling PSD & AI Thumbnail Provider
echo ===================================================
echo.

if not exist "bin\psdthumb-diag.exe" (
    echo [ERROR] bin\psdthumb-diag.exe not found!
    pause
    exit /b 1
)

echo Unregistering Shell Extension...
bin\psdthumb-diag.exe /unregister "%~dp0bin\PsdThumbnailProvider.dll"
set "UNREG_ERR=%errorlevel%"

if %UNREG_ERR% neq 0 (
    echo.
    echo [ERROR] Unregistration failed with exit code %UNREG_ERR%!
    pause
    exit /b %UNREG_ERR%
)

echo.
echo Live Status after removal:
bin\psdthumb-diag.exe /status

echo.
echo ===================================================
echo   Uninstallation complete!
echo   Associations cleanly removed/restored.
echo ===================================================
pause
