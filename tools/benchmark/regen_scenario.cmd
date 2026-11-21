@echo off
rem Retired 2026-11-21 (wr1 t07): benchmark scenarios are no longer committed. Each build makes them from their recipes
rem (tests\benchmark\<name>.abench) with tools\goldens\bench_scenarios.py materialize (cached per engine source); the bench
rem ctests use a fixture that does it. Nothing to regenerate.
echo regen_scenario: retired 2026-11-21 ^(wr1 t07^): scenarios are made per build from their recipe.
echo   python -I tools\goldens\bench_scenarios.py materialize ^<name^> [--build DIR]
exit /b 2
