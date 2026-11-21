@echo off
rem BM1: run a benchmark scenario in sim-only mode (1 warm-up + 3 timed runs) and write its JSON result to logs\.
rem   tools\benchmark\run_bench.cmd [name | path\to\scenario.abench] [more alamo_headless --bench options]
rem e.g. tools\benchmark\run_bench.cmd space_battle --bench-baseline logs\bench_space_battle_before.json
rem Default scenario: space_battle; result: logs\bench_<name>.json. Close other builds and tests first: the result is flagged
rem "noisy" when other processes take more than 10% of the CPU during a timed run.
rem wr1 t07: a scenario name is made from its recipe for the tree's build (tools\goldens\bench_scenarios.py materialize,
rem cached); no save is committed. A path runs that manifest as it is.
setlocal
cd /d "%~dp0..\.."
call "%CD%\tools\test_env.cmd"
set "PYX=%CD%\toolchain\venv\Scripts\python.exe"
set "SC=%~1"
if "%SC%"=="" set "SC=space_battle"
set "EXE="
for /f "delims=" %%e in ('""%PYX%" -I "%CD%\tools\lint\build_trees.py" exe alamo_headless.exe --main-first --msvc"') do set "EXE=%%e"
if not defined EXE (echo run_bench: no alamo_headless.exe; build first ^(build_msvc.cmd^)& exit /b 2)
for %%D in ("%EXE%\..\..") do set "TREE=%%~fD"
if /i "%SC:~-7%"==".abench" goto named
set "NAME=%SC%"
set "SC="
for /f "delims=" %%m in ('""%PYX%" -I "%CD%\tools\goldens\bench_scenarios.py" materialize %NAME% --build "%TREE%""') do set "SC=%%m"
if not defined SC (echo run_bench: scenario %NAME% not made ^(ALAMO_FOC_DATA not set?^)& exit /b 2)
goto haveit
:named
for %%F in ("%SC%") do set "NAME=%%~nF"
:haveit
if not exist logs mkdir logs
set "REST="
:collect
shift
if "%~1"=="" goto run
set REST=%REST% %1
goto collect
:run
"%EXE%" --bench "%SC%" --bench-json "logs\bench_%NAME%.json" %REST%
set RC=%ERRORLEVEL%
echo run_bench exit %RC% (result: logs\bench_%NAME%.json)
exit /b %RC%
