@echo off
rem modc t09 cleanup: deletes out\ (logs, effect cache, saves of the port runs). Quit StarWarsG.exe through its menus first
rem (Exit Game). The kit writes nothing into the game or mod folders, so there is nothing else to restore.
setlocal
tasklist /FI "IMAGENAME eq StarWarsG.exe" | find /I "StarWarsG.exe" >nul && echo StarWarsG.exe still runs: quit it through Exit Game first.
if exist "%~dp0out" rmdir /S /Q "%~dp0out"
echo modc t09 cleanup done.
exit /b 0
