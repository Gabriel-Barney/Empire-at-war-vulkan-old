# mcx4 t13 RE questions: audio cues of the honour gaps

Roadmap group `mcx4` task t13 (lane mcx4-pres); rows: `python -I tools/lint/check_tag_ledger.py --gaps --group audio`
(152 rows, 2026-11-18). Answers go to `docs/re/MCX4_T13_RE_ANSWERS.md`, one greppable section per tag or per family
(`## Faction.SFXEvent_HUD_Won_Land_Battle`; a family section must name every tag it settles), behaviour in words with
[H] / [M] / [L] and RVAs, no decompiled code. The method of `MCX4_RE_ANSWERS_GALACTIC.md` ("Method") applies.

For every tag: **every reader** (RVA; when it plays: the event, which player hears it (local player only? own /
ally / enemy variant), 2D or 3D / attached, a repeat limit or cooldown, what it replaces), or **retail-unused** with
the cross-reference evidence (disp32 search of the field offset; the ledger then cites the section). Group the
answers by trigger site where one function plays several tags.

Already settled (not asked again): faction announcer fields in `P4_SIM_SYSTEMS.md` §2.7.3 (Enemy_Fleet_Approaching,
Pop_Cap (strategic), Land_Invasion_Commencing, GUI_Start_Campaign, Planet_Gained / Lost_Control, Max_Credits_Limit /
Build_Impossible (retail-unused)); `AUDIO.md` §9 tactical hookups (base under attack, weather, intro speech §9.7 [the
`SpeechEvent_Tactical_Intro_*` rows below are listed because the port has no reader: confirm §9.7 covers them], ability
SFX §9.6, hard point fire §9.5). Faction offsets come from `xml_field_tables.md` (Faction segment); object type offsets
are GameObjectType offsets.

Port anchors: `engine/audio/include/alamo/audio/game_audio.hpp` (`faction_sfx`, `UnitSounds`), `engine/apps/game/src/
game_audio_host.cpp` (tactical hookups), `engine/apps/game/app/app_input.cpp` (galactic lines). Rows marked `stored` are
kept by the port's object view but never played.

## A. Faction HUD / announcer lines (Factions.xml)

Likely families (one answer per family is fine when one site plays them all): HUD battle won / lost (with the
`_Enemy_TSW_Present` variants: what is "TSW" here, the enemy super weapon?), retreat begin / cancel / not allowed
(reason 1..3: which reasons) / enemy begin, surrender, reinforcements (selection, pick landing zone, enroute,
ready, requesting, cancelled), bombard / bombing run availability (own / ally / enemy), special weapon charging /
ready / firing (own / enemy), super weapon moving into range countdowns (60 / 30 / 15 / 5 s: range of what),
landing zones / build pads / reinforcement points captured / lost / contested, base shield online / offline /
absorb, gravity control generator, tactical build started / loop / complete / sold, unit / pop cap, gain / lose
control (friendly / enemy: of what), corruption mission commencing / won / lost, victory near, repairing,
bunker vacated, toggle non-hero ability on / off (own / enemy), strategic / tactical win / lose music.

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| Faction | `SFXEvent_Tactical_Gain_Friendly_Control` | 0x22c8 | unread | 59 | - |
| Faction | `SFXEvent_Tactical_Lose_Friendly_Control` | 0x22b0 | unread | 59 | - |
| Faction | `SFXEvent_GUI_Toggle_Non_Hero_Ability_On` | 0x7a0 | unread | 47 | - |
| Faction | `SFXEvent_GUI_Toggle_Non_Hero_Ability_Off` | 0xc70 | unread | 45 | - |
| Faction | `SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_Off` | 0x1610 | unread | 34 | - |
| Faction | `SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_On` | 0x1140 | unread | 34 | - |
| Faction | `SFXEvent_HUD_Enemy_Special_Weapon_Charging` | 0x2538 | unread | 9 | - |
| Faction | `SFXEvent_HUD_Enemy_Special_Weapon_Firing` | 0x2550 | unread | 9 | - |
| Faction | `SFXEvent_HUD_Enemy_Special_Weapon_Ready` | 0x2520 | unread | 9 | - |
| Faction | `SFXEvent_HUD_Special_Weapon_Charging` | 0x24f0 | unread | 9 | - |
| Faction | `SFXEvent_HUD_Special_Weapon_Firing` | 0x2508 | unread | 9 | - |
| Faction | `SFXEvent_HUD_Special_Weapon_Ready` | 0x24d8 | unread | 9 | - |
| Faction | `Reinforcements_Cancelled_SFXEvent` | 0x2280 | unread | 5 | - |
| Faction | `Reinforcements_Enroute_SFXEvent` | 0x2270 | unread | 5 | docs/re/STATION_HANGARS_SOUND.md |
| Faction | `Reinforcements_Pick_Landing_Zone_SFXEvent` | 0x2260 | unread | 5 | - |
| Faction | `Reinforcements_Selection_SFXEvent` | 0x2250 | unread | 5 | - |
| Faction | `Land_Retreat_Begin_SFXEvent` | 0x21d0 | unread | 3 | - |
| Faction | `Land_Retreat_Cancel_SFXEvent` | 0x21f0 | unread | 3 | - |
| Faction | `Land_Retreat_Not_Allowed_Reason_1_SFXEvent` | 0x2210 | unread | 3 | - |
| Faction | `Land_Retreat_Not_Allowed_Reason_2_SFXEvent` | 0x2220 | unread | 3 | - |
| Faction | `Land_Retreat_Not_Allowed_Reason_3_SFXEvent` | 0x2230 | unread | 3 | - |
| Faction | `Land_Retreat_Not_Allowed_SFXEvent` | 0x2200 | unread | 3 | - |
| Faction | `Land_Surrender_SFXEvent` | 0x2240 | unread | 3 | - |
| Faction | `Music_Event_Strategic_Lose` | 0x578 | unread | 3 | - |
| Faction | `Music_Event_Strategic_Win` | 0x550 | unread | 3 | docs/re/AUDIO.md |
| Faction | `Music_Event_Tactical_Lose` | 0x528 | unread | 3 | - |
| Faction | `Music_Event_Tactical_Win` | 0x500 | unread | 3 | - |
| Faction | `Reinforcements_Ready_SFXEvent` | 0x22a0 | unread | 3 | - |
| Faction | `Reinforcements_Requesting_SFXEvent` | 0x2290 | unread | 3 | - |
| Faction | `SFXEvent_Base_Shield_Absorb_Damage` | 0x690 | unread | 3 | docs/re/TRU1_A_RE_ANSWERS.md |
| Faction | `SFXEvent_Bombard_Ally_Available` | 0x2030 | unread | 3 | - |
| Faction | `SFXEvent_Bombard_Available` | 0x2020 | unread | 3 | docs/re/L1_S4_REMAINING.md |
| Faction | `SFXEvent_Bombard_Cancelled` | 0x2050 | unread | 3 | - |
| Faction | `SFXEvent_Bombard_Enemy_Available` | 0x2040 | unread | 3 | - |
| Faction | `SFXEvent_Bombing_Run_Ally_Available` | 0x1fe0 | unread | 3 | - |
| Faction | `SFXEvent_Bombing_Run_Available` | 0x1fd0 | unread | 3 | - |
| Faction | `SFXEvent_Bombing_Run_Begin_Crosstalk` | 0x2010 | unread | 3 | docs/re/L1_S4_REMAINING.md |
| Faction | `SFXEvent_Bombing_Run_Enemy_Available` | 0x1ff0 | unread | 3 | - |
| Faction | `SFXEvent_Bunker_Vacated` | 0x750 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Advisor_Urgent` | 0x2f58 | unread | 3 | docs/re/P4_SIM_SYSTEMS.md |
| Faction | `SFXEvent_HUD_Base_Shield_Offline` | 0x2398 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Base_Shield_Online` | 0x23b8 | unread | 3 | docs/re/TRU1_A_RE_ANSWERS.md |
| Faction | `SFXEvent_HUD_Build_Pad_Captured` | 0x2378 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Build_Pad_Lost` | 0x2388 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Enemy_Base_Shield_Offline` | 0x23a8 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Enemy_Base_Shield_Online` | 0x23c8 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Gravity_Control_Generator_Off` | 0x24c8 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Gravity_Control_Generator_On` | 0x24b8 | unread | 3 | docs/re/TRU1_GRAVITY_FIELD.md |
| Faction | `SFXEvent_HUD_Landing_Zone_Captured` | 0x2348 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Landing_Zone_Lost` | 0x2358 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Last_Landing_Zone_Lost` | 0x2368 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Lost_Land_Battle` | 0x2408 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Lost_Land_Battle_Enemy_TSW_Present` | 0x2468 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Lost_Space_Battle` | 0x2428 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Lost_Space_Battle_Enemy_TSW_Present` | 0x2488 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Reinforcement_Point_Contested` | 0x2628 | unread | 3 | docs/re/L1_S4_REMAINING.md |
| Faction | `SFXEvent_HUD_Repairing` | 0x24a8 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Tactical_Victory_Near` | 0x2498 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Won_Land_Battle` | 0x23f8 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Won_Land_Battle_Enemy_TSW_Present` | 0x2458 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Won_Space_Battle` | 0x2418 | unread | 3 | - |
| Faction | `SFXEvent_HUD_Won_Space_Battle_Enemy_TSW_Present` | 0x2478 | unread | 3 | - |
| Faction | `SFXEvent_Tactical_Object_Building_Complete` | 0x2658 | unread | 3 | - |
| Faction | `SFXEvent_Tactical_Object_Building_Loop` | 0x2648 | unread | 3 | - |
| Faction | `SFXEvent_Tactical_Object_Building_Started` | 0x2638 | unread | 3 | - |
| Faction | `SFXEvent_Tactical_Object_Sold` | 0x2668 | unread | 3 | - |
| Faction | `SFXEvent_Tactical_Pop_Cap_Reached` | 0x710 | unread | 3 | docs/re/P4_SIM_SYSTEMS.md |
| Faction | `SFXEvent_Tactical_Unit_Cap_Reached` | 0x6a0 | unread | 3 | - |
| Faction | `Space_Retreat_Begin_SFXEvent` | 0x2100 | unread | 3 | docs/re/BATTLE_TRANSITIONS.md |
| Faction | `Space_Retreat_Cancel_SFXEvent` | 0x2120 | unread | 3 | - |
| Faction | `Space_Retreat_Not_Allowed_Reason_1_SFXEvent` | 0x2140 | unread | 3 | - |
| Faction | `Space_Retreat_Not_Allowed_Reason_2_SFXEvent` | 0x2150 | unread | 3 | - |
| Faction | `Space_Retreat_Not_Allowed_Reason_3_SFXEvent` | 0x2160 | unread | 3 | - |
| Faction | `Space_Retreat_Not_Allowed_SFXEvent` | 0x2130 | unread | 3 | - |
| Faction | `Space_Surrender_SFXEvent` | 0x2170 | unread | 3 | - |
| Faction | `SpeechEvent_Super_Weapon_Enemy_Moved_Into_Range` | 0x1d38 | unread | 3 | - |
| Faction | `SpeechEvent_Super_Weapon_Enemy_Moving_Into_Range` | 0x1c70 | unread | 3 | - |
| Faction | `SpeechEvent_Super_Weapon_Enemy_Moving_Range_05_Seconds` | 0x1d10 | unread | 3 | - |
| Faction | `SpeechEvent_Super_Weapon_Enemy_Moving_Range_15_Seconds` | 0x1ce8 | unread | 3 | - |
| Faction | `SpeechEvent_Super_Weapon_Enemy_Moving_Range_30_Seconds` | 0x1cc0 | unread | 3 | - |
| Faction | `SpeechEvent_Super_Weapon_Enemy_Moving_Range_60_Seconds` | 0x1c98 | unread | 3 | - |
| Faction | `SpeechEvent_Super_Weapon_Moved_Into_Range` | 0x1c48 | unread | 3 | - |
| Faction | `SpeechEvent_Super_Weapon_Moving_Into_Range` | 0x1b80 | unread | 3 | - |
| Faction | `SpeechEvent_Super_Weapon_Moving_Range_05_Seconds` | 0x1c20 | unread | 3 | - |
| Faction | `SpeechEvent_Super_Weapon_Moving_Range_15_Seconds` | 0x1bf8 | unread | 3 | - |
| Faction | `SpeechEvent_Super_Weapon_Moving_Range_30_Seconds` | 0x1bd0 | unread | 3 | - |
| Faction | `SpeechEvent_Super_Weapon_Moving_Range_60_Seconds` | 0x1ba8 | unread | 3 | - |
| Faction | `SpeechEvent_Tactical_Intro_Land_Attacker` | 0x1d60 | unread | 3 | - |
| Faction | `SpeechEvent_Tactical_Intro_Land_Defender` | 0x1d88 | unread | 3 | - |
| Faction | `SpeechEvent_Tactical_Intro_Land_Raid_Attacker` | 0x1e00 | unread | 3 | - |
| Faction | `SpeechEvent_Tactical_Intro_Land_Raid_Defender` | 0x1e28 | unread | 3 | - |
| Faction | `SpeechEvent_Tactical_Intro_Space_Attacker` | 0x1db0 | unread | 3 | - |
| Faction | `SpeechEvent_Tactical_Intro_Space_Defender` | 0x1dd8 | unread | 3 | - |
| Faction | `Land_Retreat_Enemy_Begin_SFXEvent` | 0x21e0 | unread | 2 | - |
| Faction | `Space_Retreat_Enemy_Begin_SFXEvent` | 0x2110 | unread | 2 | - |
| Faction | `SFXEvent_HUD_Lost_Tactical_Corruption_Mission` | 0x2448 | unread | 1 | - |
| Faction | `SFXEvent_HUD_Won_Tactical_Corruption_Mission` | 0x2438 | unread | 1 | - |
| Faction | `SFXEvent_Tactical_Corruption_Mission_Commencing` | 0x650 | unread | 1 | docs/re/AUDIO.md |
| Faction | `SFXEvent_Tactical_Gain_Enemy_Control` | 0x22f8 | unread | 1 | - |
| Faction | `SFXEvent_Tactical_Lose_Enemy_Control` | 0x22e0 | unread | 1 | - |

## B. Object type SFX and speech (GameObjectType)

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| GameObjectType | `SFXEvent_Attack_Hardpoint` | 0x1080 | stored | 426 | docs/re/OBJECT_TYPES.md |
| GameObjectType | `SFXEvent_Hardpoint_Destroyed` | 0x1150 | stored | 362 | docs/re/OBJECT_TYPES.md |
| GameObjectType | `Land_Damage_SFX` | 0xed8 | stored | 221 | docs/re/OBJECT_TYPES.md |
| GameObjectType | `SFXEvent_Health_Critical_Warning` | 0x1330 | stored | 64 | - |
| GameObjectType | `SFXEvent_Health_Low_Warning` | 0x1320 | stored | 64 | - |
| GameObjectType | `Corruption_1_Success_Speech_Event` | 0x2508 | stored | 60 | docs/re/xml_tags.md |
| GameObjectType | `SFXEvent_Tactical_Build_Cancelled` | 0x14b0 | stored | 56 | - |
| GameObjectType | `SFXEvent_Tactical_Build_Complete` | 0x14a0 | stored | 56 | docs/re/L1_S4_REMAINING.md |
| GameObjectType | `SFXEvent_Tactical_Build_Started` | 0x1490 | stored | 56 | docs/re/L1_S4_REMAINING.md |
| GameObjectType | `Corruption_2_Success_Speech_Event` | 0x26a8 | stored | 49 | docs/re/xml_tags.md |
| GameObjectType | `SFXEvent_Move_Into_Nebula` | 0x13c0 | stored | 49 | - |
| GameObjectType | `SFXEvent_Turret_Rotating_Loop` | 0x1440 | stored | 37 | - |
| GameObjectType | `SFXEvent_Move_Into_Asteroid_Field` | 0x13b0 | stored | 32 | - |
| GameObjectType | `Corruption_1_Failure_Speech_Event` | 0x2550 | stored | 30 | docs/re/xml_tags.md |
| GameObjectType | `Play_SFXEvent_On_Sighting` | 0x8f | stored | 30 | docs/re/FOG_OF_WAR.md |
| GameObjectType | `Corruption_1_Mission_Commencing_Speech_Event` | 0x25b8 | stored | 29 | docs/re/xml_tags.md |
| GameObjectType | `SFXEvent_Sold_Tactical` | 0x1480 | stored | 29 | - |
| GameObjectType | `Spin_Away_On_Death_SFXEvent_Start_Die` | 0x12f0 | stored | 22 | - |
| GameObjectType | `SFXEvent_Damaged_By_Asteroid` | 0x13d0 | stored | 17 | docs/re/L1_S4_REMAINING.md |
| GameObjectType | `SFXEvent_Special_Weapon_Ready` | 0x1310 | stored | 15 | docs/re/TACTICAL_COMBAT.md |
| GameObjectType | `SFXEvent_Bombard_Incoming` | 0x1390 | stored | 13 | - |
| GameObjectType | `SFXEvent_Bombard_Select_Target` | 0x1380 | stored | 13 | - |
| GameObjectType | `Corruption_3_Success_Speech_Event` | 0x2848 | stored | 8 | docs/re/xml_tags.md |
| GameObjectType | `SFXEvent_Enemy_Damaged_Health_Critical_Warning` | 0x1350 | stored | 8 | - |
| GameObjectType | `SFXEvent_Enemy_Damaged_Health_Low_Warning` | 0x1340 | stored | 8 | - |
| GameObjectType | `Corruption_3_Failure_Speech_Event` | 0x2890 | stored | 5 | docs/re/xml_tags.md |
| GameObjectType | `Selt_Destruct_SFXEvent_Start_Die` | 0x1300 | stored | 5 | - |
| GameObjectType | `SFXEvent_Powered_Active_Loop` | 0x1430 | stored | 3 | docs/re/TRU1_A_RE_ANSWERS.md |
| GameObjectType | `SFXEvent_Hardpoint_All_Weapons_Destroyed` | 0x1070 | stored | 2 | - |
| GameObjectType | `Build_Speech_Countdowns` | 0x15c8 | stored | 1 | docs/re/MCX4_T09_RE_ANSWERS.md |
| GameObjectType | `Corruption_2_Failure_Speech_Event` | 0x26f0 | stored | 1 | docs/re/xml_tags.md |
| GameObjectType | `SFXEvent_Beacon_Placed` | 0x13a0 | stored | 1 | - |
| GameObjectType | `SFXEvent_Cough_Override` | 0x1510 | stored | 1 | - |
| GameObjectType | `TSW_SFXEvent_GUI_Bttton_Press` | 0x2150 | stored | 1 | - |

## C. Hard points, abilities and actions

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| HardPoint | `Death_Explosion_SFXEvent` | 0x160 | unread | 267 | docs/re/SPACE_AND_OPEN.md |

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| Unit_Ability | `SFXEvent_GUI_Unit_Ability_Activated` | 0x180 | stored | 102 | docs/re/AUDIO.md |
| Unit_Ability | `SFXEvent_Target_Ability` | 0x170 | stored | 60 | docs/re/AUDIO.md |
| Unit_Ability | `SFXEvent_GUI_Unit_Ability_Deactivated` | 0x190 | stored | 37 | - |
| Unit_Ability | `SFXEvent_Special_Ability_Loop` | 0x1a0 | stored | 12 | docs/re/AUDIO.md |

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| Attack_Action | `Attack_SFXEvent` | 0x110 | stored | 10 | docs/re/xml_tags.md |

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| Buzz_Droids_Ability | `Activate_SFX` | 0x1a8 | stored | 6 | docs/re/xml_tags.md |

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| Proximity_Mines_Ability | `Activate_SFX` | 0x1a0 | stored | 6 | docs/re/xml_tags.md |

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| Laser_Defense_Ability | `Zap_SFXEvent` | 0x230 | stored | 2 | - |

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| Radioactive_Contaminate_Ability | `SFXEvent_Radioactive` | 0x1c8 | stored | 1 | - |

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| Tactical_Bribe_Ability | `SFXEvent_Fail` | 0x1b8 | stored | 1 | - |
| Tactical_Bribe_Ability | `SFXEvent_Success` | 0x1a8 | stored | 1 | - |

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| Tractor_Beam_Attack_Ability | `SFXEvent_Unit_Summoned` | 0x198 | stored | 1 | - |

## D. Movies

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| Movie | `SFXEvent_Frame` | 0x68 | unread | 14 | - |
| Movie | `SpeechEvent_Frame` | 0x78 | unread | 12 | - |

## E. Other

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| GameConstants | `Speech_Text_Color` | 0x1280 | stored | 1 | - |

| class | tag | offset | state | base uses | spec today |
|---|---|---|---|---:|---|
| DynamicTrack | `Files` | 0x20 | unread | 2333 | docs/re/AUDIO.md |
| DynamicTrack | `Volume_Percent` | 0x3c | unread | 2333 | docs/re/AUDIO.md |

Notes on single rows:
- `DynamicTrack.Files` / `DynamicTrack.Volume_Percent`: `xml_field_tables.md` files them under DynamicTrack, but
  DynamicTrackFX.xml has no such elements and `Files` at +0x20 overlaps `width`; base FoC writes both only in
  SpeechEvents.xml (2271) and MusicEvents.xml (62). Confirm they are the SpeechEvent parser's fields (0x320f80):
  `Files` sample list, `Volume_Percent` int, and what the speech player does with them.
- `GameObjectType.Selt_Destruct_SFXEvent_Start_Die` is spelt so in retail's table: reader and the death path.
- `GameObjectType.SFXEvent_Cough_Override`, `TSW_SFXEvent_GUI_Bttton_Press`, `SFXEvent_Beacon_Placed`: reader or unused.
- `GameConstants.Speech_Text_Color`: the subtitle colour of speech (AUDIO §3.4)? Reader and default.
- `Movie.SFXEvent_Frame` / `Movie.SpeechEvent_Frame`: the movie player's per-frame cue lists; value shape (frame, event
  pairs?) and when they fire relative to the Bink frame clock.
- `GameObjectType.Corruption_N_{Success,Failure,Mission_Commencing}_Speech_Event`: which object (the corruption hero?
  the planet?) and when; relation to AUDIO §9.7's corruption-mission speech.
- `GameObjectType.Build_Speech_Countdowns` (MCX4_T09 Q7 names it): value shape and when each countdown line plays.
