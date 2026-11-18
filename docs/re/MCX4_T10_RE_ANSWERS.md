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

## Batch 2 (re-analyst, 2026-11-18): §11, §13-§15, §17-§19 and extensions of §12 / §16a

Method. ReVa was held by another session, so this batch used REA (decompile, byte search) and a pyghidra script on a
read-only copy of the ReVa project (`scan_t11.py`, same logic as `mcx4_t10_dispscan2.py`: every register-based operand
at the asked displacements, base traced to obj +0x298 or the type getters 0x396380 / 0x3973b0; 5 141 hits, 48 s).
GameConstants fields were checked by absolute references plus a second pass over every function that receives the
GameConstants pointer (37 GameConstants methods 0x2c4370..0x2cb520, called with this = 0x140b15800 from about 70 LEA
sites, and their direct callees, 65 functions). No Ghidra comments were added this round (ReVa not reachable).

## 11. `Attack_Action` (hero-clash action) [H unless tagged]
- **Class.** `AttackActionClass` (vtable 0x8cde20, 13 slots, 0x130 bytes), a sibling of `MoveActionClass` (0x8a8460)
  and `SpecialAbilityActionClass` (0x8cde98) under `ActionClass`. The base holds the name (+0x8), `Action_Object`
  (+0x28, resolved type +0x30), `Action_Target` (+0x38, resolved +0x40), `Action_Relevance_Type` (+0x48),
  `Action_Relevance_Constant` (+0x4c) and the two curves (+0x50, +0x78). The shared parser 0x5e9910 (slot 1) feeds each
  key / value to the generic field-table parser 0x517c20 and then to the class's slot 11 (Attack 0x71eee0); an unknown
  key prints "Key-Value pair (%s, %s) is not recognized by this Action".
- **Runner.** Only `ActionDrivenHeroClashClass` (0x8bd978): pick 0x6aae80, update 0x6ab6f0. Unreachable with shipped
  data (HERO_CLASH §4, `Clash_Range` 0.0).
- **Pick** (0x6aae80 → base relevance 0x5e9300). An action applies only to an object whose type is `Action_Object`
  (pointer equality). Candidate targets are the clash's objects whose type is `Action_Target`. With no `Action_Target`
  there is no candidate, the relevance is 0 and the action is never chosen. The candidate with the highest relevance
  (slot 3) becomes the target. Attack's slot 3 (0x71eac0) first requires the target to be an enemy of the actor
  (0x39a450). The relevance (0x5e91a0) by `Action_Relevance_Type` is: 0 = the constant; 1 = the distance curve at the
  actor-target distance; 2 = the health curve at 0x396df0(actor) [M: the actor's health fraction]; 3 = the product of
  both.
- **Weighted pick.** The relevances of all actions with value > 0 are summed and one synced draw 0x532480(0, total) is
  made on the game LCG. The first action whose running sum reaches the draw is cloned (slot 4, 0x71eb30) and started
  (slot 5) with the actor, the target and the clash's object list minus those two. If the start fails, the clone is
  deleted. Running actions are serviced every slot-6 frames: Attack 2, Special_Ability 3, Move 6.
- **Update** (slot 7, 0x71ef10), while not finished (+0x120):
  1. Resolve the firing bone once: +0x128 := the index of `Attack_Firing_Bone_Name` (+0xf0) in the actor's model, or 0
     when it is not found.
  2. Face the target (0x3a8710).
  3. If the actor's current clip is `Attack_Animation_Type` (+0xd8):
     - while the clip still runs, 4 frames after the last shot the "<bone>_FLASH" sub-mesh is hidden again (0x71f050(·, 0));
     - once the clip has ended (state 2), the action is finished when the shots fired (+0x124) ≥
       `Attack_Number_Of_Shots` (+0xe4).
  4. Otherwise it fires (0x71ed40).
- **Fire** (0x71ed40):
  1. Play `Attack_Animation_Type`, `Attack_Animation_Subindex` (+0xdc), one-shot, blend 0.1 s, speed
     `Attack_Animation_Speedup` (+0xe0) (Play_Animation 0x3a92f0).
  2. If that fails, play clip type 5 with variant 0xFFFF and remember 5 / 0xFFFF. If that fails too, the action is
     finished.
  3. Play `Attack_SFXEvent` (+0x110) on the actor (0x2d5320).
  4. Call `WeaponBehaviorClass` fire 0x54ade0 with {shooter = actor, bone = +0x128, target, projectile type = the
     resolved `Attack_Projectile_Type` (+0xd0)}.
  5. Show the flash sub-mesh, add 1 to the shot count and set the last-shot frame to the current frame.
- **End** (slot 9, 0x71ec30). Play clip type 6, variant 0xFFFF, looped, blend U(0.1, 0.5) from the synced LCG 0x1ffbb0.
  If that fails, play the type's default clip (0x395920) with blend 0.1. Then hide the flash.
- **Post-parse** (slot 12, 0x71f1a0). A subindex < 0 becomes 0xFFFF (random variant).
- **Defaults** (written by the slot-0 reset 0x71e900) [M: the allocating constructor was not read]: animation type −1,
  subindex 0xFFFF, speedup 1.0, `Attack_Accuracy_Modifier` 1.0, `Attack_Number_Of_Shots` 0.
- **`Attack_Accuracy_Modifier` (+0xe8): parsed, never read.** Only the clone and the reset touch it, and the fire
  request carries no accuracy [H for the 13 methods and the helpers 0x71ed40 / 0x71f050; M that nothing outside reads
  it, since action objects are reachable only through the clash]. Every other asked field has a reader above.

## 12a. Redirect_Blaster: operand order and the redirect target (A12) [H unless tagged]
- **Turn.** d = wrap(victim yaw − projectile yaw) into (−180, 180] (0x20b6d0 on both yaws and on the difference),
  h = (360 − `Reaction_Arc_In_Degrees`) / 2.
  - Target yaw = victim yaw + (h − |d|) × 1.01 when d > 0, and victim yaw − (h − |d|) × 1.01 when d ≤ 0 (degrees).
  - Its unit vector (cos, sin, 0) goes to locomotor slot +0x218.
- **Redirect target.** Ability +0x170 (`param_1[0x2e]`) points to a record whose first qword is an object. The listener
  does nothing unless the record and its object exist, 0x48e290(ability, object) is true and record byte +0x12 is 0.
  This object is the T passed to 0x4cca10 [M: the ability's current activation target record].
- **Projectile filter.** The listener also ignores a shot whose projectile type value at type +0x1fe8 is 1, 2, 3, 4, 6, 8
  or 9 [H values; field not named].
- **After the shot is sent on.** After a redirect or a block that 0x4cca10 accepted, a victim whose current clip type is
  2 ends the listener at once. Otherwise, when the victim's clip is idle-like (0x27b5e0), or it is already in clip 0x1e
  that has not ended, the victim plays clip type 0x1e (30), variant 0xFFFF, one-shot, blend 0.1 s, speed 2.0. On the
  redirect path the 0x27b5e0 test also needs 0x397040 to be false. Then 0x48e4d0 (ability-used bookkeeping) runs.

## 13. `Terminate_Effect_On_Move_Command` (Ability_Base +0x42) [H unless tagged]
- **Reader.** SpecialAbilityClass slot 22 (+0xb0) 0x48e3c0, inherited by every ability class (a byte search finds its
  address in every SpecialAbility vtable, none overrides it). When the byte is 1 it calls the ability's slot 15 (+0x78,
  base 0x48f1e0, "end effects") with reason 3.
- **What slot 15 does.** For every entry of this ability in the object's effect list (object +0x100, keyed by the
  ability name), slot 31 (remove effect) runs on the entry's target while that target still exists. Entries whose
  target is gone are dropped. With a non-zero reason the ability's +0x41 byte is not tested.
- **Who calls slot 22.** 0x37ca30 (the object's ability set at object +0x100 + 8): for each special ability in the
  type's list (type +0x1a98) that is enabled for the owner (0x37be10: the player's unlock list, else the ability's +0x40
  default), it calls slot 22 with the object. The only caller of 0x37ca30 is 0x4f6b60, which adds the object to a
  movement coordinator (member vector +0x28, signal listeners 0x10 / 9). 0x4f6b60 is called from 0x438bf1, from
  0x5cf480 and through a vtable (0x87b290) [M: every new move order, player or AI].
- **Effect.** A move order removes, at once, the effects of every ability of the mover that has the flag. FoC sets it on
  Earthquake (6), Force_Telekinesis (2) and Cable (1).
- The type-based byte reads at +0x42 in 0x29f810 and 0x2b5ad0 are a GameObjectType field at the same offset, not this
  one. Default not read [L: false].

## 14. Arc_Sweep `Aggressiveness` / `Focus` [H for the owner; reader not found]
- **Not Arc_Sweep fields.** `tools/re/field_tables.py` put the table at 0xa3f8f0 into the Arc_Sweep segment by
  adjacency. That table (2 entries, registered with size 0x40 by the registry builder 0x515ee0) holds only
  `Aggressiveness` (type 9, +0x0) and `Focus` (type 9, +0x4).
- **Real owner.** FoC uses them only in `AI/PLAYERS/*.XML` (7 files), inside `<Personality>`. The AIPlayerType parser
  0x60f840 passes `<Personality>` to `AIPersonalityDataClass` 0x6cc2e0, which lives at AI player type +0x20. So
  `Aggressiveness` = AI player type +0x20 and `Focus` = +0x24.
- **Reader.** Not found [L]. A displacement scan cannot pick out +0x20 / +0x24 on this class; not chased. These rows
  belong to the AI group, not to tactical abilities.

## 15. Ability rows (A15) [H unless tagged]
**Battlefield_Modifier `Disable_Reinforcement_Vulnerability` (+0x194).**
- **Apply / remove.** Apply is 0x6f02c0 (from slot 30 0x6f10b0), remove is 0x6f13b0 (from slot 31 0x6f17d0).
  `Reverse_Application_Logic` (+0x196) swaps the two.
- **Players covered** (0x6f1130): the owner's player; with `Apply_To_All_Allies` (+0x195), every player allied to the
  owner (0x2823c0).
- **Write.** For each covered player, when +0x194 is set, apply inserts one entry into the player's bool modifier map at
  player +0x2c8: value true, key = the ability's source key (0x48e1e0), default stacking category. Remove erases the
  entry by that key (0x3a5920).
- **The gate.** 0x527f00(player) = not (BitwiseAnd fold of +0x2c8). While such an ability is active for a player, the
  transport unload step 0x644330 does not apply Elevated_Vulnerability to that player's unloaded units (§16a). With an
  empty map the vulnerability applies [M: the fold of an empty map].
- **Not covered.** The space arrival 0x2bf2d0 and the single-object branch of 0x644330 are not gated.
- **0x29e960, 9th argument.** It means "apply Elevated_Vulnerability to each member". 0x29e960 creates a type through
  0x2b3be0. For a company it makes one object per member (type +0x958 / +0x978 list, count +0x960 / +0x980 for land /
  space) and calls 0x3a8550 on each member when the argument is set. A single object never gets it.
  - These call sites pass 0, as an immediate or a register zeroed by XOR: 0x31182, 0x38aad, 0x2a5654, 0x2ab3a1,
    0x593e17, 0x59424e, 0x6aa605.
  - 0x29efce (0x29ef20, called from Kill 0x391310) passes on its own argument.
  - 0x6a23d4 passes R13B, not traced [M].
- **0x2a00b0, 10th argument.** It enables Elevated_Vulnerability for every object that 0x2a00b0 placed. Both call sites
  (0x2a1eb0 at 0x2a48c1 and 0x2a4d52) pass 0, so land-battle start placement never applies it.
- **Default.** False: the ctor 0x6f01a0 clears +0x194 / +0x195 with a word store.

**"Tractor_Beam" `Damage_Application_Time_In_Secs` / `Inaccuracy_Radius`: wrong class in the field table.** In
Tractor_Beam, +0x180 / +0x184 are `Activation_Min_Range` / `Activation_Max_Range`: its post-parse 0x711150 checks them
under those names. In FoC XML, `Damage_Application_Time_In_Secs` appears in Force_Lightning_Ability (1) and Hack_Ability
(2; Hack shares ForceLightning's accept 0x6fd610), and `Inaccuracy_Radius` in Corrupt_Systems_Ability (1, value 500).
- **ForceLightning +0x184 `Damage_Application_Time_In_Secs`: no reader [M].**
  - The only read is the post-parse slot 29 (0x6fdf50), which sets a negative value to 0 ("cannot be less than zero").
  - No other +0x184 access exists in the ForceLightning methods (0x6fcbc0..0x6fdf50) or the Hack methods
    (0x701240..0x702110). The other +0x184 hits nearby belong to ForceHealing (0x6fc980) and ForceTelekinesis (apply
    0x6fe1b0, its own field).
  - Scan: every +0x184 displacement in the image. A read through an outside helper that holds an ability pointer cannot
    be ruled out.
- **CorruptSystems +0x188 `Inaccuracy_Radius`.** The apply helper 0x71d4a0 (from slot 30 0x71d370) copies three values
  into the corrupted target's data pack (object +0x230):
  - pack +0x14 := `Inaccuracy_Radius`;
  - pack +8 := `Shield_Drain_Per_Second` (+0x184);
  - pack +0x10 := int(FPS × `Duration_In_Seconds` (+0x180)), at least 1.

  The pack's reader (CorruptSystemsBehaviorClass, vtable 0x8b1e18) was not read [L: how the radius scatters].

**Buzz_Droids `Chase_Radius` (+0x188), `Own_Damage_Per_Second` (+0x18c), `Enemy_Damage_Per_Second` (+0x190).**
- **Apply** (slot 30, 0x71c430). It creates the buzz-droid object (the resolved `Object_Type`, ability +0x1a0) at the
  target's position and fills its BuzzDroids data pack (object +0x228):
  - pack +8 := now + int(FPS × `Activation_Time_In_Seconds` (+0x180)), the end frame;
  - pack +0xc := `Chase_Radius`;
  - pack +0x10 := `Damage_Radius` (+0x184);
  - pack +0x28 := `Own_Damage_Per_Second`;
  - pack +0x2c := `Enemy_Damage_Per_Second`;
  - pack +0x18 := `Activate_SFX` (+0x1a8);
  - pack +0x30 := the target position.
- **Post-parse** (0x71c670). Negative values become 0. When `Damage_Radius` ≥ `Chase_Radius` it reports "Damage_Radius
  must be less than Chase_Radius" and swaps the two.
- **Types.** The values are 4-byte copies. The parser code is 0x8 (float) and they are checked as floats; "int" in the
  field table is only the kind of the XML sample.
- **Open.** The pack reader (BuzzDroidsBehaviorClass, vtable 0x8b1cd8) was not read [L: what the two damage rates do].

**Hack `Team_Member_With_Ability` (+0x1e8, resolved +0x1f0).**
- Hack slot 10 (0x701ab0) returns the resolved type when the user is a team member (object +0x348 ≠ −1), else none.
  Repair_Ability's vtable (0x8ca788) holds the same function.
- Slot 11 (0x701ad0) returns the first member of that type in the user's team container (object +0x110).
- The hack apply 0x7013d0 and slot 13 (0x701860) use it [M: as the member that does the work].
- FoC: Hack_Ability and Repair_Ability, both `Droid_R2D2`. The base slot 10 (0x17d070) returns none.

**Personal_Flame_Thrower `Damage_Delay_In_Secs` (+0x190).** The flame damage helper 0x705590 runs every state-2 frame
from the update 0x705e50.
- elapsed = `Fire_Time_In_Secs` (+0x18c) − (state pack +0x134 / FPS) [M: +0x134 is the frames-left timer set at state
  2].
- The ability's target is damaged only when elapsed > `Damage_Delay_In_Secs`, or when a flag passed in is already set
  [M]. Other objects in the cone (half-angle `Damage_Arc_In_Degrees`, +0x188) are damaged on every frame.
- Damage per frame = `Damage_Amount` (+0x184) / ((`Fire_Time_In_Secs` − `Damage_Delay_In_Secs`) × FPS), type
  `Damage_Type` (+0x180).
- Ctor 0x705440 default 0.2 s. FoC values 0.5 and 0.60.

## 17. Faction rows [M unless tagged]
- **`Land_Skirmish_Unit_Buy_Credits` (+0x28c4) / `Space_Skirmish_Unit_Buy_Credits` (+0x28c8): no reader [M].**
  - No instruction anywhere in the image has displacement 0x28c4 (t11 scan: all operand sizes, LEA included).
  - Every +0x28c8 access is the end of a qword vector of other classes (begin +0x28c0 / end +0x28c8 in 0x3035c0,
    0x3d2770, 0x3e4450, 0x3edd00) or the type ctor / copy.
  - This agrees with SKIRMISH_SETUP: the buy budget is GC `Skirmish_Buy_Credits`. A read through a sub-object pointer
    cannot be ruled out.
- **`Space_Forced_Retreat_Due_To_Superweapon` (+0x2180; for this SFX-event field the resolved event pointer is stored at
  +0x2180 itself)** [H read]. Reader: 0x343810 (from 0x343b90), which picks the speech of a retreat announcement from
  the player's faction (player +0x68).
  - **Space, both player arguments the same player** [M: the retreating player is the local one]: when an enemy
    (0x2824f0) has a live object matching query filter 0x2c [M: a superweapon-class object]:
    - if the player owns a live unit whose type has `Is_Super_Weapon_Killer` (+0x1f), it plays faction +0x2478;
    - otherwise it plays this event.
  - **Without such an enemy object** it plays faction +0x2418, or +0x2438 when 0x28ae30 is true.
  - FoC sets it for Rebel and Underworld (the Death Star retreat lines).

## 18. Leftovers of §1-§16 [H unless tagged]
- **§3 type `Projectile_Appearance_Delay_Frames`** (`WeaponBehaviorClass` fire 0x54ade0). It is the same launch delay as
  the hardpoint field.
  - i = the index of the fire request's bone in the shooter's bone list (object +0xf0 → +0x90, count +0xa0).
  - delay = 0x373610(type, i), or 0 when the bone is not in the list.
  - The new projectile's pack +0x60 := the shooter's current frame + delay. When the delay is non-zero, 0x39d6a0 is
    called on it, so it waits as in §3. Whether the fire sound is deferred on this path was not read [M].
- **§4 the branch in 0x2a00b0.** It is the arriving unit's type `Stay_In_Transport_During_Ground_Battle` (+0x2d), in
  land mode, when the type has no `Unique_Ground_Unit` replacement (type +0x1b60). With the flag set and a carrier
  present, the unit goes into the friendly `Can_Contain_Heroes_During_Ground_Battle` structure with the highest health
  (§4). Without it the unit is created the normal way (0x2b3be0). So the test is not "is a hero": it is this flag, and
  the heroes are the units that set it.
- **§6.** The duration constant 0x140891814 is −1.0 (§6a). The key 0x1408a618c is a 4-byte constant (bytes
  `17 6e 73 00`), not a string, and the category pointer 0x1407ff8a8 is the default category that Battlefield_Modifier
  also uses. Which list the entry stacks in is unchanged from §6 [M: defence].
- **§10 player +0x47c / +0x480.**
  - The only reader is 0x2814e0(player, hardpoint): it returns (now − stamp) / duration while now < stamp + duration,
    otherwise 1.0, and 1.0 when the duration is ≤ 0.
  - Its only caller is the tactical HUD 0x317d00 (from 0x30c3b0). For the hardpoint shown, the HUD sets the widget's
    enabled flag (slot +0x80) to "fraction < 1" and its fill (slot +0xf0) to the fraction.
  - No other code reads the two fields. The scan found only the writers 0x285d70 / 0x286350, the save / load LEAs
    0x284fe0 / 0x286760, and 0x1d6de0, which reads its own +0x478..+0x484 rectangle.
  - So the cooldown is enforced only by the HUD: the player's manual-fire control stays inactive until it runs out, and
    the sim does not block a manual target assignment made another way [M].
- **§16 0x29f270.** The override applies when the dying object's type `CategoryMask` (+0x1648) contains `Infantry`. The
  clone's persistence (+0x30) is then `Override_Death_Persistence_Duration` (FoC 30.0).
- **§16 `Default_Defense_Adjust` (+0x114): no reader [H].**
  - No absolute reference (MCX4_T11_T12 §S).
  - Of the 37 GameConstants methods and their direct callees, only the ctor 0x2c91c0 touches +0x114: it writes 1.0.
  - The same holds for `MaxCombat*AlignmentBonus` (+0x584 / +0x588 / +0x58c; the ctor writes a zero register).
- **§1 `Is_Escort` effect: not reached.** The 108 indirect calls through slot +0x100 belong to many classes and were not
  separated by receiver.

## 19. `Skirmish_Reinforcement_Delay_Frames` (GC +0x117e8; ctor and FoC 90) [H reads; M roles]
- **Base entry.** The player init 0x526890 (from 0x3b7d40, 0x3b9ed0, 0x4d7610, 0x4d8750) inserts into the player's int
  modifier map at player +0x230: value 90, permanent (duration −1.0), default category.
- **Battlefield_Modifier** (apply 0x6f02c0). When `Reinforcement_Time_Multiplier` (+0x180) ≠ 1, it adds
  int(90 × multiplier) − 90 to the +0x230 map of each covered player; remove takes it out again.
- **Use: 0x642ef0** (from 0x643c30, in the TransportLanding code) [M: the reinforcement-drop check]. It runs only when
  0x28afb0(game-mode manager) is true [M: skirmish / multiplayer] and request byte +0xbd is 0.
  1. D = the sum fold of player +0x230 (90 plus the deltas).
  2. D is raised to player +0x240 when +0x248 is set, and capped at player +0x244 when +0x249 is set.
  3. The drop is refused while now − request +0xb8 < D [M: the compare was read, the body of its branch was not].
- **Meaning.** The minimum gap in frames between two reinforcement drops of a player. It is not on the quick-reinforce
  path (agrees with BKG1).
- The HUD function 0x316050 (from 0x312b90) folds the same map [M: the countdown display].

## Not reached (batch 2)
- §1 `Is_Escort`: callers of `CombatantBehaviorClass` slot 32 (+0x100).
- §14: who reads AI player type +0x20 / +0x24 (`Aggressiveness` / `Focus`).
- §15: the readers of the CorruptSystems pack +0x14 and of the BuzzDroids pack +0xc / +0x28 / +0x2c.

## Not reached (time box, batch 1; §11-§17 are answered above)
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
