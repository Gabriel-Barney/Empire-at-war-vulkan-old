@echo off
rem Mod compatibility corpus sweep (roadmap mcx7; docs/MOD_COMPAT_DESIGN.md 13.2): every installed mod and stack through
rem alamo_mod_check, the headless skirmish / galactic runs and the hidden smokes, graded G0-G3 (tools\modcorpus\README.md).
rem   tools\modcorpus\run_corpus.cmd [--only KEY,..] [--stages check,runs,smokes] [--write-md] [--asan] [--list] [...]
rem Needs a built tree (build_agent.cmd msvc all; --asan also build_asan.cmd with ALAMO_SANITIZE=address and the apps).
rem Exit 0: no grade dropped since the last recorded sweep; 1: a grade dropped (the mod and its first new blocker are
rem printed); 2: usage / a tool is missing. Log: logs\modcorpus\run_corpus.txt. From PowerShell:
rem   & .\tools\modcorpus\run_corpus.cmd --% --stages check --list
setlocal
cd /d "%~dp0..\.."
call "%CD%\toolchain\msvc\setup_x64.bat" >nul
call "%CD%\tools\test_env.cmd"
set "PATH=%CD%\toolchain\venv\Scripts;%PATH%"
set "PYTHONUTF8=1"
if not exist logs\modcorpus mkdir logs\modcorpus
python -I tools\modcorpus\run_corpus.py %* > logs\modcorpus\run_corpus.txt 2>&1
set RC=%ERRORLEVEL%
type logs\modcorpus\run_corpus.txt
echo run_corpus exit %RC% (log: logs\modcorpus\run_corpus.txt)
exit /b %RC%
