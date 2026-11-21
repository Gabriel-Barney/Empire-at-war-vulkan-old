# skai t22: early Rebel approach on Geonosis (space skirmish) — trace evidence

Status: done (2026-11-16, lane skai-b; dev 104cae8a: start-station rule, tie settled, free-store hand-over rule; see the last section). Retail plan records taken 2026-11-16 (section "Retail plan records"; the hooks changed the AI's space play, so only their engine readings count); RE questions in
`docs/re/SKAI_T22_RE_QUESTIONS.md`.

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
Kit (prepared 2026-11-16, not run yet): the q2_traces space plan hooks (`tools/probe_mod/q2_traces/README.md`, "Space
skirmish plan hooks"; `run_skirmish.cmd -LuaProbe`, three runs; `analysis/space_scout.py`). Each retail Space_Scout start
records its cell (fitted from three reference distances), its `Space_Area_Needs_Scouting` desire and the two starbase
distance terms; the pad, refinery and unit-build plans are recorded too, for the build count.

## Retail plan records (2026-11-16, kit dev ecd8cdbf) [H, probe]
Runs `20261116-132329`, `-132904`, `-133417` (q2_traces `run_skirmish.cmd -LuaProbe -NonInteractive`, Geonosis, idle
Empire, Rebellion Medium AI, 5400 ticks, all `ticks_reached`). Records of the Rebel AI's Geonosis plans (own station
`SKIRMISH_REBEL_STAR_BASE_*`; the main-menu attract battle's records are left out). `tostring(AITarget)` names the cell.

| | run 1 | run 2 | run 3 | port (10 seeds, dc3afe62) |
|---|---|---|---|---|
| Space_Scout plan starts | 75 | 76 | 82 | 10.8 per run |
| first scout | CELL_0_2 at 4.5 s | CELL_0_0 at 2.3 s | CELL_0_1 at 2.3 s | CELL_3_3 5, CELL_0_2 3, CELL_0_0 1, CELL_4_3 1 |
| scouts at CELL_0_0 / 0_1 / 0_2 | 1 / 0 / 5 | 8 / 2 / 9 | 13 / 5 / 4 | 0.9 / 0.1 / 0.9 per run |
| Secure_Build_Pad_Space starts (all players, 3 runs: 29 / 32 / 26) | | | | 5.2 per run |
| Tactical_Multiplayer_Build_Space_Units_Generic starts | 5 | 5 | 5 | 2.9 per run |
| Build_Refinery_Space starts | 0 | 0 | 0 | 4.0 per run |

- **The hooks changed the Rebel AI's space play, so these runs are not parity references.** Against the 8 unprobed
  runs above: Empire `damage_taken` 196 / 302 / 215 (unprobed 0-124), Rebel `damage_taken` 1064 / 840 / 955 (75-550),
  Rebel fighters reach CELL_0_0 at frame 2062 / 2087 / 2188 and never pad 40 (CELL_1_0, reached in all 8 unprobed runs),
  13 X-wings lost from 58 s. No space plan ever came back from its first `BlockOnCommand` (no `PBLOCK` in about 130 plan
  starts per run, while the same wrapper records `PBLOCK` in land plans, e.g. the menu battle's `Secure_Contestable`), so
  the hooked space plan threads most likely stall or die in their first block (cause not found). Readings taken before
  that block stand (the desire, `dfs` / `des`, the cell): they are engine perception values, read in the plan's first
  instructions. The plan lifetimes below do not: they are probably the probe's effect.
- **Scout plans are short-lived (probe-affected, see above).** One plan instance starts a new Space_Scout every 3.4-6.7 s (about 100-200 frames;
  the six pooled instances together every ~2 s), each with a new cell, and every start reports `prev=none` (the run before
  left no end record). Port scout plans run about 1000 frames (seed 2: plan 6 from frame 1071 to 2156). The desire is
  10 for nearly every pick (`d=10.0` / `9.99`; a few 7.5, 9.4, 5.4): the same tie the port sees. So retail scouts the
  western cells too (CELL_0_0 up to 13 times a run), but no scout lives long enough to fly there.
- **The pre-placed start stations are not found by `Distance_To_Nearest_*_Starbase`.** `dfs` = 1e18 on all 112
  records while the AI's station is the start `SKIRMISH_REBEL_STAR_BASE_1`, and a real distance on all 121 records once
  the upgrade made `SKIRMISH_REBEL_STAR_BASE_2` (a new object, from 85.2 s in all three runs). `des` (the idle Empire's
  start station, never replaced) is 1e18 throughout. The port finds both (its term 1 gives desire 15 to the Rebel home
  cells at frame 69; retail never shows 15 on Geonosis).
- **Limit of the records:** no space plan recorded a `PBLOCK`, `PEXIT` or `PDONE` (not even the unit-build plan, whose
  units are built), so the probe's BlockOnCommand / ScriptExit wraps do not take effect in the space scripts, and how
  each plan ended is not recorded; `prev=none` only says no end record came before the next start.

## Port rule from the RE answer, and what is left (2026-11-16)
`SKAI_T22_SPACE_AI_RE.md` Q1.1 and Part 3 [H]: retail creates each space skirmish start station into the threat grid that
the skirmish start then deletes. The station never joins the new grid's owner lists (so `DistanceToNearest*`, the force
sums and `Find_Nearest` miss it all battle; the upgraded station is not affected), but its first re-projection puts it
into the cells and the threat layer (CellForce, concentration and point danger count it). The port does the same in
space skirmishes (`PHASE4.md`, skai t22 line; first landed at 5a5d522a as "out of the whole grid", narrowed to the owner
lists after Part 3). This matches the probe's `dfs` / `des` = 1e18 for the start stations, a reading the hooks cannot
have changed.

`geonosis_skirmish`, 10 seeds, against the 8 unprobed retail runs:

| | before (dc3afe62) | whole grid (5a5d522a) | owner lists only | retail |
|---|---|---|---|---|
| verdict | FAIL 3 / PASS 16 | FAIL 1 / PASS 18 | FAIL 2 / PASS 17 | |
| `ai.build_count[Rebel]` | 29.4 FAIL | 27.2 | 26.8 | 19.5 |
| `outcome.units_final[Rebel]` | 38.3 FAIL | 34.7 | 34.8 | 29 |
| `combat.damage_taken[Empire]` | 138 FAIL | 183 FAIL | 166 FAIL | 31 |
| `combat.damage_taken[Rebel]` | | 596 | 569 FAIL (by 3) | 250 |
| `combat.first_shot_frame` | | 1286 | 1286 | 2187 |

After merging skai t15 (dev 9ab052ee), the station uses t15's store flag `kFlagNoAiOwnerList`, so Lua `Find_Nearest`
(the free store's `closest_enemy`) also skips it, as Part 3 says retail's does: FAIL 2 / PASS 17, `ai.build_count[Rebel]`
26.6, `outcome.units_final[Rebel]` 34.5, `combat.damage_taken[Empire]` 210 FAIL, `combat.damage_taken[Rebel]` 622 FAIL,
`combat.first_shot_frame` 1286.

**The desire tie is settled** (`SKAI_T22_SPACE_AI_RE.md` Part 4 [H]): retail proposes the cells x outer, y inner, draws
nothing before Resolve, and Tactical_Targeted's greedy phase takes the first-inserted of the tied cells, as the port does.
What varies is how many of the two start X-wing squadrons Map_Control's `Secure_Build_Pad_Space` reserves first: 2 / 1 / 0
in the three retail runs, and 2 in 5 port seeds, 1 in 4, 0 in 1 (port seed 7 takes CELL_0_0 and CELL_0_1 at frame 69,
as retail run 3 does). With no free fighter, a scout accept fails and records an activation failure.

**What is left, and why it needs evidence the probe runs cannot give:** after frame 69, retail's probe runs start a new
scout every 3-7 s from 4.5 s on (CELL_0_2 in all three runs at 4.5 s), using fighters that were freed when the hooked plan
threads died (no plan returned from its first block). So the retail scout order after the first commit, and how long the
start squadrons stay with the pad plans, are probe-affected. The port's first scout of a 2-pad seed comes at frame
~1072 (CELL_0_0), when a pad plan ends. The Empire damage gap (166 vs 31) follows the Rebel squadrons reaching the
Empire corner (`fs order` attack-moves to (-3600,-3600) / (-3600,-1600) and the station from frame 623-1946). Settling it
needs an unhooked retail record of the Rebel AI's plans (for example a Frida log of goal accepts and plan ends, which does
not touch the Lua threads), which this session could not run.

## Settled by the unhooked retail runs (2026-11-16, dev 104cae8a)
Three Geonosis runs without the Lua probe (`run_skai_t22_space.cmd`, read-only Frida; `20261116-173353`, `-173917`,
`-174437`, now `geonosis_skirmish` references) show the first scout at CELL_0_0 in all three, accepted at mf 67 with one
pad. In runs 1 and 3 a second pad accepted at mf 134 (Map_Control resolves before Tactical_Targeted) takes the scout's
X-wing, the scout fails the stolen test 0x6c45e0 and is released; in run 2 no second pad is accepted and it lives to mf
934. `SKAI_T22_SPACE_AI_RE.md` Part 5 [H]: a tactical hand-over keeps the unit on the free store as a reserved entry,
so this can happen at any commit. Ported for space (`PHASE4.md`). `geonosis_skirmish` against 11 references: FAIL 1 /
PASS 18; Empire damage 64 (retail 22), Rebel build count 24.7 (20.3), Rebel final units 33.3 (30) pass.
`combat.damage_taken[Rebel]` 598 against 200 is skai t23. Port scouts still live longer (median 667 against 464 frames).
