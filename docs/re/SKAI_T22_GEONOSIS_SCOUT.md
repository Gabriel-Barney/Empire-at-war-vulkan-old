# skai t22: early Rebel approach on Geonosis (space skirmish) — trace evidence

Status: open (2026-11-16, lane skai-b, port at dev dc3afe62). Needs retail plan-level evidence (below).

## Symptom
`geonosis_skirmish` over 10 seeds: `combat.damage_taken[Empire]` port 138 vs retail 31 (allowed 99),
`ai.build_count[Rebel]` 29.4 vs 19.5, `outcome.units_final[Rebel]` 38.3 vs 29. Rebel squadrons reach the Empire
start too early: within 1500 of the TIEs (-3685,-2921) in 8/10 port seeds from frame 1814 (within 300 in 6/10), against
4/8 retail runs from frame 3602 (within 300 in 2/8, from 3942).

## First frame a Rebel fighter is within 600 of a space cell centre [trace]
Cell grid: camera box -4600..4600, 2000-unit cells (A1_READINGS §8), centres -3600, -1600, 400, 2400, 4400.
CELL_0_0 (-3600,-3600) lies about 1160 from the Empire starbase (-3155,-2530); CELL_1_0 (-1600,-3600) holds pad 40.

| run | 0_0 | 0_1 | 0_2 | 1_0 | 1_1 |
|---|---|---|---|---|---|
| retail 20261030-062751 | 4759 | - | 1606 | 3877 | - |
| retail 20261108-204213 | - | - | 2328 | 1396 | - |
| retail 20261108-203639 | 4541 | 5032 | - | 2051 | 3149 |
| retail 20261115-225742 | - | 4115 | 3410 | 1387 | 2190 |
| retail 20261115-235429 | - | - | 1606 | 2053 | 3480 |
| retail 20261116-002920 | 3702 | - | 4616 | 1387 | 3466 |
| retail 20261116-003415 | - | - | 3467 | 1365 | 5080 |
| retail 20261116-003910 | - | - | 1621 | 3391 | 3049 |
| port seeds 1-10 (dc3afe62) | 6/10, 1996-4029 | 4/10 | 9/10, 1898-4723 | 10/10, 1255-4850 | 9/10, 1556-5204 |

Retail visits CELL_0_0 late (3/8, from 3702) and goes to CELL_0_2 first in three runs (by frame ~1606). The port's
first Space_Scout often takes CELL_0_0 (seeds 2, 3, 9 at frames 1071-1385), and its free store sends idle squadrons to
`enemy_location` (-3600,-1600) / (-3600,-3600) and the starbase from frame 623-1505 (`port_ai.txt` "fs order").

## Port proposal set at the first scout (seed 2, frame 1071) [port debug]
Category 4 holds 21 proposals (under 32, so retail's insertion sort is stable as well). Space_Scout desire 10.00 for
targets 8, 9, 10 (CELL_0_0, 0_1, 0_2), 16, 17, 18, 24, 28, 29: the tie goes to the first-inserted, CELL_0_0. All
never-seen cells far from both starbases score term 2 of `Space_Area_Needs_Scouting` = 10 × TimeLastSeen = 10,
because the fogged Empire starbase gives `Distance_To_Nearest_Enemy_Starbase` = 1e18.

## Ruled out
- The starbase seen through fog: `Skirmish_Empire_Star_Base_1` has HIDE_WHEN_FOGGED and
  `Initial_State_Visible_Under_FOW` False, so FOG_OF_WAR §5.2 hides it.
- `DistanceToNearest*` without fog (experiment): the port then never visits CELL_1_0 / pad 40, which retail reaches
  early in all 8 runs, and Empire damage drops to 0 in every seed (retail: damage in 2/8). Retail applies the fog there,
  as AI_TACTICAL_PERCEPTION says [H].
- Sort ties above 32 elements: the proposal sets have 21-31 entries.
- The cell grid origin: the port uses the camera box, as A1_READINGS §8 says [H].

## Open question (needs a retail probe)
Which cell does retail's first `Space_Scout` take on Geonosis, and with what desire for CELL_0_0 / 0_1 / 0_2?
The q2_traces Lua kit records no tactical plan starts: a probe that logs the SpaceScout plan's `AITarget` (name and
frame) and `EvaluatePerception("Space_Area_Needs_Scouting", PlayerObject, <cell>)` for the five western cells on the
first proposals would settle whether retail's candidate order, the desire or the Resolve pick differs.
