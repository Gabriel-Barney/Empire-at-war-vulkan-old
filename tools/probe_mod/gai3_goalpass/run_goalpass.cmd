@echo off
rem gai3 t06 (BKG1_RE_ANSWERS B.8): per-candidate trace of the galactic AI goal passes in retail FoC. A debugger
rem (goalpass_dbg.py: hardware EXECUTE breakpoints at the targeted / global evaluation and the pass Commit; the game
rem is only read) waits up to 30 min for StarWarsG.exe, attaches, arms at once and stops by itself at galactic mode
rem frame 700, when out\stop appears or when the game exits. Vanilla game, no mod. Writes out\<yyyymmdd-hhmmss>-goalpass\
rem (trace.log, report.txt, passes.tsv, evals.tsv) and out\latest.txt. With --non-interactive: no pause, console in
rem out\run_goalpass.log (start it with Start-Process before the game). Other arguments go to goalpass_dbg.py
rem (--stop-galactic-frame N, --arm-file PATH, --label NAME, --wait-process-sec N). Steps: README.md.
setlocal
if not exist "%~dp0out" mkdir "%~dp0out"
echo %* | find /i "--non-interactive" >nul && goto :detached
call :main %*
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
:detached
call :main %* > "%~dp0out\run_goalpass.log" 2>&1
exit /b %ERRORLEVEL%
:main
"%~dp0..\..\..\toolchain\venv\Scripts\python.exe" -I "%~dp0goalpass_dbg.py" --out-dir "%~dp0out" --wait-process-sec 1800 %*
exit /b %ERRORLEVEL%
