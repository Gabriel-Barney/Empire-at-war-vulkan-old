@echo off
rem Records a Time Travel Debugging trace of a running process with the project-local TTD
rem (toolchain\re\ttd-1.11.611.0, installed by toolchain\14_re_tools.cmd).
rem   tools\re\ttd_record.cmd [process name or pid] [-accepteula]
rem Default process: StarWarsG.exe (start retail first, then run this). Traces (.run) go to
rem toolchain\re\ttd-traces. TTD needs administrator rights: this script asks for them with one Windows
rem prompt (UAC) and opens a second console where TTD runs; stop the recording with Ctrl+C there or by
rem closing the game. Replay: tools\re\windbg_mcp.cmd, or cdb.exe -z <trace>.run.
rem Licence: accepted for this Windows user on 2026-11-15 at Gabriel's request (TTD.exe -accepteula; stored as
rem HKCU\Software\Microsoft\TTD EULASigned=1). On a new machine or user TTD shows the terms again; -accepteula
rem accepts them, and that stays Gabriel's call.
setlocal EnableDelayedExpansion
set "RE_ROOT=%~dp0..\..\toolchain\re"
set "TTD=%RE_ROOT%\ttd-1.11.611.0\TTD.exe"
if not exist "%TTD%" (echo toolchain\re missing: run toolchain\14_re_tools.cmd 1>&2& exit /b 1)
set "TARGET=StarWarsG.exe"
set "EULA="
:args
if "%~1"=="" goto :run
if /i "%~1"=="-accepteula" (set "EULA=-accepteula") else (set "TARGET=%~1")
shift
goto :args
:run
set "TTD_TARGET=%TARGET%"
set "TTD_EXE=%TTD%"
set "TTD_EULA=%EULA%"
set "TTD_OUT=%RE_ROOT%\ttd-traces"
if not exist "%TTD_OUT%" mkdir "%TTD_OUT%"
powershell -NoProfile -Command ^
  "$t = $env:TTD_TARGET; if ($t -match '^\d+$') { $id = [int]$t } else { $p = Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($t)) -ErrorAction SilentlyContinue | Select-Object -First 1; if (-not $p) { Write-Host \"process $t is not running\"; exit 1 }; $id = $p.Id };" ^
  "$a = @(); if ($env:TTD_EULA) { $a += $env:TTD_EULA }; $a += '-out', ('\"' + (Resolve-Path $env:TTD_OUT).Path + '\"'), '-attach', $id;" ^
  "Write-Host \"recording pid $id to $env:TTD_OUT (a Windows prompt asks for administrator rights)\";" ^
  "Start-Process -FilePath $env:TTD_EXE -ArgumentList $a -Verb RunAs"
exit /b %ERRORLEVEL%
