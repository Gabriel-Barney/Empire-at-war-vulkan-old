@echo off
rem q6 t33 / t35 probe (what holds the galactic end screen after an elimination): builds Mods\Q6T33Probe from your
rem install, launches FoC windowed with it (common\launch_foc.py), polls the end-screen gates from memory (read-only),
rem takes screenshots around the end, writes out\<run>\report.txt when the game exits and cleans up. Steam must be
rem running. Arguments: one or more variants (E_BASE E_NOMSG E_NOFLEET E_SDBYSS E_CURPLANET D_BASE D_LONGMSG), then
rem --no-launch, --game DIR, --scan-sec S; or --report-only RUN_DIR [...]. Steps: README.md.
"%~dp0..\..\..\toolchain\venv\Scripts\python.exe" -I "%~dp0q6t33_run.py" %*
exit /b %ERRORLEVEL%
