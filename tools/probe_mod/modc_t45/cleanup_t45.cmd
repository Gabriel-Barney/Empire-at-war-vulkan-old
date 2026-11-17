@echo off
rem modc t45 cleanup: deletes out\. Quit StarWarsG.exe through its menus first (Exit Game). The kit writes nothing into the
rem game or mod folders, so there is nothing else to restore.
setlocal
tasklist /FI "IMAGENAME eq StarWarsG.exe" | find /I "StarWarsG.exe" >nul && echo StarWarsG.exe still runs: quit it through Exit Game first.
if exist "%~dp0out" rmdir /S /Q "%~dp0out"
echo modc t45 cleanup done.
exit /b 0
