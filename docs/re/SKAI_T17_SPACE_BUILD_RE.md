# skai t17: the empty EscortForce of Secure_Build_Pad_Space, and the order of the first space build queue (RE, 2026-11-16) [H unless tagged]

Answers `SKAI_T17_RE_QUESTIONS.md` Q1 and Q2. Read with ReVa (Ghidra, `/StarWarsG.exe`, FoC Steam x64, image base
0x140000000) and REA, plus the shipped Lua and XML (`extracted/foc_data_text.zip`). Addresses are RVAs. [H] = read in the
binary, the shipped data or the retail traces; [M] = one inferred link; [L] = not checked. No decompiled code is reproduced.

Functions: 0x6bad60 (goal → plan), 0x5f7090 (plan instantiation), 0x66fb30 (create one task force), 0x66fcc0 (TaskForce
definition parser), 0x6c0100 / 0x6c0750 (a TF definition's selections / reinforcement-pool entries), 0x244740 (start a Lua
thread by function name), 0x5fd970 (`Get_Force_Count`), 0x5fe240 (`Produce_Force`, shared by every TaskForce class),
0x697a20 (`FindTarget`), 0x255610 (`_ScriptExit`), solver marshallers 0x7276a0 (Reinforcement Score), 0x72b940 (Contrast
Score), 0x727d30 / 0x728000 (TacticalBuild Score / Commit), 0x288b80 and 0x497140 (can-build tests). The `0x6cfbe0` named
in the questions is `Refit_To_Definition`, not plan start.

## Summary

- **Q1.** In retail the EscortForce of a `Secure_Build_Pad_Space` plan whose solve gave it no unit **does not exist**. Plan
  instantiation creates a task force only for a TaskForce definition that received at least one selection (or reinforcement
  pool entry), or that is marked `TaskForceRequired`. With no task force there is no Lua global `EscortForce` (it stays nil)
  and no `EscortForce_Thread` is started. So the escort thread stops at no line: it never runs. The plan's only thread is
  `MainForce_Thread`. Its `QuickReinforce(PlayerObject, AITarget, MainForce, EscortForce)` gets `second_try_tf = nil` and
  ends without `ScriptExit` because MainForce holds its unit. The plan lives on and guards its pad (§1.4).
- If a task force with no members did exist (the port), the escort thread would reach `PGTaskForce.lua:350` `ScriptExit()`.
  `_ScriptExit` sets the script's exit flag, which ends the **whole plan**, MainForce thread included. `FindTarget(...,
  "Is_Friendly_Start", ...)` raises no error in space: equation names resolve through the one global equation manager that
  holds every `PerceptualEquations` file (§1.3).
- **Q2.** `Produce_Force` creates its build tasks in task-force wanted-list order (TF +0x60). The solve fills that list
  **team by team, in the order the teams are written in the TaskForce definition**, not in pick order. In
  `TacticalMultiplayerBuildSpaceUnitsGeneric.lua` the RS upgrade line is the first team and the Rebel unit line the fifth, so
  the upgrade is always queued first. The first solve with 6000 credits at station level 1 picks the RS level-two upgrade plus
  **three** units of the `= 0,3` line. The team maximum limits it, not the credits (at most 3600 of 6000). Each of
  upgrade / X-Wing / Y-Wing is picked once in the first three picks, and the fourth pick is an X-Wing or a Y-Wing (§2.2).
  This matches all seven retail runs that have build records (§2.3).

---

## Q1. A 0-minimum task force that the solve gives no unit

### 1.1 Which task forces exist after plan start (0x5f7090 → 0x66fb30) [H]

- 0x6bad60 creates the plan and calls 0x5f7090(plan, PP). 0x5f7090 sets the `Target` and `AITarget` globals (and `Budget`
  outside tactical modes). Then it
  walks the plan definition's TaskForce definitions in order and calls 0x66fb30(tfdef, plan, PP) for each.
- 0x66fb30 returns **no task force** when all three of these hold:
  1. 0x6c0100(PP, tfdef) is empty: the PP has no selection placed in a team of this definition;
  2. 0x6c0750(PP, tfdef) is empty: no tactical reinforcement-pool selection (PP +0x120 ≠ −1) in such a team;
  3. the definition's byte +0x4b is 0. 0x66fcc0 sets +0x4b only for the flag string `"TaskForceRequired"`. The flag
     `"EscortForce"` sets +0x4a instead.
- Otherwise 0x66fb30 builds the mode's task force (galactic 0x6cd360, land 0x6eaa20, space 0x6e6640). It adds one wanted
  entry (TF +0x60, team ordinal at TF +0x90) per pair from 0x6c0100.
- For a task force it got, 0x5f7090 appends it to plan +0x68 and binds the Lua global named after the definition
  (`EscortForce`), then `PlayerObject`. Then it calls 0x244740(script, `"<name>_Thread"`, 0). That looks the function up
  in the script's globals and, if it is a function, starts it as a thread (TF +0xf8 = thread id). If no such function
  exists, the task force is taken off plan +0x68 again.
- For a null result nothing at all happens: no global, no thread, no list entry. The plan still starts (0x5f7090 succeeds when
  the definition has at least one TaskForce definition).
- MoveToLocationRush's EscortForce line is `{"EscortForce", "Fighter | Corvette | Frigate = 0,4", "EscortForce"}`. It has
  no `TaskForceRequired`, so with no selection it is **not created**.
- A pooled plan script that is reused is reset by 0x2488e0 (`Flush_G`, `Base_Definitions`; LUA_API "What an error does").
  So no `EscortForce` value from an earlier plan instance is left in it [M: the pool path is not traced here].

### 1.2 What the solve can give a 0-minimum task force in a non-build tactical plan [H unless tagged]

- **No "to be produced" entries.** In a tactical plan with `AllowFreeStoreUnits` (the default, PlanDef +0xd0) and not
  MagicPlan, the Reinforcement Score 0x7276a0 vetoes (1e18) a candidate that got no free-store unit from FreeStore Score
  (rec +4 = −1) and matches no reinforcement-pool entry. Structure types (behaviour 0x2a) are the exception.
  So such a plan selects only free-store units or pool entries. A to-be-produced selection (no unit, PP +0x120 = −1) exists
  only in build plans (`AllowFreeStoreUnits = false`, through TacticalBuild) and MagicPlans.
- So Q1b's case (to-be-produced entries, AIBuildTasks that wait or fail) **does not arise** for `Secure_Build_Pad_Space`.
  What such tasks would do in a non-build space plan was not read.
- **Why the escort gets no unit** [M]: Contrast Score 0x72b940 has an "everything met" gate for a candidate with no positive
  contrast value. It returns early (no veto) while a contrast list still has an untyped remainder, a required category is
  unmet, or one of the candidate's teams is below its minimum / `MinimumTotalForce` / `MinimumTotalSize`. Otherwise it falls
  through, which the port reads as a veto (`ContrastM::score`). The decompiler loses the returned floats, so the return
  values are the port's reading.
  - Against a neutral pad (no enemy force, so nothing remains in the lists), the solve stops once MainForce's `= 1` is
    filled. The EscortForce minimum is 0.
  - The retail runs agree: each X-Wing team secures and holds its own pad, so each plan has one unit
    (`SKAI_T17_RE_QUESTIONS.md`).

### 1.3 `Get_Force_Count`, `Reinforce` and `FindTarget("Is_Friendly_Start")` [H]

- `Get_Force_Count` (0x5fd970) returns the size of the member list TF +0x78..+0x80 (written by `Add_Force`). It counts
  neither wanted entries (+0x60) nor pending reinforcements. A task force with no members returns 0.
- `SpaceTaskForceClass::Reinforce` (0x6e8620) with no pending reinforcements logs the warning "taskforce %s has no
  reinforcements" (0x248620, no exit flag) and returns no value. `BlockOnCommand(nil, …)` still runs one `PumpEvents`
  (one yield) and returns nil (`PGBase.lua`).
- `FindTarget` (0x697a20) looks the name up with `Get_Managed_Object` on `ThePerceptionFunctionManagerPtr`. That is the one
  global equation manager, filled from every `*.XML` in `Data\XML\AI\PerceptualEquations\` (A2K_T06_RE §1.2).
  - So `Is_Friendly_Start`, defined only in `BASICLANDEQUATIONS.XML`, is known to a space player. There is no "unrecognized
    perception function" error and no exit flag.
  - The candidates are the plan player's target list for the current mode. The task force's position is read only when a
    range argument is given; QuickReinforce passes `1.0` and no range.
  - It returns the best `Tactical_Location` target by `IsFriendlyStartLocation` (AI_TACTICAL_PERCEPTION §3.3), or nil when
    every evaluation fails (no `Player_%d_Position_Marker`) [M: whether the Geonosis space map has the marker was not
    checked]. Either way the following `Reinforce` returns nil at once.
- `_ScriptExit` (0x255610) sets script byte +0x121, the same flag as the error helper 0x248190. The script service then
  resumes none of the script's other threads and ends or resets the whole script at the end of the pass (LUA_API "What an
  error does"). **A `ScriptExit()` in any thread of a plan ends the plan.**

### 1.4 Net answer: where the escort thread stops, and whether the plan survives

| Step | Retail, EscortForce with no selection | Port today (task force created with 0 members) |
|---|---|---|
| Plan start | MainForce created, `MainForce_Thread` started; **no EscortForce, no `EscortForce_Thread`** | both created, both threads started |
| Escort thread | does not exist | `Produce_Force` finishes at once; `QuickReinforce(…, EscortForce, MainForce)`: own count 0 → skip; MainForce count ≠ 0 → `EscortForce.Reinforce(MainForce)` nil; `FindTarget` → `Reinforce` nil; count 0 → `ScriptExit()` at `PGTaskForce.lua:350` → whole plan ends |
| Main thread | `QuickReinforce(…, MainForce, nil)`: MainForce count ≠ 0 → `Reinforce(MainForce)` nil (no pending); `second_try_tf` nil → skipped; `FindTarget` / fall-back `Reinforce` nil; count ≠ 0 → returns. Then `Move_To` the pad, `Guard_Target` until captured, then `Guard_Target` with `wait_for_build_time = -1` on a refinery pad | ended with the plan |

Retail never reaches line 350 for this plan. MainForce keeps guarding because the escort task force and its thread were never
created [H code path; M that the retail EscortForce had no selection, which the traces show]. If MainForce had no member at
that point, its own `QuickReinforce` would reach line 350 and end the plan. Retail shows the X-Wing team moving and holding,
so it has its member.

### 1.5 Port rule

At plan start, create a task force only when its TaskForce definition received at least one selection (free-store unit,
to-be-produced unit or reinforcement-pool entry) or carries `TaskForceRequired`. Otherwise do not bind its Lua global (it
reads nil) and do not start `<name>_Thread`. Do not register its event handlers either; no `<name>_*` event can fire without a
task force. A `ScriptExit()` from any thread ends the whole plan, as the port already does.

---

## Q2. Order of the first `Tactical_Multiplayer_Build_Space_Units_Generic` queue

### 2.1 TF +0x60 and the build-task order [H]

- **Produce_Force.** The `Produce_Force` Lua member jumps to vtable slot +0xa8. SpaceTaskForceClass (vt 0x8c1b88) has
  0x5fe240 there, the same function as the galactic and land classes.
  - 0x5fe240 walks TF +0x60 from the first entry to the last and calls 0x524ee0 once per entry. Each call creates an
    AIBuildTask and runs its state machine at once (A2M_T03 Q2).
  - The tasks are made, and so first reach the production step, in TF +0x60 order.
- **TF +0x60 order** is the order of 0x6c0100's pairs:
  - 0x6c0100 walks the PP team slots (PP +0xe8, one slot per TaskForce definition × team, built by Plan Setup A 0x72a3b0
    in definition order; AI_GALACTIC_2 §1.4).
  - For each slot of this definition it walks the slot's assigned selection indices (slot +0x10). Plan Commit appends
    each selection to its slot in commit order.
  - So the order is **team by team in definition order, and within a team in commit order.** Pick order across teams
    plays no part.
- In `TacticalMultiplayerBuildSpaceUnitsGeneric.lua` the ReserveForce teams are, 0-based:
  - 0: RS upgrades `= 0,1`;
  - 1: ES upgrades;
  - 2: US upgrades;
  - 3: pirates `= 0,2`;
  - **4: the Rebel unit line `= 0,3`**;
  - 5: Empire units;
  - 6: Underworld units;
  - 7-9: hero lines `= 0,1`.
- So a Rebel plan's list is always [RS upgrade (if selected), then its Rebel units in commit order].

### 2.2 The first solve: Rebel, 6000 credits, station `Skirmish_Rebel_Star_Base_1` [H unless tagged]

Space marshaller order: FreeStore, TechTree, Plan, Reinforcement, Hero, Contrast, TacticalBuild (AI_GALACTIC_2 §1.1). This
plan sets `IgnoreTarget = true` and `AllowFreeStoreUnits = false`.

- **Candidates** are created by Plan Setup B in slot order, for types affiliated with the Rebels:
  - RS_Level_Two … RS_Level_Five_Starbase_Upgrade first;
  - then the Rebel unit line in its written order (X-Wing, Y-Wing, A-Wing, …);
  - then the Rebel heroes.
- **Per-candidate score:**
  - FreeStore 0 (non-hero, no free-store units allowed).
  - TechTree `tech/5 × 2`: 0 for X-Wing and Y-Wing (`Tech_Level` 0), and 0 for the upgrade [M: no `Tech_Level` tag, so 0].
  - Plan: `1 − assigned/max` of a team with room. With no room it is a permanent veto.
  - Reinforcement 0 (AllowFreeStoreUnits false, 0x7276a0).
  - Hero 0.
  - Contrast: one synced draw `Get_Float(0, power(T)/500)` (0x532480 with `AI_Combat_Power` 0x370e60, IgnoreTarget branch of
    0x72b940).
  - TacticalBuild 0x727d30, for non-structure types in a build plan:
    - the tactical price c (0x496d80) must be > 0, and `c + PP+0x44 ≤` the player's credits (player +0x70);
    - 0x288b80(player, T) must pass;
    - some builder object must pass 0x497140: T is in the builder type's per-faction tactical buildable list, build limits
      hold, price and build time are > 0, and the required structures exist.
    - Then the score is 1.0. Otherwise it is a permanent veto [M: return values lost by the decompiler; the port reads them
      the same way].
    - Commit 0x728000 adds c to PP +0x44 for a selection with no free unit and no pool entry.
- **What station level 1 can build.** `Skirmish_Rebel_Star_Base_1` (STARBASES.XML,
  `Tactical_Buildable_Objects_Multiplayer`) lists X-Wing, Y-Wing, two L1 upgrades and RS_Level_Two_Starbase_Upgrade. So in
  this plan only RS_Level_Two, X-Wing and Y-Wing pass TacticalBuild; every other candidate is erased.
  - 0x288b80 and 0x497140 read neither `Build_Initially_Locked` nor `Required_Star_Base_Level`. So the Y-Wing (locked, level
    2 in the galactic sense) is buildable at level 1 [H for what the two functions read; retail queues a Y-Wing at about
    frame 70 in every run, §2.3].
  - Prices: upgrade 2000, X-Wing 500, Y-Wing 550 (`Tactical_Build_Cost_Multiplayer`).
- **The picks** (solver rules in AI_GALACTIC_2 §1.2: 50 % rule, strictly greatest score, ties to the lowest index):
  1. U scores 2.0 [M: upgrade power 0, so its draw adds 0]; X scores 2.0 + dX and Y scores 2.0 + dY. The highest wins;
     on a tie U wins (lowest index).
  2. The type already picked makes up 100 % of the selection and is dropped. The better of the other two is picked.
  3. Both picked types are at 50 % and are dropped. The third type is picked. After three picks, U, X and Y are each in
     the selection once.
  4. The RS team is full, so Plan vetoes every RS level for good. X and Y are at 1/3, so the higher draw of the two is
     picked.
  5. The unit team holds 3, so X and Y are vetoed. Nothing is left; Finalize runs. `RequiredCategories` (Fighter …) is
     met, so the solve succeeds.
- **Result:** 4 selections: 1 upgrade and **3 of the `= 0,3` line** (two of one fighter type and one of the other). The
  cumulative price is at most 2000 + 2 × 550 + 500 = 3600, below 6000, so credits never veto. The team maximum is the limit.
- **TF +0x60 and Produce_Force order:** RS_Level_Two_Starbase_Upgrade, then the three fighters in the order they were
  committed.

### 2.3 Retail check (main checkout `tools/probe_mod/q2_traces/out/`, `TBUILT` records, Rebel player 1) [H]

| Run | Built after the upgrade (82.33 s = frame 2470) |
|---|---|
| 20261108-204213 | X 97.33, Y 114.33, X 129.33 |
| 20261115-225742 | Y 99.33, X 114.33, X 129.33 (X 144.33 from a later plan) |
| 20261115-235429 | X 97.33, Y 114.33, X 129.33 |
| 20261116-002920 | X 97.33, Y 114.33, Y 131.33 (A-Wings from 149.33, a later plan at level 2) |
| 20261116-003415 | Y 99.33, X 114.33, X 129.33 (X 144.33 later) |
| 20261116-003910 | X 97.33, Y 114.33, X 129.33 |
| 20261116-004405 | Y 99.33, X 114.33, X 129.33 (A-Wing 147.33 later) |

(Run 20261115-230359 has no `TBUILT` records.)

- **Queue timing.** The station queue builds one item at a time in queue order:
  - the upgrade's 80 s (2400 frames) runs from about frame 70;
  - each fighter follows back to back (X-Wing 15 s, Y-Wing 17 s).

  [M: queue rule from the timings]
- **Every run.** The first queue is the upgrade plus three fighters with both types present, the shape §2.2 predicts. Which
  fighter comes first and which type is doubled vary with the synced draws.

**Port today:** the pick order Y-Wing, X-Wing, upgrade, X-Wing matches §2.2. `claim_force` issues Production in pick order,
so the queue starts with the Y-Wing (out at 581) and the upgrade waits until 3431.

### 2.4 Port rule

Build a task force's wanted list, and issue its build tasks or Production, **team by team in the order the teams appear in
the TaskForce definition**; within one team, keep commit order. The solve and its picks stay as they are. For this plan the
starbase upgrade is then queued first, as in every retail run.

---

## Confidence and open items

| Claim | Tag |
|---|---|
| Task force made only with a selection, a pool entry or `TaskForceRequired`; otherwise no global and no thread (0x66fb30, 0x5f7090, 0x244740) | H |
| Non-build tactical plans select only free-store units or pool entries (0x7276a0) | H |
| The `Secure_Build_Pad_Space` escort gets no unit because of the contrast "everything met" gate | M (port reading of 0x72b940 returns; traces agree) |
| `Get_Force_Count` = members only | H |
| `FindTarget` resolves names globally, so `Is_Friendly_Start` works in space | H |
| `_ScriptExit` from any thread ends the whole plan | H |
| TF +0x60 / Produce_Force order = team-slot order, then commit order (0x6c0100, 0x5fe240) | H |
| First solve: upgrade + 3 fighters, both fighter types present; credits do not bind | H rules, M scores (decompiler-lost returns), H traces |
| Station queue builds in queue order, one at a time | M (trace timings) |

Not read: what to-be-produced AIBuildTasks would do in a non-build space plan (the case does not arise); the pooled-script
reset path for plan scripts; whether `_MP_SPACE_GEONOSIS` has `Player_%d_Position_Marker` objects.
