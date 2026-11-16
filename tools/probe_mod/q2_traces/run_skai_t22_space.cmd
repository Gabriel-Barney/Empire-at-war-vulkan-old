@echo off
rem skai t22: Geonosis space skirmish, NO Lua probe, with the read-only Frida goal / plan hooks (frida\skai_t22_space_hooks.js).
rem About 6 min. Clicks (or -NonInteractive to let the menu driver click): Single Player - Skirmish Battles - Space -
rem "(2) Battle Over Geonosis" (_MP_SPACE_GEONOSIS), you = Empire (slot 1, idle), slot 2 = Rebellion Medium AI -
rem Advanced... Default (6000 credits) - Start Game - Begin. Then touch nothing until it stops (5400 ticks).
rem Output: out\<run>\ with trace.q2t and frida_t22.jsonl. Read it with analysis\space_goals.py out\<run>. Cleanup: cleanup.cmd.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_skai_t22_space.ps1" %*
exit /b %ERRORLEVEL%
