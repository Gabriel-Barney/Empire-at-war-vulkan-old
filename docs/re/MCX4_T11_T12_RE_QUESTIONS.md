# MCX4 t10 (rest), t11, t12: RE questions (lane mcx4-tac2, 2026-11-18)

Roadmap group mcx4, tasks t10 (tactical rules, the part `MCX4_T10_RE_ANSWERS.md` did not reach), t11 (movement, formation
and animation) and t12 (AI tuning constants). Every row is a tier-1 tag-ledger row that base FoC sets and the port stores
without using (`python -I tools/lint/check_tag_ledger.py --gaps --group tactical|movement|ai`). Offsets are from
`xml_field_tables.md`; GameConstants is the static object at 0x140b15800 (so GC +X = 0x140b15800 + X, found by absolute
cross-references, as `MCX4_T10_RE_ANSWERS.md` §16). Method for type / ability / action fields: the displacement scan of
`MCX4_T10_RE_ANSWERS.md` (script `C:\Users\Gabriel\ghidra_scripts\mcx4_t10_dispscan.py`), base register traced to a type /
ability / action pointer.

Per row the answer gives: reader RVA(s), the effect in words (what changes, when, formula), the ctor default, a
confidence tag. **"No reader [H]" is wanted as much as a reader**: such rows go to the ledger's `RETAIL_UNUSED` table, so
say which scan covered the field. Answers:
- part A (t10 rest) is appended to `MCX4_T10_RE_ANSWERS.md` as §11-§15, §17-§19 (the ledger cites those sections);
- parts B and C go to a new `MCX4_T11_T12_RE_ANSWERS.md`, one section per question number.

Order: answer A, then C (small), then B; within a part, descending base uses. Stop where time runs out and list what was
not reached.

## A. t10 rest (append to MCX4_T10_RE_ANSWERS.md)

A11. **`Attack_Action`** (8 fields, 10-20 FoC uses each, plus `Attack_Animation_Type` 10): `Action_Object`, `Action_Target`,
   `Action_Relevance_Type`, `Action_Distance_From_Target_Curve`, `Action_Health_Percentage_Curve`, `Attack_Accuracy_Modifier`
   (+0xe8), `Attack_Number_Of_Shots` (+0xe4), `Attack_Projectile_Type`, `Attack_Animation_Type`. Which class parses the
   `Attack_Action` entries (hero clash `HERO_CLASH.md` has `MoveActionClass` vf5 0x5e9ce0 for `Move_Action`: is
   `Attack_Action` its sibling?), who runs it, and what each field does. Is any field parsed but never read?
A12. Redirect_Blaster `Max_Projectile_Redirection_Angle_In_Degrees` (ability +0x184, 52) and `Turn_To_Face_Unblockable_Shots`
   (+0x188, 52): the scan lists hits in 0x6f0000..0x713000 not yet judged. Reader and effect (the angle test against the
   incoming shot's direction? the turn?).
A13. Ability_Base `Terminate_Effect_On_Move_Command` (+0x42, 9): one type-based byte read each in 0x29f810 and 0x2b5ad0 may
   be a different field. Who reads the ability-data byte, and does a move order end the ability's effect?
A14. Arc_Sweep_Attack_Ability `Aggressiveness` (+0x0?, 7) and `Focus` (7). Note: the field table gives offsets 0x0 / 0x4,
   which look wrong; take the parser's offsets.
A15. - Battlefield_Modifier_Ability `Disable_Reinforcement_Vulnerability` (+0x194, 3): is it the gate around
     `Apply_Elevated_Vulnerability` 0x3a8550 (§16 callers 0x29e960, 0x2a00b0, 0x2bf2d0, 0x644330)? Which players' arrivals
     does it cover (owner, allies, everyone)?
   - Tractor_Beam_Attack_Ability `Damage_Application_Time_In_Secs` (+0x184, 3), `Inaccuracy_Radius` (+0x188, 1).
   - Buzz_Droids_Ability `Chase_Radius` (+0x188), `Own_Damage_Per_Second` (+0x18c), `Enemy_Damage_Per_Second` (+0x190), 2 each
     (ints).
   - Hack_Ability `Team_Member_With_Ability` (2; TACTICAL_GAPS names it).
   - Personal_Flame_Thrower_Ability `Damage_Delay_In_Secs` (+0x190, 2).
A17. Faction `Land_Skirmish_Unit_Buy_Credits` (+0x28c4) / `Space_Skirmish_Unit_Buy_Credits` (+0x28c8), 3 each: no
   register-based dword access at +0x28c4 was found, so look for a wider load, a loop over the faction's fields or an
   indirect path (the skirmish setup / buy screen, SKIRMISH_SETUP.md). `Space_Forced_Retreat_Due_To_Superweapon` (+0x2180, 2).
A18. Leftovers of §1-§16:
   - §1 `Is_Escort`: the callers of `CombatantBehaviorClass` slot 32 (+0x100, 0x638b20) and what escort status changes.
   - §3 the type vector `Projectile_Appearance_Delay_Frames` in `WeaponBehaviorClass` fire 0x54ade0: the same launch delay
     as the hardpoint field?
   - §4 the branch condition in 0x2a00b0 under which an arriving object is put inside a
     `Can_Contain_Heroes_During_Ground_Battle` structure (is it "is a hero": which flag?).
   - §6 when the `Out_Of_Combat_Defense_Adjustment` modifier is removed (entering an attack state? its duration constant
     at 0x140891814?), and the stacking key's list (defence?).
   - §10 who reads player +0x47c / +0x480 (the `Manual_Hardpoint_Firing_Cooldown_Secs` stamp): what is blocked until
     frame +0x47c + +0x480 (the next manual target assignment by that player? any hardpoint or the same?).
   - §16 the condition in 0x29f270 under which `Override_Death_Persistence_Duration` replaces the clone's persistence.
   - §16 `Default_Defense_Adjust` (GC +0x114): judge the register-based hits enough to say [H] no reader, or name it.
A19. GameConstants `Skirmish_Reinforcement_Delay_Frames` (+0x117e8; BKG1_RE_ANSWERS says it is not on the quick-reinforce
   path [M]): its reader and effect.

## B. t11 movement, formation, animation (new MCX4_T11_T12_RE_ANSWERS.md)

MOVEMENT.md already gives readers for ApproximationSmoothCosAngle / ApproximationForwardDistance, FinalFormationFacing*,
FormationMinimum / MaximumSideError, MovementReevaluationFrameCount, Rotate_Formation_Facing_Moves,
ShouldInfantryTeamsSplitAcrossFormations, ShouldSkipLandFormup, ShouldUseSpaceIdleMovement,
SpaceStaticObstacleAvoidanceBonusDistance, LandObjectTracking*, Snap_Movement_Orders_To_Center, Move_Use_Jetpack;
TRU1_C_RE gives Max_Distance_From_Spawner. Those need no answer.

B1. **MOVEMENT §3.9 "no runtime reader found"**: confirm [H] no reader (absolute GC references) for `MaxObstacleCostSpace`
   (+0x141c), `ObstacleAreaOverlapForMaxSpace` (+0x1420), `UseLinearCollisionChecks` (+0x1438), `MaxObstacleCostLand`
   (+0x1478), `MaxLandFormationFormupFrames` (+0x1460).
B2. GameObjectType: `Formation_Priority` (+0x654, int, 195), `CanCellStack` (+0x3a, 88), `Attach_To_Flagship_During_Space_Battle`
   (+0x2c, 51), `Begin_Turn_Towards_Distance` (+0x3a8, 33), `Space_Full_Stop_Command` (+0x38, 29), `MaxFacingLookAheadFrames`
   (+0x3b8, 12), `Exit_Door_Angle_Degrees` (+0x1a8c, 9), `Exit_Door_Distance` (+0x1a90, 9), `LateralAcceleration` (+0x3bc, 1).
B3. `Move_Action` (hero clash, `MoveActionClass` 0x5e9ce0 / 0x5e9ed0 / 0x5e9f30 / 0x5ea820): `Move_Speed` (+0xcc),
   `Move_Animation_Subindex` (+0xd4), `Move_Animation_Speedup` (+0xd8), `Move_Min_Distance` (+0xdc), `Move_Max_Distance`
   (+0xe0), `Move_Animation_Override` (+0xe5), `Wait_For_Finish_If_Target_Using_Jetpack` (+0xe6), `Ability_Cooldown_Time_In_Secs`
   (+0xe8), `Move_Type`, `Move_Animation_Type`. Plus `Eat_Attack_Ability.Needs_To_Rotate` (+0x1d4) and
   `Infection_Ability.Shoot_Anim`.
B4. GameConstants (absolute references): `CrouchIdleWalkBlendTime` (+0x1514), `CrouchMoveBlendTime` (+0x151c),
   `Crouch_Move_Fire_Angle_Cutoff` (+0x1534), `GripperCombatGridSnapDistance` (+0x15b8), `IdleMovementFrames` (+0x155c),
   `IdleWalkBlendTime` (+0x1510), `InfantryTurnBlendTime` (+0x1528), `MoveBlendTime` (+0x1518), `SpaceIdlePathCullCoefficient`
   (+0x1558), `SpaceLocomotorFacingLookaheadAcc` (+0x144c; MOVEMENT §2.7 "reader not traced"), `TeamCrouchMoveBlendTime`
   (+0x1524), `TeamMoveBlendTime` (+0x1520), `WalkAnimationCutoff` (+0x1504). For the blend times: is the value a sim input
   (it changes a frame count, a state or a timer that game logic reads) or only the animation blend the renderer shows?

## C. t12 AI tuning constants (MCX4_T11_T12_RE_ANSWERS.md)

C1. GameConstants: `AI_SpaceThreatDistanceFactor` (+0x10d8), `AI_LandThreatDistanceFactor` (+0x10dc),
   `AI_SpaceThreatTurnRateFactor` (+0x10e0), `AI_LandThreatTurnRateFactor` (+0x10e4), `AI_BuildTaskReservationSeconds`
   (+0x1100): reader and formula (threat of a unit vs its distance and turn rate? where the AI uses it).
C2. GameConstants `Saliency_Size` (+0x11864), `Saliency_Power` (+0x11868), `Saliency_X` (+0x1186c), `Saliency_Y` (+0x11870),
   `Saliency_Health` (+0x11874), `Saliency_Targets` (+0x11878), `Saliency_Speed` (+0x1187c): the saliency score (camera /
   cinematic or AI?), its formula and who reads it.
C3. GameConstants `Unit_Command_Rankings_By_Category` (offset from the GC parser): reader and effect.
C4. GameObjectType `Harass_Enemy_Exclusion_Range` (+0x204c), `Avoid_Enemy_Exclusion_Range` (+0x2050),
   `Ignore_For_Reoptimization` (offset from the type parser): readers (AI harass / avoid behaviour, the plan reoptimiser?).
