# mcx4 RE questions: honour gaps (tags base FoC sets that the port stores without using)

Roadmap group `mcx4` (box MCX2), tasks t01-t07; design `docs/MOD_COMPAT_DESIGN.md` §7.2; work list
`docs/status/tag-ledger.md` ("base-FoC gaps"). Answers go to `docs/re/MCX4_RE_ANSWERS.md` (behaviour in words, [H] / [M]
/ [L], RVAs; no decompiled code). For every tag: **who reads the parsed field (type / class offset), when, and what it
changes**; or **no reader anywhere in the binary** (then say so with the cross-reference evidence: such a tag is
"retail-unused" and the port keeps ignoring it).

Already settled elsewhere (not asked again): `Fires_Forward` (LOB_SOLVER §… 0x54c2f0), `Projectile_Max_Scan_Range`
(TACTICAL_COMBAT, +0x4c4), `Can_Be_Only_One` (SKIRMISH_SETUP buy limits 0xf0920), `Tactical_Build_Increments_Tech_Level` /
`Tactical_Destruction_Decrements_Tech_Level` (REMAINING_GAPS 0x498170), `Disallows_Hyperspace_Retreat` (BATTLE_TRANSITIONS
0x282750), `Max_Remote_Bombs_Per_Player` (ABILITIES_HARDPOINTS PLACE_REMOTE_BOMB), `TradeRouteMovementFactor`
(BKG3_RE_ANSWERS 0x662b30), `Is_Autoresolve_Allowed` (BATTLE_TRANSITIONS).

## Q1. Campaign `Special_Case_Production` (t01; 504 base uses in campaign files)
`MCX0_T07_UNLISTED_TAGS.md` found the string only. Where is it parsed (campaign field table or a hand parser), what is
its value shape (FoC: e.g. `<Special_Case_Production>Faction, Type, ...</Special_Case_Production>`?), and what does the
galactic production code do with it (extra buildable types per faction for that campaign? price / time override?).

## Q2. GameConstants (t02)
1. `MaximumStarbaseLevel` (+0xF4), `MaximumGroundbaseLevel` (+0xF8), `MaximumSpecialStructures` (+0xFC),
   `MaximumSpecialStructuresLand`, `MaximumSpecialStructuresSpace`: every reader outside the AI tokens (build checks,
   upgrade checks, HUD). Does any cap production or a base level?
2. `Progressive_Taxation` (+0x150), `Income_Redistribution` (+0x178): GALACTIC_ECONOMY says no reader on the galactic
   income path; confirm no reader anywhere (all xrefs of the two offsets).
3. `Skirmish_Buy_Credits`: reader and effect (pre-battle buy budget? per side?).
4. `First_Strike_Extra_Damage_Percent`: reader; when does a "first strike" apply (attacker that is not yet seen? first
   shot of a battle?), and how the percent enters damage.
5. `Random_Story_Triggers`, `Random_Story_Rewards`, `Random_Story_Max_Triggers`, `Random_Story_Empire_Construction`,
   `Random_Story_Empire_Destroy`, `Random_Story_Rebel_Construction`, `Random_Story_Rebel_Destroy`,
   `Random_Story_Reward_Empire_Buildable`, `Random_Story_Reward_Empire_Unit`, `Random_Story_Reward_Rebel_Buildable`,
   `Random_Story_Reward_Rebel_Unit`: the random story system. Is it live in FoC (which campaigns / modes start it), and
   what are the rules (trigger pick, reward pick, RNG draws, max triggers)?

## Q3. Object targeting (t03)
1. `Turret_Targets_Ground_Infantry`, `Turret_Targets_Ground_Vehicles`, `Turret_Targets_Air_Vehicles`,
   `Turret_Targets_Anything_Else` (bools on turret types) and `Ground_Infantry_Turret_Target`,
   `Ground_Vehicle_Turret_Target`, `Air_Vehicle_Turret_Target` (on target types): the target filter that reads them,
   and how the two sides combine (a turret that targets infantry may fire at a type flagged as a ground-infantry turret
   target?).
2. `Projectile_Blast_Area_Dropoff` and `Projectile_Blast_Area_Dropoff_Tiers`: blast damage falloff formula (linear by
   distance? stepped in N tiers?), and where it enters the blast damage of TACTICAL_COMBAT.

## Q4. Object production and limits (t04)
1. `Size_Value` (417 base uses): every reader (pop cap? fleet size? AI? garrison?).
2. `Required_Timeline` (243 base uses): SKIRMISH_SETUP says no tactical reader; is there a galactic reader (build
   check, Lua `Is_Buildable`), and what is a "timeline" value?

## Q5. Underworld, retreat, respawn (t05)
1. `Cheap_Bribe_Cost`, `Expensive_Bribe_Cost`: A2K_T10 has the AI blockade bribe 0x434720 [M meaning]; confirm the
   meaning and list any other reader (human bribe action, tactical bribe ability).
2. `Cash_Point_Credits_Per_Second`, `Cash_Point_Radius`, `Cash_Point_Transition_Time_Seconds`: reader and effect (a
   capturable credit point in land battles?).
3. `Respawn_Whole_Team_When_Killed`: reader; when a company member dies, does the whole team respawn (where, after how
   long, from which pool)?

## Q6. Abilities (t06)
1. `Income_Stream_Mod_Ability`: `Affects_All_Allied_Sources`, `Interval_Multiplier`: how the mod changes other income
   streams (which ones, multiply the interval?).
2. `Absorb_Blaster_Ability`: `Damage_Absorb_Amount`, `Damage_Absorb_Percentage`: the absorb rule (cap per hit? total
   pool? percent of each hit?).
3. Ability base `Unit_Strength_Category` (+0x48; TACTICAL_GAPS vf7 0x48d320 with `Unit_Strength_Factor`): who calls
   vf7 and what the pair changes (AI strength estimate?).
4. `Siphon_Credits_Ability` `Evade_Detection_Chance`: the detection roll (when, which RNG, what detection does).
5. `Income_Stream_Ability` `Split_Income_With_Allies`: how the income is split (equal shares among allied players?
   rounding?).
6. `Spawn_Ability` `Recharge_Time_In_Seconds`: differs from the base `Recharge_Seconds`? which one wins?

## Q7. Faction and campaign (t07)
1. Faction `Credits_Accumulation_Factor` (+0x3a8): GALACTIC_ECONOMY says not read on the income path [M]; every xref.
2. Faction `Corruption_Tactical_Build_Pad_Price_Multipliers`, `..._Build_Pad_Time_Multipliers`,
   `Corruption_Tactical_Production_Price_Multipliers`, `..._Production_Time_Multipliers`: value shape (per corruption
   type list?) and where they scale tactical build pad and production price / time.
3. Campaign `Is_Listed`: does it hide a campaign from the front-end lists (which lists), anything else?
4. Campaign `Autoresolve_Exclusion_Locations`, `Corruption_Level_Override`: readers and effect.
