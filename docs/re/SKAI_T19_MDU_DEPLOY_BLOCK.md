# skai t19: DEPLOY on, the coordinator and the land attack block of a Mobile Defense Unit (RE, 2026-11-16) [H unless tagged]

Answers `SKAI_T19_RE_QUESTIONS.md` Q1-Q4. Read with REA (rea-agents on the FoC Steam x64 image, base 0x140000000:
decompile, disassembly of 0x3a1ce0's tail, vtable bytes, callers) and the decompiled reference; no ReVa. Addresses are
RVAs. [H] = read in the binary or the shipped XML / Lua; [M] = inferred; [L] = guess. No decompiled code is reproduced.

Functions: 0x3a1ce0 (DEPLOY toggle), 0x38a350 (ability activation, case 2), 0x391260 (deploy forwarder), 0x61d300 (Walk
`Deploy(bool)`), 0x5ad320 / 0x42c920 (Walk stop), 0x42c640 (Walk vf +0x2b0), 0x426d50 (leave every coordinator), 0x4f7d30
(coordinator vf4, remove), 0x39bcb0 (adopt), 0x425e30 (admission), 0x426140, 0x426010, land coordinator vtable 0x86b080
(vf3 0x438b40, vf4 0x4f7d30, vf8 0x43b470, vf12 0x43e5b0), 0x4f6b60 (add), 0x4f7360 (`Issue_Degenerate_Move`), 0x4f6fb0,
0x40d0d0 (formation ctor), 0x40e0f0, 0x419ee0, 0x423c10, 0x423070 (formation state 3), 0x410a60, 0x4165f0, 0x40edd0,
0x41c1a0 (formation plan), 0x3ac100, 0x4160a0, 0x42d0f0 (`Auto_Engage`), 0x41cf70 / 0x41fd30 (Push) and its callers
0x4f6070 / 0x4f6470 (UNIT_AI), 0x4f5e30, 0x3a9e30 → 0x58d930 / 0x58e370, 0x685490, 0x6859a0, 0x73c360 / 0x73bf40
(land task-force `Move_To`), shipped `PGBASE.LUA`, `AI_PLAN_EXPANSIONGENERIC_BUILDMDUITEM.LUA`, `MOBILE_DEFENSE_UNITS.XML`.

Unit facts used throughout [H, shipped XML]: `Abstract_Mobile_Defense_Unit` has `LandBehavior` AFFECTED_BY_SHIELD,
WALK_LOCOMOTOR, IDLE, POWERED, REVEAL, HIDE_WHEN_FOGGED, UNIT_AI, SURFACE_FX, TELEKINESIS_TARGET, STUNNABLE,
WIND_DISTURBANCE, DAMAGE_TRACKING, GARRISON_UNIT, plus `SELECTABLE`: **no TARGETING (id 9, slot +0x33b), no WEAPON, no
HARASS (0x41), no AVOID_DANGER (0x42), no TEAM**. The only implicit behaviour is STARSHIP_COMBATANT (OBJECT_TYPES §9.3),
so an MDU never has TARGETING. `Deploys` Yes, `Auto_Deploys` default yes, `Max_Dist_Walk_While_Deployed` M = 0.

## Short answer

- **DEPLOY on drops the unit's coordinator and every block, before the deploy clip starts** (Q1). 0x3a1ce0(obj, 1), after
  its gates, removes the unit from every coordinator (0x426d50), adopts it into a fresh coordinator whose one formation
  holds only a position block at the unit's own position and is put straight into state 3 (arrived, no plan), and only
  then calls Walk `Deploy(true)`. It does **not** tell TARGETING to stop (that call is on the off path only).
- **There is no deploy-state gate in the formation plan for an auto-deployer** (Q2): any plan of an MDU's formation that
  builds a path undeploys it (0x41c1a0 member loop → 0x3ac100 → `Deploy(false)`). What keeps a deployed MDU deployed is
  that nothing plans its formation without a new order: its adopted formation never re-plans; the "every member can
  fire" test 0x40edd0 **skips members without TARGETING**, so an MDU formation never re-plans from an attack block either
  and its plan ends without a path; `Auto_Engage` and the UNIT_AI responses need TARGETING; 0x4160a0 refuses a
  deployed unit with M = 0; the collision push refuses it.
- `TaskForce.Move_To(object, threat)` is a **position** move (kind 1, no attack flag); after arrival the unit stays in
  that coordinator in an arrived formation that never re-plans, and no order is pending (Q3). For this plan's region
  goals `Move_To(Target)` does nothing at all (B0173_RE_ANSWERS Q3).
- `Build` runs **63 to 65 frames after the deploy's 0x29** (66 to 68 in rare cases), not in the same frame (Q4).

## Q1. `Activate_Ability("DEPLOY", true)` → 0x3a1ce0(obj, 1), in order

Lua `Activate_Ability` (0x570a50, command 43, executed at once, AI_TACTICAL §7.3.3) → 0x38a350 case 2 → 0x3a1ce0(obj,
on). 0x38a350 stores the requested mode in the ability record only after 0x3a1ce0 returns true.

**Gates** [H code; M / L meanings as tagged]:
- Both directions return false, nothing done: a TEAM unit (+0x348) with no member that has ability 2 (when the ability
  table test 0x43fa90(…, 2) is false); a non-team unit with no DEPLOY ability record (obj +0x3f0 null).
- On only (the off direction ignores them and goes on): two tests on the pack at obj +0xf0 (0x380bb0; 0x380b10 with the
  ability table's 0x43fa30(…, 2)) [L: AI_TACTICAL's "running ability countdown"]; in limbo (+0x3a7) unless its own, or
  its parent's, GARRISON_UNIT pack (+0x210) has a non-zero count (+8); obj +0x100 → +0x28 set [L]; a CORRUPT_SYSTEMS slot
  (+0x38a) [M: meaning]; ABILITY_COUNTDOWN (0x46) reporting ability 2 not ready (0x42f8e0 false and 0x42f7a0 true); a
  team member failing `Can_Activate(2)` (0x39b480(m, 2, 1)); **locomotor vf +0x2b0 true, which for Walk (0x42c640) means
  a hyperspace state (4, 5, 0x28, 0x2b, 0x2c)**. So a **walking** MDU is not refused; DEPLOY stops it (step 3).

**Steps after the gates, on (disassembly 0x1403a1f27..0x1403a1f90 read)** [H]:
1. **No TARGETING stop.** The branch for on jumps over the `Get_Behavior(9)` → vf +0xd8(1) call; only off makes it.
   (An MDU has no TARGETING, so for it the two directions do not differ here.)
2. **Leave every coordinator** (0x426d50, when the mode has a coordinator system): each coordinator's vf4 (land
   0x4f7d30) removes the unit from its unit list, drops its 0x10 listener and removes it from each of its formations
   (0x421910). Exception [H]: a live unit that is order-locked (0x39ad40: `Lock_Current_Orders` count, locked top path
   record, locked team container) is **not** removed, and then step 3 fails its "no coordinator holds it" test, so it keeps
   its old formation. Nothing in BuildMDUItem locks the MDU.
   Blocks live on formations (FIGHTER_FLYING §4.11 B: F +0xf8 stack), so the old formation's move, attack-move, guard or
   auto (Push 0x41fd30) blocks, its path and its 30-frame timer F +0x1d8 stay with that old formation and its other
   members; none of them concerns the MDU any more. DEPLOY pushes, pops or rewrites no block itself.
3. **Adopt** (0x39bcb0, MOVEMENT §4.7.1): predicate passes for an MDU before the deploy (no TEAM parent, Walk kind 1,
   admission 0x425e30: no pad behaviour yet, not hyperspacing; no coordinator holds it after step 2). Then a new land
   coordinator (0x426010) and its vf8 0x43b470: vf3 add (0x438b40 → 0x4f6b60; sets coordinator +0xa8 := 1, raises signal 9
   on the unit) and `Issue_Degenerate_Move` 0x4f7360:
   - one new formation (0x4f6fb0 → ctor 0x40d0d0: **one `int(1, FPS)` draw**, F +0x1d8 := now + FPS + r, so its first
     30-frame check is 31 to 60 frames later); the unit is added (0x40e0f0, which points the unit's pack +0x1f0 at it);
   - the coordinator's block: **kind 1 (position)** at the mean member position (the unit's own position), flags 0;
     formation type from the locomotor (vf +0x238, land 1); `Set_Move`(F, block, 0);
   - coordinator +0xa8 := 0 (so no composition at the next coordinator update, MOVEMENT §4.6.2);
   - 0x419ee0: the formation's path vectors are reset and **F state := 3 (arrived)**. No plan runs, so no path, no
     member loop and no 0x3ac100.
4. **`Deploy(true)`**: 0x391260 (type `Deploys`, a locomotor, pack +0x2b3 clear) → Walk vf +0x118(1) = 0x61d300: not in
   0x18 / 0x19, no gravity field bit (+0x3a1 & 0x40), +0x2b3 clear → Walk stop vf +0x140 = 0x5ad320 (0x42c920: path
   stack reset 0x554480 and idle animation; claim test vf +0x170 → state 0x16, speed +0xec / +0xf0 := 0), **state 0x18**,
   Deploy clip (animation 0x19, `Deployment_Anim_Rate`). (Reversing out of 0x1a: MOVEMENT §3.2.)
5. Returns true.

So the coordinator leave and the adopt come **before** the clip starts, in the same call, during the AI service that ran
the Lua line. **Off**, for comparison [H]: gates never refuse it; TARGETING vf +0xd8(1); the same leave and adopt;
then 0x3ac100(obj, 0.0) → `Deploy(false)` (refused with an occupied pad, B0173_DEPLOYED_BEHAVIOR_RE Q3 (a)).

## Q2. Can anything re-path a unit in 0x18 / 0x19 into `Deploy(false)`?

**Where `Deploy(false)` comes from** [H]: 0x3ac100 (callers 0x41c1a0 plan member loop, 0x3a1ce0 off, 0x38a350 case
0x20 MISSILE_SHIELD, 0x591a90 DEPLOY_TROOPERS, garrison hosts 0x636090 / 0x6360b0) and Walk vf +0x148 0x61e570 (a path
start on a deployed / deploying unit with remaining length ≥ M; MOVEMENT §3.2.1 item 2). Neither 0x3ac100 nor 0x61e570
reads a "do not undeploy" flag; for M = 0 both undeploy whenever they run.

**The plan has no gate for an auto-deployer** [H, 0x41c1a0]: its only deploy test returns 0 when every member is a
deployed / deploying **manual** deployer. After a successful path build (0x417520, mode ≥ 2) every auto-deployer member
gets 0x3ac100(member, formation path length). So a plan that builds a path for a formation holding an MDU undeploys it.
That is the retail rule for a new move order, and it is why nothing may plan the MDU's formation by itself.

**What could plan or path a deployed MDU, and why it does not** [H unless tagged]:

| Route | Result for a deployed MDU |
|---|---|
| Its adopted formation (Q1 step 3), each formation step | state 3 → 0x423070: n = 1, the top has no object, so the hidden / stealthed rule is skipped; the space idle branch is mode 2 only; then 0x410a60 every 30 frames: a = 0x4165f0 is **false** (n = 1 and blocks[0] is not kind 3), c = 0x40edd0 is **true** (no object) → nothing. Never state 0, never a plan. 0x4192b0 runs only in state 1 (FIGHTER_FLYING §4.11 A) |
| An attack / object block on its formation (if it had one) | 0x40edd0 **skips members without TARGETING (+0x33b = −1) and busy members (0x39a350)**. An MDU-only formation is therefore "able to fire" unless the object is stealthed (members[0]'s type without `Target_Stealth_Units`) or fogged for members[0]'s owner. Then 0x410a60 never re-plans (it needs a true and c false), and the plan itself, for a non-guard object top with c true in a land formation, **clears the members' path stacks (locomotor vf +0x138) and returns success with no path** before the member loop (no 0x3ac100; MOVEMENT §4.3.2 item 6). Exceptions: a guard top (flag 0x02) skips that stop and plans; a hidden or stealthed object makes c false, and after arrival with n = 1, 0x423070 sets state 0 (re-plan, path, undeploy) when the lead is 40 or more from the object's point |
| Coordinator re-plan (composition) | only when coordinator +0xa8 = 1, set by vf3 add; units are added only by orders and adopts, which make new coordinators. The admission 0x425e30 has **no** deploy-state test (only the occupied-pad and hyperspace tests) |
| `Auto_Engage` 0x42d0f0 (land auto attack → 0x41cf70 → Push 0x41fd30) | called only from TARGETING's `Service_Land` (BKR1 Q2): never for an MDU. For units with TARGETING its 0x4160a0 returns 0 for D (0x39a390: state 0x19 or walking deployed) with M = 0, so a deployed unit gets no auto block; a **deploying** (0x18) unit is not D [M: effect for TARGETING units not traced] |
| UNIT_AI | attacked-by response 0x4f5e30 needs TARGETING (and passes 0x4160a0); the push 0x4f6470 (from the UNIT_AI service 0x4f6070) needs UNIT_AI +0x38, set only by Lua `Enable_Attack_Positioning` (0x4f6450, callers 0x6e4e30 / 0x6e8aa0 / 0x6eaa80), or `Keep_Moving_In_Battle` (unset in FoC), and an object goal on the formation |
| `Take_Damage` 0x3a9e30 | pushes only through HARASS (0x58d930) and AVOID_DANGER (0x58e370): an MDU has neither |
| Collision | the push 0x6859a0 refuses a deployed / deploying unit with M ≤ 0; StoppedVsStopped treats it as immobile (MOVEMENT §3.2.1 item 5); the MvS step-10 push 0x685490 acts on the mover's own formation |
| TARGETING's own approach | TARGETING does not path; the approach is `Auto_Engage` (above). A D / deploying unit with M ≤ 0 drops an out-of-range target (BKR1 Q2 item 2) |
| Formation copy / guard re-issue (0x4192b0, 0x41fd30 callers) | 0x4192b0's copy branch skips D / deploying members unless M ≠ 0 (FIGHTER_FLYING §4.11 G 4); state 1 only |

So there is **no single gate**. A deployed MDU stays deployed because (1) DEPLOY on leaves its old formation and puts it
in an arrived position-block formation that never re-plans, (2) 0x40edd0 counts a formation without TARGETING members as
able to fire, (3) every autonomous push needs TARGETING (or HARASS / AVOID_DANGER / `Enable_Attack_Positioning`) and
0x4160a0 refuses D with M = 0, (4) collision never moves it. Only a **new order** (player, Lua, AI script, free store)
re-plans it: a position move, a guard, or an object order on a hidden / stealthed object undeploys an empty-pad MDU; a
non-guard object order on a visible object stops it where it stands without undeploying [H for the branch; M that no
shipped script relies on it].

## Q3. What `TaskForce.Move_To(object, threat)` leaves behind

- `LandTaskForce.Move_To` (0x6ec410) turns a live object into its **position** (B0173_RE_ANSWERS Q2 row 2) and calls
  0x73c360 (mode 0, attack flag +0x56 := 0) → 0x73bf40: a new coordinator; each admitted member leaves its old
  coordinator (0x426d50) and is added (vf3); coordinator `Move_To` 0x4f8200 with a **kind 1** block, flags = the mode
  (0), mask 0x3f, the threat at +0x54, no object, no hard point [H]. No `Order_Attack` is issued (that is Attack_Move,
  mode 1).
- On arrival the formation raises signal 10 on its members (0x423760) and goes to state 3; the status drops the unit
  from its pending list and, with no object, finishes (A1_READINGS §33.2). The status is passive: it never re-issues an
  order, and its Result is nil.
- Afterwards the unit **keeps that coordinator membership**: an arrived formation (state 3) holding the position block,
  n = 1. Its 30-frame check 0x410a60 has a = false (n = 1, kind 1) and c = true (no object): nothing, ever. **No object
  block and no pending attack order** remain, and nothing re-plans it.
- In BuildMDUItem every goal is a region (`Tactical_Location`), so `Target` is a null-object wrapper and `Move_To`
  returns nil with **no order** (B0173_RE_ANSWERS Q3). The MDU then still has the membership it had before the plan
  took it (for example the free store's last order; a task-force member is not serviced by the free store any more,
  B0416_MDU_REUSE_RE Q1.1). DEPLOY on (Q1 step 2) removes it from that coordinator before the deploy starts.

## Q4. When `Build` runs after the deploy's 0x29

- Frame N, GOM object service: Walk vf6 0x61e930 in state 0x18 sees the Deploy clip end → state 0x19, signal 0x29
  (the `AbilityBlockStatus` listener sets the count to 0 at once, so `IsFinished` is true), then 0x39fec0 adds
  TACTICAL_BUILD_OBJECTS in the same call (B0173_DEPLOYED_BEHAVIOR_RE Q1 / Q5).
- The AI player runs after the mode tick of the same frame; Planning resumes the plan threads every 3 frames (AI_TACTICAL
  §2). First resume S0 = N, N + 1 or N + 2 [M: the Planning phase relative to N].
- Shipped `BlockOnCommand` [H]: at S0 the `repeat … until block.IsFinished()` loop ends, and the function then calls
  **one more `PumpEvents()`** (a yield) before `return block.Result()`. At S0 + 3 it returns; `Set_Plan_Result(true)`;
  `Sleep(2)` starts with StartTime = `GetCurrentTime()`.
- `Sleep(2)` yields once per Planning service while `GetCurrentTime() − StartTime < 2`, with `GetCurrentTime()` =
  (double)(float(mode frame) × 0.033333335f) (BKG1_RE_ANSWERS b0102). Computed over every start frame of the first hour:
  the wait is **60 frames** for 107 893 of 108 000 start frames and 63 frames for the other 107 (the two float reads
  differ by just under 2.0).
- At the resume that ends the sleep, `Get_Unit_Table()[1]`, `TestValid` and `mdu.Build(...)` run in the same pass.

So `Build` is called at S0 + 63 (rarely S0 + 66): **63 to 65 frames after the 0x29 frame** (66 to 68 in the rare case),
never in the 0x29 frame. Through that window the retail MDU stands in 0x19 with the pad behaviour, and by Q2 nothing
re-plans it.

## Corrections to earlier docs

- AI_TACTICAL §7.3.3 part 3: "it drops the target (TARGETING vf +0xd8(1))" holds for DEPLOY **off** only; the on path
  skips it. Its "[M: moving]" for locomotor vf +0x2b0 is wrong: for Walk it is the hyperspace-state test 0x42c640 (4, 5,
  0x28, 0x2b, 0x2c), so a walking unit is not refused.
- B0416_MDU_REUSE_RE "Not determined", the bullet on a free MDU marched toward the enemy: DEPLOY does not refuse a
  moving locomotor (above); it leaves the march's coordinator, adopts and stops the MDU, and the deploy starts.
- B0416_MDU_REUSE_RE Q2 ("every one of these is a move order … `Deploy(false)` refuses"): a free-store `Attack_Move` on a
  **visible** enemy object reaches no `Deploy(false)` at all for an MDU-only formation (Q2 table, row 2); the
  "MDU does not move" result is the same.

## Port consequence

1. DEPLOY on (after the gates, one call): leave every coordinator, which ends the unit's formation membership and its
   land attack block; adopt (one formation ctor draw, a position block at the unit's own position, formation state 3,
   no plan, the formation's own 30-frame timer at now + 30 + r); then Walk `Deploy(true)` (stop, 0x18, clip). Do not
   tell combat to drop the target on the on path (off only). Refuse only a hyperspacing locomotor, not a moving one.
2. Do not add a deploy-state gate to the formation plan or to `plan_deploy_cap`: retail has none for auto-deployers, and
   a new move order still undeploys an empty-pad MDU.
3. The port's "every member can fire" test (the attack block's 30-frame re-check, FIGHTER_FLYING §4.11 A / E, and the
   plan's stop branch, MOVEMENT §4.3.2 item 6) must skip members without TARGETING and busy members: a formation of only
   such units counts as able to fire unless the object is fogged for members[0]'s owner or stealthed, so its re-check
   never re-plans and its plan stops it with no path (no cap test, no `Deploy(false)`). With this rule the seed-8 MDU
   would not undeploy even with the block in place.
4. Find who set the block after DEPLOY on in the seed-8 run: the port's writers are Lua `Attack_Target`
   (`battle_ai.cpp` ~2209), Lua `Attack_Move` on an object (`battle_ai.cpp` ~2358) and the player attack
   (`tactical_battle.cpp` ~1431). Retail gives a BuildMDUItem MDU none of these between DEPLOY and `Build` (task-force
   member, off the free store; `MainForce_Unit_Damaged` skips `Default_Unit_Damaged` once `build_started`; no
   `Auto_Engage` without TARGETING). If none fired, the b0416 StopOrder drop did not run for the on direction.
5. `Build` comes 63 to 65 frames after the deploy's 0x29; an undeploy inside that window is a port difference, not a
   timing race to tune.

## Not determined

- The Planning phase relative to the 0x29 frame (S0 − N in 0..2).
- Who calls Walk vf +0x160 (0x61e440, also undeploys) and an exhaustive list of Walk vf +0x148 callers: no full scan of
  virtual call sites was run (MOVEMENT §3.2.1 lists the known ones).
- What 0x4160a0 not refusing a **deploying** (0x18) unit does for units that do have TARGETING (an auto block could
  re-plan and reverse the deploy) [M, not traced; not reachable for an MDU].
- The meanings of 0x39a350 ("busy", pack +0x100), obj +0x100 → +0x28 and the obj +0xf0 tests in 0x3a1ce0 [L].
- Not probed in game; everything above rests on the code reads.

## Port finding: mdu1 (2026-11-21) [H: measured in the port]

The MDU race of mdu1 (`AI_PLAN_EXPANSIONGENERIC_BUILDMDUITEM` "Underworld_Mobile_Defense_Unit is not a valid build
location", land Empire / Underworld) was an **ordering** difference, not a timing race. In the port, Q1 steps 2-3 (leave
every coordinator, drop the land attack block, adopt) were a queued StopOrder event, applied in the ability event pass
after the movement tick, while step 4 (Walk `Deploy(true)`, state 0x18) ran at once in the activation call. A Lua
`Activate_Ability("DEPLOY", true)` runs between battle ticks, so the next tick's attack-block service saw the MDU still in
its old attack formation: with the target hidden for the lead (FIGHTER_FLYING §4.11 C 3, d >= 40) it re-planned the
formation in the deploy's first frame; the plan's cap test (Q2, 0x3ac100) undeployed the MDU (0x18 -> 0x1A -> 0, signal
0x29 at once), `BlockOnCommand` returned and `Build` came 63 frames later on an undeployed MDU. Seed 4 on dev 074b9a3d,
frame 58403, MDU 7765 (attack block target 791, re-plan through `TacticalBattle::attack_block_service`).

Fix: DEPLOY's leave and adopt run inside the activation call, before Walk `Deploy`, as 0x3a1ce0 does them
(`AbilityHost::stop_hold_now` -> `TacticalBattle::oi_stop_hold`; the StopOrder event stays for hosts without the hook).
