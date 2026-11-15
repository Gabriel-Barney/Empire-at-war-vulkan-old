@echo off
rem Runs one REA call against the kept session (tools\re\rea_session.py; rea-agents 6.8.0 on the Ghidra
rem 12.1.4 and JDK in toolchain\re, installed by toolchain\14_re_tools.cmd and toolchain\15_rea.cmd).
rem   rea.cmd call <tool> [key=text ...] [key:=json ...]   rea.cmd tools [substring]   rea.cmd status | stop
rem When and how to use it: .claude\agents\re-analyst.md (REA notes); docs\PROBES.md, "RE and retail-tracing tools".
rem RE / doc tasks only, normally run by the re-analyst agent: never while writing engine code (CLAUDE.md, docs\TOOLS.md).
setlocal
set "RE_ROOT=%~dp0..\..\toolchain\re"
if not exist "%RE_ROOT%\rea\node_modules\rea-agents\scripts\rea.mjs" (echo toolchain\re\rea missing: run toolchain\15_rea.cmd 1>&2& exit /b 1)
"%RE_ROOT%\venv\Scripts\python.exe" -I "%~dp0rea_session.py" %*
