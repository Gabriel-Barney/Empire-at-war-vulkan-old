## Golden checkpoints (Q1 goldens, 2026-11-06)

Why: a golden CRC file (`engine/apps/headless/testdata/*.crc`, `engine/apps/game/testdata/game_smoke_*.crc`, the sim and module goldens) held one
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
| `python tools/goldens/convert_goldens.py [--all] (--in-place \| --out-dir D) --check FILE...` | the same rule in Python for a list of files (or every tracked ALAMO_CRC file under `engine/` with `--all`); `--check` proves each converted file equals its source on every checkpoint; `--verify GOLDEN SOURCE` compares two files; a ctest checks the two writers agree byte for byte |
| `tools\regen_goldens.cmd [--only headless\|game\|all] [--dry-run] [--force]` | after an intended behaviour change: runs the golden-backed ctest commands of `build-msvc` through the program's own writer (`--crc-out ... --crc-checkpoint N`), stores the checkpoint goldens (headless; with `--only game` also the three game smoke runs, which need the GPU and the game data) and prints a diff summary: files changed, first changed checkpoint, window, differing columns. It refuses to run when tracked files have uncommitted edits unless `--force` |
| `tools\regen_goldens.cmd --bench-check` / `--no-bench` / `--only bench` | BM1f: the benchmark scenarios (`tests/benchmark/*.abench`) carry a CRC fingerprint, so a sim behaviour change makes them stale. `regen_goldens` (with `--only headless` or `all`) checks each scenario (`alamo_headless --bench X --runs 1 --warmup 0`, exit 3 = stale) and remakes a stale one through `tools\benchmark\regen_scenario.cmd`; the summary shows `version 1 -> 2`. BM3: a `host game` scenario is checked with a 120-tick hidden `alamo_game --bench-scenario` run (exit 3 = stale) and remade with `alamo_game --hidden --bench-regen` (the tree needs `alamo_game.exe`). `--dry-run` and `--bench-check` only report (exit 4 from `--bench-check` when a scenario is stale); `--no-bench` skips them |

### Workflow

1. Change behaviour, build (`build_msvc.cmd`); the golden tests fail with the first bad checkpoint and its window.
2. Commit the code, run `tools\regen_goldens.cmd`; the summary names the first changed checkpoint per file: state the intended behaviour change in
   the PR from it. Commit the goldens (a few lines each) and any remade `tests/benchmark/*` scenario files (the version bump shows in the PR; say "scenario regenerated" there).
3. Clang must reproduce the MSVC goldens (`build_clang.cmd`).

Mod goldens (mcx7 t04, `docs/MOD_COMPAT_DESIGN.md` §13.3): `tests/goldens/mods/<name>.crc`, a 600-tick space skirmish of each
reference mod (the run in `<name>.json`), ctest `alamo_headless_mod_golden_<name>` (slow) through `tools/goldens/mod_golden.py`, which
skips when the workshop item is not installed or its data changed since the golden (the spec's `fingerprint`). `tools\regen_goldens.cmd`
remakes them with the headless goldens (`--no-mods` leaves them alone, `--mods` does only them) and sets the fingerprint; name every moved
mod golden in the commit message as for a base golden.

Goldens the regen script does not cover (the apps/sim goldens written by `alamo_sim_tests --regen`, the module goldens of combat / abilities /
galactic written by their own `--write-golden` options) are still written as full logs; they compare as they are, and `convert_goldens.py
--in-place --check <files>` thins them afterwards.

### Converting the checked-in goldens (once, after the PRs that edit them have merged)

`python tools/goldens/convert_goldens.py --all --in-place --check` (or only the files you want), run the fast tier, commit. Files that were
converted keep their `checkpoint` / `keep` settings on later runs.

## Computed results and the `Behaviour:` check (wr1, 2026-11-21: expand phase)

Stage 12 replaces stored derived goldens with results computed from builds (`docs/DECISIONS.md`, "Workflow redesign").
Until wr1 t07 removes them, the stored goldens above stay in force and the tools below run beside them.

- Inventory: `docs/status/golden-inventory.md` lists every stored expected-result file as derived (to be computed) or
  reference (stays tracked).
- `tools/goldens/scenario_results.py` (t02): a scenario is a golden-backed headless ctest entry; its result is the full
  CRC log of the same command, stored under the main checkout's `.cache/scenario-results/` (gitignored) and keyed by
  the commit's `engine/`, `CMakeLists.txt` and `tests/benchmark` trees, the command, the game-data identity, compiler and
  worker count. A docs or tools commit keeps every key. `result --commit C` builds another commit in the locked worktree
  `.claude/worktrees/scenario-base` (tree `build-base-<compiler>`) when no entry exists. Nothing is stored from a tree with
  uncommitted source edits; the cache is capped (`ALAMO_SCENARIO_CACHE_MB`, default 4096) and drops the least recently used
  entries.
- `tools/goldens/behaviour_check.py` (t03, t08): `commit [REV]` diffs every scenario of a commit against its first parent
  over all ticks and judges the commit's `Behaviour:` trailer (`none`, or the CRC system columns it changes); a sim commit
  without the trailer fails. `workers` and `compilers` compare worker counts with the 0-worker run and the clang tree with
  the MSVC tree for one commit, with no stored file (the cross-compiler check stored goldens gave for free).
  `--parity-before/--parity-after` (or `parity A B`) add which parity-harness metrics moved.

Commit trailer, from now on for every commit that touches sim code (the last line block of the message):

```
Behaviour: none
Behaviour: movement, combat
```
