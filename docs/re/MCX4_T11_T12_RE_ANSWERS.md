# MCX4 t11 / t12: movement, animation and AI tags, RE answers (lane mcx4-tac2, 2026-11-18)

Answers to `MCX4_T11_T12_RE_QUESTIONS.md` parts B and C (part A goes to `MCX4_T10_RE_ANSWERS.md`). Retail FoC Steam x64
`StarWarsG.exe`; addresses are image-relative (minus 0x140000000) unless written in full.

## S. GameConstants reference scan [M: export scan]

**Method.** GameConstants is the static object at 0x140b15800 (`MCX4_T10_RE_ANSWERS.md` §16), so GC +X is the global
`DAT_<0x140b15800 + X>` in a decompilation. The full decompile export `re_reference/ghidra/StarWarsG_decompiled.zip`
(44 MB) was searched for every `DAT_` / `_DAT_` name of each asked field, and for the three dwords below it (a wider load
starting there would cover the field: none of those hits is a wide load, all are float / dword reads or casts).
Readers found (function containing the reference):

| GC field (+offset) | readers |
|---|---|
| `ApproximationSmoothCosAngle` (+0x14d4), `ApproximationForwardDistance` (+0x14d8) | 0x505fe0 |
| `FormationMinimumSideError` (+0x14cc), `FormationMaximumSideError` (+0x14d0) | 0x41dbb0, 0x42daf0 |
| `FinalFormationFacingMinimumAngle` (+0x14fc), `…DeltaCoefficient` (+0x1500) | 0x411980 |
| `WalkAnimationCutoff` (+0x1504) | 0x61f840 (WalkLocomotor clip choice: WALKMOVE below cutoff × max speed), 0x62ca10 |
| `IdleWalkBlendTime` (+0x1510) | 0x61f510, 0x62ca10 |
| `CrouchIdleWalkBlendTime` (+0x1514) | 0x62ca10 |
| `MoveBlendTime` (+0x1518) | 0x61f840 (the MOVE clip's blend), 0x62cf90 |
| `TeamMoveBlendTime` (+0x1520), `TeamCrouchMoveBlendTime` (+0x1524) | 0x62ca10 |
| `InfantryTurnBlendTime` (+0x1528) | 0x620190, 0x62bef0, 0x642740 |
| `MovementReevaluationFrameCount` (+0x152c) | 0x5d51e0 |
| `Crouch_Move_Fire_Angle_Cutoff` (+0x1534) | 0x54d090 (WeaponBehavior) |
| `Rotate_Formation_Facing_Moves` (+0x1550) | 0x439360 |
| `ShouldUseSpaceIdleMovement` (+0x1551) | 0x423070, 0x597dc0, 0x59c170, 0x59e700 |
| `IdleMovementFrames` (+0x155c) | 0x413100, 0x423070, 0x553f00 |
| `GripperCombatGridSnapDistance` (+0x15b8) | 0x405480 |
| `SpaceLocomotorFacingLookaheadAcc` (+0x144c) | 0x645370 |
| `SpaceStaticObstacleAvoidanceBonusDistance` (+0x1414) | 0x59a860, 0x59d4a0, 0x5d0740, 0x5d5130 |
| `LandObjectTrackingInterval` (+0x146c), `LandObjectTrackingTreeCount` (+0x1470) | 0x5d8140 |
| `ShouldSkipLandFormup` (+0x149c) | 0x41c1a0 |
| `ShouldInfantryTeamsSplitAcrossFormations` (+0x149d) | 0x4464c0 |
| `AI_BuildTaskReservationSeconds` (+0x1100) | 0x695a10 (A2J_RE_BUILD_MARKET §… step 1: the PBT +0x4c timer) |
| `Saliency_Size` / `_Power` / `_X` / `_Y` / `_Health` / `_Targets` / `_Speed` (+0x11864 .. +0x1187c) | 0x520df0 |

Correction to `ANIM_PARTICLES.md` §A.7: the "unknown .bss globals" 0x140b16d04 and 0x140b16d18 of the walk / move row are
GameConstants fields: 0x140b16d04 = GC +0x1504 `WalkAnimationCutoff` (FoC 0.6) and 0x140b16d18 = GC +0x1518
`MoveBlendTime` (FoC 0.5).

**No reference at all** (neither the field nor a wider load below it) — retail parses these and never reads them:

| GC field (+offset) | note |
|---|---|
| `MaxObstacleCostSpace` (+0x141c), `ObstacleAreaOverlapForMaxSpace` (+0x1420), `UseLinearCollisionChecks` (+0x1438), `MaxObstacleCostLand` (+0x1478), `MaxLandFormationFormupFrames` (+0x1460) | confirms MOVEMENT §3.9 |
| `CrouchMoveBlendTime` (+0x151c) | the crouched MOVE clip uses another blend |
| `SpaceIdlePathCullCoefficient` (+0x1558) | |
| `Default_Defense_Adjust` (+0x114) | confirms `MCX4_T10_RE_ANSWERS.md` §16 [M] |
| `AI_SpaceThreatDistanceFactor` (+0x10d8), `AI_LandThreatDistanceFactor` (+0x10dc), `AI_SpaceThreatTurnRateFactor` (+0x10e0), `AI_LandThreatTurnRateFactor` (+0x10e4) | the whole 16-byte block is unreferenced |

Caveat [M]: a read through a pointer to the GameConstants object plus a constant offset would not show as `DAT_` in the
export; no such access pattern was seen for the read fields above (all are absolute), so it is unlikely here.

### S2. Caveat checked (re-analyst, 2026-11-18) [H]
GameConstants methods do take the object as a pointer: about 70 sites load 0x140b15800 into RCX and call one of 37
methods (0x2c4370..0x2cb520). A pyghidra pass listed every register-based access at the §S offsets (plus +0x584..+0x58c,
+0x15e8, +0x117e8) inside those methods and their direct callees (65 functions in all).
- The only hits at the §S no-reader offsets are the constructor 0x2c91c0's default stores.
- The table above therefore stands as **no reader [H]** for these fields. Ctor defaults:

  | field | ctor default |
  |---|---|
  | `MaxObstacleCostSpace` | 5.0 |
  | `ObstacleAreaOverlapForMaxSpace` | 1.0 |
  | `UseLinearCollisionChecks` | true |
  | `MaxObstacleCostLand` | 15.0 |
  | `MaxLandFormationFormupFrames` | 240.0 |
  | `CrouchMoveBlendTime` | 0.75 |
  | `SpaceIdlePathCullCoefficient` | 1.5 |
  | `Default_Defense_Adjust` | 1.0 |
  | the four `AI_*Threat*Factor` | 1.0 |

- `Unit_Command_Rankings_By_Category` (+0x15e8) is the one field that this pass shows is read only through the pointer
  (§C3).

**Method for B and C.** ReVa was held by another session, so this batch used:
- REA (decompile, byte search);
- a pyghidra copy of the ReVa project with the displacement scan of `MCX4_T10_RE_ANSWERS.md` (script `scan_t11.py`, same
  logic as `mcx4_t10_dispscan2.py`; 5 141 hits; method note in that file's "Batch 2");
- the decompile export for string searches.

Tags: [H] read, [M] one step inferred, [L] inferred.

## B2. GameObjectType movement fields
| field (+offset) | reader(s) | effect | default | tag |
|---|---|---|---|---|
| `Formation_Priority` (+0x654, int) | `CombatantBehaviorClass` slot 33 (+0x108) 0x638710; `SquadronCombatantClass` / `CompanyCombatantClass` slot 33 0x6cbb80 | base: true when the owner's type value ≠ 0; squadron / company: return the value of the type held at combatant +0x50 [M: the group's type]. The 22 indirect +0x108 call sites (0x23ccc0, 0x2b6aa0, 0x30c3b0, 0x362c60, 0x387400, 0x3a6b80, 0x5662b0, 0x567110, 0x568e60, 0x62eeb0, 0x633ae0, 0x634810, 0x6fbb20) belong to several classes and were not separated, so **what the priority orders is not answered** [L]. The neighbour +0x650 (read in 0x2a00b0, 0x449f10, 0x4fbb10, 0x4ff100) is another field | 0 (ctor 0x367250 stores a zero register) [M] | H read, L effect |
| `CanCellStack` (+0x3a, bool) | **none found** | no type-tagged access at +0x3a, or at +0x38 / +0x30 for a wider load. All 51 byte accesses at +0x3a in the image are other classes: UI colour bytes in 0x3078c0, 0x309f10, 0x349230, 0x34b090; tables; the CRT. The type ctor / copy move the byte block with wide stores | wide store [M] | M |
| `Space_Full_Stop_Command` (+0x38, bool) | **none found among type reads** | none of the 244 accesses at +0x38 is type-tagged. Parameter-based byte readers not judged one by one: 0x24a870, 0x92940, 0x3c360, 0x67b310, 0x67b4c0, 0x67b730, 0x69aa80, 0x7293e0 | wide store [M] | L |
| `Attach_To_Flagship_During_Space_Battle` (+0x2c, bool) | 0x2a00b0 (battle arrival placement, from 0x2a1eb0) | see the note below the table | false [M] | H |
| `Begin_Turn_Towards_Distance` (+0x3a8, float) | 0x622b80 (from 0x6236b0 = `StarshipLocomotorBehaviorClass` vf6, mcx4 t11 [H: renames.csv / RTTI]) | see the note below the table. The class is the `STARSHIP_LOCOMOTOR`, which no FoC or EaW type lists (MOVEMENT §2.5 "legacy"); the 32 FoC users are `FIGHTER_LOCOMOTOR` fighters and `FLYING_LOCOMOTOR` birds, whose locomotors do not read it | not read | H read, M effect |
| `MaxFacingLookAheadFrames` (+0x3b8, float) | 0x645370 = the **LANDBOMBER** step (vf6 0x646800 is `LandBomberLocomotorBehaviorClass`, mcx4 t11 [H]) | each step the facing lookahead (pack +0x1e8, 0 at the pack ctor 0x553f00) grows by GC `SpaceLocomotorFacingLookaheadAcc` after the facing is set, capped at this value. The facing getter vf +0x1e8 0x645a70 looks up the position reserved for the object int(lookahead) frames from now (0x538830; no reservation then: the object's own, just-moved position) and, when it differs from the position, faces toward it (yaw / pitch of the direction, roll kept); otherwise the path orientation the step has just set stays [H]. FoC: bombing runs 30, Snowspeeder / birds 5 | 30.0 (qword store at 0x36758b, which also writes +0x3bc = 0) | H |
| `Exit_Door_Angle_Degrees` (+0x1a8c), `Exit_Door_Distance` (+0x1a90) | 0x2a00b0 (land placement of a carrier's contents), 0x2b3be0 (creation of a container's contents) | each unloaded unit is placed at carrier position + `Exit_Door_Distance` × (cos, sin)(carrier facing + `Exit_Door_Angle_Degrees`) and handed, with the angle − 45°, to the free-spot search 0x2a9670 [M: search radius 2500] | 0 (qword store of a zero register) [M] | H |
| `LateralAcceleration` (+0x3bc, float) | 0x63abe0 = the **BIKE** move step (0x639e90 = `BIKE_LOCOMOTOR` vf6, BKR2 Q2 (d)) | per move-step pass, after the turn: want = the left component (−sin yaw, cos yaw) of the steering vector 0x509810 of the path direction (0x639550, capped at length 1; 0 without a path); the sideways speed L (pack +0x1fc) moves toward want by at most this value, then is clamped to ± v / vmax (pack +0xec / 0x370f00); the travel direction is normalise((cos yaw, sin yaw) + L (−sin yaw, cos yaw)) (exact sqrt) [H]. With the default 0, L stays 0. FoC: `Scout_Trooper` 0.15 | 0 | H |
| `Snap_Movement_Orders_To_Center` (+0x8d, bool) | 0x2a7b80 (the land move-order builder, 37 callers) | see the note below the table | false [M] | H |

Notes on the longer rows:
- **`Attach_To_Flagship_During_Space_Battle`.** In space, for each unit of an arriving fleet entry whose type names no
  replacement (type +0x1b50):
  1. A unit whose type has the flag is not created with the fleet; it is queued. The exception is the placement value
     0x3fffff ("no flagship id"), where it is created normally.
  2. After the fleet is placed, if the flagship object and its container behaviour (0x390740) exist, the container's
     +0x30 is set to 1.
  3. Each queued unit is then created at the flagship's position, put inside the flagship (0x379d30) and keeps its id.
  4. Without that container the queued units are not created [M: the skip label].

  Without the flag a unit is created at the fleet position and added to the fleet carrier (behaviour 5, 0x4f33a0).
- **`Begin_Turn_Towards_Distance`.**
  1. When the height of the destination differs from the unit's by less than BTTD, or by more than 2 × BTTD, the
     destination height becomes type +0xdfc.
  2. When 0x623450 says the target is reached and both type +0x39c and 0x3724d0(type) are non-zero, the unit gets a slow
     / stop order (0x3abf80, one third of max speed).
  3. If the distance to the destination then exceeds BTTD, 0x623fe0 turns it toward the destination [M: so BTTD is the
     distance beyond which the unit turns toward its goal while slowing].
- **`Snap_Movement_Orders_To_Center`.** For a position move order (order kind 1) in land mode, it scans **every object in
  the game mode's object list** (mode +0x48, count +0x68), not only the selection. It takes the nearest object whose type
  has the flag and whose distance to the order point is ≤ 0x397780(object) [M: the object's selection / footprint
  radius], and the order point becomes that object's centre. MOVEMENT §6 should say "any object", not "a selected
  object".
  - mcx4 t11 [H]: the test is gated by order kind 1 (event +0x24) and mode vf +0xe0 = 1 (land). The distance is 3-D
    (the order point's z included). A later object replaces the pick only when strictly nearer (ties keep the first in
    list order). 0x397780 = Scale_Factor × the soft footprint radius 0x373b30 for an object that is not a land team
    container (BKT1_ATTACK_RE §1.3); FoC's only user `Prop_Good_Ground_Area` has no soft radius and no
    `Space_Obstacle_Radius`, so its model's larger half extent decides.
- **`Max_Distance_From_Spawner`** (+0x2134, int; mcx4 t11 read of the INDIGENOUS_UNIT wander service 0x621d80, the
  leash written by TRU1_C_RE §1.3) [H unless tagged]:
  1. Return at once when 0x39b000(behaviour) holds, or the unit is moving (0x39ade0), has a target (0x397a40) or is
     not in an idle clip (0x3956a0 → 0x27b5e0). A burning unit (+0x3a1 bit 2) first takes `Burning_Damage_Per_Second`
     / FPS (0x3a9e30 kind 7) and, still burning and moving, returns.
  2. With `Can_Indigenous_Unit_Stop` (type +0x84): r = synced float(0, 1); while the behaviour's +0x34 < now and
     r > 0.1, return (stay).
  3. No map rectangle (mode vf +0x1f8 all zero): return.
  4. Home (pack +0x28..+0x30, not (−1, −1, −1)) and leash D (pack +0x34) > 0: dx = synced float(−D, D), then
     dy = synced float(−√(D² − dx²), +√(D² − dx²)) [M: the sqrt argument], point = home + (dx, dy, 0). Else point =
     (synced float(cx − hx, cx + hx), synced float(cy − hy, cy + hy), 0) over the map rectangle.
  5. d = |point − position| (3-D). The step bounds are the .data constants (max, min) = (400, 50), or (150, 60) while
     burning (0x140a442c8 / cc, 0x140a442d0 / d4, read from the exe). If d > max or d < min: t = one more synced float
     [M: between the two bounds; the arguments were not kept by the decompiler], point = position + (point −
     position) × t / d.
  6. A group move there (0x6218f0).
  Not ported: the port has no INDIGENOUS_UNIT service (TRU1_C_RE §1.4).

**Round 3 (lane mcx4-tac2, 2026-11-18, ReVa scripts; method in `MCX4_T10_RE_ANSWERS.md` §1 Round 3).**
- **`Formation_Priority` (+0x654) [H]: never read, no effect in retail.**
  - The image has 5 accesses at +0x654: the ctor 0x367250, the copy 0x36c000, a store of −1 in 0x43fab0 (another
    class), and the two getters 0x638710 / 0x6cbb80.
  - There are 24 indirect calls through +0x108 (22 `call`, 2 `jmp`); 0x23c8a0 / 0x23c910 also call a function
    pointer kept at struct +0x108, which is not a vtable slot. No receiver is a combatant:
    - the targeting behaviour on itself. Slot 33 is 0x566b40 in both `TargetingBehaviorClass` (0x8b0d88) and
      `TeamTargetingBehaviorClass` (0x8afd90): 0x5662b0, 0x567110, 0x568e60, 0x62eeb0, 0x633ae0 and 0x634810 (from
      the two slot-6 services 0x633a30 / 0x62ee00), and 0x387400 through Get_Behavior(9);
    - a game mode: 0x397680 and 0x3a6b80 (object +0x2b8 → +0x20), and 0x362c60;
    - 0x2b6aa0, whose +0x108 result is used as an object pointer;
    - 0x30c3b0, a list entry whose +0xf8 returns a string;
    - `ForceHealingAbilityClass` on itself (0x6fbb20);
    - 0x23ccc0, a function-pointer field;
    - the Lua vcall thunk 0x5f95a4, bound as the method `Set_Targeting_Stickiness_Time_Threshold`.
  - So the value orders nothing. Port: parse and ignore.
- **`Space_Full_Stop_Command` (+0x38) [H for each reader judged; M for the whole]: no reader, no effect in retail.**
  The image has 35 byte reads at +0x38 off a non-stack register (`mcx4_r3_disp.py`). With each base traced through
  the decompiler, none is a GameObjectType:
  - **The 8 parameter-based readers.**
    - 0x24a870 is a one-line getter that eight `LuaMemberFunctionWrapper<…>` vtables share (COMDAT-folded, slot 17).
      It has no direct call, so no non-virtual type getter folded into it is called either.
    - 0x92940 is `LoadingThreadClass` slot 1.
    - 0x3c360 is called from the camera step 0x2ee40 [M: a camera member].
    - 0x67b310 / 0x67b4c0 / 0x67b730 copy byte 14 of argument 2, a path-query block of floats, to 0x140a443c8.
    - 0x69aa80 is `BaseBlockStatus` slot 19.
    - 0x7293e0 is the copy constructor of a 0x50-byte struct.
  - **The other 26 reads.**
    - UI colour bytes: 0x3078c0, 0x309f10, 0x349230, 0x34b090.
    - Object +0xb0 → +0x38 [M: the pack whose +8 is the selected flag, B0173]: 0x3912c0, 0x39b070, 0x3a0c20,
      0x3a6b30, 0x3a6b80, 0x57c3e0.
    - Behaviour owner +0x108 → +0x38: 0x4f5780, 0x4f6070.
    - Class members: 0x187b30, 0x57f7e0.
    - List and hash entries: 0x2ffcb0, 0x3552fc, 0x4dd530, 0x582b00, 0x72ac70.
    - The CRT: 0x77e6f8, 0x794b6c, 0x7990e4, 0x79d150.
  - **Wider loads.** The T11 type tag found none at +0x30 / +0x38 [M]. The ctor and copy move the block with wide
    stores. Port: parse and ignore.

## B3. Hero-clash `Move_Action` and the two ability tags
**`Move_Action` (`MoveActionClass`, vtable 0x8a8460, 0xf8 bytes).** It is picked and run like `Attack_Action`
(`MCX4_T10_RE_ANSWERS.md` §11) with a 6-frame service interval. Unreachable with shipped data (HERO_CLASH §4) [H unless
tagged].

Runtime bytes: +0xe7 finished, +0xe8 saved "locomotor suspended", +0xe9 waiting, +0xec last squared distance, +0xf0
restarts.

- **Destination, `Move_Type` 1** (0x5ea190).
  1. d = the distance to the target; r = U(`Move_Min_Distance` +0xdc, `Move_Max_Distance` +0xe0) from the synced LCG
     0x1ffbb0.
  2. Goal = actor + direction × (d − r): the actor stops r short of the target. If d − r ≤ 0 the action is finished.
  3. The point is adjusted (0x5e9c30) and tested (0x50eeb0), with up to 5 tries.
- **Destination, `Move_Type` 2** (0x5e9f30).
  1. Goal = actor + the direction × (d + r), rotated by U(−60°, +60°): the actor runs past the target with a random
     swing. The draws are r first, then the angle.
  2. The same test as type 1, plus a free-cell test (0x538200) when the mode provides one; up to 5 tries.
- **Start** (0x5ea910).
  1. With `Move_Animation_Override` (+0xe5): clear any earlier override, then set the move clip to
     `Move_Animation_Type` (+0xd0), `Move_Animation_Subindex` (+0xd4), `Move_Animation_Speedup` (+0xd8) (0x3a8e20).
  2. With `Move_Speed` (+0xcc) > 0: the actor's speed override := `Move_Speed` (0x3a8c90).
  3. Issue a move order (0x4f8200) and take the movement lock (0x3a8e50(actor, 1)).
- **Begin** (slot 5, 0x5e9ce0). `Move_Use_Jetpack` (+0xe4) swaps to the jetpack locomotor (MOVEMENT, the
  `MoveActionClass` rows).
- **`Wait_For_Finish_If_Target_Using_Jetpack` (+0xe6)** (0x5e9ed0). Before a start or restart, if the target is in a
  jetpack jump (0x39ae20), the actor waits facing the target until the target lands, then starts.
- **Update** (slot 7, 0x5eaab0).
  - Type 1: while the locomotor moves, the squared distance to the target is tracked. When it grows by more than 10 and
    fewer than 3 restarts have been made, the actor unlocks, clears both overrides and restarts with a new destination.
  - Type 2, and type 1 once the locomotor stops: the action is finished.
- **End** (slot 9, 0x5ea820). Clear the animation override (if `Move_Animation_Override`) and the speed override (if
  `Move_Speed` > 0), unlock, and swap the jetpack back.
- **Post-parse** (slot 12, 0x5eaca0). Subindex < 0 → 0xFFFF.
- **Defaults** (the constructor 0x5e9b50; lane mcx4-tac2, 2026-11-18) [H]: `Move_Type` 0, `Move_Speed` −1.0 (no
  override), animation type −1, subindex −1 (0xFFFF after the post-parse), speedup 1.0, min / max 0, every bool false.
  0x5e9bb0 is the scalar deleting destructor, so its values (speed 0, speedup 0) are not defaults.
- `Move_Type` is read as the int 1 / 2 at +0xc8. `Move_Animation_Type` is the clip type passed to 0x3a8e20. All asked
  `Move_Action` fields have readers.

**`Ability_Cooldown_Time_In_Secs` (+0xe8) is a `Special_Ability_Action` field** (`SpecialAbilityActionClass`, vtable
0x8cde98), not a `Move_Action` field.
- The post-parse 0x71f5f0 stores int(FPS × cooldown) at +0xec.
- The start 0x71f2e0 activates the actor's ability `Ability_Name` (+0xc8) on the target (0x37b750 on the actor's ability
  set).
- The update 0x71f5a0 runs every 3 frames, subtracts 3 from +0xec and ends the action when +0xec < 1.
- So the value is how long the clash keeps the actor busy with that action (FoC Boba Fett flamethrower 1.5 s), not the
  ability's own recharge [H].

**Details (lane mcx4-tac2, 2026-11-18, REA and the decompile export) [H unless tagged].**
- `Move_Type` names (0x5ea440): `Invalid` 0, `Move_To_Target` 1, `Move_Past_Target` 2, `Dodge_Shot_From_Target` 3.
  The begin handles only 1 and 2, so 0 and 3 fail to start (the clone is deleted).
- Begin 0x5e9ce0 in order: fails when 0x39ad40(actor) is true (a positive count at object +0x3a8, or the locomotor's
  vf +0x1d0, or the same for the team container) [M: a "busy" test]; +0xe8 := the locomotor's suspended state; with
  `Move_Use_Jetpack` the jet-pack swap; then the type's destination and start. On success a suspended locomotor is
  resumed (vf +0x208). On failure, and only with `Move_Use_Jetpack`, the swap is undone and the locomotor suspended
  again when it was.
- The waiting branch of 0x5e9ed0 counts as a successful start [M: the return value is the jump test's byte].
- 0x139800 normalises the direction in place and returns the length; the type 2 turn 0x2bab90 is x' = c·x − s·y,
  y' = c·y + s·x with the angle U(−1.0472, +1.0472) radians.
- Update type 1, moving: d² <= +0xec + 10 → +0xec := d²; else, with fewer than 3 restarts, +0xec := 0, unlock, both
  overrides cleared, 0x5e9ed0 again (its result ignored), restarts + 1; else finished. No locomotor → finished. While
  waiting: the target still jumping → face it; else waiting off, a destination (fail → finished) and the start (fail →
  finished).
- Start 0x5ea910 fails without the actor's coordinator (0x397260); the move order carries the goal and the default
  order fields.
- `Special_Ability_Action`: the frame count is 0x22f9c0(FPS × seconds), round half away from zero (FoC 1.5 s: 45 frames,
  so 15 services of 3 frames); the start needs the actor's ability set (+0x100); there is no end of its own.
- Port: `engine/tactical/combat/src/hero_clash.cpp` (`HeroClashManager` begin / service / end of the three classes),
  tests `engine/tactical/combat/tests/test_combat_clash_actions.cpp`; see `MCX4_T10_RE_ANSWERS.md` §11a.

**`Eat_Attack_Ability.Needs_To_Rotate` (+0x1d4): no reader [M].** The Eat class (0x6f7b10..0x6f8970, vtable 0x8c6ac0) has
no access at +0x1d4 at all. The 73 image-wide +0x1d4 hits are dword / float fields of other classes, plus one byte
compare in 0x4eea10, which is another class. Default not read [L].

**`Infection_Ability.Shoot_Anim` (+0x180): no reader [M].** The Infection class (0x6cc570..0x6cccc0, vtable 0x8bfc90)
has no access at +0x180 (scan: every +0x180 displacement). Its apply 0x6cc640 faces the target, stops and locks, and
plays no clip from this field. FoC: `BOMBTOSS`, 1 use.

## B4. GameConstants readers: what they do, sim or presentation
| field (FoC value) | reader | effect | sim? | tag |
|---|---|---|---|---|
| `Crouch_Move_Fire_Angle_Cutoff` (20) | `WeaponBehaviorClass` fire check 0x54d090 | when the unit is in current clip type 0x43 [M: the crouched-move clip] and has a fire target, the yaw difference between its facing and the target direction is computed (0x20b100). When \|difference\| > cutoff (degrees) the weapon does not fire on that check | sim | H read, M state |
| `GripperCombatGridSnapDistance` (35) | 0x405480(behaviour) | true when the distance between two points of the owner's team container (object +0x110: +0xa4..+0xac and +0xb0..+0xb8) exceeds the value; caller not traced [L] | sim [M] | H |
| `IdleMovementFrames` (30) | 0x553f00, 0x413100, 0x423070 | 0x553f00 (a locomotor data-pack ctor) stores int(value) at pack +0x2ac, the idle timer start. 0x413100 returns int(factor × value) when its member list is empty, else uses the first member's pack +0x2ac. 0x423070 reads it next to `ShouldUseSpaceIdleMovement` (not read) [M: frames between space idle-drift moves] | sim | H |
| `SpaceLocomotorFacingLookaheadAcc` (1.0) | 0x645370 (LANDBOMBER, B2) | added each step to the facing lookahead, capped by type `MaxFacingLookAheadFrames` (B2) | sim [M] | H |
| `WalkAnimationCutoff` (0.6) | 0x61f840 | WALKMOVE (0x42) is chosen while speed < cutoff × max speed (or when forced), else MOVE (2) | presentation (clip choice) | H |
| `FinalFormationFacingMinimumAngle` (10), `FinalFormationFacingDeltaCoefficient` (0.5) | 0x411980 (from the layout 0x40eb90; F = FormationClass, offsets below are FormationClass ones) | see "Final facing" below | sim | H |
| `Rotate_Formation_Facing_Moves` (true) | 0x439360 (destination check) | with an ordered facing (block +0x5c, angle +0x44): the final direction of the check, from which the signed angle φ to the first path segment is taken, is (cos f, sin f) when this is set, else the path's last segment. The rectangle axis is the facing either way | sim | H |
| `MovementReevaluationFrameCount` (10) | 0x5d51e0 (space coordinator vf5) | runs only when coordinator id (+0xac) % N = frame % N, frame ≥ +0xb0 and the coordinator state +0x64 = 3: (re)binds the attack target (0x394610 → 0x420930 per formation), splits the formations by role (0x397900: 1, 2, 4, 0x20) and hands each role list to 0x5d55f0, which re-plans the role group (0x5d4730) when a member's goal block no longer holds (target range / 0x5672b0 / 0x568340 tests) [M: those tests not read] | sim | H gate, M rest |
| `LandObjectTrackingInterval` (20), `LandObjectTrackingTreeCount` (25) | 0x5d8140 (tracking layer rebuild) | in land mode (vf +0xe0 = 1) a ring layer gets slice length = Interval frames and Count trees (a static / current layer, +0x30 / +0x31: one tree); space uses `SpaceObjectTrackingInterval` / `…TreeCount` (+0x1464 / +0x1468) the same way. Slice k covers [base + Interval × k, + Interval) (0x6b4810) | sim | H |
| `ShouldSkipLandFormup` (true) | 0x41c1a0 (plan, member loop) | set (and F +0x9c clear) → every member's record is started at once (+0x6d := 1 when 0x410170). Clear → a member of a formation with more than one member gets a form-up path to its form-up point (pack +0x120, the layout box at the path start) first [M: what starts the formation path afterwards is not read] | sim | H gate |
| `ShouldUseSpaceIdleMovement` (false), `IdleMovementFrames` (30) | 0x423070 (formation state 3), 0x420a40, 0x413100, 0x59c170 / 0x597dc0 / 0x59e700 (SimpleSpace), 0x553f00 | space idle drift: in space with the flag, an arrived one-block formation whose lead is idle issues a drift move (0x420a40) after int(IdleMovementFrames + synced float(0, IdleMovementFrames / 2) + 0.5) frames to lead + facing × 0x413100's distance, inside the map less 300; the SimpleSpace speed getters return `SpaceIdleMovementSpeed` (GC +0x1554) as the minimum speed instead of 0. Without the flag none of it runs; IdleMovementFrames is still the pack's idle-timer start (+0x2ac, ctor) and 0x413100's fallback | sim | H read, M meaning |
| `ApproximationSmoothCosAngle` (0.9962) | 0x505fe0 | (1) the approximation's per-step turn is at least acos(value) × (180/π) / 2 degrees; (2) after the simulation, the points are thinned: from each kept point the next point whose direction from it bends from the first step's direction by dot < value is kept (the one before it is emitted); the last point is always kept. So straight runs keep only their ends | sim | H |
| `GripperCombatGridSnapDistance` (35) | 0x405480 ← 0x306d00 ← 0x4fadb0 | 0x4fadb0 walks a grid of cells (0x48-byte entries) and 0x306d00 shows / hides objects (vf +0x110) by 0x405480's answer [L: a display grid, presentation] | not settled | M |
| `IdleWalkBlendTime`, `CrouchIdleWalkBlendTime`, `MoveBlendTime`, `TeamMoveBlendTime`, `TeamCrouchMoveBlendTime`, `InfantryTurnBlendTime` | the §S readers | passed as the blend-seconds argument of Play_Animation 0x3a92f0 (§S rows; `MoveBlendTime` in 0x61f840 is the MOVE clip's blend). Per ANIM_PARTICLES (0x265560), the new clip starts at frame 0 at its full speed whatever the blend; the blend only sets the crossfade weight of the previous pose (1 − remaining / duration). So no frame count, clip type or timer that game logic reads depends on it | presentation [each call site passes it as the blend argument: read through in "Blend times" below, H; a bone-pose read during a fade (fire bones) is the only possible sim contact, L] | M |

**Blend times (lane mcx4-tac2, REA and the decompile export, 2026-11-18) [H unless tagged].** FoC values from
`GAMECONSTANTS.XML` (EaW the same): `IdleWalkBlendTime` 0.5, `CrouchIdleWalkBlendTime` 0.5, `MoveBlendTime` 0.5,
`TeamMoveBlendTime` 0.2, `TeamCrouchMoveBlendTime` 0.5, `InfantryTurnBlendTime` 0.2. Clip classes used below:
idle-like 0x27b5e0 = {0, 1, 71, 77-82}; turn-like 0x27b670 = {3, 4, 21-24, 72, 73}; crouch 0x27b570 = {67, 71, 72, 73}.
- **WalkLocomotor (`WalkLocomotorBehaviorClass`).**
  - Move clip 0x61f840 (state 0x13): every clip it plays (MOVE, WALKMOVE, CROUCHMOVE, FIRE_MOVE, the idle at speed 0)
    blends `MoveBlendTime`, with two exceptions that blend 0: a `Walk_Transition` unit (type +0x35; its MOVE starts at
    the carried frame), and the walking-deployed clip 115 (pack +0x2b3). MOVE ↔ WALKMOVE keeps the phase.
  - End Move 0x61f510 (state 0x14, no `Walk_Transition`): let a = the locomotor's vf +0x2e0, the per-frame
    deceleration [M: the state leaves for 0x15 once the speed (pack +0xec) is ≤ a, MOVEMENT §3.5]. When 0 < a and
    (speed / a) × 0.03333 (the seconds left until the unit stops) < the .bss global 0x140b39908, the current clip is not idle-like, the unit does not burn (+0x3a1 bit 2) and pack
    +0x2b3 is clear, the default idle (0x395920; loop = type +0x30 `Loop_Idle_Anim_00`) plays with blend
    `IdleWalkBlendTime`. State 0x15 (0x61fc30) plays the same idle with blend 0x140b39908 when the clip is still not
    idle-like. Which of the two fires first depends on the unknown 0x140b39908 [M].
  - Turn 0x620190 (state 0x10, no `Has_Pre_Turn_Anim`, model has TURNL): TURNL (3) when the wrapped error ≥ 0, else
    TURNR (4); random variant (0xFFFF), one-shot, speed 1, blend = min(D / rate × 0.03333, `InfantryTurnBlendTime`).
    rate = pack +0xe8 when > 0, else the type's turn rate (0x61d520). D = |wanted yaw − facing yaw| with the
    difference wrapped into [0, 360) by 0x20b100, **not** into (−180, 180]: a right turn of e degrees counts 360 − |e|
    and so nearly always takes the cap [M: read from the decompiler; 0x20b6d0 is applied only for the sign].
- **`LandTeamInfantryLocomotorBehaviorClass` (vf6 0x62ca10, the squad members).** The clip choice 0x62c420 (called by
  0x62cf90 in states 0x13 / 0x14) returns the wanted clip and its blend; 0x62cf90's preset `MoveBlendTime` is always
  overwritten, so it never reaches a play.
  - Wanted clip: the default idle at speed 0 without a WALKMOVE clip, or in state 0x14 when (speed / a) × 0.03333 <
    `IdleWalkBlendTime` (a as above: the idle starts that many seconds before the member stops, so the blend ends as
    it stands), or in recovery 0x2A when the distance to its slot < v² / (2a) (its braking distance); else CROUCHMOVE when crouched, WALKMOVE below `WalkAnimationCutoff` × max speed, else MOVE.
  - From an idle- or turn-like current clip: blend `TeamMoveBlendTime`, or `TeamCrouchMoveBlendTime` when the new clip
    is a crouch clip; random variant; start frame = synced Get_Int(0, count − 1).
  - Else into an idle-like clip: blend `IdleWalkBlendTime`, or `CrouchIdleWalkBlendTime` into a crouch clip (Idle
    Crouch 71); variant 0, frame 0.
  - Else (move → move, or from another clip): blend 0.2 (.data 0x140a44300), or 0.4 (0x140a44308) when exactly one of
    the two clips is CROUCHMOVE (67); MOVE ↔ WALKMOVE keeps the variant; frame = old frame / old count × new count.
  - State 0 (team stopped) with a member still in MOVE / WALKMOVE / CROUCHMOVE: the default idle, random variant,
    blend `TeamMoveBlendTime`.
  - Turn 0x62bef0 (states 0x10 and recovery 0x29), only when the current clip is not turn-like: TURNL / TURNR, or
    CROUCHTURNL / R (72 / 73) at speed 0.5 (.data 0x140a44304) when crouched; random variant; **loop**; blend =
    min(D / rate × 0.03333, `InfantryTurnBlendTime`), D as above, rate = the type's turn rate × `Fleeing_Infantry_Speed_Bonus`
    while fleeing.
- **`JetPackLocomotorBehaviorClass` 0x642740** (take-off turn 0x10): TURNL / TURNR with the same blend rule (rate
  0x640af0).
- **What the blend does.** Each value is only the blend-seconds argument of 0x3a92f0, i.e. the crossfade weight of the
  previous pose (§B4 table row). No game-logic read depends on it.
- **Port** (mcx4 tac2): `anim::AnimTunables` carries the six values; `BattleCatalog::locomotor_blends()` reads them and
  `TacticalBattle::set_animation` hands them to `SimAnimation::set_tunables`. `AnimBehaviors::service_locomotor` uses
  them per the rules above (`LocomotorInput::team_member` = a Default-locomotor member of a land team container);
  `infantry_turn_blend` is the turn rule; `MovementConstants::infantry_turn_blend_time` caps the Walk arrival turn.
  Tests: `engine/anim/tests/test_anim.cpp`. [port] choices:
  - The idle starts when the speed reaches 0, not the retail lead time before arrival [port M].
  - The host does not pass the turn error / rate of a stationary turn outside the Walk arrival turn, so that turn
    blends the cap [port M].
  - Variant 0 instead of the random variant for the turn clips, and the crouch turn at speed 1 instead of 0.5 and
    one-shot instead of looped for team members: unchanged (they would add draws or change frame timing).
  - The walker's idle at speed 0 inside state 0x13 (blend `MoveBlendTime` in retail) is the port's stop path
    (`IdleWalkBlendTime`).

**Final facing** (mcx4 t11, 0x411980 / 0x421cc0 / 0x559350 / 0x41c1a0 [H unless tagged]; port:
`final_formation_facing`, `LaneShape::lateral_at`, `lane_end`):
1. 0x411980 clears F +0x248 / +0x24c / +0x250. It runs only when the formation path has ≥ 2 points and the top block has a
   facing (+0x5c; angle +0x44 in degrees).
2. ψ = the facing (wrapped into (−180, 180]) minus the yaw of the path's last segment, wrapped into [0, 360) (0x20b100),
   then into (−180, 180] (0x20b6d0). |ψ| < `FinalFormationFacingMinimumAngle` → nothing more (all 0).
3. F +0x24c := ψ in radians (the full rotation). R = √2 × (0x41a740 × 0.5 × 20) (0x41a740 = the layout half-width in
   cells). L = the path length (3-D), v = 0x41b2a0 (the slowest member's speed). When L ≠ 0 and v > 0:
   budget = ((`FinalFormationFacingDeltaCoefficient` × v) / R) × (L / v); budget < |ψ| → F +0x248 := sign(ψ) × (|ψ| −
   budget) (applied at once), else 0; F +0x250 := ((F +0x24c − F +0x248) / L) × 1.2 (radians per unit of arc).
4. Each frame 0x41dbb0 → 0x421cc0 sets the member's pack +0x150 := F +0x250 × s + F +0x248, never past F +0x24c (min for a
   rate ≥ 0, max below), s = the member's arc position on the path (to its waypoint plus its projection on the segment).
5. 0x559350 turns the member's slot offset (depth, lateral) by pack +0x150 before the slot getters 0x5566a0 (the lane
   term's lateral target) and 0x556710 (the end shift's L'_m) return it: lateral' = sin r × depth + cos r × lateral.
6. The plan's end shift (0x41c1a0) is +0x68 = cos(F +0x24c) × D − sin(F +0x24c) × L (confirms MOVEMENT §4.3.3 item 2).
   With a facing at least 10° off the last segment, the slot pattern therefore ends turned by ψ: its front faces the
   ordered facing. (Corrects MOVEMENT §4.3.3 item 4's [M]: the lateral target is the rotated slot lateral.)

**Round 3: `GripperCombatGridSnapDistance` (lane mcx4-tac2, 2026-11-18, ReVa) [H unless tagged]: presentation only,
the placement of the space HUD's per-squadron elements [L: the fighter "gripper" icons]. This replaces both B4 rows.**
1. **Call chain.** 0x405480 has one caller, the command bar (HUD 0x140b27f60) method 0x306d00. That method has one
   caller, 0x4fadb0. `SpaceModeClass` slot 23 (0x4d9110) calls 0x4fadb0 on each pass, with the fighter cell-grid
   singleton 0x404950 (FIGHTER_FLYING §5).
2. **Per cell (0x4fadb0).** It walks the dogfight grid G_D. For each cell that holds objects it computes the cell
   centre: rows are staggered, as in `centre()`, and z is the first object's type `Layer_Z_Adjust` (+0xdfc). Then it
   calls 0x306d00(HUD, cell, centre).
3. **Layout (0x306d00).** Skipped while HUD +0x2699 is set.
   - n = the cell's count, columns = ceil(√n). The centre is projected to the screen. Slots are 30 px apart, centred
     on that point (the first x is the centre's x − min(n, columns) × 30 / 2). After `columns` slots, x restarts
     and y moves by −30 in the HUD's coordinates.
   - Objects are taken in cell-list order. An object counts only when it has an entry in the HUD's map at
     +0x1ff8 (two HUD elements) and has behaviour 0x16; only those use up a slot.
   - The slot's pixel point, shifted in y by 0.048 × the screen height, is unprojected to a ray, and the ray is
     cut with the plane z = the object's z (object +0x80). The point is stored in the team data (object +0x110)
     +0xa4..+0xac.
4. **The test.** 0x405480(behaviour 0x16) = |the new point − team +0xb0..+0xb8| > the value (35).
   - Not beyond 35, or team +0xbd set: both HUD elements move to the slot's screen point (0x352650), and the first
     gets vf +0x110(1, 0, 0) [M: show]. Then team +0xbd := 1 and +0xb0..+0xb8 := the new point.
   - Beyond 35 with +0xbd clear: the elements stay where they were.
5. **Who else touches these fields.** Team +0xbd is cleared only by `FighterLocomotorBehaviorClass` slot 6 (0x5cb830),
   when the container's state (0x4ec820 → +0xa8 → +0x48) is not 0x1e [M: the state's meaning]. The sim writers
   0x2a5a60, 0x3abb80, 0x3c0020, 0x3c0800 and 0x5aec70 reset +0xa4 and +0xb0 to the team position; 0x5cd8e0 resets
   +0xa4 alone.
6. **Readers.** A scan of every read of team +0xa4 / +0xb0 / +0xbd whose base was loaded from +0x110 within 16
   instructions finds only 0x405480 and 0x306d00 [M: a reader that holds the team pointer longer is not covered].
   So no rule reads the result: presentation. The first B4 row's "sim [M]" is wrong.
 (GC +0x1100, FoC 15.0) [H read; M effect]
- **Timer start.** 0x695a10 starts the build task's reservation timer at PBT +0x4c: start = now, length = FPS × value.
  Its only caller is the galactic build market 0x53cd60, step 3 (A2J_RE_BUILD_MARKET), when a planet queue claims the
  task.
- **The test.** 0x695780 = 0x4ad350(PBT +0x4c) is the timer test in market step 2 (0x53cd60). A task held by a queue
  stays only while it passes the path test 0x6955a0, 0x695780 is false and the queue time ≤ the task's time.
- **Release.** Otherwise the task is released (0x695a30: planet = none, bid = 1e18), appended to the back of the market
  vector and unlinked from the queue.
- **Effect.** A claimed task that has not started within 15 s goes back to the market, where another planet queue can
  claim it [M: which return value of 0x4ad350 means "expired"].
- **Scope.** Galactic only: the market belongs to the planets' production behaviour. Nothing in tactical reads it.

## C2. Saliency weights [H for the caller; M for the feature names]
- **Callers.** 0x520df0 is called only by 0x2cd0d0, whose only caller is 0x2cd930. 0x2cd930 prints
  "Start_Interesting_Object_Cinematic: Type %s" and is called from 0x2cc080, 0x2cca80, 0x2cda20 and 0x4aaf00, the
  interest / cinematic camera of HERO_CLASH §3.10.
- **Formula.**
  1. For each candidate object, 0x520df0 collects seven features: size (squared extent of the model box), power
     (0x395d10), x, y, health, targets (an entry count from a map, M: objects targeting it) and speed (distance moved
     since the last sample).
  2. Each feature is normalised to [0, 1] by its minimum and maximum over the candidates.
  3. Score = Σ weight × normalised feature, with the weights `Saliency_Size`, `_Power`, `_X`, `_Y`, `_Health`,
     `_Targets`, `_Speed` (GC +0x11864..+0x1187c).
  4. The highest score wins.
- **Not a sim input.** Only the camera uses it.

## C3. `Unit_Command_Rankings_By_Category` (GC +0x15e8) [H unless tagged]
- **Value.** A list of category names. FoC: Hero, Super, Capital, Frigate, Corvette, Transport, Bomber, Fighter, Air,
  Vehicle, Infantry, Structure. It has no absolute reference (so §S could not see it).
- **Conversion.** The GameConstants method 0x2c79d0 (called once at start-up from 0x4b250, after TheGameConstants is
  initialised) turns the names into category masks in a vector at GC +0x1600.
- **Lookups.** 0x2c7bb0 returns the first rank that matches a mask, 0x2c8480 the last.
- **Users.**
  - 0x37a4c0 / 0x37a5c0: the best-ranked member of a container.
  - 0x4327c0 / 0x432a30: a team / formation.
  - 0x4ed640 / 0x4ed740.
  - 0x38290 / 0x38470: the best-ranked object of the selection.
- **Effect.** Every caller of 0x38290 plays an SFX event on the object it returns, through the sound manager 0x2d5320:
  0x33b30, 0x3a310 (the "drag_select" path), 0x3c100, 0x44280, 0x44590 and others. So the ranking chooses which unit of
  a multi-selection speaks the unit response.
- **Not a sim input** [M: the 0x4327c0 / 0x4ed640 users were not read through].

## C4. AI exclusion ranges and `Ignore_For_Reoptimization`
- **`Harass_Enemy_Exclusion_Range` (+0x204c)** [H]. Reader: `HarassBehaviorClass` (vtable 0x8a29b8) slot 6, 0x58dd10.
  - With 0 the behaviour does nothing.
  - Otherwise it searches a square of half-size R around the unit (grid query 0x2ab640) for enemies, takes the one with
    the largest 0x370e60 value of its type [M: its threat or attack range], and moves the unit to a point away from it
    (unit + (unit − enemy)).
  - The point is clamped to the map and snapped to a free cell (0x6552a0 / 0x655070), and the move goes through the
    formation / coordinator calls 0x4261d0, 0x424850, 0x41fd30.
  - FoC: GROUNDINDIGENOUS.XML, 4 uses.
- **`Avoid_Enemy_Exclusion_Range` (+0x2050)** [H read; M rest]. Reader: 0x58e8e0, slot 6 of the behaviour whose vtable
  holds it at 0x8a2b20 (class not named). Same range test and strongest-enemy pick, followed by a different move (mode
  slot +0x228, not read).
- **Defaults.** 0 for both (qword store of a zero register at +0x204c) [M].
- **`Ignore_For_Reoptimization` (+0x22d8 name, resolved type +0x22e0)** [H reads; M meaning]. Reader: the company
  re-optimiser 0x2b3e90, called at battle end from 0x28bf10 (BATTLE_TRANSITIONS) and from 0x2b76a0.
  - A company whose number of contained units differs from its member count (+0x960) is rebuilt.
  - A company type with this field set goes through 0x2aea10 instead:
    1. it takes the units of the named type out of the carriers (0x4f3fd0);
    2. it counts the company definition's members of other types;
    3. it creates that many units of the named type again and puts them back (0x29f810 + 0x4f33a0).
  - FoC: `Rebel_Artillery_Brigade` (MPTL ×2, MPTL_Spotter ×2) ignores MPTL_Spotter, so the spotters are made again, one
    per MPTL slot of the definition.
  - The other +0x22e0 hits (0x330450, 0x33e840, 0x33a580, 0x33c160) are another class's vector.

## Not reached (re-analyst batch)
- B2: what `Formation_Priority` orders (the receivers of combatant slot 33); the parameter-based +0x38 byte readers for
  `Space_Full_Stop_Command`. Answered in round 3 (B2): neither is read.
- C4: the rest of the avoid behaviour 0x58e8e0 (its move).
- B4: the caller of 0x405480 (`GripperCombatGridSnapDistance`). Answered in round 3 (B4): the HUD, presentation only.
