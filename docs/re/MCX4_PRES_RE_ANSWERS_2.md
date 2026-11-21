# mcx4-pres: RE answers, second batch (t13 / t14 / t15)

Answers to `MCX4_PRES_RE_QUESTIONS_2.md` (lane mcx4-pres, Q1-Q7). Retail `StarWarsG.exe` FoC x64 (Steam), 2026-11-18.
Addresses are RVAs (image base 0x140000000). Behaviour only, no decompiled code. **[H]** read and cross-checked,
**[M]** read, one link inferred, **[L]** inferred. Nothing was renamed or commented in the shared Ghidra project.

## Summary

Counts: **Q1-Q4 and Q7 settled. Q5: 11 rows settled (6 reader + effect, 5 retail-unused). Q6: 24 rows settled (18
reader + effect, 6 retail-unused). 0 rows still open.** Corrections to earlier answers are listed at the end.

| Q | Row / question | Answer | Conf |
|---|---|---|---|
| Q1 | Countdown column | Font `Game_Play_UI_Countdown_Font_Name` / `_Size` (bold, outline, no shadow); FoC XML sets neither, so Arial 12. Retreat timer shown to every player for any retreat, text "label 010.933" (seconds, `%03d.%03d`); build timers "label MM:SS". Galactic shows build timers only, tactical shows the retreat timer only. X = left edge, Y = top edge, fractions of the screen | H |
| Q2 | Win / lose banner | Removed only by the next banner call or mode teardown; no timer, not by the battle-end dialog. Draw flags: shadow off, outline on | H |
| Q3 | `SFXEvent_Special_Ability_Loop` | reader + effect: the object's single looped-sound slot, started / stopped by six abilities | H |
| Q4 | Retreat refusal / cancel | Codes 1, 2, 5 (plain line), 3 (Reason_1, interdiction), 4 (Reason_2, RetreatPreventionAbility), 6 (Reason_3, space TSW owned). Cancel = second retreat click by the retreater (cancel line) or the check turning false during the countdown (silent on land) | H |
| Q7 | Ability light pulse | Multiplies the model colour (not an added light), RGB only. 1 linear flash back from colour, 2 triangle, 3 sine, 4 sine both ways (colour / colour2), 5 endless cosine; N pulses over the duration | H |
| Q5 | `Space_Is_Guide_Rectangle`, `Land_Is_Guide_Rectangle` | reader + effect: radar camera indicator drawn as a bounding rectangle instead of the trapezoid | H |
| Q5 | `Use_Event_System` | reader + effect: gates radar map event blips | M |
| Q5 | `Alternate_Name_Text`, `Alternate_Description_Text` | reader + effect: ability button tooltip text, with fallback keys | H |
| Q5 | `Scale_Duration` | reader + effect: length of the button "scale up" pulse | M |
| Q5 | `Include_In_UI_Map_Header`, `Name_Adjust`, `TSW_Post_Music_Wait_Frames`, `Planet_Ability_Name`, HardPoint `Tooltip_Text` | **retail-unused** | M |
| Q6 | `Enemy_Color`, `Player_Color` | reader + effect: faction colour getter, "simplified faction colours" option | H |
| Q6 | `Apply_Y_Turret_Rotate_To_Axis`, `Apply_Z_Turret_Rotate_To_Axis` | **retail-unused** | H |
| Q6 | `Particle_Attach_To_Collision` | reader + effect: hit particles stuck to the struck object | H |
| Q6 | `Pre_Lit` | **retail-unused** | M |
| Q6 | `Highlight_Blob_Material_Name` | reader + effect: selection blob material | H |
| Q6 | `Land_Terrain_Model_Mapping` | reader + effect: per-environment land model | H (M key) |
| Q6 | Decal `Category` | **retail-unused** (copied and thrown away) | H |
| Q6 | Flare `Diffuse`, `Radius`, `Fraction`, `Slot_X`, `Slot_Y`, `Num_Slots` | reader + effect: lens flare quad colour, size, place on the flare line, atlas cell | H |
| Q6 | LightSource `Falloff_Start`, `Auto_Destruct_Time`, `Auto_Destruct_Fade_Time`, `Blob_Radius` | reader + effect | H |
| Q6 | LightSource `Blob_Color`, `Blob_Intensity` | **retail-unused** (parsed into locals, never stored) | H |
| Q6 | LightningEffect `Replace_Faded`, `Texture_Repeat`, `Texture_Scroll` | reader + effect | H (L axis) |

## Method

- ReVa (Ghidra 12.1.4, `/StarWarsG.exe`) for decompilation and cross-references; REA was not needed (ReVa was free).
- A capstone linear sweep of `.text` (2,189,205 instructions) for displacement searches, the same method and
  provenance filter as `MCX4_T14_RE_ANSWERS.md` "Method": object type through `[obj+0x298]`, faction through
  `[player+0x68]`, hard point data through `[hp+0x20]`, ability records through 0x374b90 / 0x397d90.
- Field-descriptor tables (stride 0x20: tag pointer, type code, offset at +0x10) give each tag's offset; parser type
  codes as in T14 (0x17 string, 0x16 RGBA, 0x10 vec3, 0x8 float, 0x6 int, 0x0 bool, 0x1f SFX event, 0x34 terrain map).
- Game data: `extracted/foc_data_text.zip` (GAMECONSTANTS, FACTIONS, EXPANSION_FACTIONS, LIGHTSOURCES, LENSFLARES,
  LIGHTNINGEFFECTTYPES, TERRAINDECALFX).
- Retail-unused means: no read of the stored field outside the XML parser and the copy / default code, in the sweep
  of the field offset filtered by the owning object's provenance. That is [M] unless the only consumer was read.

## Q1 Countdown timer column

### 1.1 Order of the column [H]

- The frame draw (0x2ee40 / 0x3c9e0) first deletes last frame's timer texts (the HUD list at +0x18, 0x3d950). Then it
  sets Y to `GUI_Strategic_Countdown_Timers_Screen_Y` (GC +0x1238) in galactic mode or
  `GUI_Tactical_Countdown_Timers_Screen_Y` (GC +0x122c) in tactical.
- Tactical: the TSW countdown 0x3cad0 runs first. It writes " %03d.%03d" into the command-bar text component of the
  HUD (0x140b27f60, through 0x31d2b0) in palette 0x140a7bca0 colour 13 or 10, and does not move Y. Then the retreat
  timer 0x3ce60.
- Galactic (mode type 0) only: the build timers 0x3d2b0.
- So the retreat timer and the build timers never share a column: one is tactical-only, the other galactic-only.

### 1.2 Font [H, fallback M]

- Every timer uses one font: global 0x140a60de8, made in GamePlayUIClass::Init 0x30010 through the font maker 0x260cc0
  (font manager 0x140b0a130). Name `Game_Play_UI_Countdown_Font_Name` (GC +0x3d0), size `Game_Play_UI_Countdown_Font_Size`
  (GC +0x3f0); bold on, italic off, resolution-scaled, stretch 1.0.
- Defaults (GC ctor 0x2c91c0): size 12, name empty. FoC GAMECONSTANTS writes `Countdowns_Font_Name` /
  `Countdowns_Font_Size`, tags the exe does not know (no such string), so they are ignored and the defaults stay.
- With an empty name the loader 0x2706c0 falls back to "Arial" when the lookup 0x2610e0 finds no registered font of
  that name [M]. Weight 700. Pixel height = (int)(screen height × 96 / 600) × size / 72 [M].
- Draw object: DrawTextClass ctor 0x25b070(text, x, y, colour, font, shadow = 0, outline = 1). Outline: the white glyph
  plus a dark alpha halo dilated about 1-2 px (rasteriser 0x26d3c0 / 0x26d120). The shadow branch is not used here.
- Alignment: x is the left edge, y the top edge, both fractions of the screen (0..1 from the top-left), snapped to whole
  pixels by the bounds code 0x25b6c0 [H; top-down axis M].

### 1.3 Retreat timer 0x3ce60 [H]

- Shown when: tactical mode, the cinematic controller 0x140b27a10 has +0x34a off, and the retreat coordinator (mode
  +0x30) has a retreating player (+0x1c not −1).
- Faction: the retreating player's faction (0x340060). There is no local-player or relation filter, so **every
  player sees the timer of any retreat**, his own or an enemy's.
- Space coordinator: text `Space_Retreat_Countdown_Text_ID` (faction +0x20e0), colour
  `Space_Retreat_Countdown_Color_RGBA` (+0x20d0). Land: `Land_Retreat_Countdown_Text_ID` (+0x21b0), colour
  `Land_Retreat_Countdown_Color_RGBA` (+0x219c).
- Number: seconds left, 0x340030 = countdown frames (coordinator +0x24) / 30 while state +0x18 is 1, else 0.
- Text: the label, then " %03d.%03d": whole seconds zero-padded to 3 digits, a dot, the fraction × 1000 truncated to
  3 digits. Example: "label 010.933". No placeholder in the label.
- Position: X = `GUI_Tactical_Countdown_Timers_Screen_X` (GC +0x1228, FoC 0.005), Y = the tactical Y (FoC 0.10).
  `_Spacing` is not used (one line).
- Start value: countdown frames = (int)(30 × `Land_Retreat_Countdown_Seconds`) (faction +0x2190, land start
  0x535178) or 30 × `Space_Retreat_Countdown_Seconds` (+0x20b0, space start 0x533116). FoC: 10.99 for Rebel and
  Empire, 4.99 for Underworld, both modes; colour white; text IDs TEXT_RETREAT_LAND/SPACE_<FACTION>_COUNTDOWN.

### 1.4 Build timers 0x3d2b0 [H]

- Every planet (behaviour 6 through 0x2a9ff0), **no owner filter**; production queues 0 and 1, head entry only
  (0x401960), remaining time from 0x4ad310.
- Shown when the entry's type has `Build_Countdown_Text_ID` set (+0xc7 flag). Text: label (+0x8f8), " ", then
  "%02d" of (r + 1) / 60, ":", "%02d" of (r + 1) mod 60, with r = (int)remaining seconds. So "label 01:05".
- Colour `Build_Countdown_Text_RGBA` (+0x918). X = `GUI_Strategic_Countdown_Timers_Screen_X` (GC +0x1234); Y moves
  down by `GUI_Strategic_Countdown_Timers_Screen_Spacing` (GC +0x123c, FoC 0.05) after each timer.
- `GUI_Tactical_Countdown_Timers_Screen_Spacing` (GC +0x1230) is read only on a branch that cannot run (the function
  is reached in galactic mode only): read, no effect.

## Q2 Tactical win / lose banner 0x365150 [H]

1. Removal: every call first removes the previous banner (mode +0x220, from draw list 0x140b28f48). A null winner just
   clears it. Otherwise the banner stays until the next call or mode teardown: GameMode slot 2 (0x365300) deletes it,
   slot 1 (0x35e560) zeroes it. No timer; the battle-end dialog does not touch it. The only real caller is the victory
   declare (0x343e21); the debug key path 0x62bc9 is galactic-only and does nothing.
2. Draw arguments after the font: shadow 0, outline 1 (the countdown style). X = 0.5 − width / 2 (centred, 0x25b900),
   Y = 0.4 (top edge). Font from 0x28ab50: bold, not italic, scaled.

## Q3 Unit_Ability.SFXEvent_Special_Ability_Loop (record +0x1a0) [H]

reader + effect.

- Read through the per-object record getter 0x397d90 and passed to the object's loop-sound slot 0x3a7f70(object, on,
  event).
- 0x3a7f70 needs object +0x3a0 bit 2 clear and an event whose `Play_Count` (+0xba) is negative (−1 = loop). The
  object has one loop slot (+0x3b8 / +0x3c0). Start uses `Loop_Fade_In` (+0xbc), stop `Loop_Fade_Out` (+0xc0); a
  new event replaces the old one.
- Users: EnergyWeaponAttack (31; slot 4 0x6f9e60 starts on user and target, slot 31 0x6f9dc0 stops), TractorBeamAttack
  (30; 0x710710 / 0x710670), RadioactiveContaminate (50; 0x7128a0 start / stop at phase 3, 0x7127e0, slot 15 0x712c60
  stop), ForceSight (52; slot 30 0x713a90, slot 4 0x713ba0 start, slot 15 0x713d20 stop), SuperLaser (55; 0x715450
  start on user and target, 0x715350 stop), LeechShields (58; slot 30 0x7177a0, slot 4 0x717d60 start, slot 31
  0x717c70 stop).
- Every shipped loop event has `Play_Count` −1 through its preset.

## Q4 Retreat refusal lines and cancel

### 4.1 Codes [H]

0x340920(coordinator, player, from_local_ui) returns:

| Code | When | Line played by the HUD button |
|---|---|---|
| 0 | accepted: no retreat running (+0x1c −1), state +0x18 0, +0x7e clear; a RetreatEvent is queued (0x532570 / 0x532620, queue 0x140b2ed18) | none |
| 1 | UI call for a non-local player (or a non-UI call for the local one), player −1; mode check (slot 9): GMM game-type flags set (0x28b060, 0x28af80, space also 0x28afb0), not attacker or defender of the battle (0x28a7a0 +4 / +8), faction not playable (+0x20a8), retreat disabled for the faction (0x446480, DISABLE_RETREAT) | plain `*_Retreat_Not_Allowed_SFXEvent` |
| 2 | retreat-allowed delay still running (coordinator +0x28 > 0) | plain |
| 3 | blocked list (0x35f6d0; per-player record from mode vf +0x220, list +0x180) has an active entry of code 3, or (land) the list exists with no active entry | `_Reason_1` |
| 4 | active blocked entry of code 4 | `_Reason_2` |
| 5 | another player's retreat running, or already retreating, or +0x7e | plain |
| 6 | space only (0x282750): not blocked but the enemy owns a TSW | `_Reason_3` |

- Slot 9 is land 0x5354d0 → 0x2825e0, space 0x5334e0 → 0x282750.
- Code 3 entries are added by the INTERDICT ability (0x3a2240, space only), to the players picked by 0x294910
  [M: the enemies]. Code 4 entries by RetreatPreventionAbility (0x70d120, removed at 0x70d460).
- Lines: the HUD retreat buttons (land 0x30d5d0 / 0x30d710, space 0x30e1b0 / 0x30e2f0) play exactly one line. The plain
  `Land_/Space_Retreat_Not_Allowed_SFXEvent` (+0x2200 / +0x2130) is replaced by `_Reason_1` (+0x2210 / +0x2140) for
  code 3, `_Reason_2` (+0x2220 / +0x2150) for code 4, `_Reason_3` (+0x2230 / +0x2160) for code 6. If the chosen reason
  event is not set, nothing plays (no fall-back to the plain line).
- Land `_Reason_1` and `_Reason_3` are effectively unreachable: code 6 is space-only and land gets no interdiction
  entries [M].

### 4.2 Cancel [H]

- Second `b_retreat` click by the local retreating player: 0x30d710 → 0x3408b0 → RetreatEvent with flag 1 → 0x5325b0
  → coordinator slot 15 with by_player = 1. The local player hears `SFXEvent_Retreat_Cancel` (type +0x1260), otherwise
  the faction's `Land_Retreat_Cancel_SFXEvent` (+0x21f0) / `Space_Retreat_Cancel_SFXEvent` (+0x2120).
- Automatic: when the slot 9 check turns non-zero during the countdown, the coordinator update (land 0x535b40, space
  0x5337c0) cancels with by_player = 0, silent on land, and not in state 3.
- The land cancel 0x534f40 then clears the state, retreating id, countdown and unit list, and stops the type-5 light
  pulses on the units (Q7).
- No AI or Lua cancel path was found.

## Q5 T14 rows

### Space_Is_Guide_Rectangle (+0xc0), Land_Is_Guide_Rectangle (+0x130) [H]

The radar draw (vf 88, 0x365a80) reads them through 0x358e90 / 0x358ea0. Set: the camera view indicator is drawn as the
axis-aligned bounding rectangle of the projected view corners; clear: as the trapezoid. FoC: No / No. **reader + effect**.

### Use_Event_System (+0x51d) [M]

Default 1 (radar init 0x355650). The radar update 0x355e00 shows map event blips only while it is set, together with
global 0x140b155c4. FoC: Yes. **reader + effect**.

### Alternate_Name_Text (+0x1c0) [H]

Getter 0x43f340, callers 0x307de0 and 0x2f3e20 (ability button tooltip). Set: the tooltip name; empty: the key
"TEXT_TOOLTIP_ABILITY_" + ability enum name + "_NAME". **reader + effect**.

### Alternate_Description_Text (+0x1e0) [H]

Getter 0x43ec50, caller 0x2f3e20. Empty: "TEXT_TOOLTIP_ABILITY_" + enum + "_DESCRIPTION". **reader + effect**.

### CommandBarComponent Scale_Duration [M]

Component vf 35 (0x350df0, "activate scale up") starts a timer; the ButtonComponent update (vf 11, 0x34ff70 /
0x3503f0) sets scale = (1 − timer / Scale_Duration) × `Scale` (+0x398) until the timer ends; a warning is printed when
Scale_Duration is 0. FoC: 0.125. Trigger at 0x3098b5 [L]. **reader + effect**.

### Include_In_UI_Map_Header (+0xb2), Name_Adjust (+0x65c, vec3), TSW_Post_Music_Wait_Frames (+0x21a8), Planet_Ability_Name (+0x2250) [M]

No read of the stored field outside the parser / defaults in the provenance sweep. **retail-unused**.

### HardPoint Tooltip_Text (+0x120) [M]

The `[hp+0x20]` sweep finds the neighbouring control fields (+0x4c / +0x4d) but no read of +0x120. **retail-unused**.

## Q6 T15 rows

### GameConstants Enemy_Color / Player_Color (faction +0xf8 / +0xe8) [H]

Reader: the faction colour getter 0x33e340 (49 callers: units, radar, UI). It returns:
- the local faction's `Player_Color` (+0xe8), or for any other non-neutral (+0x108 clear) playable (+0x20a8) faction
  `Enemy_Color` (+0xf8), when there is a local player and either the local player's flag +0x3c0 is set [L: meaning
  not identified] or the **"simplified faction colours" option** is on (global 0x140a7c475, written from the
  `IDC_SIMPLIFIED_FACTION_COLORS` option at 0x0a166c in 0x0a0f30) and the caller did not pass "own colour" (argument 1);
- the faction's own colour (+0xa8) otherwise.

**reader + effect**, both rows.

### Apply_Y_Turret_Rotate_To_Axis (+0x5b0), Apply_Z_Turret_Rotate_To_Axis (+0x5ac) [H/M]

The turret code 0x631210..0x6324cd reads the neighbouring turret fields but never +0x5ac / +0x5b0; nothing else reads
them through the type. **retail-unused**, both rows.

### Particle_Attach_To_Collision (+0xb7) [H]

- Asteroid hit 0x436920: a random entry of `Shield_Hit_Particles` (+0xcb8, synced random 0x140a13e24) is attached to
  the struck object (0x38d320) when the flag is set; `Asteroid_Damage_Hit_Particles` (+0xc98) is never attached.
- Projectile detonation 0x4c73d0: `Projectile_Object_Detonation_Particle` (+0x1a28) or `_Armor_Reduced` (+0x1a38) is
  attached to the hit object when the flag is set, else left in the world.

**reader + effect**.

### Pre_Lit (+0x25) [M]

No read through the type outside the parser. **retail-unused**.

### Highlight_Blob_Material_Name (+0x348) [H]

The type fix-up 0x377500 (0x3781f9) resolves the name in the blob registry 0x140b2f170 (0x427410) into the handle at
+0x368. SelectBehavior 0x3c19c0 (service 0x3c2310) draws the ground blob (land, 0x12f3e0) or the space blob (0x13a2d0)
with that material, tinted with the owner's `Selection_Blob_RGBA`. **reader + effect**.

### Land_Terrain_Model_Mapping (+0x220, type 0x34) [H; key M]

- The type's model loader 0x375380, land branch (0x375b77..0x375c96). It runs only when `Land_Model_Name` (+0x1e0) is
  not empty.
- The key is an environment id read from the current game mode (`[mode]+8`, mode global 0x140b15418) [M: the keys
  are the environment names of enum 0x5e8770, "Temperate", "Arctic" and so on].
- The model name starts as `Land_Model_Name`. If an entry of the mapping (vector at +0x220, 0x28-byte entries:
  environment id, model name) has the current environment, its model name is used instead. The environment used is
  stored at +0x238.
- A loaded land model (+0x2a0) is kept when the mapping is empty or the stored environment matches; otherwise the
  model is reloaded for the new environment.

**reader + effect**.

### Decal Category (+0x40) [H]

The decal spawn 0x5f0d40 (TerrainDecalFX, reached only through the name lookup 0x470de0 from 0x5edab0 / 0x5ede70) copies
`Category` into a temporary string and destroys it at once; the decal's category slot (+0x220) is always set to 0.
`Render_Mode` (+0x20) is converted to the decal's render mode (+0x21c); Category has no effect. FoC: "Footprints" ×18,
"DEFAULT" ×1. **retail-unused**.

### Flare rows (LensFlares.xml; element 0x20 bytes, vector at alLensFlare +0xf0) [H]

The lens flare quad builder 0x7abdd0 (called from 0x7acc10 / 0x7acd60) makes one camera-facing quad per element.

- **Diffuse** (+0x0, vec3): quad colour RGB = Diffuse × the flare's current intensity (+0xc8) × 255. Alpha is not taken
  from it. **reader + effect**.
- **Radius** (+0xc): half-size of the quad along the camera right / up vectors. **reader + effect**.
- **Fraction** (+0x10): where the quad sits on the line from the light's screen point (0) to the screen centre (1);
  negative goes past the light, above 1 past the centre [M: the far end is the unprojected screen centre].
  **reader + effect**.
- **Slot_X** (+0x14), **Slot_Y** (+0x18), **Num_Slots** (+0x1c): the flare texture is a Num_Slots × Num_Slots grid; the quad
  uses cell (Slot_X, Slot_Y): u from Slot_X / Num_Slots to (Slot_X + 1) / Num_Slots, v likewise from Slot_Y.
  **reader + effect**, all three.

Context [M]: the flare's visibility (+0xcc) is a smoothstep of the view angle above a threshold, times the share of 4
unblocked ray tests.

### LightSource rows (LightSources.xml) [H]

The per-light parser 0x489340 fills a local block (Intensity 1, Diffuse 1,1,1, Radius 40, Falloff_Start 20,
Auto_Destruct_Time 1, Auto_Destruct_Fade_Time 1, Blob_Color 1,1,1, Blob_Intensity 1, Blob_Radius 0 by default) and copies
part of it into the alLightSource (ctor 0x14a0b0) made by the LightSources loader 0x488ec0.

- **Falloff_Start** → light +0x104. The light term 0x14af00 and the blob 0x14b570: full strength up to Falloff_Start,
  then linear to 0 at Radius: factor = min(1, (Radius − d) / (Radius − Falloff_Start)), 0 beyond Radius.
  **reader + effect**.
- **Auto_Destruct_Time** → +0x118, the remaining life. The light update 0x14ac20 (vtable slot 7): while it is above 0
  it counts down by the frame time; at 0 or below the light is removed from the scene and detached from its parent. A
  value of 0 never destructs. **reader + effect**.
- **Auto_Destruct_Fade_Time** → +0x11c. When above 0, the light colour (0x14af00, 0x14b4e0) is scaled by
  clamp(remaining / Auto_Destruct_Fade_Time, 0, 1): the light fades out over its last Fade_Time seconds. Note: a light
  with Fade_Time > 0 and Auto_Destruct_Time 0 is black. **reader + effect**.
- **Blob_Radius** → +0x128; above 0 also creates a ground blob (0x14adf0, same blob class as 0x12f3e0). 0x14b660 places
  it under the light with size Blob_Radius. Its colour is the light colour (Intensity × Diffuse × fade) × the falloff
  factor for the light's height above the ground × 0.1. The blob's blue channel copies green (red, green, green, 1),
  likely a retail bug. **reader + effect**.
- **Blob_Color** (+0x20) and **Blob_Intensity** (+0x2c): parsed into the local block but never copied to the light;
  the blob uses the light colour instead. **retail-unused**, both rows.
- FoC ships one light: LightSourceSmallGreen (green, intensity 0.4, radius 40, falloff 20, destruct 0.4 s, fade 0.4 s,
  blob radius 30).

### LightningEffect rows (LightningEffectTypes.xml) [H]

The type ctor 0x482370 sets defaults: Texture_Repeat 0, Texture_Scroll 0, Replace_Faded false. The manager entry
0x481a00 copies the type into a render template (ctor 0x7dc420): Texture_Repeat → +0x150, Texture_Scroll → +0x154,
Replace_Faded → +0x1a8.

- **Replace_Faded**: the effect update 0x7dcd40.
  - Off: every bolt is updated; once Number_Bolts bolts exist and all have faded (bolt +0x210), the effect is marked
    finished (+0x166) and removes itself if its auto-remove flag (+0x165) is set.
  - On: faded bolts are dropped from the list every update and the effect never finishes, so new bolts keep spawning
    to replace them, up to Number_Bolts at once.
  - Either way at most one bolt spawns per update, at random intervals in [Bolt_Creation_Interval_Min, _Max] (render
    random 0x140a13e20).
  - **reader + effect**.
- **Texture_Repeat**: copied to the bolt's strip (strip +0xec, 0x7b44b0). The strip vertex build (0x7b3c95) runs the
  texture along the bolt length / Texture_Repeat times, so one repeat per Texture_Repeat world units; 0 stretches the
  texture once over the bolt. **reader + effect**.
- **Texture_Scroll**: copied to strip +0xe8 (0x7b4450; the other rate +0xe4 is 0). The strip update 0x7b3740 sets the
  texture-matrix offset to frac(Texture_Scroll × t), t = seconds since the strip started, so the texture slides at
  Texture_Scroll texture-lengths per second. It is the matrix's second-row offset (the across-bolt coordinate in the
  vertex layout: u along the bolt, v 0.5 on the centre line) [L: the axis depends on the matrix convention, not traced
  into the shader]. **reader + effect**.
- FoC: Replace_Faded true on 1 type; Texture_Repeat 1 on 1 type and Texture_Scroll 50 on 1 type, 0 on the other 12.

## Q7 Ability light pulses (object light effect started by 0x39f2a0) [H]

### 7.1 Record

- The object holds one record at obj +0x1a8 (0x40 bytes, vtable 0x14087a1b8, ctor 0x4ee420): +0x8 type, +0xc colour
  (C1), +0x18 colour2 (C2), +0x24 duration D (seconds), +0x28 start time (set by 0x39f2a0), +0x2c pulse count N.
- Setters: 0x3a8bd0, 0x3a8b70, 0x3a8b40, 0x3a8ba0, 0x3a8bc0. The ability start 0x48e4d0 starts a pulse only if the
  object has no record or its record has type 0, so a running pulse is not replaced.

### 7.2 What it changes

- The apply step 0x392fe0, run from the object colour callback 0x3a0c20 (and 0x53d600), changes the model's **colour
  multiplier** (the model colour stored at model +0x13c..+0x148). It is not an added light.
- Only RGB changes. B below is the colour the multiplier would have without the pulse.
- Let e = now − start. The pulse is active while start ≠ 0 and 0 ≤ e < D; outside that the record is removed.

### 7.3 Types (enum 0x5e4a80; jump table 0x39354c)

| Value | Name | Colour while active |
|---|---|---|
| 0 | None | no change |
| 1 | Color_Flash | C1 + (B − C1) × e / D: starts at C1, linear back to B over D |
| 2 | Linear_Pulse | φ = frac(e / (D / N)); triangle f = 2φ for φ < 0.5, else 2(1 − φ); B + (C1 − B) × f |
| 3 | Smooth_Pulse | same φ; f = sin(π φ); B + (C1 − B) × f |
| 4 | Two_Way_Pulse | same φ; s = sin(2π φ); s ≥ 0 blends towards C1 by s, s < 0 towards C2 by abs(s) |
| 5 | Continuous_Smooth_Pulse | never expires; period D; f = (cos(2π frac(e / D)) + 1) / 2; C1 + (B − C1) × f (starts at B); N unused |
| 6 | (no XML name; code only, 0x5cc970, 0x626c80) | alpha = C1.r + (in.r − C1.r) × e / D; it reads the red channel, likely a bug [L] |

- So in a pulse type 2-4 each of the N pulses lasts D / N seconds and rises from B to C1 and back.
- sinf 0x776650 and cosf 0x776150 [M].
- The retreat cancel 0x534f40 stops the type-5 pulses on the retreating units (Q4).

## Corrections to earlier answers

- T13 (Q4): the refusal line is one event, chosen by code: Reason_1 / _2 / _3 **replace** the plain Not_Allowed line
  (codes 3 / 4 / 6), and an unset reason event is silent. Codes 3 and 4 are now identified (interdiction,
  RetreatPreventionAbility).
- T14 (Q1): `GUI_Tactical_Countdown_Timers_Screen_Spacing` is read by 0x3d2b0 only on an unreachable branch (read, no
  effect). The strategic spacing is the one that moves the build timers.
- T15: `Enemy_Color` / `Player_Color` are no longer open (getter 0x33e340, simplified faction colours option).

## Not reached

None. Partly inferred points still worth a probe: the Arial fall-back for the empty countdown font name (Q1, [M]), the
meaning of player +0x3c0 in 0x33e340 (Q6, [L]) and the scroll axis of Texture_Scroll (Q6, [L]).
