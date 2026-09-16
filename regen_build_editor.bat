@echo off
rem Double-click variant of regen.bat: re-configure, then build Editor (Release).
rem Re-run CMake configure first so GLOB'ed source lists pick up added/removed files,
rem then build the Editor target. Pass a target or config on the command line to
rem override, e.g. regen_build.bat AssetImporter Debug.

setlocal
cd /d "%~dp0"
set EXITCODE=0

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cmake not found in PATH. Install CMake or add it to PATH.
    set EXITCODE=1
    goto end
)

echo [1/2] Re-configuring build tree "msvc" ...
cmake -S . -B msvc
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    set EXITCODE=1
    goto end
)

set TARGET=%~1
if "%TARGET%"=="" set TARGET=Editor
set CONFIG=%~2
if "%CONFIG%"=="" set CONFIG=Release

echo [2/2] Building %TARGET% [%CONFIG%] ...
cmake --build msvc --config %CONFIG% --target %TARGET%
set EXITCODE=%errorlevel%

:end
rem Keep the console open so a double-click launch shows the result before closing.
pause
exit /b %EXITCODE%
