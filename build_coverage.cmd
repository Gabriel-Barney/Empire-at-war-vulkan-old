@echo off
rem Code coverage: clang 20 (MSVC ABI) source-based coverage (-fprofile-instr-generate -fcoverage-mapping) over a module list,
rem runs ctest with one raw profile per process, then tools\coverage\coverage_report.py merges them (llvm-profdata) and writes
rem the per-module / per-file summary and an HTML report (llvm-cov). docs/BUILD.md, "Code coverage".
rem Usage: build_coverage.cmd ["module list"] [fast^|all]   default: the sim set, fast tier (ctest -LE slow); "all" runs slow tests too.
rem Dir build-cov\. Logs logs\cov_configure.txt / logs\cov_build.txt; report build-cov\coverage\ (summary.txt, summary.json, html\index.html).
rem Not a gate build and not recorded on the roadmap. Coverage counters change no sim result (they only count), but the
rem instrumented tests run slower; a test that fails here and passes in build-wt-clang is a timing or fixture issue first.
setlocal EnableDelayedExpansion
cd /d "%~dp0"
rem Tree name from tools\lint\build_trees.py (ref3 t03), the one place that names build trees.
set "BT="
for /f "delims=" %%t in ('""%~dp0toolchain\venv\Scripts\python.exe" -I "%~dp0tools\lint\build_trees.py" name main cov"') do set "BT=%%t"
if not defined BT (echo %~nx0: no tree name from tools\lint\build_trees.py& exit /b 2)
if not exist logs mkdir logs
set TC=%~dp0toolchain
set MODULES=core;vfs;data;jobs;galactic;objects;anim;commands;tactical;lua;ai
if not "%~1"=="" set MODULES=%~1
set TIER=fast
if not "%~2"=="" set TIER=%~2
if not exist "%TC%\llvm\bin\clang++.exe" (
  echo clang missing: run toolchain\5_portability_compilers.cmd first.
  exit /b 2
)
call "%TC%\msvc\setup_x64.bat" >nul
set PATH=%TC%\llvm\bin;%TC%\venv\Scripts;%PATH%
set CMAKE=%TC%\venv\Scripts\cmake.exe
set CTEST=%TC%\venv\Scripts\ctest.exe
call "%~dp0tools\test_env.cmd"
call "%~dp0tools\cache_env.cmd"
set "COV=-fprofile-instr-generate -fcoverage-mapping"
> logs\cov_configure.txt (
  "%CMAKE%" -S . -B %BT% -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=%TC%\venv\Scripts\ninja.exe" "-DCMAKE_CXX_COMPILER=%TC%\llvm\bin\clang++.exe" "-DCMAKE_CXX_FLAGS=-Wconversion -Wshadow %COV%" "-DCMAKE_EXE_LINKER_FLAGS=%COV%" "-DALAMO_ENGINE_MODULES=!MODULES!" 2>&1
  echo configure exit !ERRORLEVEL!
)
if exist %BT%\coverage\raw rmdir /s /q %BT%\coverage\raw
mkdir %BT%\coverage\raw
set CTEST_SEL=-LE slow
if /i "%TIER%"=="all" set CTEST_SEL=
> logs\cov_build.txt (
  "%CMAKE%" --build %BT% 2>&1
  echo build exit !ERRORLEVEL!
  echo === tests
  if not exist %BT%\tmp mkdir %BT%\tmp
  set "TMP=%~dp0%BT%\tmp"
  set "TEMP=%~dp0%BT%\tmp"
  rem %%m = binary signature, %%p = process id: one raw profile per test process, merged afterwards.
  set "LLVM_PROFILE_FILE=%~dp0%BT%\coverage\raw\%%m-%%p.profraw"
  python -I tools\lint\test_slot.py -- "%CTEST%" --test-dir %BT% !CTEST_SEL! --output-on-failure --output-junit "%~dp0%BT%\ctest_junit.xml" -j 6 2>&1
  echo ctest exit !ERRORLEVEL!
  python tools\lint\ctest_skips.py "%~dp0%BT%\ctest_junit.xml"
)
echo BUILD_DONE>> logs\cov_configure.txt
python tools\lint\build_summary.py logs\cov_configure.txt logs\cov_build.txt
findstr /c:"build exit 0" logs\cov_build.txt >nul || exit /b 2
python -I tools\coverage\coverage_report.py --tree %BT%
exit /b !ERRORLEVEL!
