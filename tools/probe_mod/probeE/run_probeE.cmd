@echo off
rem Probe E launch: FALLBACKPATH chain vs replace. Steam must be running. Starts the game through common\launch_foc.py
rem (ref3 t01: the one place a probe kit starts StarWarsG.exe), full screen as before.
"%~dp0..\..\..\toolchain\venv\Scripts\python.exe" -I "%~dp0..\common\launch_foc.py" --no-windowed MODPATH=Mods\ProbeE FALLBACKPATH=Mods\ProbeE_FB
