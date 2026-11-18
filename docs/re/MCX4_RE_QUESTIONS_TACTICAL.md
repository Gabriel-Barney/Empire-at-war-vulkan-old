# mcx4 RE questions, tactical / objects share (lane mcx4-tac)

Roadmap group `mcx4` (box MCX2), tasks t03-t06 (tactical part). Base list: `MCX4_RE_QUESTIONS.md` Q3, Q4, Q5.2, Q5.3, Q6;
this file adds the tags that list does not ask and narrows a few. Answers go to `docs/re/MCX4_RE_ANSWERS_TACTICAL.md`
(behaviour in words, [H] / [M] / [L], RVAs; no decompiled code). For every tag: **who reads the parsed field (type /
class offset from `xml_field_tables.md`), when, and what it changes**, or **no reader anywhere** (with the xref evidence:
the tag is then "retail-unused" and the port keeps ignoring it).

## A. Asked in `MCX4_RE_QUESTIONS.md` (answer them here)
- Q3.1 `Turret_Targets_Air_Vehicles` / `_Ground_Vehicles` / `_Ground_Infantry` / `_Anything_Else` (type +0x5d8 / +0x5dc /
  +0x5e0 / +0x5e4, int, ctor default 9999) and `Air_Vehicle_Turret_Target` / `Ground_Infantry_Turret_Target` /
  `Ground_Vehicle_Turret_Target` (type +0xb4 / +0xb5 / +0xb6, bool). The ints are counts or priorities, not bools (base
  values 1, 2, 3, 4, 9999): what does the number mean (max simultaneous turrets on that target class? a priority rank?),
  which code reads them (turret / hardpoint target pick, `TURRET` behaviour, garrison?), and how the target-side flags
  classify a target (several flags set? none set -> "anything else"?).
- Q3.2 `Projectile_Blast_Area_Dropoff` (+0xaa bool) and `Projectile_Blast_Area_Dropoff_Tiers` (+0x488 int): the blast
  falloff (by distance from the centre in N steps? which distance: centre, edge?) and where it enters area damage
  (TACTICAL_COMBAT §3.5 / the area damage function).
- Q4.1 `Size_Value` (+0x898 int): every reader.
- Q4.2 `Required_Timeline` (+0xf08 int): every reader; what a timeline value is.
- Q5.2 `Cash_Point_Credits_Per_Second` (+0x1fb0), `Cash_Point_Radius` (+0x1fb4), `Cash_Point_Transition_Time_Seconds`
  (+0x1fb8): the behaviour that reads them (a marker type in MARKERS.XML; capture rule, payout cadence, who is paid).
- Q5.3 `Respawn_Whole_Team_When_Killed` (+0x16 bool).
- Q6.1-Q6.6: `Income_Stream_Mod_Ability` `Affects_All_Allied_Sources` (+0x198) / `Interval_Multiplier` (+0x180);
  `Absorb_Blaster_Ability` `Damage_Absorb_Percentage` (+0x17c) / `Damage_Absorb_Amount` (+0x180); ability base
  `Unit_Strength_Category` (+0x48) with `Unit_Strength_Factor` (+0x50); `Siphon_Credits_Ability` `Evade_Detection_Chance`
  (+0x178); `Income_Stream_Ability` `Split_Income_With_Allies` (+0x184); `Spawn_Ability` (or its class)
  `Recharge_Time_In_Seconds` (+0x194, int) against the base `Recharge_Seconds` (+0x24).

## B. Added by this lane
- B1 `Is_Indigenous_Unit_Corruptible` (type +0xa9 bool; base: False on indigenous units and props): reader; does it gate
  a corruption / bribe / "corrupt indigenous" action, or galactic corruption of planets with indigenous units?
- B2 `Build_Limit_Lifetime_For_All_Allies` (+0x88c): SKIRMISH_SETUP §7 names the tactical build check 0x497140 and build
  bar 0x288b80 / 0x288d70. What does the lifetime counter 0x281c70 count in a tactical battle (objects the player built
  in this battle? since the campaign start? produced by any means or only through the build manager?), and is the
  galactic production check another reader?
- B3 `Tactical_Production_Queue` (+0xa30): readers besides the build manager 0x496a20 / 0x497140 and the queue-open test
  (e.g. the build-pad build, AI production, the HUD queue display): which queue index a type uses where.
- B4 `Can_Be_Only_One` (+0x78): readers besides the skirmish buy dialog 0xf0920 (galactic production, tactical build bar,
  reinforcement placement, Lua)?
- B5 `Fires_Forward` (+0x44): readers besides the weapon aim 0x54a270 (hardpoint `Fire_At` 0x3825b0? the bombing-run
  bomber, which is the only base type with `Yes`): does it also skip the facing turn / the in-cone test?
- B6 `Projectile_Max_Scan_Range` (+0x4c4): confirm the lost-target scan (TACTICAL_COMBAT §3.3) is the only reader and that
  it can never pick a candidate; any other reader (homing start, missile spawn)?
- B7 `Disallows_Hyperspace_Retreat` (type +0x39 bool; base: one `No`, Merchant_Freighter): BATTLE_TRANSITIONS §3.4 names
  the retreat-permission test 0x282750 as its reader, but that function's full order (CU8F_T01_T02_T07_RE §3.2: game
  types, interdict list, TACTICAL_SUPER_WEAPON query) reads no type field. Who reads +0x39 (the retreatable-units list
  vf8 of the space / land retreat coordinators, the HUD retreat button, galactic fleet retreat?) and what it does (the
  unit stays behind? the whole request is refused?).
- B8 `Projectile_Bomb_Fall_Accel_Rate` (type +0x1e88, TACTICAL_COMBAT §3.3 categories 2 / 5 / 9 "ballistic"): the flight
  step that reads it (per-frame z speed += rate? capped? the initial z speed?). The port flies bombs straight, so a
  `Fires_Forward` bomber (Y-Wing_Bombing_Run) would drop level bombs.
