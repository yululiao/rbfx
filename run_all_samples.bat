@echo off
REM ============================================================================
REM  Full windowed smoke of EVERY Lua sample under Source\LuaSamples.
REM  Human-triggered only. The gen-lua-binding skill does NOT call this -- it
REM  smokes just the samples it changed (run_samples.ps1 -Only <name>).
REM
REM  There is no sample list to edit: run_samples.ps1 re-scans
REM  Source\LuaSamples\*\main.lua on every run, so a newly ported sample is
REM  already part of this suite the moment its directory exists.
REM
REM  Pass-through args (optional):  run_all_samples.bat -Config Debug
REM  Exit code 0 = all passed, 1 = at least one failed.
REM ============================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Source\Tools\LuaBindingCI\run_samples.ps1" %*
echo.
pause
