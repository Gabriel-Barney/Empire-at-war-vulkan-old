@echo off
rem mov2 t07 probe (squad stop distance, re-order braking, MovingVsMoving holds): builds Mods\Mov2T07Probe from your
rem install, launches FoC windowed with it (common\launch_foc.py), tracks the probe objects every logic frame
rem (read-only), writes out\<run>\report.txt when the game exits, cleans up and restores the profile. Steam must be
rem running. Arguments: --no-launch, --no-profile, --map NAME, --reps N, --dry-run, --report-only RUN_DIR.
"%~dp0..\..\..\toolchain\venv\Scripts\python.exe" -I "%~dp0mov2_t07_run.py" %*
exit /b %ERRORLEVEL%
