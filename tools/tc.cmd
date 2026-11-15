@echo off
rem Run any project-local toolchain program by name, with the toolchain environment set (default workflow, 2026-11-15).
rem Nothing in toolchain\ is on the system PATH (CLAUDE.md rule 5); this puts it on PATH for one command only.
rem Usage:  tools\tc.cmd <program> [args]        e.g.  tools\tc.cmd cmake --version
rem         tools\tc.cmd clang-tidy --version    tools\tc.cmd dxc -?    tools\tc.cmd fxc /?    tools\tc.cmd cl
rem         tools\tc.cmd --where                 prints the folders it adds, in search order
rem From PowerShell:  & .\tools\tc.cmd --% <program> [args]   (nothing after it on the line)
rem Order: MSVC / Windows SDK (cl, link, lib, fxc, rc via setup_x64.bat), then venv (python, cmake, ninja), LLVM (clang,
rem clangd, clang-tidy, clang-format, lld, lldb, llvm-* tools), DXC, sccache, Rust (cargo, rustc; RUSTUP_HOME /
rem CARGO_HOME set), MinGit, then apitrace 14.0 (apitrace, d3dretrace, qapitrace), then the retail RE tools: WinDbg (cdb,
rem kd, DbgX.Shell), Frida (frida, frida-trace, ... from toolchain\re\venv) and REA (tools\re\rea.cmd, run as `rea`). These come
rem last, so their DLLs and their Python 3.14 shadow nothing. RE tools: clean-room rule, re-analyst / probe / RE tasks only
rem (docs/TOOLS.md, "Exception"). lldb and lldb-dap also get Python 3.10.11 (step 16) on PATH and as PYTHONHOME.
rem Builds still go through the build scripts (build_agent.cmd etc.), never a hand-run cmake configure (docs/BUILD.md).
rem The worktree has no toolchain folder: python -I tools\worktree\worktree_tool.py link (the SessionStart hook does it).
setlocal
for %%R in ("%~dp0..") do set "ROOT=%%~fR\"
set "TC=%ROOT%toolchain"
if not exist "%TC%\" (
  echo tc: %TC% missing. In a worktree run: python -I tools\worktree\worktree_tool.py link
  exit /b 1
)
if "%~1"=="" (
  echo usage: tools\tc.cmd ^<program^> [args]  ^|  tools\tc.cmd --where
  exit /b 2
)
if exist "%TC%\msvc\setup_x64.bat" call "%TC%\msvc\setup_x64.bat" >nul
set "RUSTUP_HOME=%TC%\rust\rustup"
set "CARGO_HOME=%TC%\rust\cargo"
set "ADD=%TC%\venv\Scripts;%TC%\llvm\bin;%TC%\dxc\bin\x64;%TC%\sccache;%CARGO_HOME%\bin;%TC%\mingit\cmd;%TC%\capture\apitrace-14.0\bin"
set "WDBG=%TC%\re\windbg-1.2611.4001.0"
set "ADD=%ADD%;%WDBG%\amd64;%WDBG%;%TC%\re\venv\Scripts;%ROOT%tools\re"
set "PATH=%ADD%;%PATH%"
set "LLDBPY=%TC%\lldb-python-3.10.11\tools"
if /i "%~1"=="lldb" set "LLDB_NEEDS_PY=1"
if /i "%~1"=="lldb-dap" set "LLDB_NEEDS_PY=1"
if defined LLDB_NEEDS_PY (
  if not exist "%LLDBPY%\python310.dll" (
    echo tc: lldb needs Python 3.10: run toolchain\16_lldb_python.cmd
    exit /b 1
  )
  set "PYTHONHOME=%LLDBPY%"
  set "PATH=%LLDBPY%;%PATH%"
)
if /i "%~1"=="--where" (
  for %%D in ("%ADD:;=" "%") do echo %%~D
  where cl 2>nul
  where fxc 2>nul
  exit /b 0
)
%*
exit /b %ERRORLEVEL%
