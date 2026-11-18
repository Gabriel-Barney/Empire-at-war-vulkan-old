# mcx4 t14 RE questions: ui rows of the honour gaps

Roadmap group `mcx4` task t14 (lane mcx4-pres); rows: `python -I tools/lint/check_tag_ledger.py --gaps --group ui`
(234 rows, 2026-11-18). Answers go to `docs/re/MCX4_T14_RE_ANSWERS.md`, one greppable section per tag or
per family (a family section names every tag it settles in backticks), behaviour in words with [H] / [M] / [L] and
RVAs, no decompiled code. The method of `MCX4_RE_ANSWERS_GALACTIC.md` ("Method") applies.

For every tag: **every reader** (RVA; what it changes on screen or in the rules, when it runs, value shape and default),
or **retail-unused** with the cross-reference evidence (disp32 search of the field offset finding only ctor / dtor /
copy; the ledger then cites the section). A reader that is only debug / MP / hardware-detection is out of scope:
say so (`docs/DECISIONS.md` "Out-of-scope tag-ledger rows"). Group answers by reader function.

Port anchors: `engine/ui` (command bar `hud_*`, radar, mouse pointers `gui_cursor`, encyclopedia `gui_info_screens`, menus `gui_menus.cpp`), `engine/apps/game/src/hud_feeds.cpp`. Also asked (moved here from the scope group by t16): `Campaign.Is_Multiplayer` (MCX4_RE_ANSWERS_GALACTIC Q7.3 says 0x478f90 filters on it: confirm the single-player lists exclude Is_Multiplayer campaigns), `MP_Encyclopedia_Text` (shown in skirmish, retail's multiplayer game type?), `MP_Default_Game_Timer` / `_Win_Condition*` / `_Land/Space_Tactical_Win_Condition` (skirmish setup defaults?), `Multiplayer_Losing_Team_Bonus_Credit_Percentage` (skirmish or MP-only?). And for mcx8 t09: does the skirmish options dialog (0xea8d0) set settings flag 0x20 ("an option was changed") like the campaign advanced options (0xeb1c0), so Game.InterventionsEnabled reads 0.0 in a skirmish with changed options?

Offsets are of the row's class (`xml_field_tables.md`; GameConstants offsets are GC offsets, object type offsets
GameObjectType offsets). Rows marked `stored` are kept by the port but nothing reads them.

## GameConstants (113)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Galactic_Zoom_Acceleration` | 0x5d0 | 0x8 | stored | 2 | docs/re/P1_CAMERA_INPUT.md |
| `Max_Galactic_Zoom_Distance` | 0x5c4 | 0x8 | stored | 2 | docs/re/P1_CAMERA_INPUT.md |
| `Max_Galactic_Zoom_Speed` | 0x5cc | 0x8 | stored | 2 | - |
| `Min_Galactic_Zoom_Speed` | 0x5c8 | 0x8 | stored | 2 | - |
| `Win_Lose_Message_Font_Size` | 0x3a0 | 0x5 | stored | 2 | - |
| `Animate_During_Galactic_Mode_Pause` | 0x11e6c | 0x0 | stored | 1 | docs/re/xml_tags.md |
| `Auto_Rotate_For_Space_Targeting` | 0x15d0 | 0x0 | stored | 1 | docs/re/xml_tags.md |
| `Battle_Load_Planet_Ambient` | 0x11890 | 0x10 | stored | 1 | docs/re/xml_tags.md |
| `Battle_Load_Planet_Direction` | 0x11884 | 0x10 | stored | 1 | docs/re/xml_tags.md |
| `Battle_Load_Planet_Viewport` | 0x11850 | 0x11 | stored | 1 | docs/re/xml_tags.md |
| `Battle_Pending_Message_Font` | 0x548 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Battle_Pending_Message_Font_Size` | 0x568 | 0x5 | stored | 1 | docs/re/xml_tags.md |
| `Battle_Pending_Message_Pos_X` | 0x56c | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `Battle_Pending_Message_Pos_Y` | 0x570 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `CB_Flash_Count` | 0x1248 | 0x6 | stored | 1 | docs/re/xml_tags.md |
| `CB_Flash_Duration` | 0x1244 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `CB_Movie_Offset` | 0x1290 | 0xf | stored | 1 | docs/re/xml_tags.md |
| `Command_Bar_Default_Font_Name` | 0x4c0 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Command_Bar_Default_Font_Size` | 0x4e0 | 0x5 | stored | 1 | docs/re/xml_tags.md |
| `Corrupt_Side_Leader_Name` | 0x1380 | 0x1b | stored | 1 | docs/re/xml_tags.md |
| `Corrupt_Side_Name` | 0x1360 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Choice_Benefit` | 0x11fb0 | 0x2a | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Choice_Encyclopedia` | 0x11fe0 | 0x2a | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Choice_Icon_Name` | 0x11f80 | 0x2a | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Choice_Name` | 0x11f98 | 0x2a | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Encyclopedia_Backdrop` | 0x12148 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Encyclopedia_Complete` | 0x12188 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Encyclopedia_Header` | 0x12168 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Encyclopedia_Incomplete` | 0x121a8 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Encyclopedia_Left_Edge` | 0x121e8 | 0x6 | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Encyclopedia_Money_Icon` | 0x121c8 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Encyclopedia_Spacing` | 0x121ec | 0x6 | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Line_Grow_Seconds` | 0x11f58 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Line_Radius` | 0x11f50 | 0x8 | stored | 1 | docs/re/FIXPASS_GALACTIC.md |
| `Corruption_Line_Start_End_Offset` | 0x11f54 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Mission_Requirement_Icon_Name` | 0x11fc8 | 0x2a | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Planet_Icon_Encyclopedia_Desc` | 0x12028 | 0x2a | stored | 1 | docs/re/xml_tags.md |
| `Corruption_Planet_Icon_Encyclopedia_Name` | 0x12010 | 0x2a | stored | 1 | docs/re/xml_tags.md |
| `Display_Bink_Movie_Frames` | 0x11708 | 0x0 | stored | 1 | docs/re/xml_tags.md |
| `Droid_Encyclopedia_Offset` | 0x1298 | 0xf | stored | 1 | docs/re/xml_tags.md |
| `Encyclopedia_Class_Y_Offset` | 0x13b0 | 0x6 | stored | 1 | docs/re/xml_tags.md |
| `Encyclopedia_Cost_Offset` | 0x13a4 | 0x6 | stored | 1 | docs/re/xml_tags.md |
| `Encyclopedia_Icon_X_Offset` | 0x13a8 | 0x6 | stored | 1 | docs/re/xml_tags.md |
| `Encyclopedia_Icon_Y_Offset` | 0x13ac | 0x6 | stored | 1 | docs/re/xml_tags.md |
| `Encyclopedia_Name_Offset` | 0x13a0 | 0x6 | stored | 1 | docs/re/xml_tags.md |
| `Encyclopedia_Population_Offset` | 0x139c | 0x6 | stored | 1 | docs/re/HUD_GAPS.md |
| `Event_Message_Default_Font_Name` | 0x420 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Event_Message_Default_Font_Size` | 0x440 | 0x5 | stored | 1 | docs/re/xml_tags.md |
| `Evil_Side_Leader_Name` | 0x1340 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `GMC_Battle_Fade_Time` | 0x1c0 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GMC_Battle_Zoom_Time` | 0x1bc | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Cycle_Speed` | 0x1208 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Darken_Level` | 0x120c | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Flash_Duration` | 0x1200 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Flash_Level` | 0x11fc | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Hilite_Level` | 0x121c | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Planet_Fade_Duration` | 0x1224 | 0x8 | stored | 1 | docs/re/GALACTIC_REVEAL_CORRUPTION.md |
| `GUI_Planet_Flash_Level` | 0x1220 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Rapid_Flash_Duration` | 0x1204 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Strategic_Countdown_Timers_Screen_Spacing` | 0x123c | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Strategic_Countdown_Timers_Screen_X` | 0x1234 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Strategic_Countdown_Timers_Screen_Y` | 0x1238 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Tactical_Countdown_Timers_Screen_Spacing` | 0x1230 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Tactical_Countdown_Timers_Screen_X` | 0x1228 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Tactical_Countdown_Timers_Screen_Y` | 0x122c | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `Galactic_Zoom_In_Station_Offset` | 0x5f0 | 0x10 | stored | 1 | docs/re/xml_tags.md |
| `Galactic_Zoom_In_Station_Rotation` | 0x5fc | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `Game_Object_Name_Font_Name` | 0x510 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Game_Object_Name_Font_Size` | 0x530 | 0x5 | stored | 1 | docs/re/xml_tags.md |
| `Good_Side_Leader_Name` | 0x1300 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Health_Bar_Spacing` | 0x11e7c | 0x8 | stored | 1 | docs/re/UI.md |
| `Health_Critical_Percent_Threshold` | 0x11b3c | 0x8 | stored | 1 | docs/re/TACTICAL_GAPS.md |
| `Icons_Per_Column` | 0x124c | 0x6 | stored | 1 | - |
| `In_Game_Message_Default_Font_Name` | 0x3f8 | 0x17 | stored | 1 | - |
| `In_Game_Message_Default_Font_Size` | 0x418 | 0x5 | stored | 1 | - |
| `Large_Coin_Stack_Size` | 0x188 | 0x8 | stored | 1 | - |
| `Left_Queue_Tint` | 0x13c4 | 0x16 | stored | 1 | - |
| `MP_Default_Game_Timer` | 0x5a0 | 0x6 | stored | 1 | - |
| `MP_Default_Land_Tactical_Win_Condition` | 0x5b8 | 0xe | stored | 1 | - |
| `MP_Default_Space_Tactical_Win_Condition` | 0x5bc | 0xe | stored | 1 | - |
| `MP_Default_Win_Condition` | 0x5a4 | 0x6 | stored | 1 | - |
| `MP_Default_Win_Condition_Float_Param` | 0x5ac | 0x8 | stored | 1 | - |
| `MP_Default_Win_Condition_Int_Param` | 0x5a8 | 0x6 | stored | 1 | - |
| `Map_Preview_Image_Size` | 0x2a4 | 0x6 | stored | 1 | - |
| `Medium_Coin_Stack_Size` | 0x184 | 0x8 | stored | 1 | - |
| `Min_Accuracy_For_Icon` | 0x13bc | 0x8 | stored | 1 | - |
| `Min_Sight_Range_For_Icon` | 0x13c0 | 0x8 | stored | 1 | - |
| `Mouse_Over_Highlight_Scale` | 0x11ec | 0x8 | stored | 1 | - |
| `Multiplayer_Losing_Team_Bonus_Credit_Percentage` | 0x5c0 | 0x8 | stored | 1 | - |
| `Num_Structures_For_Large_Planet_Name` | 0x198 | 0x6 | stored | 1 | - |
| `Num_Structures_For_Medium_Planet_Name` | 0x194 | 0x6 | stored | 1 | - |
| `Planet_Ability_Icon_Names` | 0x1618 | 0x1b | stored | 1 | - |
| `Planet_Ability_RGBs` | 0x1648 | 0x12 | stored | 1 | - |
| `Planet_Ability_Text_IDs` | 0x1630 | 0x1b | stored | 1 | - |
| `PlayModeSwitchMovies` | 0x2f8 | 0x0 | stored | 1 | - |
| `Right_Queue_Tint` | 0x13d4 | 0x16 | stored | 1 | - |
| `Shield_Flash_Duration` | 0x1574 | 0x8 | stored | 1 | - |
| `Shield_Flash_Scale` | 0x1568 | 0x10 | stored | 1 | - |
| `ShipNameTextFiles` | 0x119a8 | 0x36 | stored | 1 | - |
| `SpaceReinforceFeedbackOnlyWhileDragging` | 0x11d4 | 0x0 | stored | 1 | - |
| `Team_Healthbar_Offset` | 0x11860 | 0x8 | stored | 1 | - |
| `Text_Button_Default_Font_Name` | 0x4e8 | 0x17 | stored | 1 | - |
| `Text_Button_Default_Font_Size` | 0x508 | 0x5 | stored | 1 | - |
| `Text_Reveal_Rate` | 0x2b4 | 0x8 | stored | 1 | - |
| `Tool_Tip_Font_Name` | 0x470 | 0x17 | stored | 1 | - |
| `Tool_Tip_Font_Size` | 0x490 | 0x5 | stored | 1 | - |
| `Tool_Tip_Small_Font_Name` | 0x498 | 0x17 | stored | 1 | - |
| `Tool_Tip_Small_Font_Size` | 0x4b8 | 0x5 | stored | 1 | - |
| `WaypointFlagModelName` | 0xd0 | 0x17 | stored | 1 | - |
| `WaypointLineLandDashLength` | 0x13e4 | 0x8 | stored | 1 | - |
| `WaypointLineLandDashVelocity` | 0x13ec | 0x8 | stored | 1 | - |
| `WaypointLineLandGapLength` | 0x13e8 | 0x8 | stored | 1 | - |
| `Win_Lose_Message_Font` | 0x380 | 0x17 | stored | 1 | - |

## GameObjectType (50)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Show_Name` | 0x23 | 0x0 | stored | 112 | - |
| `MP_Encyclopedia_Text` | 0x1898 | 0x2a | stored | 99 | - |
| `Tactical_Build_Start_Lower_Z` | 0xa04 | 0x8 | stored | 80 | - |
| `Radar_Icon_Scale_Land` | 0x1d54 | 0x8 | stored | 74 | - |
| `Radar_Icon_Scale_Space` | 0x1d58 | 0x8 | stored | 74 | - |
| `GUI_Velocity` | 0x1b0c | 0x8 | stored | 67 | docs/re/xml_tags.md |
| `Name_Adjust` | 0x65c | 0x10 | stored | 67 | docs/re/OBJECT_TYPES.md |
| `Describe_Wildlife` | 0x1bc8 | 0x17 | stored | 64 | docs/re/GALACTIC_DIALOGS.md |
| `Encyclopedia_Weather_Info` | 0x1910 | 0x17 | stored | 64 | docs/re/xml_tags.md |
| `Encyclopedia_Weather_Name` | 0x18f0 | 0x17 | stored | 64 | docs/re/xml_tags.md |
| `GUI_Distance` | 0x1b10 | 0x8 | stored | 63 | docs/re/xml_tags.md |
| `Include_In_UI_Map_Header` | 0xb2 | 0x0 | stored | 62 | - |
| `Planet_Ability_Name` | 0x2250 | 0x17 | stored | 62 | - |
| `Corruption_1_Mission_Briefing` | 0x2598 | 0x17 | stored | 60 | docs/re/xml_tags.md |
| `Corruption_1_Success_Bink_Hologram_Name` | 0x2530 | 0x17 | stored | 60 | docs/re/xml_tags.md |
| `Visible_To_Enemies_When_Empty` | 0x98 | 0x0 | stored | 58 | docs/re/L1_S4_REMAINING.md |
| `Build_Tab_Special_Structures` | 0x4f | 0x0 | stored | 56 | docs/re/xml_tags.md |
| `Corruption_2_Mission_Briefing` | 0x2738 | 0x17 | stored | 49 | docs/re/xml_tags.md |
| `Corruption_2_Success_Bink_Hologram_Name` | 0x26d0 | 0x17 | stored | 49 | docs/re/xml_tags.md |
| `User_Bound_Max` | 0x30c | 0x10 | stored | 40 | - |
| `User_Bound_Min` | 0x300 | 0x10 | stored | 40 | - |
| `Hides_When_Built_On` | 0x97 | 0x0 | stored | 35 | docs/re/L1_S4_REMAINING.md |
| `Build_Tab_Space_Station` | 0x51 | 0x0 | stored | 31 | docs/re/xml_tags.md |
| `Corruption_1_Failure_Bink_Hologram_Name` | 0x2578 | 0x17 | stored | 30 | docs/re/xml_tags.md |
| `Enemy_Spawn_Text` | 0x19e8 | 0x2a | stored | 29 | docs/re/xml_tags.md |
| `Destroyed_Space_Tactical_Map` | 0x6b0 | 0x17 | stored | 25 | docs/re/BATTLE_TRANSITIONS.md |
| `Friendly_Spawn_Text` | 0x19d0 | 0x2a | stored | 21 | docs/re/xml_tags.md |
| `Build_Tab_Outpost` | 0x50 | 0x0 | stored | 16 | docs/re/xml_tags.md |
| `Radar_Range_Icon_Name` | 0x1728 | 0x17 | stored | 12 | docs/re/OBJECT_TYPES.md |
| `Build_Tab_Heroes` | 0x4e | 0x0 | stored | 11 | docs/re/xml_tags.md |
| `Secondary_Objective` | 0x19b0 | 0x17 | stored | 9 | - |
| `Show_Hero_Head` | 0x17 | 0x0 | stored | 9 | - |
| `Corruption_3_Mission_Briefing` | 0x28d8 | 0x17 | stored | 8 | docs/re/xml_tags.md |
| `Corruption_3_Success_Bink_Hologram_Name` | 0x2870 | 0x17 | stored | 8 | docs/re/xml_tags.md |
| `Use_Special_Submit_Rules` | 0xd5 | 0x0 | stored | 7 | docs/re/MOVEMENT.md |
| `End_Control_Transition_Radar_Event` | 0x1778 | 0x17 | stored | 6 | docs/re/xml_tags.md |
| `GUI_Angles` | 0x1b00 | 0x10 | stored | 6 | docs/re/xml_tags.md |
| `GUI_X_Rot` | 0x1b14 | 0x8 | stored | 6 | docs/re/xml_tags.md |
| `Corruption_3_Failure_Bink_Hologram_Name` | 0x28b8 | 0x17 | stored | 5 | docs/re/xml_tags.md |
| `Radar_Rotate_Icon` | 0xd6 | 0x0 | stored | 5 | docs/re/UI.md |
| `Not_Really_Selectable` | 0xe7 | 0x0 | stored | 2 | docs/re/COMMANDS.md |
| `Radar_Register_As_Foreground_Object` | 0xdd | 0x0 | stored | 2 | - |
| `Beacon_Lifetime_In_Secs` | 0x17b8 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `Beacon_Radar_Map_Event_Name` | 0x1798 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Build_Countdown_Text_ID` | 0x8d8 | 0x17 | stored | 1 | docs/re/MCX4_T09_RE_ANSWERS.md |
| `Corruption_2_Failure_Bink_Hologram_Name` | 0x2718 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Galactic_Fleet_Override_Icon_Index` | 0x1930 | 0x5 | stored | 1 | docs/re/GALAXY_MAP_PRESENTATION.md |
| `Hero_Ability` | 0x1860 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Radar_Clip_To_Visible_Region` | 0xde | 0x0 | stored | 1 | docs/re/UI.md |
| `TSW_Post_Music_Wait_Frames` | 0x21a8 | 0x6 | stored | 1 | - |

## Faction (39)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Fleet_Icon_Name` | 0x1d8 | 0x17 | unread | 8 | docs/re/xml_tags.md |
| `Ground_Base_Icon_Name` | 0x178 | 0x17 | unread | 8 | docs/re/xml_tags.md |
| `Ground_Transport_Icon_Name` | 0x1b8 | 0x17 | unread | 8 | docs/re/xml_tags.md |
| `Planet_Icon_Offset` | 0x39c | 0x10 | unread | 8 | - |
| `Planet_Icon_Scale` | 0x398 | 0x8 | unread | 8 | - |
| `Ship_Icon_Name` | 0x1f8 | 0x17 | unread | 8 | - |
| `Squadron_Icon_Name` | 0x218 | 0x17 | unread | 8 | - |
| `Star_Base_Icon_Name` | 0x198 | 0x17 | unread | 8 | - |
| `Alternate_Icon_Prefix` | 0x358 | 0x17 | unread | 3 | docs/re/xml_tags.md |
| `Carrier_Icon_Name` | 0x278 | 0x17 | unread | 3 | docs/re/xml_tags.md |
| `Corvette_Icon_Name` | 0x2b8 | 0x17 | unread | 3 | docs/re/xml_tags.md |
| `Default_Transmission_Message` | 0x2f38 | 0x17 | unread | 3 | docs/re/GALACTIC_DIALOGS.md |
| `Defeat_Text` | 0x2e30 | 0x17 | unread | 3 | docs/re/xml_tags.md |
| `Fighter_Icon_Name` | 0x2d8 | 0x17 | unread | 3 | docs/re/xml_tags.md |
| `Finale_Movie` | 0x2db0 | 0x17 | unread | 3 | docs/re/xml_tags.md |
| `Frigate_Icon_Name` | 0x298 | 0x17 | unread | 3 | docs/re/xml_tags.md |
| `Generic_Win_Movie` | 0x2d90 | 0x17 | unread | 3 | docs/re/xml_tags.md |
| `Helper_Icon_Name` | 0x26b8 | 0x17 | unread | 3 | docs/re/xml_tags.md |
| `Infantry_Icon_Name` | 0x238 | 0x17 | unread | 3 | - |
| `Land_Ability_Targeting_Range_Overlay_Material_Name` | 0x2d38 | 0x17 | unread | 3 | - |
| `Land_Ability_Targeting_Range_Overlay_Scale_Factor` | 0x2d68 | 0x8 | unread | 3 | - |
| `Land_Area_Effect_Range_Overlay_Material_Name` | 0x2d00 | 0x17 | unread | 3 | - |
| `Land_Area_Effect_Range_Overlay_Scale_Factor` | 0x2d30 | 0x8 | unread | 3 | - |
| `Land_Lose_Image` | 0x2eb0 | 0x17 | unread | 3 | - |
| `Land_Retreat_Countdown_Text_ID` | 0x21b0 | 0x17 | unread | 3 | - |
| `Land_Win_Image` | 0x2e90 | 0x17 | unread | 3 | - |
| `Minimum_Visible_Base_Level` | 0x20ac | 0x6 | unread | 3 | - |
| `Space_Lose_Image` | 0x2e70 | 0x17 | unread | 3 | - |
| `Space_Retreat_Countdown_Text_ID` | 0x20e0 | 0x17 | unread | 3 | - |
| `Space_Win_Image` | 0x2e50 | 0x17 | unread | 3 | - |
| `Standalone_Space_Maps_Special_Weapon_A` | 0x2b00 | 0x1d | unread | 3 | - |
| `Standalone_Space_Maps_Special_Weapon_B` | 0x2b10 | 0x1d | unread | 3 | - |
| `Superweapon_Win_Movie` | 0x2d70 | 0x17 | unread | 3 | - |
| `Tactical_Intro_Command_Bar_Movie_Name` | 0x2f90 | 0x17 | unread | 3 | docs/re/AUDIO.md |
| `Vehicle_Icon_Name` | 0x258 | 0x17 | unread | 3 | - |
| `Victory_Text` | 0x2e10 | 0x17 | unread | 3 | - |
| `Finale_Movie_2` | 0x2dd0 | 0x17 | unread | 1 | docs/re/xml_tags.md |
| `Post_Credits_Movie` | 0x2df0 | 0x17 | unread | 1 | - |
| `Text_Nickname_ID` | 0x48 | 0x17 | unread | 1 | - |

## Campaign (3)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Markup_Filename` | 0x210 | 0x1b | stored | 259 | docs/re/AI_GALACTIC_2.md |
| `Show_Completed_Tab` | 0x379 | 0x0 | stored | 58 | docs/re/MCX4_RE_ANSWERS_GALACTIC.md |
| `Is_Multiplayer` | 0xc8 | 0x0 | stored | 8 | docs/re/BATTLE_TRANSITIONS.md |

## CommandBarComponent (8)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Should_Ghost` | 0x3e2 | 0x50 | unread | 198 | docs/re/UI.md |
| `Stackable` | 0x3e1 | 0x50 | unread | 145 | docs/re/UI.md |
| `Scale_Duration` | 0x3a8 | 0x8 | unread | 115 | docs/re/UI.md |
| `Snap_Drag` | 0x3e3 | 0x50 | unread | 115 | docs/re/UI.md |
| `Snap_Location` | 0x3e3 | 0x50 | unread | 115 | docs/re/UI.md |
| `Ghost_Base_Only` | 0x3e3 | 0x50 | unread | 56 | docs/re/UI.md |
| `Can_Drag_Stack` | 0x3e4 | 0x50 | unread | 20 | docs/re/UI.md |
| `Cross_Fade` | 0x3e2 | 0x50 | unread | 4 | docs/re/UI.md |

## HardPoint (1)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Tooltip_Text` | 0x120 | 0x17 | unread | 431 | docs/re/HUD_GAPS.md |

## Laser_Defense_Ability (1)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Lightning_Effect_Name` | 0x1e8 | 0x17 | stored | 5 | docs/re/ABILITY_PARTICLES.md |

## MousePointer (6)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Icon_Name` | 0x98 | 0x17 | unread | 1016 | docs/re/BKG1_RE_ANSWERS.md |
| `Apply_To_All_Allies` | 0x178 | 0x0 | unread | 46 | docs/re/A2M_R2_RE_B.md |
| `Affects_All_Allies` | 0x198 | 0x0 | unread | 30 | docs/re/TACTICAL_GAPS.md |
| `Ability_Name` | 0x178 | 0x17 | unread | 15 | docs/re/xml_tags.md |
| `Required_Corruption_Level` | 0x178 | 0x6 | unread | 2 | - |
| `Transition_Time_In_Secs` | 0x17c | 0x8 | unread | 2 | - |

## Movie (3)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Overlay` | 0xd1 | 0x0 | unread | 9 | - |
| `Pause_Game_While_Playing` | 0xd3 | 0x0 | unread | 5 | - |
| `Movie_Start_Frame` | 0x20 | 0x6 | unread | 1 | - |

## RadarMapSettings (5)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Land_Is_Guide_Rectangle` | 0x130 | 0x0 | unread | 1 | - |
| `Land_Position_LeftTop` | 0xec | 0xf | unread | 1 | - |
| `Space_Is_Guide_Rectangle` | 0xc0 | 0x0 | unread | 1 | - |
| `Space_Position_LeftTop` | 0x7c | 0xf | unread | 1 | - |
| `Use_Event_System` | 0x51d | 0x0 | unread | 1 | - |

## Tractor_Beam_Attack_Ability (1)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Lightning_Effect_Name` | 0x190 | 0x17 | stored | 5 | docs/re/ABILITY_PARTICLES.md |

## TradeRoute (2)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Land_Position_LeftTop` | 0xec | 0xf | unread | 1 | - |
| `Space_Position_LeftTop` | 0x7c | 0xf | unread | 1 | - |

## Unit_Ability (2)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Alternate_Description_Text` | 0x1e0 | 0x17 | stored | 34 | docs/re/xml_tags.md |
| `Alternate_Name_Text` | 0x1c0 | 0x17 | stored | 31 | docs/re/xml_tags.md |

