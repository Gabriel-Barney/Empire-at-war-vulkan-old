---
name: to-tickets
description: Break a plan, spec, research report or the current conversation into tracer-bullet roadmap work - vertical slices as group tasks, blocking edges as task order and group `after`, each group with an exit test - and, once approved, write it to roadmap-store. Use when asked to plan, break down, split into tasks or tickets, turn a design / spec / docs/re finding into roadmap work, or make a new group.
argument-hint: [spec path | backlog ids | group id]
---

# To tickets

Break a plan, spec or conversation into **tickets**: tracer-bullet vertical slices, each declaring what **blocks** it, written to
the roadmap. Adapted from `mattpocock/skills` (MIT, `.claude/skills/LICENSE-mattpocock-skills.txt`): there tickets go to an issue
tracker; here a ticket is a **task** of a roadmap **group**, and the rules of `docs/ROADMAP_PROTOCOL.md` apply (exit test, cap 8,
`after`, `sub`, tier). Change the roadmap only through `python roadmap-store/roadmap_store.py` (sync first).

## 1. Gather context

Work from what is in the conversation. A reference passed in (a `docs/` path, backlog ids, a group id) gets read in full: the
spec, the entries (`backlog list --full`), the group (`roadmap-store/data/items/<id>.json`) and the `docs/re/` pages it cites.

## 2. Explore the code (optional)

If you have not yet, look at the current code so tickets use the project's terms (`docs/PHASE4.md` step map, `docs/re/`, XML / Lua
names) and respect `docs/DECISIONS.md`. Look for prefactoring that makes the work easy ("make the change easy, then make the easy
change"); a prefactor is its own first ticket. Under the current focus a prefactor counts only when it serves the group's exit test.

## 3. Draft vertical slices

Break the work into tracer-bullet tickets:
- Each cuts a narrow but **complete** path through every layer it needs (data / XML loading, sim rule, Lua binding, save, UI or
  headless output, test): vertical, not one layer at a time.
- Each is verifiable on its own: a ctest entry, a parity metric, a headless scenario, a `docs/re/` fact matched.
- Each fits in one fresh context window, so one lane session can finish it.
- Prefactoring comes first.
- Research tickets (an RE question, a probe) state their stop condition (the question answered, seeds, traces) and go first when
  later tickets depend on the answer.

Give each ticket its **blocking edges**. Inside one group, blockers come earlier in the task order (`o`); a ticket blocked by work
in another group means that group goes in this group's `after` (only hard waits). A ticket with no blockers can start at once.

**Wide refactors are the exception.** One mechanical change whose blast radius crosses many modules (renaming a shared type,
changing an `engine/core` public header) cannot land green as one slice. Sequence it as **expand, migrate, contract**: add the new
form beside the old; migrate callers in batches (per module), each its own ticket blocked by the expand, with `dev` green after each
because the old form still exists; then delete the old form in a ticket blocked by every batch. `engine/core` public headers and
the top-level CMake go through the integrating agent (CLAUDE.md).

**Group the tickets.** At most 8 tasks per group; more means more groups, chained by `after`. Each group gets one **exit test**
that the tickets' acceptance criteria add up to (the metric or test that says the group is done), a `node` (flowchart box), `mod`
(the modules it touches), `eta` and a `tier` on parity tasks. Tier-3 work is paused (current focus): leave it out or mark it.

## 4. Quiz the user

Show the breakdown as a numbered list per group: title, blocked by, what it delivers, and the group's exit test. Ask:
- Is the granularity right (too coarse / too fine)?
- Are the blocking edges right: does each ticket depend only on what really gates it?
- Merge or split any?

Iterate until approved. In an unattended run, stop here: write the proposal to a scratch file, report it, and write nothing to the
roadmap (a new group is Gabriel's call unless he asked for it).

## 5. Write it to the roadmap

Write the approved groups with one `python roadmap-store/roadmap_store.py write <scratch file>` (a JSON list of `set` ops on
`items/<id>`; shapes in `docs/ROADMAP_PROTOCOL.md`), blockers first. Each task: `t` (the ticket as an agent brief, see the
`triage` skill's AGENT-BRIEF.md: behaviour, acceptance, out of scope, no line numbers), `s: "todo"`, `o`, `sub` (Parity, Engine,
Process or Research) and `tier` where it applies. Tickets added to an existing group instead: `backlog add "<brief>" --by <session>
--sev S1|S2 --to <group>`, or a `write` that adds the tasks (respect the cap). Then `roadmap_store.py lint` and one `log` line.

Work the **frontier**: a ticket whose blockers are all done. Do not close or change the group or entries the work came from beyond
what was approved (`backlog promote` the entries that became tasks, so they are not left open twice).

Avoid file paths and code in ticket text; they go stale. Exception: a snippet from a prototype that pins a decision better than
prose (a state machine, a data layout); trim it to the decision and say where it came from.
