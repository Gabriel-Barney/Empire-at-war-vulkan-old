# skai t19: does a deployed MDU keep a land attack block? (questions, 2026-11-16)

Answer goes into `docs/re/SKAI_T19_MDU_DEPLOY_BLOCK.md` (new). REA and `re_reference/` only (no ReVa turn was booked).

## The port's failure

`AI_PLAN_EXPANSIONGENERIC_BUILDMDUITEM.LUA` (shipped): `Produce_Force` → `QuickReinforce` →
`BlockOnCommand(MainForce.Move_To(Target, MainForce.Get_Self_Threat_Max()))` →
`BlockOnCommand(MainForce.Activate_Ability("DEPLOY", true))` → `Build(...)` on the MDU.

On a WIP branch (claude/tru1-a, seed 8, `--skirmish-ai foc-land --ai Empire,Underworld`) the port's MDU finishes DEPLOY
(Walk state 0x19, signal 0x29, DeployedBehavior TACTICAL_BUILD_OBJECTS added) while it still holds a **land attack block**
(the port's `TacticalBattle::attack_blocks_`, the attack formation's 30-frame re-check of FIGHTER_FLYING §4.11 A / E and
MOVEMENT §4.3.2). The next re-check re-plans a path for it; the formation plan's cap test (MOVEMENT §3.2.1 item 2,
0x3ac100(member, path length), port `plan_deploy_cap` / `deploy_path_start`) calls Walk `Deploy(false)` 7 frames later, the
pad is empty so it undeploys (B0173_DEPLOYED_BEHAVIOR_RE Q3 (a)), the DeployedBehavior is removed, and the plan's `Build`
fails "Underworld_Mobile_Defense_Unit is not a valid build location". The port already drops the block on DEPLOY's stop
(b0416, `tactical_battle_oi_abilities.cpp` StopOrder; DEPLOY 0x3a1ce0 "stops, then adopts", MOVEMENT §4.7.2 / §4.7.5).

## Questions

1. **DEPLOY on.** `Activate_Ability("DEPLOY", true)` on a Walk auto-deployer (0x3a1ce0(obj, 1)): in order, what does it do
   with the unit's movement coordinator / formation membership and with any object block (Attack_Move / Guard kind-3 block,
   the auto-attack block push 0x41fd30, the attack formation timer +0x1d8)? The DEPLOY-off path is known to tell TARGETING
   to stop (vf +0xd8(1)) and leave the coordinator (0x426d50 + 0x39bcb0); does the on path do the same, before or after the
   deploy clip starts?
2. **A deployed unit and re-plans.** While a Walk unit is in state 0x18 (deploying) or 0x19 (deployed), can anything re-path
   it so that the cap test 0x3ac100 → `Deploy(false)` runs: the attack formation's periodic re-check / re-plan, a coordinator
   re-plan, the land auto-attack push 0x41fd30 when an enemy comes into view, TARGETING's own approach? Name the gate that
   skips a deployed unit (state test, coordinator admission 0x425e30, behaviour 0x29 test, "has weapons" test), or say there
   is none.
3. **Move_To(object, threat) leftovers.** When `TaskForce.Move_To(object, threat)` finishes (the MDU arrived), does the unit
   keep a coordinator membership, an object block or a pending attack order that a later frame re-plans?
4. **Timing.** After DEPLOY's 0x29 ends the `BlockOnCommand`, does the plan script resume (and call `Build`) in the same
   frame, or at the plan's next script pump (how many frames later)?

The port change depends on 1 and 2: either DEPLOY on drops the unit's attack block / coordinator (as off does), or deployed
units are skipped by the re-plan; or (if retail does neither) the port's block was set by a path retail does not have.
