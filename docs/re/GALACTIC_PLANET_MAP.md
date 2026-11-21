# Galactic planet map ("Base Layout", FoC advanced planetary mode) (StarWarsG.exe FoC x64 v1.121) — behaviour spec

Written 2026-11-17 by the RE agent for roadmap group ui4 t09 (question Q2 of `UI4_RE_QUESTIONS.md`). Clean-room: behaviour,
data, constants and order of steps only, no code. Addresses are RVAs in StarWarsG.exe. Sources: the decompile export
(`Global.c`), REA (byte search, disassembly), FoC `CommandBarComponents.xml`, `planetary_mode.alo` /
`planetary_mode_overlay.alo` (alodump), `Data\Art\Maps\*.ted` and `textures.meg` from the FoC install.

Confidence: **[H]** read in code or data, **[M]** read but the intent or a detail is inferred, **[L]** guess.

Builds on: UI.md §2 (command bar, shells, canvas), GALACTIC_DIALOGS.md §4 (organiser, `advanced_planetary`), HUD_GAPS.md §4.2
(day pips at `day_clock`), REMAINING_GAPS.md §3.2 (tactical placement on markers, marker id +0x3b0), COMMANDS.md (FleetManagement
ops, SetMarkerID), STORY_PRESENTATION_REWARDS.md §6 (advisor text box), P1_CAMERA_INPUT.md §5 (galactic wheel).

**Component ids.** The HUD names components by the enum of the table at 0x140a174e0 (0x48-byte records; HUD_GAPS.md header).
Record fields used here: +0x00 name, +0x10 drag-start handler, +0x18 left-click handler, +0x38 right-click handler [M],
+0x40 drop handler (called on the drop target with the dragged component; 0x34d940 plays `GUI_Drop_Fleet` first). The map's
components are 0x2b0..0x338 (`map_shell` 0x2b2 … `unit_pad09` 0x338). The port resolves them by **name**.

"HUD" = CommandBarClass at 0x140b27f60. "PD" = the planet data pack (planet +0xB8). "Local" = the local player.

---

## 0. Summary

- The map opens from the organiser's `advanced_planetary` button ("Base Layout") or a wheel-up while the galactic camera is in
  the planet close-up. It replaces the bottom bar and organiser with the full-screen `map_shell`; the galactic game keeps running.
- `map_window` shows a pre-rendered picture of the planet's land map: `<Land_Tactical_Map name>.dds`.
- Pads are placed over the picture at the **UI markers** stored in the `.ted` (top chunk 2), read once at boot. Base structures
  sit on In/Out-base and specific-structure markers, landed ground units on Defending Forces markers.
- The left column lists the local player's **ground units in orbit** (20 stacks). Dragging them onto the map lands them;
  dragging landed units back launches them; dragging between pads rearranges structures or units. Every move is sent as
  FleetManagement and **SetMarkerID** commands, so the layout decides where things stand in the next land battle.
- Back, zoom-out, wheel-down, selecting another planet or losing sight of a foreign planet closes it.

---

## 1. Opening and closing

### 1.1 Open (0x3c5fc0) [H]
Callers: the `advanced_planetary` click (table thunk 0x3479d0), wheel up / story reward ZOOM_IN (0x3e4660, only when the
galactic camera is in the planet close-up, camera +0x44 = 3), and the radar click re-open (0x3d1260). It does nothing unless:
1. HUD mode is galactic (+0x211c = 1) and a planet is selected (+0x2150);
2. no galactic dialog is open: corruption choice (+0x2b58), sabotage (+0x2b08), the other FoC ability dialogs (+0x2908,
   +0x28e0, +0x2930, +0x2b70) [M: which flag is which dialog], and the map is not already open (+0x2a34);
3. the planet's surface is accessible (0x375220: type `Planet_Surface_Accessible`, unless PD +0x2c8 overrides it);
4. if the planet's owner is not the local player, the local player may see its ground (0x50b970 with the local player):
   - the planet is corrupted (PD +0x2f4 ≠ −1) and the local faction has `Benefits_From_Corruption` → yes; else
   - the type has `Planet_Groundbase_Stealth` and a ground-base owner (PD +0x124 ≠ −1) not allied to the local player → no; else
   - yes when the local visibility mask of the planet has `Ground_Company_Contents` (0x100; GALACTIC_REVEAL_CORRUPTION §2.2).
   A refused open does nothing (no sound, no text). An accepted foreign open sets the **foreign-view flag** HUD +0x2ae4 = 1:
   every pad and stack is then disabled (no drag).

The organiser and its `advanced_planetary` button show only in the close-up (GALACTIC_DIALOGS §4), so in practice the map
opens from the close-up camera.

### 1.2 What opening does, in order [H]
1. +0x2a34 := 1 (map open).
2. Hide `i_main_commandbar` and `i_main_organize`; show `map_shell` and `map_overlay_shell`.
3. Clear the advisor text lines (0x2f8bf0, STORY_PRESENTATION_REWARDS §6.2).
4. Texts (§7): `back_button`, `ground_forces_text`, `drage_drop_text`, `unit_cap_text`, `building_cap_text`.
5. The `map_window` picture and scale factors (§3).
6. `weather` icon = planet type `Encyclopedia_Weather_Icon` (+0x18d0); `planet_bonus` icon = type `Planet_Ability_Icon` (+0x2230).
7. Hide `Planetary_Income_number` and `c_filters0..3`.
8. Build the pads from the markers (0x3d9ea0, §4.2), put the structures on them (0x3d7790, §4.4), put the landed units on the
   unit pads (0x3d93f0, §4.5), and move the galactic radar's planet blips onto `radar_galactic_adv_map` (0x3e1020, §8).

No pause, no camera move and no sound are part of opening [H as read].

### 1.3 Close (0x3ce970) [H]
Hide `map_shell` and `map_overlay_shell`. If the map was open in galactic mode: +0x2a34 := 0; show `i_main_commandbar` and
`i_main_organize`; clear `map_window`'s texture and release the picture; +0x2ae4 := 0; move the radar blips back to
`radar_galactic` (0x3e1020); re-run the organiser show rule (0x3e83a0). The camera is not touched.

What closes it [H]:

| trigger | path |
|---|---|
| `back_button` click | table thunk 0x3479e0 → 0x3ce970 |
| `b_zoom_adv_map` click | thunk 0x3ee490: with a planet selected, camera state 0 or 2 → zoom in (0x3e4660), else zoom out (0x3e48a0); at state 3 with the map open, zoom out **only closes the map** (the close-up stays) |
| mouse wheel down (world action 0x13) / story reward ZOOM_OUT | 0x3e48a0, as above. Wheel up at state 3 opens the map, so the wheel steps galaxy → close-up → map and back (this answers P1_CAMERA_INPUT §5's [L]) |
| left double-click on empty space (world actions 2 / 0xB) | 0x3e49f0: at state 3, zoom out to the galaxy (0x3c4080) and close the map [M: whether a double-click on the map backdrop reaches the world handler] |
| selecting another planet | 0x3ca480: closes the map, then zooms out |
| the foreign planet can no longer be seen | per-frame check in 0x3e4cb0 (same test as §1.1 item 4) |
| a click on the radar or a planet blip | 0x3d1260: camera jump, then close and re-open (§8) |
| HUD reset | 0x306290 |

Esc and right-click while open were not traced (§10).

### 1.4 While the map is open [H unless tagged]
- The galactic game keeps running: no pause call; the day pips move to the `day_clock` bone (HUD_GAPS §4.2).
- Hidden or held: the hero roster (0x311380 hides it), the command-bar Bink movie, the advisor text box (0x2e4260 does not show
  queued messages; they wait until the map closes).
- The encyclopedia / tooltip pop-up anchors at `Text_adv_help` instead of `text_galactic_help` (0x308890).
- `pm_backdrop` is an opaque 1024 × 768 quad (`build_backdrop.dds`, MeshGloss) at the shell centre and `widescreen_filler` a blank
  quad covering x −912..912 for wide screens, so the galaxy is covered [H data, M visual].
- The overlay shell (`planetary_mode_overlay.alo`, `Dialog_Scene`) adds an edge fade (`i_fade_edge.tga`) around the map window and
  additive scan lines over the radar (`radar_galactic_two`) and the whole screen (`radar_galactic04`) [H data].
- Story reward "flash command bar component" (0x2f5000) flashes the build pad holding a structure of the given type.

---

## 2. Layout [H data]

`map_shell` = `planetary_mode.alo`, `Model_Offset_X/Y False`, `Manual_Offset True`, `Hidden True`: centred on the 1024 × 768
canvas (UI.md §2.4). Bone positions in canvas units from the centre (x right, y up):

| component (type) | bone | shows / does |
|---|---|---|
| `ground_forces_text` (TextButton, Arial Bold 7, white, emboss) | (−394, 300) | header over the orbit stacks |
| `land_fleet_00..19` (TextButton, Stackable, Drag_And_Drop, count text offset 17,−17) | x −459/−415/−371/−327, y 255..79 step 44 (00 top-left, row-major) | ground units in orbit (§5) |
| `drage_drop_text` (Arial Bold 7, grey) | (−394, −12) | hint under the stacks |
| `unit_cap_text` / `unit_cap_number` (green 64,255,64) | (−481, −71) / (−295, −71) | §7 |
| `building_cap_text` / `building_cap_number` | (−481, −94.5) / (−299, −94) | §7 |
| `Allegiance_A` / `Planet_name_text` (red 255,64,64 by XML, recoloured) / `Planetary_Income_number` | (−481, −140) / (−463, −140) / (−295, −140) | §7 |
| `Day_Counter_text` / `day_clock` | (−485, −163) / (−395, −163) | §7, day pips |
| `radar_galactic_adv_map` (Button 175 × 175) | (−409, −285) | galactic radar (§8) |
| `c_filters0..3` | x −294.7, y −217/−253/−289/−325 | hidden (§8) |
| `c_option_g` | (−295, −361) | options menu (§8) |
| `b_zoom_adv_map` (`i_icon_zoom_out.tga`, `TEXT_TOOLTIP_ZOOM_BUTTON`) | (−485, −359) | §1.3 |
| `Text_adv_help` | (−504, −344) | pop-up anchor (§1.4) |
| `map_window` (Icon, Size 512 × 512, Scale 1.25, tint 200,200,200,255, layer 2; mesh 640 × 640) | (92, 7) | the land-map picture (§3) |
| `build_pad00..79`, `unit_pad00..09` | all at the origin | placed at run time (§4); the .alo also has a `unit_pad10` bone with no component |
| `weather` / `planet_bonus` | (471, 80) / (471, 27) | planet icons (§1.2 step 6) |
| `back_button` (`back_button.tga`, text white 7) | (164, −360) | closes (§1.3) |
| `pm_backdrop`, `widescreen_filler` | (0, 0), (−712, 0) | §1.4 |

---

## 3. The land-map picture (`map_window`)

### 3.1 Picture source [H code, data]
- Name = the planet type's `Land_Tactical_Map` (+0x690) with its **last four characters replaced by `.dds`**:
  `_Land_Planet_Tatooine_02.ted` → `_Land_Planet_Tatooine_02.dds`, loaded as a texture (0x25e990) and set as `map_window`'s
  texture. FoC `textures.meg` holds one such picture per land map (`DATA\ART\TEXTURES\_LAND_PLANET_*.DDS`, 512 × 512 DXT5 with
  mips; EaW's are 256 × 256).
- A planet type without `Land_Tactical_Map` gets `maptemp.tga` (`MAPTEMP.DDS`, 512 × 512, is in `textures.meg`) [M: the `.tga`
  request resolves to the `.dds`].
- The picture fills the window: the component keeps its own size (512 × 1.25 = 640 canvas units square) [M].

### 3.2 Map data read at boot, not at open (0x3d4490, called from the game init 0x4b250) [H]
For every planet type (list 0x407f60, kind 3) with a `Land_Tactical_Map`, path `Data\Art\Maps\<name>` (MEG-aware, UI.md §9.2):
1. **UI markers** (0x47c670, log "Parsing found map for UI objects"): top chunk 2 of the `.ted`; each child chunk 0x0B is one
   marker with minis 0x0C = marker object id (u32), 0x0D = CRC32 of the marker type name in upper case (u32), 0x0E = x and
   0x0F = y as floats in 0..1 across the map. Kept as 16-byte records in file order. Example: `_LAND_PLANET_TATOOINE_02.TED`
   chunk 2 holds 42 markers; CRC 1269546234 = `DEFENDING FORCES POSITION`, 160844762 = `SKIRMISH_BUILD_PAD`.
2. **World size** (0x47b030, UI.md §9.3): header minis 0x10 / 0x11, width and height.
Both go into a map keyed by planet type (HUD +0x2a40). The `.ted` is not opened again when the map screen opens.

### 3.3 Scale factors [H]
On open: HUD +0x2ac8/+0x2acc = world width / height, +0x2ad0/+0x2ad4 = `map_window` size (vf +0x2b0) / world width / height
(left 0 when the planet has no entry or the sizes are not positive). They are used only for the shield-range circle (§4.4).

---

## 4. Pads

### 4.1 Marker classes (0x3d9ea0) [H]
Each marker's type is found by its CRC (0x407db0). Tests in this order, the first that matches wins:

| marker type | class |
|---|---|
| `OUT BASE SPECIAL STRUCTURE POSITION` | Out |
| `IN BASE SPECIAL STRUCTURE POSITION` | In |
| `DEFENDING FORCES POSITION` | Defender |
| any type with `Marker_For_Specific_Object_Type` entries (+0x20f8; shield, power generator, turbolaser, pirate command centre…) | Specific |
| a name starting with `REINFORCEMENT_POINT` (19-character case-insensitive prefix) | Reinforcement |
| `GARRISON_BUNKER_CAPTURABLE` | Bunker |
| `SKIRMISH_BUILD_PAD` | Build pad |

Other markers (attacker entry, story zones…) get no pad.

### 4.2 Pad groups [H]

| components | XML group and look | filled from (marker order within a class) | max | tag (pad data slot 3) |
|---|---|---|---|---|
| `build_pad00..32` | `build_pad`: blank `i_button_units.tga`, pulse frames `i_button_units00..04` (10 fps, loop, animate back), Drag_And_Drop, Ghost_Base_Only | In, then Specific, then Out | 33 | In / Out = 1, Specific = 3 |
| `build_pad33..39` | `reinforce_pad`: icon `i_icon_reinforce.tga` | Reinforcement | 7 | — |
| `build_pad40..79` | `bunker_build_pad`: icon = alternate 0 `i_icon_bunker.tga` for a bunker, alternate 1 `i_icon_build_pad.tga` for a build pad | Bunker, then Build pad | 40 | — |
| `unit_pad00..09` | `unit_pad`: blank `i_button_buildings.tga`, pulse frames `i_button_buildings00..04`, Drag_And_Drop | Defender | 10 | — |

(The XML group names are swapped against their art: build pads use the "units" art and unit pads the "buildings" art.)
Markers beyond a group's size are dropped. Each placed pad: data slot 0 = the object on it (none yet), slot 1 = its marker
record, disabled, no icon (or the fixed icon above), size scale 1.0. Unused pads are moved off screen (y + 2000, z −1),
cleared and hidden. Free-pad lists kept for the next steps: In +0x2a50, Out +0x2a68, Specific +0x2a80, unit pads +0x2a98.

### 4.3 Where a pad sits (0x3dc870) [H formula, M axis]
u = 2·(x − 0.5), v = 2·(y − 0.5) from the marker; position = `map_window` bone + (picW / 2 · u · s, picH / 2 · v · s), z 7,
where picW / picH = the picture's pixel size (512) and s = `map_window`'s Scale (1.25). So the marker square 0..1 maps onto
the 640-unit window. Canvas y is up; whether `.ted` y = 0 is the bottom or the top of the picture needs a capture (§10).

### 4.4 Structures on the pads (0x3d7790) [H unless tagged]
Input: the planet's ground special structures (PD +0x150 list, count also kept in HUD +0x2adc). The rules mirror the tactical
placement (REMAINING_GAPS §3.2 steps 2–4):
1. Types with `Place_Other_Type_At_Every_Specific_Marker_Position` (+0x22f0): every Specific pad whose marker lists the other
   type gets this structure type's `Icon_Name`; the pad holds the **type**, stays disabled, and leaves the free list.
2. Other structures, and the `Tactical_Additional_Structure_Type` (+0x8d0) types of the structures (each once): a Specific pad
   whose marker lists the type gets its `Icon_Name`, tint (255, 255, 255, 192), disabled, holds the type. If the type has
   `Base_Shield_Radius` (+0x21f4) > 0 the pad also gets the overlay `shield_range.tga`, scaled so its diameter is
   2 × radius × (HUD +0x2ad0) canvas units: the shield's reach drawn on the map.
3. The rest by base position: first those with a remembered marker (object +0x3b0 ≠ −1) go to the pad with that marker id in
   their `Base_Position` group (`IN_BASE` → In pads, `OUT_BASE` → Out pads); then the others take the first free pad of their
   group, falling over to the other group when it is full [M: exact pass order]. The pad holds the structure object, shows its
   `Icon_Name`, and is enabled unless foreign view. A **SetMarkerID** command (COMMANDS kind 53) is sent for each structure
   placed this way (marker = the pad's marker id) [H for the command, M for its argument].
Structures with no free pad are not shown.

### 4.5 Landed units on the unit pads (0x3d93f0) [H]
Input: the planet's surface unit list (PD +0xd8; count kept in HUD +0x2ad8), only units owned by the planet's owner.
1. A unit with a remembered marker (+0x3b0 ≠ −1) takes the free unit pad with that marker id. If none matches, a
   SetMarkerID(unit, −1) is sent and the unit waits.
2. Units with no marker, then the waiting ones, take the free unit pads in pad order; SetMarkerID(unit, pad's marker id) is
   sent for each.
3. A filled pad shows the unit's display type (0x3973b0) `Icon_Name` and is enabled unless foreign view. Units beyond the free
   pads are not shown.

### 4.6 Per-frame update while open (0x3e4cb0, from the galactic HUD frame 0x30c3b0) [H]
- Re-checks §1.1 item 4 for a foreign planet (closes on failure).
- Rebuilds the orbit stacks (§5) and the two cap numbers (§7).
- When the landed-unit count ≠ HUD +0x2ad8 or the structure count ≠ +0x2adc, re-runs §4.2, §4.4 and §4.5.
- With no drag in progress: unit pads stop pulsing, show their blank; hidden when filter bit 1 is clear (§8) or the pad is
  unused; disabled when empty or foreign view. Build pads 00..32: blank on tag 1 / 2, none on others; hidden when filter bit 2
  is clear or tag 0; disabled when empty or foreign view.
- During a drag of an orbit stack or a unit pad: every other unit pad pulses (its icon frames), enabled and shown, and the build
  pads 00..32 are disabled. During a drag of a build pad 00..32: the other tag-1/2 build pads pulse, enabled and shown, and the
  unit pads are disabled. The pulse marks the valid drop targets.

---

## 5. Ground forces in orbit: `land_fleet_00..19` (0x3e4cb0) [H unless tagged]
- Source: for fleet slot k = 0, 1, 2 (fleet data +0x48; the organiser's three panes), the fleets in the planet's orbit list
  (PD +0xc8) with fleet behaviour, **owned by the local player**; their contents (0x3965f0) whose direct parent is the fleet,
  not being removed (+0x3a0 & 0x2) and not flagged at +0x1b0 → +0x31 [M: excludes units carried as part of another object].
- Stacked by display type (0x3973b0): one stack per type with its count and a representative unit. Only units with the
  land behaviour (+0x337 ≠ −1) get a slot. Stacks fill `land_fleet_00` up to the number of slots (20) in the order of the
  stack map, which is keyed by the type object's address [H; the port picks a stable order, §9].
- Each slot: icon and count through the organiser slot fill 0x309a80 (UI.md §7). Disabled when the planet's surface is
  full (surface count 0x3f9530 ≥ `Max_Ground_Forces_On_Planet`, GameConstants +0x10c8, 10) or foreign view. Unused slots
  hidden and cleared.
- [M] A hero-team special case swaps the representative and sends a command (0x5b1170) when the kept unit is a team member
  (`DAT_140b278b0`); not needed for the layout.
- Note: the "start_land_drag" event in 0x2f6c30 is the drag of the galactic planet label's ground-forces icon
  (`g_planet_land_forces`, 0x202) to another planet, not a drag inside this map.

---

## 6. Drag and drop [H]

### 6.1 Handlers (component table)
| component | drag start (+0x10) | left click (+0x18) | drop handler (+0x40), target = this component |
|---|---|---|---|
| `land_fleet_00..19` | 0x346570 | — | 0x347a80: dragged is a unit pad → **launch** (0x3cf500) |
| `map_window` | — | — | 0x347a20: dragged is an orbit stack → **land** (0x3cf660) |
| `unit_pad00..09` | 0x346570 | — | 0x347a40: dragged is a unit pad or an orbit stack → 0x3dca30 |
| `build_pad00..32` | 0x346570 | — | 0x3479f0: dragged is a build pad 00..32 → 0x3d9c10 |
| `build_pad33..39` | — | — | 0x3479f0 (never matches: these pads have no tag) |
| `build_pad40..79` | — | — | — |

No pad has a left-click handler: clicking a pad does nothing (no build, no select). Drag start (0x346570) needs the component
enabled and holding an object (empty or disabled pads cannot be dragged); it logs "Initiating drag of %s". A drop onto a
component with a drop handler plays `GUI_Drop_Fleet`; a drop anywhere else does nothing.

### 6.2 Rules
Commands go through the outgoing queue (0x3b01f0). FleetManagement ops (COMMANDS kind 8): 0 form a new fleet, 2 detach from
its fleet, 4 launch, 5 land.
1. **Orbit stack → `map_window`** (land, 0x3cf660): when the surface count < `Max_Ground_Forces_On_Planet`, the stack's
   representative unit has the land behaviour and its fleet exists: op 2 (detach), then op 5 (land on this planet). One unit
   per drop. It gets a unit pad at the next rebuild (§4.5).
2. **Orbit stack → unit pad** (0x3dca30): SetMarkerID(unit, the pad's marker id); then land it as in rule 1 (same room test);
   if the pad already held a landed unit, that unit is launched: op 4 (launch), op 0 (new fleet), SetMarkerID(unit, −1). The
   launch does not depend on the landing succeeding. HUD +0x2ad8 := 0 forces a rebuild.
3. **Unit pad → unit pad** (0x3dca30): the two pads swap their units (moving to an empty pad empties and disables the
   source); SetMarkerID is sent for each moved unit with its new pad's marker id; icons swap.
4. **Unit pad → orbit stack** (0x3cf500, any slot): launch the unit: op 4, op 0 (new fleet), SetMarkerID(unit, −1).
5. **Build pad → build pad** (0x3d9c10, pads 00..32): only when both pads have the same tag and it is 1 or 2, so In-base and
   Out-base structures can be swapped or moved to a free In/Out pad, but Specific-structure pads (tag 3) never take part.
   Each moved structure gets SetMarkerID with its new pad's marker id; the target pad's enabled state follows the foreign flag.
   No code found sets tag 2 [M].

---

## 7. Texts and numbers [H]

| component | content |
|---|---|
| `back_button` | `TEXT_BACK_BUTTON` ("Back") |
| `ground_forces_text` | `TEXT_GROUND_FORCES` ("Ground forces in orbit") |
| `drage_drop_text` | `TEXT_DRAG_AND_DROP` ("Drag and drop onto map") |
| `unit_cap_text` / `unit_cap_number` | `TEXT_UNIT_CAP` / `"%d/%d"`: landed units on the planet (PD +0xd8 count, any owner) / the number of organiser garrison pads (group of `Pad_00`, 10) [M: group size] |
| `building_cap_text` / `building_cap_number` | `TEXT_BUILDING_CAP` / `"%d/%d"`: ground special structures (PD +0x160, 0 while the ground-base level PD +0x130 < 1) / the planet's land structure cap (PD +0x230, or type `Special_Structures_Land` +0x92c without PD) |
| `Planet_name_text` | the planet type's display name, upper-cased (as `Text_Planet`; set on planet selection 0x3d6730 / 0x3de570); colour = the owner faction's colour when ownership is visible (0x50bf10), else the `Neutral` faction's (0x3e8a80) |
| `Allegiance_A` | the same icon as `Text_Allegiance_A`: its alternates rebel / empire / underworld / pirate / jabba by owner faction (the three playable factions by HUD +0x2708/+0x2728/+0x2748, then `Pirates`, else index 4); empty when ownership is not visible |
| `Planetary_Income_number` | hidden on open; only its colour is updated (0x3e8a80). Never visible [M: no other show found] |
| `Day_Counter_text` | the same text as `Text_Day_Counter`: `"%s: %d"` with `TEXT_CYCLES` and the galactic cycle count (mode +0x370) |
| `day_clock` | anchor of the ten day pips (HUD_GAPS §4.2) |
| `Text_adv_help` | no text of its own: anchor of the encyclopedia / tooltip pop-up (0x308890) |

---

## 8. Filters, options, zoom, radar [H]
- **`c_filters0..3`**: toggles of HUD +0x2ae0 bits 1 / 2 / 4 / 8 (0x3ccf30; set when the button's toggle state is 3). Bit 1
  shows the unit pads, bit 2 the build pads 00..32; bits 4 and 8 are not read. Separate from the galactic `b_filters` (+0x21c4).
  Galactic start (0x3d3200) sets +0x2ae0 = 0xF and the four buttons on, and the map hides them on every open, so in retail all
  pads always show.
- **`c_option_g`**: same handler as `b_option_g` (0x346840 → 0x30ac30: the options menu callback HUD +0x27f0).
- **`b_zoom_adv_map`**: §1.3.
- **`radar_galactic_adv_map`**: while the map is open the galactic radar's planet blips are laid out on it instead of
  `radar_galactic` (0x3e1020: blip = radar centre + (x / 380 · w / 2, y / 460 · h / 2)). A click on the radar (0x346d10 →
  0x3d1260) jumps the galactic camera to the clicked galaxy point; a click on a planet blip (`g_radar_blip`, 0x346a10) first
  selects that planet if it is revealed (0x3de970), then does the same. In both cases, with the map open, the map is closed and
  re-opened, so it shows the newly selected planet (subject to §1.1).

---

## 9. Port mapping
- **engine/ui** (galactic HUD): the `map_shell` screen per §2 and §7; open / close rules §1; the per-frame refresh §4.6 and §5;
  filters fixed on; drop and drag-start handlers §6; pulse on valid targets; foreign-view disabling.
- **engine/assets / terrain**: read top chunk 2 (UI markers) and the header world size of every planet's land `.ted` once at
  campaign load (§3.2); load `<map>.dds` from `Data\Art\Textures` for the picture, `maptemp` as fallback.
- **engine/galactic** (sim, through commands only): FleetManagement ops 2 / 5 / 4 / 0 and SetMarkerID (kind 53, object
  +0x3b0) exactly as §6.2 and §4.4–4.5. The UI must send the same SetMarkerID commands on open as retail (§4.4 step 3, §4.5),
  because the tactical placement reads +0x3b0 (REMAINING_GAPS §3.2).
- **SetMarkerID execute (0x5b0660, ui4 t13)** [H]: in galactic mode only, find the object by id and set +0x3b0 to the marker; no
  owner, type or range check, nothing else written. A scan of the decompile for +0x3b0 reads finds only the battle structure
  placement (0x2a1eb0, REMAINING_GAPS §3.2 step 4) and this map (0x3d7790, 0x3d93f0): a landed unit's marker is the map's pad
  memory and does not steer the land battle's defender placement (0x2a00b0 does not read it) [M: an indirect read through a
  helper is not excluded].
- **[port] choices to record**: the orbit-stack order (retail sorts by type address; use a stable type order, for example type
  index), and the marker y direction until §10 item 1 is captured.

---

## 10. Open: needs a retail capture
1. One screenshot of the map for a planet with a known `.ted` (for example Tatooine, `_LAND_PLANET_TATOOINE_02`): confirms the
   marker y direction, pad sizes and icons, the shield circle, which texts show, `Allegiance_A`, `weather` / `planet_bonus`.
2. Hover over a pad, an orbit stack, `weather` and `planet_bonus`: the pop-up's content and position (`Text_adv_help`).
3. Esc, right-click and a double-click on the backdrop while the map is open: close, options menu or nothing.
4. Drag an orbit stack onto an occupied unit pad while the surface is full: is the occupant launched and nothing landed (§6.2
   rule 2 as read)?
5. Day pips advancing while open (no pause, as read) and the radar-blip click switching planets.

---

## 11. Named entry points (this pass)
| RVA | role |
|---|---|
| 0x3c5fc0 / 0x3ce970 | open / close the planet map |
| 0x3479d0 / 0x3479e0 | `advanced_planetary` / `back_button` click thunks |
| 0x3ee490 | `b_zoom_adv_map` click: zoom in or out by camera state |
| 0x3e4660 / 0x3e48a0 / 0x3e49f0 | galactic zoom in / zoom out / double-click toggle |
| 0x3d4490 | boot: read UI markers and world size of every planet's land map |
| 0x47c670 | read `.ted` top chunk 2 (UI markers) |
| 0x3d9ea0 | classify markers, place pads |
| 0x3dc870 | place one pad over the picture |
| 0x3d7790 | structures onto pads (+ shield circle, SetMarkerID) |
| 0x3d93f0 | landed units onto unit pads (+ SetMarkerID) |
| 0x3e4cb0 | per-frame refresh: orbit stacks, caps, pad states |
| 0x3cf660 / 0x3cf500 / 0x3dca30 / 0x3d9c10 | drop: land / launch / unit-pad drop / build-pad swap |
| 0x346570 | drag start of a stack or pad |
| 0x3df840 | send SetMarkerID(object, marker) |
| 0x3ccf30 | `c_filters` toggle → HUD +0x2ae0 |
| 0x3e1020 | radar blips onto `radar_galactic` / `radar_galactic_adv_map` |
| 0x3d1260 | radar click: camera jump, re-open the map |
| 0x50b970 | can the local player see a planet's ground (foreign open test) |
