@echo off
rem Fuzz build: clang 20 (MSVC ABI) with ASan + libFuzzer instrumentation, builds only the tools\fuzz targets, then
rem runs each for a time budget through tools\fuzz\run_fuzz.py (docs/BUILD.md, "Fuzzing").
rem Usage: build_fuzz.cmd [seconds per target, default 60] [extra run_fuzz.py args, e.g. --only fuzz_xml]
rem        build_fuzz.cmd 0   builds only.
rem Dir build-fuzz\, logs logs\fuzz_configure.txt / logs\fuzz_build.txt; corpus and crash files in build-fuzz\fuzz\.
rem libFuzzer on Windows needs ASan, the static CRT (/MT, the project default) and no /DEBUG or /INCREMENTAL link, so no -g.
rem -asan-stack=0 for the same catch-handler crash as build_asan.cmd ("Sanitizer limits" in docs/BUILD.md). Coverage is
rem inline-8bit-counters,pc-table instead of fuzzer-no-link: its trace-cmp part crashes any C++ catch handler on clang 20
rem (MSVC ABI), and the ALO / ALA readers report errors by exception (tested 2026-11-15). Cost: no comparison-guided mutation.
setlocal EnableDelayedExpansion
cd /d "%~dp0"
if not exist logs mkdir logs
set TC=%~dp0toolchain
set SECS=60
if not "%~1"=="" set SECS=%~1
if not exist "%TC%\llvm\bin\clang++.exe" (
  echo clang missing: run toolchain\5_portability_compilers.cmd first.
  exit /b 2
)
call "%TC%\msvc\setup_x64.bat" >nul
set PATH=%TC%\llvm\bin;%TC%\llvm\lib\clang\20\lib\windows;%TC%\venv\Scripts;%PATH%
set CMAKE=%TC%\venv\Scripts\cmake.exe
call "%~dp0tools\test_env.cmd"
call "%~dp0tools\cache_env.cmd"
set "SAN=-fsanitize=address -fsanitize-coverage=inline-8bit-counters,pc-table -mllvm -asan-stack=0 -fno-omit-frame-pointer -Wno-unused-command-line-argument"
> logs\fuzz_configure.txt (
  "%CMAKE%" -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=%TC%\venv\Scripts\ninja.exe" "-DCMAKE_CXX_COMPILER=%TC%\llvm\bin\clang++.exe" "-DCMAKE_CXX_FLAGS=-Wconversion -Wshadow !SAN!" "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address -Wl,/IGNORE:4217" "-DALAMO_ENGINE_MODULES=core;vfs;data;assets" -DALAMO_FUZZ=ON 2>&1
  echo configure exit !ERRORLEVEL!
)
> logs\fuzz_build.txt (
  "%CMAKE%" --build build-fuzz --target alamo_fuzzers 2>&1
  echo build exit !ERRORLEVEL!
)
findstr /c:"build exit 0" logs\fuzz_build.txt >nul || (
  echo build_fuzz: build failed, see logs\fuzz_build.txt
  python tools\lint\build_summary.py logs\fuzz_configure.txt logs\fuzz_build.txt
  exit /b 2
)
echo build_fuzz: fuzzers built in build-fuzz\bin
if "%SECS%"=="0" exit /b 0
shift
set REST=
:collect
if "%~1"=="" goto run
set REST=!REST! %1
shift
goto collect
:run
python -I tools\fuzz\run_fuzz.py --tree build-fuzz --seconds %SECS% !REST!
exit /b !ERRORLEVEL!
