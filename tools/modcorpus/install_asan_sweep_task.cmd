@echo off
rem Registers (or replaces) a per-user weekly Task Scheduler entry for the ASan corpus sweep (roadmap mcx7 t05):
rem tools\modcorpus\asan_sweep.cmd in the main checkout of this repository (whatever branch it has checked out).
rem No admin needed; it runs while you are logged on. Only Gabriel runs this (2026-11-18: agents add the mode, never the task).
rem Usage: tools\modcorpus\install_asan_sweep_task.cmd [DAY] [HH:MM]   default SUN 04:00 (DAY: MON..SUN).
rem Remove it with: schtasks /Delete /TN AlamoModCorpusAsan /F
setlocal
set "DAY=%~1"
if "%DAY%"=="" set "DAY=SUN"
set "T=%~2"
if "%T%"=="" set "T=04:00"
set "GCD="
for /f "delims=" %%G in ('git -C "%~dp0..\.." rev-parse --path-format^=absolute --git-common-dir') do set "GCD=%%G"
if not defined GCD (echo install_asan_sweep_task: not inside a checkout of the repository.& exit /b 2)
for %%I in ("%GCD%\..") do set "MAIN=%%~fI"
if not exist "%MAIN%\tools\modcorpus\asan_sweep.cmd" (echo install_asan_sweep_task: %MAIN% has no tools\modcorpus\asan_sweep.cmd yet.& exit /b 2)
schtasks /Create /F /SC WEEKLY /D %DAY% /ST %T% /TN AlamoModCorpusAsan /TR "\"%MAIN%\tools\modcorpus\asan_sweep.cmd\""
exit /b %ERRORLEVEL%
