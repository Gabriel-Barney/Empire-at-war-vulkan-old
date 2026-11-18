# mcx4 t13: RE answers, audio cues of the honour gaps

Answers to `MCX4_T13_RE_QUESTIONS.md` (lane mcx4-pres, roadmap group `mcx4` task t13, 152 audio rows). Retail
`StarWarsG.exe` FoC x64 (Steam), 2026-11-18. Addresses are RVAs (image base 0x140000000). Behaviour only, no decompiled
code. **[H]** read and cross-checked, **[M]** read, one link inferred, **[L]** inferred. This run used REA and offline
reads of the image only; ReVa / Ghidra was not opened (another lane held the turn), so nothing was renamed in the shared
project.

## Method

- **Where a field is read.** Three searches per field, all over the whole image:
  1. a capstone linear sweep of `.text` that indexes every memory displacement and every immediate;
  2. a text search of an offline Ghidra C export of every function (hex and decimal spellings of the offset);
  3. for per-enum arrays, the scaled-index form `(index + K) * 0x10` as well (the toggle and hard point arrays are only
     reached that way; a plain displacement search misses them).
  Hits were checked in context (base register = a faction, type, GameConstants or ability object). REA `xrefs` gave the
  RIP-relative readers of GameConstants fields (GameConstants is one global at 0x140b15800).
- **Field layout by parser type code** (field-descriptor tables in `.rdata`, stride 0x20: tag, type, offset, extra; table
  start and size from the registration list 0x515ee0):
  - 0x1f SFX reference: the event pointer is at the offset.
  - 0x20 speech / 0x21 music: the name string is at the offset and the resolved event pointer at offset + 0x20. Readers
    use the pointer, so the search must use offset + 0x20 (the rows' offsets are the string offsets).
  - 0x22 (type, SFX) list: 0x50-byte entries, object type at +0x40, event at +0x48.
  - 0x4c: 77 entries × 0x10 indexed by the unit ability enum. 0x42: 13 entries × 0x10 indexed by the hard point type.
- **Not readers** (excluded when they were the only hits): faction ctor 0x33a580, dtor 0x33c160 / 0x33c342, post-load
  resolver 0x330450; object type ctor 0x367250, copy 0x36c000, dtor 0x36a070; GameConstants ctor 0x2c4370, defaults
  0x2c91c0, dtor 0x2c5b30, resolver 0x2c79d0; the static initialiser 0x5b43d0. **Retail-unused** below means: every hit of
  the field's displacement (and of the pointer offset for speech / music) is one of these.
- **Who hears it.** Local player 0x294a40 (id 0x294a70), player by id 0x294bc0, neutral player 0x294aa0; a faction is
  reached through player +0x68. The relation table is player +0x370[id]: 0 = self or ally, 1 = enemy. 0x2823e0 /
  0x2823c0 test relation 0, 0x2824f0 / 0x2824d0 test relation 1, 0x2813b0 returns it. "Friendly" below means relation 0
  to the local player, which includes the local player.
- **How it plays.** 0x2d5290 = 2D, not forced. 0x2d5320 = attached to an object (3D-attached when the event's `Is_3D`
  +0x29 is set, else 2D-attached). 0x2d5240 = 3D at a position (fog-gated). 0x2d50e0 / 0x2d5010 = by name, attached / 2D.
  0x2d5c00 = stop that event on that object. Speech: queue 0x321170 (FIFO, manager 0x140b2b300). Music: 0x2d21f0
  (manager 0x140b27dc0; event, mode, resume, force). The SFX manager is 0x140b27e60. Every SFX play also passes the
  event's own gates (`Max_Instances`, `Overlap_Test`, `Probability`, AUDIO §1.2). "No cooldown" means none beyond those.

## Summary

Counts: **136 tags have a reader, 15 are retail-unused, 1 is open** (`Unit_Ability.SFXEvent_Special_Ability_Loop`).

| Tag(s) | Reader (RVA) or retail-unused | Who hears it, how | Conf |
|---|---|---|---|
| Faction `SFXEvent_Tactical_Gain_Friendly_Control`, `SFXEvent_Tactical_Lose_Friendly_Control`, `SFXEvent_Tactical_Gain_Enemy_Control`, `SFXEvent_Tactical_Lose_Enemy_Control` | capture complete 0x500e30 (from CapturePointBehavior 0x501980) | local faction's per-type list; friendly and enemy halves; 2D | H |
| Faction `SFXEvent_HUD_Reinforcement_Point_Contested` | 0x501980 | local owner of a reinforcement point being neutralised; 2D | H |
| Faction `SFXEvent_HUD_Landing_Zone_Captured`, `SFXEvent_HUD_Landing_Zone_Lost`, `SFXEvent_HUD_Last_Landing_Zone_Lost`, `SFXEvent_HUD_Build_Pad_Captured`, `SFXEvent_HUD_Build_Pad_Lost` | **retail-unused** | (the per-type control lists do this job) | H |
| Faction `SFXEvent_HUD_Won_Land_Battle`, `SFXEvent_HUD_Won_Land_Battle_Enemy_TSW_Present`, `SFXEvent_HUD_Won_Space_Battle`, `SFXEvent_HUD_Won_Space_Battle_Enemy_TSW_Present`, `SFXEvent_HUD_Lost_Land_Battle`, `SFXEvent_HUD_Lost_Land_Battle_Enemy_TSW_Present`, `SFXEvent_HUD_Lost_Space_Battle`, `SFXEvent_HUD_Lost_Space_Battle_Enemy_TSW_Present`, `SFXEvent_HUD_Won_Tactical_Corruption_Mission`, `SFXEvent_HUD_Lost_Tactical_Corruption_Mission` | 0x343810 from the victory declare 0x343b90 | local winner / local enemy of the winner; 2D | H |
| Faction `Music_Event_Tactical_Win`, `Music_Event_Tactical_Lose` | 0x28f250 (from 0x343b90 and dialog 0x88fe0) | local player; music modes 9 / 10 | H |
| Faction `Music_Event_Strategic_Win`, `Music_Event_Strategic_Lose` | **retail-unused** | | H |
| Faction `Land_Retreat_Begin_SFXEvent`, `Land_Retreat_Enemy_Begin_SFXEvent`, `Space_Retreat_Begin_SFXEvent`, `Space_Retreat_Enemy_Begin_SFXEvent` | 0x5350d0 (land), 0x533080 (space) | local retreater / local enemy of the retreater; 2D | H |
| Faction `Land_Retreat_Cancel_SFXEvent`, `Space_Retreat_Cancel_SFXEvent` | 0x534f40 (land), 0x532ee0 (space) | see section; space plays for any retreater | H |
| Faction `Land_Retreat_Not_Allowed_SFXEvent`, `Land_Retreat_Not_Allowed_Reason_1_SFXEvent`, `_Reason_2_`, `_Reason_3_`, `Space_Retreat_Not_Allowed_SFXEvent`, `Space_Retreat_Not_Allowed_Reason_1_SFXEvent`, `_Reason_2_`, `_Reason_3_` | HUD retreat buttons 0x30d5d0 / 0x30d710 (land), 0x30e1b0 / 0x30e2f0 (space) | local player who pressed retreat; 2D | H (M on two codes) |
| Faction `Land_Surrender_SFXEvent`, `Space_Surrender_SFXEvent` | **retail-unused** | | H |
| Faction `Reinforcements_Selection_SFXEvent`, `Reinforcements_Pick_Landing_Zone_SFXEvent`, `Reinforcements_Cancelled_SFXEvent`, `Reinforcements_Enroute_SFXEvent`, `Reinforcements_Ready_SFXEvent` | 0xdbab0 / 0x2e5580; 0xdc200 / 0x2ea5d0; 0xdc0e0 / 0x343e0; 0x36300 / 0x595650; 0x287e70 | local player; 2D | H |
| Faction `Reinforcements_Requesting_SFXEvent` | **retail-unused** | | H |
| Faction `SFXEvent_Bombard_Available`, `SFXEvent_Bombard_Ally_Available`, `SFXEvent_Bombard_Enemy_Available` | PlanetaryBombardManager 0x52a110 | local player, from the **available player's** faction; 2D | H |
| Faction `SFXEvent_Bombing_Run_Available`, `SFXEvent_Bombing_Run_Ally_Available`, `SFXEvent_Bombing_Run_Enemy_Available` | BombingRunManager 0x495dd0 | same scheme; 2D | H |
| Faction `SFXEvent_Bombing_Run_Begin_Crosstalk` | bombing-run launch 0x492d60 | friendly bomber owner's faction, attached to the first bomber | H |
| Faction `SFXEvent_Bombard_Cancelled` | 0x2e7bc0, 0x2f6c30 | local player; 2D | H |
| Faction `SFXEvent_HUD_Special_Weapon_Ready`, `SFXEvent_HUD_Enemy_Special_Weapon_Ready` | SpecialWeaponBehavior 0x492010 | local faction list entry by weapon index; 2D | H |
| Faction `SFXEvent_HUD_Special_Weapon_Firing`, `SFXEvent_HUD_Enemy_Special_Weapon_Firing` | 0x491cd0 | local faction list entry by weapon index; 2D | H |
| Faction `SFXEvent_HUD_Special_Weapon_Charging`, `SFXEvent_HUD_Enemy_Special_Weapon_Charging` | **retail-unused** | | H |
| Faction `SpeechEvent_Super_Weapon_Moving_Range_60_Seconds`, `_30_`, `_15_`, `_05_`, `SpeechEvent_Super_Weapon_Enemy_Moving_Range_60_Seconds`, `_30_`, `_15_`, `_05_` | TSW approach 0x42b560 (from 0x42a7b0) | local faction; speech queue | H |
| Faction `SpeechEvent_Super_Weapon_Moved_Into_Range`, `SpeechEvent_Super_Weapon_Enemy_Moved_Into_Range` | TSW service 0x42a7b0 | local faction; speech queue | H |
| Faction `SpeechEvent_Super_Weapon_Moving_Into_Range`, `SpeechEvent_Super_Weapon_Enemy_Moving_Into_Range` | **retail-unused** | | H |
| Faction `SpeechEvent_Tactical_Intro_Land_Attacker`, `_Land_Defender`, `_Land_Raid_Attacker`, `_Land_Raid_Defender`, `_Space_Attacker`, `_Space_Defender` | 0x364b20 (AUDIO §9.7 confirmed) | local faction; speech queue | H |
| Faction `SFXEvent_HUD_Base_Shield_Online`, `SFXEvent_HUD_Base_Shield_Offline`, `SFXEvent_HUD_Enemy_Base_Shield_Online`, `SFXEvent_HUD_Enemy_Base_Shield_Offline` | BaseShield Set_State 0x56b540 | local faction; 2D | H |
| Faction `SFXEvent_Base_Shield_Absorb_Damage` | 0x56ba20 | shield owner's faction; 3D at the hit | H |
| Faction `SFXEvent_HUD_Gravity_Control_Generator_On`, `SFXEvent_HUD_Gravity_Control_Generator_Off` | GravityControlFieldBehavior 0x61c700 | local faction; 2D | H |
| Faction `SFXEvent_Tactical_Object_Building_Started`, `SFXEvent_Tactical_Object_Building_Loop`, `SFXEvent_Tactical_Object_Building_Complete` | 0x42dbd0 (start), 0x42e890 (complete) | owner's faction, attached; any owner | H |
| Faction `SFXEvent_Tactical_Object_Sold` | TacticalSellBehavior 0x5248b0 | owner's faction; 3D at the object | H |
| Faction `SFXEvent_Tactical_Unit_Cap_Reached` | ReinforceEvent::Execute 0x403cb0 | local issuer; 2D | H |
| Faction `SFXEvent_Tactical_Pop_Cap_Reached` | Player::Update 0x287e70 (P4 §2.7.3) | local player; 2D | H |
| Faction `SFXEvent_HUD_Tactical_Victory_Near` | overrun reveal 0x364920 | local player on the stronger side, multiplayer only; 2D | H |
| Faction `SFXEvent_HUD_Repairing` | cursor action 0x41f70 | local player; 2D | H |
| Faction `SFXEvent_Bunker_Vacated` | garrison eject 0x444dd0 | local owner, attached to the leaving unit | H |
| Faction `SFXEvent_HUD_Advisor_Urgent` | advisor enqueue 0x2dd630 (shares +0x2f58 with `_Message` / `_Hint`) | local; 2D; the Urgent value is overwritten by Hint | H (M urgent silent) |
| Faction `SFXEvent_Tactical_Corruption_Mission_Commencing` | 0x2af7c0 / 0x2b0520 (AUDIO §9.7) | local initiator, fallback when the type has no speech; 2D | H |
| Faction `SFXEvent_GUI_Toggle_Non_Hero_Ability_On`, `SFXEvent_GUI_Toggle_Non_Hero_Ability_Off`, `SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_On`, `SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_Off` | SetUnitAbilityModeEvent 0x430270; countdown expiry 0x42f460 | issuer's faction, own / enemy variant for the local listener; 2D | H |
| Type `SFXEvent_Attack_Hardpoint` | attack responses 0x44590, 0x44cd0 (getter 0x373800) | local unit response, attached | H |
| Type `SFXEvent_Hardpoint_Destroyed`, `SFXEvent_Hardpoint_All_Weapons_Destroyed`; HardPoint `Death_Explosion_SFXEvent` | hard point destroyed 0x386660 | local parent owner, 2D; explosion any owner, 3D at the bone | H (M explosion pointer) |
| Type `Land_Damage_SFX` | 0x3ac290 | any owner, by name attached | H (M "NULL") |
| Type `SFXEvent_Health_Low_Warning`, `SFXEvent_Health_Critical_Warning`, `SFXEvent_Enemy_Damaged_Health_Low_Warning`, `SFXEvent_Enemy_Damaged_Health_Critical_Warning` | Take_Damage 0x3a9e30 | local victim / local attacker; 2D | H |
| Type `SFXEvent_Tactical_Build_Started`, `SFXEvent_Tactical_Build_Complete`, `SFXEvent_Tactical_Build_Cancelled` | 0x496a20 / 0x3caae0; 0x498170 / 0x402840 / 0x42e890; 0x498820 | local builder in tactical; 2D | H |
| Type `Build_Speech_Countdowns` | 0x401e50 (MCX4_T09 Q7) | | H |
| Type `Corruption_1_Success_Speech_Event`, `Corruption_2_Success_Speech_Event`, `Corruption_3_Success_Speech_Event`, `Corruption_1_Failure_Speech_Event`, `Corruption_2_Failure_Speech_Event`, `Corruption_3_Failure_Speech_Event`, `Corruption_1_Mission_Commencing_Speech_Event` | 0x28bf10, 0x52bf50 (getters 0x3716f0 / 0x3713d0); commencing 0x371450 | local corruptor; speech + hologram | H (M one flag) |
| Type `SFXEvent_Move_Into_Asteroid_Field`, `SFXEvent_Move_Into_Nebula` | move responses 0x3bf20 / 0x442f0 / 0x44ba0 | local unit, attached | H |
| Type `SFXEvent_Turret_Rotating_Loop` | 0x3a8080 | any owner, attached loop | H |
| Type `Play_SFXEvent_On_Sighting` | HideWhenFogged service 0x53ddc0 → 0x361470 | local player, once per type per battle | H |
| Type `SFXEvent_Sold_Tactical` | 0x5248b0, UI sell 0x3471d0 | local seller; 2D | H |
| Type `Spin_Away_On_Death_SFXEvent_Start_Die` | DeathBehavior 0x58cf90 | any owner, attached or 2D | H |
| Type `SFXEvent_Damaged_By_Asteroid` | 0x436920 | any owner, attached, every asteroid hit | H |
| Type `SFXEvent_Special_Weapon_Ready` | 0x492010, LobbingSuperweapon 0x4ea6f0 | local or friendly owner; 2D | H |
| Type `SFXEvent_Bombard_Incoming` | 0x5295f0 | local player; 2D | H |
| Type `SFXEvent_Bombard_Select_Target` | **retail-unused** | | H |
| Type `Selt_Destruct_SFXEvent_Start_Die` | SelfDestructBehavior 0x630950 (start), 0x630840 / 0x6306f0 (stop) | any owner, attached | H (M slot meaning) |
| Type `SFXEvent_Powered_Active_Loop` | BaseShield 0x56ac00, 0x56b540 | any owner, attached loop | H |
| Type `SFXEvent_Beacon_Placed` | PlaceBeaconEvent 0x436540 | friendly beacon; 2D | H |
| Type `SFXEvent_Cough_Override` | InfectionBehavior 0x636e80 | any owner, attached, 1 in 5 every 60 frames (synced RNG) | H |
| Type `TSW_SFXEvent_GUI_Bttton_Press` | 0x42b8d0 (from HUD 0x2f4f30) | local player; 2D | H |
| Unit_Ability `SFXEvent_GUI_Unit_Ability_Activated`, `SFXEvent_GUI_Unit_Ability_Deactivated` | HUD ability button 0x307190 | local player; 2D | H |
| Unit_Ability `SFXEvent_Target_Ability` | ability activation 0x3a3e20 (from Set_Unit_Ability_Active 0x38a350) | any owner, attached to the unit | H (M gate) |
| Unit_Ability `SFXEvent_Special_Ability_Loop` | **open**: no reader found | | M |
| Attack_Action `Attack_SFXEvent` | AttackAction 0x71ed40 (from update 0x71ef10) | hero clash actor, attached, every attack repeat | H |
| Buzz_Droids_Ability `Activate_SFX` | fill 0x71c430 → BuzzDroidsBehavior 0x6379a0 | local droid owner only, attached | H |
| Proximity_Mines_Ability `Activate_SFX` | fill 0x71a650 → ProximityMineBehavior 0x6373b0 | any owner, attached to each mine when it arms | H |
| Laser_Defense_Ability `Zap_SFXEvent` | 0x716230 (from slot 4 0x716730) | any owner, attached, once per recharge | H |
| Radioactive_Contaminate_Ability `SFXEvent_Radioactive` | slot 4 0x7128a0 | any owner, attached to the spawned object | H |
| Tactical_Bribe_Ability `SFXEvent_Success`, `SFXEvent_Fail` | 0x5b07c0 | any owner, attached to the briber | H |
| Tractor_Beam_Attack_Ability `SFXEvent_Unit_Summoned` (really `Summon_Ability`) | SummonAbility slot 4 0x71cff0 → 0x48e810 | any owner, on the summoned unit | H |
| Movie `SFXEvent_Frame`, `SpeechEvent_Frame` | Bink frame tick 0x44dea0 → 0x5dbd60 / 0x5dbe70 | 2D / speech, first cue in each frame step | H |
| GameConstants `Speech_Text_Color` | 0x3639d0 (hologram subtitle), 0x4614a0 (SCREEN_TEXT) | text colour, default white | H |
| DynamicTrack `Files`, `Volume_Percent` (really **MusicEvent** fields) | music update 0x2d2ab0 | playlist and volume byte | H |

## Faction.SFXEvent_Tactical_Gain_Friendly_Control family (capture point control changes)

Settles `SFXEvent_Tactical_Gain_Friendly_Control` (+0x22c8), `SFXEvent_Tactical_Lose_Friendly_Control` (+0x22b0),
`SFXEvent_Tactical_Gain_Enemy_Control` (+0x22f8), `SFXEvent_Tactical_Lose_Enemy_Control` (+0x22e0). [H]

- **Value shape.** Each tag is a per-object-type list (type code 0x22): every XML line is `ObjectType, SFXEvent`, e.g.
  `Reinforcement_Point, RHD_Landing_Zone_Captured`, `Skirmish_Build_Pad, ...`, `Secondary_Mining_Facility, ...`,
  `N_Gravity_Well_Station, ...`. Getters: 0x33e7d0 Gain_Friendly, 0x33e8b0 Lose_Friendly, 0x33e760 Gain_Enemy,
  0x33e840 Lose_Enemy. The first entry whose type equals the captured object's type wins; no match = silent.
- **Reader.** Capture complete 0x500e30, reached only from CapturePointBehaviorClass service 0x501980 (vtable 0x87b5d0,
  slot 6), when a capture point changes owner. "Control" = ownership of a capture point object (reinforcement point,
  build pad, secondary structure, gravity well station and so on).
- **Friendly half** (L = local player): runs when L is friendly to the old owner or to the new owner. Friendly to the
  old owner → `Lose_Friendly`; otherwise → `Gain_Friendly`. When the type's radar flag (type +0x1788) is set, a radar
  event (0x355ca0) is raised first.
- **Enemy half:** if the old owner's faction is not `Is_Neutral` (+0x108) and L is hostile to the old owner →
  `Lose_Enemy`. Otherwise, if the new owner's faction is neutral → stop. Otherwise, if L is hostile to the new owner →
  `Gain_Enemy`.
- Lists come from the **local** player's faction. 2D, not forced. One friendly and one enemy line can both play for the
  same capture. No cooldown.

## Faction.SFXEvent_HUD_Reinforcement_Point_Contested

Settles `SFXEvent_HUD_Reinforcement_Point_Contested` (+0x2628). [H]

- Reader: CapturePointBehavior service 0x501980, at the moment capture progress starts from 0 with a new controller.
- Conditions: the point has the REINFORCEMENT_POINT behaviour (obj +0x37b ≠ −1); its owner is the local player; the new
  pending controller is the **neutral** player (the point is being neutralised, the first half of an enemy capture); L
  is friendly to the owner or to the controller.
- Local faction, 2D. Once per capture attempt (progress restarting from 0).

## Faction.SFXEvent_HUD_Landing_Zone_Captured family (retail-unused)

Settles `SFXEvent_HUD_Landing_Zone_Captured` (+0x2348), `SFXEvent_HUD_Landing_Zone_Lost` (+0x2358),
`SFXEvent_HUD_Last_Landing_Zone_Lost` (+0x2368), `SFXEvent_HUD_Build_Pad_Captured` (+0x2378),
`SFXEvent_HUD_Build_Pad_Lost` (+0x2388): **retail-unused** [H]. Every displacement hit is the faction ctor 0x33a580,
dtor 0x33c160 / 0x33c342 or resolver 0x330450. Shipped Factions.xml routes the same lines through the per-type control
lists above (`Reinforcement_Point` / `Skirmish_Build_Pad` entries of `SFXEvent_Tactical_*_Control`).

## Faction.SFXEvent_HUD_Won_Land_Battle family (battle won / lost)

Settles `SFXEvent_HUD_Won_Land_Battle` (+0x23f8), `SFXEvent_HUD_Lost_Land_Battle` (+0x2408),
`SFXEvent_HUD_Won_Space_Battle` (+0x2418), `SFXEvent_HUD_Lost_Space_Battle` (+0x2428),
`SFXEvent_HUD_Won_Tactical_Corruption_Mission` (+0x2438), `SFXEvent_HUD_Lost_Tactical_Corruption_Mission` (+0x2448),
`SFXEvent_HUD_Won_Land_Battle_Enemy_TSW_Present` (+0x2458), `SFXEvent_HUD_Lost_Land_Battle_Enemy_TSW_Present` (+0x2468),
`SFXEvent_HUD_Won_Space_Battle_Enemy_TSW_Present` (+0x2478), `SFXEvent_HUD_Lost_Space_Battle_Enemy_TSW_Present`
(+0x2488). [H]

- **TSW** = tactical super weapon: an object with the TACTICAL_SUPER_WEAPON behaviour (0x2c), the Death Star / Death Star
  II in shipped data.
- **Reader:** 0x343810(monitor, winner, local), called by the victory declare 0x343b90. 0x343b90 runs it only for
  victory types 0xb..0x11 and 0x12..0x16, only when the monitor's mode is the current mode, and not when the victory
  record's silent byte (+0x15) is set (BATTLE_TRANSITIONS.md, declare section).
- **Flags computed first:**
  - *enemy TSW*: some enemy of the local player owns a TACTICAL_SUPER_WEAPON object (query 0x2a9ff0);
  - *own killer*: the local player owns a live, unhidden object (obj +0x3a0 bits 0x40 / 0x80 / 0x02 clear) whose type has
    `Is_Super_Weapon_Killer` (type +0x1f).
  - Quirk: the player loop stops at the first enemy with a TSW, so the own-killer check has only seen players with a
    lower index at that point. [H on the loop, M on the effect]
- **Winner is the local player:**
  - land: enemy TSW → `_Won_Land_Battle_Enemy_TSW_Present`; else corruption mission (0x28ae30) →
    `_Won_Tactical_Corruption_Mission`; else `_Won_Land_Battle`;
  - space: enemy TSW and own killer → `_Won_Space_Battle_Enemy_TSW_Present`; enemy TSW without a killer →
    `Space_Forced_Retreat_Due_To_Superweapon` (+0x2180, not in this batch); else corruption →
    `_Won_Tactical_Corruption_Mission`; else `_Won_Space_Battle`.
- **Local player hostile to the winner:**
  - land: enemy TSW → `_Lost_Land_Battle_Enemy_TSW_Present`; else corruption → `_Lost_Tactical_Corruption_Mission`;
    else `_Lost_Land_Battle`;
  - space: enemy TSW → `_Lost_Space_Battle_Enemy_TSW_Present`; else corruption → `_Lost_Tactical_Corruption_Mission`;
    else `_Lost_Space_Battle`.
- An ally of the winner (relation 0, not the winner) hears nothing.
- Local faction, 2D. Once per victory declare.

## Faction.Music_Event_Tactical_Win family (tactical win / lose music)

Settles `Music_Event_Tactical_Win` (string +0x500, pointer +0x520) and `Music_Event_Tactical_Lose` (string +0x528,
pointer +0x548). [H]

- Getters 0x33e5a0 (win) / 0x33e540 (lose). Both first try the `_Vs_Faction` lists (+0x5a8 / +0x5c0, entries keyed by
  the opposing faction), then the plain field.
- Reader 0x28f250(GMM, winner, loser), called from the victory declare 0x343b90 and from the battle-end dialog 0x88fe0.
  - Local player id equals the winner id → the winner's faction win music, music mode 9.
  - Otherwise → the lose music of the **winner's** faction, mode 10.
  - Skipped when the music player is already in that mode. Not forced, no resume.
- Quirk: an allied win (local player on the winning side but not the winner) gets lose music.
- So music modes 9 / 10 are "tactical win / lose" (AUDIO §2.3 should say so).

## Faction.Music_Event_Strategic_Win family (retail-unused)

Settles `Music_Event_Strategic_Win` (string +0x550, pointer +0x570) and `Music_Event_Strategic_Lose` (string +0x578,
pointer +0x598): **retail-unused** [H].

- No hit of +0x550 / +0x570 / +0x578 / +0x598 has a faction base outside the ctor / dtor / resolver. The other hits are
  ButtonComponent and other classes with their own fields at those offsets.
- The galactic end-of-game music comes from AudioConstants instead: `Music_Event_Battle_End_Summary_Screen_Win` /
  `_Lose` (AudioConstants 0x140b30160 +0xe8 / +0x110), played in music mode 16 by the battle-end dialogs 0x87770 / 0x88050.
- The port's aud2 strategic win / lose hookup therefore has no retail counterpart.

## Faction.Land_Retreat_Begin_SFXEvent family (retreat)

Settles `Land_Retreat_Begin_SFXEvent` (+0x21d0), `Land_Retreat_Enemy_Begin_SFXEvent` (+0x21e0),
`Land_Retreat_Cancel_SFXEvent` (+0x21f0), `Land_Retreat_Not_Allowed_SFXEvent` (+0x2200),
`Land_Retreat_Not_Allowed_Reason_1_SFXEvent` (+0x2210), `Land_Retreat_Not_Allowed_Reason_2_SFXEvent` (+0x2220),
`Land_Retreat_Not_Allowed_Reason_3_SFXEvent` (+0x2230), `Space_Retreat_Begin_SFXEvent` (+0x2100),
`Space_Retreat_Enemy_Begin_SFXEvent` (+0x2110), `Space_Retreat_Cancel_SFXEvent` (+0x2120),
`Space_Retreat_Not_Allowed_SFXEvent` (+0x2130), `Space_Retreat_Not_Allowed_Reason_1_SFXEvent` (+0x2140),
`Space_Retreat_Not_Allowed_Reason_2_SFXEvent` (+0x2150), `Space_Retreat_Not_Allowed_Reason_3_SFXEvent` (+0x2160). [H]

The retreat coordinators: LandRetreatCoordinatorClass and the space coordinator; vtable slot 14 = begin, slot 15 =
cancel. All lines below are 2D.

- **Begin**, land 0x5350d0 (countdown = `Land_Retreat_Countdown_Seconds` +0x2190 × FPS), space 0x533080 (+0x20b0):
  - retreater is the local player → the first retreating unit whose type sets `SFXEvent_Retreat_Start` (type +0x1250);
    if none does, the **local** faction's `Land_Retreat_Begin_SFXEvent` / `Space_Retreat_Begin_SFXEvent`;
  - local player hostile to the retreater → local faction's `Land_Retreat_Enemy_Begin_SFXEvent` /
    `Space_Retreat_Enemy_Begin_SFXEvent`;
  - allies of the retreater: nothing.
- **Cancel:**
  - land 0x534f40: only when the cancel is the player's (argument 1) [M on the meaning], the retreat state is 1 and the
    retreater is local → type `SFXEvent_Retreat_Cancel` (+0x1260) if set, else local faction's
    `Land_Retreat_Cancel_SFXEvent`;
  - space 0x532ee0: the type's `SFXEvent_Retreat_Cancel` only for a local retreater. Otherwise the **retreater's**
    faction `Space_Retreat_Cancel_SFXEvent` plays for any retreater, also for non-player cancels, unless the state is 3.
    Quirk: everyone hears an enemy's space retreat being cancelled, in the enemy's faction voice.
- **Not allowed:** the HUD retreat button (land 0x30d5d0 / 0x30d710, space 0x30e1b0 / 0x30e2f0) asks 0x340920
  (coordinator, local id, 1). A non-zero refusal code plays the local faction's `*_Retreat_Not_Allowed_SFXEvent`, and
  then also, by code:
  - code 3 → `_Reason_1_` [M: land, blocked state with no active entry, 0x2825e0];
  - code 4 → `_Reason_2_` [M: a RetreatPreventionAbility is active, 0x70d120 stores 4 via 0x48e1e0];
  - code 6 → `_Reason_3_`: the player owns a TACTICAL_SUPER_WEAPON in space (0x282750).
  Other codes (1 = not local / not a participant / game type; 2 = the retreat-allowed countdown still runs; 5 = already
  retreating) play only the plain `Not_Allowed` line.

## Faction.Land_Surrender_SFXEvent family (retail-unused)

Settles `Land_Surrender_SFXEvent` (+0x2240) and `Space_Surrender_SFXEvent` (+0x2170): **retail-unused** [H]. The
only non-ctor hit, 0x42a200, reads object **type** +0x2170 (`TSW_SFXEvent_Weapon_Power_Up`), not the faction.

## Faction.Reinforcements_Selection_SFXEvent family (reinforcements)

Settles `Reinforcements_Selection_SFXEvent` (+0x2250), `Reinforcements_Pick_Landing_Zone_SFXEvent` (+0x2260),
`Reinforcements_Enroute_SFXEvent` (+0x2270), `Reinforcements_Cancelled_SFXEvent` (+0x2280),
`Reinforcements_Requesting_SFXEvent` (+0x2290), `Reinforcements_Ready_SFXEvent` (+0x22a0). All local player only, 2D,
local faction. [H]

- `Selection`: the reinforcement panel opens; 0xdbab0 (IDC_INVADING_FORCES_LIST) and the command bar 0x2e5580.
- `Pick_Landing_Zone`: a unit is picked for drop in 0xdc200 (reached through 0xdc0e0 on IDC_REINFORCE_BUTTON or a list
  event); a greyed item plays the negative-feedback sound instead. Also the command-bar path 0x2ea5d0 [M on the UI
  element].
- `Enroute`:
  - 0x36300: the drop click queues the ReinforceEvent; the pending unit's type `SFXEvent_Fleet_Move` (+0xfb0) replaces
    the faction line when set;
  - 0x595650: station hangar single-ship spawn for a local owner (STATION_HANGARS_SOUND.md).
- `Cancelled`: 0xdc0e0 IDC_CANCEL_BUTTON; 0x343e0 placement cancel with argument 1.
- `Ready`: Player::Update 0x287e70, when the skirmish garrison re-delivery timer (0x280c50,
  `Garrison_Reinforcement_Delay_Seconds` GC +0x2ee4) expires for the local player.
- `Requesting`: **retail-unused** (ctor / dtor / resolver only).

## Faction.SFXEvent_Bombard_Available family (bombard and bombing run)

Settles `SFXEvent_Bombard_Available` (+0x2020), `SFXEvent_Bombard_Ally_Available` (+0x2030),
`SFXEvent_Bombard_Enemy_Available` (+0x2040), `SFXEvent_Bombard_Cancelled` (+0x2050),
`SFXEvent_Bombing_Run_Available` (+0x1fd0), `SFXEvent_Bombing_Run_Ally_Available` (+0x1fe0),
`SFXEvent_Bombing_Run_Enemy_Available` (+0x1ff0), `SFXEvent_Bombing_Run_Begin_Crosstalk` (+0x2010). [H]

- **Availability.** PlanetaryBombardManager 0x52a110 (common tick) and BombingRunManager 0x495dd0, same scheme.
  - For each player P whose available flag goes from 0 to 1 while the gate (bombard 0x528970, bombing run 0x494410)
    holds. Only the first announcement per manager update; frame > 0.
  - P is local → `_Available`; local friendly to P → `_Ally_Available`; local hostile to P → `_Enemy_Available`.
  - All three come from **P's** faction, not the local faction (disassembly 0x52a9da..0x52aa1a). 2D.
- `SFXEvent_Bombing_Run_Begin_Crosstalk`: bombing-run launch 0x492d60 (from 0x5246d0 / 0x6e9d90). The first bomber's
  owner must be friendly to the local player; that owner's faction line plays attached to the first bomber.
- `SFXEvent_Bombard_Cancelled`: targeting toggled off (0x2e7bc0) and the HUD cancel (0x2f6c30). Local faction, 2D.
  (`SFXEvent_Bombing_Run_Cancelled` +0x2000 is the same in 0x2e7950 / 0x2f6c30; not in this batch.)
- The object type tags of the same feature are in `GameObjectType.SFXEvent_Bombard_Incoming` below.

## Faction.SFXEvent_HUD_Special_Weapon_Ready family (special weapons)

Settles `SFXEvent_HUD_Special_Weapon_Ready` (+0x24d8), `SFXEvent_HUD_Enemy_Special_Weapon_Ready` (+0x2520),
`SFXEvent_HUD_Special_Weapon_Firing` (+0x2508), `SFXEvent_HUD_Enemy_Special_Weapon_Firing` (+0x2550),
`SFXEvent_HUD_Special_Weapon_Charging` (+0x24f0), `SFXEvent_HUD_Enemy_Special_Weapon_Charging` (+0x2538), and object
type `SFXEvent_Special_Weapon_Ready` (+0x1310). [H]

- **Value shape:** each faction tag is a list; every XML occurrence appends one event (FoC: hypervelocity gun, ion
  cannon, an empty third). Index i = the weapon type's `Special_Weapon_Target_Action_Index` (type +0x1de8).
  Getters: Ready 0x33e690, Enemy_Ready 0x33e630, Firing 0x33e660, Enemy_Firing 0x33e600.
- **Ready**, SpecialWeaponBehaviorClass 0x492010 (vtable slot 6): when the ready frame passes, only if 0x527620 holds:
  - owner is local or friendly → the weapon type's `SFXEvent_Special_Weapon_Ready` if set, else the local faction's
    `SFXEvent_HUD_Special_Weapon_Ready`[i];
  - owner hostile → local faction's `SFXEvent_HUD_Enemy_Special_Weapon_Ready`[i];
  - then the ready frame is cleared (once per recharge) and notify 0x1f is sent. 2D.
- **Firing**, 0x491cd0 (from 0x40a3b0 / 0x576220 / 0x6e6ad0): `SFXEvent_HUD_Special_Weapon_Firing`[i] when the shooter
  is local or friendly, `SFXEvent_HUD_Enemy_Special_Weapon_Firing`[i] when hostile. Local faction, 2D. The shooter
  type's `SFXEvent_Fire` plays 3D at the shooter as well.
- `SFXEvent_HUD_Special_Weapon_Charging` and `SFXEvent_HUD_Enemy_Special_Weapon_Charging`: **retail-unused** (no getter;
  ctor / dtor / resolver only).
- Type `SFXEvent_Special_Weapon_Ready` has a second reader: LobbingSuperweaponBehaviorClass 0x4ea6f0 (slot 6), for a
  local owner, with a base-power check (type +0x8b, 0x527520). 2D.

## Faction.SpeechEvent_Super_Weapon_Moving_Range_60_Seconds family (super weapon approach)

Settles `SpeechEvent_Super_Weapon_Moving_Range_60_Seconds`, `SpeechEvent_Super_Weapon_Moving_Range_30_Seconds`,
`SpeechEvent_Super_Weapon_Moving_Range_15_Seconds`, `SpeechEvent_Super_Weapon_Moving_Range_05_Seconds`,
`SpeechEvent_Super_Weapon_Enemy_Moving_Range_60_Seconds`, `SpeechEvent_Super_Weapon_Enemy_Moving_Range_30_Seconds`,
`SpeechEvent_Super_Weapon_Enemy_Moving_Range_15_Seconds`, `SpeechEvent_Super_Weapon_Enemy_Moving_Range_05_Seconds`,
`SpeechEvent_Super_Weapon_Moved_Into_Range`, `SpeechEvent_Super_Weapon_Enemy_Moved_Into_Range`,
`SpeechEvent_Super_Weapon_Moving_Into_Range`, `SpeechEvent_Super_Weapon_Enemy_Moving_Into_Range`. [H]

Speech fields: the row offsets are the strings; the readers use the pointers at +0x20. Own countdown pointers
+0x1bc8 / +0x1bf0 / +0x1c18 / +0x1c40 (60 / 30 / 15 / 5 s), enemy +0x1cb8 / +0x1ce0 / +0x1d08 / +0x1d30; moved into
range +0x1c68 / enemy +0x1d58.

- "Range" = the TSW's attack distance from its target (`TSW_Attack_Distance_From_Target`, type +0x2140): the Death Star
  is moving toward the planet / target it will fire on.
- **Countdowns**, 0x42b560, called from the TSW service 0x42a7b0 in state 3 (moving to firing position):
  - ETA = remaining distance / max speed / FPS − 2 s;
  - when the ETA crosses 60, 30, 15 or 5 s in a step, the highest threshold crossed in that step plays (once each);
  - local relation to the TSW owner ≠ 1 (own or ally) → `SpeechEvent_Super_Weapon_Moving_Range_NN_Seconds`; enemy →
    `..._Enemy_Moving_Range_NN_Seconds`.
- **In range**, 0x42a7b0: `SpeechEvent_Super_Weapon_Moved_Into_Range` (own / ally) or `_Enemy_Moved_Into_Range`, once.
- Local faction; queued as speech (FIFO).
- `SpeechEvent_Super_Weapon_Moving_Into_Range` (+0x1b80, pointer +0x1ba0) and `_Enemy_Moving_Into_Range` (+0x1c70,
  pointer +0x1c90): **retail-unused**. Their only non-ctor hits read object type fields at the same offsets.

## Faction.SpeechEvent_Tactical_Intro_Land_Attacker family (tactical intro speech)

Settles `SpeechEvent_Tactical_Intro_Land_Attacker`, `SpeechEvent_Tactical_Intro_Land_Defender`,
`SpeechEvent_Tactical_Intro_Land_Raid_Attacker`, `SpeechEvent_Tactical_Intro_Land_Raid_Defender`,
`SpeechEvent_Tactical_Intro_Space_Attacker`, `SpeechEvent_Tactical_Intro_Space_Defender`. [H]

AUDIO.md §9.7 covers them all: 0x364b20 from the common tactical tick 0x3639d0, local faction and side only, queued as
speech once per battle, 10 s of sim time after the start, campaign battles only. Pointers read there: Land_Attacker
+0x1d80, Land_Defender +0x1da8, Space_Attacker +0x1dd0, Space_Defender +0x1df8, Land_Raid_Attacker +0x1e20,
Land_Raid_Defender +0x1e48 (Last_Location +0x1e70 / +0x1e98). The port only lacks the reader; no new rule.

## Faction.SFXEvent_HUD_Base_Shield_Online family (base shield)

Settles `SFXEvent_HUD_Base_Shield_Online` (+0x23b8), `SFXEvent_HUD_Base_Shield_Offline` (+0x2398),
`SFXEvent_HUD_Enemy_Base_Shield_Online` (+0x23c8), `SFXEvent_HUD_Enemy_Base_Shield_Offline` (+0x23a8),
`SFXEvent_Base_Shield_Absorb_Damage` (+0x690), and object type `SFXEvent_Powered_Active_Loop` (+0x1430). [H]
(TRU1_A_RE_ANSWERS.md §Set_State and §A4.)

- BaseShieldBehavior Set_State 0x56b540:
  - state 2 → 1 (back online) → `SFXEvent_HUD_Base_Shield_Online` when the shield owner is friendly to the local player,
    `SFXEvent_HUD_Enemy_Base_Shield_Online` when hostile;
  - 1 → 2 (offline) → `_Offline` / `_Enemy_Offline` the same way.
  - Local faction, 2D, on the transition only.
- `SFXEvent_Base_Shield_Absorb_Damage`: 0x56ba20, when the shield absorbs a hit. The **shield owner's** faction; 3D at
  the hit (source) position. Any owner.
- Type `SFXEvent_Powered_Active_Loop`: started by BaseShieldBehavior slot 11 (0x56ac00) and started / stopped in
  0x56b540. Attached to the shield generator, with the event's fade in / out. Any owner.

## Faction.SFXEvent_HUD_Gravity_Control_Generator_On family

Settles `SFXEvent_HUD_Gravity_Control_Generator_On` (+0x24b8) and `SFXEvent_HUD_Gravity_Control_Generator_Off`
(+0x24c8). [H] GravityControlFieldBehaviorClass 0x61c700 (slot 6), on the field's on / off state change (power, owner),
local faction, 2D (TRU1_GRAVITY_FIELD.md §1.2).

## Faction.SFXEvent_Tactical_Object_Building_Started family (tactical build pads and selling)

Settles `SFXEvent_Tactical_Object_Building_Started` (+0x2638), `SFXEvent_Tactical_Object_Building_Loop` (+0x2648),
`SFXEvent_Tactical_Object_Building_Complete` (+0x2658), `SFXEvent_Tactical_Object_Sold` (+0x2668), and object type
`SFXEvent_Sold_Tactical` (+0x1480). [H] (L1_S4_REMAINING.md, build pads.)

- **Start**, 0x42dbd0: a local builder (with the build's sfx flag) first hears the built type's
  `SFXEvent_Tactical_Build_Started`, else `SFXEvent_Build_Started` (2D). Then, for any owner, the under-construction
  owner's faction `SFXEvent_Tactical_Object_Building_Started` and `SFXEvent_Tactical_Object_Building_Loop`, both
  attached to the under-construction object.
- **Complete**, 0x42e890: a local builder hears the type's `Tactical_Build_Complete` / `Build_Complete`. The loop is
  stopped on the under-construction object (0x2d5c00). `SFXEvent_Tactical_Object_Building_Complete` plays attached to the
  finished object, owner's faction, any owner.
- **Sell**, TacticalSellBehaviorClass 0x5248b0 (command 34): a local seller hears type `SFXEvent_Sold_Tactical` (2D);
  then, for any owner, the owner's faction `SFXEvent_Tactical_Object_Sold`, 3D at the object's position (fog-gated). The
  UI sell button 0x3471d0 also plays `SFXEvent_Sold_Tactical`.

## Faction.SFXEvent_Tactical_Unit_Cap_Reached family

Settles `SFXEvent_Tactical_Unit_Cap_Reached` (+0x6a0) and `SFXEvent_Tactical_Pop_Cap_Reached` (+0x710). [H]

- `Unit_Cap`: ReinforceEventClass::Execute 0x403cb0 (slot 4), when the issuing player is local and the room test
  0x2b03e0 fails (no free unit slot on the map). Land and space. Local faction, 2D, once per refused drop.
- `Pop_Cap`: P4_SIM_SYSTEMS.md §2.7.3 (Player::Update 0x287e70).

## Faction.SFXEvent_HUD_Tactical_Victory_Near

Settles `SFXEvent_HUD_Tactical_Victory_Near` (+0x2498). [H]

- Reader: the overrun reveal 0x364920. It runs only in the multiplayer game types (1 / 2 / 4), every 15 frames, after
  `Minimum_Tactical_Overrun_Time_In_Secs` (GC +0x15a8, 30 s).
- Condition: weaker side's AI combat power × `Tactical_Overrun_Multiple` (GC +0x15ac, 3.0) < stronger side's.
- Once per side (flags +0x2cd / +0x2ce); played only when the stronger side is the local player's. Local faction, 2D.
- Never in single player or skirmish (FOG_OF_WAR.md, overrun row and §5.4).

## Faction.SFXEvent_HUD_Repairing

Settles `SFXEvent_HUD_Repairing` (+0x24a8). [H] Getter 0x478f0, called by the cursor-action handler 0x41f70 (cases 1 / 2):
the local player clicks a damaged hard point (health < 1, not already in the repair list 0x386120) on a station
(+0x38e). This queues RepairHardpointEventClass (0x430750, kind 0x2d). Local faction, 2D, once per click.

## Faction.SFXEvent_Bunker_Vacated

Settles `SFXEvent_Bunker_Vacated` (+0x750). [H] Garrison eject 0x444dd0, once per leaving unit (callers 0x443ff0 /
0x444ab0 / 0x444c40). Only when the leaving unit's owner is the local player; the owner's faction line, attached to
the unit. (`SFXEvent_Bunker_Garrisoned` +0x740 is 0x4446b0; not in this batch.)

## Faction.SFXEvent_HUD_Advisor_Urgent

Settles `SFXEvent_HUD_Advisor_Urgent` (+0x2f58). [H] (M where marked)

- The field descriptors (table at 0x140a3d550..0x140a3d590) give `SFXEvent_HUD_Advisor_Message`,
  `SFXEvent_HUD_Advisor_Urgent` and `SFXEvent_HUD_Advisor_Hint` the **same** offset +0x2f58. Each tag overwrites it.
  - Every shipped faction (Rebel, Empire, Underworld) lists them in the order Message (`GUI_Droid_Hint_SFX`), Urgent
    (`GUI_R2_Urgent_Hint_SFX`), Hint (`GUI_Droid_Hint_SFX`).
  - So +0x2f58 ends as the Hint value, and the Urgent value is never heard [M: in-document parse order].
- Reader: advisor enqueue 0x2dd630. A non-front (normal) message plays +0x2f58. A front (urgent) message plays +0x2f68,
  which no XML tag fills: it is touched only by the ctor (0x33ba42), the dtor (0x33c486) and 0x2dd698. So urgent
  advisor messages are silent [M]. Local faction, 2D.
- This corrects P4_SIM_SYSTEMS.md §2.7.1, which reads Urgent as +0x2f68.

## Faction.SFXEvent_Tactical_Corruption_Mission_Commencing

Settles `SFXEvent_Tactical_Corruption_Mission_Commencing` (+0x650). [H] AUDIO.md §9.7, last paragraph:
- It plays at the galactic moment a corruption battle is initiated (0x2af7c0 space, 0x2b0520 land), for a local
  initiator who is the pending corruption player.
- It is the 2D fallback when the initiating type has no matching `Corruption_N_Mission_Commencing_Speech_Event` (getter
  0x371450). On land it replaces `SFXEvent_Land_Invasion_Commencing`.

## Faction.SFXEvent_GUI_Toggle_Non_Hero_Ability_On family (ability toggles)

Settles `SFXEvent_GUI_Toggle_Non_Hero_Ability_On` (+0x7a0), `SFXEvent_GUI_Toggle_Non_Hero_Ability_Off` (+0xc70),
`SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_On` (+0x1140), `SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_Off` (+0x1610). [H]

- **Value shape:** type 0x4c, 77 entries × 0x10 indexed by the unit ability enum; XML lines are `ABILITY, SFXEvent`
  (`DEFEND, GUI_Toggle_Shields_On`; `BARRAGE,` with no event = silent). Accessed as (ability + K) × 0x10 with K = 0x7a,
  0xc7, 0x114, 0x161.
- **Reader 1:** SetUnitAbilityModeEventClass::Execute 0x430270 (vtable 0x86a880 slot 4; issued from 0x3a88a0 / 0x570a50
  / 0x5745c0).
  - Runs after at least one unit of the event toggled, when the event's sound flag (+0x25c) is set and DAT_140b155c4 is
    clear.
  - Uses the **issuer's** faction.
  - On, when the issuer's On entry is set: local hostile to the issuer → `SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_On`;
    otherwise (own or ally) → `SFXEvent_GUI_Toggle_Non_Hero_Ability_On`.
  - Off: the same with `_Toggle_Non_Hero_Ability_Off` / `_Enemy_Toggle_Non_Hero_Ability_Off`.
  - 2D. When no unit toggled and the issuer is local with flag +0x25d, the negative-feedback sound plays instead.
- **Reader 2:** AbilityCountdownBehaviorClass 0x42f910 → expiry 0x42f460 (verified in the disassembly). When an active
  phase runs out and the ability auto-deactivates, for a local owner only, it tests that the owner's `_Off` entry is
  set but plays the owner's `_Enemy_Toggle_Non_Hero_Ability_Off` entry. Quirk: a timed-out toggle speaks the enemy line
  to its own player.

## GameObjectType.SFXEvent_Attack_Hardpoint

Settles `SFXEvent_Attack_Hardpoint` (+0x1080). [H]

- **Value shape:** type 0x42, 13 entries × 0x10 indexed by hard point type (`HardPointType`, the hard point record's
  +0x48). Getter 0x373800 (index < 13 else none).
- **Reader 1**, group attack response 0x44590 (from the attack-order UI 0x37250 / 0x3b090 / 0x3b360 / 0x3c220). The
  speaker is the given unit, else the group's first speaker (0x38290). The first that exists wins:
  1. the attacked hard point is known and the speaker's type has `SFXEvent_Attack_Hardpoint` for its hard point type;
  2. the group has more than one free unit (units in a container or garrison not counted [M]) and the type sets
     `SFXEvent_Group_Attack` (+0x1040);
  3. the type's attack response for the target's type (0x373820);
  4. the type's generic attack response (0x373700).
  It plays attached to the speaker. Afterwards a free-RNG pick (up to 50 tries) of another group member of a different
  type adds that member's `SFXEvent_Assist_Attack` (+0x1060) as a follow-up (0x2d4200).
- **Reader 2**, single-unit response 0x44cd0: only for a unit owned by the local player; the same chain without step 2
  and without the assist line.
- It replaces the normal attack response; no cooldown beyond the event's own.

## GameObjectType.SFXEvent_Hardpoint_Destroyed family (hard point destroyed)

Settles `SFXEvent_Hardpoint_Destroyed` (+0x1150), `SFXEvent_Hardpoint_All_Weapons_Destroyed` (+0x1070), and HardPoint
`Death_Explosion_SFXEvent` (+0x160). [H] (M where marked)

- Reader: hard point destroyed 0x386660.
- **Announcement:** only when the parent's owner is the local player, the parent is alive, and the destroy reason
  argument is neither −1 nor 6 [M on what those values mean].
  - Weapon hard point kinds 5..10 with none of those left on the parent → `SFXEvent_Hardpoint_All_Weapons_Destroyed`.
  - Otherwise, when no hard point of the destroyed one's type is left → the parent type's `SFXEvent_Hardpoint_Destroyed`
    entry for that hard point type (getter 0x373aa0, same 13-entry shape as above).
  - 2D, from the parent's type.
- **Explosion:** HardPoint `Death_Explosion_SFXEvent` (name +0x160, resolved event pointer read at +0x248 [M]) plays 3D
  at the hard point's bone position, with the parent passed as the source. Any owner. If unset, the
  `Death_Breakoff_Prop` type's (+0x440) `Death_SFXEvent_Start_Die` plays instead.

## GameObjectType.Land_Damage_SFX

Settles `Land_Damage_SFX` (+0xed8). [H] 0x3ac290, called from the damage paths (0x3ab890 and others). When the object's
land damage stage changes (stage thresholds +0xeb0, count +0xeb8, alternates +0xec8), that stage's `Land_Damage_SFX`
name plays by name, attached to the object (0x2d50e0), unless it is "NULL" [M on the compare]. Any owner, once per
stage change.

## GameObjectType.SFXEvent_Health_Low_Warning family (health warnings)

Settles `SFXEvent_Health_Low_Warning` (+0x1320), `SFXEvent_Health_Critical_Warning` (+0x1330),
`SFXEvent_Enemy_Damaged_Health_Low_Warning` (+0x1340), `SFXEvent_Enemy_Damaged_Health_Critical_Warning` (+0x1350). [H]

- Reader: Take_Damage 0x3a9e30, when one hit takes the victim's health across a threshold. The critical threshold is
  tested first (`Health_Critical_Percent_Threshold` GC +0x11b3c, 0.10), then the low one
  (`Health_Low_Percent_Threshold` GC +0x11b38, 0.33).
- Victim owned by the local player → the victim type's `SFXEvent_Health_Critical_Warning` / `_Low_Warning`.
- Otherwise, attacker owned by the local player (for a projectile, its shooter) → the attacker type's
  `SFXEvent_Enemy_Damaged_Health_Critical_Warning` / `_Low_Warning`.
- 2D; one line per crossing hit; no cooldown.

## GameObjectType.SFXEvent_Tactical_Build_Started family (tactical production)

Settles `SFXEvent_Tactical_Build_Started` (+0x1490), `SFXEvent_Tactical_Build_Complete` (+0x14a0),
`SFXEvent_Tactical_Build_Cancelled` (+0x14b0), and `Build_Speech_Countdowns` (+0x15c8). [H]

The local owner hears the `Tactical_*` variant in a tactical mode and the plain `Build_*` variant in galactic. A
`Build_Speech_*` speech, when set, replaces both.
- **Queue add** 0x496a20 (verified at 0x496d20): type `Build_Speech_Underway` (pointer +0x1570) queued as speech, else
  `SFXEvent_Tactical_Build_Started`, else `SFXEvent_Build_Started` (+0x1450). The HUD path 0x3caae0 makes the same choice.
- **Complete:** build manager 0x498170 (from Player::Update 0x287e70) and 0x402840 (+0x1598 at 0x402aff):
  `Build_Speech_Completed` (pointer +0x1598), else `SFXEvent_Tactical_Build_Complete`, else `Build_Complete`.
- **Cancel:** 0x498820, from the cancel-event executors 0x4035d0 / 0x403770 / 0x403900: `Build_Speech_Stopped` (pointer
  +0x15c0), else `SFXEvent_Tactical_Build_Cancelled`, else `SFXEvent_Build_Cancelled` (+0x1470).
- Build pads use 0x42dbd0 / 0x42e890 (Faction.SFXEvent_Tactical_Object_Building_Started family).
- 2D.
- `Build_Speech_Countdowns`: MCX4_T09_RE_ANSWERS.md Q7 (0x401e50).

## GameObjectType.Corruption_1_Success_Speech_Event family (corruption speech)

Settles `Corruption_1_Success_Speech_Event` (+0x2508), `Corruption_2_Success_Speech_Event` (+0x26a8),
`Corruption_3_Success_Speech_Event` (+0x2848), `Corruption_1_Failure_Speech_Event` (+0x2550),
`Corruption_2_Failure_Speech_Event` (+0x26f0), `Corruption_3_Failure_Speech_Event` (+0x2890),
`Corruption_1_Mission_Commencing_Speech_Event` (+0x25b8). [H] (M where marked)

- **Which object:** the corrupting unit's type, i.e. the Underworld hero or unit that started the corruption mission.
  Slot N is used when the type's `Corruption_N_Type` equals the pending corruption type (0x140b155d8; N = 1, 2, 3).
- Getters: Success 0x3716f0 (pointers +0x2528 / +0x26c8 / +0x2868), Failure 0x3713d0 (+0x2570 / +0x2710 / +0x28b0),
  Commencing 0x371450 (+0x25d8 / +0x2778 / +0x2918), hologram movie names 0x371650 / 0x371370.
- **Mission end**, 0x28bf10 (from the common tactical tick 0x3639d0):
  - the winner is the pending corruption player (GMM +0x1f4) → success. For a local corruptor with flag +0x2e4 [M], the
    external hologram 0x362510 shows the success Bink and queues the success speech;
  - otherwise → the failure speech and hologram, for a local corruptor.
- **Direct corruption (no battle):** 0x52bf50 (from 0x4b4d70 / 0x52b160) takes the same success path.
- **Commencing:** AUDIO.md §9.7 (Faction.SFXEvent_Tactical_Corruption_Mission_Commencing above).
- Speech queue; the hologram subtitle uses `Speech_Text_Color` (GameConstants.Speech_Text_Color below).

## GameObjectType.SFXEvent_Move_Into_Asteroid_Field family

Settles `SFXEvent_Move_Into_Asteroid_Field` (+0x13b0) and `SFXEvent_Move_Into_Nebula` (+0x13c0). [H]

- Readers: the move-order UI responses 0x3bf20 / 0x442f0 / 0x44ba0, for a unit owned by the local player in a space
  tactical mode.
- When the move destination is inside an asteroid field (0x437150) or a nebula (0x4379a0), that line replaces the normal
  move response (0x3738b0). Attached to the unit.
- Galactic moves use `SFXEvent_Fleet_Move` (+0xfb0) instead.

## GameObjectType.SFXEvent_Turret_Rotating_Loop

Settles `SFXEvent_Turret_Rotating_Loop` (+0x1440). [H] 0x3a8080 (from the turret update 0x632270). It starts attached
to the object, with the event's fade-in, when the turret begins to turn (latch obj +0x3b7), and stops when it stops
turning. Any owner.

## GameObjectType.Play_SFXEvent_On_Sighting

Settles `Play_SFXEvent_On_Sighting` (+0x8f, bool). [H]

- Reader: HideWhenFoggedBehavior service 0x53ddc0 (verified at 0x53e07e), when the object becomes visible to the local
  player.
- Flag set → the tactical mode's "unit type spotted" (vf+0x160 = 0x361470): the local faction's
  `SFXEvent_Unit_Type_Spotted` entry for the type (list of `Type, SFXEvent`), else `SFXEvent_Generic_Unit_Spotted`
  (+0x2310). Once per type per battle (set at mode +0x238), and only when no 2D SFX instance is playing.
- Not played that way, the object has no +0x33a slot, and the local player is hostile to the owner → vf+0x168 =
  0x3613a0, `SFXEvent_Enemy_Spotted` (+0x2338): once per battle (mode +0x278), only for attacker / defender objects.
- 2D. See FOG_OF_WAR.md.

## GameObjectType.Spin_Away_On_Death_SFXEvent_Start_Die

Settles `Spin_Away_On_Death_SFXEvent_Start_Die` (+0x12f0). [H] DeathBehavior 0x58cf90, phase 1 of a spin-away death:
attached to the dying object when the event `Is_3D`, else 2D when `Is_2D`. Any owner, once per death.

## GameObjectType.SFXEvent_Damaged_By_Asteroid

Settles `SFXEvent_Damaged_By_Asteroid` (+0x13d0). [H] 0x436920, from the asteroid field service 0x437310: attached to
the damaged object, any owner, on every asteroid damage hit (no cooldown beyond the event's own).

## GameObjectType.SFXEvent_Bombard_Incoming family

Settles `SFXEvent_Bombard_Incoming` (+0x1390) and `SFXEvent_Bombard_Select_Target` (+0x1380). [H]

- `SFXEvent_Bombard_Incoming`: 0x5295f0, from the HUD bombard button 0x2f6c30 when the bombard is launched. It plays
  the local player's bombarding ship type's line, 2D.
- `SFXEvent_Bombard_Select_Target`: **retail-unused** (type ctor 0x367250, copy 0x36c000, dtor 0x36a070 only).

## GameObjectType.Selt_Destruct_SFXEvent_Start_Die

Settles `Selt_Destruct_SFXEvent_Start_Die` (+0x1300; the misspelling is retail's tag). [H] (M on the slot names)
SelfDestructBehaviorClass:
- slot 23 (0x630950) starts it attached to the object when the self-destruct countdown begins;
- slot 24 (0x630840) and 0x6306f0 stop it (0x2d5c00) when the countdown is cancelled or the object blows up.
- Any owner. The death itself then follows the normal death path.

## GameObjectType.SFXEvent_Beacon_Placed

Settles `SFXEvent_Beacon_Placed` (+0x13a0). [H] PlaceBeaconEventClass::Execute 0x436540 (slot 4): when the new beacon
is friendly to the local player (0x39a230), the beacon type's line plays 2D. Once per beacon.

## GameObjectType.SFXEvent_Cough_Override

Settles `SFXEvent_Cough_Override` (+0x1510). [H] InfectionBehaviorClass 0x636e80 (slot 6):
- every 60 frames, for an infected unit that is not garrisoned, a **synced** RNG draw 0x1ffb40(0x140a13e24, 0, 4) == 0
  (1 in 5) plays a cough attached to the unit;
- the sound is the type's `SFXEvent_Cough_Override`, else the event named `Unit_Infantry_Coughing`;
- any owner.
- The draw is on the synced LCG, so a port that plays it must draw from its sim RNG or prove the draw is not part of the
  sim (tier-3 note).

## GameObjectType.TSW_SFXEvent_GUI_Bttton_Press

Settles `TSW_SFXEvent_GUI_Bttton_Press` (+0x2150; retail spelling). [H] 0x42b8d0 (from the HUD TSW button 0x2f4f30):
- the local player presses the super-weapon button; the TSW event is queued (kind 1 from idle state 0, kind 2 from
  state 4) and the line plays 2D;
- when the press is not allowed, the negative-feedback sound plays instead.

## Unit_Ability.SFXEvent_GUI_Unit_Ability_Activated family (HUD ability button)

Settles `SFXEvent_GUI_Unit_Ability_Activated` (+0x180) and `SFXEvent_GUI_Unit_Ability_Deactivated` (+0x190). [H]

- The record is a `<Unit_Abilities_Data><Unit_Ability>` entry. Type vector +0x1ab0, getter 0x374b90(type, 0 or 1).
  The object's per-ability slot (obj + (ability + 0x3d) × 0x10, 0x397d90) points at the same record.
- Reader: the HUD ability button 0x307190 (from 0x2e0730 / 0x2e7ed0 / 0x346ea0), local player only. For the selected
  units of the clicked group:
  - if every unit already has the ability active → it is a deactivate click → `_Deactivated`;
  - otherwise it is an activate click → `_Activated`.
  - The record is the first unit's type's first Unit_Ability when the clicked ability is that type's ability #0, else
    its second.
  - 2D, once per click.
- For targeted abilities (enum 5 and 0x1e, 0x1f, 0x22, 0x25, 0x29, 0x37) an activate click enters targeting mode
  (0x2e0920 / 0x2e0730) after the sound. So `_Activated` plays when targeting starts, not when the target is chosen.
- A click where nothing can toggle plays the negative-feedback sound instead.

## Unit_Ability.SFXEvent_Target_Ability

Settles `SFXEvent_Target_Ability` (+0x170). [H] (M on the gate)

- Reader: the unit-ability activation 0x3a3e20, called by `Set_Unit_Ability_Active` 0x38a350 for the instant
  target / area abilities. Those are types 19, 23..26, 28, 35, 36, 42 (ABILITIES_HARDPOINTS.md §2.5) plus SELF_DESTRUCT
  (39) from 0x630c20.
- After spawning the record's `Spawned_Object_Type` (if any) and setting the ability mode, it plays the record's
  `SFXEvent_Target_Ability` attached to the unit. Any owner.
- Gate: the play is skipped when the SFX manager's id lookup 0x2d4880 finds the event's +0x20 value. Event +0x20 holds the
  CRC of the event's name **as written** (0x522910); the manager's registry is keyed by the CRC of the **upper-cased**
  name (loader 0x2d5df0). So the lookup fails, and the line plays, for every mixed-case event name. All 33 shipped
  `SFXEvent_Target_Ability` events are mixed-case, so it always plays in shipped data; an all-upper-case event name
  would be silenced [M: reads as an intended "already playing" test with the wrong key].
- Targeted abilities that go through other handlers (BARRAGE 0x3a1930, JET_PACK 0x3a38d0 and so on) do not play it.

## Unit_Ability.SFXEvent_Special_Ability_Loop

Settles nothing yet for `SFXEvent_Special_Ability_Loop` (+0x1a0): **open, no reader found** [M].

What was tried:
- Every SFX start / stop call site in the C export: one hit at +0x1a0, 0x7003d0. That is GenericAttackAbilityClass
  slot 4 reading its **own** field (the attack sound played when its delayed damage is applied), not a Unit_Ability
  record, so AUDIO.md §9.6's guess that 0x7003d0 is the loop is wrong.
- The callers of the record getter 0x374b90 and of the per-object slot 0x397d90.
- The intersection of all 458 functions with displacement 0x1a0 and the functions that call an SFX start: none reads a
  Unit_Ability record.

Shipped values (`Unit_Star_Destroyer_Tractor_Beam` ×7, `Unit_Death_Star_2_Fire`, `Unit_Yoda_Force_Sight_Loop`, ...)
suggest a loop while a special ability is active. See "Needs Ghidra".

## Attack_Action.Attack_SFXEvent

Settles `Attack_Action.Attack_SFXEvent` (+0x110). [H]

- `Attack_Action` is a hero clash action (HeroClash.xml, `Clash_Actions` of an `Action_Driven` clash); AttackActionClass
  (vtable 0x8cde20). Slot 4 0x71eb30 clones it.
- Update, slot 7 0x71ef10: the actor faces the target. Each time the attack animation is not running (or has ended) and
  the repeat count (+0x124) is below its limit (+0xe4), 0x71ed40 starts the next repeat:
  - plays `Attack_Animation_Type` / `_Subindex`; if the type has no such animation it tries animation 5 with any index,
    and with neither the action ends;
  - plays `Attack_SFXEvent` attached to the actor;
  - fires `Attack_Projectile_Type` from `Attack_Firing_Bone_Name` at the target.
- Any owner; once per attack repeat.

## Buzz_Droids_Ability.Activate_SFX

Settles `Buzz_Droids_Ability.Activate_SFX` (+0x1a8). [H]

- The ability (BuzzDroidsAbilityClass, fill 0x71c430) spawns the droid object type and writes its data pack:
  - activation frame = now + FPS × `Activation_Time_In_Seconds`;
  - chase and damage radii, own / enemy damage per second;
  - a copy of `Activate_SFX` (pack +0x18).
- BuzzDroidsBehaviorClass service 0x6379a0 (slot 6): on exactly the activation frame, **if the droid's owner is the
  local player**, plays `Activate_SFX` attached to the droid. Other players hear nothing.

## Proximity_Mines_Ability.Activate_SFX

Settles `Proximity_Mines_Ability.Activate_SFX` (+0x1a0). [H]

- ProximityMinesAbilityClass 0x71a650 spawns `Number_Of_Mines` mines of `Mine_Type` around the target point (angle and
  radius from the synced RNG). Each mine's pack gets the arm frame (now + FPS × `Activation_Time_In_Seconds` + 1),
  `Trigger_Radius` and a copy of `Activate_SFX`.
- ProximityMineBehaviorClass service 0x6373b0 (slot 6): on exactly the arm frame, for a live, visible mine, plays
  `Activate_SFX` attached to that mine. Any owner; once per mine (so N mines give N plays on the same frame, subject to
  `Max_Instances`). Trigger checks then run every 5 frames.

## Laser_Defense_Ability.Zap_SFXEvent

Settles `Laser_Defense_Ability.Zap_SFXEvent` (+0x230). [H]

- LaserDefenseAbilityClass slot 4 0x716730 runs every frame while the ability is active. When its recharge counter is
  below 1, it calls 0x716230, then resets the counter to FPS × `Recharge_Time_In_Secs` (+0x228).
- 0x716230 looks for the first enemy projectile whose category is in `Projectile_Types_Targeted` within
  `Protection_Radius`. For it, it plays `Zap_SFXEvent` attached to the user, draws the laser beam and destroys the
  projectile.
- Any owner; at most one zap per recharge.

## Radioactive_Contaminate_Ability.SFXEvent_Radioactive

Settles `SFXEvent_Radioactive` (+0x1c8). [H] RadioactiveContaminateAbilityClass slot 4 (0x7128a0): when the ability
spawns its contamination object, the sound plays attached to that object. Any owner; once per spawn [M on the exact
moment inside the slot].

## Tactical_Bribe_Ability.SFXEvent_Success family

Settles `Tactical_Bribe_Ability.SFXEvent_Success` (+0x1a8) and `Tactical_Bribe_Ability.SFXEvent_Fail` (+0x1b8). [H]

TacticalBribeAbilityClass slot 30, 0x5b07c0, when the bribe is applied to a target that passes the ability's target
checks.
- Cost = the target's (or its container's) type bribe cost (type +0x2358) × the user's type multiplier (+0x23e0 or
  +0x23e4, chosen by the game mode); a cost of 1 or less counts as 0.
- No player, cost above the player's credits, or cost 0 → `SFXEvent_Fail`, attached to the bribing unit, and nothing
  else happens.
- Otherwise the credits are spent, `SFXEvent_Success` plays attached to the bribing unit, and the target is converted.
- Any owner; once per use.

## Tractor_Beam_Attack_Ability.SFXEvent_Unit_Summoned

Settles `SFXEvent_Unit_Summoned` (+0x198). [H] The ledger's class is a segment-boundary slip of the field-table
generator: the descriptor entry (0x140a431f0) sits after `Object_Type` / `Creation_Delay_In_Seconds`, and the only XML
parent is `Summon_Ability`.
- SummonAbilityClass slot 4 0x71cff0: when the summon countdown on the target reaches 0, it spawns the summoned type and
  plays `SFXEvent_Unit_Summoned` on the new unit through the ability helper 0x48e810. That helper plays the event 2D when
  it is `Is_2D`, else attached.
- Any owner; once per summon.

## Movie.SFXEvent_Frame family (movie frame cues)

Settles `Movie.SFXEvent_Frame` (+0x68) and `Movie.SpeechEvent_Frame` (+0x78). [H]

- **Value shape:** each XML line is `EventName, Frame` (e.g. `Empire_Land_Win_Line_1, 60`). Repeated tags build a list
  per field: list head at the offset, count at offset + 8. `MusicEvent_Frame` (+0x88) is the same for music (no
  shipped uses).
- **Reader:** the Bink movie tick 0x44dea0 (from 0x60330), once per decoded Bink frame for the current movie record
  (0x140b306d0):
  - F_prev = Bink's frame number before the advance; F = the number after it;
  - the caption check (subtitle option on) runs first;
  - SFX cues 0x5dbd60: the first list entry, in XML order, whose frame lies in (F_prev, F] plays 2D, not forced
    (0x2d5290);
  - music cues 0x5dbc40: the same, music mode 17;
  - speech cues 0x5dbe70: the same, but all current speech is stopped (0x321830) and then the cue is queued, so it
    starts at once.
- Bink advances one frame per step, so a cue fires on the Bink frame whose number equals its frame value (Bink numbers
  frames from 1 [M]).
- Only one cue per list per step: two cues with the same frame → only the first in list order plays [H on the loop,
  M on it mattering].
- Movies that end (not looping, last frame reached) stop before cues of that step are checked.

## GameConstants.Speech_Text_Color

Settles `Speech_Text_Color` (GC +0x1280). [H]

- Default (0x2c91c0): 255, 255, 255, 255 (opaque white). The FoC XML sets 255 255 255.
- Reader 1, the common tactical tick 0x3639d0 (REA xref 0x363de4): the **external hologram subtitle**. While a hologram
  speech plays (mode +0x2d4 = speech ticket, mode +0x2d8 = its event; set by the intro command-bar movie of AUDIO §9.7
  step 5 and by the corruption hologram 0x362510):
  - once that event is the head of the speech queue, its text is shown as a persistent HUD line (no timeout) in
    `Speech_Text_Color`;
  - when the speech ends, the Bink stops and the line is removed.
  - No subtitle-option gate at this site [M].
- Reader 2: the SCREEN_TEXT story reward 0x4614a0 (xref 0x461553), colour of the ALIEN_SPEECH and unnamed channels
  (STORY_PRESENTATION_REWARDS.md).
- AUDIO §3's normal speech queue shows no text by itself.

## DynamicTrack.Files family (really MusicEvent fields)

Settles `DynamicTrack.Files` (+0x20) and `DynamicTrack.Volume_Percent` (+0x3c). [H]

The registration list 0x515ee0 gives each descriptor table's start and size, which fixes the boundaries that
`xml_field_tables.md` guessed:

| table | size | fields | owner |
|---|---|---|---|
| 0x140a33a50 | 9 entries | `texture_name`, `width` +0x20, `opacity`, `segment_length`, `Render_Mode`, `fade_*`, `min_geometry_lod` | DynamicTrack |
| 0x140a33b70 | 6 entries | `Files` +0x20, `Volume_Percent` +0x3c, `Fade_In_Seconds` +0x40, `Fade_Out_Previous_Seconds` +0x44, `Loop` +0x3d, `MSS_Internal_Loop` +0x3e | **MusicEvent** (exactly MusicEvents.xml's tag set) |
| 0x140a33c30 | 3 entries | `Files` +0x40, `Text_ID` +0x28, `Volume_Percent` +0x58 | **SpeechEvent** (SpeechEvents.xml's set; `Overlap_Test` has no entry) |

- So the ledger rows `DynamicTrack.Files` / `DynamicTrack.Volume_Percent` are the MusicEvent fields. The rows filed
  under MusicEvent (+0x40 / +0x28 / +0x58) are the SpeechEvent fields.
- **MusicEvent reader**, music update 0x2d2ab0:
  - `Files` = vector of 0x20-byte name strings (begin +0x20, end +0x28); the file counter steps through it;
  - `Volume_Percent` is read as one byte / 100 and passed to the stream start 0x22c030 with `Fade_In_Seconds`,
    `Fade_Out_Previous_Seconds` and `MSS_Internal_Loop`; `Loop` (+0x3d) is tested at stream end. AUDIO.md §2.1 / §2.2
    describe it.
  - MusicEvent has **no** `Text_ID` field; AUDIO §2.1's `Text_ID` row belongs to SpeechEvent.
- **SpeechEvent reader**, speech update 0x321dc0:
  - `Files` (+0x40 / +0x48) is picked with the **free** RNG 0x1ffb40(0x140a13e20, 0, n − 1);
  - `Volume_Percent` (byte +0x58) / 100 is the stream gain (0x22ca30);
  - `Text_ID` (+0x28) is the subtitle text read by 0x320cb0 (see Speech_Text_Color).
  - AUDIO.md §3.1 already describes this behaviour.

## Corrections to other specs (for the lane to apply or file)

- AUDIO.md §2.3: music modes 9 / 10 are tactical win / lose (0x28f250).
- AUDIO.md §2.1: MusicEvent has no `Text_ID`; that field is SpeechEvent's.
- AUDIO.md §9.6, unit-ability row:
  - `_Activated` / `_Deactivated` are settled (0x307190);
  - `SFXEvent_Target_Ability` is 0x3a3e20;
  - 0x7003d0's +0x1a0 is GenericAttackAbilityClass's own sound, not `SFXEvent_Special_Ability_Loop`.
- P4_SIM_SYSTEMS.md §2.7.1: Advisor Message / Urgent / Hint all write +0x2f58 (Hint wins in shipped data); urgent
  messages play +0x2f68, which is never set.
- MCX4_T09_RE_ANSWERS.md: `Build_Speech_Underway` / `_Completed` / `_Stopped` are not retail-unused. They are read
  through their resolved pointers +0x1570 / +0x1598 / +0x15c0 (0x496a20, 0x498170, 0x402840, 0x498820); the earlier
  search used the string offsets.
- `xml_field_tables.md`: the DynamicTrack / MusicEvent / SpeechEvent segment boundaries (table above), and
  `Tractor_Beam_Attack_Ability.SFXEvent_Unit_Summoned` is `Summon_Ability`'s.
- Port notes (quirks to keep):
  - the port's strategic win / lose music hookup has no retail counterpart;
  - bombard / bombing-run availability lines come from the available player's faction;
  - a space retreat cancel is heard for non-local retreaters;
  - a countdown-expired toggle plays the Enemy_Off entry;
  - an allied tactical win gets lose music;
  - the cough draw uses the synced RNG.

## Not yet answered

- `Unit_Ability.SFXEvent_Special_Ability_Loop` (+0x1a0). Tried: all SFX start / stop call sites in the C export, the
  callers of 0x374b90 / 0x397d90, and the 0x1a0 displacement users that also call an SFX start. No reader found.

## Needs Ghidra

- `Unit_Ability.SFXEvent_Special_Ability_Loop`. Ask for a typed cross-reference of the Unit_Ability record struct (the
  type +0x1ab0 vector entries and the per-object slot pointers, obj + (ability + 0x3d) × 0x10) to find any read of
  record +0x1a0. Likely places: the special-ability apply paths around 0x48c000..0x492000 and the tractor beam /
  Death Star II fire abilities, which use the shipped loop values. If no read exists, the row becomes retail-unused.
- Optional checks of [M] points: the meaning of the destroy-reason values −1 / 6 in 0x386660; the land retreat cancel
  argument in 0x534f40; refusal codes 3 / 4 in 0x340920; the corruption hologram flag +0x2e4 in 0x28bf10.
