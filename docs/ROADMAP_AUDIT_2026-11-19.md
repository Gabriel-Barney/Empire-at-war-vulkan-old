# Roadmap audit and restructure: proposal (2026-11-19)

Phase 1 of Gabriel's audit request: a proposal only. Nothing has been written to `roadmap-store/`. Each goal's write batch is
a separate file in [`roadmap_audit_2026-11-19/`](roadmap_audit_2026-11-19/), to approve one by one. Session
`roadmap-audit-08a8b0`.

## Summary

| Count | Value |
|---|---|
| Open groups audited | 44 (41 `partly`, 2 `gab`, 1 `parked`), 183 open tasks |
| Open backlog entries audited | 190 (all S3: 106 tier 1, 31 tier 2, 41 tier 3, 12 untiered) |
| Out of scope | 0 groups, 0 tasks, 0 entries |
| Unclear, for Gabriel | 2 groups (`bm4`, `bo4`), 1 entry (b0315) |
| Dropped as obsolete or duplicate | 3 entries (b0091, b0132, b0410) |
| Backlog categories | 18 -> 14 (inbox included) |
| Entries re-filed by the category merge | 93 (open and closed; boxes unchanged) |
| Backlog entries clustered | 143 into 21 groups; 47 stay in the backlog (goal 4, option A chosen by Gabriel) |
| New groups | 21 (`goal4_backlog_groups.json`) |

**Baseline** (before any change): `roadmap_store.py sync` reported `in sync with origin/main`. `lint` reported 0 errors and 22 warnings:
18 open tasks without `sev` (sp3, sp4, sp6, sp7, fsr t06, xess t06, ui4 t14), 3 Parity entries without a tier (b0254,
b0260, b0261), and 17 started groups against a `wip` of 8. All five batches together were applied to a scratch copy of the
store (no git, no push) with the store's own `apply_write` / `auto_status`. Lint afterwards: 0 errors, the same 22 warnings.

**Claims.** `claims` shows no live claim. Seven groups still have an expired one: `bm4` (bm4-parallel-agents-3d6411), `bo4`
(lane-bo3), `modc` (lane-modc), `mov2` (lane-mov), `sp3` (simp-ai), `sp6` (simp-core) and `ui1` (lane-ui1). None of the
batches touches them.

**Inputs your prompt left open.** I filled them as follows. Change any of them and I will redo the affected goal.
- Scope source of truth: `README.md` "Goals" (core goals 1-5, side goals, "Scope", "Not pursued") and `docs/DECISIONS.md`
  ("Platform, compilers and scope", the decisions table, "Current focus", the named exceptions, "Out-of-scope tag-ledger rows").
- Recommended order of development: README's core-goal order. 1 play the real game (core systems -> menus / audio ->
  parity check -> QA), 2 mods, 3 use the whole PC (performance), then side goals and port features.
- Category target: "as few as make sense", with one category per flowchart box.
- Name of the pool category: `backlog-pool` (goal 4 only).

### Conflicts: I stopped here and did not pick a side

1. **Goal 4 against the backlog protocol.** These parts of goal 4 conflict with the protocol:
   - *One category that holds every entry*: every category is tied to one box (`docs/ROADMAP_PROTOCOL.md`, "Categories").
     `backlog move --cat` gives each entry the new category's box, so a pool category would overwrite the stage and box of
     all 190 entries. It would also leave the categories from goal 2 empty.
   - *Groups inside a category*: categories hold backlog entries, and groups live in boxes. A group with tasks made from
     entries is a promotion (`backlog promote --new`), and promoted entries leave the backlog.
   - *Every entry into a group*: 41 entries are tier 3. Under "Current focus" (DECISIONS, 2026-11-10) a tier-3 finding "is
     dropped or filed as an S3 backlog entry with `--tier 3`, never made a task". 12 untiered entries are "done only when
     it serves a tier-1 or tier-2 group's exit test".

   The options are in goal 4 below.
2. **"Delete nothing" against concurrency rule 5.** Rule 5 says out-of-scope tasks, groups and boxes are deleted after a
   `docs/DECISIONS.md` row is written. Your constraint says they become `oos` / `dropped`. This audit found nothing out of
   scope, so no batch is affected. The question is still open for any item you rule out of scope (open question 2).
3. **Stage reorder against stage numbering.** The page numbers stages by position (`page_shell.html` line 559-573:
   `Stage ${st.num}`). Moving stages renumbers them, and about 85 references break: "Stage 6 · Parity", "Stage 9 · Mod
   system" and "Stage 10" in `CLAUDE.md`, DECISIONS, the protocol, `lane.md`, the triage / finish-group skills and
   MOD_COMPAT_DESIGN, plus 21 in store texts. On 2026-11-19 you kept Stage 10 separate for exactly that reason (DECISIONS,
   "Mod system in one stage"). Ordering inside stages (goal 3a) has no such effect, so it is a separate file from the stage
   order (goal 3b).

## Goal 1: scope audit

Every open group serves one of README's core goals or a named exception in DECISIONS. Nothing is out of scope. Two groups are
unclear, not because they are hard but because the current focus pauses untiered work that serves no tier-1/2 exit test.

| Group | Box | Open | Verdict | Reason (scope doc) |
|---|---|---|---|---|
| aitac, mdu1, mapx1 | AIVIEW | 8, 2, 1 | in scope | README goal 1 (AI with the original's rules); tier 1/2 |
| mov2 | MOVE | 8 | in scope | goal 1, MOVEMENT tier 1 |
| tru2, trl2 | ABIL | 4, 8 | in scope | goal 1; trl2 t01 is S1 |
| r3i, r3j, anm1 | ANIM | 1, 4, 3 | in scope | goal 1, feel and animation timing are tier 1 (Parity tiers) |
| gal1, gai1, gai2, gai3 | Q2F | 6, 7, 5, 4 | in scope | goal 1, galactic rules tier 1, AI metrics tier 2 |
| map2, map3 | MAP | 3, 2 | in scope | goal 1 |
| ui2 | STORY | 2 | in scope | goal 1 (story campaigns); t12 / t13 need Gabriel |
| ui1, ui3, ui4, ui5, vis1, vfx2 | UIART | 7, 6, 1, 1, 1, 4 | in scope | goal 1 (feel, tier 1); ui4 / ui5 / vis1 wait only on Gabriel's playtests |
| aud1, aud3, aud5, aud6, aud7 | AUDIO | 1, 8, 8, 8, 1 | in scope | goal 1 (sound timing is tier-1 feel); aud7 t01 is S1 |
| q4 | Q4 | 4 | in scope | goal 1, Milestone 1 parity; t06 is S1 (golden drift on dev) |
| qafull | QAFULL | 3 | in scope | goal 1. Its exit text ("S1 / S2 backlog entries, not new groups") and t05 ("file each defect as a new roadmap group") predate the 2026-11-15 rule that S1 / S2 are group tasks (open question 5) |
| mcx2, mcx3, mcx4, mcx8, mcx9, modc | MCX2-MCX6, MODC | 2, 1, 19, 5, 3, 3 | in scope | goal 2; DECISIONS mod rows 2026-11-13 to 11-19. mcx3 t14 asks to restate the exit for the AOW stack, which DECISIONS 2026-11-18 (b) already settled (open question 6) |
| sp3, sp4, sp6, sp7 | SIMP | 6, 3, 3, 3 | in scope | goal 3; DECISIONS "Parallel sim redesign: exception to the current focus" (2026-11-14) |
| opt3f | OPT | 3 (parked) | in scope, paused | goal 3; tier 3, parked under the current focus. Leave as is |
| fsr, xess | FSR, XESS | 1, 1 (gab) | in scope | goal 5 (upscaling); approved dependencies 2026-11-13. Only Gabriel's look is left |
| **bm4** | BMU | 5 | **unclear: ask Gabriel** | README side goal "Repeatable benchmark" is in scope, but the work is untiered and serves no tier-1/2 exit ("Current focus"); its work sits on a bundled branch. Recommend `park bm4` |
| **bo4** | OPT | 4 | **unclear: ask Gabriel** | DECISIONS "Build speed" (2026-11-13) is in scope, but it is untiered process work under the current focus. Recommend keeping it open if build time still hurts lanes, otherwise `park` |

**Backlog entries.** All 190 are in the project's scope. Proposed changes:

| Entry | Recommendation | Reason |
|---|---|---|
| b0091 | drop (in `goal1_scope.json`) | Fixed: the forced-include alias stops sccache replaying another worktree's include paths (DECISIONS "Build speed") |
| b0132 | drop (in batch) | Obsolete: `git ls-remote` shows no `claude/a2m-diag-*` branch on origin |
| b0410 | drop (in batch) | Duplicate of b0404: both say `build_asan.cmd` passes `-mllvm -asan-stack=0` to the linker (still true, `build_asan.cmd:36`) |
| b0315 | ask Gabriel | Splash_E3 music only plays on a splash screen, and the port has none. Localised splash screens are out of scope (DECISIONS "Out-of-scope tag-ledger rows"), but the splash screen itself is not ruled on |
| b0488 | re-triage (not in a batch) | Filed as tier-3 `perf`, but it is a correctness bug: profile paths outside the ANSI code page. It belongs in `render-ui` |
| b0257 | keep in backlog | MSAA alpha-to-coverage is graphics fidelity, which README defers until the core game and the mod platform are complete |
| b0225 | keep | One of the dialogs it names, IDD_AUTO_RESOLVE_DIALOG, is MP-only and was removed (DECISIONS "Multiplayer code removed"). Only the single-player ones remain to check |

## Goal 2: condense the categories (18 -> 14)

Rule used: merge only categories that share a flowchart box. A category's box is what `backlog add` and `backlog move --cat`
give an entry, so merging across boxes would file new entries into the wrong box. The surviving id is kept, so only 93
entries change `cat`, and no entry changes its `node`. Deleted categories leave no entry behind: lint treats an unknown `cat`
as an error, even on a closed entry.

| Old category | Box | Open | -> New category | New label |
|---|---|---|---|---|
| inbox | - | 5 (4 after goal 1) | inbox | Untriaged |
| parity-sim | MOVE | 10 | **parity-sim** | Movement and combat |
| parity-combat | MOVE | 14 | parity-sim | (merged; 36 entries re-filed) |
| parity-anim | ANIM | 3 | parity-anim | unchanged |
| parity-rules | ABIL | 15 | parity-rules | unchanged |
| parity-ai | Q2F | 18 | **parity-ai** | Galactic rules and AI |
| parity-campaign | Q2F | 13 | parity-ai | (merged; 41 re-filed) |
| parity-tactical-ai | AIVIEW | 8 | parity-tactical-ai | unchanged |
| parity-story | STORY | 0 | parity-story | unchanged (its own box) |
| lua-data | RE | 5 | lua-data | unchanged |
| render-ui | UIART | 52 | **render-ui** | Render and UI (audio removed from label and rule) |
| audio | AUDIO | 13 | audio | unchanged label; order 120 -> 55; **gets its own rule** |
| mod-system | MODC | 7 | mod-system | unchanged |
| perf | OPT | 11 | perf | unchanged |
| tools-harness | PROC | 12 | **tools-harness** | Tools, docs and workflow |
| docs-process | PROC | 3 | tools-harness | (merged; 3 re-filed) |
| re-research | CONFIRM | 1 | **re-research** | RE questions and Gabriel's runs |
| gabriel | CONFIRM | 0 | re-research | (merged; 13 re-filed; the `gab` rule now files into re-research) |

**Rules.** The rule list keeps its order. The merged categories' rules now point at the survivors. One fix: there was no
`audio` rule, and render-ui's regex caught `audio|\bsfx`, so every new audio finding was filed into render-ui (UIART). A new
`audio` rule (`\baudio\b|sfx|\bmusic\b|speech|\bvo\b|announcer|ambien|voice line`) sits just before the story and AI rules.

I simulated it over the 190 open entries:
- 11 of the 13 audio entries now route to `audio` (none did before).
- 5 non-audio entries would also route there: b0245 (credits roll music), b0439 (ShotEvent SFX), b0450, b0487 (ability
  toggle sounds) and b0490.

Rules only file new entries and `retag` touches only `inbox`, so no existing entry moves.

Batch: `goal2_categories.json` (1 `meta/backlog` update, 93 entry updates, 1 log line). Phase 2 also needs a one-line edit
in this repository: the category list in `docs/ROADMAP_PROTOCOL.md`, "Categories".

**Not merged, and why.**
- `parity-anim` into `parity-rules`: ANIM and ABIL are different boxes.
- `parity-story` into `parity-ai`: STORY is in Stage 4, Q2F in Stage 2.
- `lua-data`: its box RE is done. Pointing it at a live box would be a separate decision (open question 7).

## Goal 3: reorder to the development order

### 3a. Inside stages (safe: no renumbering), `goal3a_box_group_order.json`

| Stage | Before | After | Why |
|---|---|---|---|
| Campaigns and menus (st3) | STORY 1, UIART 2, AUDIO 3 | UIART 1, AUDIO 2, STORY 3 | Story end to end needs the UI and the audio first (ui2 is after p4ui) |
| Mod system (st9) | MCX1-MCX6 1-6, Q5 7, Q6 8, MODC 9 | Q5 1, Q6 2, MCX1-MCX6 3-8, MODC 9 | Q5 -> Q6 -> MCX was the order the work happened in; MODC's exit waits on mcx8 |
| AUDIO box | aud1 193, aud3 195, aud5 230, aud6 231, **aud7 231 (duplicate)** | aud7 192, aud1 193, aud3 195, aud5 230, aud6 231 | aud7 is the box's only S1 task; this also removes the duplicate order |
| AIVIEW box | mapx1 has no `order` | mapx1 219 (after aitac 218) | A group without `order` sorts unpredictably |
| Other stages | unchanged | unchanged | Their box order already follows the dependencies (st2 FOLLOW -> RE -> MOVE / ABIL / ANIM / AIVIEW -> Q2F -> MAP; st5 OPT -> SIMP; side5 BM1 -> BM4) |

### 3b. Stage order (renumbers stages; conflict 3), `goal3b_stage_order.json`

| Position | Before (page number) | After (page number) |
|---|---|---|
| 0 | st0 Foundations (Stage 0) | st0 Foundations (Stage 0) |
| 1 | st1 Fix what the playtest showed (1) | st1 (1) |
| 2 | st2 Finish the core systems (2) | st2 (2) |
| 3 | st4 Confirm the port's guesses (3) | st4 (3) |
| 4 | st3 Campaigns and menus (4) | st3 (4) |
| 5 | side5 Benchmark (Side) | **st6 Parity (Stage 5, was 6)** |
| 6 | st5 Performance (5) | **st7 Full QA pass (6, was 7)** |
| 7 | st6 Parity (6) | **st9 Mod system (7, was 9)** |
| 8 | st7 Full QA pass (7) | **st10 Unified mod system refactor (8, was 10)** |
| 9 | st8 Port features (8) | **st5 Performance (9, was 5)** |
| 10 | st9 Mod system (9) | side5 Benchmark (Side) |
| 11 | st10 Unified mod refactor (10) | **st8 Port features (10, was 8)** |
| 12 | st11 Code refactoring (11) | st11 Code refactoring (11) |

Reason: README lists goal 1 (play the real game, judged by Q4 and the QA pass) before goal 2 (mods) and goal 3 (use the
whole PC). Side goals and port features come last.

**Dependency check.** No `after` edge breaks: every open group's `after` names a finished group or a group in the same
stage. Three things change meaning:
- `q4` waits on `opt2` (done) in Performance, which would now come after Parity. Harmless while opt2 stays done.
- The box arrows `OPT -> Q4` and `SIMP -> Q4` ("feeds into", dashed) would point back to an earlier stage. In practice the
  SP groups' A/B checks use the Q4 harness, so the arrow is closer to Q4 -> SIMP. I left the arrows alone (open question 3).
- `batch` hands out groups in stage order, so the SP lanes, which you approved "to be worked now" (2026-11-14), would come
  after the mod groups. Reordering stages changes which groups `batch` offers first.

Flag, not changed: **`aud3` is after `aud1`**, and aud1's only open task is t09, Gabriel listening to a playtest. So aud3's
eight voice-line tasks wait on a playtest that they do not technically need. I recommend dropping `aud1` from aud3's
`after` (open question 4), but I did not put that in a batch.

### 3c. Upscaler boxes (optional), `goal3c_upscaler_boxes.json`

Boxes FSR and XESS sit in the side goal "Repeatable benchmark", but they are README goal 5 (upscaling), not benchmark work.
The batch moves them to st8 Port features (order 2, 3). This changes no stage number.

## Goal 4: backlog into groups (option A)

The open backlog was clustered by theme and box, at most 8 entries per group, each group with an exit test. The clustering
works for any of the options below. The full data (titles, boxes, exit tests, presets, entry lists) is in
`roadmap_audit_2026-11-19/goal4_clusters_proposal.json`.

| Proposed id | Box | Entries | Title |
|---|---|---|---|
| aud8 | AUDIO | 8 | Audio system rules: music modes, 3D mods, volume restore, inactive window |
| aud9 | AUDIO | 5 | Audio triggers still unwired: HUD shield / pop-cap VO, object loops, capture bonus, cancel lines |
| gai4 | Q2F | 8 | Underworld galactic AI timing leftovers (a2m) |
| gai5 | Q2F | 7 | Galactic and skirmish AI rule leftovers |
| gal2 | Q2F | 7 | Galactic rules leftovers: bounty, scoring events, contested space, hero neutralize, buy list |
| cmb3 | MOVE | 8 | Combat rule leftovers: fire trim, slot legs, shield regen, lobbing super weapon, gravity field |
| mov3 | MOVE | 8 | Movement rule leftovers and movement test gaps |
| tru3 | ABIL | 7 | Ability, shield and spawner rule leftovers |
| anm2 | ANIM | 5 | Deploy and hangar animation leftovers |
| tai1 | AIVIEW | 6 | Tactical AI tier-2 leftovers |
| map4 | MAP | 8 | Map placement and setup data leftovers |
| map5 | MAP | 5 | Map camera, fog and environment data |
| rad1 | UIART | 5 | Radar: icon scale, land passability, galactic radar, asteroid fields |
| ui6 | UIART | 8 | HUD stand-ins and missing panels from the ui1 review |
| ui7 | UIART | 8 | UI questions for the binary and retail shots |
| ui8 | UIART | 6 | Galactic map and HUD polish |
| ui9 | UIART | 7 | Test-build display defects (gchk) |
| vis2 | UIART | 8 | Render rule leftovers |
| lua1 | STORY | 4 | Lua API leftovers in story and AI scripts |
| tst2 | PROC | 8 | Test and harness gaps that serve tier-1 / tier-2 groups (reopens the done box PROC) |
| port1 | FLEETG | 7 | Port feature follow-ups: fleet cohesion, fleet groups phases 2-3, names |

**Stays in the backlog (47).**
- 41 tier-3 entries (current focus), including the 3 that goal 1 drops.
- b0383, b0385 and b0501: you decided on 2026-11-19 that they stay S3.
- b0009 and b0404: untiered tool items that serve no tier-1/2 exit.
- b0315: open question 1.

S1 / S2 entries: none in the backlog (lint agrees). Stage 6 / 9 / 10 follow-ups: every entry from a st6 / st9 / st10 group
is tier 3, which `keep_followups` sends to the backlog anyway, so none moves.

**Decision (Gabriel, 2026-11-19): option A.** The batch is `goal4_backlog_groups.json` (166 writes).
- 143 entries are set `promoted: <group>/tNN`.
- 21 groups are set with `title`, `tag`, `node`, `status: big`, `preset`, `mod`, `order` (after the last group of their
  box), `exit`, `why` and `eta`. The eta target is 2026-12-12 (~2 days), or 2026-12-19 (~3 days) for gai4, tai1, ui7,
  map4 and vis2.
- Box PROC is set back to `partly` because tst2 opens it again.
- 1 log line.

The tasks are built the way `backlog promote --new` builds them (`t` text with "(backlog bNNNN, found in X)", `bl`, `sub`,
`sev`, `tier`, `g`). `write` turns the new groups from `big` to `partly`, since every task is still open. They are not
"started" (nothing closed and no claim), so the `wip` count does not change. Dry run of all six batches in order on a
scratch copy: lint 0 errors and 19 warnings. The three no-tier warnings are gone because b0254 / b0260 / b0261 are now
port1 tasks. `group-status` reports 0 groups to change, and no group or box is left inconsistent with the done rules.
Open groups go from 44 to 65.

The options as they were proposed:
- **A, protocol as written (my recommendation).** No pool category. Promote the 143 tier-1/2 entries as 21 new groups in
  the boxes above (`backlog promote <ids> --new <id> --title ... --node ... --exit ...`). Each group gets `eta`, a preset
  and status `big`. The other 47 stay in the categories from goal 2.
- **B, A but parked.** As A, but every new group is `park`ed at once, so it does not flood "Most important" or `batch`.
  You unpark groups when you want them worked. This stays closest to "backlog entries become work only when Gabriel picks
  them".
- **C, literal pool.** Create category `backlog-pool` (pick its box), move all 190 entries into it (each entry's `node`
  becomes the pool's box), and record the clusters only in this report. This needs a protocol change, because the store
  has no field for clusters inside a category, and it undoes goal 2.

Option A or B would add 21 groups to the 44 open. Whichever you pick, tier-3 entries stay out unless you lift the current
focus.

## Batch files

Apply each with `python roadmap-store/roadmap_store.py write docs/roadmap_audit_2026-11-19/<file>` from the main checkout,
one goal per commit, then run `lint`.

| File | Writes | What |
|---|---|---|
| `goal1_scope.json` | 4 | drop b0091, b0132, b0410 (`s: dropped`, `why`, `closed`), 1 log line |
| `goal2_categories.json` | 95 | `meta/backlog` cats + rules, 93 entries re-filed, 1 log line |
| `goal3a_box_group_order.json` | 15 | box order in st3 / st9, mapx1 and aud7 order, 1 log line |
| `goal3b_stage_order.json` | 14 | stage order; **renumbers stages (conflict 3)** |
| `goal3c_upscaler_boxes.json` | 3 | FSR, XESS -> st8 (optional) |
| `goal4_backlog_groups.json` | 166 | option A: 143 entries promoted into 21 new groups, box PROC reopened, 1 log line |
| `goal4_clusters_proposal.json` | - | the cluster data `goal4_backlog_groups.json` was generated from |

The log documents use the ids `roadmap-audit-2026-11-19-g1` ... `-g4`. `goal4_backlog_groups.json` was generated against
the store as of this audit. If entries are promoted or dropped before it is applied, regenerate it.

## Open questions (only Gabriel can decide)

1. **b0315 Splash_E3 music:** should the port get a splash screen, or is this entry dropped as out of scope? If it is out
   of scope, it needs a DECISIONS row.
2. **Out-of-scope handling:** protocol rule 5 deletes out-of-scope tasks and groups; your constraint keeps them as `oos` /
   `dropped`. Which wins from now on? If yours, rule 5 needs a one-line change.
3. **Stage order (3b):** apply it, and update the ~85 "Stage N" references, or keep the stage numbers and apply 3a only? If
   applied, should the `OPT -> Q4` / `SIMP -> Q4` arrows be reversed or removed?
4. **aud3 `after: [aud1]`:** drop it, so the voice lines do not wait on your aud1 playtest?
5. **qafull:** reword t05 and the exit to the 2026-11-15 rule (defects become tasks of their groups, not new groups or
   backlog S1 / S2)?
6. **mcx3 t14:** close it as already settled by DECISIONS 2026-11-18 (b)?
7. **lua-data's box** is RE, which is done. Leave it, or point it at STORY or AIVIEW?
8. **bm4 / bo4:** park both, keep both, or park one?
9. ~~**Goal 4:** option A, B or C?~~ Settled: option A (Gabriel, 2026-11-19).
