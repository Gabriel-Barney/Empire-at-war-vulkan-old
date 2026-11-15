@echo off
rem Run any project-local toolchain program by name, with the toolchain environment set (default workflow, 2026-11-15).
rem Nothing in toolchain\ is on the system PATH (CLAUDE.md rule 5); this puts it on PATH for one command only.
rem Usage:  tools\tc.cmd <program> [args]        e.g.  tools\tc.cmd cmake --version
rem         tools\tc.cmd clang-tidy --version    tools\tc.cmd dxc -?    tools\tc.cmd fxc /?    tools\tc.cmd cl
rem         tools\tc.cmd --where                 prints the folders it adds, in search order
rem From PowerShell:  & .\tools\tc.cmd --% <program> [args]   (nothing after it on the line)
rem Order: MSVC / Windows SDK (cl, link, lib, fxc, rc via setup_x64.bat), then venv (python, cmake, ninja), LLVM (clang,
rem clangd, clang-tidy, clang-format, lld), DXC, sccache, Rust (cargo, rustc; RUSTUP_HOME / CARGO_HOME set), MinGit.
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
set "ADD=%TC%\venv\Scripts;%TC%\llvm\bin;%TC%\dxc\bin\x64;%TC%\sccache;%CARGO_HOME%\bin;%TC%\mingit\cmd"
set "PATH=%ADD%;%PATH%"
if /i "%~1"=="--where" (
  for %%D in ("%ADD:;=" "%") do echo %%~D
  where cl 2>nul
  where fxc 2>nul
  exit /b 0
)
%*
exit /b %ERRORLEVEL%
