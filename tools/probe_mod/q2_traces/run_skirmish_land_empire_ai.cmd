@echo off
rem b0418: long Q2 LAND skirmish trace (hands-off, about 22 min): the Empire AI against an idle human Underworld on Utapau.
rem Retail FoC has no AI-vs-AI skirmish, so the port's Empire vs Underworld AI test has no retail counterpart; this run
rem and its twin (run_skirmish_land_underworld_ai.cmd) compare each AI alone. Questions: docs\re\B0418_UTAPAU_AI_RUNS.md. What the Empire AI
rem buys and when, whether it captures a mineral-processor pad and builds a refinery, when its units reach the idle base,
rem which base objects die and whether the battle ends within 36000 ticks.
rem Clicks: Single Player - Skirmish Battles - game type Land - "Utapau" (_MP_LAND_UTAPAU), you = Underworld (slot 1),
rem slot 2 = Empire Medium AI - Advanced... Default - Start Game. Then touch nothing until it stops.
rem Output: out\<run>\ (send the folder back). Read it with analysis\land_attack.py <run dir>. Cleanup: cleanup.cmd.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_trace.ps1" -Scenario skirmish -NoDrive -LuaProbe -Ticks 36000 -TimeoutMin 30 -Label "Utapau land b0418, Underworld idle vs Empire Medium, advanced Default (by hand)" %*
exit /b %ERRORLEVEL%
