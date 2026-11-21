@echo off
rem modc t45 / t50: retail observation kit (no payload). Steps and questions: README.md.
rem Usage: run_t45.cmd retail <workshop id>   the original (StarWarsG.exe) windowed with STEAMMOD=<id>
rem Nothing is written into the game or mod folders; out\ is only for screenshots you keep.
setlocal
set "ID=%~2"
if /i not "%~1"=="retail" (echo usage: run_t45.cmd retail ^<workshop id^>& exit /b 2)
if "%ID%"=="" (echo usage: run_t45.cmd retail ^<workshop id^>& exit /b 2)
set "ROOT=%~dp0..\..\.."
if not exist "%~dp0out" mkdir "%~dp0out"
rem launch_foc.py: a StarWarsG.exe started from a Claude shell otherwise exits at once (probe_mod\common\launch_foc.py).
echo Starting the original windowed: StarWarsG.exe STEAMMOD=%ID%  ^(Steam must be running^)
"%ROOT%\toolchain\venv\Scripts\python.exe" -I "%ROOT%\tools\probe_mod\common\launch_foc.py" STEAMMOD=%ID%
exit /b %errorlevel%
