@echo off
rem Starts mcp-windbg 1.6.0 (stdio MCP server around cdb.exe) with the project-local WinDbg from toolchain\re
rem (installed by toolchain\14_re_tools.cmd). Its open_cdb_dump tool takes a TTD trace (.run) as well as a
rem crash dump: this WinDbg's cdb.exe loads the TTD replay engine. Arguments are passed to mcp-windbg.
rem Registered on the re-analyst agent; any worker may also run cdb itself (tools\tc.cmd cdb). Never copy decompiled code into the repo.
setlocal
set "RE_ROOT=%~dp0..\..\toolchain\re"
if not exist "%RE_ROOT%\venv\Scripts\mcp-windbg.exe" (echo toolchain\re missing: run toolchain\14_re_tools.cmd 1>&2& exit /b 1)
"%RE_ROOT%\venv\Scripts\mcp-windbg.exe" --cdb-path "%RE_ROOT%\windbg-1.2611.4001.0\amd64\cdb.exe" %*
