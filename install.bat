@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo ===================================================
echo   Installing PSD & AI Native Thumbnail Provider
echo ===================================================
echo.

if not exist "bin\PsdThumbnailProvider.dll" (
    echo [ERROR] bin\PsdThumbnailProvider.dll not found!
    echo If building from source, please run build.bat first.
    echo If using a binary release, please ensure the bin\ folder is present.
    echo.
    pause
    exit /b 1
)

if not exist "bin\psdthumb-diag.exe" (
    echo [ERROR] bin\psdthumb-diag.exe not found!
    echo If building from source, please run build.bat first.
    echo.
    pause
    exit /b 1
)

echo Registering 64-bit Shell COM Extension...
bin\psdthumb-diag.exe /register "%~dp0bin\PsdThumbnailProvider.dll"
set "REG_ERR=%errorlevel%"

if %REG_ERR% neq 0 (
    echo.
    echo [ERROR] Registration failed with exit code %REG_ERR%!
    echo Please verify permissions or run as administrator if installing system-wide.
    echo.
    pause
    exit /b %REG_ERR%
)

echo.
echo Checking live registration status...
bin\psdthumb-diag.exe /status

echo.
echo ===================================================
echo   Installation Successful!
echo   Thumbnails are now active in Windows Explorer.
echo ===================================================
pause
