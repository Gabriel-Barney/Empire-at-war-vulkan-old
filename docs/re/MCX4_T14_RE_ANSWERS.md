# mcx4 t14: RE answers, ui rows of the honour gaps

Answers to `MCX4_T14_RE_QUESTIONS.md` (lane mcx4-pres, roadmap group `mcx4` task t14, 169 ui rows) and to the mcx8 t09
question about skirmish flag 0x20. Retail `StarWarsG.exe` FoC x64 (Steam), 2026-11-18. Addresses are RVAs (image base
0x140000000). Behaviour only, no decompiled code. **[H]** read and cross-checked, **[M]** read, one link inferred,
**[L]** inferred. This run had the ReVa turn (ReVa decompiles plus an offline sweep of the image); nothing was renamed in
the shared project.

## Method

As in `MCX4_T13_RE_ANSWERS.md` ("Method"), with these additions:

- **Sweep.** A capstone linear sweep over `.pdata` functions, every Ghidra function range (leaf functions have no
  `.pdata`) and the gaps between them (1,984,641 instructions), indexing every displacement (base, index, scale), every
  RIP-relative target and every immediate. GameConstants (one global at 0x140b15800) fields are found by their RIP
  targets plus the GameConstants member functions called with `rcx` = &GameConstants (including the tiny getters
  0x2c7bf0 .. 0x2c7d60).
- **Provenance filter for small offsets.** Object fields below about 0x400 hit thousands of unrelated displacements. A
  hit counts only when its base register was loaded from `[r + 0x68]` (player → faction) or `[r + 0x298]` (object →
  type), or when the field address is formed by an immediate add on such a pointer, or when the hit is inside the
  owning class's methods (vtables from `docs/re/ghidra/classes.csv` plus their direct callees).
- **Parser type codes used here.** 0x17 string (std::string: size at +0x10, capacity at +0x18, so a test of
  "offset + 0x10" is an "is the string empty" test); 0x1d object type reference (the resolved type pointer is at
  **offset + 8**; readers use that); 0x2a / 0x1b string lists (DynamicVector: data +8, count +0x10); 0x13 / 0x14 int and
  RGB lists (same layout); 0x16 RGBA (four floats 0..1); 0x50 has no offset: the table holds a **setter function** that
  sets one bit of a flag byte (see `CommandBarComponent` below).
- **Exclusions** (never readers): those of T13, plus the HUD layout bulk copy 0x31b590 and the string-table builder
  0x5b43d0.
- **Retail-unused** below means every hit of the field is one of the exclusions, or a displacement whose base is not
  the owning object (checked by provenance). **Read, no effect** means a reader exists but its value is always
  overwritten or never used afterwards; it is counted with retail-unused.

## Summary

Counts: **124 tags have a reader, 33 are retail-unused (or read with no effect), 1 is out of scope, 11 are still
open, 0 are listed under the wrong class.** mcx8 t09: **yes**, the skirmish options dialog sets flag 0x20 (section at
the end).

| Tag(s) | Reader (RVA) or retail-unused | What it does | Conf |
|---|---|---|---|
| GC `Auto_Rotate_For_Space_Targeting` | space targeting service 0x633ae0 | lets every unit turn to face its target | H |
| GC `Battle_Load_Planet_Ambient`, `_Direction`, `_Viewport` | battle load planet 0x8f6c0, planet info 0xd3eb0 | camera, ambient, viewport of the planet model | H |
| GC `Battle_Pending_Message_Font`, `_Font_Size`, `_Pos_X`, `_Pos_Y` | **retail-unused** (font built, never drawn) | | H |
| GC `CB_Flash_Count` / `CB_Flash_Duration` | 0x27fad0, 0x287e70 / **retail-unused** | flash count of `b_reinforcement` | H |
| GC `Command_Bar_Default_Font_Name`, `_Size`, `Text_Button_Default_Font_Name`, `_Size` | text components 0x34a4b0, 0x349a20, 0x351d50 | shared default text font | H |
| GC `Game_Object_Name_Font_Name`, `_Size` | boot 0x4df10 → mode manager font (0x35e440) | in-world text font | H |
| GC `Tool_Tip_Font_Name`, `_Size`, `Tool_Tip_Small_Font_Name`, `_Size` | boot 0x3b1a60 | the two tooltip fonts | H (M use) |
| GC `In_Game_Message_Default_Font_Name`, `_Size`, `Event_Message_Default_Font_Name`, `_Size` | **read, no effect** / **retail-unused** | | H |
| GC `Win_Lose_Message_Font`, `_Font_Size` | 0x28ab50 → banner 0x365150 | tactical win / lose banner font | H |
| GC `Good_Side_Leader_Name`, `Evil_Side_Leader_Name`, `Corrupt_Side_Name`, `Corrupt_Side_Leader_Name` | sandbox objectives 0x32bd10, statistics chart 0x970b0, Load_Plots 0x28dbe0, HUD 0x2fd330 / 0x30ae50, galaxy map 0x3d3200 | side identification | H |
| GC corruption choice / encyclopedia family (13 tags) | choice menu 0x3c6eb0, tooltips 0x2e0c20 / 0x2e2910 / 0x2eedc0, encyclopedia 0x2ed780 | texts, icons, layout | H |
| GC `Corruption_Line_Radius`, `_Start_End_Offset`, `_Grow_Seconds` | PlanetaryBehavior 0x3fbfe0 | corruption lines between planets | H |
| GC `Display_Bink_Movie_Frames` | 0x44dea0 → 0x44f9e0 | **out of scope** (debug frame counter) | H |
| GC `Droid_Encyclopedia_Offset` | HUD callback 0x9db00 → pop-up placement 0x308890 | pop-up offset | M |
| GC `GMC_Battle_Zoom_Time`, `GMC_Battle_Fade_Time` | galactic camera 0x44a3f0 | zoom to battle, fade | H |
| GC `GUI_Cycle_Speed`, `GUI_Darken_Level` / `GUI_Hilite_Level` | colour callback 0x3a0c20 mode 4 / **read, no effect** (modes 5, 6 never set) | | H / M |
| GC `GUI_Strategic_Countdown_Timers_Screen_*`, `GUI_Tactical_Countdown_Timers_Screen_*` (6) | 0x3ce60, 0x3d2b0, 0x2ee40, 0x3c9e0 | countdown timer column | H |
| GC `Galactic_Zoom_In_Station_Rotation` / `_Offset` | 0x3c9fc0, 0x3e7840 / **retail-unused** | station spin in planet zoom-in | H |
| GC `Large_Coin_Stack_Size`, `Medium_Coin_Stack_Size`, `Map_Preview_Image_Size` | **retail-unused** | | H |
| GC `Left_Queue_Tint`, `Right_Queue_Tint` | queue items 0x3e6220, 0x31a940 | build queue tint | H |
| GC `Mouse_Over_Highlight_Scale` | colour callback 0x3a0c20 → 0x3c26c0 | hover brightening | H |
| GC `Multiplayer_Losing_Team_Bonus_Credit_Percentage` | 0x3615d0 (skirmish / MP, three teams) | catch-up credits | H |
| GC `Num_Structures_For_Medium_Planet_Name`, `_Large_` | planet labels 0x3ea1b0 | label size class | H |
| GC `Planet_Ability_Icon_Names`, `_RGBs`, `_Text_IDs` | droid log legend 0x9db60 | planet ability legend | H |
| GC `PlayModeSwitchMovies` | START_MOVIE reward 0x28bc10 | movies on / off | H |
| GC `Shield_Flash_Duration`, `Shield_Flash_Scale` | shield hit 0x549ac0 | shield flash light | H |
| GC `ShipNameTextFiles` | ship names manager 0x4e6b20 | name list files | M |
| GC `SpaceReinforceFeedbackOnlyWhileDragging` | SpaceReinforceFOWSampler 0x4d8de0 | placement feedback gate | M |
| GC `Team_Healthbar_Offset` | health bar anchor 0x2e7fd0 | bar height for team members | M |
| GC `Text_Reveal_Rate` | 0x31d750 | typewriter characters per second | H |
| GC `WaypointFlagModelName`, `WaypointLineLandDashLength`, `_GapLength`, `_DashVelocity` | waypoint draw 0x3e240, 0x3e9e0 | flag model, dashed line | H |
| Type `Beacon_Lifetime_In_Secs`, `Beacon_Radar_Map_Event_Name` | BeaconBehavior 0x647460 / 0x6474f0, PlaceBeaconEvent 0x436540 | beacon life, radar event | H |
| Type `Corruption_1/2/3_Failure_Bink_Hologram_Name`, `_Mission_Briefing` (6) | 0x371370 → 0x28bf10; 0x371410 → 0x52c570 | failure hologram, briefing | H (M briefing use) |
| Type `Encyclopedia_Weather_Name`, `_Info` | 0x2f0af0 (tooltip 0x2e0c20) | weather lines of the planet pop-up | M |
| Type `End_Control_Transition_Radar_Event` | capture complete 0x500e30 | radar event on capture | H |
| Type `Enemy_Spawn_Text`, `Friendly_Spawn_Text`, `Secondary_Objective` | sandbox objectives 0x32bd10 | objective texts | M |
| Type `GUI_Angles`, `GUI_Distance`, `GUI_Velocity`, `GUI_X_Rot` | **retail-unused** | | M |
| Type `Hero_Ability` | 0x2ec250 (pop-up 0x2f15d0) | hero ability line | M |
| Type `MP_Encyclopedia_Text` | 0x2de200 | replaces `Encyclopedia_Text` in skirmish / MP | H |
| Type radar family (6) | radar 0x35b4d0, 0x35c000, 0x362580, 0x4e7e30 | blip scale, rotation, layer, clip, range icon | H (M some) |
| Type `Show_Hero_Head`, `Show_Name` | 0x318720; 0x3989a0 → 0x3a4510 | hero portrait gate; name label | M |
| Type `Tactical_Build_Start_Lower_Z` | **retail-unused** | | M |
| Type `Include_In_UI_Map_Header`, `Name_Adjust`, `Planet_Ability_Name`, `TSW_Post_Music_Wait_Frames` | **open** | | |
| Faction `Alternate_Icon_Prefix` | HUD 0x30c3b0 → icon components 0x352d80 | faction icon prefix | M |
| Faction `Infantry_Icon_Name`, `Fighter_Icon_Name` | commander icons 0x3df8b0 | battle pending side icon | H |
| Faction `Helper_Icon_Name` | droid log 0x9db60, 0x300a00 | helper droid picture | H |
| Faction 10 other icon names, `Planet_Icon_Offset`, `Planet_Icon_Scale`, `Text_Nickname_ID` | **retail-unused** | | M |
| Faction `Victory_Text`, `Defeat_Text`, win movies (5), `Land/Space_Win/Lose_Image` (4) | game over 0x92bb0; battle end 0x88fe0 | end screens | H |
| Faction `Land_Ability_Targeting_Range_Overlay_*`, `Land_Area_Effect_Range_Overlay_*` (4) | 0x35280, 0x356b0, 0x35560, 0x35c30, 0x2ee40 | ability range blobs | H (M draw) |
| Faction `Land_Retreat_Countdown_Text_ID`, `Space_Retreat_Countdown_Text_ID` | retreat countdown 0x3ce60 | countdown label | H |
| Campaign `Show_Completed_Tab` | Load_Plots 0x28dbe0 → objectives dialog 0x107280 | shows the Completed tab | H |
| CommandBarComponent flag family (7 bits) / `Scale_Duration` | component draw / drag code / **open** | | M |
| HardPoint `Tooltip_Text`, RadarMapSettings (3), Unit_Ability `Alternate_Name_Text`, `Alternate_Description_Text` | **open** | | |
| Laser_Defense_Ability `Lightning_Effect_Name` | **retail-unused** | | M |
| Movie `Movie_Start_Frame` | Bink open 0x44d920 | start frame | H |

## GameConstants.Auto_Rotate_For_Space_Targeting

Settles `Auto_Rotate_For_Space_Targeting` (+0x15d0, bool, FoC False). [H]

- Reader: the space targeting service `TargetingBehaviorClass` 0x633ae0 (the land one 0x634810 does not read it).
- When a unit has a target and is free to turn (its turn byte +0x333 is set and its locomotor is not busy), the service
  turns the unit's facing towards the target, but only if **any** of: this constant is true; the behaviour's own
  "always rotate" virtual (+0x108) says so; the list at object +0x2d0 is empty (its hard point list [M]). The turn is
  made only when the yaw error exceeds 10 degrees (locomotor virtual +0x218 with the wanted heading).
- With FoC False, space units with hard points keep their heading and rely on turrets; hard-point-less ships turn.

## GameConstants.Battle_Load_Planet_Direction family (battle load planet render)

Settles `Battle_Load_Planet_Direction` (+0x11884, vec3), `Battle_Load_Planet_Ambient` (+0x11890, vec3),
`Battle_Load_Planet_Viewport` (+0x11850, four floats). [H]

- Readers: the battle load dialog's planet render 0x8f6c0 (called by the dialog open 0x8cb50) and the planet info
  dialog fill 0xd3eb0 (same set-up, GALACTIC_DIALOGS §9.2).
- The planet model is fitted by its bounding sphere: the camera is placed at the planet centre plus `Direction` times
  (radius / 2 / tan(fov / 2)), so the planet fills the view whatever its size. `Ambient` is the scene ambient colour.
  Three lights are set by the code itself (fixed values). `Viewport` (x, y, w, h in screen fractions) is the render
  viewport of the planet image. FoC: direction 0, 0.75, 0.55; ambient 0.1, 0.1, 0.13; viewport 0, 1, 1, 0.

## GameConstants.Battle_Pending_Message_Font family

Settles `Battle_Pending_Message_Font` (+0x548), `Battle_Pending_Message_Font_Size` (+0x568),
`Battle_Pending_Message_Pos_X` (+0x56c), `Battle_Pending_Message_Pos_Y` (+0x570). **Retail-unused.** [H]

- The mode manager init 0x28ab50 builds a font from `_Font` / `_Font_Size` into mode manager +0x90 (0x140b15470) once.
  The only other touches of that slot are the boot clear 0x16fa0 and the reset 0x28d940 (which frees it): no draw uses
  it. The pending dialog takes its title font from its own component (GALACTIC_DIALOGS §1.3).
- `_Pos_X` / `_Pos_Y`: no reader at all (ctor / defaults only).

## GameConstants.CB_Flash_Count

Settles `CB_Flash_Count` (+0x1248, int, FoC 4) and `CB_Flash_Duration` (+0x1244). [H]

- Readers of the count: 0x27fad0 (a type added to a player's tactical reinforcement pool) and Player::Update 0x287e70
  (the same add, in its loop). When the player is the local player, both call the command-bar flash 0x2f5440 on
  component 0xe7, which is **`b_reinforcement`** (name table 0x140a174e0, filled by 0x347da0), with duration −1 and
  `CB_Flash_Count` as the flash count: duration −1 makes the flash run by count (0x2f50c0, the same flash the
  FLASH_GUI story reward uses).
- `CB_Flash_Duration`: **retail-unused** (no reader).

## GameConstants.Command_Bar_Default_Font_Name family (text component default fonts)

Settles `Command_Bar_Default_Font_Name` (+0x4c0, FoC EmpireAtWar-Medium), `Command_Bar_Default_Font_Size` (+0x4e0, 7),
`Text_Button_Default_Font_Name` (+0x4e8, EmpireAtWar-Medium), `Text_Button_Default_Font_Size` (+0x508, 7). [H, M order]

- There is one shared default text font, 0x140b2bc88. It is created on first need by whichever component is built
  first: a text component (ctor 0x34a4b0) creates it from `Command_Bar_Default_Font_Name` + `_Size`; a text button
  (setup 0x349a20) creates it from `Text_Button_Default_Font_Name` + `_Size`. The HUD can replace it (0x351d20, called
  from 0x2fc390 at HUD load). With FoC values both pairs are equal, so the order does not matter.
- Text components (draw 0x352250) and text buttons (0x3509e0) use the shared font when they have no font of their own.
- A component with a font size but no font **name** uses `Command_Bar_Default_Font_Name` as the name (0x349a20,
  0x351d50), with its own size; when the text does not fit the width, the size is stepped down to 6 at least.

## GameConstants.Game_Object_Name_Font_Name

Settles `Game_Object_Name_Font_Name` (+0x510, EmpireAtWar-Medium) and `Game_Object_Name_Font_Size` (+0x530, 7). [H, M use]

- Boot 0x4df10 (Init_3D_Game) creates font 0x140a61a10 from them; a failure aborts the 3D init.
- That font is handed to the game mode manager (0x28ab50, stored at manager +0x58, returned by GameModeClass vf67
  0x35e440) and to the command bar (0x447190, +0x78). vf67 is called by the HUD text draws (waypoints 0x3e240, HUD
  0x40640 / 0x41780, 0x2f6c30, 0x307de0 and others): it is the in-world / HUD text font (object names, floating text)
  [M which texts exactly].

## GameConstants.Tool_Tip_Font_Name family

Settles `Tool_Tip_Font_Name` (+0x470, Arial), `Tool_Tip_Font_Size` (+0x490, 7), `Tool_Tip_Small_Font_Name` (+0x498,
Arial), `Tool_Tip_Small_Font_Size` (+0x4b8, 5). [H read, M use]

- 0x3b1a60 (called by boot 0x4df10) creates two fonts into object 0x140a286f0: +0xc0 from `Tool_Tip_Font_*`, +0xc8
  from `Tool_Tip_Small_Font_*`. That object is the tooltip renderer [M]; the second font is the small line font.

## GameConstants.In_Game_Message_Default_Font_Name family

Settles `In_Game_Message_Default_Font_Name` (+0x3f8), `In_Game_Message_Default_Font_Size` (+0x418),
`Event_Message_Default_Font_Name` (+0x420), `Event_Message_Default_Font_Size` (+0x440). [H]

- `In_Game_Message_*`: **read, no effect.** The message-list set-up 0x49110 copies them as the default, then takes an
  explicit name and size if given. Its only caller (boot 0x4b250) sets up the two message lists (0x1409cf320,
  0x1409cf390) with explicit fonts, "Arial Bold" 9 and "Arial" 7, so the constants are always overwritten.
- `Event_Message_*`: **retail-unused** (no reader).

## GameConstants.Win_Lose_Message_Font

Settles `Win_Lose_Message_Font` (+0x380, EmpireAtWar-Bold) and `Win_Lose_Message_Font_Size` (+0x3a0). [H]

- The mode manager init 0x28ab50 creates the font into manager +0x88 (0x140b15468) once.
- Reader: the tactical win / lose banner, GameModeClass vf77 0x365150. It removes the previous banner, then: if the
  local player is the winner or allied to it, draws the text in `Win_Message_Color` (+0x360); else, if the local player
  is an enemy of the winner, in `Lose_Message_Color` (+0x370); else nothing. Text centred horizontally (x = 0.5 −
  width / 2), top at y = 0.4 of the screen.
- FoC GAMECONSTANTS.XML has `Win_Lose_Message_Font_Size` twice (36, then 24): the parser keeps the last, 24 [M].

## GameConstants.Good_Side_Leader_Name family (side names)

Settles `Good_Side_Leader_Name` (+0x1300, Mon_Mothma), `Evil_Side_Leader_Name` (+0x1340, Emperor_Palpatine),
`Corrupt_Side_Name` (+0x1360, UNDERWORLD), `Corrupt_Side_Leader_Name` (+0x1380, list: Tyber_Zann, Tyber_Zann_Team,
The_Peacebringer, Tyber_Zann_Team_No_Peacebringer). [H]

- Tactical **sandbox objectives** builder 0x32bd10 (SET_SANDBOX_OBJECTIVES, TRU1_C §1.5): picks the local player's side
  by comparing its faction name with `Good_Side_Name` / `Evil_Side_Name` / `Corrupt_Side_Name`, and uses the side's
  leader (`Good_Side_Leader_Name`, `Evil_Side_Leader_Name`, or any type of the corrupt list) as the leader object of
  the "protect / kill the leader" objectives.
- `Corrupt_Side_Name` also: galactic statistics chart 0x970b0 (finds the good / evil / corrupt players by faction name
  for the three lines of `IDC_DATA_CHART_IMAGE`); Load_Plots 0x28dbe0 (loads the campaign's corrupt-side plot only when
  a player of that faction exists and is active, player +0x180); HUD init 0x2fd330 / 0x30ae50 (copies the evil and
  corrupt names into the HUD, +0x2728 / +0x2748, for faction checks); galaxy map set-up 0x3d3200 (per-side models).
- Not readers: 0x2eb070 / 0x3df060 / 0x3df8b0 read GameConstants +0x1398, a separate field after the list (the
  commander icon stack divisor), not the list.

## GameConstants.Corruption_Choice_Name family (corruption choice and encyclopedia)

Settles `Corruption_Choice_Name` (+0x11f98), `Corruption_Choice_Benefit` (+0x11fb0), `Corruption_Choice_Encyclopedia`
(+0x11fe0), `Corruption_Choice_Icon_Name` (+0x11f80), `Corruption_Mission_Requirement_Icon_Name` (+0x11fc8),
`Corruption_Planet_Icon_Encyclopedia_Name` (+0x12010), `Corruption_Planet_Icon_Encyclopedia_Desc` (+0x12028),
`Corruption_Encyclopedia_Header` (+0x12168), `Corruption_Encyclopedia_Backdrop` (+0x12148),
`Corruption_Encyclopedia_Complete` (+0x12188), `Corruption_Encyclopedia_Incomplete` (+0x121a8),
`Corruption_Encyclopedia_Money_Icon` (+0x121c8), `Corruption_Encyclopedia_Left_Edge` (+0x121e8, int, FoC 101),
`Corruption_Encyclopedia_Spacing` (+0x121ec, int, FoC 50). [H reads, M layout]

- **Value shape.** The seven "choice / planet icon" tags are lists indexed by corruption type (type 0x2a). Getters,
  each returning nothing when the index is past the list: 0x2c7c50 Name, 0x2c7bf0 Benefit, 0x2c7c10 Encyclopedia,
  0x2c7c30 Icon_Name, 0x2c7c70 Mission_Requirement_Icon_Name, 0x2c7d60 Planet_Icon_Encyclopedia_Name, 0x2c7d40
  Planet_Icon_Encyclopedia_Desc. Text values are text IDs (localised through 0x1fa680).
- **Corruption choice menu** 0x3c6eb0 (the menu offered when a corruptor picks a corruption type): per choice the
  icon (`Icon_Name`), the name, the benefit text and the mission requirement icon.
- **Tooltips / pop-ups** 0x2e0c20 (galactic tooltip builder), 0x2e2910 and 0x2eedc0: the planet corruption icon tooltip
  shows `Mission_Requirement_Icon_Name` with `Planet_Icon_Encyclopedia_Name` and `_Desc`; the corruption entry of a
  planet shows `Mission_Requirement_Icon_Name` + `Choice_Name` and, when the corruption record has no text of its own,
  `Choice_Encyclopedia`. Index 9 gives the title icon of the `TEXT_CORRUPTION_ENCYCLOPEDIA_TITLE` pop-up, shown only
  when the local faction has `Benefits_From_Corruption` (+0x10b).
- **Corruption encyclopedia** 0x2ed780: header texture (`_Header`), backdrop (`_Backdrop`), one row per corruption with
  the complete / incomplete mark (`_Complete` / `_Incomplete`), the money icon (`_Money_Icon`) and the requirement icon
  (index 8 for one row); rows start at `_Left_Edge` pixels and advance by `_Spacing` pixels.

## GameConstants.Corruption_Line_Radius family

Settles `Corruption_Line_Radius` (+0x11f50, 12.5), `Corruption_Line_Start_End_Offset` (+0x11f54, 18.0),
`Corruption_Line_Grow_Seconds` (+0x11f58, 7.0). [H]

- Reader: PlanetaryBehavior 0x3fbfe0 (from its service vf6 0x3fe1b0), for each corruption line record of a planet
  (planet behaviour +0x320 list, 0x18-byte entries).
- The line runs from planet to planet; each end is pulled in by `Start_End_Offset` along the line (length − 2 ×
  offset). `Radius` is the width handed to the line effect (0x172df0). The line grows: its fraction (+0x10) advances
  by 1 / (FPS × `Grow_Seconds`) per frame up to 1.0, so a new line takes `Grow_Seconds` to reach full length.

## GameConstants.Display_Bink_Movie_Frames

Settles `Display_Bink_Movie_Frames` (+0x11708, FoC False). **Out of scope (debug).** [H]

- Bink frame tick 0x44dea0: when set, 0x44f9e0 draws the current Bink frame number on screen in "Arial Unicode MS" 8.
  A debug overlay only.

## GameConstants.Droid_Encyclopedia_Offset

Settles `Droid_Encyclopedia_Offset` (+0x1298, two floats, FoC 24 −179). [M]

- The HUD init 0x30ae50 stores a callback (code at 0x9db00, passed by boot 0x4b250) at HUD +0x2890; the callback
  returns this pair. The encyclopedia / tooltip pop-up placement 0x308890 (callers 0x2e0c20, 0x2e2910, 0x2e3200,
  0x2f3e20) adds it to the pop-up position (anchor `Text_adv_help`, HUD_GAPS). So the pop-up sits 24 px right and
  179 px up from its anchor [M: the callback body is not a defined function in Ghidra; the two reads at 0x9db25 /
  0x9db34 are inside it].

## GameConstants.GMC_Battle_Zoom_Time family

Settles `GMC_Battle_Zoom_Time` (+0x1bc, FoC 1.0) and `GMC_Battle_Fade_Time` (+0x1c0, 0.75). [H]

- Reader: the galactic camera update 0x44a3f0, in the zoom-to-battle state (+0x70 set, galactic mode).
- Progress t = elapsed / `Zoom_Time` (clamped 0..1); the camera eases towards the battle planet with an acceleration
  curve (×1.6 up to t = 0.25, then a decelerating branch) and the zoom limits shrink with it. When elapsed +
  `Fade_Time` reaches `Zoom_Time`, the screen fade starts once (overlay 0x140a28c10, 0x44b300 with 0.75 ×
  `Fade_Time`) and is then advanced each frame (0x44bc90). Wall-clock time (timeGetTime), not sim time.

## GameConstants.GUI_Cycle_Speed family (object colour cycle)

Settles `GUI_Cycle_Speed` (+0x1208, 5), `GUI_Darken_Level` (+0x120c, 0.5), `GUI_Hilite_Level` (+0x121c, 3.0). Pairs with
T15 `GUI_Cycle_Color`. [H, M for Hilite]

- The object colour modes are set by 0x392ba0 (state block at object +0x1b0) and applied by the per-model colour
  callback 0x3a0c20.
- **Mode 4 (cycle):** 0x392ba0 copies `GUI_Cycle_Color` (FoC 1, −1, −1) into the state. Each frame the phase advances by
  dt × `GUI_Cycle_Speed`; factor = sin(phase) × `GUI_Darken_Level` + `GUI_Darken_Level` (0..1 with FoC values); the
  model colour is multiplied by 1 + cycle colour × factor per channel, so it pulses towards red. Set by the galactic
  planet selection 0x3ca480 (which overrides the colour to 6, 0, 0), the planet labels 0x3ea1b0 and
  FindWeaknessAbility 0x6fb360 (the marked target).
- **Modes 5 / 6:** 0x392ba0 stores +`GUI_Hilite_Level` (5) or −`GUI_Hilite_Level` (6). **Read, no effect:** no caller of
  0x392ba0 passes 5 or 6 (callers pass 0, 2, 3, 4, 7; the callback only re-applies a stored mode) [M].

## GameConstants.GUI_Tactical_Countdown_Timers_Screen_X family (countdown timer column)

Settles `GUI_Tactical_Countdown_Timers_Screen_X` (+0x1228, 0.005), `_Y` (+0x122c, 0.10), `_Spacing` (+0x1230, 0.05),
`GUI_Strategic_Countdown_Timers_Screen_X` (+0x1234, 0.005), `_Y` (+0x1238, 0.02), `_Spacing` (+0x123c, 0.05). [H]

- Two drawers share the column: the retreat countdown 0x3ce60 and the build countdown timers 0x3d2b0 (MCX4_T09). Both
  are called from the frame draw 0x2ee40 and from 0x3c9e0.
- X: the tactical value in tactical modes (mode virtual +0xf0), the strategic value in galactic (+0xf8); screen
  fractions. Y: 0x2ee40 / 0x3c9e0 start the column at the matching `_Y`; every timer drawn moves the next one down by
  the matching `_Spacing`.

## GameConstants.Galactic_Zoom_In_Station_Rotation

Settles `Galactic_Zoom_In_Station_Rotation` (+0x5fc, 0.04) and `Galactic_Zoom_In_Station_Offset` (+0x5f0). [H]

- Reader: the planet zoom-in starbase model (galactic HUD 0x3c9fc0 from 0x3e83a0, and 0x3e7840 from the HUD update
  0x301750). While the zoom-in view is open (HUD +0x211c = 1, camera state 3) and a station is shown, its yaw (HUD
  +0x10c8) grows by elapsed × `Rotation` (0x3c9fc0 only rebuilds the matrix with no time step).
- `Galactic_Zoom_In_Station_Offset`: **retail-unused** (no reader).

## GameConstants.Large_Coin_Stack_Size family

Settles `Large_Coin_Stack_Size` (+0x188), `Medium_Coin_Stack_Size` (+0x184), `Map_Preview_Image_Size` (+0x2a4).
**Retail-unused** [H]: no RIP reader, no GameConstants method touches them, and the offline export names them only in
the ctor / defaults.

## GameConstants.Left_Queue_Tint family

Settles `Left_Queue_Tint` (+0x13c4, 128 128 255 215) and `Right_Queue_Tint` (+0x13d4, 255 128 128 215). [H, M meaning]

- Galactic queue item 0x3e6220 (from 0x3e6550): the item's tint is `Left_Queue_Tint` when its slot index is up to 0x54
  and `Right_Queue_Tint` above (two halves of the production queue display).
- 0x31a940 (HUD queue refresh, from 0x31b590): `Right_Queue_Tint` as the button tint; its alpha drives a flicker
  (UI random: on 5 % of refreshes the alpha drops by 15..25) [M].

## GameConstants.Mouse_Over_Highlight_Scale

Settles `Mouse_Over_Highlight_Scale` (+0x11ec, 1.5). [H]

- 0x3c26c0, called from the per-model colour callback 0x3a0c20: the model's RGB is multiplied by
  1 + (scale − 1) × hover amount (object +0x44, 0..1). A hovered object is up to 1.5× brighter.

## GameConstants.Multiplayer_Losing_Team_Bonus_Credit_Percentage

Settles `Multiplayer_Losing_Team_Bonus_Credit_Percentage` (+0x5c0, 0.08). [H]

- Reader: 0x3615d0, run from the tactical mode update 0x3639d0 only in a tactical game without a galactic parent
  (0x28afb0: skirmish and multiplayer).
- Players whose faction is playable (`Is_Playable` +0x20a8) are put into teams by alliance (A, B, C). Only when there
  are **three** non-empty teams: the team credit totals (player +0x7c) are ranked; each of the two lower teams gets
  (leader total − its total) × this percentage, split between its members by their share (1 − own / team total; equal
  split when the team total is 0); players flagged +0x488 get nothing. A two-sided game never pays it. Not
  multiplayer-only, but needs three sides.

## GameConstants.Num_Structures_For_Medium_Planet_Name family

Settles `Num_Structures_For_Medium_Planet_Name` (+0x194, 3) and `Num_Structures_For_Large_Planet_Name` (+0x198, 5). [H]

- Galactic planet labels 0x3ea1b0 (from 0x3dda70 and 0x2f55a0): for a planet the label set-up accepts (0x50b840), the
  planet's count at +0x160 (its structure count) picks the label size class: below Medium → 0 (small), below Large → 1
  (medium), else 2 (large) (label virtual +0x1c0).

## GameConstants.Planet_Ability_Icon_Names family

Settles `Planet_Ability_Icon_Names` (+0x1618, list), `Planet_Ability_Text_IDs` (+0x1630, list), `Planet_Ability_RGBs`
(+0x1648, list of RGB). [H]

- Reader: 0x9db60, part of the galactic droid log dialog (0x9c9f0: `IDC_DROID_LOG_RADIO`, `IDC_DATA_CHART_RADIO`,
  `IDC_SUMMARY_SCREEN_RADIO`, `IDC_TECH_TREE_RADIO`). For i = 0 .. min(icon count, text count) − 1: icon control i gets
  the icon tinted with RGB entry i (alpha 255), `IDC_PA_TEXT%d` gets the localised text i. A legend of planet
  abilities.

## GameConstants.PlayModeSwitchMovies

Settles `PlayModeSwitchMovies` (+0x2f8, FoC Yes). [H]

- Reader: the START_MOVIE story reward 0x28bc10 (STORY_PRESENTATION_REWARDS §5.17 calls this flag "movies off"): the
  full-screen movie plays only when this flag is set, DISABLE_MOVIES is clear, and the game type is not multiplayer
  (types 1, 2, 4 skip; 0 and 3 play).

## GameConstants.Shield_Flash_Duration family

Settles `Shield_Flash_Duration` (+0x1574, 0.1) and `Shield_Flash_Scale` (+0x1568, vec3 1.0, 1.1, 1.25). [H]

- Reader: the shield hit 0x549ac0 (from Take_Damage 0x3a9e30 and LeechShields vf4 0x717d60), when the hit is absorbed
  by shields: the shield sub-mesh is shown (0x549810, type `Shield_Sub_Mesh`, T15) and the object's light effect is
  started with type 1, colour `Shield_Flash_Scale` and duration `Shield_Flash_Duration` (0x3a8bd0 / 0x3a8b70 /
  0x3a8ba0, the same object light API the ability light pulses use, T15). So the "scale" is a colour multiplier:
  a short bluish flash.

## GameConstants.ShipNameTextFiles

Settles `ShipNameTextFiles` (+0x119a8, list). [M]

- Reader: TheShipNamesManagerClass::NameListStruct 0x4e6b20: loads each listed text file as a name list (the random
  names given to units whose type asks for one). Where the names show is outside this row.

## GameConstants.SpaceReinforceFeedbackOnlyWhileDragging

Settles `SpaceReinforceFeedbackOnlyWhileDragging` (+0x11d4, FoC false). [M]

- Reader: SpaceReinforceFOWSampler 0x4d8de0 (space reinforcement placement feedback). Flag off: the feedback follows
  the HUD state 0x2f9c30 (a reinforcement placement is in progress); flag on: it follows the sampler's own drag byte
  (+0x2cc). The cell value is the reinforcement point's strength / 16.

## GameConstants.Team_Healthbar_Offset

Settles `Team_Healthbar_Offset` (+0x11860, 20). [M]

- Health bar anchor 0x2e7fd0 (from 0x31de70): for an object without a team slot (object +0x348 = −1) the bar height
  comes from the model's bounding box along the camera up axis; for a team member (squad infantry) the fixed
  `Team_Healthbar_Offset` is used instead, scaled like the bounds height. The bar is then projected to the screen.

## GameConstants.Text_Reveal_Rate

Settles `Text_Reveal_Rate` (+0x2b4, 120). [H]

- Reader: 0x31d750 (HUD text lines, per frame): a revealing line shows elapsed seconds × rate characters more each
  frame, up to its length (typewriter effect; wall clock). 0x2df0c0, 0x3c82f0 and 0x3c95d0 read +0x2b8, the next
  field, not this one.

## GameConstants.WaypointFlagModelName family

Settles `WaypointFlagModelName` (+0xd0, w_waypoint_beacon.alo), `WaypointLineLandDashLength` (+0x13e4, 5.0),
`WaypointLineLandGapLength` (+0x13e8, 5.0), `WaypointLineLandDashVelocity` (+0x13ec, 1.0). Pairs with T15
`WaypointLineTextureName`. [H]

- Waypoint draw 0x3e240 (from the frame draw 0x2ee40), shown only while waypoints are visible: the flag model is
  loaded once (HUD +0x918) and placed at each waypoint; the scroll phase (HUD +0x920) grows by `DashVelocity` per
  frame; the land line is dashed with period `DashLength` + `GapLength`.
- 0x3e9e0 builds each line (texture `WaypointLineTextureName`) and, for dashed lines, passes dash and gap length
  (0x428db0).

## GameObjectType.Beacon_Lifetime_In_Secs family

Settles `Beacon_Lifetime_In_Secs` (+0x17b8, FoC 10.0) and `Beacon_Radar_Map_Event_Name` (+0x1798,
Default_Beacon_Placement). [H]

- BeaconBehavior vf9 0x647460 / vf10 0x6474f0: the beacon expires at now + FPS × lifetime (frames).
- PlaceBeaconEvent vf4 0x436540 (the place-beacon command): for a friendly listener, plays `SFXEvent_Beacon_Placed`
  (T13) and raises the named radar map event when the name is not empty. Beacons are a team-play command; whether
  single player can issue it is not settled here [L].

## GameObjectType.Corruption_1_Failure_Bink_Hologram_Name family

Settles `Corruption_1_Failure_Bink_Hologram_Name` (+0x2578), `Corruption_2_Failure_Bink_Hologram_Name` (+0x2718),
`Corruption_3_Failure_Bink_Hologram_Name` (+0x28b8), `Corruption_1_Mission_Briefing` (+0x2598),
`Corruption_2_Mission_Briefing` (+0x2738), `Corruption_3_Mission_Briefing` (+0x28d8). [H, M briefing use]

- The three slots belong to the type's three corruption entries (`Corruption_1/2/3_Type` at +0x2464 / +0x2608 /
  +0x27a8). Getters by corruption type: 0x371370 (failure hologram name), 0x371410 (briefing), 0x3713d0 (failure
  speech, T13).
- **Failure hologram:** corruption mission end 0x28bf10 (from 0x3639d0): when the local player is the corruptor and the
  failure flag (+0x2e4) is set, the hologram plays (0x362510) with the failure speech and this Bink.
- **Briefing:** 0x52c570 (from 0x52b690, the corruption mission record set-up) copies the briefing text of the chosen
  corruption into the mission record (record +0x90 string); empty when the type has none [M where it is shown].

## GameObjectType.Encyclopedia_Weather_Name family

Settles `Encyclopedia_Weather_Name` (+0x18f0) and `Encyclopedia_Weather_Info` (+0x1910). [M]

- Reader: 0x2f0af0, called by the galactic tooltip builder 0x2e0c20: for a planet (object +0x298 type) it adds the
  weather name and the weather info text lines to the planet's encyclopedia pop-up.

## GameObjectType.End_Control_Transition_Radar_Event

Settles `End_Control_Transition_Radar_Event` (+0x1778). [H]

- Capture complete 0x500e30 (T13 `SFXEvent_Tactical_Gain_Friendly_Control`): on a capture point's owner change, for a
  local listener friendly to the old or new owner, the named radar map event is raised (0x355ca0) when the name is not
  empty (the test of +0x1788 in T13 is this string's size).

## GameObjectType.Enemy_Spawn_Text family (sandbox objectives)

Settles `Enemy_Spawn_Text` (+0x19e8, list), `Friendly_Spawn_Text` (+0x19d0, list), `Secondary_Objective` (+0x19b0). [M]

- Getters 0x371a90 (enemy) / 0x371b20 (friendly), and the field itself for `Secondary_Objective`, are read only by the
  tactical sandbox objectives builder 0x32bd10 (SET_SANDBOX_OBJECTIVES): objective texts for a spawner / target type of
  the sandbox map, by whether its owner is friendly or enemy to the local player.

## GameObjectType.GUI_Angles family

Settles `GUI_Angles` (+0x1b00), `GUI_Distance` (+0x1b10), `GUI_Velocity` (+0x1b0c), `GUI_X_Rot` (+0x1b14).
**Retail-unused** [M]: the only displacement hits outside the type ctor / copy / dtor are in CommandBarClass
0x2f9570 / 0x2f9270 with the command bar as base (its own fields), not a type. Same for T15 `GUI_Model_Name`.

## GameObjectType.Hero_Ability

Settles `Hero_Ability` (+0x1860). [M]

- 0x2ec250 (from the pop-up builder 0x2f15d0) copies the string as the hero's ability line of the unit pop-up (text ID
  localised by the pop-up).

## GameObjectType.MP_Encyclopedia_Text

Settles `MP_Encyclopedia_Text` (+0x1898, list). [H]

- Encyclopedia text builder 0x2de200 (used by the tooltips 0x2e0c20, 0x2e2910 and the pop-up 0x2f15d0): in a tactical
  game without a galactic parent (0x28afb0: **skirmish** and multiplayer), when the type's `MP_Encyclopedia_Text` list
  is not empty, its lines replace `Encyclopedia_Text` (+0x1880). Campaign battles and galactic keep
  `Encyclopedia_Text`. Visible in single-player skirmish tooltips.

## GameObjectType.Radar_Icon_Scale_Land family (radar blips)

Settles `Radar_Icon_Scale_Land` (+0x1d54), `Radar_Icon_Scale_Space` (+0x1d58), `Radar_Rotate_Icon` (+0xd6),
`Radar_Register_As_Foreground_Object` (+0xdd), `Radar_Clip_To_Visible_Region` (+0xde), `Radar_Range_Icon_Name`
(+0x1728). [H for the reads, M for the draw details]

- Readers: the radar blip builders 0x35b4d0 and 0x35c000 (both from GameModeClass vf88 0x365a80), the radar object
  registration GameModeClass vf87 0x362580, and 0x4e7e30 (radar set-up, from Load_Plots 0x28dbe0, 0x475260, 0x4753b0).
- `_Scale_Land` / `_Scale_Space`: the blip icon size multiplier in land / space mode.
- `Radar_Rotate_Icon`: the blip icon turns with the object's facing.
- `Radar_Register_As_Foreground_Object`: the object goes on the radar's foreground layer (drawn above the others).
- `Radar_Clip_To_Visible_Region`: the blip is clipped to the visible radar region.
- `Radar_Range_Icon_Name`: a range icon drawn around the blip (texture resolved by 0x3782a0; drawn when the name is not
  empty, scaled by the object's range).

## GameObjectType.Show_Hero_Head family

Settles `Show_Hero_Head` (+0x17) and `Show_Name` (+0x23). [M]

- `Show_Hero_Head`: 0x318720 (HUD panel, from 0x300c00): a selected object qualifies for the hero head (portrait)
  display when its type is a hero (+0x13) or has `Show_Hero_Head`.
- `Show_Name`: object update 0x3989a0: when the type has `Show_Name` and the object's model has a label attachment,
  the floating name label is updated (0x3a4510). (T15 `Camera_Aligned` is read in the same function.)

## GameObjectType.Tactical_Build_Start_Lower_Z

Settles `Tactical_Build_Start_Lower_Z` (+0xa04). **Retail-unused** [M]: no displacement hit with a type base outside the
ctor / copy / dtor (the only other hit is CRT code).

## Faction.Alternate_Icon_Prefix

Settles `Alternate_Icon_Prefix` (+0x358, FoC r_ / e_). [M]

- HUD update 0x30c3b0: when the local player's faction changes, its prefix is copied into the global 0x140a28450.
- Icon components (IconComponentClass vf20 0x352d80, 0x347c10, 0x7efd80) use the global when choosing their texture:
  components flagged for it load the faction-prefixed alternate texture [M which components].

## Faction.Infantry_Icon_Name family (battle pending commander icons)

Settles `Infantry_Icon_Name` (+0x238) and `Fighter_Icon_Name` (+0x2d8). [H]

- Commander icons 0x3df8b0 (`graph_left` / `graph_right` of the battle pending dialog, GALACTIC_DIALOGS §1): a side with
  no hero / commander portrait shows its faction's `Infantry_Icon_Name` in a land battle and `Fighter_Icon_Name` in a
  space battle.

## Faction.Helper_Icon_Name

Settles `Helper_Icon_Name` (+0x26b8, i_rebel_help_droid.tga / i_empire_help_droid.tga). [H]

- Droid log dialog 0x9db60 (`IDC_DROID_PICTURE_`): the local faction's helper droid picture; also 0x300a00 (from
  0x2ff5a0, the HUD advisor / droid panel) [M].

## Faction.Fleet_Icon_Name family (unused faction icon names)

Settles `Carrier_Icon_Name` (+0x278), `Corvette_Icon_Name` (+0x2b8), `Fleet_Icon_Name` (+0x1d8), `Frigate_Icon_Name`
(+0x298), `Ground_Base_Icon_Name` (+0x178), `Ground_Transport_Icon_Name` (+0x1b8), `Ship_Icon_Name` (+0x1f8),
`Squadron_Icon_Name` (+0x218), `Star_Base_Icon_Name` (+0x198), `Vehicle_Icon_Name` (+0x258), `Planet_Icon_Offset`
(+0x39c), `Planet_Icon_Scale` (+0x398), `Text_Nickname_ID` (+0x48). **Retail-unused** [M].

- No read with a faction base (loaded from player +0x68 or formed by an immediate add on a faction pointer) outside the
  faction ctor / dtor / resolver. The remaining displacement hits are other objects at the same small offsets (AI build
  tasks, targeting, task forces). The galactic fleet and planet icons come from elsewhere (GALAXY_MAP_PRESENTATION §1).

## Faction.Victory_Text family (game over screen)

Settles `Victory_Text` (+0x2e10), `Defeat_Text` (+0x2e30), `Generic_Win_Movie` (+0x2d90), `Superweapon_Win_Movie`
(+0x2d70), `Finale_Movie` (+0x2db0), `Finale_Movie_2` (+0x2dd0), `Post_Credits_Movie` (+0x2df0), `Land_Win_Image`
(+0x2e90), `Space_Win_Image` (+0x2e50), `Land_Lose_Image` (+0x2eb0), `Space_Lose_Image` (+0x2e70). [H]

- **Game over screen** 0x92bb0 (galactic end, from the main loop 0x5d990 and 0x5f6b0):
  - Movies, from the faction of the player the screen is built for (the winner [M]): `Superweapon_Win_Movie` when
    the end record's type is 3, 6 or 7 (the super-weapon victories [M]), else
    `Generic_Win_Movie`; `Finale_Movie`, `Finale_Movie_2` and `Post_Credits_Movie` are copied into the movie queue
    globals (0x140a111f0, 0x140a11210, 0x140a111d0) played after it.
  - Text, from the **local** faction: on a win `Victory_Text`, on a loss `TEXT_DEFEAT` with `Defeat_Text`.
  - Image, local faction: on a win one of `Space_Win_Image` / `Land_Win_Image`, on a loss one of `Space_Lose_Image` /
    `Land_Lose_Image`, picked 50 / 50 by the UI random (not synced).
- **Battle end dialog** 0x88fe0 (tactical end summary, from 0x87770): the local faction's land image in a land battle
  (argument 1), else the space image; win or lose image by the result [H win branch, M lose branch].

## Faction.Land_Ability_Targeting_Range_Overlay_Material_Name family (ability range blobs)

Settles `Land_Ability_Targeting_Range_Overlay_Material_Name` (+0x2d38), `Land_Ability_Targeting_Range_Overlay_Scale_Factor`
(+0x2d68), `Land_Area_Effect_Range_Overlay_Material_Name` (+0x2d00), `Land_Area_Effect_Range_Overlay_Scale_Factor`
(+0x2d30). Pairs with T15 `*_Overlay_RGBA`. [H reads, M draw]

- Local faction, land mode, while the player aims an ability. Targeting range: 0x35280 (from 0x35f00 / 0x37ae0)
  builds the ground blob with the material and the RGBA; its radius is the ability's range × `Scale_Factor` (0x35560
  and the frame draw 0x2ee40). Area effect: 0x356b0 (from 0x35f00, 0x36060, 0x37a00, 0x37ae0) with radius × the area
  scale factor (0x35c30, 0x2ee40).

## Faction.Land_Retreat_Countdown_Text_ID family

Settles `Land_Retreat_Countdown_Text_ID` (+0x21b0) and `Space_Retreat_Countdown_Text_ID` (+0x20e0). Pairs with T15
`*_Retreat_Countdown_Color_RGBA`. [H]

- Retreat countdown 0x3ce60: in space mode (mode virtual +0x30) the local faction's `Space_Retreat_Countdown_Text_ID`
  in `Space_Retreat_Countdown_Color_RGBA`; in land mode (+0x38) the land pair. The localised text (with the seconds
  left) is drawn at the countdown timer column (GC `GUI_Tactical_Countdown_Timers_Screen_*`).

## Campaign.Show_Completed_Tab

Settles `Show_Completed_Tab` (+0x379, bool). [H]

- Load_Plots 0x28dbe0 copies the campaign's flag into 0x140b305e8. Reader: the objectives dialog build 0x107280
  (GALACTIC_DIALOGS, "Built by 0x107280"): when the flag is false, `IDC_RADIO_COMPLETED` (the Completed tab) is hidden.
- FoC: single-player campaigns False, Underworld GC campaigns True.

## CommandBarComponent.Stackable family (component flag bits)

Settles `Stackable`, `Should_Ghost`, `Cross_Fade`, `Snap_Drag`, `Snap_Location`, `Ghost_Base_Only`, `Can_Drag_Stack`;
`Scale_Duration` stays open. Pairs with T15 `Swap_Texture`, `Offset_Render`, `Should_Render_At_Drag_Pos`. [H bits, M roles]

- The rows' "offsets" are not offsets: type 0x50 entries hold a setter function, and each setter sets or clears one
  bit of a flag byte of the component. Bits (byte / mask, setter): `Swap_Texture` 0x3e1 / 0x01 (0x512fd0), `Stackable`
  0x3e1 / 0x10 (0x513090), `Should_Ghost` 0x3e2 / 0x08 (0x5131e0), `Cross_Fade` 0x3e2 / 0x10 (0x513210), `Snap_Drag`
  0x3e3 / 0x01 (0x5132d0), `Snap_Location` 0x3e3 / 0x02 (0x513300), `Offset_Render` 0x3e3 / 0x04 (0x513330),
  `Ghost_Base_Only` 0x3e3 / 0x08 (0x513360), `Can_Drag_Stack` 0x3e4 / 0x02 (0x513480), `Should_Render_At_Drag_Pos`
  0x3e5 / 0x80 (0x5136c0).
- Bit readers (component methods): `Stackable` 0x34cef0 (getter) and TextButton vf13 0x34f160; `Should_Ghost` TextButton
  vf13 0x34f160 and vf17 0x352880; `Cross_Fade` IconComponent vf11 0x350360, vf20 0x352d80 and 0x351990; `Snap_Drag`
  and `Snap_Location` 0x352650 (drag placement); `Ghost_Base_Only` TextButton vf17 0x352880; `Can_Drag_Stack` 0x34b330
  (getter), TextButton vf50 0x354760 and the copy 0x348a50. (vf13 / vf11 are the draw / update of the button classes
  [M].)
- `Scale_Duration` (+0x3a8, float): **open**: its only hits are two immediates in unrelated code (0x48b8a0, 0x4d9780).

## Laser_Defense_Ability.Lightning_Effect_Name

Settles `Lightning_Effect_Name` (+0x1e8). **Retail-unused** [M]: inside LaserDefenseAbilityClass only the ctor / dtor
(0x715fa0) and the init 0x715eb0 touch +0x1e8; the zap (0x716230, T13) draws no lightning.

## Movie.Movie_Start_Frame

Settles `Movie_Start_Frame` (+0x20, int). [H]

- Bink open 0x44d920: when start frame + 1 is above 1 and below the movie's frame count, the movie jumps there
  (BinkGoto) before the first frame; the value is also stored with the open movie. FoC uses it only in the credits.

## Still open (11)

- Type `Include_In_UI_Map_Header` (+0xb2), `Name_Adjust` (+0x65c), `TSW_Post_Music_Wait_Frames` (+0x21a8): small offsets;
  every hit found is another object at the same offset. `Planet_Ability_Name` (+0x2250): one immediate in 0x51a280 not
  traced [L].
- HardPoint `Tooltip_Text` (+0x120); RadarMapSettings `Land_Is_Guide_Rectangle` (+0x130), `Space_Is_Guide_Rectangle`
  (+0xc0), `Use_Event_System` (+0x51d) (radar map object 0x140b2bcb0: methods not separated from the HUD's);
  Unit_Ability `Alternate_Name_Text` (+0x1c0), `Alternate_Description_Text` (+0x1e0) (ability button tooltip code not
  found); CommandBarComponent `Scale_Duration`.

## mcx8 t09: skirmish options and game-settings flag 0x20

Question: does the skirmish options dialog (0xea8d0) set game-settings flag 0x20 like the campaign advanced options
dialog (0xeb1c0), so `Game.InterventionsEnabled` is 0.0 in a skirmish with changed options (AI_GALACTIC_TOKENS §4)?
**Yes.** [H]

- 0xea8d0 is not a separate skirmish dialog: it is the shared **open** of the advanced options dialog, whose window
  procedure is 0xeb1c0 (init 0xeba10). Callers: 0x288c0, 0x950f0 (campaign), 0xb6410, 0xf2690 (skirmish set-up).
- In 0xeb1c0 every option change sets bit 0x20 of the dialog flags 0x140a62208 (several sites, e.g. 0xeb232, 0xeb7b8,
  0xeb8a7); `IDC_DEFAULTS_BUTTON` clears it (and reloads the defaults, 0xeda80); `IDC_BUTTON_ACCEPT` calls 0x2b6c0,
  0xf4840, 0xb9e00 and 0x95770. 0xf4840 copies the flags (and the other dialog values) to the skirmish settings copy
  0x140a622d8.
- Skirmish start 0xf2690 copies 0x140a622d8 into the game settings flags 0x140b154bc without masking. `InterventionsEnabled`
  (0x6e1fb0) reads that global, so after any changed and accepted option the skirmish runs with flag 0x20 set and the
  token is 0.0, as in the campaign. Bit 0x10 is handled separately by 0xeda80.

## Corrections to other specs

- AI_GALACTIC_TOKENS §4 (mcx8 t09): the skirmish path sets flag 0x20 too (section above).
- STORY_PRESENTATION_REWARDS §5.17: the "movies off" flag at GameConstants +0x2f8 is `PlayModeSwitchMovies`.
