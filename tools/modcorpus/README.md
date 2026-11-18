# Mod compatibility corpus runner (mcx7)

Grades every installed FoC mod and stack G0-G3 (`docs/MOD_COMPAT_DESIGN.md` §1.3, §12-§13). Python standard library
only, run with `-I`.

| File | What |
|---|---|
| `run_corpus.py`, `run_corpus.cmd` | The sweep (stages below), the matrix, the drop check, `--write-md` |
| `grade.py` | The grade rules, written once (§12.2-§12.3), and the drop comparison; `--selftest` |
| `corpus.json` | Display names, the reference set's map / campaign picks (`docs/MODS.md` §10.3-§10.4) and accepted blockers |
| `test_corpus.py` | ctest `alamo_modcorpus_tests` (no game data): grade selftest, sweeps of the conformance mods `stacks` and of `mod_fixture`, a deliberately broken mod that must drop a grade and make the runner exit 1, `--write-md`, the cache |
| `asan_sweep.cmd`, `install_asan_sweep_task.cmd` | t05: the weekly ASan sweep; the installer registers it in Task Scheduler (Gabriel runs it; agents never do) |

The mod goldens (t04) are `tests/goldens/mods/` (spec `<name>.json`, golden `<name>.crc`), run by
`tools/goldens/mod_golden.py` (ctest `alamo_headless_mod_golden_<name>`, slow) and remade by `tools\regen_goldens.cmd --mods`.

## A sweep

```
tools\modcorpus\run_corpus.cmd                       every setup, every stage (about 10-20 min per big mod)
tools\modcorpus\run_corpus.cmd --only 1125571106      one setup (key or part of the name); the day's matrix keeps the rest
tools\modcorpus\run_corpus.cmd --stages check         load reports only (grades up to G1)
tools\modcorpus\run_corpus.cmd --write-md             also replace the generated table in docs/MODS.md §10.4
```

1. **Setups.** `alamo_mod_check --list-mods` lists the registry (`docs/MODS.md` §3.1.1): every workshop item with a
   `Data` folder in every Steam library, then `<game>\Mods\*`; a folder with `<variant>-modinfo.json` files gives one
   setup per variant (2083545253 `subA` on Thrawn's Revenge, `subB` on Fall of the Republic). Each setup is resolved like
   the launcher does (`--resolve`: declared dependencies, else the submod hint's parent), so a submod runs on its parent.
   A workshop folder without `Data` is not listed (nothing to mount).
2. **check**: `alamo_mod_check --resolve --json` (report v4, asset cache in `logs/modcorpus/asset_cache`).
3. **runs** (skipped for a G0 setup): space and land skirmish on the mod's map (corpus.json's pick, else the first
   Official-tab map the mod itself ships, else the first listed, then the same on the Custom tab), 3000 ticks at workers 0 and 4, CRC logs compared with
   `alamo_headless --compare`; one galactic campaign, 9000 ticks, `--economy --galactic-ai --story --scoring
   --object-scripts`, workers 4 (corpus.json's pick, else the first campaign with planets in database order). `[port]`
   choice: the design's "first the menu lists" is read as database order, since the report does not sort by menu.
4. **smokes**: `alamo_game --smoke skirmish` and `--smoke campaign`, `--hidden --audio null` (needs a GPU). A smoke that
   fails on a step timeout (steps wait in frames, which run out when another lane loads the machine) is run once more;
   the matrix marks it `(retried)` and a second failure counts.
5. **--asan**: the runs again with `build-asan\bin\alamo_headless.exe` (workers 4 only: the worker-count compare is the
   normal runs' job), `ASAN_OPTIONS=detect_container_overflow=0` (MSVC STL false positives, `docs/BUILD.md`), timeouts
   times 8. A sanitizer report keeps the setup below G2.

Each headless / game run holds a machine test slot (`tools/lint/test_slot.py`); its timeout (`--timeout`, 1800 s)
counts from when it has the slot. Results are cached in `logs/modcorpus/cache` by build (the exes' size and write time),
options and every stacked mod's content hash: an unchanged mod on an unchanged build is not run again, but its grade
is computed again with the current rules and `corpus.json` accepts. `--no-cache` runs everything.

## Output and exit code

`logs/modcorpus/<date>/`: `matrix.json` (schema `alamo.mod_corpus` 1: per setup the grade, blockers, accepted blockers,
stack, runs, smokes), `matrix.md`, `<setup>.json` (the full result) and `<setup>/` (each command's output, the load
report). The sweep compares with the accepted baseline of its mode (`logs/modcorpus/baseline.json`, `baseline-asan.json`
for `--asan`, or `--baseline FILE`): a lower grade prints `GRADE DROP <key> (<name>): G2 -> G0, first new blocker: ...`
and exits 1. A previous grade above what this sweep's stages can give is capped first (a `--stages check` sweep is never
a drop). A sweep without a drop updates the baseline (when it ran at least the baseline's stages); after a drop the
baseline stays, so every later sweep fails again until the drop is fixed or recorded with `--accept-drops`. Results with
a timeout, a skipped smoke or a tool failure are not cached. A workshop item without a `Data` folder is graded G0
("nothing to mount"); a crash of `alamo_mod_check` itself is G0 too, with a `port:` blocker (a port failure).
Exit 2: no game folder or a tool missing.

## Accepted blockers

`corpus.json` `accept` lists blockers (regex) explained as the mod's own design or retail-equivalent, each with `why`
and `ref`; they are listed in the matrix and do not lower the grade. An entry of a mod applies to every setup whose stack
contains it. Add one only with the explanation recorded where its `ref` points (`docs/MODS.md` §10.4, a `docs/re` page).

## Limits

- G1's "every warning is retail-equivalent or a filed gap" (design §1.3) is not checked automatically: G1 needs 0 errors.
- G2's "deterministic on both compilers": a sweep runs one build (MSVC); the clang side is checked by the mod goldens of
  the reference set (`tests/goldens/mods`, ctest on both compilers), not per corpus mod.
- The galactic campaign is the first one with planets in database order (`[port]`: the report does not sort campaigns as
  the menu does), unless `corpus.json` names one.
