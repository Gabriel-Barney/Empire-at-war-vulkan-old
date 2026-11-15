@echo off
rem Starts the headless ReVa MCP server (Ghidra 12.1.4 + ReVa 7.3.2 from toolchain\re, installed by
rem toolchain\14_re_tools.cmd) on stdio. Arguments are passed to mcp-reva (e.g. --tool-group core-analysis).
rem ReVa keeps its Ghidra projects in .reva\projects under the current directory, so start it from one fixed
rem folder (re_reference\ is git-ignored). RE / doc tasks only: never while writing engine code (CLAUDE.md).
setlocal
set "RE_ROOT=%~dp0..\..\toolchain\re"
if not exist "%RE_ROOT%\venv\Scripts\mcp-reva.exe" (echo toolchain\re missing: run toolchain\14_re_tools.cmd 1>&2& exit /b 1)
set "GHIDRA_INSTALL_DIR=%RE_ROOT%\ghidra_12.1.4_PUBLIC"
if not defined JAVA_HOME set "JAVA_HOME=C:\Program Files\Java\jdk-27"
"%RE_ROOT%\venv\Scripts\mcp-reva.exe" %*
