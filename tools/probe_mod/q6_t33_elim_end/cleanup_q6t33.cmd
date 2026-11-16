@echo off
rem Removes Mods\Q6T33Probe from the game (only a folder that carries Q6T33_PROBE_MARKER.txt). Safe to repeat; the same
rem cleanup runs by itself at the end of every run. Arguments: --game DIR, --dry-run, --no-pause.
"%~dp0..\..\..\toolchain\venv\Scripts\python.exe" -I "%~dp0q6t33_cleanup.py" %*
exit /b %ERRORLEVEL%
