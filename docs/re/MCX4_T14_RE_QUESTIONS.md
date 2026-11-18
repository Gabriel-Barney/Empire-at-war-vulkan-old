# mcx4 t14 RE questions: ui rows of the honour gaps (Ghidra batch)

Roadmap group `mcx4` task t14 (lane mcx4-pres). Trimmed 2026-11-18 to the 169 rows that existing `docs/re/` specs, the
port's code and the FoC XML do not settle (a docs-only triage settled or annotated the rest). Answers go to
`docs/re/MCX4_T14_RE_ANSWERS.md`, one greppable section per tag or per family (`## Class.Tag`; a family section names
every tag it settles in backticks), behaviour in words with [H] / [M] / [L] and RVAs, no decompiled code. Method as in
`MCX4_T13_RE_ANSWERS.md` ("Method": displacement sweep incl. scaled-index forms, offset + 0x20 for speech / music refs,
the ctor / dtor / resolver exclusions).

For every tag: **every reader** (RVA; what it changes on screen or in the rules, when it runs, value shape and default),
or **retail-unused** with the evidence. A reader that is only debug / MP / hardware detection: say so (out of scope,
`docs/DECISIONS.md` "Out-of-scope tag-ledger rows"). Rows are grouped by class; the hint column is the triage's guess
at the reader area.

Also asked: mcx8 t09 - does the skirmish options dialog (0xea8d0) set game-settings flag 0x20 ("an option was
changed") like the campaign advanced options dialog (0xeb1c0), so Game.InterventionsEnabled is 0.0 in a skirmish
with changed options? (AI_GALACTIC_TOKENS §4.)

## GameConstants (85)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Auto_Rotate_For_Space_Targeting` | 0x15d0 | 0x0 | stored | 1 | space targeting camera; FoC False |
| `Battle_Load_Planet_Ambient` | 0x11890 | 0x10 | stored | 1 | battle load screen planet model lighting / viewport (UI.md §5.1 BattleLoadDialogClass 0x8d930) |
| `Battle_Load_Planet_Direction` | 0x11884 | 0x10 | stored | 1 | battle load screen planet model lighting / viewport (UI.md §5.1 BattleLoadDialogClass 0x8d930) |
| `Battle_Load_Planet_Viewport` | 0x11850 | 0x11 | stored | 1 | battle load screen planet model lighting / viewport (UI.md §5.1 BattleLoadDialogClass 0x8d930) |
| `Battle_Pending_Message_Font` | 0x548 | 0x17 | stored | 1 | GALACTIC_DIALOGS §1.3 takes the title font from the component: check whether these are read at all (maybe MP countdown only) |
| `Battle_Pending_Message_Font_Size` | 0x568 | 0x5 | stored | 1 | GALACTIC_DIALOGS §1.3 takes the title font from the component: check whether these are read at all (maybe MP countdown only) |
| `Battle_Pending_Message_Pos_X` | 0x56c | 0x8 | stored | 1 | GALACTIC_DIALOGS §1.3 takes the title font from the component: check whether these are read at all (maybe MP countdown only) |
| `Battle_Pending_Message_Pos_Y` | 0x570 | 0x8 | stored | 1 | GALACTIC_DIALOGS §1.3 takes the title font from the component: check whether these are read at all (maybe MP countdown only) |
| `CB_Flash_Count` | 0x1248 | 0x6 | stored | 1 | command-bar button flash (story FLASH_GUI / holocron flash) |
| `CB_Flash_Duration` | 0x1244 | 0x8 | stored | 1 | command-bar button flash (story FLASH_GUI / holocron flash) |
| `Command_Bar_Default_Font_Name` | 0x4c0 | 0x17 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `Command_Bar_Default_Font_Size` | 0x4e0 | 0x5 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `Corrupt_Side_Leader_Name` | 0x1380 | 0x1b | stored | 1 | side / leader name texts (skirmish setup or end screen?) |
| `Corrupt_Side_Name` | 0x1360 | 0x17 | stored | 1 | side / leader name texts (skirmish setup or end screen?) |
| `Corruption_Choice_Benefit` | 0x11fb0 | 0x2a | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Corruption_Choice_Encyclopedia` | 0x11fe0 | 0x2a | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Corruption_Choice_Icon_Name` | 0x11f80 | 0x2a | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Corruption_Choice_Name` | 0x11f98 | 0x2a | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Corruption_Encyclopedia_Backdrop` | 0x12148 | 0x17 | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Corruption_Encyclopedia_Complete` | 0x12188 | 0x17 | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Corruption_Encyclopedia_Header` | 0x12168 | 0x17 | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Corruption_Encyclopedia_Incomplete` | 0x121a8 | 0x17 | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Corruption_Encyclopedia_Left_Edge` | 0x121e8 | 0x6 | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Corruption_Encyclopedia_Money_Icon` | 0x121c8 | 0x17 | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Corruption_Encyclopedia_Spacing` | 0x121ec | 0x6 | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Corruption_Line_Grow_Seconds` | 0x11f58 | 0x8 | stored | 1 | midpoint line particles use them, consumer not traced [M]: narrow question |
| `Corruption_Line_Radius` | 0x11f50 | 0x8 | stored | 1 | midpoint line particles use them, consumer not traced [M]: narrow question |
| `Corruption_Line_Start_End_Offset` | 0x11f54 | 0x8 | stored | 1 | midpoint line particles use them, consumer not traced [M]: narrow question |
| `Corruption_Mission_Requirement_Icon_Name` | 0x11fc8 | 0x2a | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Corruption_Planet_Icon_Encyclopedia_Desc` | 0x12028 | 0x2a | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Corruption_Planet_Icon_Encyclopedia_Name` | 0x12010 | 0x2a | stored | 1 | corruption family: the corruption-choice menu and the corruption pop-up of the galactic HUD (GALACTIC_REVEAL_CORRUPTION area); one question |
| `Display_Bink_Movie_Frames` | 0x11708 | 0x0 | stored | 1 | likely a debug frame-counter overlay: confirm, then OUT_OF_SCOPE (debug) |
| `Droid_Encyclopedia_Offset` | 0x1298 | 0xf | stored | 1 | droid log / holocron page layout (P4_SIM_SYSTEMS §2.7.2 area) |
| `Event_Message_Default_Font_Name` | 0x420 | 0x17 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `Event_Message_Default_Font_Size` | 0x440 | 0x5 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `Evil_Side_Leader_Name` | 0x1340 | 0x17 | stored | 1 | side / leader name texts (skirmish setup or end screen?) |
| `GMC_Battle_Fade_Time` | 0x1c0 | 0x8 | stored | 1 | galactic-to-battle camera zoom / fade (BATTLE_TRANSITIONS) |
| `GMC_Battle_Zoom_Time` | 0x1bc | 0x8 | stored | 1 | galactic-to-battle camera zoom / fade (BATTLE_TRANSITIONS) |
| `GUI_Cycle_Speed` | 0x1208 | 0x8 | stored | 1 | 0x392ba0 modes 4/5/6 named [M] but the constants are not: confirm +0x1208 / +0x120c / +0x121c reads |
| `GUI_Darken_Level` | 0x120c | 0x8 | stored | 1 | 0x392ba0 modes 4/5/6 named [M] but the constants are not: confirm +0x1208 / +0x120c / +0x121c reads |
| `GUI_Hilite_Level` | 0x121c | 0x8 | stored | 1 | 0x392ba0 modes 4/5/6 named [M] but the constants are not: confirm +0x1208 / +0x120c / +0x121c reads |
| `GUI_Strategic_Countdown_Timers_Screen_Spacing` | 0x123c | 0x8 | stored | 1 | likely the positions of the timers drawn by 0x3d2b0: one narrow question |
| `GUI_Strategic_Countdown_Timers_Screen_X` | 0x1234 | 0x8 | stored | 1 | likely the positions of the timers drawn by 0x3d2b0: one narrow question |
| `GUI_Strategic_Countdown_Timers_Screen_Y` | 0x1238 | 0x8 | stored | 1 | likely the positions of the timers drawn by 0x3d2b0: one narrow question |
| `GUI_Tactical_Countdown_Timers_Screen_Spacing` | 0x1230 | 0x8 | stored | 1 | likely the positions of the timers drawn by 0x3d2b0: one narrow question |
| `GUI_Tactical_Countdown_Timers_Screen_X` | 0x1228 | 0x8 | stored | 1 | likely the positions of the timers drawn by 0x3d2b0: one narrow question |
| `GUI_Tactical_Countdown_Timers_Screen_Y` | 0x122c | 0x8 | stored | 1 | likely the positions of the timers drawn by 0x3d2b0: one narrow question |
| `Galactic_Zoom_In_Station_Offset` | 0x5f0 | 0x10 | stored | 1 | starbase placement in the planet zoom-in |
| `Galactic_Zoom_In_Station_Rotation` | 0x5fc | 0x8 | stored | 1 | starbase placement in the planet zoom-in |
| `Game_Object_Name_Font_Name` | 0x510 | 0x17 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `Game_Object_Name_Font_Size` | 0x530 | 0x5 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `Good_Side_Leader_Name` | 0x1300 | 0x17 | stored | 1 | side / leader name texts (skirmish setup or end screen?) |
| `In_Game_Message_Default_Font_Name` | 0x3f8 | 0x17 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `In_Game_Message_Default_Font_Size` | 0x418 | 0x5 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `Large_Coin_Stack_Size` | 0x188 | 0x8 | stored | 1 | credit coin-stack visual |
| `Left_Queue_Tint` | 0x13c4 | 0x16 | stored | 1 | build-queue tint |
| `Map_Preview_Image_Size` | 0x2a4 | 0x6 | stored | 1 | skirmish map preview (UI.md §9.6) |
| `Medium_Coin_Stack_Size` | 0x184 | 0x8 | stored | 1 | credit coin-stack visual |
| `Mouse_Over_Highlight_Scale` | 0x11ec | 0x8 | stored | 1 | hover highlight |
| `Multiplayer_Losing_Team_Bonus_Credit_Percentage` | 0x5c0 | 0x8 | stored | 1 | is it MP-only? then OUT_OF_SCOPE |
| `Num_Structures_For_Large_Planet_Name` | 0x198 | 0x6 | stored | 1 | planet label size (UI.md §1.6 0x3dda70) |
| `Num_Structures_For_Medium_Planet_Name` | 0x194 | 0x6 | stored | 1 | planet label size (UI.md §1.6 0x3dda70) |
| `Planet_Ability_Icon_Names` | 0x1618 | 0x1b | stored | 1 | planet-ability family with type Planet_Ability_Name (planet label / tooltip) |
| `Planet_Ability_RGBs` | 0x1648 | 0x12 | stored | 1 | planet-ability family with type Planet_Ability_Name (planet label / tooltip) |
| `Planet_Ability_Text_IDs` | 0x1630 | 0x1b | stored | 1 | planet-ability family with type Planet_Ability_Name (planet label / tooltip) |
| `PlayModeSwitchMovies` | 0x2f8 | 0x0 | stored | 1 |  |
| `Right_Queue_Tint` | 0x13d4 | 0x16 | stored | 1 | build-queue tint |
| `Shield_Flash_Duration` | 0x1574 | 0x8 | stored | 1 | shield-hit flash (space) |
| `Shield_Flash_Scale` | 0x1568 | 0x10 | stored | 1 | shield-hit flash (space) |
| `ShipNameTextFiles` | 0x119a8 | 0x36 | stored | 1 | unit name lists |
| `SpaceReinforceFeedbackOnlyWhileDragging` | 0x11d4 | 0x0 | stored | 1 | space reinforcement overlay |
| `Team_Healthbar_Offset` | 0x11860 | 0x8 | stored | 1 |  |
| `Text_Button_Default_Font_Name` | 0x4e8 | 0x17 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `Text_Button_Default_Font_Size` | 0x508 | 0x5 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `Text_Reveal_Rate` | 0x2b4 | 0x8 | stored | 1 | typewriter text rate (FoC 120) |
| `Tool_Tip_Font_Name` | 0x470 | 0x17 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `Tool_Tip_Font_Size` | 0x490 | 0x5 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `Tool_Tip_Small_Font_Name` | 0x498 | 0x17 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `Tool_Tip_Small_Font_Size` | 0x4b8 | 0x5 | stored | 1 | HUD default fonts family: one question (HUD init 0x30ae50 / component font fallback; UI.md §3.4 covers GUIDialogs fonts only) |
| `WaypointFlagModelName` | 0xd0 | 0x17 | stored | 1 | waypoint family with T15 *WaypointLineTextureName (WaypointLineSubClass, UI.md §1.5 [L]) |
| `WaypointLineLandDashLength` | 0x13e4 | 0x8 | stored | 1 | waypoint family with T15 *WaypointLineTextureName (WaypointLineSubClass, UI.md §1.5 [L]) |
| `WaypointLineLandDashVelocity` | 0x13ec | 0x8 | stored | 1 | waypoint family with T15 *WaypointLineTextureName (WaypointLineSubClass, UI.md §1.5 [L]) |
| `WaypointLineLandGapLength` | 0x13e8 | 0x8 | stored | 1 | waypoint family with T15 *WaypointLineTextureName (WaypointLineSubClass, UI.md §1.5 [L]) |
| `Win_Lose_Message_Font` | 0x380 | 0x17 | stored | 1 | tactical win / lose banner text (with T15 Win_/Lose_Message_Color) |
| `Win_Lose_Message_Font_Size` | 0x3a0 | 0x5 | stored | 2 | tactical win / lose banner text (with T15 Win_/Lose_Message_Color) |

## GameObjectType (33)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Beacon_Lifetime_In_Secs` | 0x17b8 | 0x8 | stored | 1 | beacons (PlaceBeacon): likely MP-only, then OUT_OF_SCOPE |
| `Beacon_Radar_Map_Event_Name` | 0x1798 | 0x17 | stored | 1 | beacons (PlaceBeacon): likely MP-only, then OUT_OF_SCOPE |
| `Corruption_1_Failure_Bink_Hologram_Name` | 0x2578 | 0x17 | stored | 30 | mission path of §4.5 (briefing dialog, failure hologram): narrow question |
| `Corruption_1_Mission_Briefing` | 0x2598 | 0x17 | stored | 60 | mission path of §4.5 (briefing dialog, failure hologram): narrow question |
| `Corruption_2_Failure_Bink_Hologram_Name` | 0x2718 | 0x17 | stored | 1 | mission path of §4.5 (briefing dialog, failure hologram): narrow question |
| `Corruption_2_Mission_Briefing` | 0x2738 | 0x17 | stored | 49 | mission path of §4.5 (briefing dialog, failure hologram): narrow question |
| `Corruption_3_Failure_Bink_Hologram_Name` | 0x28b8 | 0x17 | stored | 5 | mission path of §4.5 (briefing dialog, failure hologram): narrow question |
| `Corruption_3_Mission_Briefing` | 0x28d8 | 0x17 | stored | 8 | mission path of §4.5 (briefing dialog, failure hologram): narrow question |
| `Encyclopedia_Weather_Info` | 0x1910 | 0x17 | stored | 64 | §9.2 covers Describe_Weather / _Icon only; weather tooltip? |
| `Encyclopedia_Weather_Name` | 0x18f0 | 0x17 | stored | 64 | §9.2 covers Describe_Weather / _Icon only; weather tooltip? |
| `End_Control_Transition_Radar_Event` | 0x1778 | 0x17 | stored | 6 |  |
| `Enemy_Spawn_Text` | 0x19e8 | 0x2a | stored | 29 |  |
| `Friendly_Spawn_Text` | 0x19d0 | 0x2a | stored | 21 |  |
| `GUI_Angles` | 0x1b00 | 0x10 | stored | 6 | GUI 3D-model view family (with T15 GUI_Model_Name) |
| `GUI_Distance` | 0x1b10 | 0x8 | stored | 63 | GUI 3D-model view family (with T15 GUI_Model_Name) |
| `GUI_Velocity` | 0x1b0c | 0x8 | stored | 67 | GUI 3D-model view family (with T15 GUI_Model_Name) |
| `GUI_X_Rot` | 0x1b14 | 0x8 | stored | 6 | GUI 3D-model view family (with T15 GUI_Model_Name) |
| `Hero_Ability` | 0x1860 | 0x17 | stored | 1 |  |
| `Include_In_UI_Map_Header` | 0xb2 | 0x0 | stored | 62 | skirmish map header / preview markers (UI.md §9.3) |
| `MP_Encyclopedia_Text` | 0x1898 | 0x2a | stored | 99 | asked: used in skirmish? |
| `Name_Adjust` | 0x65c | 0x10 | stored | 67 | planet label offset? (UI.md §1.6 0x3dda70) |
| `Planet_Ability_Name` | 0x2250 | 0x17 | stored | 62 | with GC Planet_Ability_* |
| `Radar_Clip_To_Visible_Region` | 0xde | 0x0 | stored | 1 | radar blip family: one question on the radar object 0x140b2bcb0; port notes no reader (hud_world.hpp:64-65) |
| `Radar_Icon_Scale_Land` | 0x1d54 | 0x8 | stored | 74 | radar blip family: one question on the radar object 0x140b2bcb0; port notes no reader (hud_world.hpp:64-65) |
| `Radar_Icon_Scale_Space` | 0x1d58 | 0x8 | stored | 74 | radar blip family: one question on the radar object 0x140b2bcb0; port notes no reader (hud_world.hpp:64-65) |
| `Radar_Range_Icon_Name` | 0x1728 | 0x17 | stored | 12 | radar blip family: one question on the radar object 0x140b2bcb0; port notes no reader (hud_world.hpp:64-65) |
| `Radar_Register_As_Foreground_Object` | 0xdd | 0x0 | stored | 2 | radar blip family: one question on the radar object 0x140b2bcb0; port notes no reader (hud_world.hpp:64-65) |
| `Radar_Rotate_Icon` | 0xd6 | 0x0 | stored | 5 | radar blip family: one question on the radar object 0x140b2bcb0; port notes no reader (hud_world.hpp:64-65) |
| `Secondary_Objective` | 0x19b0 | 0x17 | stored | 9 |  |
| `Show_Hero_Head` | 0x17 | 0x0 | stored | 9 |  |
| `Show_Name` | 0x23 | 0x0 | stored | 112 |  |
| `TSW_Post_Music_Wait_Frames` | 0x21a8 | 0x6 | stored | 1 | TSW state machine (sibling Post_Destruction_Wait_Frames) |
| `Tactical_Build_Start_Lower_Z` | 0xa04 | 0x8 | stored | 80 | tactical build-pad construction (structure rises from below) |

## Faction (34)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Alternate_Icon_Prefix` | 0x358 | 0x17 | unread | 3 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Carrier_Icon_Name` | 0x278 | 0x17 | unread | 3 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Corvette_Icon_Name` | 0x2b8 | 0x17 | unread | 3 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Defeat_Text` | 0x2e30 | 0x17 | unread | 3 | battle / game end screen family |
| `Fighter_Icon_Name` | 0x2d8 | 0x17 | unread | 3 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Finale_Movie` | 0x2db0 | 0x17 | unread | 3 | campaign-win movie family |
| `Finale_Movie_2` | 0x2dd0 | 0x17 | unread | 1 | campaign-win movie family |
| `Fleet_Icon_Name` | 0x1d8 | 0x17 | unread | 8 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Frigate_Icon_Name` | 0x298 | 0x17 | unread | 3 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Generic_Win_Movie` | 0x2d90 | 0x17 | unread | 3 | campaign-win movie family |
| `Ground_Base_Icon_Name` | 0x178 | 0x17 | unread | 8 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Ground_Transport_Icon_Name` | 0x1b8 | 0x17 | unread | 8 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Helper_Icon_Name` | 0x26b8 | 0x17 | unread | 3 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Infantry_Icon_Name` | 0x238 | 0x17 | unread | 3 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Land_Ability_Targeting_Range_Overlay_Material_Name` | 0x2d38 | 0x17 | unread | 3 | ability range overlays on the blob path (with T15 *_Overlay_RGBA) |
| `Land_Ability_Targeting_Range_Overlay_Scale_Factor` | 0x2d68 | 0x8 | unread | 3 | ability range overlays on the blob path (with T15 *_Overlay_RGBA) |
| `Land_Area_Effect_Range_Overlay_Material_Name` | 0x2d00 | 0x17 | unread | 3 | ability range overlays on the blob path (with T15 *_Overlay_RGBA) |
| `Land_Area_Effect_Range_Overlay_Scale_Factor` | 0x2d30 | 0x8 | unread | 3 | ability range overlays on the blob path (with T15 *_Overlay_RGBA) |
| `Land_Lose_Image` | 0x2eb0 | 0x17 | unread | 3 | battle / game end screen family |
| `Land_Retreat_Countdown_Text_ID` | 0x21b0 | 0x17 | unread | 3 | retreat countdown label (port shows progress: app_tactical_keys.cpp:329) |
| `Land_Win_Image` | 0x2e90 | 0x17 | unread | 3 | battle / game end screen family |
| `Planet_Icon_Offset` | 0x39c | 0x10 | unread | 8 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Planet_Icon_Scale` | 0x398 | 0x8 | unread | 8 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Post_Credits_Movie` | 0x2df0 | 0x17 | unread | 1 | campaign-win movie family |
| `Ship_Icon_Name` | 0x1f8 | 0x17 | unread | 8 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Space_Lose_Image` | 0x2e70 | 0x17 | unread | 3 | battle / game end screen family |
| `Space_Retreat_Countdown_Text_ID` | 0x20e0 | 0x17 | unread | 3 | retreat countdown label (port shows progress: app_tactical_keys.cpp:329) |
| `Space_Win_Image` | 0x2e50 | 0x17 | unread | 3 | battle / game end screen family |
| `Squadron_Icon_Name` | 0x218 | 0x17 | unread | 8 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Star_Base_Icon_Name` | 0x198 | 0x17 | unread | 8 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Superweapon_Win_Movie` | 0x2d70 | 0x17 | unread | 3 | campaign-win movie family |
| `Text_Nickname_ID` | 0x48 | 0x17 | unread | 1 |  |
| `Vehicle_Icon_Name` | 0x258 | 0x17 | unread | 3 | faction icon family (g_fleet.tga, i_icon_fleet.tga ...): galactic planet label / stack icons (UI.md §1.6, GALAXY_MAP_PRESENTATION §1); one question |
| `Victory_Text` | 0x2e10 | 0x17 | unread | 3 | battle / game end screen family |

## Campaign (1)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Show_Completed_Tab` | 0x379 | 0x0 | stored | 58 | 0x28dbe0 copies +0x379 to 0x140b305e8: find that global's readers |

## CommandBarComponent (8)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Can_Drag_Stack` | 0x3e4 | 0x50 | unread | 20 | CBC flag family (bits 0x3e1-0x3e5): drag / draw code 0x2f6c30, 0x2ffcb0; one question with T15 rows |
| `Cross_Fade` | 0x3e2 | 0x50 | unread | 4 | CBC flag family (bits 0x3e1-0x3e5): drag / draw code 0x2f6c30, 0x2ffcb0; one question with T15 rows |
| `Ghost_Base_Only` | 0x3e3 | 0x50 | unread | 56 | CBC flag family (bits 0x3e1-0x3e5): drag / draw code 0x2f6c30, 0x2ffcb0; one question with T15 rows |
| `Scale_Duration` | 0x3a8 | 0x8 | unread | 115 | CBC flag family (bits 0x3e1-0x3e5): drag / draw code 0x2f6c30, 0x2ffcb0; one question with T15 rows |
| `Should_Ghost` | 0x3e2 | 0x50 | unread | 198 | CBC flag family (bits 0x3e1-0x3e5): drag / draw code 0x2f6c30, 0x2ffcb0; one question with T15 rows |
| `Snap_Drag` | 0x3e3 | 0x50 | unread | 115 | CBC flag family (bits 0x3e1-0x3e5): drag / draw code 0x2f6c30, 0x2ffcb0; one question with T15 rows |
| `Snap_Location` | 0x3e3 | 0x50 | unread | 115 | CBC flag family (bits 0x3e1-0x3e5): drag / draw code 0x2f6c30, 0x2ffcb0; one question with T15 rows |
| `Stackable` | 0x3e1 | 0x50 | unread | 145 | CBC flag family (bits 0x3e1-0x3e5): drag / draw code 0x2f6c30, 0x2ffcb0; one question with T15 rows |

## HardPoint (1)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Tooltip_Text` | 0x120 | 0x17 | unread | 431 | hardpoint reticle hover tooltip (HUD_GAPS §1.6 / §2.1); base: 267 HardPoint uses |

## Laser_Defense_Ability (1)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Lightning_Effect_Name` | 0x1e8 | 0x17 | stored | 5 | [L] not seen used; §9 lists a +0x1e8 lightning field: narrow |

## Movie (1)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Movie_Start_Frame` | 0x20 | 0x6 | unread | 1 | Bink open 0x44d920 start frame (Credits only) |

## RadarMapSettings (3)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Land_Is_Guide_Rectangle` | 0x130 | 0x0 | unread | 1 | radar map object 0x140b2bcb0 (UI.md §1.7) |
| `Space_Is_Guide_Rectangle` | 0xc0 | 0x0 | unread | 1 | radar map object 0x140b2bcb0 (UI.md §1.7) |
| `Use_Event_System` | 0x51d | 0x0 | unread | 1 | radar map object 0x140b2bcb0 (UI.md §1.7) |

## Unit_Ability (2)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Alternate_Description_Text` | 0x1e0 | 0x17 | stored | 34 | ability button tooltip when the alternate state is on |
| `Alternate_Name_Text` | 0x1c0 | 0x17 | stored | 31 | ability button tooltip when the alternate state is on |

