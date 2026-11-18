# mcx4-pres: RE answers, third batch (t13 / t14 / t15)

Answers to `MCX4_PRES_RE_QUESTIONS_3.md` (lane mcx4-pres, Q1-Q12). Retail `StarWarsG.exe` FoC x64 (Steam), 2026-11-18.
Addresses are RVAs (image base 0x140000000). Behaviour only, no decompiled code. **[H]** read and cross-checked,
**[M]** read, one link inferred, **[L]** inferred. Nothing was renamed or commented in the shared Ghidra project.

## Summary

Counts: **all 12 questions answered. Rows: 15 reader + effect, 1 read with effect only through another row, 1
retail-unused, 3 ledger mis-attributions (Q8).** Two earlier answers are corrected (end of file).

| Q | Row / question | Verdict | Conf |
|---|---|---|---|
| Q12 | Type `Radar_Rotate_Icon` | reader + effect, but **not** a facing turn: it turns the icon texture a quarter turn clockwise inside its quad. The facing turn comes from `Radar_Show_Facing` (default Yes): quad turned counter-clockwise by (Z angle − 90°), world +X right, +Y up, radar never turns with the camera | H (screen direction M) |
| Q1 | Type `Camera_Aligned` | reader + effect, but **no billboard**: the object is put on a manager list whose update rebuilds the drawn transform from the object's stored angles every update. No camera data is read | H (no camera: M) |
| Q3 | Type `Highlight_Blob_Material_Name` | reader + effect: drawn every frame for every object of the type **owned by the local player** (not tied to selection or hover), as well as the normal selection blob; size Select_Box_Scale × Scale_Factor | H |
| Q2 | GC `Num_Structures_For_Medium/Large_Planet_Name` | reader + effect: the class picks entry 0 / 1 / 2 of the `g_planet_name` component's `Alternate_Font_Name` list as its font (FoC: Medium, Medium, Bold); point size unchanged (7) | H |
| Q11 | Land bombard targeting | a separate button: `pb_switch` toggles bombard targeting; `b_special_weapon` toggles bombing-run targeting on land and fires the special weapon object in space. A bombard click queues PlanetaryBombardEvent (event type 54) with the ground point | H |
| Q4 | GC `Tool_Tip_Small_Font_Name` / `_Size` | reader + effect: the in-world object tooltip (hover box over a unit); small font only for the "contains" lines (TEXT_CONTAINS + unit name + " x N") and the bar row height | H |
| Q5 | GC `Icons_Per_Column` | reader + effect: tactical sidebar column (clones of `skirmish_upgrade`), new column every 13 icons | M |
| Q5 | GC `Min_Accuracy_For_Icon`, `Min_Sight_Range_For_Icon` | reader + effect: the per-unit weather status icon is shown when the current weather's accuracy modifier < 0.4 or sight modifier < 0.5 | H (categories M) |
| Q6 | Movie `Overlay` | reader + effect: the movie quad goes into the last draw list of the movie layer instead of the first (also forced by `Alpha`) | H (visible meaning L) |
| Q6 | Movie `Pause_Game_While_Playing` | reader + effect: pauses the game mode at movie start, resumes at movie stop, for any movie played through the Bink player, command-bar ones included | H |
| Q6 | Movie `Movie_Start_Frame` | reader + effect (settled in T14): first shown frame | H |
| Q7 | Type `User_Bound_Min` / `_Max` | reader + effect: replace the render model's bounding box when Min is not (0, 0, 0) | H (consumers M) |
| Q7 | Type `Should_Death_Clone_Play_Idle` | reader + effect: the death clone starts Idle when the dying object had no animation running | H (anim fields M) |
| Q8 | "TacticalCamera" `Intensity`, `Radius`, `Diffuse` | **ledger mis-attribution**: they are LightSource rows (LightSources.xml), read by the light parser; the FoC counts add up same-named tags of Decal, Flare, LightningEffect and LightSource | H |
| Q9 | Faction `Minimum_Visible_Base_Level` | reader + effect: one way a planet's ground / space structures become visible to a faction on the galaxy map: base level ≥ this value | M |
| Q9 | Faction `Standalone_Space_Maps_Special_Weapon_A` / `_B` | reader + effect: in standalone (skirmish / MP) space battles each playable faction gets this object spawned at the map's special-weapon marker | H |
| Q10 | GC `GUI_Flash_Level`, `GUI_Rapid_Flash_Duration` | reader + effect: **object** model flash (not a button): brightness × (1 + Level × remaining / duration), 3 flashes | H |
| Q10 | GC `GUI_Planet_Flash_Level` | **retail-unused** (only the GameConstants ctor writes it, default 10) | H |

## Method

- **ReVa was not available to this agent** (no `mcp__reva__*` tools were exposed to the subagent). Used instead:
  the Ghidra 12.2 export `re_reference/ghidra/StarWarsG_decompiled.zip` (read only, never copied), a capstone
  linear sweep of every `.pdata` range plus the gaps between them (leaf functions; 2,003,164 instructions) with
  RIP targets resolved, and REA (Ghidra 12.1.4; the session needed a 25-minute startup limit to finish its analysis,
  then answered at once).
- Provenance filters as in `MCX4_T14_RE_ANSWERS.md` "Method": object type through `[obj+0x298]`, faction through
  `[player+0x68]`; GameConstants fields by RIP target (base 0x140b15800).
- Field offsets from `docs/re/xml_field_tables.md`; data from `extracted/foc_data_text.zip`.

## Q12 GameObjectType.Radar_Rotate_Icon (+0xd6) and the radar icon angle

### 12.1 Where the angle comes from [H]

- Type defaults (reset 0x367100, called by the type ctor 0x367250 on type + 0x10): `Radar_Rotate_Icon` = No,
  `Radar_Show_Facing` (+0xdc) = **Yes**. FoC sets `Radar_Rotate_Icon` Yes on Empire_Star_Base_2..5 (STARBASES.XML) and
  `Radar_Show_Facing` No on 18 objects (skirmish buildables, markers, special structures).
- Game-object blips (builder 0x35b4d0, both branches 0x35b9d7..): the blip record (0x54 bytes, added by 0x356410)
  gets +0x3c = `Radar_Rotate_Icon`, +0x40 / +0x44 = `Radar_Icon_Size`, and the angle triple +0x48..+0x50 = the
  object's orientation angles (obj +0x84 / +0x88 / +0x8c, degrees) when `Radar_Show_Facing` is set, else
  (0, 90, 0) (constant 0x140a28520). Only +0x50 (the Z angle) is used by the draw.
- The object's Z angle is its yaw in degrees: set together with the copy at +0x98 by 0x3a8710; the sim places
  model-space offsets by rotating them by Z (0x38de80, local +X forward, counter-clockwise from world +X).
- Radar proxy blips (0x35c000, the mode's radar proxy list): angle = atan2 of the proxy matrix's X axis
  (column 0: m10, m00) in degrees + 90 when `Radar_Show_Facing`, else 0 [H read; what proxies are: L].

### 12.2 How the draw uses it [H read, M screen direction]

Quad builder 0x26ba90 (radar render object, 0x26xxxx):

- Position: world X / Y map linearly into the radar rectangle (render +0x60..+0x6c bounds) as x = 2f − 1, y = 2g − 1;
  world +Y is the radar's top (the terrain texture 0x268fb0 puts world +Y at texture row 0). No camera term: **the
  radar does not turn with the camera**; only the view indicator shows the camera.
- Flip: render +0x4c0 mirrors both axes and adds 180° to the angle. Its only setter path (0x268ef0 via 0x356ba0,
  from 0x2b3e00) passes 0, so retail never flips [M].
- Rotation: when the record angle A is not exactly 0, the quad's four corners turn about the blip centre by
  θ = A × π/180 − π/2 (+π if flipped), counter-clockwise for positive θ (x' = c·dx − s·dy, y' = s·dx + c·dy).
  **A == 0.0 skips the rotation** (so a unit at Z = 0 is drawn unturned, 90° off the formula; also every blip with
  `Radar_Show_Facing` No).
- `Radar_Rotate_Icon`: the four UV corners are shifted one vertex along the quad, which shows the texture turned a
  quarter turn **clockwise** inside the quad (texture top along the quad's right edge).

### 12.3 What the port draws [M]

- Not `Radar_Rotate_Icon`: icon texture top points along the facing (A − 90° + 90° = Z), counter-clockwise on
  screen for a positive Z.
- `Radar_Rotate_Icon`: the texture's left edge points along the facing (top points to Z − 90°).
- Both: no turn at A exactly 0, none for `Radar_Show_Facing` No, none for dot blips (types without a radar icon,
  drawn by 0x359a90).

## Q1 GameObjectType.Camera_Aligned (+0x2f)

- The object update 0x3989a0 (at 0x3993f0, the only read of type +0x2f in the image) calls 0x29bfa0: it adds the
  object to GameObjectManager list +0x130 (once; the manager holds a reference). Removal: 0x2b6850 from the
  remove-from-world path 0x39fc90, and the manager teardown 0x29c800 / dtor 0x29aaa0. [H]
- GameObjectManager update 0x2be640 (loop 0x2be7df..0x2be81d) walks that list and calls the transform rebuild
  0x3ac530(object, 0) for every entry, every update. [H]
- 0x3ac530 (the general "rebuild from angles", called from 16 other call sites) sets the world matrix to
  translation = position (+0x78..+0x80) and rotation = Rz(+0x98) · Ry(+0x94) · Rx(+0x90) · Rz(90°), angles in
  degrees (Rz 0x480f0, Rx 0x2cf8d0, Ry inline); pushes it to the model, re-places the shadow blob (0x266340), updates
  attached children, clears the "transform dirty" bit (obj +0x3a1 bit 0). [H]
- **No camera position or direction is read** anywhere in this path, and the list has no other reader [M: no
  writer of a camera-aligned object's angles from camera code was found either].
- So: not a billboard. The model's axes stay the object's own; sim facing is untouched (only the drawn transform is
  refreshed). FoC sets it on every galactic planet (PLANETS.XML, `yes`; Galaxy_Core_Art_Model `No`) and on
  Empire_Star_Base_2..5 and UM12_STAR_BASE. Port: refresh the drawn transform of these objects from their stored
  orientation each update (including the fixed 90° art turn about Z), no camera facing.

## Q3 GameObjectType.Highlight_Blob_Material_Name (+0x348, handle +0x368)

SelectBehavior (vtable 0x867768), service vf6 0x3c2310 [H]:

- **Creation** (0x3c19c0): once, while the object is not hidden in a container (obj +0x3a7 clear; set by the
  garrison / contain path 0x37ade0, cleared by 0x37a095..). Needs a resolved handle (> 0). Land (mode type 1): a
  ground blob (class 0x12f3e0) at select data +0x20; space (mode type 2): a space blob (0x13a2d0) at +0x30.
  Colour: the owner faction's `Selection_Blob_RGBA` (+0x28b4), or the player colour (0x281940) when GMM flag
  0x28af60 is set [M: multiplayer]. When obj +0x3a7 becomes set the blob is removed (0x3c1d80) and remade later.
- **Visible** (per update, 0x3c2710): when the object's owner is the **local player** (0x39b020: owner id == local
  id), no cinematic is running (0x2cd0c0 on 0x140b27a10) and controls are not locked (0x140a28c91, set by Lua
  `Lock_Controls`, 0x543060). Selection and mouse-over play no part.
- **As well as** the normal selection blob: that one lives in other slots (+0x18 land / +0x28 space), is made by
  0x3c1f90 when the local player selects the object, and is visible for the local player's objects or allied
  objects with `Is_Community_Property`. A selected hero shows both.
- **Size / place**: square, side factor Select_Box_Scale × Scale_Factor (type +0x318 × +0x658) for both axes,
  rotation 0, at the object's position (or its container's when attached, 0x37a6c0). Land: projected ground blob
  (0x12f570); space: flat blob (0x13a460).
- Port: for the local player's objects whose type has the material, draw this blob every frame (land ground blob /
  space blob), in addition to the selection blob.

## Q2 GameConstants.Num_Structures_For_Medium_Planet_Name / _Large_ (+0x194 / +0x198)

- Galactic planet labels 0x3ea1b0: the component is the planet's `g_planet_name` (label set slot 9; slot 10 gets
  the formatted value text) [M slot = name]. The class is picked only when the planet's ground info is visible to the
  local player (0x50b840, see Q9); otherwise class 0. [H]
- `g_planet_name` is a TextButton; its vf +0x1c0 is 0x350ed0: when the class changes it copies entry [class] of the
  component's `Alternate_Font_Name` list (data +0x210, a string list split on spaces / commas by the list parser,
  0x51a280) into `Font_Name` (+0x1f0) (0x34c130; an out-of-range class keeps `Font_Name`), rebuilds the font through
  the font manager 0x260cc0 with the component's `Font_Point_Size` (+0x3b8), bold on, and re-lays out both text
  lines. [H]
- FoC (COMMANDBARCOMPONENTS.XML): `Font_Name` EmpireAtWar-Bold, `Alternate_Font_Name` "EmpireAtWar-Medium,
  EmpireAtWar-Medium, EmpireAtWar-Bold", `Font_Point_Size` 7. So: fewer than 3 structures → EmpireAtWar-Medium,
  3-4 → EmpireAtWar-Medium, 5 or more → EmpireAtWar-Bold; size 7 in every class. No scale, no other component.
- Plain TextComponents (vf +0x1c0 = 0x350ec0) only store the class.

## Q11 Land bombard targeting (0x2e7bc0) and bombing-run targeting (0x2e7950)

- **Buttons** (component callback table 0xa174e0, 0x48-byte entries): `b_special_weapon` → 0x346a80;
  `pb_switch` → 0x347930 → 0x2e7bc0 (bombard toggle); `b_special_weapon2` → 0x346b50. [H]
- 0x346a80: HUD mode (HUD +0x211c, set by 0x306290) 2 → 0x31f760: fire the special weapon object bound to the
  button; any other mode → 0x2e7950 (bombing-run toggle) [M: mode 2 = space tactical]. So on land
  `b_special_weapon` is the bombing run and **bombard has its own button `pb_switch`**; the choice is by button, not by
  faction or manager. Each toggle notifies its manager (BombingRunManager = mode[7], 0x4953d0; PlanetaryBombardManager
  = mode[8], 0x5296d0) and, in land mode, makes the target marker blob (faction +0x26d8 / +0x2700). Toggling off
  plays the faction's cancel line (+0x2000 / +0x2050). The bombard toggle also calls the button's vf +0x2e8.
- **Click in bombard targeting** (0x2f6c30, left click): the ground point under the cursor (0x3bb2d0); check
  0x528a70(PBM, local player, point); if it passes: a PlanetaryBombardEvent (ctor 0x52afc0, **event type 54 / 0x36**,
  target point at +0x48..+0x50 via 0x5247a0) is queued on the event queue 0x140b2ed18 (0x3b01f0), 0x5295f0 plays
  `SFXEvent_Bombard_Incoming`, the marker is removed and targeting ends. If the check fails: an error sound
  (0x140b301c8) and targeting stays on.
- **Event execute** 0x52aff0: the point is quantised per axis (0x23bc10), z below 0.0001 becomes 0, the check is
  repeated for the event's player (+0x24) and 0x528140 starts the bombardment there.
- Bombing run, same flow: check 0x494540 on mode[7], BombingRunEvent (ctor 0x524560, **type 28 / 0x1c**), 0x495360.

## Q4 GameConstants.Tool_Tip_Small_Font_Name / _Size (renderer 0x140a286f0 +0xc8)

- 0x140a286f0 is the **in-world object tooltip** (its first field is the hovered object id; 0x3a89d0 marks it
  dirty when that object's health changes). The text box: AddLine 0x3b0e60(box, text, small) stacks lines (width =
  widest, 0.0035 padding, background 0x26ecf0, border 0x26c4e0), font +0xc0 normal or +0xc8 small. [H]
- The object description 0x39d8f0 adds the name line ("name - N%"), extra lines and bar rows, all with the normal
  font, except:
  - **Contents lines** (0x39e140, the only caller passing small = 1): for each distinct object type in the object's
    transport / garrison cargo (behaviour slot +0x337, list at obj +0xc8 → +8) and in its team list (obj +0xf8 → +0x18,
    when the call is not for a sub-object), one line: localised `TEXT_CONTAINS` + the type's display name, plus
    " x N" when there are more than one. [H read, M list meanings]
  - **Bar rows** (0x3b11b0, from 0x39d6c0): a 0.1-wide bar, filled part green (24, 168, 42) for the fraction from
    0x385a70, rest dark red (112, 0, 0); the row height is 0.4 × the small font's line height and an optional label is
    drawn in the small font (the only caller passes an empty label). [H read, L fraction = health]
- Not used by the command-bar tooltip (`tooltip_name` / `tooltip_price` components).

## Q5 Command bar icon rows

### Icons_Per_Column (GC +0x124c, FoC 13) [M]

- 0x301190 (frame layout) lays out the HUD icon list (HUD +0x1138, built by 0x317480 as clones of the
  `skirmish_upgrade` component, index 0x232): one icon per local-side object (owner allied with the local player,
  0x2823e0) whose type has `Show_In_Sidebar_When_Complete` (+0xca), icon `Icon_Name`, text "N%".
- Start at the `skirmish_upgrade` position; up to three header entries (HUD +0x1160..+0x1162) and the leading run
  of entries (up to the first whose type has `Show_In_Sidebar_Tray`, +0xcb) go straight down; the rest go down one
  icon height each and, after `Icons_Per_Column` icons, start a new column one icon width to the right at the top.
  The second list (HUD +0x1150) continues the same wrapping. **reader + effect**.

### Min_Accuracy_For_Icon (+0x13bc, FoC 0.4), Min_Sight_Range_For_Icon (+0x13c0, FoC 0.5) [H, categories M]

- Gate 0x2f9950, called by the unit status-icon update 0x31de70 for each unit with a status set: the weather icon
  (status slot 5, the `lt_weather_icon` component [M]) is shown when the unit, or any member of its company, has:
  - a weapon of category 4, 6 or 7 (weapon record +0x1fe8; also category 4 for units with behaviour 0xb) whose
    current-weather accuracy modifier (0x468750) is **below** `Min_Accuracy_For_Icon`, or
  - a current-weather sight modifier (0x468510) **below** `Min_Sight_Range_For_Icon`.
- Weather system 0x140b30b40: a modifier is looked up only while the weather blend (+0x448) is above 0.5; missing
  category → the Default entry → 1.0. The icon texture is "i_pa" + the current weather's name with its first 14
  characters dropped (0x468860).
- Both rows **reader + effect**.

## Q6 Movie rows (Bink player 0x44d920 start, 0x44f130 stop)

- **Overlay** (+0xd1) [H read, L visible meaning]: the full-screen movie quad is added to the movie layer
  (0x140b306a0) with 0x25c080 (first of its six draw lists) when Overlay and Alpha are both off, else with 0x25c220
  (last list); the layer draws its lists in order (0x25c8c0). So an Overlay movie is drawn after everything else in
  that layer (captions, credits). Not a texture name. Movies started with "drawn by caller" (the command-bar movie
  0x2fc7a0) skip this. FoC: on the six win movies, the two Underworld campaign win movies and Credits.
  **reader + effect**.
- **Alpha** (+0xd2) (not asked, same code): Bink opened with alpha, alpha texture, blend mode 2.
- **Pause_Game_While_Playing** (+0xd3) [H]: at start the game mode's vf +0x38 (pause) is called, at stop vf +0x40
  (resume), when a game mode exists. It applies to every movie played through 0x44d920 with the flag, the command-bar
  movie path included; FoC sets it only on the four Star_Wars_Intro movies and Underworld_Prolog_Text.
  **reader + effect**.
- **Movie_Start_Frame** (+0x20): T14 — when start + 1 lies in (1, frame count) the player jumps there (BinkGoto,
  1-based) before the first frame, so the first frame shown is index `Movie_Start_Frame`; the value also seeds the
  frame counter. **reader + effect**.
- Has_Credits (+0xd0) starts the credits, Halt_Game_Music (+0xd4) stops the music (0x2d2790) [H, not asked].

## Q7 User_Bound_Min / _Max, Should_Death_Clone_Play_Idle

- **User_Bound_Min** (+0x300) / **User_Bound_Max** (+0x30c) [H read, M consumers]: when a model is made for the
  object (0x3a4820 at 0x3a4b11, 0x3be4b0 at 0x3be4f8) and Min is not exactly (0, 0, 0), the render model's bounding
  box is set to (Min, Max) (0x12db90: model +0x158..+0x16c, flag +0x31 bit 4) and its bounds are recomputed from that
  box instead of the meshes (0x12def0). Max is used as given. The model bounds feed culling and model-extent queries
  (e.g. 0x264ab0, used by radar blips and planet bounds); object selection and collision use their own data.
  **reader + effect**, both rows.
- **Should_Death_Clone_Play_Idle** (+0x93) [H read, M animation fields]: death clone creation 0x29f270 (from
  0x39bdb0) picks the clone type (0x3718f0: the dying type's clone for the damage type, else default, else its own
  type), spawns it with the dying object's position and angles and copies its animation state (0x395590). If the dying
  object had an animation running, the clone plays the same type and sub-index (0x3a92f0); if none and the clone type
  has this flag, it plays animation type 0 (Idle) with sub-index 0xffff (any). Otherwise no animation is started there.
  **reader + effect**.

## Q8 "TacticalCamera" Intensity / Radius / Diffuse — ledger mis-attribution [H]

- The tags sit in the light-source descriptor table at 0x140a40570, nine entries: `Diffuse` (vec3, +0x4),
  `Intensity` (float, +0x0), `Radius` (+0x10), `Falloff_Start` (+0x14), `Auto_Destruct_Time`,
  `Auto_Destruct_Fade_Time`, `Blob_Radius`, `Blob_Color`, `Blob_Intensity`. It follows the TacticalCamera table in
  memory, which is why the segment split put three of them under TacticalCamera.
- Reader: the per-light parser 0x489340 → 0x5191a0 (table 0x140a40570, count 9); also registered by 0x515ee0.
  TacticalCamera has no such tags.
- Effect (PRES_2 Q6 LightSource): light colour = Intensity × Diffuse (× the auto-destruct fade); Radius is where the
  falloff reaches 0 (full strength up to Falloff_Start). Defaults Intensity 1, Diffuse 1,1,1, Radius 40.
- The ledger's FoC counts are sums of same-named tags of other classes: Intensity 20 = 19 Decal (TERRAINDECALFX,
  +0x60) + 1 LightSource; Radius 19 = 12 Flare + 6 LightningEffect (+0xcc) + 1 LightSource; Diffuse 13 = 12 Flare +
  1 LightSource. Port: move the three rows to LightSource (1 use each in FoC); Flare Diffuse / Radius are settled in
  PRES_2; Decal Intensity and LightningEffect Radius were not traced here.

## Q9 Faction rows

- **Minimum_Visible_Base_Level** (+0x20ac, int; FoC Rebel 1, Empire 3, Underworld 1; ctor default 0) [M]: the
  galaxy-map visibility tests 0x50b840 (ground) and 0x50c420 (space), and 0x50bf10 (label colour), treat a planet's
  ground / space structure info as visible to a faction when, among other routes (fog reveal flag, allied owner), the
  planet's ground base level (data +0x130) / space station level (+0x148) is **at least** the viewing faction's
  value [M: +0x130 / +0x148 are the levels, both compared with 5 at 0x400ccb]. **reader + effect**.
- **Standalone_Space_Maps_Special_Weapon_A** (+0x2b00, resolved type +0x2b08) / **_B** (+0x2b10 → +0x2b18) [H]:
  - Load_Map 0x56480: in a standalone game (GMM 0x28ae40 [M]) in space mode, at the first object of kind 99 (the
    map's special-weapon marker [M]), for every playable faction (+0x20a8) with a type set, the type is spawned for
    that player with the marker's position and angles; when the type's `Special_Weapon_Index` (+0x1de4) is 0 or more
    it is registered as the mode's special weapon (0x35c520).
  - 0x293d60 (from 0x294fa0 / 0x295e4c) does the same, spawning only types with an index of 0 or more.
  - 0x317d00 (HUD, space) looks the local faction's A type up for the special weapon button.
  - FoC: Rebel A Ground_Ion_Cannon, Empire A Ground_Empire_Hypervelocity_Gun, B empty, Underworld none.
  - **reader + effect**, both rows.

## Q10 GUI_Flash_Level, GUI_Rapid_Flash_Duration, GUI_Planet_Flash_Level

These drive an **object** colour flash (obj +0x1b0 record), not a command-bar button [H]:

- Mode set 0x392ba0: mode 1 = level `GUI_Flash_Level` (+0x11fc, FoC 3.0), duration `GUI_Flash_Duration`
  (+0x1200, 0.4), count 1; mode 2 = the same, count 3; mode 3 = level `GUI_Flash_Level`, duration
  `GUI_Rapid_Flash_Duration` (+0x1204, 0.1), count 3; mode 7 = fade-in over `GUI_Planet_Fade_Duration`. Start time =
  frame / FPS.
- Apply (colour callback 0x3a0c20): modes 1-3 multiply the model colour RGB by 1 + Level × (remaining / duration),
  so the object starts at 4× brightness (FoC) and falls linearly to normal; when a flash ends the count drops and the
  flash restarts until it reaches 0. Mode 7 scales alpha by 1 − remaining / duration.
- Triggers: mode 3 when the object is selected by the HUD click path 0x37250 (the object, or every member of its
  company) and by 0x44fe0; mode 2 by the story reward FLASH_OBJECT (0x456970); mode 7 by galactic mode 0x4b26d0. No
  caller sets mode 1 directly.
- `GUI_Flash_Level`, `GUI_Rapid_Flash_Duration`: **reader + effect**. `GUI_Planet_Flash_Level` (+0x1220): only the
  GameConstants ctor writes it (default 10.0, 0x2ca13d); **retail-unused**.

## Corrections to earlier answers

- T15 `Camera_Aligned`: not a billboard; the object's drawn transform is rebuilt from its own angles every manager
  update (Q1).
- T14 `Radar_Rotate_Icon`: does not make the icon turn with the facing; it turns the texture a quarter turn inside
  the quad. The facing turn is `Radar_Show_Facing` (default Yes) (Q12).

## Not settled

- Q12: screen direction (counter-clockwise, texture top = facing) rests on the radar quads using y up, inferred from
  the terrain texture's row order [M]; what the radar proxy list (0x35c000) holds [L].
- Q1: no camera writer of a camera-aligned object's angles was found; a TTD trace of a galactic planet's +0x84..+0x98
  would confirm [M].
- Q5: the names of weapon categories 4 / 6 / 7 (the enum is not in the shipped XML) [M]; the exact header / tray
  rules of the sidebar column [L].
- Q6: what else is drawn in the movie layer, so the visible effect of Overlay [L].
- Q9: meaning of planet +0x130 / +0x148 as base levels [M]; the other reveal routes were not traced.
