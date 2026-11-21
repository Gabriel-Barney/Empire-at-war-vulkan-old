@echo off
rem Starts FoC windowed with no mod (storyb run). From a Claude shell: cmd /c <this file>. It goes through
rem common\launch_foc.py (ref3 t01: the one place a probe kit starts StarWarsG.exe; a StarWarsG.exe started straight
rem from a Claude shell exits at once). Game folder: launch_foc.py's DEFAULT_GAME (Gabriel's Steam install).
"%~dp0..\..\..\toolchain\venv\Scripts\python.exe" -I "%~dp0..\common\launch_foc.py"
