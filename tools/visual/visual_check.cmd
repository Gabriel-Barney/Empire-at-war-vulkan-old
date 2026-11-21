@echo off
rem Screenshot check (PAF t09; wr1 t07, 2026-11-21: no stored baselines). Compares REV's game smoke screenshots (default
rem HEAD) with its parent's, both taken from the smoke scenarios' results (tools\visual\visual_builds.py; results cached in
rem <main checkout>\.cache\scenario-results, a commit not cached is built and run: needs the game data and a GPU).
rem Usage: tools\visual\visual_check.cmd [parent] [REV]
rem Mismatches get a diff image in logs\visual_diff\<scenario>\ (new image dimmed, differing pixels red). Exit 0 = all match.
rem Shots whose content depends on wall-clock time or this PC's display modes are dropped by the scenarios (scenarios.json).
setlocal
cd /d "%~dp0..\.."
set "REV=%~1"
if /i "%REV%"=="parent" set "REV=%~2"
"%CD%\toolchain\venv\Scripts\python.exe" -I tools\visual\visual_builds.py %REV%
exit /b %ERRORLEVEL%
