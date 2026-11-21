## Golden checkpoints (Q1 goldens, 2026-11-06)

**Since wr1 t07 (2026-11-21) no derived golden is stored in git.** The CRC log formats below are still what the programs write
and compare (worker-count entries, `--compare`, scenario results); stored-golden regeneration is gone. How behaviour changes are
checked now: "Computed results and the `Behaviour:` check" below; what replaced each former file: `golden-inventory.md`.

Why (2026-11-06): a golden CRC file (`engine/apps/headless/testdata/*.crc`, `engine/apps/game/testdata/game_smoke_*.crc`, the sim and module goldens) held one
line of per-system CRCs per tick (8198 lines for `skirmish_ai_foc_golden.crc`), so every behaviour change rewrote thousands of lines and the PRs
conflicted in git. A checkpoint golden stores every 64th tick: the same check, about 1.5 % of the size, and a change moves a few lines.

### Format `ALAMO_CRC 2`

```
ALAMO_CRC 2
systems <count> <names...>        same header as ALAMO_CRC 1
parity <count> <names...>
checkpoint <N>                    N > 0 (64 for the checked-in goldens)
keep <tick> <tick> ...            optional: extra stored ticks, increasing
# comment lines                   kept as they are (seed, ticks, ...), ignored by compare
<tick> <combined> <parity> <crc0> <crc1> ...   one line per stored tick
```

Stored ticks: the first record, the last record, every tick with `tick % N == 0` and every tick of the `keep` line. Nothing else is stored.
`ALAMO_CRC 1` (every tick) is unchanged and still what `--crc-out` writes by default: the worker-count tests (`alamo_add_worker_count_check`)
compare full logs with each other. Code: `CrcLog::checkpointed` / `to_text` / `parse` / `first_divergence` in `engine/core/.../sim_crc.hpp`;
tests `alamo_core_sim_tests` (round trip, mismatch message, v1 against v2) and `alamo_headless_crc_checkpoint` (C++ and Python writers byte for
byte, `--compare`, the game smoke compare).

### Comparison

`--golden F` (every `alamo_headless` run mode), `--compare EXPECTED ACTUAL`, `first_divergence()` and `engine/apps/game/compare_smoke.cmake`:

- Two full logs compare exactly as before (same messages).
- When either log is a checkpoint log, only its stored ticks are compared (the expected log's when both are): each must exist in the other log
  with the same CRCs, and both logs must end at the same tick (a longer or shorter run is a mismatch).
- A mismatch reports the first bad checkpoint, the window of ticks it lies in (the first differing tick is after the previous checkpoint that
  matched, or the checkpoint itself when it is the first record), the first differing system and every differing column (systems, `parity`,
  `combined`), and says how to get the exact tick: run both sides with `--crc-out full.crc` (no `--crc-checkpoint`; the expected side is the
  code before the change) and run `alamo_headless --compare expected_full.crc actual_full.crc`.
- A change between two checkpoints that is gone again by the next one is not seen. That is the price; use `--crc-keep T,...` for ticks that
  must be pinned, or a smaller N for a golden.

### Tools

| Tool | What |
|---|---|
| `--crc-out F --crc-checkpoint N [--crc-keep T,T,...]` | any `alamo_headless` run writes the checkpoint log instead of the full one |
| `alamo_headless --crc-convert IN OUT [--crc-checkpoint N] [--crc-keep T,...]` | rewrites a CRC log as a checkpoint log (default N 64) |
| `alamo_headless --compare EXPECTED ACTUAL` | compares two logs with the rules above (exact tick of a difference: two full logs) |
| `python tools/goldens/convert_goldens.py` | the same format rules in Python (the parser `behaviour_check.py` uses; a ctest checks the two writers agree byte for byte) |

`tools\regen_goldens.cmd`, `tools\lint\golden_merge.py` and `tools\benchmark\regen_scenario.cmd` were retired by wr1 t07: nothing is
regenerated. Benchmark scenarios are made per build from their recipe (`tools/goldens/bench_scenarios.py`, wr1 t06); the mod
scenarios run from `tests/goldens/mods/<name>.json` (`tools/goldens/mod_golden.py`, nightly tier).

## Computed results and the `Behaviour:` check (wr1, 2026-11-21; authoritative since t07)

Stage 12 replaced stored derived goldens with results computed from builds (`docs/DECISIONS.md`, "Workflow redesign"). Skill:
`behaviour-check`.

- Inventory: `docs/status/golden-inventory.md` lists every former stored expected-result file as derived (removed by t07,
  now a scenario) or reference (stays tracked).
- `tools/goldens/scenario_results.py` (t02): a scenario is an entry of `tools/goldens/scenarios.json`; its result folder
  (the run's CRC log with `--crc-out`, or the files a test exe writes into `<out>`) is stored under the main checkout's `.cache/scenario-results/` (gitignored) and keyed by
  the commit's `engine/`, `CMakeLists.txt` and `tests/benchmark` trees, the command, the game-data identity, compiler and
  worker count. A docs or tools commit keeps every key. `result --commit C` builds another commit in the locked worktree
  `.claude/worktrees/scenario-base` (tree `build-base-<compiler>`) when no entry exists. Nothing is stored from a tree with
  uncommitted source edits; the cache is capped (`ALAMO_SCENARIO_CACHE_MB`, default 4096) and drops the least recently used
  entries.
- `tools/goldens/behaviour_check.py` (t03, t08): `commit [REV]` diffs every scenario of a commit against its first parent
  over all ticks and judges the commit's `Behaviour:` trailer (`none`, or the CRC system columns it changes); a sim commit
  without the trailer fails. `workers` and `compilers` compare worker counts with the 0-worker run and the clang tree with
  the MSVC tree for one commit, with no stored file (the cross-compiler check stored goldens gave for free).
  `--parity-before/--parity-after` (or `parity A B`) add which parity-harness metrics moved. `range BASE HEAD` is the
  verifier's landing check (every commit's trailers together); for sim landings the verifier also runs `compilers` on its
  two trees. `nightly` runs the scenarios marked `nightly` from the last gated night (`tools\gate_scheduled.cmd`).
- Scenarios (t05, t07): `tools/goldens/scenarios.json` holds every former derived golden's run (headless skirmishes and
  replay, module and sim test exes, galactic AI logs, game smoke runs, mod scenarios). Each runs its test program so that it
  writes into a result folder (`--write-golden <out>/...`, `--golden-dir <out>`, `--golden-out <out>` for the exes that
  used to rewrite tracked files in place, `--png-dir <out>`, `--ai-log`, `--crc-out`). The checker compares a result
  folder file by file: CRC logs by column; other text and binary outputs byte for byte (registry `ignore` lines left out),
  a difference charged to the scenario's `system`; screenshots with `png_diff.py` tolerances, reported but never judged.
  A scenario whose needs are missing (game data, GPU, workshop item) is a SKIP; one the parent's registry lacks or runs
  differently has no parent result (its first landing). Before t07 deleted the stored files, the `goldens` command showed
  28 of 29 scenarios reproducing them (the 29th, `skirmish_ai_foc_land`, was stale on dev: ngt1 t04).
- Screenshots: `tools\visual\visual_check.cmd parent [REV]` diffs REV's smoke screenshots against its parent's
  (`tools/visual/visual_builds.py`), the replacement for `tests/visual_baselines`.

Names a `Behaviour:` trailer can list: the CRC system columns (the `systems` line of a log: `frame`, `objects`,
`galactic.state`, `spatial`, `players`, `rng`, `commands`, `galactic.conflicts`, `battle.state`, `battle.movement`,
`battle.combat`, `battle.abilities`, `battle.ai`, ...), plus the charge names of non-CRC outputs in `scenarios.json`:
`galactic.ai` (galactic AI logs and digests), `story` (story event logs, sim_tests), `dmath` (`alamo::dmath` digests),
`shaderxlat` (emitted HLSL); `commands` also covers recorded command streams (`.acmd`, smoke `.rec`), `battle.ai` the
tactical AI digests, `battle.state` / `battle.abilities` / `battle.combat` / `galactic.state` a module's CRC run when its
file is not ALAMO_CRC.

Commit trailer, from now on for every commit that touches sim code (the last line block of the message):

```
Behaviour: none
Behaviour: movement, combat
```
