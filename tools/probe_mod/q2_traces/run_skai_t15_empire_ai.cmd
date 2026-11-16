@echo off
rem skai t15: the b0418 Empire AI run (run_skirmish_land_empire_ai.cmd) with the read-only Frida plan-end hooks
rem (frida\skai_t15_hooks.js; docs\re\SKAI_T15_PLAN_RETEST.md 3.5). About 22 min, hands-off after the clicks.
rem Clicks: Single Player - Skirmish Battles - game type Land - "Utapau" (_MP_LAND_UTAPAU), you = Underworld (slot 1),
rem slot 2 = Empire Medium AI - Advanced... Default - Start Game. Then touch nothing until it stops.
rem Output: out\<run>\ with frida_t15.jsonl. Read it with analysis\plan_ends.py out\<run>. Cleanup: cleanup.cmd.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_skai_t15_hooks.ps1" -Faction empire %*
exit /b %ERRORLEVEL%
