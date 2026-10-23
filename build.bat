@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo ===================================================
echo   Building PSD Thumbnail Provider (64-bit)
echo ===================================================
echo.

if not exist bin mkdir bin

:: Find clang++ and windres
set "CLANG="
set "WINDRES="
where clang++.exe >nul 2>nul
if %errorlevel% equ 0 (
    set "CLANG=clang++.exe"
    where llvm-windres.exe >nul 2>nul && set "WINDRES=llvm-windres.exe"
    if "!WINDRES!"=="" where windres.exe >nul 2>nul && set "WINDRES=windres.exe"
) else (
    for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT*") do (
        for /d %%S in ("%%D\llvm-mingw*") do (
            if exist "%%S\bin\clang++.exe" (
                set "CLANG=%%S\bin\clang++.exe"
                if exist "%%S\bin\llvm-windres.exe" (
                    set "WINDRES=%%S\bin\llvm-windres.exe"
                ) else if exist "%%S\bin\windres.exe" (
                    set "WINDRES=%%S\bin\windres.exe"
                )
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

set "RES_OBJ="
if not "%WINDRES%"=="" (
    echo Compiling version resource...
    "%WINDRES%" -i src\shellext\version.rc -o bin\version.o
    if %errorlevel% equ 0 set "RES_OBJ=bin\version.o"
)

echo [1/2] Compiling PsdThumbnailProvider.dll (Unicode)...
"%CLANG%" -shared -std=c++20 -O2 -DUNICODE -D_UNICODE -DNOMINMAX ^
    src\core\psd_decoder.cpp ^
    src\shellext\thumbnail_provider.cpp ^
    src\shellext\dllmain.cpp ^
    !RES_OBJ! ^
    -o bin\PsdThumbnailProvider.dll ^
    -lole32 -loleaut32 -lshlwapi -lwindowscodecs -lgdi32 -luser32 -ladvapi32 -lshell32 -luuid -lcrypt32 -static
if %errorlevel% neq 0 (
    echo [ERROR] Failed to compile DLL!
    exit /b %errorlevel%
)
echo      -^> bin\PsdThumbnailProvider.dll created successfully.

echo.
echo [2/2] Compiling psdthumb-diag.exe (Unicode)...
"%CLANG%" -std=c++20 -O2 -municode -DUNICODE -D_UNICODE -DNOMINMAX ^
    src\core\psd_decoder.cpp ^
    src\diag\main.cpp ^
    -o bin\psdthumb-diag.exe ^
    -lole32 -loleaut32 -lshlwapi -lwindowscodecs -lgdi32 -luser32 -ladvapi32 -lshell32 -luuid -lcrypt32 -static
if %errorlevel% neq 0 (
    echo [ERROR] Failed to compile diagnostic tool!
    exit /b %errorlevel%
)
echo      -^> bin\psdthumb-diag.exe created successfully.

if exist bin\version.o del bin\version.o

echo.
echo ===================================================
echo   Build Successful!
echo ===================================================
