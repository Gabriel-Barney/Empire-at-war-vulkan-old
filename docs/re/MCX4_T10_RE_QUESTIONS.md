# MCX4 t10: tactical rules long tail, RE questions (lane mcx4-tac, 2026-11-18)

Roadmap group mcx4, task t10. Every row below is a tier-1 tag-ledger row that base FoC sets (`base_uses` > 0) and the port
stores without using. List: `python -I tools/lint/check_tag_ledger.py --gaps --group tactical`. Type field offsets are in
`xml_field_tables.md`. Answers go to `MCX4_T10_RE_ANSWERS.md`, one section per row group, with the reader RVA(s), the effect
in words, the ctor default, and a confidence tag. **"No reader" [H] is wanted as much as a reader**: such rows go to the
ledger's `RETAIL_UNUSED` table. Use the method of `MCX4_RE_ANSWERS_TACTICAL.md` §10, a scan of every access at the
field's displacement through a type pointer.

Rows are listed in descending base uses; answer in that order and stop where time runs out.

## A. GameObjectType flags and values
1. `Is_Escort` (194): the reader and what it changes (formation, AI guard, targeting, fleet movement).
2. `Immune_To_Damage` (46): where Take_Damage or the kill path tests it; whether hardpoints, shields and the death
   path are covered too.
3. `Projectile_Appearance_Delay_Frames` (30; also HardPoint, 30): who reads the type field and who reads the hardpoint
   field (a shot's visibility, the spawn delay or the first sweep frame?).
4. `Can_Contain_Heroes_During_Ground_Battle` (26): the garrison / transport check that reads it.
5. `Is_Force_Sensitive` (20).
6. `Out_Of_Combat_Defense_Adjustment` (19): the formula and when "out of combat" holds (FIGHTER_FLYING.md names it).
7. `Conversion_Ability_Changes_To_Enemy` (12).
8. `Respawn_Tied_To_Structure` (2), `Projectile_Acceleration_Per_Frame` (2: TACTICAL_COMBAT §3.3 says "the update
   adds 0.0" [L]; confirm), `Deployed_Max_Attack_Distance_Multiplier` (1), `Deployed_Turret_Elevate_Extent_Degrees` (1).

## B. HardPoint
9. `Allow_Opportunity_Fire_When_Targeting` (57) / `_When_Idle`: TACTICAL_COMBAT §2 already gives the rule; confirm the
   ctor default (true?) only.
10. `Power_To_Weapons_Mode_Weapon_Delay_Multiplier` (6), `Fire_When_In_Power_To_Weapons_Mode` (6),
    `Defend_Mode_Weapon_Delay_Multiplier` (2), `Full_Salvo_Weapon_Delay_Multiplier` (1), `Manual_Hardpoint_Firing_Cooldown_Secs`
    (1): the recharge formula that multiplies them (TACTICAL_COMBAT around "weapon delay": complete it) and the manual
    cooldown's reader.

## C. Attack_Action (8 tags, 10-20 uses each)
11. Which class parses the `Attack_Action` entries (`Action_Object`, `Action_Target`, `Action_Relevance_Type`,
    `Action_Distance_From_Target_Curve`, `Action_Health_Percentage_Curve`, `Attack_Accuracy_Modifier`,
    `Attack_Number_Of_Shots`, `Attack_Projectile_Type`), who runs them (AI? the unit's attack pick?) and what each field does.

## D. Ability fields
12. Redirect_Blaster `Max_Projectile_Redirection_Angle_In_Degrees` (52), `Turn_To_Face_Unblockable_Shots` (52).
13. Ability_Base `Terminate_Effect_On_Move_Command` (9).
14. Arc_Sweep_Attack `Aggressiveness` / `Focus` (7).
15. Battlefield_Modifier `Disable_Reinforcement_Vulnerability` (3); Tractor_Beam_Attack `Damage_Application_Time_In_Secs` (3),
    `Inaccuracy_Radius` (1); Buzz_Droids `Chase_Radius`, `Enemy_Damage_Per_Second`, `Own_Damage_Per_Second` (2);
    Hack `Team_Member_With_Ability` (2); Personal_Flame_Thrower `Damage_Delay_In_Secs` (2).

## E. GameConstants and Faction
16. `Elevated_Vulnerability_Duration` / `_Factor` and the `Space_` pair; `Depleted_Shield_Damage_Increment`;
    `Default_Defense_Adjust`; `MaxCombatAccuracyAlignmentBonus` / `MaxCombatDamageAlignmentBonus` /
    `MaxCombatSensorRangeAlignmentBonus`; `Override_Death_Persistence_Duration` (all 1-2 uses).
17. Faction `Land_Skirmish_Unit_Buy_Credits` / `Space_Skirmish_Unit_Buy_Credits` (3), `Space_Forced_Retreat_Due_To_Superweapon` (2).

Out of this list (other owners or already specced): the Income_Stream_Ability rows (mcx4-gal, MCX4_T09_RE_QUESTIONS Q9-Q11),
`Skirmish_Reinforcement_Delay_Frames` (BKG1_RE_ANSWERS), `Tactical_Build_Time_Multiplier` (A2H_T06_TACTICAL_ETA).
