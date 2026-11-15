---
name: run-group
description: The default workflow for a request to complete roadmap work - "complete all tasks under AUD1", "do aud3", "finish skai", "work the AUDIO box", "clear stage 6", "pick up the next ready group". Resolves the group, box or stage, checks prerequisites, claims and the current focus, then runs every group through the full lane loop (start-group, build test-first, review, finish-group, land on dev), in this session for one group or in up to three parallel sessions for several.
argument-hint: <group id | box id | stage | "next"> [more ids]
---

# Run a group (the default workflow)

A request to complete roadmap work always goes through this skill, whatever its wording. It ties the project's pieces together:
the `lane` loop (`.claude/agents/lane.md`), the skills it calls, the roadmap rules (`docs/ROADMAP_PROTOCOL.md`) and the tool
index (`docs/TOOLS.md`). Every tool and skill is available without asking (`docs/DECISIONS.md`, "Default tool access").

## 1. Resolve what was asked

`python roadmap-store/roadmap_store.py sync`, then match the id case-insensitively:
- **A group** (`roadmap-store/data/items/<id>.json`, e.g. `aud1`): that group.
- **A box** (`nodes/<ID>.json`, e.g. `AUDIO`): its groups that are not `done`.
- **A stage** or **"next"**: `roadmap_store.py batch 3` lists ready groups whose `touches` do not overlap.
- Several ids: each as above.

For each group show: title, open tasks (with tier and `g`), `exit` test, `after` and whether each is done, `claimed_by`.

## 2. Check before starting

- **`after` not done**: the group waits. In a multi-group run it goes after its prerequisite in the same queue; alone, stop and say
  which group blocks it.
- **Claimed by another session** (`roadmap_store.py claims`): skip it and say so.
- **Current focus** (CLAUDE.md rule 6): tier-3 tasks are skipped, not started. Crashes, hangs and save corruption are always fixed.
- **No `exit` test**: write one first (`update items/<id> {"exit": "..."}`); it is how the group finishes.
- **Tasks only Gabriel can do** (`g`): leave them open, do the rest, list them in the report.
- **Vague tasks**: sharpen them before coding (`to-tickets` to split; `grill-with-docs` when Gabriel is here to answer).

## 3. Decide how to run it

- **One group**: run it **in this session**, as the lane. This session keeps the `Agent` tool, so `re-analyst`, `reviewer` and
  parallel design or review agents work directly. Work in this session's own worktree (an app session already has one; in the
  main checkout, first `tools\new_worktree.cmd <id>` and work there).
- **Several groups**: at most **three lanes at once**, each a separate session with a sequential queue of groups (prerequisites
  first, groups that touch the same modules in the same queue). In the desktop app start each with `start_session`
  (`initiation: "user_asked"`, `context: "fresh"`, a worktree of its own, prompt `/run-group <id> [<id> ...]` plus the reason for
  the order). Without sessions, spawn `lane` agents (they have no `Agent` tool, so ReVa questions go through a `re-question`
  file). Then coordinate: answer their questions, review what lands, start the next queue when one finishes.

## 4. The lane loop, per group

1. **Start**: `start-group <id>` (claim, worktree check, plan, `/goal` from the exit test).
2. **Know before you code**: retail behaviour from `docs/re/`; if it is silent, look yourself (REA, `re_reference/`, cdb on a TTD
   trace) or ask (`re-question`); only what the running game shows becomes a `probe-kit` run. Techniques and formats: `research`.
   Never copy decompiled code into the repo.
3. **Build each task test-first**: `tdd` (vertical slices, expected values from `docs/re/` or retail), `codebase-design` when an
   interface is in question, `prototype` when a rule or screen must be felt out first. Toolchain programs via `tools\tc.cmd`.
4. **Green**: `build_agent.cmd ... changed` while iterating, `green-fast-tier` after a large edit, clang too for sim code,
   `build_asan.cmd` for threading code, `determinism-check` for sim code. Something unexplained: `diagnosing-bugs`. A golden that
   moved on purpose: `regen-goldens`; never edit goldens to get green.
5. **Findings on the way**: `triage` (S1 / S2 become tasks of the group that owns them, S3 the backlog, tier 3 dropped or parked).
6. **Finish**: `finish-group <id>` (fast tier, `/code-review` + `spec-review`, roadmap: tasks done, group / box done or
   `close-group`, docs, `tools\land_on_dev.cmd`).

Work task by task; commit after each; keep going until the exit test passes or every remaining task is blocked on Gabriel.

## 5. Report

Per group: tasks closed and left open (with why), the exit test result, the `dev` commits, goldens changed, skips, anything Gabriel
must do (a `g` task, a decision, a UAC prompt, a look at `alamo_game.exe`), and one friction line for `retro`.
