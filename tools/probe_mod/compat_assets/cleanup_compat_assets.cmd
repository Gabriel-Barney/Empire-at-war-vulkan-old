@echo off
rem Removes Mods\CompatAssetsProbe from the game (only if it carries CAP_PROBE_MARKER.txt) and restores the player's
rem profile from the newest run's snapshot (not while the game runs). Safe to repeat; every run ends with the same
rem cleanup. Arguments: --game DIR, --run-dir DIR, --dry-run, --no-profile, --no-pause.
"%~dp0..\..\..\toolchain\venv\Scripts\python.exe" -I "%~dp0compat_assets_cleanup.py" %*
set "RC=%ERRORLEVEL%"
echo %* | find /i "--no-pause" >nul && exit /b %RC%
echo.
pause
exit /b %RC%
