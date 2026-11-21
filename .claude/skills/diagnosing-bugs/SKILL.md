---
name: diagnosing-bugs
description: Phased loop for hard bugs - a failing or flaky test, a golden or CRC that moved, a worker-count or MSVC-vs-clang difference, a parity-harness mismatch, a crash, hang or desync, a slow tick or frame. Use when asked to diagnose, debug or find the cause of something broken, or before the second guess at a fix. Builds a fast red/green signal first, then ranked hypotheses, then the fix with a regression test.
argument-hint: <symptom or failing test>
---
# Diagnosing bugs

A fast, trustworthy red/green signal does most of the work. Build it before theorising; reading code to form a theory
without one is the failure mode this skill prevents. Adapted from `mattpocock/skills` (MIT) to this repo's tools.

Ground rules, all phases:
- Retail behaviour: `docs/re/` first, then look yourself (REA, `re_reference/`, cdb on a TTD trace; never copy decompiled code
  into the repo) or ask through `re-question`, then a probe kit that you run on retail yourself (rule 7; never the port's `alamo_game.exe`). A diagnosis that needs a retail fact it does not have stops and says so.
- Build and test only through the scripts (`build_agent.cmd`, `build_module.cmd`); never hand-configure a build tree.
  A failing rerun that is identical is refused (exit 3): change something, or read the first failure again, before `ALAMO_RERUN=1`.
- A hung test exe of yours: `python -I tools/worktree/stop_tree_procs.py`.
- A crash, hang or save corruption is fixed even under the tier 1/2 focus; a tier-3 performance regression is filed, not started
  (`CLAUDE.md` rule 6).

## 1. Build the loop

Goal: one command, run and shown, that is red on this bug, deterministic, fast (seconds, not a full gate) and agent-runnable.
Pick the cheapest that fits, roughly in this order:

| Symptom | Loop |
|---|---|
| A ctest entry fails | `& .\build_agent.cmd --% msvc wt <module> <test regex>` (tree `wt` spelled out when a single module is followed by a regex; a regex runs even `slow` tests; the `skips:` line says what did not run) |
| A golden or CRC moved | `alamo_headless <scenario> --crc-out new_full.crc` against the golden's scenario, then `--compare a.crc b.crc` (names the first differing tick and system column; full logs, no `--crc-checkpoint`) |
| Worker-count or MSVC-vs-clang difference | The same scenario at worker count 0 and N, or the msvc and clang trees; then `--compare`. This is always a real bug, never drift (`determinism-check` skill) |
| Parity-harness mismatch | The scenario from `tools/parity/scenarios.json`, shrunk to the fewest ticks and seeds that still differ |
| Behaviour regressed between commits | `git bisect run` with the loop above as the script |
| Flaky | Rerun the one test in a loop with `ctest --repeat until-fail:N`; two parallel entries sharing a `%TEMP%` fixture or output path are the first suspect (`CLAUDE.md`, parallel ctest) |
| Crash or hang in `alamo_game` | A headless scenario that reaches the same system; the game itself is Gabriel's to launch |
| Slow tick | `alamo_headless ... --profile` (wall-clock only, sim code never reads it); baseline in `docs/PERFORMANCE.md`. Measure first, then bisect; logs are usually the wrong tool for a slowdown |
| Same input, two configs or versions | Differential loop: run both and diff the outputs (CRC logs with `--compare`, load reports, `apitrace dump` of two captures) |
| Only a person can trigger or see it (the port's `alamo_game.exe`) | Last resort: `scripts/hitl-loop.template.sh`, copied and edited, run by Gabriel; you read its `KEY=VALUE` output |

Then tighten it: faster, a sharper assertion, deterministic. For a nondeterministic bug raise the reproduction rate (repeat,
vary worker counts, add load) instead of chasing a clean repro.

Done when the command is shown red. If no loop can be built, stop: list what was tried and ask for the missing artifact, access
or permission to add instrumentation. No hypotheses without a loop.

## 2. Reproduce and minimise

Confirm the loop shows the reported symptom, not a nearby failure. Write the exact symptom down (test name, first differing tick
and column, error text) so the fix is verified against it later. Then cut input, ticks, seeds, mods, config and steps one at a time,
rerunning after each cut. Done when every remaining element is load-bearing.

## 3. Hypothesise

Write 3 to 5 ranked hypotheses before testing any. Each is falsifiable: "if X is the cause, changing Y makes the bug disappear
(or Z makes it worse)". Drop or sharpen any without a prediction. Show the list; the user may re-rank it. Do not block on an answer.

The first candidates for a sim difference are the `determinism-check` list (unordered iteration, parallel writes, FMA or CRT math,
uninitialised or padding bytes, outside inputs, scheduling, new state missing from the CRC and save).

## 4. Instrument

One probe per prediction, one variable at a time. A debugger before logs. Logs go only at the boundary that separates two
hypotheses, never "log everything and grep". Tag every debug line with a unique prefix such as `[DEBUG-a4f2]` so cleanup is one
`Grep`. Sim code never logs a profiler value or wall-clock time into anything the CRC reads.

## 5. Fix and regression test

Write the test before the fix, at a seam that exercises the real bug pattern at its call site (a shallow test that cannot fail
for the real reason does not count). In this repo that means a test in the module's test exe registered with `add_test`; a long
scenario gets one ctest entry per worker count (`alamo_add_worker_count_check`), `slow` label over about 5 s, and no fixed
`%TEMP%` path. Confirm it fails, apply the fix, confirm it passes, rerun the phase 1 loop. If you forced the red by mutating
code or a fixture, diff against a pristine copy to prove the mutation landed before trusting the result.
No fitting seam is a finding: record it (backlog, or the task for a Stage 6 group) because the architecture is blocking a lock-down;
the `codebase-design` skill has the vocabulary for describing the seam that is missing.
Never declare a behaviour change to get green; a result that moved on purpose is declared in the `Behaviour:` trailer
(`behaviour-check` skill).

## 6. Cleanup

Done when all of these hold:
- the original loop no longer reproduces, and the regression test passes (or the missing seam is filed);
- `Grep` for the `[DEBUG-` prefix finds nothing, and throwaway harnesses are deleted or under a clearly named debug folder;
- the build summary shows 0 warnings on every compiler the change needs (`CLAUDE.md`, build section);
- the commit message names the hypothesis that was correct and the ones ruled out, so the next debugger learns from it.
Facts about retail behaviour found on the way go to the `docs/re/` spec, per `docs/README.md`.
