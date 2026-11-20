@echo off
rem BM1: regenerate a benchmark scenario with the arguments its manifest recorded - the one command after a save-format,
rem sim-behaviour or game-data change ("scenario changed, not comparable"). The version is bumped when the files change.
rem   tools\benchmark\regen_scenario.cmd [name | path\to\scenario.abench] [build dir]
rem Default scenario: tests\benchmark\space_battle.abench. Build dir: the first MSVC tree (build-msvc, then build-wt-msvc) with alamo_headless.exe (tools\lint\build_trees.py).
rem Game data from tools\test_env.cmd (ALAMO_FOC_DATA). Commit the changed tests\bench files and say so in the PR.
setlocal
cd /d "%~dp0..\.."
call "%CD%\tools\test_env.cmd"
set "SC=%~1"
if "%SC%"=="" set "SC=space_battle"
if /i not "%SC:~-7%"==".abench" set "SC=tests\benchmark\%SC%.abench"
set "EXE="
if not "%~2"=="" set "EXE=%~2\bin\alamo_headless.exe"
if "%EXE%"=="" for /f "delims=" %%e in ('""%CD%\toolchain\venv\Scripts\python.exe" -I "%CD%\tools\lint\build_trees.py" exe alamo_headless.exe --main-first --msvc"') do set "EXE=%%e"
if not exist "%EXE%" (echo regen_scenario: no alamo_headless.exe; build first ^(build_msvc.cmd^)& exit /b 2)
if not exist "%SC%" (echo regen_scenario: %SC% not found& exit /b 2)
echo regen_scenario: %EXE% --bench-regen %SC%
"%EXE%" --bench-regen "%SC%" --quiet
set RC=%ERRORLEVEL%
echo regen_scenario exit %RC%
exit /b %RC%
