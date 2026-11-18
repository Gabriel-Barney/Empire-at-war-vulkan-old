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
