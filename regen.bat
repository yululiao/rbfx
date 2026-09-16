@echo off
rem Re-run CMake configure so GLOB'ed source lists pick up added/removed files.
rem The Editor and other targets use GLOB without CONFIGURE_DEPENDS, so the build
rem tree must be re-configured after creating or deleting source files.
rem
rem Usage:
rem   regen.bat                     re-configure only (this is what a double-click runs)
rem   regen.bat build               re-configure, then build Editor (Release)
rem   regen.bat build <target>      re-configure, then build <target> (Release)
rem   regen.bat build <target> <config>
rem                                 e.g. regen.bat build AssetImporter Debug

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

if /i not "%~1"=="build" (
    echo Done. Build tree is up to date.
    goto end
)

set TARGET=%~2
if "%TARGET%"=="" set TARGET=Editor
set CONFIG=%~3
if "%CONFIG%"=="" set CONFIG=Release

echo [2/2] Building %TARGET% [%CONFIG%] ...
cmake --build msvc --config %CONFIG% --target %TARGET%
set EXITCODE=%errorlevel%

:end
rem Keep the console open so a double-click launch shows the result before closing.
pause
exit /b %EXITCODE%
