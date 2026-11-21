@echo off
rem Starts the headless ReVa MCP server (Ghidra 12.1.4 + ReVa 7.3.2 from toolchain\re, installed by
rem toolchain\14_re_tools.cmd) on stdio. Arguments are passed to mcp-reva (e.g. --tool-group core-analysis).
rem The Ghidra project is kept in toolchain\re\reva-work\foc (tools\re\reva_persistent.py), shared by every
rem worktree through the toolchain junction; StarWarsG.exe is imported and analysed there once.
rem Ghidra locks an open project: one ReVa session at a time.
rem Registered only on the re-analyst agent (one ReVa session at a time); never copy decompiled code into the repo (CLAUDE.md).
setlocal
set "RE_ROOT=%~dp0..\..\toolchain\re"
if not exist "%RE_ROOT%\venv\Scripts\mcp-reva.exe" (echo toolchain\re missing: run toolchain\14_re_tools.cmd 1>&2& exit /b 1)
set "GHIDRA_INSTALL_DIR=%RE_ROOT%\ghidra_12.1.4_PUBLIC"
set "JAVA_HOME=%RE_ROOT%\jdk-21.0.13+9"
"%RE_ROOT%\venv\Scripts\python.exe" -I "%~dp0reva_persistent.py" "%RE_ROOT%\reva-work\foc" %*
