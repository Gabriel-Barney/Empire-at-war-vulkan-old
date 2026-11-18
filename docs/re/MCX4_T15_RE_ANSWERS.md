# mcx4 t15: RE answers, render rows of the honour gaps

Answers to `MCX4_T15_RE_QUESTIONS.md` (lane mcx4-pres, roadmap group `mcx4` task t15, 139 render rows), plus the
Campaign camera sweep (STRATEGIC_GAPS §7.1). Retail `StarWarsG.exe` FoC x64 (Steam), 2026-11-18. Addresses are RVAs
(image base 0x140000000). Behaviour only, no decompiled code. **[H]** read and cross-checked, **[M]** read, one link
inferred, **[L]** inferred. Method, type codes and exclusions: `MCX4_T14_RE_ANSWERS.md` "Method" (same run).

## Summary

Counts: **102 tags have a reader, 9 are retail-unused, 4 are out of scope, 24 are still open; 12 rows are listed under
the wrong class** (10 Force_Healing_Ability rows belong to the base special ability table, 2 Hack_Ability rows to
Force_Lightning_Ability; both counted with their readers). Campaign camera: retail-unused.

| Tag(s) | Reader (RVA) or retail-unused | What it does | Conf |
|---|---|---|---|
| GC `CB_Movie_Color` | HUD movie start 0x2fc7a0 | colour of the command-bar movie quad | H |
| GC `Droid_Text_Color`, `LoopWaypointLineTextureName` | **retail-unused** | | H |
| GC `Earthquake_Transition_Time`, `_Shake_Speed`, `_Shake_Magnitude` | EarthquakeTargetBehavior 0x633330, 0x633550, 0x633730 | ground shake ramp, rate, amplitude | M |
| GC `Enemy_Color`, `Player_Color` | faction ctor 0x33a580 only (copied into every faction) | **open** downstream | M |
| GC `Fleet_Movement_Line_Texture_Name` | fleet route line 0x435c30 | galaxy fleet route texture | M |
| GC `GUI_Cycle_Color` | colour mode 4, 0x392ba0 | cycle colour | H |
| GC `Good_/High_/Infantry_/Lava_/Slow_Ground_Color_Tint` | SurfaceFXBehavior 0x5ee820 | ground status tints | M |
| GC `Land_Collidable_Grid_Cull_Size`, `Space_Collidable_Grid_Cull_Size`, `Space_Large_Ship_Grid_Cull_Size` | mode getters 0x3b7f10, 0x4d7730, 0x4d7740 | render cull grid (internals) | H |
| GC `Land_Tactical_Camera_Locked`, `Space_Tactical_Camera_Locked` | mode camera set-up 0x3b81b0 / 0x3b8cd0, 0x4d7820 / 0x4d7da0 / 0x4d80b0 → 0x2cee30 | restricted camera limits | H (M meaning) |
| GC `Win_Message_Color`, `Lose_Message_Color` | banner 0x365150 | win / lose banner colours | H |
| GC `Nebula_Effect_Color` | colour callback 0x3a0c20 | nebula tint | M |
| GC `Neutral_UI_Color`, `Use_Neutral_UI_Color` | tactical HUD 0x30fc0, 0x39420, 0x2f6c30 | UI tint override | M |
| GC `Occlusion_Silhouettes_Enabled` | 0x3a4820, 0x3a9630, 0x3be4b0 | global silhouette switch | M |
| GC `Radar_Colorize_Multiplayer_Enemy`, `Radar_Multiplayer_Enemy_Color` | radar 0x35b4d0, MP only | **out of scope** (MP) | H |
| GC `ReinforcementOverlayGoodColor`, `_BadColor` | 0x345c0 | reinforcement placement overlay | H |
| GC `SetupPhaseInvalidDragColor` | SetupPhaseManager 0x3bd840, 0x3bded0 | invalid placement colour | M |
| GC `Telekinesis_*` (6) | TelekinesisTargetBehavior 0x63f210, 0x63f470, 0x63f730 | lift, bob, wobble | H |
| GC `Terrain_Resurface_Rand`, `_Tolerance` | TerrainTextureModificationBehavior 0x624310 | footprint resurfacing | M |
| GC `WaypointLineTextureName` | waypoint line 0x3e9e0 (+ 0x539c70, 0x67a960) | line texture | H |
| Type `Barrel_Bone_Name` | TurretBehavior 0x631ea0 / 0x632120 | barrel bone | H |
| Type `Camera_Aligned` | object update 0x3989a0 → 0x29bfa0 | billboard model | M |
| Type `Cinematic_Anim_Index`, `Pause_During_Cinematic_Anim` | 0x38de00; 0x3a6b80 | cinematic animation | M |
| Type `Death_Explosions_End` | DeathBehavior 0x58cf90, 0x58d430 | end-of-death explosions | M |
| Type `Dynamic_Transform_*` (6) | DynamicTransformBehavior vf9 0x638050 | scale / colour morph | H |
| Type `Exclude_From_Distance_Fade` | 0x388b60, 0x38c730, 0x392160, 0x3a0c20, 0x3a4820 | no distance fade | M |
| Type `GUI_Model_Name` | **retail-unused** | | M |
| Type `Glory_Cinematics` | 0x2cc940, 0x4a3b40 | glory cinematic list | M |
| Type `No_Reflection_Below_Detail_Level`, `No_Refraction_Below_Detail_Level` | getters 0x375250 / 0x375260 | **out of scope** (detail levels) | H |
| Type `Occlusion_Silhouette_Enabled` | 0x3a4820, 0x3a9630, 0x3be4b0 | per-type silhouette | M |
| Type `Projectile_*_Spawn_Effect` (5), `Projectile_Object_Creation_Particle` | projectile behaviours 0x4cc010, 0x4c6950, 0x4ca2f0, 0x4cb6f0, 0x4d0800, 0x54ade0 | spawned effect on hit | M |
| Type `Reinforcement_Region_Blob_Name` | ReinforcementPointBehavior vf6 0x53d860 | region blob | M |
| Type `Retreat_Self_Destruct_Explosion` | space retreat 0x533e30, 0x364b20 | retreat explosion | M |
| Type `Shield_Sub_Mesh` | shield hit 0x549810 | shield mesh shown on hit | H |
| Type `Terrain_Texture_Modifier_Material`, `_Join_Distance` | 0x624310 | footprint terrain paint | H |
| Type `Tread_Scroll_Rate` | TreadScrollBehavior vf6 0x629760 | tread UV scroll | H |
| Type `Wind_Disturbance_*` (3) | WindDisturbanceBehavior vf6 0x6247e0 | grass wind | H |
| Type `Zoomed_Terrain_Index` | planet zoom-in 0x3e83a0 | zoom-in terrain | M |
| Type `Apply_Y/Z_Turret_Rotate_To_Axis`, `Highlight_Blob_Material_Name`, `Land_Terrain_Model_Mapping`, `Particle_Attach_To_Collision`, `Pre_Lit` | **open** | | |
| Faction `Bombardment_Lighting_Color_List`, `_Intensity_List`, `_Seconds_List` | PlanetaryBombardManager 0x52a110 | flash light per salvo | H |
| Faction `Bombardment_Shadow_Blob_Material_Name` | bombard targeting 0x2e7bc0 | target marker blob | H |
| Faction `Bribed_Color` | colour callback 0x3a0c20 | bribed unit tint | M |
| Faction `Land_Ability_Targeting_Range_Overlay_RGBA`, `Land_Area_Effect_Range_Overlay_RGBA` | 0x35280, 0x356b0 | range blob colours | H |
| Faction `Land_Retreat_Countdown_Color_RGBA`, `Space_Retreat_Countdown_Color_RGBA` | retreat countdown 0x3ce60 | countdown colour | H |
| Faction `Selection_Blob_RGBA` | SelectBehavior 0x3c19c0, 0x3c1f90 | selection blob colour | M |
| Faction `Land_Mode_Garrison_Selection_Blob_Material_Name`, `Space_Mode_` | **retail-unused** | | M |
| Attack_Action `Attack_Firing_Bone_Name` | AttackAction 0x71ef10, 0x71f050 | firing bone | M |
| Blast_Ability `Charged_Effect`, `Charging_Effect` | BlastAbility vf4 0x71c190, vf30 0x71c0b0 | charge effects | M |
| Cable_Ability 5 rows / `Cable_Fadeout_Time` | CableAttack 0x6f34d0, 0x6f3180, 0x6f2490 / **retail-unused** | cable render | M |
| CommandBarComponent `Swap_Texture`, `Offset_Render` / `Should_Render_At_Drag_Pos` | button draw 0x34e280, 0x34ff70, 0x34f160 / **retail-unused** | | M / H |
| Decal `Category`, Flare (6), LightSource (6), LightningEffect (3) | **open** | | |
| Eat_Attack_Ability `Rotation_Base_Bone_Name`, Force_Confuse_Ability `Confuse_Effect` | **retail-unused** | | M |
| Force_Healing_Ability light block (10) (really **SpecialAbility base**) | 0x48e4d0 (owner), 0x48e880 (target) | ability light pulses | H |
| Grenade_Attack_Ability `Grenade_Spawn_Bone` | GrenadeAttack 0x700a70 | grenade spawn bone | M |
| Hack_Ability `Lightning_Source_Bone_Name`, `_Target_` (really **Force_Lightning_Ability**) | ForceLightning 0x6fd3d0, 0x6fd880, 0x6fdc40 | lightning bones | M |
| Leech_Shields_Ability `Leech_Effect` | LeechShields vf30 0x7177a0 | leech effect | M |
| Personal_Flame_Thrower_Ability `Flame_Emitter_Model_Name` | 0x705590, 0x7061b0 | emitter model | M |
| Tactical_Bribe_Ability `Bribed_Effect`, `Bribed_Effect_Small`, `Effect_Z_Offset` | bribe vf30 0x5b07c0 | bribe effects | H |
| Campaign `Camera_Shift_X`, `Camera_Shift_Y`, `Camera_Distance` | **retail-unused** | | M |

## GameConstants.CB_Movie_Color

Settles `CB_Movie_Color` (+0x12d0, RGBA 255 255 255 220). [H]

- HUD command-bar movie start 0x2fc7a0 (COMMANDBAR_MOVIE, STORY_PRESENTATION_REWARDS §6): the movie quad (HUD +0xdd0)
  gets this colour (component virtual +0x28), so the talking-head movie is drawn with alpha 220 / 255.

## GameConstants.Droid_Text_Color

Settles `Droid_Text_Color` (+0x12b0). **Retail-unused** [H]: no RIP reader, no GameConstants method; the displacement
hits of 0x12b0 (ProjectileBehavior 0x4c7050 .. 0x4cb280) are another object. The droid log uses its line colour.

## GameConstants.Earthquake_Transition_Time family

Settles `Earthquake_Transition_Time` (+0x2d8, 2.0), `Earthquake_Shake_Speed` (+0x2dc, 1.0), `Earthquake_Shake_Magnitude`
(+0x2e0, vec3 0, 0, 0.7). [M]

- EarthquakeTargetBehavior (objects hit by an earthquake ability): service vf6 0x633330 and 0x633550 compute the ramp
  fraction (now − start) / `Transition_Time` (clamped 0..1) for the shake fade in / out; 0x633730 picks the next shake
  step frame as now + `Shake_Speed` / max(intensity, 0.2) and offsets the object by `Shake_Magnitude` (per axis, so FoC
  shakes only vertically) times the intensity [M for the offset formula].

## GameConstants.Enemy_Color family

Settles nothing yet for `Enemy_Color` (+0x344, 239 9 9 255) and `Player_Color` (+0x334, 50 209 63 255): **open**. [M]

- The only reader is the faction ctor 0x33a580: every faction gets `Player_Color` copied into faction +0xe8 and
  `Enemy_Color` into +0xf8. No reader of those two faction slots through player +0x68 or an immediate add on a faction
  pointer was found; a getter reaching them another way is not excluded.

## GameConstants.Fleet_Movement_Line_Texture_Name

Settles `Fleet_Movement_Line_Texture_Name` (+0x50, w_galaxy_line.tga). [M]

- 0x435c30 (from the fleet hyperspace route code 0x431c80, 0x4352f0, 0x435690): the texture of the galaxy-map line
  drawn along a moving fleet's route. Geometry not traced.

## GameConstants.GUI_Cycle_Color

Settles `GUI_Cycle_Color` (+0x1210, vec3 1, −1, −1). [H] Colour mode 4 of 0x392ba0: see `MCX4_T14_RE_ANSWERS.md`
"GameConstants.GUI_Cycle_Speed family".

## GameConstants.Good_Ground_Color_Tint family

Settles `Good_Ground_Color_Tint` (+0x115c), `High_Ground_Color_Tint` (+0x117c), `Infantry_Ground_Color_Tint`
(+0x11ac), `Lava_Ground_Color_Tint` (+0x119c), `Slow_Ground_Color_Tint` (+0x118c). [M]

- Reader: SurfaceFXBehavior 0x5ee820 (from 0x5efe50). It picks one tint per terrain status of the cell under the
  object (good, high ground, infantry-only, lava, slow) and hands it to the surface effect (two sites per tint). FoC:
  good 0 255 0. Which effect shows the tint (ground decal or overlay) is not traced.

## GameConstants.Land_Collidable_Grid_Cull_Size family

Settles `Land_Collidable_Grid_Cull_Size` (+0x11724), `Space_Collidable_Grid_Cull_Size` (+0x11720),
`Space_Large_Ship_Grid_Cull_Size` (+0x11728). [H] Getters: LandModeClass vf36 0x3b7f10, SpaceModeClass vf36 0x4d7730,
SpaceModeClass vf38 0x4d7740: the cell size of the mode's render cull grids. Internals (tier 3).

## GameConstants.Land_Tactical_Camera_Locked family

Settles `Land_Tactical_Camera_Locked` (+0x49) and `Space_Tactical_Camera_Locked` (+0x48), FoC True both. [H read,
M meaning]

- Read when the tactical mode sets up or resets its camera: land LandModeClass vf1 0x3b81b0 and vf15 0x3b8cd0; space
  SpaceModeClass vf1 0x4d7820, vf13 0x4d7da0, vf15 0x4d80b0. Each passes the flag to 0x2cee30(camera, locked, reset).
- 0x2cee30 stores the flag (camera +0x10e) and picks the camera limit set: locked → the mode's own set (land
  0x140a2a5f0, space 0x140a2a4f0, stored at camera +0x148); unlocked → a free set 0x140a2a6f0. With reset the camera
  takes the set's defaults; otherwise its distance, pitch and yaw are clamped into the set's ranges. So "locked" is
  the normal restricted tactical camera; False would give the free camera.

## GameConstants.Win_Message_Color family

Settles `Win_Message_Color` (+0x360, 223 243 255 255) and `Lose_Message_Color` (+0x370, 255 244 223 255). [H] The
tactical banner 0x365150: see `MCX4_T14_RE_ANSWERS.md` "GameConstants.Win_Lose_Message_Font".

## GameConstants.Nebula_Effect_Color

Settles `Nebula_Effect_Color` (+0x11828, 255 255 255 64). [M]

- Per-model colour callback 0x3a0c20 (all four channels): applied to objects inside a nebula (space), as the model's
  colour multiplier; the alpha 64 makes them faint. Which test marks "inside a nebula" is not traced.

## GameConstants.Neutral_UI_Color family

Settles `Use_Neutral_UI_Color` (+0x321, true) and `Neutral_UI_Color` (+0x324, 150 150 150 255). [M]

- Tactical HUD 0x30fc0 (from the frame draw 0x2ee40) and 0x39420: in tactical mode the HUD tint is the local faction's
  colour (0x33e340); when `Use_Neutral_UI_Color` is set it is replaced by `Neutral_UI_Color`. The result goes into the
  HUD tint globals 0x140a61780 / 0x140a617c8.
- 0x2f6c30 (HUD update) uses `Neutral_UI_Color` directly for one element (at HUD +0x2184) [L which one].

## GameConstants.Occlusion_Silhouettes_Enabled

Settles `Occlusion_Silhouettes_Enabled` (+0x11e0, true) together with type `Occlusion_Silhouette_Enabled` (+0xd9). [M]

- Read in 0x3a4820, 0x3a9630 and 0x3be4b0, each next to a read of the object type's `Occlusion_Silhouette_Enabled`:
  the occluded-silhouette pass is set up for an object only when both are on. 0x3a4820 is the object render set-up
  (from 0x3989a0, 0x3a0930), 0x3a9630 runs on owner change (0x38e5a0), 0x3be4b0 from 0x3c0020.

## GameConstants.Radar_Colorize_Multiplayer_Enemy family

Settles `Radar_Colorize_Multiplayer_Enemy` (+0x11e54) and `Radar_Multiplayer_Enemy_Color` (+0x11e58). **Out of scope
(multiplayer).** [H]

- Radar blip colour 0x35b4d0: only when the game is multiplayer (0x28af60) and the flag is set, an enemy blip takes
  this colour instead of its owner colour. Single player never reads it.

## GameConstants.ReinforcementOverlayGoodColor family

Settles `ReinforcementOverlayGoodColor` (+0x116e8, 0 128 0 128) and `ReinforcementOverlayBadColor` (+0x116f8, 128 0 0
128). [H]

- 0x345c0 (from the frame draw 0x2ee40): the reinforcement placement overlay; valid cells / regions use the good
  colour, invalid the bad one.

## GameConstants.SetupPhaseInvalidDragColor

Settles `SetupPhaseInvalidDragColor` (+0x114c, 200 0 0 0). [M]

- SetupPhaseManagerClass (0x3bd840, 0x3bded0, from 0x3bd7b0 / 0x3beb30 / 0x3bec30): the colour of a unit dragged to an
  invalid spot during the pre-battle setup phase.

## GameConstants.Telekinesis_Hover_Height family

Settles `Telekinesis_Hover_Height` (+0x2c0, 22.0), `Telekinesis_Transition_Time` (+0x2c4), `Telekinesis_Wobble_Cycle_Time`
(+0x2c8), `Telekinesis_Wobble_Fade_Time` (+0x2cc), `Telekinesis_Max_Wobble_Angle` (+0x2d0), `Telekinesis_Max_Bob_Height`
(+0x2d4). [H reads, M curve]

- TelekinesisTargetBehavior (behaviour 0x30, the lifted unit): service vf6 0x63f210 and 0x63f470 ramp the lift over
  `Transition_Time` (fraction = elapsed / time) up to `Hover_Height`; 0x63f730 adds the wobble: a tilt up to
  `Max_Wobble_Angle` and a bob up to `Max_Bob_Height`, cycling every `Wobble_Cycle_Time` and faded in over
  `Wobble_Fade_Time`.

## GameConstants.Terrain_Resurface_Rand family

Settles `Terrain_Resurface_Rand` (+0x11790, 2) and `Terrain_Resurface_Tolerance` (+0x11794, 0.03). [H read, L use]

- TerrainTextureModificationBehavior vf10 0x624310 (with type `Terrain_Texture_Modifier_*`, two sites each) when a
  structure's footprint repaints the terrain. By the names: a blend tolerance and a random spread of the repainted edge
  [L]; the formula was not read.

## GameConstants.WaypointLineTextureName family

Settles `WaypointLineTextureName` (+0xb0, w_generic_white.tga) and `LoopWaypointLineTextureName` (+0x90). [H]

- 0x3e9e0 (waypoint line build, from 0x3e240): the tactical waypoint line texture. 0x539c70 and 0x67a960 also use it
  for their lines [M which lines].
- `LoopWaypointLineTextureName`: **retail-unused** (no reader).

## GameObjectType.Barrel_Bone_Name

Settles `Barrel_Bone_Name` (+0x560). [H read, M effect]

- TurretBehavior vf9 0x631ea0 and vf10 0x632120 (turret attach): the bone indices of `Turret_Bone_Name` (+0x540) and
  `Barrel_Bone_Name` are looked up on the model; when the turret bone exists its index goes to the turret data +0x40
  and, when the barrel bone exists, the barrel's to +0x44; both bones are switched to program control (0x12d430). So
  the turret yaws the turret bone and pitches the barrel bone.

## GameObjectType.Camera_Aligned

Settles `Camera_Aligned` (+0x2f). [M] Object update 0x3989a0: when set, 0x29bfa0 turns the object's model to face the
camera (billboard) each update.

## GameObjectType.Cinematic_Anim_Index family

Settles `Cinematic_Anim_Index` (+0x1e40) and `Pause_During_Cinematic_Anim` (+0x69). [M]

- `Cinematic_Anim_Index`: CinematicAnimationEventClass 0x38de00 (from 0x6899f0) plays that animation index of the
  object's model for a cinematic animation event.
- `Pause_During_Cinematic_Anim`: 0x3a6b80 (from 0x2be640): objects of such types are paused while a cinematic
  animation runs.

## GameObjectType.Death_Explosions_End

Settles `Death_Explosions_End` (+0xcf8, list). [M] DeathBehavior 0x58cf90 (phase 2 of the death, T13) and service vf6
0x58d430 (tests the list count): the explosions spawned at the end of the death sequence.

## GameObjectType.Dynamic_Transform_Seconds family

Settles `Dynamic_Transform_Delay_Seconds` (+0x2400), `Dynamic_Transform_Start_Scale` (+0x2404),
`Dynamic_Transform_End_Scale` (+0x2408), `Dynamic_Transform_Seconds` (+0x240c), `Dynamic_Transform_Start_Color`
(+0x2410), `Dynamic_Transform_End_Color` (+0x2420). [H]

- DynamicTransformBehaviorClass vf9 0x638050 (behaviour 0x5a) copies them into the behaviour at start: the morph begins
  after FPS × `Delay_Seconds`, runs `Seconds`, scale from start to end and colour from start to end (RGBA).

## GameObjectType.Exclude_From_Distance_Fade

Settles `Exclude_From_Distance_Fade` (+0x86). [M] Read in 0x388b60, 0x38c730, 0x392160, the colour callback 0x3a0c20
and the render set-up 0x3a4820: objects of such types are not faded out with camera distance.

## GameObjectType.GUI_Model_Name

Settles `GUI_Model_Name` (+0x1b18). **Retail-unused** [M]: the only hits besides the type ctor / copy / dtor are
CommandBarClass 0x2f9270 fields (same offset, command bar base). See T14 `GUI_Angles` family.

## GameObjectType.Glory_Cinematics

Settles `Glory_Cinematics` (+0x21b8, list). [M] 0x2cc940 (from 0x39bdb0) and 0x4a3b40 (from 0x2cc940, 0x4a53f0) read the
list (count, entries): the glory (kill) cinematic chosen for the unit.

## GameObjectType.No_Reflection_Below_Detail_Level family

Settles `No_Reflection_Below_Detail_Level` (+0x1810) and `No_Refraction_Below_Detail_Level` (+0x1814). **Out of scope
(graphic detail levels).** [H] Getters 0x375250 / 0x375260 only; by their names they gate the object in the water
reflection / refraction pass by detail level [L].

## GameObjectType.Projectile_Instant_Heal_Spawn_Effect family

Settles `Projectile_Cause_Invulnerability_Spawn_Effect` (+0x1f40), `Projectile_Combat_Mod_Effect` (+0x2440),
`Projectile_Convert_Enemy_Spawn_Effect` (+0x1f18), `Projectile_Instant_Heal_Spawn_Effect` (+0x1ed8),
`Projectile_Weaken_Enemy_Spawn_Effect` (+0x1f68), `Projectile_Object_Creation_Particle` (+0x1a78). [M]

- Type references (code 0x1d): readers use the resolved type at offset + 8. Projectile behaviour handlers spawn the
  effect object at the hit: invulnerability 0x4cc010 (and 0x35db50), combat modifier 0x4c6950, convert enemy 0x4ca2f0,
  instant heal 0x4cb6f0, weaken enemy 0x4d0800 (and 0x291330), object creation particle 0x54ade0.

## GameObjectType.Reinforcement_Region_Blob_Name

Settles `Reinforcement_Region_Blob_Name` (+0x1e00). [M] ReinforcementPointBehavior vf6 0x53d860: the blob material
drawn on the ground for the reinforcement point's region.

## GameObjectType.Retreat_Self_Destruct_Explosion

Settles `Retreat_Self_Destruct_Explosion` (+0x1e70, type reference). [M] Space retreat 0x533e30 (from the coordinator
update 0x5337c0) and 0x364b20: the explosion spawned for a retreating unit that is destroyed instead of jumping out
[L for when].

## GameObjectType.Shield_Sub_Mesh

Settles `Shield_Sub_Mesh` (+0x23c0). [H] Shield hit 0x549810 (from 0x549ac0): when shields absorb a hit, the named
sub-mesh of the model is shown (empty name: skipped); the flash itself is GC `Shield_Flash_*` (T14).

## GameObjectType.Terrain_Texture_Modifier_Material family

Settles `Terrain_Texture_Modifier_Material` (+0x1b70) and `Terrain_Texture_Modifier_Join_Distance` (+0x1b74). [H read,
M meaning]
TerrainTextureModificationBehavior vf10 0x624310: the terrain material painted under the structure's footprint, and
the distance within which neighbouring footprints join into one patch.

## GameObjectType.Tread_Scroll_Rate

Settles `Tread_Scroll_Rate` (+0x1b6c). [H read, M unit] TreadScrollBehavior vf6 0x629760 (behaviour 0x25): the tread
texture scroll rate while the unit moves.

## GameObjectType.Wind_Disturbance_Radius family

Settles `Wind_Disturbance_Radius` (+0x448), `Wind_Disturbance_Strength` (+0x44c), `Wind_Disturbance_Sphere_Alpha`
(+0x450). [H read, M effect] WindDisturbanceBehavior vf6 0x6247e0 (behaviour 0x0e) hands all three to the wind
disturbance (grass / foliage bending) around the unit.

## GameObjectType.Zoomed_Terrain_Index

Settles `Zoomed_Terrain_Index` (+0x1988). [M] Planet zoom-in 0x3e83a0 (from the HUD update 0x30c3b0 and 0x3ce970): the
terrain index used for the zoomed-in planet view.

## Faction.Bombardment_Lighting_Color_List family

Settles `Bombardment_Lighting_Color_List` (+0x27d0, RGB list), `Bombardment_Lighting_Intensity_List` (+0x27e8, int
list), `Bombardment_Lighting_Seconds_List` (+0x2800, float list). [H]

- PlanetaryBombardManager 0x52a110 (T13 bombard): for each shot of a salvo, entry i (i = the shot's salvo index) of the
  bombarding faction's lists gives a light flash at the impact point: colour i, intensity i, life FPS × seconds i. No
  flash when the seconds entry is missing or not above 0; missing colour / intensity entries give 0.

## Faction.Bombardment_Shadow_Blob_Material_Name

Settles `Bombardment_Shadow_Blob_Material_Name` (+0x2700). [H] Bombard targeting 0x2e7bc0: while the local player aims
a bombardment (HUD targeting mode 1), a ground blob with the local faction's material marks the target.

## Faction.Bribed_Color

Settles `Bribed_Color` (+0x3088). [M] Per-model colour callback 0x3a0c20 (immediate add on a faction pointer): a
bribed unit is tinted with the faction's `Bribed_Color` [L that the trigger is the bribed flag +0x3cc set by
TacticalBribe 0x5b07c0].

## Faction.Land_Ability_Targeting_Range_Overlay_RGBA family

Settles `Land_Ability_Targeting_Range_Overlay_RGBA` (+0x2d58) and `Land_Area_Effect_Range_Overlay_RGBA` (+0x2d20). [H]
The colours of the two ability range blobs (0x35280 and 0x356b0); see T14 "Faction.Land_Ability_Targeting_Range_Overlay_Material_Name family".

## Faction.Land_Retreat_Countdown_Color_RGBA family

Settles `Land_Retreat_Countdown_Color_RGBA` (+0x219c) and `Space_Retreat_Countdown_Color_RGBA` (+0x20d0). [H] The
retreat countdown 0x3ce60 draws the local faction's label in this colour; see T14
"Faction.Land_Retreat_Countdown_Text_ID family".

## Faction.Selection_Blob_RGBA

Settles `Selection_Blob_RGBA` (+0x28b4). [M] SelectBehavior 0x3c19c0 (from vf6 0x3c2310) and 0x3c1f90 (from 0x3a6b30),
both feeding the selection blob draw 0x3c2710: the colour of the selection blob under selected units.

## Faction.Land_Mode_Garrison_Selection_Blob_Material_Name family

Settles `Land_Mode_Garrison_Selection_Blob_Material_Name` (+0x2868) and `Space_Mode_Garrison_Selection_Blob_Material_Name`
(+0x2890). **Retail-unused** [M]: no read with a faction base and no immediate add on a faction pointer; the hits at
these offsets are HUD fields.

## Attack_Action.Attack_Firing_Bone_Name

Settles `Attack_Firing_Bone_Name` (+0xf0). [M] AttackActionClass vf7 0x71ef10 and 0x71f050 (the hero clash attack
action, T13 `Attack_SFXEvent`): the bone the attack's projectile is fired from.

## Blast_Ability.Charged_Effect family

Settles `Charged_Effect` (+0x180) and `Charging_Effect` (+0x190), type references. [M] Readers use offset + 8:
`Charged_Effect` (+0x188) in BlastAbility vf4 0x71c190, `Charging_Effect` (+0x198) in vf30 0x71c0b0: the effect while
the blast charges, and the one when it is charged.

## Cable_Ability.Cable_Width family

Settles `Cable_Width` (+0x198), `Cable_Texture_Name` (+0x1a0), `Cable_Render_Mode` (+0x1c0), `Cable_Length_Fraction`
(+0x190), `Cable_End_Speed_Fraction` (+0x194), `Cable_Fadeout_Time` (+0x19c). Class: CableAttackAbilityClass. [M]

- Activation vf4 0x6f2d50 → 0x6f34d0 creates the cable render object: width = `Cable_Width`, texture =
  `Cable_Texture_Name`, render mode = `Cable_Render_Mode` (render object +0x148 / +0x150).
- `Cable_Length_Fraction`: 0x6f3180 (cable length as a fraction of the distance). `Cable_End_Speed_Fraction`:
  0x6f2490 (the cable end's speed as a fraction of the target's).
- `Cable_Fadeout_Time`: **retail-unused** (no reader in the class).

## CommandBarComponent.Swap_Texture family

Settles `Swap_Texture` (bit 0x3e1 / 0x01), `Offset_Render` (0x3e3 / 0x04), `Should_Render_At_Drag_Pos` (0x3e5 / 0x80).
[H bits, M roles] Bit layout and setters: `MCX4_T14_RE_ANSWERS.md` "CommandBarComponent.Stackable family".

- `Swap_Texture`: ButtonComponent vf13 0x34e280 and vf11 0x34ff70. `Offset_Render`: ButtonComponent vf13 0x34e280 and
  TextButton vf13 0x34f160.
- `Should_Render_At_Drag_Pos`: **retail-unused**: no test of bit 0x80 of +0x3e5 outside its setter 0x5136c0 (the other
  bits of that byte are read).

## Eat_Attack_Ability.Rotation_Base_Bone_Name

Settles `Rotation_Base_Bone_Name` (+0x1d8). **Retail-unused** [M]: EatAttackAbilityClass ctor / dtor only.

## Force_Confuse_Ability.Confuse_Effect

Settles `Confuse_Effect` (+0x190, type reference). **Retail-unused** [M]: the ctor touches +0x190; nothing in
ForceConfuseAbilityClass reads the resolved pointer +0x198.

## Force_Healing_Ability.Owner_Light_Effect_Type family (ability light pulses)

Settles `Owner_Light_Effect_Type` (+0xb0), `Owner_Light_Effect_Color` (+0xb4), `Owner_Light_Effect_Color2` (+0xc0),
`Owner_Light_Effect_Duration` (+0xcc), `Owner_Light_Effect_Pulse_Count` (+0xd0), `Target_Light_Effect_Type` (+0xd4),
`Target_Light_Effect_Color` (+0xd8), `Target_Light_Effect_Color2` (+0xe4), `Target_Light_Effect_Duration` (+0xf0),
`Target_Light_Effect_Pulse_Count` (+0xf4). **Misfiled:** the real owner is the base special ability table 0xa3d690
(first tag `Initially_Enabled`), read by SpecialAbilityClass for every ability. [H]

- Owner: 0x48e4d0(ability, owner, target), called at activation by many abilities (RemoveCorruption, GalacticSabotage,
  HackSuperWeapon, Infection, FindWeakness, ...). When the owner type is not 0 and the owner has no running light
  effect (object +0x1a8 empty), it starts one: type, colour, second colour, duration, pulse count (0x3a8bd0 /
  0x3a8b70 / 0x3a8b40 / 0x3a8ba0 / 0x3a8bc0, then 0x39f2a0). It also plays the ability's own effects and SFX.
- Target: 0x48e880(ability, target), from 0x48e4d0, 0x48cca0, ConcentrateFire 0x6f5550, Summon 0x71cff0: the same with
  the target block.
- The object light effect is the one the shield flash uses (T14 `Shield_Flash_*`): a colour pulse on the model,
  between the two colours, `Pulse_Count` times over `Duration` [M for the pulse shape].

## Grenade_Attack_Ability.Grenade_Spawn_Bone

Settles `Grenade_Spawn_Bone` (+0x1c8). [M] GrenadeAttackAbilityClass 0x700a70: the bone the grenade is spawned from
(0x518de0 is the parser setter region, not a reader).

## Hack_Ability.Lightning_Source_Bone_Name family

Settles `Lightning_Source_Bone_Name` (+0x1b0) and `Lightning_Target_Bone_Name` (+0x1d0). **Misfiled:** these offsets
are in table 0xa3ea70 (`Damage_Amount`, `Start_Lightning_Frame_Number`, `Lightning_Effect_Name` +0x190, ...), which
FoC uses as **Force_Lightning_Ability** (NAMEDHEROUNITS.XML, B_HAND_L). Hack_Ability's own bone names are at +0x1a0 /
+0x1c0 (table 0xa3ec70 / 0xa3ed70) and are read by HackAbility vf30 0x7013d0. [M]

- Readers: ForceLightningAbilityClass vf13 0x6fd3d0, vf4 0x6fd880 and 0x6fdc40: the lightning runs from the source bone
  of the caster to the target bone of each target.

## Leech_Shields_Ability.Leech_Effect

Settles `Leech_Effect` (+0x208, type reference). [M] LeechShields vf30 0x7177a0 reads the resolved pointer (+0x210):
the effect spawned when the leech starts.

## Personal_Flame_Thrower_Ability.Flame_Emitter_Model_Name

Settles `Flame_Emitter_Model_Name` (+0x198). [M] PersonalFlameThrowerAbilityClass 0x705590 and 0x7061b0: the emitter
model attached to the unit while the flame thrower fires.

## Tactical_Bribe_Ability.Bribed_Effect family

Settles `Bribed_Effect` (+0x180), `Bribed_Effect_Small` (+0x190), `Effect_Z_Offset` (+0x1a0). [H]

- TacticalBribe vf30 0x5b07c0, on a successful bribe (after the credit charge and `SFXEvent_Success`, T13):
  - `Bribed_Effect` (resolved +0x188) is spawned for the briber's owner above the target: at the first bone of a
    global bone-name list found on the target's model, else at the target's position plus the model height; plus
    `Effect_Z_Offset` in both cases.
  - `Bribed_Effect_Small` (resolved +0x198) is handed to the target (0x38d580), and the target's bribed flag
    (+0x3cc) is set (it drives `Bribed_Color`).

## Still open (24)

- GameConstants `Enemy_Color`, `Player_Color` (section above).
- GameObjectType `Apply_Y_Turret_Rotate_To_Axis` (+0x5b0), `Apply_Z_Turret_Rotate_To_Axis` (+0x5ac),
  `Particle_Attach_To_Collision` (+0xb7), `Pre_Lit` (+0x25): small offsets, every hit found is another object.
  `Highlight_Blob_Material_Name` (+0x348): the hits (0x27f9a0, 0x3ea1b0, IonCannon, LuckyShot) were not confirmed as type
  reads [L]. `Land_Terrain_Model_Mapping` (+0x220, type 0x34): not traced.
- Decal `Category` (+0x40), Flare (6 rows, alLensFlare), LightSource (6 rows, alLightSource), LightningEffect
  `Replace_Faded`, `Texture_Repeat`, `Texture_Scroll`: render-library classes (al*), small offsets; their methods were not
  separated in this run.

## Campaign.Camera_Distance family (STRATEGIC_GAPS §7.1)

Settles `Camera_Shift_X` (+0xcc), `Camera_Shift_Y` (+0xd0), `Camera_Distance` (+0xd4). **Retail-unused.** [M]

- Touches: the campaign ctor 0x4ae110 (defaults shift X 40, shift Y 0, distance 1200), the reset 0x4ae7a0 (0, 0, 1200),
  the copy 0x478430, and the XML parser. A disp32 / disp8 sweep of +0xcc..+0xd4 finds no other access with a campaign
  base; the galactic camera code (0x3c2000 .. 0x3c5000, galactic mode 0x4b21f0 / 0x4b23c0, HUD 0x2ea110, map set-up
  0x3d3200) touches these offsets only on other objects (e.g. the selection blob object +0xcc in 0x3c2710).
- **Correction to STRATEGIC_GAPS §7.1 line 435:** FoC data does set them (`<Camera_Distance>` appears in CAMPAIGNS_ALPHA,
  _DEMOS, _DEMOS_02, _MAIN, _MULTIPLAYER, _P1 .. _P5 and more); the values are parsed and never used.
