@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo ===================================================
echo   Building PSD Thumbnail Provider (64-bit)
echo ===================================================
echo.

if not exist bin mkdir bin

:: Find clang++
set "CLANG="
where clang++.exe >nul 2>nul
if %errorlevel% equ 0 (
    set "CLANG=clang++.exe"
) else (
    for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT*") do (
        for /d %%S in ("%%D\llvm-mingw*") do (
            if exist "%%S\bin\clang++.exe" (
                set "CLANG=%%S\bin\clang++.exe"
            )
        )
    )
)

if "%CLANG%"=="" (
    echo [ERROR] clang++ not found! Please install LLVM-MinGW or run via developer prompt.
    exit /b 1
)

echo Using compiler: %CLANG%
echo.

echo [1/2] Compiling PsdThumbnailProvider.dll...
"%CLANG%" -shared -std=c++20 -O2 ^
    src\core\psd_decoder.cpp ^
    src\shellext\thumbnail_provider.cpp ^
    src\shellext\dllmain.cpp ^
    src\shellext\psd_thumb.def ^
    -o bin\PsdThumbnailProvider.dll ^
    -lole32 -loleaut32 -lshlwapi -lwindowscodecs -lgdi32 -luuid -lcrypt32 -static
if %errorlevel% neq 0 (
    echo [ERROR] Failed to compile DLL!
    exit /b %errorlevel%
)
echo      -^> bin\PsdThumbnailProvider.dll created successfully.

echo.
echo [2/2] Compiling psdthumb-diag.exe...
"%CLANG%" -std=c++20 -O2 -municode ^
    src\core\psd_decoder.cpp ^
    src\diag\main.cpp ^
    -o bin\psdthumb-diag.exe ^
    -lole32 -loleaut32 -lshlwapi -lwindowscodecs -lgdi32 -luuid -lcrypt32 -static
if %errorlevel% neq 0 (
    echo [ERROR] Failed to compile diagnostic tool!
    exit /b %errorlevel%
)
echo      -^> bin\psdthumb-diag.exe created successfully.

echo.
echo ===================================================
echo   Build Successful!
echo ===================================================
