# MCX4 t10: tactical rules long tail, RE answers (lane mcx4-tac, 2026-11-18)

Answers to `MCX4_T10_RE_QUESTIONS.md`, one section per question number. Retail FoC Steam x64 `StarWarsG.exe`, Ghidra
12.1.4 through ReVa. Addresses are RVAs (Ghidra address − 0x140000000). [H] read in the decompiler / disassembly,
[M] read but one step inferred, [L] inferred. "No reader [H]" is written only where the scan below covered the field.

**Method (as MCX4_RE_ANSWERS_TACTICAL §10).** A Ghidra script (`C:\Users\Gabriel\ghidra_scripts\mcx4_t10_dispscan.py`,
second run `mcx4_t10_dispscan2.py`) walked all 1 970 469 instructions and listed every register-based memory operand
whose displacement is one of the asked offsets (type, HardPoint, ability, Faction offsets, plus the neighbouring offsets
a wider load could start at: +0x4f0 / +0x4f8, +0x1a10 / +0x1a18 / +0x1a20, +0x1ac0 / +0x1ad0, +0x2008, +0x23f0, +0x580,
+0x28c0, +0x2178). Each hit's base register was traced back (≤ 120 instructions, through register moves and stack
spills) and tagged "type" when it comes from obj +0x298 or the type getters 0x396380 / 0x3973b0; every hit at the
rarer offsets was then judged by hand. 7 884 hits in all. GameConstants is a static object at 0x140b15800 (check:
`Hardpoint_Recharge_Cutoff_For_Opportunity_Fire` +0x11848 = 0x140b27048, TACTICAL_GAPS), so its fields were checked by
absolute cross-references instead. The type constructor is 0x367250, the type copy (`Variant_Of_Existing_Type`)
0x36c000; the HardPoint data constructor is 0x53e3b0, its copy 0x472560; a hardpoint object holds its
`HardPointDataClass` at hardpoint +0x20.

**Type 0x1d fields are 16 bytes: the name, then the resolved type pointer at +8.** So `Conversion_Ability_Changes_To_Enemy`
(+0x1ac8) resolves to +0x1ad0 and `Respawn_Tied_To_Structure` (+0x2000) to **+0x2008**. MCX4_RE_ANSWERS_TACTICAL §9 /
§13 step 1 read type +0x2008 as a team count test: it is `Respawn_Tied_To_Structure` (§8 below).

## 1. `Is_Escort` (type +0xb8) [reader H, effect L]
- Only one byte read of +0xb8 has a type pointer as its base: **0x638b20, `CombatantBehaviorClass` vtable (0x8b21d0)
  slot 32 (+0x100)**. It returns true when the behaviour's owner object (behaviour +0x28) exists and its type has
  `Is_Escort`. The other byte accesses at +0xb8 are other classes (writers 0x1bfb50, 0x2040b0, 0x475660, 0x475f30,
  0x4764d0, 0x5d8d00, 0x6709a0; a struct copy 0x5226b0; readers 0x4767c0 (from 0x5d990), 0x50fcd0 (14 callers in
  0x50e0c0..0x50f480), 0x267c90 (`SceneClass` vf10), 0x2d5df0, 0x2d7a40, none on a type).
- Who calls slot +0x100 on a combatant behaviour was not settled: +0x100 is a common slot and the indirect-call
  search returns 60 sites across unrelated vtables (Issue_Auto_Target 0x38d730 calls +0x100 on another object type).
  So what escort status changes (formation, guard, targeting) is **not answered** [L].
- Ctor default: false (OBJECT_TYPES §4 table; the ctor clears the block with a wider store) [M].

## 2. `Immune_To_Damage` (type +0xa3) [H for Take_Damage; M for the scope]
- Reader: **`Take_Damage` 0x3a9e30** (at 0x3aa1db): after its first argument checks, a victim whose type has
  +0xa3 set **returns at once**: no health change, no shield absorb, no damage event (the shield absorb 0x549ac0 and
  the health setter are called further down) [H]. The next test in the same place is the object byte +0x3a7.
- No other byte read of +0xa3 is on a type: 0x3a9910 (5 callers) copies +0xa3 of a pointer reloaded from the stack
  after a call to 0x426140 into an argument [L: may be a type]; the three reads in 0x5d58f0
  (`SpaceMovementCoordinatorClass` vf12) use a coordinator record (R12), not a type.
- So hardpoints are covered only as far as their damage goes through the parent's `Take_Damage` [M]; the death path
  (`Die` 0x39bdb0, Kill 0x391310) does not read +0xa3 (no hit there), so a scripted or forced kill still kills [M].
- Ctor default: false (wider store) [M].

## 3. `Projectile_Appearance_Delay_Frames` [H for the hardpoint field; M for the type list]
- **HardPoint +0x4a4 (int).** Reader: `HardPointClass::Fire_At` 0x3825b0 (three reads; the hardpoint data at
  hardpoint +0x20). When it is > 0:
  1. the fire sound (hardpoint `Fire_SFXEvent` +0x240, else the owner type's `SFXEvent_Fire` +0x1240) is **not**
     played at the shot;
  2. the new projectile's pack gets +0x60 := current frame + delay and +0xb0 := that sound, and 0x39d6a0 is called on
     the projectile [M: it hides the model].
  The projectile service then holds the projectile still while the frame ≤ pack +0x60 and plays the pack sound on the
  frame equal to it (MCX4_RE_ANSWERS_TACTICAL §12). So the delay is a **launch delay**: the shot exists but does not
  move (or sound) for that many frames, then flies. It does not change the recharge. With 0 the sound plays at once.
  Ctor default 0 (no write at +0x4a4 in the HardPoint ctor, which clears the block [M]); copy 0x472560.
- **GameObjectType +0x1a08**: a vector of ints (code 0x12; data pointer +0x1a10, count +0x1a18). Its only reader is the
  getter 0x373610(type, i) (element i, 0 when out of range), whose only caller is `WeaponBehaviorClass` fire 0x54ade0
  (non-hardpoint weapons), with i = a fire-point index [M]. That the effect there is the same launch delay was not read
  [M]. Default: empty vector. (The +0x1a10 / +0x1a18 / +0x1a20 accesses in 0x2f93f0 are a different object.)

## 4. `Can_Contain_Heroes_During_Ground_Battle` (type +0x7d) [H for the reader; M for the branch]
- Reader: **0x2a00b0** (called from 0x2a1eb0; the land-battle placement of arriving units, which also calls
  `Apply_Elevated_Vulnerability`, §16), the only type-based read. For an arriving object U (a hero [M: the branch
  condition was not read]) it scans the candidate objects of the battle and keeps those that
  - have a type with +0x7d set,
  - belong to U's owner (obj +0x58),
  - are not inside a container themselves (obj +0xf8 = 0),
  and picks the one with the highest 0x3727a0 value (type `Tactical_Health` +0xdcc × the difficulty factor × the
  owner's and modifier factors). If one is found U is created inside it: 0x29f810 at that structure's position, then
  added to it (0x379d30); on success the new object takes U's id (+0x54) and the old U is removed (0x391310(·, 1)).
  When none is found or the add fails, U is placed the normal way (its TRANSPORT branch, behaviour 5, or a plain spawn).
- So the flag marks structures that heroes start the land battle **inside**; without one the hero spawns outside.
- Other +0x7d hits: 0x33fd40 reads its own object's +0x7c / +0x7d (another class), 0x432c80 / 0x435680 a record at
  +0xc0, stack frames and LEAs: none on a type.
- Ctor default: false (wider store) [M].

## 5. `Is_Force_Sensitive` (type +0x15) [M]
Type-based reads (all byte tests):
- **HUNT behaviour** (`HuntBehaviorClass` vf6 0x628b40 and its helper 0x627d40): 0x627d40 reads the hunter's own
  flag and, when it is set, collects the force-sensitive candidates into a separate list [M: that list is preferred];
  0x628b40, after 0x627d40 picks the destination, adds two synced random offsets (0x1ffbb0 on the game random
  0x140a13e24) to it **only when the hunted target is not force sensitive**, so force users are hunted to their exact
  position [M].
- **0x30fd00** (called from 0x2e32f0), twice: a hero filter that passes a type with `Is_Named_Hero` (+0x13) or
  `Is_Generic_Hero` (+0x14) or `Is_Force_Sensitive` (+0x15) under further conditions (0x3a9760) [M: purpose].
- Other +0x15 byte accesses (91) are writes after an allocation, small struct copies (0x341250, 0x341c40) and other
  classes. Ctor default false [M].

## 6. `Out_Of_Combat_Defense_Adjustment` (type +0xde0, float) [H for the readers; M for the formula]
- Readers: the fighter locomotor (vf6 0x5cb830) states **0x5cbea0, 0x5cd8e0 and 0x5ce010** (Moving 0x1B, FIGHTER_FLYING
  §4.1), all through the type getter 0x3973b0. Each, when the object has modifier sources (obj +0xf0), writes the value
  into the object's modifier list (+0xf0 → +0x138) with `ModifierList_Set_By_Source` 0x3fe810 (replace by source,
  so it does not stack with itself; ABILITIES_HARDPOINTS §6.3), with the object as source, the
  stacking key at 0x1408a618c and the duration constant at 0x140891814 [M: kind = defence, list 0 of TACTICAL_COMBAT's
  modifier table, used as (1 − r) on damage taken].
- So "out of combat" = the fighter is in one of those three locomotor states (flying its path, not attacking) [M]; when
  the modifier is removed on entering an attack state was not read [L].
- All other +0xde0 hits are qword fields of other classes (0x2exxxx..0x3exxxx) and the copy 0x36c000. Ctor default 0
  [M].

## 7. `Conversion_Ability_Changes_To_Enemy` (type +0x1ac8; resolved type +0x1ad0) [H reader, M effect]
- Readers of +0x1ad0: **`TacticalBribeAbilityClass` vf30 (apply) 0x5b07c0** (twice) and **0x4ca2f0** (called from
  0x4ca020; class not identified). In both, when the target's type has a conversion type that differs from its own
  type, the converted object uses that type [M]; the steps around it were not read.
- Default: none (null). The name half (+0x1ac8) is touched only by the ctor, copy, 0x36a070 and the parser 0x5b43d0.

## 8. Four single-use rows
- **`Respawn_Tied_To_Structure` (+0x2000; resolved +0x2008)** [H reader; M for the counts]. Reader: the hero respawn
  scheduler **0x48eb10** (and only it among type reads). Without the field, the scheduler needs `Is_Named_Hero`. With
  it, it lists the objects of the structure type (0x2a87d0 over the game mode's object registry) and counts those that
  qualify [M: D's owner, alive], then lists and counts the objects of D's own type the same way; the respawn is
  **refused unless there are more of the structure than of the unit** (structure count ≤ unit count → refused). So a
  structure-tied unit (FoC: 2 uses) respawns only while its owner keeps the structure, one per structure.
  MCX4_RE_ANSWERS_TACTICAL §9 / §13 step 1 call this "the type +0x2008 count test"; that is this tag. Other +0x2008
  accesses (0x2d9890, 0x2dac80, 0x301750, 0x3027d0, 0x312b90, 0x316050, 0x3fc750) are another class's field.
  Default: none.
- **`Projectile_Acceleration_Per_Frame` (+0x4fc, float): no reader [H].** The only accesses are the copy 0x36c000
  (dword read and write) and a store at 0x1d5aeb in 0x1d4c80, another class (it also writes +0x4f0 and reads +0x4a4
  through an unrelated pointer). No wider load starting at +0x4f0 / +0x4f8 covers it on a type (those reads are
  `Projectile_Fire_Pulse_Count` dword reads and a dword read of +0x4f8). The ctor sets it to **0.0** with a qword store
  at +0x4f8 (`Projectile_Redirect_Chance_Modifier` = 1.0, +0x4fc = 0). This confirms TACTICAL_COMBAT §3.3: the
  projectile step adds 0.0 (the speed clamp of MCX4_RE_ANSWERS_TACTICAL §12) and never reads an acceleration.
- **`Deployed_Max_Attack_Distance_Multiplier` (+0x23f8, float)** [H]. Reader: the maximum attack distance getter
  **0x3749d0**(type, object) (31 callers: targeting, AI, movement). Order: an active unit-ability record among the
  type's first two (+0x1ab0 list, active by 0x39b950) with a range > 0 wins; then a per-object entry (+0xc40 list
  indexed by obj +0x324); otherwise `Targeting_Max_Attack_Distance` (+0x500) × `Garrisoned_Max_Attack_Distance_Multiplier`
  (GameConstants +0x120ac) when the object is garrisoned (obj +0x3a7), else × this multiplier when deployed (0x39a390),
  else +0x500 alone. **Ctor default 1.0** (0x3691d8).
- **`Deployed_Turret_Elevate_Extent_Degrees` (+0x5d4, float)** [H]. Readers: 0x54c2f0 (weapon aim test, from
  0x54a270), 0x631a00 and 0x631210 (turret code, 0x631cc0 / 0x631fe0 / 0x632050 / 0x632270). Each uses
  `Turret_Elevate_Extent_Degrees` (+0x5a4) as the elevation limit and this value instead when the object is deployed
  (0x39a390), then refuses or clamps an elevation above it. **Ctor default 180.0** (0x367887).

## 9. HardPoint `Allow_Opportunity_Fire_When_Targeting` / `_When_Idle` defaults [H]
The HardPoint ctor 0x53e3b0 writes the dword 0x00010101 at +0x3d5: **`_When_Idle` (+0x3d5) = true,
`_When_Targeting` (+0x3d6) = true**, `Engine_Death_Hide_Engine_Particles` (+0x3d7) = true, `Allows_Special_Weapon_Use`
(+0x3d8) = false. Readers as TACTICAL_COMBAT §2: +0x3d5 in 0x385620 (idle test), +0x3d6 in 0x387400 and
Issue_Auto_Target 0x38d730; the copy 0x472560 copies both.

## 10. Weapon delay multipliers and the manual cooldown [H unless tagged]
- **W (recharge after the shot), `Fire_At` 0x3825b0:** for unit-ability slots 0 and 1 of the owner's type
  (0x374b50(type, k) gives the ability id m), when that ability is active (0x39b950(owner, m, 1)) W is multiplied by the
  hardpoint table entry **+0x250 + 4·m**; the order is W = [slot-0 entry] × (unit-ability `WEAPON_DELAY` multiplier,
  0x398010(owner, 1)) × [slot-1 entry]. FoC tags: `Defend_Mode_` m = 1 (+0x254), `Turbo_Mode_` m = 9 (+0x274),
  `Power_To_Weapons_Mode_` m = 0x1d (+0x2c4), `Full_Salvo_Weapon_Delay_Multiplier` m = 0x3e (+0x348). Then the
  TACTICAL_COMBAT §3.1 step 12 rule applies unchanged: on the pulse-delay branch W is applied always, on the full recharge only
  when W < 1. Defaults 1.0 (the ctor writes 1.0 at +0x250 [M: the rest of the table the same way]).
- **`Fire_When_In_Power_To_Weapons_Mode` (+0x3a1 = +0x384 + 0x1d)** is the per-mode gate of
  `Does_Weapon_State_Allow_Firing_At` 0x384850 step 4 (TACTICAL_COMBAT): a hardpoint with it set to No does not fire
  while the owner's POWER_TO_WEAPONS is active. Default true (ctor fill 0x01010101). Full salvo has no
  `Fire_When_In_` tag; its byte (+0x3c2) stays true.
- **`Manual_Hardpoint_Firing_Cooldown_Secs` (HardPoint +0x54, float):** reader 0x285d70, called by `Fire_At` when the
  hardpoint holds a player id at hardpoint +0xc8 (set by a manual target assignment [M]). It stamps that **player**
  (0x294bc0 = player by id): player +0x47c := the current frame, player +0x480 := int(FPS × cooldown); then `Fire_At`
  resets hardpoint +0xc8 to −1. So the cooldown is per player, starting at the manual shot. The reader of player
  +0x47c / +0x480 (the test that blocks the next manual assignment) was not traced [L]. Other +0x54 reads through a
  +0x20 pointer (0x1d3320, 0x49a1d0, 0x7ba280, 0x7c30c0) are other classes [M].

### 6a. Addendum (lane mcx4-tac2, 2026-11-18, REA decompile) [H]
- The fighter locomotor vf6 0x5cb830 dispatches state 0x1B to 0x5ce010, 0x1C to 0x5cd8e0, **0x1D and 0x1E both to
  0x5cc220** (Directed_Combat), 0x1F to 0x5cbea0.
- 0x5ce010 / 0x5cd8e0 / 0x5cbea0 call `ModifierList_Set_By_Source` with the value `Out_Of_Combat_Defense_Adjustment`;
  **0x5cc220 makes the same call with the value 0.0**. Same list, key (0x1408a618c), category pointer (0x1407ff8a8) and
  source (0x4ad4c0(object, −1)) in all four. The duration constant 0x140891814 is **−1.0** (permanent for this insert,
  ABILITIES_HARDPOINTS §6.3).
- So the entry exists from the first fighter-state frame on, holds the adjustment while the fighter is in Moving, Idle or
  Dead_Stop, and holds 0 while it is in Idle_Combat or Directed_Combat; other states (hyperspace, launch) leave it as it
  was. Port: `CombatSystem::set_out_of_combat`, called by `TacticalBattle::combat_sync` each tick.

### 16a. Apply_Elevated_Vulnerability callers (lane mcx4-tac2, 2026-11-18, REA decompile) [H unless tagged]
- 0x3a8550(object): land pair (`Elevated_Vulnerability_Duration` / `_Factor`, GC +0x1177c / +0x11780), space pair when
  the game mode's vf+0xe0 returns 2; with modifier sources (+0xf0) it **appends** (0x38c850, not by source) an entry
  named "Elevated_Vulnerability", value = factor, category 0, for the duration. FoC: land −2.0 for 3 s, space −3.0 for
  5 s, so an arriving unit takes 3× / 4× damage (defence list, 1 − r) for that time.
- **Transport unload step 0x644330** (TransportLanding): for each unloaded unit, applied only when the transport owner's
  player (mode vf+0x220(owner id)) is null or `0x527f00(player)` is true, i.e. the BitwiseAnd fold of the player's bool
  modifier map at player +0x2c8 is false [M: that map holds `Disable_Reinforcement_Vulnerability`]. A second call at
  the end of the same function (the single-object branch) is **not** gated.
- **0x2bf2d0** (space reinforcement / hyperspace arrival placement, AI_TACTICAL §… step 6): applied to the arriving
  object, or to every member when it is a team container; not gated.
- **0x29e960**: applied to the object created by 0x2b3be0 when its 9th argument (a bool) is set [M: the caller's
  meaning not read].

## 12. Redirect_Blaster `Max_Projectile_Redirection_Angle_In_Degrees` / `Turn_To_Face_Unblockable_Shots` (lane mcx4-tac2, REA decompile) [H unless tagged]
`RedirectBlasterAbilityClass` Take_Damage listener vf30 0x709c90 (ABILITIES_HARDPOINTS §6 "Redirect_Blaster"):
- **Arc test on yaws, not positions:** d = wrap(victim yaw (+0x8c) − projectile yaw (+0x8c)); the shot counts as
  "outside the reaction arc" when −h < d < h with h = (360 − `Reaction_Arc_In_Degrees` (+0x180)) / 2, i.e. the
  projectile flies the same way the victim faces (it comes from behind).
- **Outside the arc:** nothing is blocked. When **`Turn_To_Face_Unblockable_Shots` (+0x188)** is set and the victim's clip is
  idle-like (0x27b5e0), the victim's locomotor (behaviour 1, vf +0x218) gets a facing order to the arc edge overshot by
  1.01: yaw' = yaw ∓ (h − |d|) × 1.01 (sign of d) [M: the exact operand order of the decompiler's expression]; then
  "not handled" (the damage lands).
- **Inside the arc:** redirect roll (synced draw, < projectile `Projectile_Redirect_Chance_Modifier` (+0x4f8) ×
  `Redirect_Chance`) → `0x4cca10(projectile, victim, target T)` with T = the ability's redirect target record; else the
  block roll (< `Projectile_Block_Chance_Modifier` (+0x4f4) × `Block_Chance`) → `0x4cca10(projectile, victim, none,
  angle = Max_Projectile_Redirection_Angle_In_Degrees (+0x184))`.
- **0x4cca10 spawns a new shot** of the projectile's own type at the projectile's position, owned by the victim's player
  (0x29f810(type, victim owner, pos, orientation)): with a target T, aimed at T's position led by distance / (FPS ×
  speed); without one (block), **yaw = projectile yaw + 180 + U(±angle)** (0x532480 = a draw on the game LCG
  0x140a13e24, U(a, b) = a + (b − a) × r) and, when a second draw < 0.5, the pitch gets a U(±angle) offset too. The new
  shot's pack copies the old one's damage fields, takes speed 0x370f00, and has flags 0x10; categories 4 / 10 [M] get a
  point 300 ahead as their goal. Then the original shot is consumed (the listener returns "handled").
- So `Max_Projectile_Redirection_Angle_In_Degrees` is the spread of a **blocked** shot sent back at its shooter's side;
  a redirect aims at a target and ignores it. FoC: 52 entries.
- Port status (2026-11-18): `AbilitySystem::damage_listener` tests the arc on the source's position and only cancels the
  damage; it spawns no redirected / blocked shot. Roadmap task (mcx4) to implement this section.

## 16. GameConstants [H unless tagged] (absolute references to 0x140b15800 + offset)
- **`Elevated_Vulnerability_Duration` / `_Factor` (+0x1177c / +0x11780) and the `Space_` pair (+0x11784 / +0x11788):**
  only reader `Apply_Elevated_Vulnerability` 0x3a8550(object): takes the land pair, or the space pair when the game
  mode's vf+0xe0 returns 2; with modifier sources (obj +0xf0) it appends (0x38c850) a modifier named
  "Elevated_Vulnerability" with value = factor, stacking category 0, for the duration (TACTICAL_COMBAT's defence row).
  Callers: 0x29e960, 0x2a00b0 (land-battle arrival placement, §4), 0x2bf2d0 (twice), 0x644330 (twice) [M: these are
  the reinforcement / arrival paths].
- **`Depleted_Shield_Damage_Increment` (+0x11838):** only reader `Shield::Absorb` 0x549ac0: on a hit with damage > 0
  in the depleted branch, when int(FPS × increment) > 0 the shield's frame counter +0x108 is increased by
  int(FPS × increment) and clamped to ≥ 0 [M: +0x108 = the remaining `Depleted_Shield_Disable_Time` lockout, so each hit
  on a depleted shield extends the lockout by `increment` seconds].
- **`Override_Death_Persistence_Duration` (+0x11eb8):** only reader the death-clone spawner 0x29f270: under a flag
  (local condition not read [M]) the clone record's +0x30 (its persistence time) := this value.
- **`MaxCombatAccuracyAlignmentBonus` / `MaxCombatDamageAlignmentBonus` / `MaxCombatSensorRangeAlignmentBonus`
  (+0x584 / +0x588 / +0x58c): no reader [H].** No absolute reference to 0x140b15d84..0x140b15d8f (the neighbours +0x57c,
  +0x580, +0x590.. are read, so the reference table is populated there); the 14 register-based hits at these
  displacements are all writes or other classes' qword fields (GameConstants ctor 0x2c91c0 [M: identified by its
  stores], 0x3549a0 / 0x354a00, 0x3484d0, 0x351750, 0x2e7620, 0x300a00, 0x308890, 0x2de90). Default 0 [M].
- **`Default_Defense_Adjust` (+0x114): no reader [M].** No absolute reference to 0x140b15914 (+0x108, +0x10c, +0x118,
  +0x11c are read); the 225 register-based hits at +0x114 were not judged one by one.

## Not reached (time box)
- §11 `Attack_Action` (class, runner, eight fields).
- §12 Redirect_Blaster `Max_Projectile_Redirection_Angle_In_Degrees` / `Turn_To_Face_Unblockable_Shots` (ability
  +0x184 / +0x188; the scan lists the +0x184 / +0x188 hits in the ability code 0x6f0000..0x713000, not judged).
- §13 `Terminate_Effect_On_Move_Command` (+0x42; one type-based byte read each in 0x29f810 and 0x2b5ad0, probably a
  different field of the type table: not judged).
- §14 Arc_Sweep `Aggressiveness` / `Focus`; §15 Battlefield_Modifier `Disable_Reinforcement_Vulnerability` (likely the
  gate around `Apply_Elevated_Vulnerability`, §16 callers [L]), Tractor_Beam, Buzz_Droids, Hack, Personal_Flame_Thrower
  fields.
- §17 Faction `Land_` / `Space_Skirmish_Unit_Buy_Credits` (+0x28c4 / +0x28c8: no register-based dword access at
  +0x28c4 at all; the +0x28c0 / +0x28c8 qword hits are another class, so the reader goes through another path, not
  settled) and `Space_Forced_Retreat_Due_To_Superweapon` (+0x2180).
- §1 effect of `Is_Escort` (callers of `CombatantBehaviorClass` slot 32).

Ghidra comments added (shared project): 0x638b20, 0x3aa1db, 0x2a13cf, 0x48eb77, 0x373610, 0x382d68, 0x285d70, 0x3749d0,
0x3a8550 (prefix "mcx4 t10").
