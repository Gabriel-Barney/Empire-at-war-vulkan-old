@echo off
rem gai3 goalpass cleanup. The kit installs no mod, so nothing in the game folder is removed. If a goalpass debugger
rem is still running (out\running.pid), this writes out\stop: the debugger clears its breakpoints in every thread,
rem detaches (the game keeps running) and parses its log. Safe to repeat. --no-pause: no pause at the end.
"%~dp0..\..\..\toolchain\venv\Scripts\python.exe" -I "%~dp0goalpass_dbg.py" --cleanup --out-dir "%~dp0out"
set "RC=%ERRORLEVEL%"
echo %* | find /i "--no-pause" >nul && exit /b %RC%
echo.
pause
exit /b %RC%
