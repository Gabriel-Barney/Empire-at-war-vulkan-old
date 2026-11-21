---
name: behaviour-check
description: Declare a commit's behaviour change (`Behaviour:` trailer) and read the behaviour check's diff report (tools/goldens/behaviour_check.py), which replaced stored golden CRC files in wr1 t07. Use before committing any sim change, when the verifier rejects a landing on the behaviour or compiler check, when a report names a scenario, tick window and columns, or when someone asks to "regenerate", "update" or "refresh" goldens (there are none to regenerate).
argument-hint: [commit, default HEAD]
---
# Behaviour declaration and the diff report

Since wr1 t07 (2026-11-21) no derived golden is stored. Each scenario of `tools/goldens/scenarios.json` (headless
skirmishes, the sim / AI / module test exes, galactic AI logs, game smoke runs, mod goldens) is run on the parent and on
the commit, and `tools/goldens/behaviour_check.py` compares the two results. The commit message says what may differ.
Format, scenario fields and charge names: `docs/status/golden-checkpoints.md`, "Computed results".

## 1. Declare

Every commit that touches sim code (`engine/` except render, audio, video, ui) ends with one trailer line, before
`Co-Authored-By:`:

```
Behaviour: none
Behaviour: battle.combat, battle.ai
```

- `none`: no scenario result may change. Use it for refactors, tests, tools, comments.
- A list: the CRC system columns (`systems` line of a CRC log) or charge names (`galactic.ai`, `story`, `dmath`,
  `shaderxlat`, `commands`) that may change. Name only what the change is meant to move; `combined` follows by itself,
  `parity` follows a declared parity part.
- A sim commit without the trailer is rejected even when nothing changed.

## 2. Check before submitting

```
python -I tools/goldens/behaviour_check.py commit HEAD            # this commit against its parent (fast scenarios)
python -I tools/goldens/behaviour_check.py range origin/dev HEAD  # what the verifier runs on a landing
```

Results are cached under `<main checkout>/.cache/scenario-results/` by source, scenario, data, compiler and worker
count; the parent is built once in `.claude/worktrees/scenario-base` when not cached. Commit first: a tree with
uncommitted engine edits runs but never caches.

## 3. Read the report

```
  FAIL skirmish_ai_foc: Behaviour: none, but differs in battle.combat, combined (first difference in ticks 129..192)
  PASS sim_tests: declared change: synthetic_golden.crc: differs in battle.state ...; story_synthetic_golden.txt: differs from line 14 (charged to story)
  SKIP game_smoke_land: skipped, needs a GPU
```

- `FAIL ... Behaviour: none, but differs in X`: the change moved X. Intended: declare X. Not intended: a regression,
  find it (`diagnosing-bugs`; the tick window bounds the first difference: rerun both sides with `--crc-out` and
  `alamo_headless --compare A B` for the exact tick).
- `FAIL ... undeclared change in X`: declared something, but X also moved.
- `only the run length changed`: declare the systems that end the run earlier or later.
- `new scenario or run ... no parent result`: the commit adds or changes a scenario's run; nothing to compare yet.
- `screenshots (report only)`: rendering differs; never judged. Look at the diff images:
  `tools\visual\visual_check.cmd parent` writes them to `logs\visual_diff\`.
- `(runs exited N / M)`: the test exe's own CHECKs failed on one side; fix that first.

## 4. Determinism failures are never a declaration

`behaviour_check.py workers` (every worker count against 0) and `compilers` (clang tree against MSVC tree; the nightly
gate runs it on every scenario) must be identical: a difference there is a bug (`determinism-check`), not a behaviour change,
and no trailer excuses it.

## Nightly scenarios

Scenarios marked `nightly` (GPU smoke runs, the long galactic AI campaign, mod goldens) are not in the landing check:
the nightly gate runs `behaviour_check.py nightly` over the night's range (`logs\behaviour_nightly.txt`). Before landing a
change to rendering or the game shell, run `commit HEAD --nightly --only game_smoke` yourself.
