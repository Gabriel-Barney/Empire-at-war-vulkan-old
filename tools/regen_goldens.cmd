@echo off
rem Retired 2026-11-21 (wr1 t07): no derived golden is stored any more, so there is nothing to regenerate. A behaviour
rem change is declared in the commit's Behaviour: trailer and checked by tools\goldens\behaviour_check.py (skill
rem behaviour-check; docs/status/golden-checkpoints.md, "Computed results").
echo regen_goldens: retired 2026-11-21 ^(wr1 t07^): no stored goldens to regenerate.
echo   Declare an intended change in the commit's Behaviour: trailer, then check it:
echo     python -I tools\goldens\behaviour_check.py commit HEAD
echo   Skill: behaviour-check. Benchmark scenarios are made per build by tools\goldens\bench_scenarios.py.
exit /b 2
