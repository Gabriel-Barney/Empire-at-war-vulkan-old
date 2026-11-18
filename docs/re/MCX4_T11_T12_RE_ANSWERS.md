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
| `Begin_Turn_Towards_Distance` (+0x3a8, float) | 0x622b80 (from 0x6236b0; in the walk-locomotor range 0x61f..0x624) | see the note below the table | not read | H read, M effect |
| `MaxFacingLookAheadFrames` (+0x3b8, float) | 0x645370 (space locomotor step, from 0x646800) | each step the facing lookahead (object +0xa8 pack +0x1e8) grows by GC `SpaceLocomotorFacingLookaheadAcc` and is capped at this value [M: a lookahead in frames; its own reader not traced] | 30.0 (qword store at 0x36758b, which also writes +0x3bc = 0) | H |
| `Exit_Door_Angle_Degrees` (+0x1a8c), `Exit_Door_Distance` (+0x1a90) | 0x2a00b0 (land placement of a carrier's contents), 0x2b3be0 (creation of a container's contents) | each unloaded unit is placed at carrier position + `Exit_Door_Distance` × (cos, sin)(carrier facing + `Exit_Door_Angle_Degrees`) and handed, with the angle − 45°, to the free-spot search 0x2a9670 [M: search radius 2500] | 0 (qword store of a zero register) [M] | H |
| `LateralAcceleration` (+0x3bc, float) | 0x63abe0 (locomotor step, 3 calls from 0x639e90) | the unit's sideways speed (pack +0x1fc) moves toward the wanted lateral speed by at most this much per step, then is clamped to ± pack +0xec / max speed (0x370f00). With the default 0 the sideways speed never changes [M] | 0 | H read, M class |
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
- **Defaults** [M: values written by the slot-0 reset 0x5e9bb0, not the allocating constructor]: `Move_Speed` 0 (no
  override), subindex 0xFFFF, speedup 0.0, min / max 0, every bool false.
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
| `SpaceLocomotorFacingLookaheadAcc` (1.0) | 0x645370 | added each step to the facing lookahead, capped by type `MaxFacingLookAheadFrames` (B2) | sim [M] | H |
| `IdleWalkBlendTime`, `CrouchIdleWalkBlendTime`, `MoveBlendTime`, `TeamMoveBlendTime`, `TeamCrouchMoveBlendTime`, `InfantryTurnBlendTime` | the §S readers | passed as the blend-seconds argument of Play_Animation 0x3a92f0 (§S rows; `MoveBlendTime` in 0x61f840 is the MOVE clip's blend). Per ANIM_PARTICLES (0x265560), the new clip starts at frame 0 at its full speed whatever the blend; the blend only sets the crossfade weight of the previous pose (1 − remaining / duration). So no frame count, clip type or timer that game logic reads depends on it | presentation [M: each of the 7 call sites passes it as the blend argument, checked only through §S; a bone-pose read during a fade (fire bones) is the only possible sim contact, L] | M |

## C1. `AI_BuildTaskReservationSeconds` (GC +0x1100, FoC 15.0) [H read; M effect]
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
  `Space_Full_Stop_Command`.
- C4: the rest of the avoid behaviour 0x58e8e0 (its move).
- B4: the caller of 0x405480 (`GripperCombatGridSnapDistance`).
