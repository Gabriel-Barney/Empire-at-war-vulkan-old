@echo off
rem Registers (or replaces) a per-user nightly Task Scheduler entry that gates origin/dev (tools\gate_scheduled.cmd).
rem No admin needed; it runs while you are logged on. Usage: tools\install_gate_task.cmd [HH:MM] [--no-register]
rem (default 03:00). It copies gate_scheduled.cmd to <main checkout>\logs\gate_task\ (so a checkout or merge never changes
rem the script while it runs; logs\ is gitignored) and writes gate_task.cmd next to it: a launcher with the main checkout
rem baked in, which logs each run to gate_last_run.txt there (start line first, then everything the gate prints, then the
rem exit code). Never put these files under %LOCALAPPDATA% or %APPDATA%: a command run from the Claude desktop app (an
rem MSIX package) has its AppData writes redirected to AppData\Local\Packages\Claude_*\LocalCache\..., which Task
rem Scheduler does not see, so the task found no script and exited 1 within a second every night from 2026-11-12 (ref6
rem t07, confirmed 2026-11-20). Run it from any checkout; rerun it after gate_scheduled.cmd changes. --no-register writes
rem the files only.
rem ALAMO_GATE_DEST overrides the folder (for testing). Remove the task with: schtasks /Delete /TN AlamoGateNightly /F
setlocal
rem SHIFT below also moves %0, so the script's folder is kept first.
set "HERE=%~dp0"
set "T=03:00"
set "REG=1"
:args
if "%~1"=="" goto args_done
if /i "%~1"=="--no-register" (set "REG=") else set "T=%~1"
shift
goto args
:args_done
set "GCD="
for /f "delims=" %%G in ('git -C "%HERE%.." rev-parse --path-format^=absolute --git-common-dir') do set "GCD=%%G"
if not defined GCD (echo install_gate_task: not inside a checkout of the repository.& exit /b 2)
for %%I in ("%GCD%\..") do set "MAIN=%%~fI"
set "DEST=%MAIN%\logs\gate_task"
if defined ALAMO_GATE_DEST set "DEST=%ALAMO_GATE_DEST%"
if not exist "%DEST%" mkdir "%DEST%"
copy /y "%HERE%gate_scheduled.cmd" "%DEST%\gate_scheduled.cmd" >nul
if errorlevel 1 (echo install_gate_task: copy to %DEST% failed.& exit /b 1)
> "%DEST%\gate_task.cmd" (
  echo @echo off
  echo rem Written by tools\install_gate_task.cmd for the AlamoGateNightly task; rerun the installer instead of editing it.
  echo echo %%DATE%% %%TIME%% gate_task start: "%MAIN%" ^> "%%~dp0gate_last_run.txt"
  echo call "%%~dp0gate_scheduled.cmd" "%MAIN%" ^>^> "%%~dp0gate_last_run.txt" 2^>^&1
  echo set RC=%%ERRORLEVEL%%
  echo echo %%DATE%% %%TIME%% gate_task exit %%RC%% ^>^> "%%~dp0gate_last_run.txt"
  echo exit /b %%RC%%
)
if errorlevel 1 (echo install_gate_task: writing %DEST%\gate_task.cmd failed.& exit /b 1)
echo install_gate_task: %DEST%\gate_task.cmd gates %MAIN%; each run logs to %DEST%\gate_last_run.txt
if not defined REG exit /b 0
schtasks /Create /F /SC DAILY /ST %T% /TN AlamoGateNightly /TR "\"%DEST%\gate_task.cmd\""
exit /b %ERRORLEVEL%
