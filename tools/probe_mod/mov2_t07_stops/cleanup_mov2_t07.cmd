@echo off
rem Removes Mods\Mov2T07Probe (only a folder with MOV2T07_PROBE_MARKER.txt) and restores the profile of the last run.
rem Safe to repeat. Arguments: --game DIR, --dry-run, --run-dir DIR, --no-profile, --no-pause.
"%~dp0..\..\..\toolchain\venv\Scripts\python.exe" -I "%~dp0mov2_t07_cleanup.py" %*
exit /b %ERRORLEVEL%
