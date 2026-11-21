@echo off
rem compat_assets probe (roadmap mcx2 t02, docs/MOD_COMPAT_DESIGN.md sec. 10.2): what retail draws for broken models,
rem animations and particles. Backs up the profile, builds Mods\CompatAssetsProbe from your install, launches FoC (or
rem waits with --no-launch), scans the probe records, screenshots each slot, writes out\<run>\report.txt, cleans up.
rem Steps: README.md. Arguments are passed on (--non-interactive, --no-launch, --windowed, --dry-run, --no-profile,
rem --game DIR, --only KEYS, --skip KEYS, --report-only RECORDS.log).
"%~dp0..\..\..\toolchain\venv\Scripts\python.exe" -I "%~dp0compat_assets_run.py" %*
set "RC=%ERRORLEVEL%"
echo %* | find /i "--non-interactive" >nul && exit /b %RC%
echo %* | find /i "--dry-run" >nul && exit /b %RC%
echo.
pause
exit /b %RC%
