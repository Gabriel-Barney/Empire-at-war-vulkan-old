# skai t15: the running-plan re-test 0x6c47c0, and how a land Destroy_Structure plan ends at a commit (RE, 2026-11-15) [H unless tagged]

Answers `SKAI_T15_RE_QUESTIONS.md` Q1-Q5. Read with REA (rea-agents on the FoC Steam x64 image, base 0x140000000) and the
decompiled reference; no ReVa. Addresses are RVAs. [H] = read in the binary or the shipped Lua; [M] = inferred.

Functions: 0x6c47c0 (PP re-validation), 0x6c45e0 (validity), 0x6c5de0 (contrast Setup A), 0x5f1d00 (tactical list),
0x6c4b20 (contrast commit), 0x6bf840 (one unit's subtraction), 0x72af80 (contrast Finalize), 0x72ac70 (satisfied test),
0x6c2280 (PP signal handler), 0x6c27d0 (unit leaves the PP), 0x4d5a70 (free-store service), 0x6908f0 (land free store
vf22, "give this unit to this build task"), 0x647df0 / 0x64aec0 / 0x648980 (AIBuildTask), 0x6c8970 (Resolve_Category),
0x722a30 (maintenance), 0x614880 (budget release), 0x693750 (goal state 3), 0x691840 (goal destructor), 0x5f6a50 (plan
end), 0x5fefd0 (Release_Forces), 0x6006b0 (plan event post), 0x2488e0 (script reset), 0x5f8310 (plan service).

**Trace check.** The run folder `20261114-210208-skirmish-vanilla-lua` is not in any worktree on this machine any more
(`foc-underworld-mdu-destination-6eb8bc` is gone; the main checkout's `q2_traces/out` has no 2026-11-14 run). Q3 therefore
rests on the binary and on the numbers quoted in the questions file.

## Summary

- The hypothesis is **refuted**. In tactical modes, `Produce_Force` taking the units off the free store does not change what
  the re-test counts. The PP keeps its selection, and the re-test subtracts the same units it subtracted at accept (§1.3,
  §3.1).
- With unchanged inputs, the re-test repeats the accept-time Finalize, and that Finalize passed. It fails at a later commit
  only when an input changed (§3.2):
  - the perceived enemy power at or around the target;
  - the plan's recent-failure count at that target;
  - the health of a selected unit;
  - a selected unit was released singly.

  A dead selected unit does **not** lower the coverage.
- A `PEXIT` with `tf=?` and MainForce count 0 is what **any** plan end from outside the script leaves (§4):
  - the goal system drops the goal;
  - or `Purge_Goals` ends the plan.

  Either way, `Release_Forces(1.0)` raises `No_Units_Remaining` synchronously, and `Default_No_Units_Remaining` calls
  `ScriptExit` outside every thread.
- The record alone cannot tell which drop rule fired. Resolve_Category's displacement of an incumbent fits the stochastic
  survival (27 s, 83 s) as well as a failed re-test does (§3.4). §3.5 lists the retail hooks that would settle it.

## 1. Q1: 0x6c47c0 for a running tactical plan

### 1.1 Steps in order [H]
1. If the validity byte PP +0x4c is already 0, return false. The byte is **sticky**: nothing in the re-test sets it back to 1.
2. PP +0x100 clear (not yet reserved): the accept-style re-solve, the same body as 0x6c4a50. This path is not used for a running plan.
3. PP +0x100 set:
   1. +0x4c = 0x6c45e0. That validity check fails when any of these holds:
      - a reinforcement-pool slot of the PP (+0x120, index ≥ 0) is live but not owned by this PP (free store vf+0xe8 / vf+0xf8);
      - a build pad of +0x150 fails 0x4d6440;
      - a PP +0x108 unit that is still on the free store has a free-store entry +0x30 that is neither 0 nor this PP ("stolen").
   2. If PlanDef +0xaa (`IgnoreTarget`) is set, or PP +0x4d (`Test_Target_Contrast`) is 0, return that validity.
   3. Rebuild the contrast globals, as Setup A 0x6c5de0 does at a solve (step for step the same code):
      - no goal target: both lists empty, T = 0;
      - otherwise T = 1 − (f·PFCA + `MinContrastScale`)/(f·PFCA + `MaxContrastScale`). Here f = 0x588980(the mode's learning system, PlanDef, target) is read **now**, not copied from the accept.
      - Tactical (PP +0x198 ≠ 0): the escort flag 0x140b3a525 is set when any TF definition of the plan (PlanDef +0x28 list) has +0x4a (`EscortForce`). The main list is 0x5f1d00(…, flag 0) into 0x140b3a600. The escort list is 0x5f1d00(…, flag 1) into 0x140b3a630, built only when the escort flag is set. Lists, rows and scaling: AI_TACTICAL §7.7.1.
   4. Commit 0x6c4b20(PP) (§1.3).
   5. The contrast marshaller's Finalize, through the singleton 0x140b3ae98, vtable +0x30 = 0x72af80 (§1.6).
   6. Return +0x4c.

### 1.2 Inputs that are not rebuilt [H]
The re-test does **not** re-solve. Selection, teams, per-selection contrast entry and layer flags all come from the PP's last
solve, which for a running plan is the accept re-solve 0x6c4a50:
- selection types PP +0x58;
- per-selection contrast entry +0x70;
- team records +0xe8, each with a member-index list;
- unit pointers +0x108;
- extras +0x88 / +0xa0;
- layer flags PP +0x1a0 (space) / +0x1a1 (ground), set by Setup C 0x72aad0 → 0x72b240 for the main and the escort list.

Nothing on the re-test path writes the layer flags.

### 1.3 What the commit 0x6c4b20 subtracts [H]
1. Remaining lists: 0x140b3a618 and 0x140b3a648 start as copies of the target lists.
2. Walk every team record of PP +0xe8, member by member. Each selection index is used once (a bitset).
3. The list for a member:
   - the escort list when an escort list exists and either the member's TF definition is an `EscortForce` or the main contrast is already met;
   - otherwise the main list.

   "Main met" (0x140b3a528) is re-tested with 0x72ac70 after each subtraction, for the layers flagged in +0x1a0 / +0x1a1. So MainForce units fill the main list first, then the escort list.
4. Selections that are in no team go to the main list; then the PP +0x88 extras, with no unit.
5. Store target − remaining in PP +0x1b0 / +0x1c8 and the lists themselves in +0x1e0 / +0x1f8.

One subtraction (0x6bf840(type, entry, list, …, unit)):
- **Type.** PP +0x58[i]; when PP +0x108[i] is a live object, that object's own type (0x396380: its team or company type, else its own).
- **P.** The type's `AI_Combat_Power` (0x370e60: type +0xdf0; for some variant types it is scaled by a fill fraction [M]).
  - Multiplied by **health** 0x396df0 **only when the unit pointer is non-null**: current hull over max for a single object; the team aggregate 0x405230 for a team [M: what the aggregate sums].
  - Multiplied by a per-category factor (the largest table value over the type's category bits, plus 1) when that factor is non-zero. The table is filled from the selected units' type list +0x1a98 (modifier objects, vf+0x38 / +0x40 / +0x48) [M: their XML source; none expected for the DS units].
- **Where it goes.** Contrast entry < 1: P comes off the untyped total row of the same layer. Otherwise w·P comes off the typed row (w = 0x5f1f90 weight; × terrain factor 0x5f22c0 when PP +0x19c ≠ −1), and P also comes off that layer's untyped total.

**Which units count**, by what happened to them since the accept:

| Unit state | PP effect | Counted in the re-test |
|---|---|---|
| Taken into the task force by `Produce_Force` | none (§3.1) | yes, × its current health |
| Dead, or changed owner | 0x6c2280 (PP signal handler, signals 0x10 / 0x1a): pointer +0x108[i] set to null, the selection entry stays | **yes, at full type power** (no health factor) |
| Released singly: `Release_Unit` (`Default_Unit_Damaged` heal / kite, `Try_Garrison`, `Release_To_Hide`), `Release_Forces` | TF vf+0x188 → 0x5ff610 → 0x6c27d0: the selection entry is **erased** from +0x58 / +0x70 / +0xb8 / +0x108 / +0x120 / +0x168 / +0x180 and the team lists | no |
| Joined by a land reinforcement drop | 0x6bf3d0: fills a null +0x108 slot of the same type, else appends | the filled slot yes; an appended unit has no +0x58 entry [M: not counted] |

### 1.4 Tolerance [H]
The tolerance T has the same formula as at accept. f is re-read at the re-test (§1.1 step 3.3), so it is the same T unless
the recent-failure count of this plan at this target changed. A plan end (0x5f6a50 → 0x58a720) records an outcome for its
goal.

### 1.5 Typed rows of a land plan [H, shipped Lua]
`DestroyStructure.lua` defines no `EnemyContrastTypes`, so the rows are the PGAICommands `Set_Contrast_Values` defaults:
- space rows: Fighter, Bomber, Transport, Corvette, Frigate, Capital;
- land rows: Infantry, Vehicle, Air, Structure, LandHero. The Structure row weighs AntiStructure 4 and Infantry / Vehicle / Air / Structure 1.

In a land list every row has layer byte 1, so only PP +0x1a1 matters.

### 1.6 The exact condition that sets +0x4c = 0 (0x72af80, instruction-checked) [H]
Nothing happens for an `IgnoreTarget` plan or an empty main remaining list. Otherwise each failing check clears +0x4c:
1. **Main untyped total.** The layer flag of entry 0 is set (+0x1a1 for a ground entry), its target > 0, and remaining /
   target > T.
2. **Short main list.** If the main list has fewer than 2 entries, stop here.
3. **Main entry 1.** Only when its layer differs from entry 0's (galactic only): the same test.
4. **Escort list**, when it is not empty:
   - the same test on its entry 0;
   - if it has fewer than 2 entries, **stop here**;
   - otherwise its entry 1, as in step 3.
5. **Satisfied tests.** AND +0x4c with 0x72ac70(main, ground) when +0x1a1 is set, and with 0x72ac70(main, space) when
   +0x1a0 is set. When the escort flag is set, the same two on the escort list.

0x72ac70(target, remaining, T, layer) is satisfied when Σ positive remaining over typed rows (key ≠ 0) of that layer,
divided by the sum of their targets, is ≤ T. A layer with no positive typed target is satisfied. So with the layer flags
clear (no candidate ever had a positive weight there), the re-test cannot fail on contrast.

## 2. Q2: where the re-test runs and what a failure does

### 2.1 Callers [H]
The re-test runs at every Commit, for every active goal that has a PotentialPlan (goal +0x68), on one of three paths:

| Path | When | MagicPlan exception |
|---|---|---|
| 0x6c8970 kept path | the category has filtered proposals, and the goal's effective desire ≥ best proposal or it is not replaceable | **skipped** when the plan's PlanDef +0xa8 is set |
| 0x6c8970 old-candidate path | effective desire < best and replaceable | **none**: the re-test always runs |
| maintenance 0x722a30 | the category has no proposals | skipped when +0xa8 is set |

Maintenance order:
1. not fundable (0x6129c0);
2. desire ≤ 0 and replaceable;
3. plan with PP and (not MagicPlan) and re-test false;
4. galactic only: AllowedTime < ETA.

### 2.2 What a failure does [H unless tagged]

**Resolve_Category, kept or old-candidate path** (0x6c8970):
1. 0x614880(goal system +0x190, goal). Its body is galactic-only (mode +0x34 == 0); in tactical it does nothing.
2. The goal is not put back: the category's active vector was already emptied, and the goal joins neither the kept set
   nor the pool.
3. Its last reference goes when the local vector is freed, and the goal destructor 0x691840 runs the plan end 0x5f6a50
   [M: reference count reaching 0 there; nothing else ends the plan].

**Maintenance:** 0x7225d0 (remove from the active vector), then 0x614880, then the same destructor path.

**Old candidate not picked again** by the greedy or random phase: at the end of Resolve_Category, 0x614880, then 0x693750
(state 3, Released), then the destructor.

**Plan end 0x5f6a50, in order:**
1. finish the plan's build tasks (0x64ad00), and prune their free-store claims (0x4d5150);
2. **for each task force, vf+0xb0(1.0) = `Release_Forces(1.0)`** (§4);
3. plan +0x80 = 1 (ended);
4. learning outcome 0x58a720(goal, plan +0xa8);
5. 0x693750(goal), then 0x693970(goal, 0);
6. drop the goal's PP (goal +0x68 = 0: its destructor unreserves);
7. plan +0x88 = 0;
8. if the script is live, reset it with 0x2488e0;
9. release the script.

### 2.3 The "keep despite invalid" flag [H]
- The flag is **PlanDef +0xa8 = `MagicPlan`**, reached as plan +0x60 → +0xa8; plan +0x60 is the PlanDefinition, written
  by the plan start 0x5f7090. BUILD_TASK_LIST 0x648980 reads the same byte to send a build task to HIJACK_UNIT.
- It is the Lua global `MagicPlan`: PGAICommands `Base_Definitions` sets it false, and only the galactic
  `Generate_Magic_*` / `GenerateMagicCashDrop` plans set it true.
- No LandMode plan sets it, `DestroyStructure.lua` included.
- No LandMode plan calls `Test_Target_Contrast` (only galactic plans: ConquerOpponent, Crush, DeathStarUse, Raid), and
  the `Produce_Force()` clear of +0x4d is galactic-only (BKG1_RE_ANSWERS §4.2).
- So a land `Destroy_Structure` plan runs the full contrast re-test at every commit.

## 3. Q3: does it explain the trace?

### 3.1 Produce_Force does not change what the re-test counts [H]
1. Tactical `Produce_Force` makes one build task per wanted entry. BUILD_TASK_LIST 0x648980, for an `AllowFreeStoreUnits`
   plan, attaches the task to every free-store entry of its type (0x4d2bd0).
2. The free-store service 0x4d5a70 hands an entry's unit to the first claiming task for which the land free store's vf22
   0x6908f0 answers "give". That answer needs the entry's reservation (+0x30) to equal the task's PP. So a task receives
   exactly the units its own PP selected.
3. The hand-over itself, in order:
   - 0x647df0 (task unit = that unit);
   - the store node is erased;
   - next state FREESTORE_ALLOC (3) in tactical, WAIT_FOR_FREESTORE_ALLOC (0x16) only in galactic.
4. 0x6c27d0 (erase the selection entry) is called only from TF vf49 0x5ff610 (a unit leaves the task force) and from the
   galactic state 0x16. So after `Produce_Force` the PP still lists every selected unit, with its live pointer.

### 3.2 When the re-test can fail at the first commit [H rules, M application]
The commit and Finalize are recomputed from scratch with the same code as at accept. With the same inputs the result is
the accept result, which passed. So the first re-test fails only when one of these changed:
- **The lists.** The target object's own power, or the enemy force around the target (the escort list,
  `Target.Location.EnemyForceUnnormalized`). For example:
  - defenders moved in;
  - the threat grid re-projected;
  - the fog-memory double count of AI_TACTICAL_PERCEPTION open item 3 started counting an enemy twice.
- **f.** Another failure recorded for this plan at this target raises the typed rows (× (Max + f·PFCA)) and lowers T.
- **Health.** A selected live unit lost hull.
- **A unit released singly** before the commit (§1.3 table).
- **The modifier factor** of §1.3 changed [M].

A selected unit that **died** keeps covering at full type power, so losses on the way do not make the re-test fail; they
can even raise the coverage, because a damaged unit that dies counts more.

### 3.3 Command centre against vehicle factory [H]
- **Zero-power command centre, no defenders.** Every main-list row is 0. The entry-0 check needs a target > 0, and the
  satisfied test of an all-zero layer passes. The escort list is 0 too. So the re-test cannot fail, and AI_TACTICAL §7.7.6
  stands: that plan holds until the block ends or the goal is displaced.
- **With defenders** around the command centre, the escort list is positive. The plan can then fail when their force grows
  past what the escort and spill-over MainForce units cover.
- **Vehicle factory** (`AI_Combat_Power` > 0). The main list entry 0 and the Structure row are positive. 4-10 units were
  enough at accept, and stay enough while the inputs of §3.2 hold. Nothing in the binary makes such a plan fail
  systematically at the next commit.

### 3.4 The other rule that fits the trace [H rules, M attribution]
Resolve_Category (AI_GALACTIC §1.6) runs at the same instant and drops running goals with the same record signature:
- An incumbent whose effective desire (hysteresis bonus included) is below the best filtered proposal of
  `Tactical_Targeted` becomes an old candidate.
- It keeps its slot only if the greedy phase reaches it, or the **weighted random phase** draws it, before
  `GoalSetExtensionSize` (2) + |active| slots are filled. Otherwise it is released at the end of Resolve_Category.
- A Destroy_Structure goal on a low-desire structure has to win again at every commit after the one draw it won. The
  vehicle factory is one: it gets only 0.001 + 15 × (1 − `Target.Location.EnemyForce`) of `Should_Destroy_Structure`,
  against +5 for turrets and repair or bacta structures, +10 for shields and power, and +20 for mineral processors
  (`BasicLandEquations.xml`).
- That gives "most plans end at the first commit, some survive one or five more" (the 27 s and 83 s cases) without any
  contrast failure.
- The port runs this phase too. But its old-candidate path skips the re-test (`ai_goals.cpp` classify step: only kept goals
  call `plan_invalid`), and its `plan_invalid` has no contrast part.

### 3.5 How to tell them apart on retail [M: proposed, not run]
A Frida run of the same kit (Empire AI, Utapau, past 19000 frames), hooking:
- 0x6c47c0: the PP argument and the return value;
- the four `+0x4c = 0` stores in 0x72af80 (0x72b01d, 0x72b090, 0x72b10a, 0x72b17d) and the four ANDs after its 0x72ac70
  calls (0x72b1a8, 0x72b1d2, 0x72b205, 0x72b22f);
- 0x6c8970's release calls 0x614880 and 0x693750 (by return address);
- 0x691840 (goal destructor);
- 0x69c750 (`Purge_Goals`).

Each Destroy_Structure PEXIT then shows which path ended it. Kit (2026-11-16): `tools/probe_mod/q2_traces/run_skai_t15_empire_ai.cmd` / `run_skai_t15_rebel_ai.cmd` with `frida/skai_t15_hooks.js` (hooks 0x6c47c0 return, 0x5f6a50, 0x693750, 0x614880, 0x691840, 0x69c750, 0x6c8970 / 0x722a30 brackets; it logs the re-test's return value instead of the eight stores), read by `analysis/plan_ends.py`. Also check the run for a `BURNUNITSLAND` PSTART next to the
PEXITs (§5).

## 4. Q4: how the drop produces ScriptExit with count 0 outside the MainForce thread [H unless tagged]
1. Plan end 0x5f6a50 step 2 calls TF vf+0xb0(1.0) → `Release_Forces` 0x5fefd0(1.0).
2. Every member leaves through vf+0x188 → 0x5ff610:
   - 0x6c27d0 re-admits it to the free store with +0x30 = 0;
   - its type's default abilities are re-applied unless its movement is locked.
3. Because f == 1.0 (or the entry list is empty), 0x5fefd0 then posts event 7 `No_Units_Remaining` through 0x6006b0(TF, 7,
   no params, **0**).
4. 0x6006b0 looks up the handler (0x6887c0).
   - It does nothing when there is no handler, or when the script's exit flag (+0x121) is already set.
   - With the last argument 0, it **calls the handler at once** (0x243640). The queued form (0x25ab50, dispatched at
     `PumpEvents`) is the non-zero case.
5. `DestroyStructure.lua` defines no `MainForce_No_Units_Remaining`, so PGEvents' `Default_No_Units_Remaining` runs and
   calls `ScriptExit()`.
6. That call runs on the plan's main Lua state, not inside the MainForce or GuardForce coroutine, so `GetThreadID` maps to no
   task force (`tf=?`), and MainForce is already empty (count 0). PGBase's `ScriptExit` only sets the C exit flag there; it
   does not yield.
7. 0x5f6a50 then finishes (§2.2). The script reset 0x2488e0:
   - sets +0x121;
   - clears the `LuaThreadTable`, so the MainForce thread (still in its Attack_Move `BlockOnCommand`) and the GuardForce
     `Escort` loop are dropped without running again;
   - for a pooled script (`ScriptPoolCount 16`), runs `Flush_G` and `Base_Definitions`.

   GuardForce's `Escort` loop plays no part in the exit.

**Which plans leave this record.** A plan ended from outside leaves a `PEXIT` only when its script's handler for event 7
calls `ScriptExit` and the script has not exited already. The q2 kit's note ("a goal-system release leaves no end record")
fits plans whose override does not exit: `BurnUnitsLand.lua`'s `MainForce_No_Units_Remaining() end` is empty, which is why
its run 1 in SKAI_T06_LAND_RUNS ended with `prev=none`.

A plan ended by its own `ScriptExit` goes through the plan service 0x5f8310: the exit flag is already set, so no second
record [M: the exact cases the note was written from].

## 5. Q5: other ways units leave a running plan at a commit [H unless tagged]

| Candidate | Verdict |
|---|---|
| Another PP's solve taking task-force units | **Out.** Free-store candidates come from the free-store map (FreeStore Setup A 0x7287a0), and task-force members are not on it after the hand-over (§3.1). Only units still on the store can be stolen (0x6c45e0). |
| `Collect_All_Free_Units` (BurnUnitsLand, 0x6bfb40) | **Out** for members: it takes only store entries with an object and +0x30 == 0. |
| MagicPlan / `MagicPlanStealing` | **Out.** HIJACK_UNIT (0x648370) runs only for MagicPlan build tasks, and only galactic magic plans set it (§2.3). |
| Free-store re-admission | **A consequence, not a cause.** It happens only when a unit leaves a task force (0x5ff610 → 0x6c27d0). |
| `Purge_Goals` 0x69c750 | **Ends whole plans**, with the same `tf=?` count-0 PEXIT (§4), for every other running plan that passes 0x5f7650 (goal-system removable, the test `Replaceable` 0x692740 also uses) [M: player scoping, AI_GALACTIC §1.5]. It runs from a BurnUnitsLand thread at its start, after the commit's Planning, so its PEXITs follow a `BURNUNITSLAND` PSTART [M]. |
| Single-unit handlers (`Default_Unit_Damaged`, `Try_Garrison`, `Release_To_Hide`) | **Remove one unit at a time.** They erase its selection entry (§1.3), and an emptied task force raises event 7 from `Release_Unit` 0x5ff2d0. They do not empty the task force at a commit instant. |

## Port consequence

For `engine/ai/tactical/src/tactical_ai.cpp` `plan_invalid` (~l.1257) and the contrast code in `tactical_pp.cpp`:

1. **Port the running-plan re-test.** Keep the result sticky: once invalid, always invalid.

   `plan_invalid(g)` stays "ended or stolen" (0x6c45e0, today's `pp_stolen`), and then, for a running PP that is not
   `IgnoreTarget` and whose `test_contrast` is set, adds the contrast part:
   1. rebuild the main list (and the escort list when the plan has an `EscortForce` TF), with the same builder the PP
      Setup A uses: current perception, current f;
   2. run the commit over the PP's **stored** selection;
   3. run the Finalize checks of §1.6, under the PP's stored layer flags.
2. **Keep, per PP, what the commit reads:**
   - each selection's type, contrast entry, team and nullable unit handle;
   - the escort-TF flag per team;
   - the ground / space layer flags from the last solve.

   Factor the subtraction (type power × health when the unit handle is live, weight and terrain on the typed row, P off
   the layer total) out of the solver's commit, so the solve and the re-test share it.
3. **Selection edits must match retail:**

   | Event | Selection edit |
   |---|---|
   | `Produce_Force` hand-over | none |
   | Death or owner change | null the unit handle, keep the entry: it then counts at full type power |
   | `Release_Unit` / `Release_Forces` / the task-force leave path | erase the entry |
   | Reinforcement join | fill a null slot of the same type |
4. **Goal system** (`engine/ai/src/ai_goals.cpp`, Resolve_Category classify):
   - old candidates with a PP must also be re-tested, and released when invalid, without the MagicPlan exception;
   - the kept path and maintenance skip the re-test for MagicPlan plans (today "not modelled", l.437).
5. **Plan end from a drop:**
   - `Release_Forces(1.0)` must raise `No_Units_Remaining` **synchronously**, before the script is reset, so that
     `Default_No_Units_Remaining` → `ScriptExit` runs;
   - this happens only when the script has a handler and has not exited (empty overrides such as BurnUnitsLand's do
     nothing);
   - then the plan's threads are dropped without running again.
6. **Before changing goldens, find which rule ends retail's plans** (§3.5). If the hooks show 0x6c47c0 returning false,
   items 1-3 close the gap. If they show the old-candidate release or `Purge_Goals`, the port's difference is in
   Resolve_Category or Burn_Units_Land timing, and items 1-3 are still required for parity but will not by themselves
   shorten plan 14.

## Port check (q4d-skai, 2026-11-16, not yet against the hook run)

- Ported the re-test as the "Port consequence" items 1-4 say: `retest_tactical_pp` (`engine/ai/tactical/src/tactical_pp.cpp`)
  rebuilds the lists from the current perception and recent failures. It commits the stored selection: a live unit at its
  health; a dead, captured or not yet arrived one at full power; an appended reinforcement is skipped. The finalize then runs
  under the solve's layer flags. The result is sticky (`TacPpRec::contrast_failed`). `plan_invalid` calls it, and so does the
  Resolve_Category old-candidate path. That path sits behind `GoalHooks::retest_old_candidates()`, which is tactical only:
  galactic keeps the earlier rule.
- `--skirmish-ai foc-land --human Underworld --ai Empire --seed 1 --ticks 36000`: **no re-test failed**. The run is unchanged:
  the Empire wins at 18429, and plan 14 (4847, Destroy_Structure on object 792) still holds. This matches §3.2-3.3: the re-test
  does not make the plan end.
- The port's difference is upstream. Over that run, 162 Destroy_Structure proposals reached slot 10:
  - 9 were accepted;
  - 61 failed at the first solve on Contrast, and 92 on Plan (no free unit fills the team minimum);
  - 6 more failed the accept re-solve.

  Retail starts a new Destroy_Structure at almost every commit after about 19000 frames. So if the hook run shows Resolve_Category
  displacement (§3.4), the work is the port's lack of free units at proposal time (Land_Scout_Area accepted 299 times,
  Secure_Contestable 261), not the plan end.

## Retail hook runs (2026-11-16) [H: measured]

Kit of §3.5, FoC 1.121 Steam, Utapau Heights, Advanced Default, Medium AI, hands off, driven by the coordinator session.
Read with `analysis/plan_ends.py <run>`: it finds the Destroy_Structure plan definition from the `PEXIT tf=?` records and
classifies **every** plan end of it by where the end came from (`mf` = mode frame = battle time × 30, offset 0 here).

| | Run 1: Empire AI, human Underworld idle | Run 2: Rebel AI, human Empire idle |
|---|---|---|
| Folder (this worktree, `tools\probe_mod\q2_traces\out\`) | `20261116-005957-skirmish-vanilla-lua` | `20261116-012157-skirmish-vanilla-lua` |
| Battle end | **ended** at about 25707 (defeat screen; kit exit `mode_changed`) | none within 36000 (`ticks_reached`) |
| Base kills (`land_attack.py`) | vehicle factory 14368, droid works 17366, palace 24378, barracks 25499 | towers 11318 / 17212, shield 21663, light factory 22069, research 24793, comm array 25479, barracks 29004, power 29665; the CC survives |
| Refinery / tech 2 / tech 3 built | 2569 / 7678 / 19390 (as on 2026-11-14) | 2659 / 9781 / 16987 |
| Destroy_Structure plan ends | 19 | 54 |
| Purge_Goals (Burn_Units_Land, plan end from 0x69c8cc) | 6 | 26 |
| The plan's own exit (0x725bf0) | 5 | 10 |
| Re-test 0x6c47c0 false, then released (Resolve_Category 0x6c8c9a or maintenance 0x722c2d) | 4 (1 maintenance) | 7 |
| Old candidate not picked again (§3.4, release 0x6c95b0) | 4 | 7 |
| Random-phase re-check failure (release 0x6c9996, AI_GALACTIC §1.6 step 6) [M: site] | 0 | 4 |
| Re-test false results, all plan types | 31 (27 PPs) | 19 (18 PPs) |

Run 1 Destroy_Structure lives: 13.7 s (one commit interval, 414 frames) in 11 of 18, 26 s in 4, longer in 3.

**Answer to t15.** Retail's land Destroy_Structure plans end at a commit by every goal-system rule, not by one: the contrast
re-test does fail (11 of 73 ends over both runs), the old-candidate displacement of §3.4 is as frequent (11), and once
Burn_Units_Land runs (from about 516 s / 600 s) its `Purge_Goals` ends the most (32). The re-test alone does not make the port's
plans short; the port needs all three (it has displacement and Purge_Goals already).

**Retail spread.** The Empire AI destroyed the idle Underworld base this time (about 25707); on 2026-11-14 it had not ended
by 36000. So "retail's Empire AI does not finish an idle base within 36000" (B0418 §5, skai's exit test) holds for one of
two runs. The Rebel AI has not ended an idle Empire base in any of three runs (SKAI_T06 §1, §6, run 2 here).

### Port status (q4d-skai, 2026-11-16)

The re-test of "Port consequence" items 1-4 is in the port behind `TacticalAiConfig::plan_retest` (headless
`--plan-retest`), **off by default**. With it off, every golden and outcome test is unchanged (the re-test is not called, and
the CRC adds the sticky failure byte only when a re-test failed).

With it on, the re-test fails in the port the way retail's does (§3.2, the failure line names the list, layer and coverage):
- headless synthetic skirmish, frame 302: a SkirmishSweep area gained enemies (main space total 600 of 2000 left, tolerance
  0.267);
- foc-land-campaign seed 1, frame 961: a ClearInfantry zone now held enemy types the selection does not cover (untyped total
  met, typed rows 2520 of 2520 left, tolerance 0.933); frame 1303: a DestroyLandUnit escort list fully uncovered.

Consequences of switching it on (why it is not on yet):
- goldens move: the synthetic, foc and foc-land-campaign skirmishes, the game smoke recordings;
- foc-land-campaign seeds 1-12 (20000 ticks): 4-8 re-test failures per seed; 10 of 12 still end with the Rebels retreating
  (2755-11566, was 2239-3550 for all 12), seeds 1 and 3 run to the 14400-frame cap. `alamo_headless_skirmish_ai_foc_land_campaign_outcome_seed1`
  keeps `--expect-battle-end` (DECISIONS.md "AI-vs-AI land skirmish tests do not need a battle end": the campaign-rules land
  invasion keeps it), so that test fails with the switch on. Turning it on is Gabriel's decision.

**On by default since skai t18 (2026-11-16, Gabriel option (c); DECISIONS.md "Running-plan re-test on by default").**
`--no-plan-retest` turns it off. Seeds 1 and 3 stall in a rule-following standoff (no 2:1 retreat for the Rebels, the Empire's
last AT-AT on a cross-map Clear_Infantry walk), so outcome seed 1 was replaced by seed 2. Idle-human Utapau with the re-test
on (dev + skai-lane2, MSVC, 1 worker, 36000 ticks, seeds 1-10): Rebel AI ends 1 of 10 (33956), Empire AI 10 of 10
(17525-19883), Underworld AI 5 of 10 (23561-35241). Retail so far: Rebel 0 of 3, Empire 1 of 2 (about 25707), Underworld 0 of 1.
