# b0416 RE answers: does retail hand an occupied Mobile Defense Unit to a second `BuildMDUItem` plan?

Date: 2026-11-14. Binary: StarWarsG.exe (x64 Steam). Addresses are RVAs. Tags: [H] read directly in the
decompilation, the exe's vtable bytes or the shipped XML / Lua, [M] inferred from neighbouring code or an earlier doc,
[L] guess. No decompiled code is reproduced.

Case: land skirmish, Underworld AI, `AI_PLAN_EXPANSIONGENERIC_BUILDMDUITEM.LUA` (one task force `MainForce`,
`"Underworld_Mobile_Defense_Unit | Rebel_Mobile_Defense_Unit | Empire_Mobile_Defense_Unit = 1"`, `IgnoreTarget`,
**no** `AllowFreeStoreUnits` / `AllowEngagedUnits` line, so both are the default true, AI_GALACTIC §1.9.1). In the
port a later `Build_*` goal gets the same, already deployed MDU whose pad holds a turret, and `mdu.Build(...)` raises
"already has something built on it".

Short answer: **retail does not get there.** The free-store marshaller of the plan solve (FreeStore Setup A 0x7287a0)
drops every free-store unit that has TACTICAL_BUILD_OBJECTS and a pad with a finished or under-construction object.
The occupied MDU is never a candidate, never reserved, and a build task's type claim on it lapses. The port's tactical
Setup A does not have this test.

Vocabulary (earlier docs): behaviour id 0x29 = TACTICAL_BUILD_OBJECTS, slot byte obj +0x35b (0xff = absent); pad
pack obj +0x150 (+8 UC object, read by 0x42e3d0; +0x10 finished object, read by 0x42e3b0); PP = PotentialPlanClass
(+0x198 mode: 0 galactic, 1 land, 2 space; +0x104 solve counter; +0x108 selected units, null = to be produced;
+0x120 pool index per selection); free-store entry +0x30 = the PP that reserves the unit; BT = AIBuildTaskClass;
plan definition +0xd0 `AllowFreeStoreUnits`, +0xd1 `AllowEngagedUnits`, +0xa8 MagicPlan.

## Q1. Which existing units can fill "Underworld_Mobile_Defense_Unit = 1"

### 1.1 The candidate test: FreeStore Setup A 0x7287a0 [H, re-read for this task]

Runs at the start of every PP solve (Init 0x6c0d00 at proposal, the re-solve at accept; land marshaller order
FreeStore, Plan, Reinforcement, TechTree, Hero, Contrast, TacticalBuild, AI_GALACTIC_3 §1.1), and only when the plan
definition has `AllowFreeStoreUnits` and is not a MagicPlan. It walks the **land free-store map** (free store +0x40)
in list order. Units that are not on the free store (task-force members, units inside a landing) are never seen. An
entry becomes a candidate only when all of the following hold, in this order:

1. Reservation: PP +0x104 == 0 (first solve) or entry +0x30 == 0.
2. Health: 0x396470(unit) >= `Health_Low_Percent_Threshold` (0x140b27338; FoC 0.33).
3. (Distance key: computed here for a goal with a target; never rejects in tactical.)
4. Clause A "engaged": entry +0x30 == this PP, or plan definition +0xd1, or no TEAM behaviour (+0x348), or no
   locomotor pack (+0xa8), or pack +0x1c4 == -1. An MDU has no TEAM and the plan allows engaged units: always passes.
5. Clause B: no SPECIAL_WEAPON (+0x361) and no LOBBING_SUPERWEAPON (+0x369), or the weapon is ready. Passes for an MDU.
6. Garrison: obj +0x210 null, or its +8 == 0 and +0x34 == 0.
7. Limbo: obj +0x3a7 == 0.
8. Hero clash: obj +0x3b4 == 0.
9. Lock 0x39ad40 false: lock count obj +0x3a8 <= 0; the unit's locomotor vf +0x1d0 (0x42cbf0 for Walk, vtable
   0x8ad798 slot 58 [H]) false, i.e. the top path record's lock byte +0x5c is clear; and not a member of a TEAM whose
   container locomotor is locked.
10. The company type (0x3973b0) has no INDIGENOUS_UNIT (0x20) in the mode's behaviour set.
11. **Build pad: if obj +0x35b != 0xff, take `Get_Behavior(0x29)`; when 0x42e3b0 (finished object) or 0x42e3d0 (UC
    object) is not null, the entry is skipped.** This is the last test before the entry is appended to the candidate
    vector 0x140b3a5b8.

Test 11 is not new: A2K_T02 Q2(c) item 7, A2M_T04 Q1 and BKG1_RE_ANSWERS §1.1 list it for the galactic solve (where it
never fires). It is mode-independent code and is the rule that matters here.

Answers to the four sub-questions:

| | Excluded? | Where |
|---|---|---|
| (a) a deployed unit | **No.** Nothing in Setup A reads the Walk deploy state (0x18 / 0x19 / 0x1a) or the DEPLOY ability. A deployed MDU with an **empty** pad is a normal candidate [H] | — |
| (b) behaviour 0x29 and a UC or finished object on the pad | **Yes**, test 11 [H] | 0x7287a0, every solve, first solve included |
| (c) a unit that cannot move | **No** movability test. The coordinator admission 0x425e30 is not called from the solve; the lock test 9 reads only the lock count and the path-record lock byte, and a standing deployed unit has neither [H code, M that a deployed MDU has no locked path record] | — |
| (d) a unit flagged by a previous plan | **No per-unit plan flag.** Only the live reservation entry +0x30 (cleared when the plan ends, test 1), `Lock_Current_Orders` (+0x3a8, test 9; this script never calls it) and obj +0x3a2 bit 2 (a +2 score bonus in 0x728560, meaning unknown [L]) | — |

### 1.2 What the solve does without a free candidate [H from AI_GALACTIC_3 §1.4-1.10, not re-read]

- FreeStore Score 0x728560 returns 0 with no free unit of the type.
- Reinforcement Score 0x7276a0: a matching entry of the reinforcement pool (skirmish: the player's bought-unit table,
  player +0x2c8) scores 2.0 and becomes the selection (PP +0x120 = pool index). The pool type
  `Underworld_MDU_Company` matches the candidate `Underworld_Mobile_Defense_Unit` through its first `Company_Units`
  member. With no free unit and no matching entry (or no population room, or reinforcements disabled) it returns the
  veto 1e18.
- TacticalBuild Score 0x727d30 is "not applicable" for a normal unit type in a plan with `AllowFreeStoreUnits`: **this
  plan never buys an MDU.**
- So the task force's one required unit is either a free MDU that passes 1.1, or a pool company. With neither, the
  team's minimum of 1 is not met, the solve fails (PP +0x4c clear), `Find_Plan_For_Goal` 0x6bae50 releases the PP and
  the goal has no plan in that proposal pass [M for the last link: the Plan marshaller's finalize was not re-read].

### 1.3 Instantiation (0x6bad60 -> 0x5f7090 -> 0x66fb30) and `Produce_Force` for an on-map unit

- 0x66fb30 creates the task force with wanted types only, no members (A2M_T03 Q1). In tactical, 0x6c0100 lists a
  selection only when it has a free unit or its pool index is -1; a pool selection is left to `Reinforce` [H, A2M_T03].
- `Produce_Force` 0x5fe240 makes one BT per wanted entry. BUILD_TASK_LIST 0x648980 [H, re-read]: in skirmish
  (0x28afb0 true) with `AllowFreeStoreUnits` it takes the claim branch: 0x4d2bd0 makes the task a claimant of **every**
  free-store entry whose representative type (0x4d3900) equals the task's type, with no reservation and no pad test;
  no entry of the type -> state 7 (failed). So the task also claims the occupied MDU.
- The claim is honoured by the free-store service 0x4d5a70 (every 3 frames) through the land hand-over test, free
  store vf +0xb0 = **0x6908f0** (LandFreeStoreClass vtable 0x8b6b68 slot 22) [H, vtable bytes read from the exe]:
  for a unit with SPECIAL_WEAPON / LOBBING_SUPERWEAPON first the charge test (no weapon pack -> 2, not ready -> 1);
  then **0 (hand over) only when entry +0x30 equals the task's PP** (TF +0xf0 -> +0x88 -> +0x68), else **2 (the claim
  lapses, 0x649070)**. Same reservation rule as the galactic 0x690240, without the "at a planet" test.
- The occupied MDU was not selected, so it is not reserved by this PP: its claim always lapses. Only the unit the
  solve selected (reserved at accept, A1_READINGS §28.4) is handed over (0x647df0, tactical state 3) and joins the
  task force.

So there are two gates, both [H]: Setup A never selects the occupied MDU, and the hand-over gives a build task only
the unit its own PP reserved.

## Q2. The MDU after the first plan's `ScriptExit()`

- Plan end 0x5f6a50: `Release_Forces(1.0)` returns each member to the free store through the land admission
  0x690a10 [H for the admission, re-read; M that the release uses the same slot, AI_GALACTIC §1.9.4]. The deployed
  MDU passes it: not `Create_Team`, no TRANSPORT, type not `Is_Dummy`, has the implicit STARSHIP_COMBATANT (0x13) and
  a locomotor (WALK_LOCOMOTOR stays attached while deployed), no INDIGENOUS, no container, owner matches. The
  admission has **no** deploy or pad test. The new entry has +0x30 = 0: it is a plain free unit.
- Nothing marks it unavailable. Its only protection is Setup A test 11, evaluated fresh at each solve from the pad
  pack. When the turret dies or is sold (pad slot cleared, B0173_DEPLOYED_BEHAVIOR_RE Q3b) the same MDU, still
  deployed, is a candidate again, and the script then works on it (Q4, "empty pad" row).
- `BusyTacticalFreeStore` (`UnitServiceRate` 2): `On_Unit_Service` runs for it about every 2.1 s (category Vehicle,
  not Structure / Transport; entry unreserved). With no active orders it tries, in order: `SPOILER_LOCK` (no such
  ability), `Service_Kite` (`Move_To` away from a deadly enemy that is good against it), `Service_Heal` (hull < 1.0:
  `Move_To` a `HealsVehicles` healer more than 100 away), `Service_Garrison` (nothing), `Service_Attack`
  (`Attack_Move` to the nearest enemy within 600, or anywhere in skirmish because `aggressive_mode` is true, or to
  `enemy_location`), `Service_Guard` (`Guard_Target` / `Attack_Move`) [H, shipped Lua]. Every one of these is a move
  order to an auto-deployer; with an occupied pad Walk `Deploy(false)` 0x61d300 refuses, so the MDU stays in state
  0x19 and does not move (B0173_DEPLOYED_BEHAVIOR_RE Q3a). The orders are re-issued every unit service for the rest
  of the battle. The script never calls `Lock_Current_Orders` on it and has no MDU or pad special case.
- A free MDU with an **empty** pad (the second MDU of a dropped company, or one whose turret died) is undeployed and
  walked off by the same free-store orders [M: follows from the same rules; not probed].

## Q3. Goal and plan side

- Goal definitions (`AI_GOALS_UNDERWORLD_LAND.XML`): `Build_Ysalamiri_Cage`, `Build_Rocket_Pod`,
  `Build_Sensor_Scrambler` are `Tactical_Location`, `Med_Priority`, `Low_Threat`, `Time_Limit` 0.0, and have **no**
  `Tracking_Duration`, `Per_Failure_Desire_Adjust` or activation tracking [H]. Nothing damps a repeat.
- Equations (`AI_EQUATIONS_UNDERWORLD_LAND.XML`) [H]: `Should_Build_Rocket_Pod` = 50 x `Target.EnemyForce` x
  `Target.FriendlyForce`; `Should_Build_Sensor_Scrambler` = 50 x (friendly Structure force + friendly Vehicle force
  at the target); `Should_Build_Ysalamiri_Cage` = 50 x (a listed Force hero present) x `Target.FriendlyForce`. None
  reads `HasBuiltObject`, an MDU count or a turret count (the generic skirmish pad equations do use
  `1 - Variable_Target.HasBuiltObject`; these three do not). So the goals **are proposed again** after a build,
  for the same or another hint zone.
- The plan cannot buy an MDU (Q1.2). MDUs are bought by `TACTICALMULTIPLAYERBUILDLANDUNITSGENERIC.LUA`
  (`ReserveForce`, `AllowFreeStoreUnits = false`), whose Underworld row contains `Underworld_MDU_Company = 0,1`
  [H]. The company costs 1200 (`Tactical_Build_Cost_Multiplayer`), needs `UC_Level_Two_Tech_Upgrade`, has both
  build-limit tags commented out, and its `Company_Units` lists **two** `Underworld_Mobile_Defense_Unit` [H, shipped
  XML]. A finished company goes to the reinforcement pool, not to the map (SKIRMISH_SETUP §3.1).
- What retail therefore does with a second `Build_*` goal:
  1. a free MDU with an empty pad exists -> it is selected; the script runs on it (Q4);
  2. else a pooled `Underworld_MDU_Company` exists -> pool selection; `Are_All_Units_On_Free_Store` is false, so the
     land branch of `QuickReinforce` (PGTASKFORCE.LUA) runs `WaitForAllReinforcements` and then **`ScriptExit()`**
     ("abandoning plan for reproposal") [H, shipped Lua]. That plan run only drops the company; its MDUs (two per
     company [M: one object per member, SKIRMISH_SETUP §3.1 item 4]) go to the free store undeployed, and a later
     proposal takes case 1;
  3. else -> the solve fails and no plan runs. The goal waits until the unit-build plan has put another company in
     the pool.

## Q4. If the script did run on an occupied, deployed MDU

It cannot in retail (Q1), but each call is defined:

| Step | Result |
|---|---|
| `BlockOnCommand(MainForce.Produce_Force())` | claim, hand-over, block ends when the task ends |
| `QuickReinforce(...)` | all units on the store -> only `Activate_Ability("SPREAD_OUT", false)` |
| `Move_To(Target, ...)` | nil, silent (B0173_RE_ANSWERS Q3) |
| `MainForce.Activate_Ability("DEPLOY", true)` | 0x5f9890 **always returns an AbilityBlockStatus** [H, re-read]. Per member it calls 0x570a50, which returns false for DEPLOY when the requested state equals the current one (AI_TACTICAL §7.3.3; probe s78f t01). A false result subscribes nothing and queues plan event 0x13 `Unit_Ability_Finished(unit)`. The block's count stays 0, so `IsFinished` is true on the first poll: `BlockOnCommand` returns after one `PumpEvents`. No deploy request, no signal |
| `Set_Plan_Result(true)`, `Sleep(2)`, `mdu = Get_Unit_Table()[1]` | as usual |
| `mdu.Build(item)` on the occupied pad | 0x5739c0 [H, re-read]: after the type and "valid build location" tests, 0x42e3d0 or 0x42e3b0 not null -> **0x248190** with "GameObjectWrapper::Build -- Object %s already has something built on it.", returns nothing. 0x248190 is the **error** helper (sets script byte +0x121), not the warning 0x248620 (used in this function only for "game rules prevent building"). The plan script ends at the end of that service pass (B0173_RE_ANSWERS Q4) |

On a deployed MDU with an **empty** pad (the legal retail case) the same steps run and `Build` succeeds: the DEPLOY
line is the "equal -> empty block" case and costs one service.

## Q5. Conclusion

**No** [H for the rule, M for "no other route"]: in retail the "already has something built on it" script error is
not raised in this scenario. An MDU whose pad holds a UC or finished object is filtered out of the plan solve's
free-store candidates (0x7287a0, test 11), so the plan is staffed by another MDU (free with an empty pad, or a pool
company that is first dropped by a plan run that exits) or does not run. No route was found by which the shipped
script reaches `Build` on an occupied pad: a task-force member is off the free store, the pad is occupied from the
frame `Build` succeeds, and the hand-over gate keeps an unreserved unit away from a build task.

## Not determined

- Not probed in game. The conclusion rests on the code reads above.
- The Plan marshaller finalize that fails the solve when the team's minimum is not met was taken from AI_GALACTIC
  §1.9.3 / AI_GALACTIC_3 §1.10, not re-read.
- 0x649070 (claim lapse): when the last claim of a task lapses and the task has no AIPlanetBuildTask, the decompiled
  mode test reads "mode == 0 -> state 7 (failed)", while A2L_T01 Q7.3 words it as "tactical only". Which is right
  decides what a tactical task does when its only claimed unit is not reserved for it (for example the selected MDU
  died between accept and `Produce_Force`): fail (`Production_Failed`) or wait. Not needed for this case.
- Whether `Release_Forces` at plan end calls the land admission 0x690a10 directly or another insert path [M].
- Whether the two MDUs of one dropped company both join the dropping task force before it exits [M]; either way both
  end on the free store.
- What stops a free, empty MDU that the free store is marching toward the enemy when a plan takes it: the DEPLOY gate
  refuses a moving locomotor (0x3a1ce0) and the block then waits for 0xb / 0x29 / 0x10; a `Build` after an unrelated
  0xb would be the "not a valid build location" error of B0173_DEPLOYED_BEHAVIOR_RE Q5, a different message. Not
  traced here.
- The meaning of obj +0x3a2 bit 2 (free-store score bonus).

## Port consequence

The port's tactical free-unit list (`engine/ai/tactical/src/tactical_ai.cpp`, the loop that fills `in.free_units`,
and `FreeStoreM::setup_a` in `tactical_pp.cpp`) applies only the hero-clash, reservation and health tests of 1.1.

1. **Add Setup A test 11**: a free-store unit that currently has the TACTICAL_BUILD_OBJECTS behaviour and whose pad
   holds a finished or UC object is not a free-store candidate, in every solve (first solve included), for every
   plan. This alone removes the b0416 error: the occupied MDU is no longer selected.
2. Keep the test tied to the live pad state, not to a flag: once the pad is empty the deployed MDU is a candidate
   again, and no deploy-state test may be added (a deployed MDU with an empty pad is selectable).
3. For completeness the other Setup A tests the tactical port lacks are: garrison (inside / entering a garrison, or a
   garrison with occupants), limbo (+0x3a7), lock (0x39ad40: `Lock_Current_Orders` count, locked path record, locked
   team container), INDIGENOUS company type, and clauses A / B. They are separate tier-1 items, not needed for b0416.
4. With no candidate the plan may only use a reinforcement-pool entry (`Underworld_MDU_Company`); it must not buy a
   unit, and with neither the solve fails and no plan is instantiated for the goal in that pass.
5. A build task's free-store claim is by type, but the land hand-over gives a unit only to a task whose PP holds the
   unit's reservation (entry +0x30 == that PP); any other claim lapses. Check that the port's tactical
   `Produce_Force` cannot hand an unreserved same-type unit (the occupied MDU) to the task.
6. Leave the Lua side as it is: `Build` on an occupied pad is a real script error that ends the plan script, and a
   task-force `Activate_Ability("DEPLOY", true)` on an already deployed unit returns a block that is finished at its
   first poll (plus a queued `Unit_Ability_Finished`). Do not soften the error to hide a selection fault.
7. Expected retail-like behaviour after the fix: one turret per MDU; later `Build_*` goals use the company's second
   MDU, then wait for the unit-build plan to pool another `Underworld_MDU_Company`; a pool-staffed plan run only
   drops the company and exits.
