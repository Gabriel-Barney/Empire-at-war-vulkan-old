@echo off
rem Unattended gate run on origin/dev (PAF t01; on origin/dev since 2026-11-12): build_gate.cmd (MSVC + clang + sanitizers)
rem on the tip of origin/dev, one line per run in <main checkout>\logs\gate_history.txt.
rem Before 2026-11-12 it gated whatever branch the main checkout had checked out and skipped when that had uncommitted
rem edits, so dev's tip went ungated for days. Now it gates origin/dev in its own locked worktree,
rem .claude\worktrees\gate-dev (detached, junctions to Data, extracted, toolchain and roadmap-store; made on the first run,
rem kept by clean_worktrees because it is locked), and never touches the main checkout's files or branch.
rem Usage: gate_scheduled.cmd [any checkout of the repo]   (default: the checkout this file is in)
rem tools\install_gate_task.cmd copies this file to <main checkout>\logs\gate_task\ and registers a launcher that calls
rem it with the main checkout, so the running script is never a file a checkout or merge changes under it.
rem Exit code = the gate's (0 = all pass, 0 warnings); 3 = skipped (origin/dev already passed at this commit); 4 = setup failed.
setlocal EnableDelayedExpansion
set "ROOT=%~1"
if "%ROOT%"=="" set "ROOT=%~dp0.."
set "GCD="
for /f "delims=" %%G in ('git -C "%ROOT%" rev-parse --path-format^=absolute --git-common-dir 2^>nul') do set "GCD=%%G"
if not defined GCD (
  echo gate_scheduled: %ROOT% is not a checkout of the repository.
  exit /b 4
)
for %%I in ("%GCD%\..") do set "MAIN=%%~fI"
set "WT=%MAIN%\.claude\worktrees\gate-dev"
set "HIST=%MAIN%\logs\gate_history.txt"
if not exist "%MAIN%\logs" mkdir "%MAIN%\logs"

git -C "%MAIN%" fetch origin --prune
if errorlevel 1 (
  echo %DATE% %TIME% ? dev gate setup failed: git fetch>> "%HIST%"
  exit /b 4
)
set "SHA="
for /f %%H in ('git -C "%MAIN%" rev-parse --short origin/dev') do set "SHA=%%H"
if not defined SHA (
  echo %DATE% %TIME% ? dev gate setup failed: no origin/dev>> "%HIST%"
  exit /b 4
)
rem Nights with no history line since the last run (the task did not start): said on this run's line, " (missed N nights
rem (first..last))" (wr0 t04). The counter is read from origin/dev, since this file runs from a copy in logs\gate_task.
set "MISSED="
for /f "delims=" %%M in ('git -C "%MAIN%" show origin/dev:tools/lint/gate_nights.py 2^>nul ^| python -I - missed "%HIST%"') do set "MISSED= (%%M)"
if defined MISSED echo gate_scheduled:!MISSED!
rem Nothing landed since the last passing night: skip (a failing commit is gated again, to tell flaky from broken).
findstr /c:" !SHA! dev gate exit 0" "%HIST%" >nul 2>&1
if not errorlevel 1 (
  echo %DATE% %TIME% !SHA! dev gate skipped: this origin/dev commit already passed!MISSED!>> "%HIST%"
  exit /b 3
)

if not exist "%WT%\.git" (
  git -C "%MAIN%" worktree add --detach "%WT%" origin/dev
  if errorlevel 1 (
    echo %DATE% %TIME% !SHA! dev gate setup failed: git worktree add>> "%HIST%"
    exit /b 4
  )
  git -C "%MAIN%" worktree lock --reason "nightly dev gate (tools\gate_scheduled.cmd)" "%WT%"
)
for %%D in (Data extracted toolchain roadmap-store) do (
  if exist "%MAIN%\%%D" if not exist "%WT%\%%D" mklink /J "%WT%\%%D" "%MAIN%\%%D" >nul
)
rem The gate worktree holds no work of its own: force it to the commit under test (ignored build trees are kept).
git -C "%WT%" checkout -f --detach origin/dev
if errorlevel 1 (
  echo %DATE% %TIME% !SHA! dev gate setup failed: checkout in %WT%>> "%HIST%"
  exit /b 4
)

rem ALAMO_GATE_DRY=1 stops here: checks the setup (fetch, worktree, junctions, checkout) without the 10-minute gate.
if defined ALAMO_GATE_DRY (
  echo gate_scheduled: dry run, would gate origin/dev !SHA! in %WT%!MISSED!
  exit /b 0
)
rem wr2 t06: the nightly gate is one of the two runs that still write roadmap build records (tools\lint\record_build.py).
set "ALAMO_RECORD_BUILD=1"
call "%WT%\build_gate.cmd"
set RC=!ERRORLEVEL!
if exist "%WT%\logs\gate_summary.txt" copy /y "%WT%\logs\gate_summary.txt" "%MAIN%\logs\gate_dev_summary.txt" >nul
echo %DATE% %TIME% !SHA! dev gate exit !RC!!MISSED!>> "%HIST%"
exit /b !RC!
