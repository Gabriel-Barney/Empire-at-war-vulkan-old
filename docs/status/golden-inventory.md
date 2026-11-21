## Golden inventory (wr1 t01, 2026-11-21)

Purpose: the list that "goldens as computed diffs" (`docs/DECISIONS.md`, "Workflow redesign", first bullet) works from.
Every tracked file that a test (or a check script) compares a run's output against, with its class:

- `derived`: a function of code + game data + scenario definition. A build can make it again. Candidate to leave git;
  the check becomes a parent-versus-commit run diff.
- `reference`: an input, retail-derived data, or hand-written expected values. Stays tracked.

Made on origin/dev `0e844d4f` (2026-11-21). Files: `git ls-files` under every `testdata/`, `golden/`, `goldens/` and
`benchmark/` folder, `tests/visual_baselines/`, `tools/parity/*.json`, plus files found by grepping the CMake files for
`--golden`, `-DGOLDEN`, `-DEXPECTED`, `--check-baseline`, `--refs` and `*_GOLDEN_DIR` / `ALAMO_SIM_TESTDATA`. Scripts are
left out. Readers: `add_test` names and the helper `alamo_add_worker_count_check` (`engine/CMakeLists.txt`), read from the
CMake files and the test sources. Commits: `git log origin/dev --no-merges --since=2026-10-30 --format=%H -- <path> | wc -l`.
The initial import `fdc6b9c4` (2026-10-30) is inside that window, so 1 means "imported or added, never changed since".

Worker-count suffixes: `alamo_add_worker_count_check(<name> ...)` makes `<name>` (0 workers, writes `<build>/<name>_w0.crc`)
and `<name>_w1`, `<name>_w4`, `<name>_w16`, `<name>_w<N>` (N = logical cores - 1) unless `WORKERS` says otherwise. Below,
`<name>{,_wK}` means all of these entries.

**Totals: derived 127 files in 30 rows; reference 121 files in 13 rows (24 single files, plus 97 files of mod / conformance
input trees in one row).**

### Derived

| # | Path | What | Written by | Read by (ctest) | Commits | Notes |
|---|---|---|---|---|---|---|
| D1 | `engine/apps/headless/testdata/skirmish_ai_synthetic_golden.crc` | CRC v2 (checkpoint 64) of the synthetic AI-vs-AI space skirmish | `tools\regen_goldens.cmd` (headless); by hand: same command with `--crc-out` | `alamo_headless_skirmish_ai_synthetic` (`--golden`), `alamo_headless_skirmish_trace` (`-DGOLDEN`) | 28 | No game data. Both run `--check-workers` (several counts in one process). |
| D2 | `engine/apps/headless/testdata/skirmish_ai_foc_golden.crc` | CRC v2 (64), `--skirmish-ai foc` (FoC `_MP_SPACE_KUAT`, Empire vs Rebel) | `regen_goldens.cmd` | `alamo_headless_skirmish_ai_foc` | 60 | FoC data; slow. 4 workers only. |
| D3 | `engine/apps/headless/testdata/skirmish_ai_foc_land_golden.crc` | CRC v2 (64), `--skirmish-ai foc-land --ticks 3600` (`_MP_LAND_UTAPAU` skirmish) | `regen_goldens.cmd` | `alamo_headless_skirmish_ai_foc_land` | 86 | FoC data; slow. Second-highest churn of all files. |
| D4 | `engine/apps/headless/testdata/skirmish_ai_foc_land_campaign_golden.crc` | CRC v2 (64), `--skirmish-ai foc-land-campaign --ticks 3600` (Utapau campaign map) | `regen_goldens.cmd` | `alamo_headless_skirmish_ai_foc_land_campaign` | 85 | FoC data; slow. |
| D5 | `engine/apps/headless/testdata/replay_golden.crc` | CRC v2 (64) of `--replay replay_stream.acmd` | `regen_goldens.cmd` (first entry by name: `alamo_headless_replay_golden`) | `alamo_headless_replay_golden` (`--check-workers`), `alamo_headless_replay_workers` (0-worker entry, `SETUP_ARGS --golden`) | 2 | No game data. The `_wK` entries compare with the 0-worker log, not with this file. |
| D6 | `engine/apps/headless/testdata/galactic_ai_hotm_golden.txt` | Galactic AI log (per-minute planets / plans / conflicts / CRC) of `--economy --galactic-ai --ticks 18000 --workers 4`, HotM | by hand: same command with `--ai-log <golden>` (not covered by `regen_goldens`, which takes `*.crc` only) | `alamo_headless_galactic_ai` (`--ai-golden`) | 41 | FoC data. Text format, not ALAMO_CRC. |
| D7 | `engine/apps/headless/testdata/galactic_ai_hotm_human_golden.txt` | Galactic AI log of the same campaign with a scripted human Rebel, 36000 ticks | by hand: same command with `--workers 0 --ai-log <golden>` (MSVC) | `alamo_headless_galactic_ai_human{,_wK}` (every entry compares its AI log) | 48 | FoC data; slow. Input: R2. |
| D8 | `engine/apps/game/testdata/game_smoke_{skirmish,campaign,land}.crc` and `.rec` (6 files) | CRC v2 (64) and command recording of the scripted game smoke runs | `regen_goldens.cmd --only game` (or copy `<build>/smoke_out/game_smoke_<kind>.*`) | `alamo_game_smoke_<kind>_golden` (through `compare_smoke.cmake`; `.rec` byte for byte); `game_smoke_campaign.*` also `alamo_game_smoke_screens_golden` | crc 61 / 64 / 94, rec 32 / 19 / 53 (skirmish / campaign / land) | FoC data and a GPU; slow. `game_smoke_land.crc` is the most-changed file in this list. |
| D9 | `engine/apps/sim/testdata/synthetic.acmd`, `synthetic_golden.crc` | Recorded orders of a live synthetic G0 map run (seed 4242, 600 ticks) and the CRC v2 (64) of its replay | `alamo_sim_tests --regen` (rewrites the stream and the golden) | `alamo_sim_tests`; the `.acmd` also `alamo_commands_tests` (old-stream load / re-encode) | acmd 1, crc 3 | No game data. The test re-records the stream and requires the same bytes, so the stream is derived. |
| D10 | `engine/apps/sim/testdata/_SPACE_PLANET_CORELLIA_PIRACY.acmd`, `_SPACE_PLANET_CORELLIA_PIRACY_golden.crc`, `_LAND_PLANET_BYSS_KIDNAPPING.acmd`, `_LAND_PLANET_BYSS_KIDNAPPING_golden.crc` | Same as D9 on two real FoC maps | `alamo_sim_tests --regen` | `alamo_sim_tests`; the `.acmd` files also `alamo_commands_tests` | acmd 1 / 1, crc 3 / 4 (space / land) | FoC data. |
| D11 | `engine/apps/sim/testdata/galactic_hotm_golden.crc` | CRC v2 (64) of 2700 galactic HotM frames through the sim host | `alamo_sim_tests --regen` | `alamo_sim_tests` | 9 | FoC data. |
| D12 | `engine/apps/sim/testdata/story_synthetic_golden.txt`, `story_underworld_golden.txt` | Story event logs (`alamo story log v1`): synthetic campaign seed 7, 600 ticks; `Full_Story_Campaign_Underworld` seed 99, 900 ticks | `alamo_sim_tests --regen` or `--regen-story` | `alamo_sim_tests` | 4 / 4 | Underworld log needs FoC data. Text, not ALAMO_CRC. |
| D13 | `engine/apps/sim/testdata/battle_synthetic_golden.crc`, `battle_queue_golden.crc`, `battle_bm_golden.crc` | CRC v1 (full) of three scripted battles: synthetic space battle, the queued-order battle, the Black Market gate battle | `alamo_sim_battle_tests --regen` | `alamo_sim_battle_tests` | 16 / 17 / 17 | Full logs (905 to 2505 lines); `convert_goldens.py --in-place --check` would thin them. |
| D14 | `engine/apps/sim/testdata/battle_l1_golden.crc` | CRC v1 of the L1 campaign battle (special weapons, `battle.abilities` state) | `alamo_sim_l1_tests --regen` | `alamo_sim_l1_tests` | 15 | Full log. |
| D15 | `engine/ai/tests/golden/ai_golden.txt` | Three digests (perception, matrices, nonzero cost count) of a synthetic galactic perception run | `alamo_ai_tests --write-golden` | `alamo_ai_tests` | 1 | No game data. |
| D16 | `engine/ai/tests/golden/ai_galactic_synthetic_crc.txt`, `ai_underworld_crc.txt` | Per-frame CRC (every 150th / 300th frame) plus decisions of the synthetic galactic AI campaign and the Underworld special systems | `alamo_ai_galactic_tests --write-golden` | `alamo_ai_galactic_tests` | 26 / 19 | Own text format. |
| D17 | `engine/ai/tactical/tests/golden/ai_tactical_scenario_crc.txt` | One CRC of the tactical AI scenario | `alamo_ai_tactical_tests --write-golden <file>` | `alamo_ai_tactical_tests` (`--golden`) | 3 | One line. |
| D18 | `engine/ai/tactical/tests/golden/ai_tactical_perception_crc.txt` | One CRC of the tactical perception scenario (1 / 4 / N threads) | `alamo_ai_tactical_perception_tests --write-golden <file>` | `alamo_ai_tactical_perception_tests` (`--golden`) | 3 | One line. |
| D19 | `engine/ai/tactical/tests/golden/ai_tactical_world_crc.txt` | Per-frame CRC (every 150th) plus decisions and orders of the mock-host space skirmish | `alamo_ai_tactical_world_tests --write-golden` | `alamo_ai_tactical_world_tests` | 12 | Own text format. |
| D20 | `engine/core/tests/golden/dmath_golden.txt` | Digests of `alamo::dmath` outputs (result bits) | `alamo_core_dmath_tests --write-golden <file>` | `alamo_core_dmath_tests` (`--golden`) | 1 | Its point is MSVC = clang bit for bit (rule 3), not a behaviour change; see notes. |
| D21 | `engine/galactic/tests/golden/galactic_synthetic_crc.txt` | CRC v2 (64) of the scripted synthetic galactic campaign | `alamo_galactic_economy_tests --write-golden` | `alamo_galactic_economy_synthetic_campaign` (`--golden-dir`) | 6 | No game data. |
| D22 | `engine/galactic/tests/golden/galactic_foc_crc.txt` | CRC v1 of the FoC galactic campaign | `alamo_galactic_economy_tests --write-golden` | `alamo_galactic_economy_foc_campaign` (`--golden-dir`) | 10 | FoC data. Full log (96 lines). |
| D23 | `engine/shaderxlat/tests/golden/{ps11_ops,ps14_phase,ps20_ops,ps30_io,vs11_skin,vs20_flow,vs30_io}.hlsl` (7) | HLSL text the translator emits for hand-written test shaders (`tests/mini_asm.cpp`) | `alamo_shaderxlat_tests --write-golden` | `alamo_shaderxlat_tests` (`cpu`) | 1 each | Text compare, no sim. |
| D24 | `engine/tactical/abilities/tests/golden/abilities_space_crc.txt` | CRC v2 (64) of the scripted ability / hardpoint space battle | `alamo_tactical_abilities_tests --write-golden <file>` | `alamo_tactical_abilities_tests` (`--golden`) | 10 | |
| D25 | `engine/tactical/combat/tests/golden/combat_space_crc.txt` | CRC v2 (64) of the scripted 2-sided space battle | `alamo_tactical_combat_tests --write-golden <file>` | `alamo_tactical_combat_tests` (`--golden`) | 8 | |
| D26 | `tests/goldens/mods/{aol_space,fotr_space,remake_space,tr_space}.crc` (4) | CRC v2 (64) of a 600-tick space skirmish of each reference mod | `regen_goldens.cmd` (headless or `--mods`) through `tools/goldens/mod_golden.py` | `alamo_headless_mod_golden_<name>` | 6 / 7 / 8 / 7 | FoC data plus the installed workshop item; skips otherwise; slow. Run spec: R5. |
| D27 | `tests/benchmark/space_battle.{alsv,acmd,crc}` | Benchmark save, orders (header-only stream, 72 bytes) and fingerprint (CRC v2, checkpoint 16) | `tools\benchmark\regen_scenario.cmd` = `alamo_headless --bench-regen` (make line in R4); `regen_goldens.cmd` remakes stale ones | `alamo_headless_bench_space_battle{,_wK}`, `alamo_headless_bench_space_battle_svc_streams{,_wK}` | 70 / 70 / 32 | FoC data; slow. These tests pass `--bench-allow-changed`: a stale fingerprint only prints "not comparable". |
| D28 | `tests/benchmark/land_battle.{alsv,acmd,crc}`, `galactic_late.{alsv,acmd,crc}` | Same for the land battle (checkpoint 16) and the late galactic campaign (checkpoint 64) | as D27 | `alamo_headless_bench_land_battle{,_wK}`, `alamo_headless_bench_land_battle_svc{,_wK}`; `alamo_headless_bench_galactic_late{,_wK}` | land 71 / 71 / 67, galactic_late 27 / 27 / 24 | As D27. |
| D29 | `tests/benchmark/{space_battle_game,nebula_battle,water_battle}.{alsv,acmd,crc}` (9) | Rendered-scene benchmark save, orders and fingerprint (checkpoint 16), made by a rendered `alamo_game` run | `alamo_game --hidden --bench-regen` (`regen_goldens.cmd --only bench`) | `alamo_game_bench_scene_<scene>` and `alamo_headless_bench_game_<scene>` (both fail on a stale fingerprint, exit 3); `alamo_headless_bench_space_battle_game{,_wK}` (allow-changed) | space_battle_game 64 / 64 / 26, nebula 66 / 66 / 26, water 73 / 73 / 57 | FoC data and a GPU to make; slow. `benchmark_save.cpp` writes `saved_at 0`, so the save has no wall-clock field and the make step is deterministic. |
| D30 | `tests/visual_baselines/smoke/*.png` (41), `tests/visual_baselines/smoke_screens/*.png` (20) | 320-px-wide copies of the game smoke screenshots | `tools\visual\visual_check.cmd update` (from `<build>/smoke_out`, port output only) | no ctest; `tools\visual\visual_check.cmd` (not in the gate) | 1 to 8 per file | Regenerated purely from the port's own output, so derived. `docs/re/HUD_GAPS.md` says they were not refreshed and dev already differs (2026-11-08). |

### Reference

| # | Path | What | Written by | Read by (ctest) | Commits | Notes |
|---|---|---|---|---|---|---|
| R1 | `engine/apps/headless/testdata/replay_stream.acmd` | Fixed synthetic command stream that drives the Q1 replay | made once by `alamo_headless --record-stream` (seeded generator; its arguments are not recorded anywhere); no test rewrites it | `alamo_headless_replay_workers{,_wK}`, `alamo_headless_replay_golden`, `alamo_headless_crc_checkpoint`, `alamo_commands_tests` | 1 | Port output once, but it is an input now: nothing regenerates it, and `alamo_commands_tests` uses it as a fixed old-format stream. |
| R2 | `engine/apps/headless/testdata/galactic_human_hotm.txt` | Hand-written scripted-human command list (HotM, Rebel) | by hand | `alamo_headless_galactic_ai_human{,_wK}` (`--human-script`) | 1 | Scenario input. |
| R3 | `engine/apps/game/testdata/bench_orbit.abench`, `bench_orbit_taa.abench` | Hand-written benchmark recipes (camera path, preset, ticks) | by hand | `alamo_game_bench_scenario`, `alamo_game_bench_scenario_taa` (copied, then `--bench-make`) | 1 / 1 | Scenario definition. |
| R4 | `tests/benchmark/{space_battle,land_battle,galactic_late,space_battle_game,nebula_battle,water_battle}.abench` (6) | Scenario manifests | `--bench-make`, rewritten by `--bench-regen` | every D27 to D29 test; `space_battle.abench` also `alamo_game_bench_scenario_headless` | space 76, land 80, galactic_late 28, space_game 68, nebula 69, water 74 | Mixed file. The `make` line, description, `host`, `mode`, `preset`, `frames_per_tick` and `camera` lines are the scenario definition (reference). `version`, `save`, `orders`, `fingerprint`, `start_tick` and `final_crc` are derived and cause all the churn. Classified reference because the file holds the only copy of the definition; t02 should split it. |
| R5 | `tests/goldens/mods/{aol_space,fotr_space,remake_space,tr_space}.json` (4) | Mod golden run specs: workshop id, mod stack, `alamo_headless` arguments, data `fingerprint` | by hand; `regen_goldens.cmd` rewrites `fingerprint` | `alamo_headless_mod_golden_<name>` (`--spec`; also the CMake glob that makes the tests) | 2 each | Scenario definition. `fingerprint` is the installed mod's data hash, set at regen. |
| R6 | `launcher/src/testdata/game_flags.json` | Recorded `alamo_game --list-flags --json` output | copied from `alamo_game --list-flags --json` | `alamo_game_list_flags` (`-DEXPECTED`, line for line); Rust `cargo test` (`launcher/src/launch.rs`, `include_str!`) | 1 | Contract between two programs. The launcher compiles it in and its tests run without a C++ build, so it stays tracked even though the game can produce it. |
| R7 | `launcher/src/testdata/{galaxy_sample,mod_catalog,mod_conflicts}.json` (3) | Recorded outputs of the galaxy export and `alamo_mod_check` on the fixtures | copied by hand | Rust `cargo test` only (`include_str!`); no ctest | 1 each | Inputs to the launcher tests. `galaxy_sample.json` has a wall-clock `savedAt`. |
| R8 | `tools/parity/baseline.json` | Per-metric verdicts of the last single parity run (port vs retail), with the dev commit | `parity_harness.py run --write-baseline` | `parity_harness_{hotm_galactic,geonosis_skirmish}` (`--check-baseline`) | 36 | Needs the retail reference runs (untracked, below), so not a function of a build alone. A ratchet: "a metric that passed may not fail". |
| R9 | `tools/parity/baseline_seeds.json` | Same for the distribution mode (10 seeds) | `parity_harness.py run --seeds 10 --write-baseline` | `parity_harness_{hotm_galactic,geonosis_skirmish}_seeds` | 26 | As R8. |
| R10 | `tools/parity/scenarios.json`, `tools/parity/outcomes.json` | Parity scenarios, reference-run names, tolerances and thresholds with reasons | by hand | `parity_harness_*`, `alamo_headless_outcome_{foc_space,foc_station,foc_land_campaign,foc_land_full,foc_land_empire_underworld}` | 11 / 5 | Hand-written expected values. |
| R11 | `tools/parity/autoresolve_refs.json` | Retail auto-resolve battle outcomes | from retail runs | `parity_autoresolve` (`--refs`) | 2 | Retail-derived. |
| R12 | `tools/lint/tag_reads_baseline.txt` | Allowed string tag reads per sim file (now `total 0`) | by hand (only goes down) | `alamo_objects_tag_reads_lint` | 9 | A lint limit, not a run result; at 0 it is the rule itself. |
| R13 | `engine/apps/game/testdata/mod_fixture/**` (36), `engine/apps/game/testdata/conformance/**` (61) | Synthetic mod and conformance mod trees (XML, Lua, modinfo, fake Steam library) | by hand | `alamo_game_shared_constants_tests`, `alamo_game_open_tests`, `alamo_game_mod_fixture_tests`, `alamo_mod_check` fixture test, `alamo_game_mod_picker_tests`, `alamo_game_conformance_values_tests`, `alamo_game_conformance_stacks_tests` | 11 (mod_fixture), 8 (conformance), whole trees | Test inputs, one summary row. |

### Notes for t02-t06

Format (t02, t03):
- CRC v2 (`ALAMO_CRC 2`): checkpoint 64 for D1-D5, D8 `.crc`, D9-D11, D21, D24-D26 and `galactic_late.crc`; checkpoint 16 for
  the other five benchmark fingerprints. No golden has a `keep` line.
- CRC v1 (full logs): D13 (3 files), D14, D22. They are the module and sim goldens `regen_goldens` does not cover.
- Not ALAMO_CRC: D6, D7 (galactic AI logs), D12 (story logs), D15-D19 (own digest / per-frame formats), D20 (digests),
  D23 (HLSL text), D8 `.rec` and the benchmark `.alsv` / `.acmd` (binary). A computed diff needs a compare rule for each.
- `regen_goldens.cmd` covers D1-D5, D8, D26, D27-D29 (and the R4 / R5 derived fields). It does not cover D6, D7 (`--ai-log`),
  D9-D14 (`--regen` of the sim exes) or D15-D25 (`--write-golden` of the module exes). `tools/lint/golden_merge.py`
  (land_on_dev conflict handling) knows D1-D5, D8 and `tests/benchmark/*` only.

Worker counts:
- Several counts inside one process: `alamo_headless_skirmish_ai_synthetic`, `alamo_headless_skirmish_trace`,
  `alamo_headless_replay_golden` (`--check-workers`); `alamo_sim_tests` (map replays, synthetic story; HotM at 0 and 4),
  `alamo_sim_battle_tests` (queue battle at 0 / 1 / 4 / 15), `alamo_sim_l1_tests`, `alamo_galactic_economy_*_campaign`
  (0 / 1 / 4 / N), `alamo_tactical_combat_tests` and `alamo_tactical_abilities_tests` (1 / 4 / N threads),
  `alamo_ai_tactical_perception_tests` (1 / 4 / N), `alamo_ai_tests` (1 / 4 / hw threads, jobs 0 / 1 / 4 / default).
- One process per count (`alamo_add_worker_count_check`) that also reads a stored file: `alamo_headless_replay_workers`
  (0-worker entry vs D5), `alamo_headless_galactic_ai_human` (every entry vs D7), the `alamo_headless_bench_*` checks (scenario
  files; stale fingerprint only prints).
- One process per count with no stored file (already a computed check inside one build): `alamo_sim_battle_ai_hero_clash`,
  `alamo_sim_fleet_groups_battle`, `alamo_sim_save_continue` (workers 1, 4), `alamo_headless_limits_<mod>_<kind>` (workers 4;
  mods `factions_80`, `levels_10`, `huge_delay`; kinds space, land, galactic, plus `land_cp` for huge_delay),
  `alamo_headless_skirmish_ai_foc_land_outcome` (plus `_profiled`, which reads its 0-worker log),
  `alamo_headless_skirmish_ai_foc_land_ai_lane`, `alamo_headless_skirmish_ai_foc_ai_lane`.

MSVC vs clang:
- Every derived golden except D30 is written by MSVC and read by the clang tree's same ctest, so today the stored file is also
  the cross-compiler check. A parent-versus-commit diff inside one compiler does not replace that; t0x needs a same-commit
  MSVC-vs-clang compare (both trees run the scenario, `alamo_headless --compare` on the two logs). D20 (dmath) exists only
  for that check.
- Explicit cross-compiler compare outside ctest: `tools/conformance/limit_mods.py run --exe <msvc> --exe2 <clang>`.
- D30 is MSVC only (`visual_check.cmd` takes the main MSVC tree).

Scenario definitions (command lines in CMake that a computed diff must run on parent and commit):
- `alamo_headless --skirmish-ai synthetic --check-workers --quiet` (D1); `--skirmish-ai foc --workers 4 --quiet` (D2);
  `--skirmish-ai foc-land --ticks 3600 --workers 4 --quiet` (D3); `--skirmish-ai foc-land-campaign --ticks 3600 --workers 4
  --quiet` (D4); `--replay engine/apps/headless/testdata/replay_stream.acmd` (D5).
- `alamo_headless --economy --galactic-ai --ticks 18000 --workers 4 --quiet` (D6); `--economy --galactic-ai --ticks 36000
  --quiet --human Rebel --human-script engine/apps/headless/testdata/galactic_human_hotm.txt` (D7).
- `alamo_game --smoke skirmish|campaign|land --hidden --audio null [--seed 2 for land]` (D8); the screens run adds
  `ALAMO_SMOKE_SCREENS=1;ALAMO_PSEUDO_LOC=1`.
- Mod goldens: the `args` and `stack` of each R5 spec, run by `mod_golden.py` with `--game <ALAMO_FOC_DATA> [--workers 4]`.
- Benchmarks: the `make` line of each R4 manifest (`--save-session ...` for `host headless`, `--bench-map ...` for
  `host game`).
- The sim and module goldens (D9-D25) have their scenarios inside the test source, not on a command line; the test exe's
  own write option is the "run" for them.
- No stored file, but golden-like: the alamo_add_worker_count_check scenarios above, and `alamo_headless_outcome_*`
  (parity tier 2, judged over seeds by `outcomes.json`).

Rule-level candidates for t05 (a golden whose real content is a rule, checkable without a stored log):
- D12 story logs: the order, frame, reward and cause of each story event. Asserting the expected events would pin the rule
  and leave the frame-exact log to the run diff.
- D13 `battle_queue_golden.crc`: queued orders run in order; a plain order or Stop clears the queue (the test also has a trace
  digest). `battle_bm_golden.crc`: Black Market gate, both players gated without a purchase, only the Underworld after
  `S10_BM_Item` (the test already asserts the counts; the golden adds only the CRC).
- D14: special weapon fired and hit (asserted in the test); the golden adds the CRC.
- D6, D7: per-minute planet counts, plans and conflicts of the galactic AI. AI choices are parity tier 2: a share-of-seeds
  check (as `alamo_headless_outcome_*`) fits better than a pinned log.
- D3, D4: land skirmish structure is already checked rule-level by `--expect-skirmish-base` and the outcome sweeps.
- D8 `.rec`: which commands the scripted menu input produces (UI-to-command mapping).
- R6: the flag list contract; it stays as a stored expected value.
- Shape to copy: `alamo_headless_eval_uw_infra` (`check_uw_infra.cmake`) compares five values with retail's frame-0 row of
  `docs/re/A2M_R3_RE_A.md` and needs no golden.

Out of scope, left out of the counts:
- Retail reference runs of the parity harness: untracked, in the Q2 kit's ignored `tools/probe_mod/q2_traces/out/` of the
  main checkout (`ALAMO_Q2_TRACES` overrides). Nothing to move.
- Probe kit images and tables (`tools/probe_mod/*/refs/*.png`, `fixpass_capture/*.png`, `a2f_budget/port_reference_a2j.tsv`,
  `s10_map_owners/expected_objects.json`): read by probe kit scripts, not by any ctest.
- Test inputs read from `docs/` (`docs/formats/examples`, `docs/shaders/catalog.json` by the assets tests; `docs/re` by
  `alamo_tactical_movement_tests`) and `engine/rhi/tests/shaders/*.hlsl`: inputs, not expected results.
- Golden values written inline in test sources (for example the Lua golden in `engine/lua/tests/test_lua.cpp`): not files.

### Migration status (t04-t06, 2026-11-21)

| Rows | Covered by | State |
|---|---|---|
| D1-D7 headless goldens (alamo_headless `--golden <tracked file>`) | `tools/goldens/scenario_results.py` scenarios, `behaviour_check.py commit/range/workers/compilers` | expand phase: `land_on_dev.cmd` runs `range --expand` and logs each landing to `logs\behaviour_landings.txt` (t04 counts twenty landings with no disagreement) |
| Benchmark `.alsv` / `.acmd` / `.crc` and the derived manifest lines | `tools/goldens/bench_scenarios.py` (recipe in git, scenario made per build key); the headless bench ctests read `${CMAKE_BINARY_DIR}/bench_scenarios` through a fixture | t06: headless and `alamo_headless_bench_game_*` done; `alamo_game_bench_scene_*` (apps/game) still read `tests/benchmark` until t07 strips the files |
| D17, D18, D20, D24, D25 (module exes with `--write-golden <file>`) | next: a scenario kind "test exe" whose result is the file `--write-golden <tmp>` writes; D24 / D25 are CRC v2, so columns apply; D17 / D18 / D20 compare as text (one value) | t05 open |
| D9-D14 (`alamo_sim*_tests --regen`, rewrites tracked files in place) | next: give the sim exes `--regen-to DIR` (write beside, not over) so a scenario can take the output; D13 / D14 / D12 story logs are rule-level candidates (notes above) | t05 open |
| D15, D16, D19, D21, D22 (own text formats, `--write-golden` without a path or `--golden-dir`) | as the module rows once each exe takes an output path | t05 open |
| D8 game smoke `.crc` / `.rec` (GPU) | `alamo_game` smoke runs as scenarios on the GPU machine (nightly), like the rendered bench scenes | t05 open |
| Mod goldens (`tests/goldens/mods/*.crc`) | scenarios of `mod_golden.py`'s command (skips without the workshop item) | t05 open |
| Visual baselines (61 PNGs, no ctest) | computed by `tools\visual` on the parent and the commit at review time, never stored | t05 open (no test reads them; t07 can drop them once `visual_check` diffs two builds) |
