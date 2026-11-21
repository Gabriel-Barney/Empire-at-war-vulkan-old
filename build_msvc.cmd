@echo off
rem Build all C++ tools with the project-local MSVC toolchain. Logs go to logs\.
setlocal EnableDelayedExpansion
cd /d "%~dp0"
rem Tree name from tools\lint\build_trees.py (ref3 t03), the one place that names build trees.
set "BT="
for /f "delims=" %%t in ('""%~dp0toolchain\venv\Scripts\python.exe" -I "%~dp0tools\lint\build_trees.py" name main msvc"') do set "BT=%%t"
if not defined BT (echo %~nx0: no tree name from tools\lint\build_trees.py& exit /b 2)
if not exist logs mkdir logs
if not exist toolchain\msvc\setup_x64.bat (
  echo Toolchain missing: run toolchain\2_setup.cmd first.> logs\msvc_configure.txt
  echo BUILD_DONE>> logs\msvc_configure.txt
  exit /b 1
)
call toolchain\msvc\setup_x64.bat >nul
set PATH=%~dp0toolchain\venv\Scripts;%PATH%
call "%~dp0tools\test_env.cmd"
call "%~dp0tools\cache_env.cmd"
rem Stamp for tools\land_on_dev.cmd (tools\lint\verified_build.py): becomes build-msvc\verified.json on a pass.
python -I tools\lint\verified_build.py begin %BT% --modules all --tests all
rem Which code this run builds (commit, worktree, source hash), copied into the build record with the outcome.
python -I tools\lint\build_state.py begin %BT% --modules all --tests all
> logs\msvc_configure.txt (
  where cl.exe 2>&1
  cl.exe 2>&1
  rem -U drops a cached module list so the full build always gets every module (engine/CMakeLists.txt default).
  cmake -S . -B %BT% -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl -U ALAMO_ENGINE_MODULES 2>&1
  echo configure exit !ERRORLEVEL!
)
> logs\msvc_build.txt (
  cmake --build %BT% 2>&1
  echo build exit !ERRORLEVEL!
  echo === tests
  rem Per-tree temp dir: tests write fixtures under fixed %TEMP% names, so concurrent builds would clobber each other.
  if not exist %BT%\tmp mkdir %BT%\tmp
  set "TMP=%~dp0%BT%\tmp"
  set "TEMP=%~dp0%BT%\tmp"
  python -I tools\lint\test_slot.py -- python -I tools\lint\flaky_retry.py -- ctest --test-dir %BT% --output-on-failure --output-junit "%~dp0%BT%\ctest_junit.xml" -j 8 2>&1
  echo ctest exit !ERRORLEVEL!
  python tools\lint\ctest_skips.py "%~dp0%BT%\ctest_junit.xml"
)
echo BUILD_DONE>> logs\msvc_configure.txt
python tools\lint\build_summary.py --state %BT%\build_state.json logs\msvc_configure.txt logs\msvc_build.txt
if not errorlevel 1 python -I tools\lint\verified_build.py pass %BT%
set "ALAMO_BUILD_EXTRA=%~dp0%BT%\build_state.json"
python -I tools\lint\record_build.py msvc %BT% logs\msvc_configure.txt logs\msvc_build.txt
