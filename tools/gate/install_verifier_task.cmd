@echo off
rem Registers (or replaces) the per-user logon task AlamoVerifier that runs the verifier (wr2 t02, tools\gate\verifier.py):
rem the one process that advances dev from the landing queue (tools\gate\land_queue.py submit). No admin; it runs while you
rem are logged on. Usage: tools\gate\install_verifier_task.cmd [--no-register]
rem It copies verifier.py and land_queue.py to <main checkout>\logs\verifier_task\ (so a checkout or merge never changes
rem the running code; logs\ is gitignored) and writes verifier_task.cmd beside them, which logs to verifier_log.txt there.
rem Never under %LOCALAPPDATA% / %APPDATA% (the Claude app redirects those writes; docs/BUILD.md, "Scheduled gate").
rem Rerun after verifier.py or land_queue.py change. Remove with: schtasks /Delete /TN AlamoVerifier /F
rem Start it now without logging off: schtasks /Run /TN AlamoVerifier
setlocal
set "HERE=%~dp0"
set "REG=1"
if /i "%~1"=="--no-register" set "REG="
set "GCD="
for /f "delims=" %%G in ('git -C "%HERE%..\.." rev-parse --path-format^=absolute --git-common-dir') do set "GCD=%%G"
if not defined GCD (echo install_verifier_task: not inside a checkout of the repository.& exit /b 2)
for %%I in ("%GCD%\..") do set "MAIN=%%~fI"
set "DEST=%MAIN%\logs\verifier_task"
if not exist "%DEST%" mkdir "%DEST%"
copy /y "%HERE%verifier.py" "%DEST%\verifier.py" >nul
if errorlevel 1 (echo install_verifier_task: copy to %DEST% failed.& exit /b 1)
copy /y "%HERE%land_queue.py" "%DEST%\land_queue.py" >nul
set "PYX=%MAIN%\toolchain\venv\Scripts\python.exe"
> "%DEST%\verifier_task.cmd" (
  echo @echo off
  echo rem Written by tools\gate\install_verifier_task.cmd for the AlamoVerifier task; rerun the installer instead of editing it.
  echo echo %%DATE%% %%TIME%% verifier start: "%MAIN%" ^>^> "%%~dp0verifier_log.txt"
  echo "%PYX%" -I "%%~dp0verifier.py" run --main "%MAIN%" ^>^> "%%~dp0verifier_log.txt" 2^>^&1
  echo echo %%DATE%% %%TIME%% verifier exit %%ERRORLEVEL%% ^>^> "%%~dp0verifier_log.txt"
)
if errorlevel 1 (echo install_verifier_task: writing %DEST%\verifier_task.cmd failed.& exit /b 1)
echo install_verifier_task: %DEST%\verifier_task.cmd runs the verifier for %MAIN%; log %DEST%\verifier_log.txt
if not defined REG exit /b 0
schtasks /Create /F /SC ONLOGON /TN AlamoVerifier /TR "\"%DEST%\verifier_task.cmd\""
exit /b %ERRORLEVEL%
