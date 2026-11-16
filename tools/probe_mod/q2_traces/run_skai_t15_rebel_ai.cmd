@echo off
rem skai t14 run 2 + t15: the Rebel AI land attack run (run_skirmish_land_attack.cmd: FNEAR and plan records) with the
rem read-only Frida plan-end hooks (frida\skai_t15_hooks.js). About 22 min, hands-off after the clicks.
rem Clicks: Single Player - Skirmish Battles - game type Land - "Utapau" (_MP_LAND_UTAPAU), you = Empire (slot 1),
rem slot 2 = Rebel Medium AI - Advanced... Default - Start Game. Then touch nothing until it stops.
rem Output: out\<run>\ with frida_t15.jsonl. Read it with analysis\plan_ends.py and analysis\land_attack.py. Cleanup: cleanup.cmd.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_skai_t15_hooks.ps1" -Faction rebel %*
exit /b %ERRORLEVEL%
