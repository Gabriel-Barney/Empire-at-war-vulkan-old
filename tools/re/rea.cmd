@echo off
rem Runs one REA call against the kept session (tools\re\rea_session.py; rea-agents 6.8.0 on the Ghidra
rem 12.1.4 and JDK in toolchain\re, installed by toolchain\14_re_tools.cmd and toolchain\15_rea.cmd).
rem   rea.cmd call <tool> [key=text ...] [key:=json ...]   rea.cmd tools [substring]   rea.cmd status | stop
rem When and how to use it: .claude\agents\re-analyst.md (REA notes); docs\PROBES.md, "RE and retail-tracing tools".
rem Any worker may use it, also while writing engine code (Gabriel 2026-11-15); never copy decompiled code into the repo (CLAUDE.md, docs\TOOLS.md).
setlocal
set "RE_ROOT=%~dp0..\..\toolchain\re"
if not exist "%RE_ROOT%\rea\node_modules\rea-agents\scripts\rea.mjs" (echo toolchain\re\rea missing: run toolchain\15_rea.cmd 1>&2& exit /b 1)
"%RE_ROOT%\venv\Scripts\python.exe" -I "%~dp0rea_session.py" %*
