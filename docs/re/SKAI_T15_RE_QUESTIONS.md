# skai t15: what ends retail's land Destroy_Structure plans at the next commit (questions, 2026-11-16)

Answers go into `docs/re/SKAI_T15_PLAN_RETEST.md` (new) and a short pointer in `AI_TACTICAL.md` §7.7.3 item 3.

## What the retail trace shows

Retail run `20261114-210208-skirmish-vanilla-lua` (Empire Medium AI against an idle Underworld human, Utapau Heights, kit
`tools/probe_mod/q2_traces`, plan records described in `make_plan_hooks.py`; the run folder is in the worktree
`.claude/worktrees/foc-underworld-mdu-destination-6eb8bc/tools/probe_mod/q2_traces/out/`). For `Destroy_Structure`
(`Data/Scripts/AI/LandMode/DestroyStructure.lua`: MainForce_Thread = Produce_Force, QuickReinforce,
Set_Land_AI_Targeting_Priorities, `BlockOnCommand(MainForce.Attack_Move(AITarget, ...))`, ScriptExit):

- PSTART, then PBLOCK k=1 (Produce_Force returned, MainForce count 4-10, e.g. AT_ST_WALKER, DARK_TROOPER_PHASEI,
  STORMTROOPER_TEAM) about 0.5 s later, then **PEXIT about 13.4 s after PBLOCK** in 12 of 15 plans (one 27 s, one 83 s),
  with **tf=? (not the MainForce thread) and MainForce count 0**. No PBLOCK k=2: the Attack_Move block never returned.
- The PEXITs fall exactly on the AI's commit ticks (PSTARTs come every ~13.9 s: 655.7, 669.7, 683.6, 697.5, 711.4 s ...;
  PEXIT 669.63, 697.50, 725.33 s ...). Two plans on different targets exit at the same instant (697.50, 808.90).
- Targets: U_GROUND_VEHICLE_FACTORY, U_GROUND_DROID_WORKS, SECONDARY_MERCENARY_OUTFITTER, U_GROUND_PALACE (structures
  with AI_Combat_Power, unlike the command centres; AI_TACTICAL §7.7.2).
- The port keeps such a plan for thousands of frames (plan 14, 4847-9896: AT-STs on the vehicle factory for 5000 frames;
  `B0418_UTAPAU_AI_RUNS.md` §8).

Port today: tactical `plan_invalid` (`engine/ai/tactical/src/tactical_ai.cpp` ~l.1257) is only "plan ended or a PP unit was
stolen" (0x6c45e0); the running-plan contrast re-test of 0x6c47c0 step 2 is not ported (`A2M_T04_RE_ANSWERS.md` Q4.3,
`BKG1_RE_ANSWERS.md` §4.2; `engine/ai/src/galactic_ai.cpp` keeps `test_contrast` "inert"). Hypothesis: that re-test fails at
the first commit for a land plan whose units the task force took off the free store, so the goal is dropped and the plan's
units are released.

## Questions (REA only; no ReVa needed)

Q1. **0x6c47c0 for a running tactical plan (PP +0x100 set, PP +0x198 != 0, land).** Its steps in order. In step 2 (the
contrast re-test when PlanDef +0xaa == 0 and PP +0x4d != 0): which lists are rebuilt (0x5f1d00 main / escort as §7.7.1), and
**which units are committed against them** by 0x6c4b20 (PP +0x108? +0x1b0 / +0x1c8? what fills those for a running plan):
do units the task force took off the free store with Produce_Force still count; dead units; units released by single-unit
handlers? Unit power used (full `AI_Combat_Power`, health-attenuated, category masks). The tolerance used (the same T as
at accept, f = recent failures?). The exact condition that sets +0x4c = 0.

Q2. **Where it runs and what a failure does.** Is 0x6c47c0 called for every active goal at every Commit in tactical
(Resolve_Category 0x6c8970 kept-goal path, maintenance 0x722a30 when a category has no proposals), or only on some paths?
On failure: release 0x614880 / goal deactivation 0x693750 / plan end 0x5f6a50 order. Is the PlanDef +0x60 -> +0xa8 flag
("keep despite invalid") set for any land plan, and what sets it (XML tag, Lua call)? Is it set for LandMode
DestroyStructure?

Q3. **Does it explain the trace?** With the answers to Q1-Q2: a Destroy_Structure task force of 4-10 units against a
vehicle factory (its AI_Combat_Power) - does the re-test fail at the first commit after Produce_Force (and why), and does it
pass for a zero-power command centre (§7.7.6 says that plan holds until the CC dies)? What would make a plan survive one or
several commits (the 27 s and 83 s cases)?

Q4. **How the release produces a ScriptExit with MainForce count 0 from another thread.** After the goal system releases a
running plan: Release_Forces(1.0) empties MainForce; which Lua path then calls ScriptExit (the TF event
`Default_No_Units_Remaining` from PGEvents, GuardForce's `Escort` loop in PGTaskForce, or the plan-end code), and is the
thread killed afterwards? (The kit's earlier note says a goal-system release normally leaves no end record.)

Q5. Anything else at a tactical Commit that takes units out of a running plan's task force (a new PP's solve taking units
that are in a task force, MagicPlanStealing, the free store's re-admission) - rule it in or out.
