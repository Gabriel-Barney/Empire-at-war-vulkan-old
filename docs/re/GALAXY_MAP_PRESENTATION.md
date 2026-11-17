# Galaxy map presentation, main menu dressing and attract demo: behaviour spec (StarWarsG.exe FoC x64)

Scope: what the original draws on the galactic map around planets and fleets, the selected-planet ellipse, the
hero bar, the main-menu logos and version text, the attract-demo battle, and where story unit rewards appear.
Written from the binary, the shipped data (FoC + EaW MEGs) and base screenshots 20–28 compared with port
screenshots 29–35. Confidence: [H] read in code or data, [M] inferred from code with a gap, [L] guess or
screenshot-only. Addresses are RVAs from 0x140000000. Related: UI.md §1.6 / §3, HUD_GAPS.md §3,
MAIN_MENU_BACKGROUND.md, STORY.md §5, STRATEGIC_GAPS.md 6.1.

Implementing modules: **ui** = HUD components / dialogs, **render** = 3D models / quads,
**apps/game** = the host that feeds them and sets up sessions.

---

## 1. Unit icons around a planet (fleet and ground "stacks")

### 1.1 Components (CommandBarComponents.xml, FoC) [H]
| component | what | notes |
|---|---|---|
| `g_planet_fleet` | orbit-fleet button | Blank textures `fleet_empty`, `fleet_stacks_01..05` (36×45 "pancake" stacks); Icon_Alternate list `i_encyclopedia_rebel/empire/pirates/underworld/hutt_symbol`, `i_button_command_bar_rebel_symbol_raid`, `i_button_command_bar_death_star_symbol`; Offset `3 47`; Default_Offset `36 42`; Size 0.75; Drag_And_Drop; group "Fleet Icon" |
| `g_planet_land_forces` | ground-forces button | same stacks; Icon `i_button_unknown`; Offset `3 5`; mouse-over `i_icon_ground_units_select` |
| `g_hero` | hero medal | alternates `i_icon_hero_you`, `i_icon_hero_enemy`, `i_icon_rebel_hero`, `i_icon_empire_hero`, `i_icon_underworld_hero` |
| `g_conflict`, `g_build`, `g_smuggler` (`i_icon_smuggler`), `g_bounty_hunter` (`i_icon_hunter`), `g_smuggled`, `g_corruption_icon` | status icons | Default_Offset `0 13`; `g_conflict` Offset `30 −16` shifts the status row |
| `g_planet_name` / `g_planet_value` | text | Offset `30 0` (7 pt) / `30 −11` |
| `g_weather` / `g_planet_ability` / `g_corruption_text` | | `−20 −31` / `0 −34` / `30 15` |
| `b_filters0..3` | galactic filter buttons | credit, base level, ability/weather, hero → HUD +0x21c4 bits 1/2/4/8 (0x3cd120); all off at start |

### 1.2 Per-planet record and placement [H unless tagged]
- Build (0x3d3790): each planet record (0x188 B, UI.md §1.6) gets **three clones of `g_planet_fleet`**
  (slots 0..2), then name (upper-cased), value, the status icons, five hero medals, land forces,
  corruption text, weather/ability.
- Fleet buttons are queued only once the planet is revealed (planet flag +0x2c9; 0x3dda70). Alpha follows
  the zoom rule of UI.md §1.6.
- Per frame (0x3ea1b0), with (px, py) = planet position projected to the HUD canvas:
  slot 0 = (px − 36, py + 42), slot 1 = (px, py + 47), slot 2 = (px + 36, py + 42); land forces
  (px, py + 5). +y is up [M on the sign; base screenshots put fleets above the planet].
- **Checked on base 24 / 25 / 40.jpg, C2 and C3 captures (pt4qa, 2026-10-30) [H screenshot]:** the stack art is drawn at `Scale` 1.0 (36×45 quad, oval 34 units wide,
  its centre 11.5 below the slot position); `Size` x 0.75 is the unit icon's scale only (the XML's own note). `fleet_empty` is an opaque dark disc, not an outline: alpha 255 in the
  atlas, and the same fill over sky, dust and planet in 40.jpg, tinted Empire blue on every planet including unowned ones (fill ≈ RGB 4,16,30; the art x `<Color>` gives 11,27,48, so
  about 0.6 of it: source of the extra darkening not traced). `g_planet_name` starts about 31 units right of the planet centre, overlapping the disc's edge as in the port.
- Value text needs filter bit 1, the ground/space level pips need bit **2** (b_filters1, base level); bit 4 (b_filters2)
  gates the weather / ability icons (corrected ui4 t10, 2026-11-17; earlier text said bit 4 for the pips). Status
  details refresh round-robin (0x3ec380), a few planets per frame. Full gate list and the status row: §1.7.

### 1.3 Which fleet goes in which slot (0x3eb680) [H]
- Orbit list 0x395900. A fleet's own slot = fleet +0xc0 → +0x48 (0x4327b0).
- Shown if owned by the local player, or the planet is visible (0x50b5a0) and the fleet is not stealthed
  (0x3a9760).
- If the orbit owner (0x3f9270) is the local player, fleets keep their slots (slot-0 fleets merge into
  slot 0).
- Otherwise **enemy fleets go to slot 0 (left)**; the local player's fleets keep slots 1/2 and a slot-0
  fleet moves to 2 (right). Base 28.jpg: pirate fleet upper left, Tarkin upper right.
- **Port model (playtest 3 t07, 2026-10-29; engine/galactic `FleetState::slot`, src/fleet_slots.cpp) [M]:** the slot lives in
  the sim, not the HUD. The binary gives the field and the display rule above, not the assignment rules, so these are guesses:
  every orbiting fleet has a slot 0..2 unique among *its owner's* fleets at that planet (owners have separate slot spaces; the
  display rule above resolves collisions between owners); a fleet entering an orbit takes the owner's lowest free slot, leaving
  frees it; a fourth fleet of one owner stays unslotted (-1) until a slot frees. FleetManagement op 8 {fleet, slot} moves a fleet
  to a free slot or merges it into the fleet in the slot (the HUD drop on an occupied slot); op 8 with `a` = another planet
  records the slot to take on arrival there; ops 0 / 2 / 4 / 6 / 7 take the slot of the new fleet in `b`. Arrival merging (STRATEGIC /
  GALACTIC_ECONOMY: a fleet that stops merges into a friendly fleet already in orbit [H]) is unchanged without a slot request
  (first friendly fleet in orbit order); with a request it merges into the fleet in that slot, or takes the slot when it is free.
  Slots are hashed in an "FSL1" CRC tail only once a command chose one. Not recovered: whether the original merges
  slot-0 fleets only at display time (the 1.3 text) or in the sim.

### 1.4 What the button shows (0x3df060) [H unless tagged]
- **Icon = the representative unit's `Icon_Name`** (type +0x1820), chosen by 0x3d15b0:
  - removed units (+0x3a0 bit 1) skipped; transports (+0x337 ≠ −1) contribute their cargo;
  - heroes (Is_Named_Hero / Is_Generic_Hero, or a unit with an attached hero, 0x38fa20) compete on
    lowest `Ranking_In_Category` (+0x1660);
  - every unit competes on max tactical health (0x3727a0); a hero needs ≥, a non-hero needs >;
  - hero filter (bit 8) on → the hero wins; off → the biggest unit wins.
- **Stack texture** = `fleet_stacks_0n` with n = min(ceil(units / Ships_Per_Stack), 5)
  (GameConstants, 5 in FoC [M on the field]). Icon y = −h/2 + layers × 3 (Offset x = 3 px per layer).
- **Badge** (lower-effect texture): `i_icon_stealth.tga` when every unit is stealthed, else
  `i_icon_raid.tga` for a raid fleet (0x432e20), else none. Base 24/26.jpg: the small yellow running
  figure by Boba Fett is the stealth badge.
- Colour white; secondary colour = owner faction colour.
  **Port (R2 t01, 2026-10-29) [M]:** the secondary colour tints the pancake stack texture (grey / white art: `fleet_stacks_0n` in
  MT_commandbar are 36×45 grey rims) with the owner's Factions.xml `<Color>`; the unit icon and the badges stay white. Which faction
  field the original reads (`<Color>` +0xa8 vs `<Big_Fleet_Color>` +0xc8, whose alpha 150 and name suggest a fleet-stack use) is not
  traced: **needs RE** (read of faction +0xc8 in the 0x3df060 draw path). Planet-name colour on the map: the port uses
  `<Display_Font_Color>` (+0xb8; the Planet Info dialog's name colour is that field [H], GALACTIC_DIALOGS §9.2), Neutral faction's
  for unowned planets [M for the map labels]; earlier hard-coded values read off a screenshot are gone.
- The faction-emblem list (Icon_Alternate, index from 0x3d18f0: Rebel 0, Empire 1, Pirate 2,
  Underworld 3, else 4, raid 5, `Galactic_Fleet_Override_Icon_Index` wins) is used **only by the fleet
  tooltip** (`fleet_tooltip`, 0x2e2910), never on the map.
- Land forces (+0x26): shown when the surface is accessible (0x375220) and the local player owns the
  ground or sees it (0x50ba40); built the same way from the surface list (0x397120) with ground = 1.

### 1.5 Hero medals (0x3ec380) [H]
With global 0x140a7c475 set (multiplayer-style "you vs enemy" [M]): `i_icon_hero_you` / `_enemy`;
otherwise by hero faction: rebel / empire / underworld. Base 24.jpg: the blue medal under Corellia is
`i_icon_empire_hero`.

### 1.6 Port fix — **ui** (components), **apps/game** (feed)
Replace the faction-emblem discs with: stack texture by unit count, the representative unit's
`Icon_Name` on top, stealth/raid badge, three slots placed per §1.2/§1.3, land-forces button below.
Keep the emblem only in the fleet tooltip.

### 1.7 Label gates and status row (ui4 t10, 2026-11-17) [H unless tagged]
Read through REA and the decompiled export: 0x3d3790 (per-planet record build), 0x3ea1b0 (per-frame placement),
0x3db950 (pip rows), 0x3dbc00 (status row), 0x3ec380 (round-robin refresh). Filter bits = HUD +0x21c4 (§1.1:
1 credit, 2 base level, 4 ability/weather, 8 hero).

**Record.** Per planet the build clones, in this order: three `g_planet_fleet`, `g_planet_name` (the wide name text at
type +0x140, upper-cased), `g_planet_value`, `g_space_level`, 5 × `g_space_level_pips`, `g_space_icon`, `g_ground_level`,
9 × `g_ground_level_pips`, `g_ground_icon`, `g_conflict`, `g_hero`, `g_enemy_hero`, 3 more `g_hero`, `g_build`,
`g_smuggler`, `g_bounty_hunter`, `g_planet_land_forces`, `g_radar_blip`, `g_smuggled`, 2 × `g_special_ability`,
`g_planet_ring`, `g_weather` (icon = type `Encyclopedia_Weather_Icon` +0x18d0), `g_planet_ability` (icon = type
`Planet_Ability_Icon` +0x2230), `g_corruption_text`, `g_corruption_icon`. Not cloned, so never drawn on the map:
`g_political_control` (no code reference) and `g_credit_bar` (its name is not even in the executable).

**Per frame (0x3ea1b0).** Owner colour = the owner faction's colour, or the `Neutral` faction's while the planet is
unrevealed (+0x2c9) or its owner not visible (0x50bf10).
- `g_planet_value` (bit 1): text "+N" (literal plus), N = round-half-away of the planet's income (Planet_Income with
  gross = 0, 0x3f68b0, GALACTIC_ECONOMY §1.4) when the local player may see the credit income (0x50b2a0, REMAINING_GAPS
  §6.3), else of the planet base value (0x3733c0); owner colour. Hidden without bit 1. This is the only credit read-out on the map
  (answers t10's `g_credit_bar` question: there is none).
- Level pips: ground pip i (0..8) is drawn for i < the planet's max ground base level (0x372740), in full owner
  colour when i < the current ground base level, else in half-intensity owner colour (RGB / 2); the current level
  counts as 0 when the local player may not see it (0x50b840). Space pips (0..4) the same with the max (0x372780) and
  current starbase level (0x50c420 / 0x3f8b30). Pips at or past the max are hidden. 0x3db950 then hides **all** pips
  without bit 2; with bit 2 it lays each row out from the planet plus the first pip's `Default_Offset` (space 34 −13,
  ground 34 −26) [M: 0x34c050 = Default_Offset], one pip width per step, lowered by the value text's `Offset` y (−11)
  when bit 1 is on. `g_space_level`, `g_ground_level` (the Bars) and `g_space_icon`, `g_ground_icon` are hidden every
  frame.
- `g_weather` and `g_planet_ability`: shown with bit 4, hidden without it; no visibility gate.
- `g_corruption_text`: shown whenever planet data +0x2e2 or +0x2e3 is set (corruption being applied / removed):
  `TEXT_CORRUPTION_PERCENT` (or `TEXT_CORRUPTION_REMOVE_PERCENT` with +0x2e3) formatted with 100 × the progress
  (0x4b7e20). Such planets are refreshed every frame, the others round-robin.

**Status row (0x3dbc00).** Start = planet position, + the value text's `Offset` y when bit 1 is on, + `g_ground_icon`'s
`Default_Offset` y when bit 2 is on [M: which of its two `Default_Offset` tags], + `g_conflict`'s `Offset` (30, −16).
The visible icons are then placed left to right in this **fixed order**, each centred half its width on, the pen moving
by its width (no gap): `g_conflict`, `g_hero` (you), `g_enemy_hero`, `g_hero` (rebel), `g_hero` (empire), `g_hero`
(underworld), `g_build`, `g_smuggler`, `g_bounty_hunter`, `g_smuggled`, `g_corruption_icon`.

**Show rules (0x3ec380, round-robin: count / 30 + 1 planets per frame from HUD +0xfb0, 30 = 0x140b0a340).** No filter
or visibility gate except where named; "local" = the local player.
- `g_conflict`: when the planet is the one 0x3ce850 recorded (HUD +0xe08; set by the space / land conflict drivers
  0x2b92e0 / 0x2b76a0 and by 0x2a7580) it is shown and starts blinking (`Blink_Duration` 3), and +0xe08 is cleared
  [M: hidden again when the blink ends].
- Hero medals (§1.5): `g_hero` (you) and `g_enemy_hero` only with the global 0x140a7c475; the three faction medals only
  without it. "You" = a hero allied to the local player (0x39a230); enemy = another hero that passes the stealth-hero
  view test 0x2e6810; the faction medal follows the hero owner's faction (the three faction names held at HUD
  +0x2708 / +0x2728 / +0x2748) [M: which list, orbit or surface, is scanned first].
- `g_build`: shown while the planet's owner is the local player and either production queue has a head entry
  (0x401960, queue 0 or 1); otherwise it starts blinking (`Blink_Duration` 6) [M: then hides].
- `g_smuggler` / `g_smuggled`: when planet data +0x200 (the siphoning player, GALACTIC_ECONOMY §1.3 / §1.4) is set:
  `g_smuggler` if that player is local, else `g_smuggled`. Shown to every viewer.
- `g_bounty_hunter`: when the planet data map at +0xa0 has a non-empty entry under key 14 and the planet's owner is
  not the local player [M: key 14 = the bounty-hunter ability].
- `g_corruption_icon`: on a corrupted planet (data +0x2f4 ≠ −1) and bit 4: GameConstants `Corruption_Planet_Icon`
  [corruption type]; on an uncorrupted planet, when the local faction has `Benefits_From_Corruption` (+0x10b):
  `Corruption_Planet_Icon`[8] = `i_icon_corruption_info.tga`, with no filter; otherwise hidden.

**Organiser and planet map labels.**
- `Text_Land_Fleet` (organiser, 0x3e83a0): hidden on a destroyed planet (data +0x2c8); otherwise line 1
  `TEXT_BASE_LAYOUT`, line 2 (`Text_Offset2` 0 −20) `TEXT_UNKNOWN` when the local player may not see the planet's
  ground forces (0x50ba40), else empty.
- `planet_bonus` (planet-map shell, set on entering the map 0x3c5fc0): icon = the planet type's
  `Planet_Ability_Icon` (+0x2230), the texture `g_planet_ability` uses on the map.

**Port (2026-11-17).** `engine/ui/src/galactic_hud.cpp` draws `g_credit_bar` and `g_political_control`, shows the
levels with the `g_ground_level` / `g_space_level` Bars instead of the pip rows, hides weather / ability / corruption
text on fogged planets, and orders the status row differently (hero medals before build, enemy hero last, a 2-unit
gap); retail does none of these.

---

## 2. Fleets travelling between planets

### 2.1 Fleet model `w_planet_fleet.alo` (EaW models.meg; FoC reuses it) [H for the file]
| ALT | meshes | look |
|---|---|---|
| ALT0 | `Fleet_ALT0` | MeshGlossColorize, `w_galaxy_line_dotted`, colorized |
| ALT1 | `Fleet_ALT1` + `glow_ALT1` | MeshAdditive `w_planet_fleet_dot.tga`, UV scroll 0.91, colour (0.88, 0.80, 0.46); billboard `w_galaxy_dot` |
| ALT2 | `Superfleet_ALT2` | never selected [M] |
`w_planet_fleet_ds.alo` = the Death Star sphere.

- The fleet service (0x4352f0) sets the model alt: **1 when every non-transport member is stealthed,
  else 0**. The colour is the owner faction colour (0x266040). Only **one** ALT is drawn
  (the `_ALTn` filter, instance +0xc4). [H]
- In orbit (0x4338d0): position = planet + (18, 10, 0), facing 90°, fleet +0x2b0 = planet. In transit
  +0x2b0 = null; the hop runs from origin + dir × origin radius to dest − dir × dest radius (0x569d50). [H]
- Base screenshots show **no fleet model at orbiting planets**: only the HUD buttons of §1. The in-orbit
  hiding mechanism was not traced [M]. The port draws triangles for all ALTs both in orbit and in
  transit. **Fix (render + apps/game):** draw only while in transit (+0x2b0 null), with only the
  selected ALT.

### 2.2 Drag preview (0x3da30) [H]
While dragging a fleet: lines with the GameConstants +0x70 texture (`w_galaxy_line.tga`), width 2.0;
grey (128) along the planet path; a direct line to the cursor in dark grey (50) only within
`MaximumFleetMovementDistance` (GameConstants +0x108, 100). **ui/render**.

---

## 3. Dashed ellipse around the selected planet (base 25.jpg) [H unless tagged]
- It is the **planet-selection model** `Data\Art\Models\w_planet_select_neutral.alo`, loaded at
  HUD +0x2248 (0x3d3200); the selected planet is HUD +0x2150.
- Placement (0x3c9ab0): at the planet, oriented to the camera (X = screen right, Z = toward the camera),
  IDLE animation synced to the frame; tinted with the owner faction colour (instance +0x13c..+0x148);
  "neutral" colour while the planet is not revealed (0x50bf10).
- Meshes (bones carry a 45° X tilt, which makes the circles read as ellipses):
  - `planet_select`, `planet_select01`: `w_galaxy_select.tga`, the notched inner ring, radius 26.4;
  - `rings_dotted_line`, `rings_dotted_line01`: `w_galaxy_line_dotted.tga`, additive, UV scroll 0.82,
    radius **77.42** — the dashed ellipse;
  - faction variants also have `rings_darken` (`w_galaxy_grad`, radius 146).
- Variant files exist (`w_planet_select_rebel/empire/pirate/_ring`); the HUD loads only the neutral one.
- Measured on 25.jpg: ≈79 world units horizontal radius = the model's 77.42. **It is not a range ring**
  (not MaximumFleetMovementDistance = 100); purely decorative [M].
- **render** draws the model; **apps/game** supplies the selected planet and owner colour.

### 3.1 The thin ellipse round every planet: `g_planet_ring` (t49, 2026-10-30) [H screenshot unless tagged]
Base `tools/probe_mod/fixpass_capture/C2_galactic_silent.png` and `tools/probe_mod/q2_traces/out/20261030-061228-galactic-vanilla-lua/shots/ui_005_gc_running.png`:
- every planet has it, fogged or not, selected or not. It is `planet_ring.tga` (atlas 116×68, stroke alpha at most 111) at atlas size:
  stroke 156 px wide and 89 px high at 1080p (1 unit = 1.406 px) on all planets, whatever their size or distance. A HUD quad, so it also crosses the front of the planet.
- Colour = the name label's (green, blue, grey on the captures), blend alpha ≈ 0.68 on top of the art (fit on both captures). Source of the 0.68 not traced.
- Not the selection model of §3 (r 77 world units). Not a 3D ring.

### 3.2 Trade routes end at the planet centres (t49) [H screenshot]
On `ui_005_gc_running.png` a route becomes visible well inside the disc where it runs toward the camera (Felucia, Kashyyyk downward), and stops at the
silhouette on the far side (Nal Hutta): one straight ribbon centre to centre, depth tested against the sphere.

### 3.3 Planet model alternates (t49)
`W_planet_*` models with alternates: ALT0 low-res textures (`ring_LOW_ALT0` on Kuat), ALT1 high-res textures (`ring_high_ALT1` + `ALPHA_ALT1` on Kuat, `rings_ALT1` on Nal Hutta), ALT2 the fog grid.
The galaxy view shows ALT0 [M: Kuat on `ui_005_gc_running.png` has the plain ring, no `w_ring_mesh` band]. **Who selects ALT1 is not traced**, no capture of a planet close-up exists.

---

## 4. Hero bar (check of HUD_GAPS.md §3) — confirmed, one addition
- Confirmed [H]: `i_main_hero_frame_rebel.alo` slot bones are at negative x (`good00` at −46.7 is the
  rightmost, then −89.9, …), so the friendly bar is pinned top-right and grows to the left. The empire
  (enemy) frame has positive-x slots and pins top-left. Frame meshes ALT0..ALT5 (widths 67…283) use
  `i_main_hero_frame_rebel.tga` — the green frame of the screenshots. 26.jpg (zoomed to a planet) still
  shows the bar.
- **Sort order** (comparator 0x3202e0, used with an insertion sort for these list sizes, so stable) [H for
  the code]: a before b when both `Ranking_In_Category` (type +0x1660) are > 0 and a's is lower; equal
  ranking → type +0x11c ascending (unsigned 32-bit); either ranking ≤ 0 → treated as equal (gathering
  order kept). Slot `good00` (rightmost) gets the first entry.
- Base 24.jpg, right to left: Emperor (rank 1), Boba Fett, Piett (Accuser), Tarkin, Veers (all rank 3).
  The rank-1-first part matches. The tie order does **not** match an unsigned compare of the upper-cased
  name CRC (OBJECT_TYPES.md's meaning of +0x11c), which would put Veers second. It does match the CRC of
  the names as written in the XML (case kept) for the company types `Boba_Fett_Team`,
  `Accuser_Star_Destroyer`, `Grand_Moff_Tarkin_Team`, `General_Veers_Team` [L; one screenshot with 4 ties
  cannot separate the candidates]. Port rule until a probe settles it: lower ranking first, ties by
  CRC32 of the type name as written, ascending.
- **ui** (HUD_GAPS §3 already maps it).

---

## 5. Main menu logos and version string
- FoC Patch2 `MENUBACK_OVERLAY.DDS` (1024×768 A8R8G8B8) contains **both** the top logo (Star Wars /
  Empire at War / Forces of Corruption) **and** the bottom-left LucasArts and Petroglyph logos. It is a
  full-window quad stretched to any aspect (MAIN_MENU_BACKGROUND.md §5). [H]
- `IDC_MENU_PETRO_LOGO` is a push button in `IDD_MAIN_MENU_DIALOG` at (−374, 642, 118×118) with
  `i_button_petro.tga` (118×118, the same art) and mouse-over variants. At 16:9 it lands at about
  x 287–515 px, on top of the overlay's logo (283–512): the button is a hotspot over the overlay art [M].
- Port 29–35 show only the button, so **the overlay quad is missing**. Fix (**ui** + **apps/game**):
  draw the localized `Menuback_Overlay` quad under the dialog (MAIN_MENU_BACKGROUND §6 item 4).

> **Status 2026-10-30:** The overlay quad exists: `GuiManager::set_backdrop` draws one full-canvas quad under the dialog (`engine/ui/include/alamo/ui/gui_dialog.hpp:261-267`), set to `MenuBack_overlay.tga` for the main menu (`engine/ui/src/gui_menus.cpp:467`).

- Version text [H]: `"V <version> <build>"` (0x21dae0, 0x21d7e0), colour (40, 160, 255, 128), at
  normalised (2/W, 1 − textH − 2/H) — bottom-left, 2 px in — in 2D scene #2 (0xc6b80). **ui**.

---

## 6. Attract demo map (corrects MAIN_MENU_BACKGROUND.md §4/§6)
- The map is right: FoC `Demo_Attract_Maps` = `Demo_Attract_Kamino.ted` (space), and the original
  loads it **directly** with Load_Map (0x22f10 step 5) — no galaxy, no conflict. [H]
- **Map object owners are player indices = faction order** (Factions.xml then Expansion_Factions.xml:
  Rebel 0, Empire 1, Pirates 2, Neutral 3, Hostile 4, Sarlacc 5, Underworld 6, Hutts 7). Kamino placements
  by owner [H, read from the .ted]:
  | owner | objects |
  |---|---|
  | 0 Rebel (BasicRebel AI) | Home_One, Calamari_Cruiser, MC30_Frigate, Nebulon_B_Frigate, Alliance_Assault_Frigate, Corellian_Corvette, Marauder_Missile_Cruiser, Millennium_Falcon, 2 Rebel_X-Wing, 2 B-Wing, 1 Y-Wing, 1 A-Wing squadron (15) |
  | 1 Empire (no AI set) | Tartan_Patrol_Cruiser (1) |
  | 3 Neutral | Kamino_Backdrop_Large, Moon_Small, Asteroid Field Large/Medium×3/Small (6) |
  | 6 Underworld (AI_Player_Underworld) | Kedalbe_Battleship, Krayt_Class_Destroyer, 2 Vengeance_Frigate, Interceptor4_Frigate, 2 Crusader_Gunship, Houndstooth, The_Peacebringer, 5 StarViper, 1 Skipray squadron, Underworld_Star_Base_4 (16) |
  This is why 0x22f10 sets AIs on players **0 and 6**. All units come from the map; nothing is added.
- **Why the port shows few ships** [H, from the port code, read-only]: `start_attract_map`
  (apps/game game_main.cpp) builds a two-planet skirmish galaxy with players {Rebel = 0,
  Underworld = 1, Neutral = 2}, hops the Rebel default skirmish fleet to the AI planet and fights that
  conflict on the Kamino map. The battle map pre-pass (apps/sim `bt_map_prepass`) drops placements whose
  owner index is not a session player, so **owner 6 (all Underworld ships and the station) and owner 3
  (Kamino planet, moon, asteroids) are discarded**, and owner 1 (the Empire Tartan) is given to the
  port's player 1 (Underworld). What remains is the 15 map Rebel units plus the two default skirmish
  fleets. `_Space_Planet_Kuat_01.ted` is only the label of the scaffolding galaxy planet (logged, never
  loaded) — or the fallback map when Kamino fails to load as space.
- **Fix (apps/game):** run the demo as a plain tactical space session on the map with the full
  faction-ordered player table (index = faction order, so map owners resolve unchanged), AI on players 0
  and 6, no starting forces, no galaxy hop. If the skirmish-galaxy path is kept, it must create players
  at the faction-order indices and add no default fleets.

> **Status 2026-10-30:** The demo no longer hops a two-planet skirmish galaxy. The attract session is a plain map battle with Rebel and Underworld AI players at faction-order indices and no default fleets (`engine/apps/game/src/game_session.cpp:117-121`, `CampaignBattles::start_map_battle` in `engine/apps/sim/include/alamo/sim/campaign_battles.hpp:171-175`), started by `App::start_attract_map` (`engine/apps/game/app/game_main.cpp:847`).

- EaW data (`front_end_demo_attract.rec` → `Front_end_demo_attract.ted`) uses the same owner rule;
  check its owners with the same .ted scan before choosing AI players.

---

## 7. Story SPAWN_HERO / UNIQUE_UNIT placement (Heart of the Maw: "Coruscant" → Corellia)

Reward enum: BUILDABLE_UNIT 1, **UNIQUE_UNIT 2**, REMOVE_UNIT 3, CREDITS 4, **SPAWN_HERO 5**, …
(switch 0x456970). UNIQUE_UNIT and SPAWN_HERO run the **same handler 0x4626a0**. [H]

### 7.1 Rule (0x4626a0) [H unless tagged]
1. Type = `Reward_Param1` by name; unknown → log "Invalid object type specified", nothing.
2. Anchor planet:
   - `Reward_Param2` given: the first live object whose type is that name (planet type name).
   - **None exists (planet not on this map): the first planet in the plot player's per-owner object list**
     (query 0x2a9ff0, planet behaviour, owner = plot player). None → log "can't find a planet!", nothing.
     The **capital / Home_Location is not consulted**.
   - `Reward_Param2` empty: the planet passed with the event; none → log "No planet specified".
3. Ownership check: if the anchor is not fully owned by the player (both owner fields of the planet's
   owner record 0x3956c0 equal the player [M: space and ground owner]), pick the **nearest** planet
   (squared galactic distance from the anchor) that is fully owned by the player (0x3f7e70).
4. If that planet's orbit owner (0x3f9270) exists and is not the player (enemy fleet in orbit), pick the
   nearest fully-owned planet without a foreign orbit owner (0x3f7f20); none → keep step 3's choice.
5. Log "STORY CREATE - CREATING %s NEAR %s (chose %s)". Create `Reward_Param3` copies (atoi; empty → 1;
   < 1 → none) through the planet create path (0x400b40), each added to the planet HUD (0x3d2410). For
   the local player: the HUD flashes/selects the new unit's fleet or planet and highlights the planet
   (0x3d01d0, 1.5 s). Then the planet's first non-stealthed orbit fleet re-enters orbit / merges
   (0x4338d0) [M].

### 7.2 Heart of the Maw (Sandbox_Heart_Of_The_Maw_Empire) [H for data, M for list order]
- Empire planets: Kashyyyk, Kuat, Corellia, Tatooine (Starting_Forces order); `Locations` order:
  Corellia, Felucia, Naboo, Kamino, Kuat, Kashyyyk, …; Home_Location Empire = Corellia.
- Starting tech 3, so the tech-1/2/3 events (Boba Fett, Veers, Accuser/Piett) fire at start together with
  Tarkin and the Emperor: five spawns on day 1, all at Corellia (base 24/26).
- Corellia is first in Locations order **and** the home; the code takes the per-owner list order, not the
  home. Corellia before Kashyyyk means the list is not in Starting_Forces order. **Port rule:** the
  lowest-index planet (map/Locations order) owned by the plot player [M], then steps 3–4. The port's
  `story_session.cpp` currently logs a failure here (its comment marks the fallback as open).

> **Status 2026-10-30:** With the named planet missing from the map, SPAWN_HERO / UNIQUE_UNIT now anchor on the lowest-index planet the plot player owns and then apply steps 3-4 (`engine/apps/sim/src/story_session.cpp:900-940`).

- Module: **apps/sim** story rewards (galactic sim); no presentation module involved.

---

## 8. Open items / probes
- Fleet model in orbit: confirm it is hidden (and how) — probe: a fleet parked at a planet, zoomed in.
- Hero-bar tie key: a probe save with three rank-3 heroes whose CRC orders differ between the
  original-case and upper-case names.
- Per-owner list order for §7: a probe campaign where the first owned planet in Locations order is not
  the first in Starting_Forces and not the home, with a SPAWN_HERO at a missing planet.
- Superfleet_ALT2 use; the meaning of global 0x140a7c475 (hero medal mode).
