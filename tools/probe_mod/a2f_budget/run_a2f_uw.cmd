@echo off
rem gai3 t04 / t07 probe: the a2f_budget kit played as EMPIRE (Rebel and Underworld are the AI players) with the
rem Underworld infrastructure reads (README section 12): a2f_run.py --human-empire --saboteur --uw-infra. Heart of the
rem Maw, human EMPIRE, Medium, no orders, to galactic frame 12000 (quit at "saboteur run recorded (frame 12000)").
rem Same steps as run_a2f.cmd (profile backup, build + install Mods\A2fBudgetProbe, launch, scan, report, cleanup,
rem profile restore). It reopens itself in its own console window (start), so the recorder does not depend on the
rem shell or the session that started it. Arguments are passed on to a2f_run.py (--windowed, --no-launch,
rem --non-interactive = no pause at the end). Cleanup if a run was interrupted: cleanup_a2f.cmd.
if /i "%~1"=="--here" goto here
start "GAI3 t04 t07 probe" cmd /c ""%~f0" --here %*"
echo Started in its own window. tools\probe_mod\a2f_budget\out\latest.txt names the run folder, its run_status.txt the state.
exit /b 0
:here
set "ARGS="
shift /1
:collect
if "%~1"=="" goto run
set "ARGS=%ARGS% %1"
shift /1
goto collect
:run
"%~dp0..\..\..\toolchain\venv\Scripts\python.exe" -I "%~dp0a2f_run.py" --human-empire --saboteur --uw-infra %ARGS%
set "RC=%ERRORLEVEL%"
echo.
echo GAI3 t04 / t07 probe (human Empire, --uw-infra) finished: exit %RC% (0 = records and report). Run folder: tools\probe_mod\a2f_budget\out\latest.txt
echo %ARGS% | find /i "--non-interactive" >nul && exit /b %RC%
pause
exit /b %RC%
