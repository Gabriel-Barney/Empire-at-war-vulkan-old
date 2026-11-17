# ui4 RE questions (galactic HUD, 2026-11-17)

Roadmap group `ui4` (box UIART). Answers go into the spec named per question, with RVAs, read vs inferred kept apart, no
decompiled code. Sources: REA (`tools\tc.cmd rea call ...`) and the decompiled export `re_reference/ghidra/StarWarsG_decompiled.zip`
(`Global.c`); a question only the running game answers is marked "probe" in the answer, not guessed.

Known anchors: HUD = CommandBarClass (`this` of 0x2fd330, the galactic HUD update); component ids are the GUI component table
indexes passed to 0x347b50 (`0x3f..0x42` = b_filters0..3, `0x45` / `0x46` = b_space_tab / b_planet_tab per the strings in
0x2fd330, `0x47` = probably the tab dial); HUD +0x21c4 = filter bits; HUD +0x2150 = selected planet; HUD +0x2a34 = advanced
planet map open; planet record = 0x188 bytes at HUD +0xde0 (count +0xde8).

## Q1 (t12) Galactic build tabs -> `docs/re/GALACTIC_DIALOGS.md` §5.1
HUD +0x21ac is a byte: 0x3de450 sets it 0, 0x3de9b0 sets it 1, both play `GUI_Dial_Switch_SFX` when it changes and set an
animation / frame on component 0x47. 0x2fd330 picks "b_planet_tab" (0x46) when it is 0, "b_space_tab" (0x45) when 1.
1. Which value is the land (planet) tab and which the space tab? What calls 0x3de450 / 0x3de9b0 (button click, key, planet
   selection, the advanced map)? What is the default at campaign start and does selecting another planet reset it?
2. The galactic build-option filter (around 0x3152xx: `cVar32 == HUD+0x21ac`, the "does not have GUI_Row set" warning): which
   types go to which tab (type +0x86c, +0xf0c, behaviour 0x5f - name the XML fields), what the type kinds 5 / 6 / 7 skipped there
   are, and how GUI_Row (+0x1b40) maps a type to the pad rows (row 0 vs 1, the `/10` and `>= 10` arithmetic).
3. How many pads per tab; what happens to options past the pad count (scroll? dropped?).
4. Component 0x47: name (b_dial?) and what its animation / frame shows per tab. Are the pad backgrounds (`i_button_pad_rebel` /
   `_empire`, Blank_Texture_Name index) chosen by the tab (GALACTIC_DIALOGS §5.1 says [M])?

## Q2 (t09) FoC Base Layout planet map -> new `docs/re/GALACTIC_PLANET_MAP.md`
Opened by the organiser's `advanced_planetary` button; shell `map_shell` = `planetary_mode.alo` (bones: back_button,
ground_forces_text, drage_drop_text, unit_cap_text / _number, building_cap_text / _number, Planet_name_text, Allegiance_A,
Planetary_Income_number, day_clock, Day_Counter_text, land_fleet_00..19 (4 x 5 grid), map_window, build_pad00..79 and
unit_pad00..10 (all at the origin: placed at run time), weather, planet_bonus, pm_backdrop, b_zoom_adv_map,
radar_galactic_adv_map, c_filters0..3, c_option_g, Text_adv_help, widescreen_filler) and `map_overlay_shell` =
`planetary_mode_overlay.alo`. Enter / leave: 0x3c5fc0 / 0x3ce970 (GALACTIC_DIALOGS §4).
1. What `map_window` shows: the source of the land-map picture (the planet's land map .ted overview / radar texture? which
   map: Planet `Land_Tactical_Map`?), how it is fitted into the window, and whether the map file is loaded for it.
2. build_pad00..79 / unit_pad00..10: what each stands for (the land map's build-pad markers, `Skirmish_Build_Pad` /
   `Special_Structure_Pad`, the planet's `Special_Structures` / ground structures?), how a pad's position on the map is mapped
   into map_window, its texture / icon, and what clicking a pad does (build there? select?).
3. land_fleet_00..19 (Stackable, Drag_And_Drop, "start_land_drag"): which units fill the 20 slots (ground units on the surface,
   one per type stacked?), order, and the drag / drop rules (drop on a pad? on map_window? on the galaxy?).
4. Texts: ground_forces_text, drage_drop_text, unit_cap_text / unit_cap_number, building_cap_text / building_cap_number (which
   text ids, which numbers: planet `Max_Ground_Units` / `Max_Special_Structures` / used counts?), Planet_name_text,
   Planetary_Income_number, Day_Counter_text, Allegiance_A, Text_adv_help.
5. weather / planet_bonus: which planet fields (Encyclopedia_Weather_Icon / _Name, planet bonus / Encyclopedia_Good_Against
   texts?).
6. c_filters0..3 (do they mirror b_filters0..3 bits?), c_option_g, b_zoom_adv_map, radar_galactic_adv_map (a galactic radar
   in the map screen?), back_button (leaves the map), the galactic camera and the galaxy draw while the map is open, game
   pause while open.
7. What can the original show that only a retail capture settles (say so: a probe kit is then built for it).

## Q3 (t10) Galactic planet labels -> `docs/re/GALAXY_MAP_PRESENTATION.md` §1.2
1. Which filter bit gates the g_ground_level / g_space_level pips (§1.2 says bit 4 = b_filters2; the base-level button is
   b_filters1 = bit 2). Read the gate in the per-frame planet label code (0x3ea1b0 / 0x3ec380).
2. g_credit_bar: what level it shows (value / max on map? income steps?), and whether it needs bit 1.
3. Status-row order and show rules of g_conflict, g_build, g_smuggler, g_bounty_hunter, g_smuggled, g_corruption_icon,
   g_enemy_hero (0x3ec380 round-robin refresh): exact conditions (local player only? visibility gate?).
4. Text_Land_Fleet (organiser) and planet_bonus: what they show.

## Q4 (t11) Galactic message texts and announcer triggers -> `docs/re/GALACTIC_REVEAL_CORRUPTION.md` §4.4 / `P4_SIM_SYSTEMS.md` §2.3
1. Where the message callback 0x140b313c0 texts (slice outcomes, sabotage vigilance) appear on screen (message log? holocron
   message? screen text with which position, colour, duration?).
2. Droid Log (Holocron) battle entry: which text ids / format the original builds for a conflict entry.
3. The exact trigger of each Factions.xml announcer event: Enemy_Fleet_Approaching_Planet, Strategic_Pop_Cap_Reached,
   Max_Credits_Limit_Reached, Build_Impossible_Location_Blockaded, Land_Invasion_Commencing, GUI_Start_Campaign (who calls the
   faction-event play with that field, under what condition, any repeat limit).

## Q5 (t07) Galactic radar click and Holocron controls -> `docs/re/HUD_GAPS.md`
1. Clicking the galactic radar (`radar_galactic` / radar map component in galactic mode): does it move the galactic camera
   (RadarCameraJump) or select a planet, or nothing?
2. The Holocron summary dialog's sort buttons, IDC_BUTTON_PLANETARY and IDC_HINTS_CHECK: what each does in the original.
