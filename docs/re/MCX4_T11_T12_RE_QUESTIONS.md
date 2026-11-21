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
A12. (answered by REA: MCX4_T10_RE_ANSWERS §12; only confirm the operand order of the turn and which record
   +0x2e (`param_1[0x2e]`) is the redirect target.)
A13. Ability_Base `Terminate_Effect_On_Move_Command` (+0x42, 9): one type-based byte read each in 0x29f810 and 0x2b5ad0 may
   be a different field. Who reads the ability-data byte, and does a move order end the ability's effect?
A14. Arc_Sweep_Attack_Ability `Aggressiveness` (+0x0?, 7) and `Focus` (7). Note: the field table gives offsets 0x0 / 0x4,
   which look wrong; take the parser's offsets.
A15. - Battlefield_Modifier_Ability `Disable_Reinforcement_Vulnerability` (+0x194, 3): §16a (already in the answers
     file, REA) found the transport-unload gate `0x527f00(player)` = BitwiseAnd over the player's bool modifier map at
     player +0x2c8. Confirm this ability writes that map (apply / remove), which players it covers, and what the 9th
     argument of 0x29e960 and the 0x2a00b0 call mean (which arrivals get the vulnerability).
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

## B. t11 movement, formation, animation (new MCX4_T11_T12_RE_ANSWERS.md, already started: its §S lists the GC readers
found by an export scan and the GC rows with no reader, so B1, C1 and the no-reader part of B4 are settled)

B2. GameObjectType: `Formation_Priority` (+0x654, int, 195), `CanCellStack` (+0x3a, 88), `Attach_To_Flagship_During_Space_Battle`
   (+0x2c, 51), `Begin_Turn_Towards_Distance` (+0x3a8, 33), `Space_Full_Stop_Command` (+0x38, 29), `MaxFacingLookAheadFrames`
   (+0x3b8, 12), `Exit_Door_Angle_Degrees` (+0x1a8c, 9), `Exit_Door_Distance` (+0x1a90, 9), `LateralAcceleration` (+0x3bc, 1),
   `Snap_Movement_Orders_To_Center` (+0x8d, 1; MOVEMENT §6 says "a selected object", but its only FoC user is a prop:
   which objects are scanned, which radius).
B3. `Move_Action` (hero clash, `MoveActionClass` 0x5e9ce0 / 0x5e9ed0 / 0x5e9f30 / 0x5ea820): `Move_Speed` (+0xcc),
   `Move_Animation_Subindex` (+0xd4), `Move_Animation_Speedup` (+0xd8), `Move_Min_Distance` (+0xdc), `Move_Max_Distance`
   (+0xe0), `Move_Animation_Override` (+0xe5), `Wait_For_Finish_If_Target_Using_Jetpack` (+0xe6), `Ability_Cooldown_Time_In_Secs`
   (+0xe8), `Move_Type`, `Move_Animation_Type`. Plus `Eat_Attack_Ability.Needs_To_Rotate` (+0x1d4) and
   `Infection_Ability.Shoot_Anim`.
B4. GC readers known (§S table): say what each does, briefly: `Crouch_Move_Fire_Angle_Cutoff` (0x54d090),
   `GripperCombatGridSnapDistance` (0x405480), `IdleMovementFrames` (0x413100, 0x423070, 0x553f00),
   `SpaceLocomotorFacingLookaheadAcc` (0x645370), and the blend times (`IdleWalkBlendTime`, `CrouchIdleWalkBlendTime`,
   `MoveBlendTime`, `TeamMoveBlendTime`, `TeamCrouchMoveBlendTime`, `InfantryTurnBlendTime`: 0x61f510, 0x61f840, 0x620190,
   0x62bef0, 0x62ca10, 0x62cf90, 0x642740): is any of them a sim input (a frame count, a state or a timer game logic
   reads), or only the clip blend the renderer shows?

## C. t12 AI tuning constants (MCX4_T11_T12_RE_ANSWERS.md)

C1. (settled: the four AI_*Threat* factors have no reader, §S.) `AI_BuildTaskReservationSeconds`: its reader 0x695a10
   (A2J_RE_BUILD_MARKET step 1, the PBT +0x4c timer): what the timer gates (tactical too?).
C2. Saliency (`Saliency_Size`, `_Power`, `_X`, `_Y`, `_Health`, `_Targets`, `_Speed`): only reader 0x520df0, a min/max
   normalised score over a list of objects. Its callers (camera / cinematic, selection, AI target pick?) and the formula.
C3. GameConstants `Unit_Command_Rankings_By_Category` (offset from the GC parser): reader and effect.
C4. GameObjectType `Harass_Enemy_Exclusion_Range` (+0x204c), `Avoid_Enemy_Exclusion_Range` (+0x2050),
   `Ignore_For_Reoptimization` (offset from the type parser): readers (AI harass / avoid behaviour, the plan reoptimiser?).
