@echo off
rem Portability check: build everything with the project-local LLVM clang++ (GNU driver, MSVC ABI, project MSVC
rem headers/libs), -Wall -Wextra -Wpedantic from the CMake files plus -Wconversion -Wshadow, then run ctest.
rem Dir build-clang\, logs logs\clang_configure.txt / logs\clang_build.txt (finished when clang_configure.txt ends with BUILD_DONE).
setlocal EnableDelayedExpansion
cd /d "%~dp0"
rem Tree name from tools\lint\build_trees.py (ref3 t03), the one place that names build trees.
set "BT="
for /f "delims=" %%t in ('""%~dp0toolchain\venv\Scripts\python.exe" -I "%~dp0tools\lint\build_trees.py" name main clang"') do set "BT=%%t"
if not defined BT (echo %~nx0: no tree name from tools\lint\build_trees.py& exit /b 2)
if not exist logs mkdir logs
set TC=%~dp0toolchain
if not exist "%TC%\llvm\bin\clang++.exe" (
  echo clang missing: run toolchain\5_portability_compilers.cmd first.> logs\clang_configure.txt
  echo BUILD_DONE>> logs\clang_configure.txt
  exit /b 1
)
call "%TC%\msvc\setup_x64.bat" >nul
set PATH=%TC%\llvm\bin;%TC%\venv\Scripts;%PATH%
set CMAKE=%TC%\venv\Scripts\cmake.exe
set CTEST=%TC%\venv\Scripts\ctest.exe
call "%~dp0tools\test_env.cmd"
call "%~dp0tools\cache_env.cmd"
rem Which code this run builds (commit, worktree, source hash), copied into the build record with the outcome.
python -I tools\lint\build_state.py begin %BT% --modules all --tests all
> logs\clang_configure.txt (
  "%TC%\llvm\bin\clang++.exe" --version 2>&1
  rem -U drops a cached module list so the full build always gets every module (engine/CMakeLists.txt default).
  "%CMAKE%" -S . -B %BT% -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=%TC%\venv\Scripts\ninja.exe" "-DCMAKE_CXX_COMPILER=%TC%\llvm\bin\clang++.exe" "-DCMAKE_CXX_FLAGS=-Wconversion -Wshadow" -U ALAMO_ENGINE_MODULES %* 2>&1
  echo configure exit !ERRORLEVEL!
)
> logs\clang_build.txt (
  "%CMAKE%" --build %BT% 2>&1
  echo build exit !ERRORLEVEL!
  echo === tests
  rem Per-tree temp dir: tests write fixtures under fixed %TEMP% names, so concurrent builds would clobber each other.
  if not exist %BT%\tmp mkdir %BT%\tmp
  set "TMP=%~dp0%BT%\tmp"
  set "TEMP=%~dp0%BT%\tmp"
  python -I tools\lint\test_slot.py -- "%CTEST%" --test-dir %BT% --output-on-failure --output-junit "%~dp0%BT%\ctest_junit.xml" -j 8 2>&1
  echo ctest exit !ERRORLEVEL!
  python tools\lint\ctest_skips.py "%~dp0%BT%\ctest_junit.xml"
)
echo BUILD_DONE>> logs\clang_configure.txt
python tools\lint\build_summary.py --state %BT%\build_state.json logs\clang_configure.txt logs\clang_build.txt
set "ALAMO_BUILD_EXTRA=%~dp0%BT%\build_state.json"
python -I tools\lint\record_build.py clang %BT% logs\clang_configure.txt logs\clang_build.txt
