@echo off
rem Starts FoC windowed with MODPATH=Mods\CU8HProbe (the kashyyyk / jetpack / luke sessions; start the kit with
rem --no-launch first). From a Claude shell: cmd /c <this file>. It goes through common\launch_foc.py (ref3 t01: the one
rem place a probe kit starts StarWarsG.exe), which keeps the game running when it is started from a Claude shell.
"%~dp0..\..\..\toolchain\venv\Scripts\python.exe" -I "%~dp0..\common\launch_foc.py" MODPATH=Mods\CU8HProbe
