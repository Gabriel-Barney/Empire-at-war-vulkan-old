# mcx4 t15 RE questions: render rows of the honour gaps (Ghidra batch)

Roadmap group `mcx4` task t15 (lane mcx4-pres). Trimmed 2026-11-18 to the 139 rows that existing `docs/re/` specs, the
port's code and the FoC XML do not settle (a docs-only triage settled or annotated the rest). Answers go to
`docs/re/MCX4_T15_RE_ANSWERS.md`, one greppable section per tag or per family (`## Class.Tag`; a family section names
every tag it settles in backticks), behaviour in words with [H] / [M] / [L] and RVAs, no decompiled code. Method as in
`MCX4_T13_RE_ANSWERS.md` ("Method": displacement sweep incl. scaled-index forms, offset + 0x20 for speech / music refs,
the ctor / dtor / resolver exclusions).

For every tag: **every reader** (RVA; what it changes on screen or in the rules, when it runs, value shape and default),
or **retail-unused** with the evidence. A reader that is only debug / MP / hardware detection: say so (out of scope,
`docs/DECISIONS.md` "Out-of-scope tag-ledger rows"). Rows are grouped by class; the hint column is the triage's guess
at the reader area.

## GameConstants (40)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `CB_Movie_Color` | 0x12d0 | 0x16 | stored | 1 | colour of the HUD movie quad (0x2fc7a0) |
| `Droid_Text_Color` | 0x12b0 | 0x16 | stored | 1 | 'not used here' (droid log uses the line colour): disp32 for any other reader |
| `Earthquake_Shake_Magnitude` | 0x2e0 | 0x10 | stored | 1 | earthquake shake family |
| `Earthquake_Shake_Speed` | 0x2dc | 0x8 | stored | 1 | earthquake shake family |
| `Earthquake_Transition_Time` | 0x2d8 | 0x8 | stored | 1 | earthquake shake family |
| `Enemy_Color` | 0x344 | 0x16 | stored | 1 | UI relation colours family |
| `Fleet_Movement_Line_Texture_Name` | 0x50 | 0x17 | stored | 3 | port draws a [port] line (galactic_view.hpp:24); geometry undocumented |
| `GUI_Cycle_Color` | 0x1210 | 0x10 | stored | 1 | 0x392ba0 mode 4 'cycle colour' [M]: with GUI_Cycle_Speed |
| `Good_Ground_Color_Tint` | 0x115c | 0x16 | stored | 1 | 'presentation' only: terrain status overlay reader |
| `High_Ground_Color_Tint` | 0x117c | 0x16 | stored | 1 | 'presentation' only: terrain status overlay reader |
| `Infantry_Ground_Color_Tint` | 0x11ac | 0x16 | stored | 1 | 'presentation' only: terrain status overlay reader |
| `Land_Collidable_Grid_Cull_Size` | 0x11724 | 0x8 | stored | 1 | render cull grid: maybe tier 3 (internals) |
| `Land_Tactical_Camera_Locked` | 0x49 | 0x0 | stored | 1 | FoC True; tactical camera rotation lock? |
| `Lava_Ground_Color_Tint` | 0x119c | 0x16 | stored | 1 | 'presentation' only: terrain status overlay reader |
| `LoopWaypointLineTextureName` | 0x90 | 0x17 | stored | 1 | waypoint family (with T14 Waypoint*) |
| `Lose_Message_Color` | 0x370 | 0x16 | stored | 1 | with T14 Win_Lose_Message_Font* |
| `Nebula_Effect_Color` | 0x11828 | 0x16 | stored | 1 | nebula screen tint |
| `Neutral_UI_Color` | 0x324 | 0x16 | stored | 1 | UI relation colours family |
| `Occlusion_Silhouettes_Enabled` | 0x11e0 | 0x0 | stored | 1 | global switch of the occluded-silhouette pass; with type Occlusion_Silhouette_Enabled |
| `Player_Color` | 0x334 | 0x16 | stored | 1 | UI relation colours family |
| `Radar_Colorize_Multiplayer_Enemy` | 0x11e54 | 0x0 | stored | 1 | FoC No; likely MP-only, then OUT_OF_SCOPE |
| `Radar_Multiplayer_Enemy_Color` | 0x11e58 | 0x16 | stored | 1 | FoC No; likely MP-only, then OUT_OF_SCOPE |
| `ReinforcementOverlayBadColor` | 0x116f8 | 0x16 | stored | 1 | reinforcement placement overlay |
| `ReinforcementOverlayGoodColor` | 0x116e8 | 0x16 | stored | 1 | reinforcement placement overlay |
| `SetupPhaseInvalidDragColor` | 0x114c | 0x16 | stored | 1 | 'client side' only |
| `Slow_Ground_Color_Tint` | 0x118c | 0x16 | stored | 1 | 'presentation' only: terrain status overlay reader |
| `Space_Collidable_Grid_Cull_Size` | 0x11720 | 0x8 | stored | 1 | render cull grid: maybe tier 3 (internals) |
| `Space_Large_Ship_Grid_Cull_Size` | 0x11728 | 0x8 | stored | 1 | render cull grid: maybe tier 3 (internals) |
| `Space_Tactical_Camera_Locked` | 0x48 | 0x0 | stored | 1 | FoC True; tactical camera rotation lock? |
| `Telekinesis_Hover_Height` | 0x2c0 | 0x8 | stored | 1 | lift visual family (TELEKINESIS_TARGET behaviour 0x30) |
| `Telekinesis_Max_Bob_Height` | 0x2d4 | 0x8 | stored | 1 | lift visual family (TELEKINESIS_TARGET behaviour 0x30) |
| `Telekinesis_Max_Wobble_Angle` | 0x2d0 | 0x8 | stored | 1 | lift visual family (TELEKINESIS_TARGET behaviour 0x30) |
| `Telekinesis_Transition_Time` | 0x2c4 | 0x8 | stored | 1 | lift visual family (TELEKINESIS_TARGET behaviour 0x30) |
| `Telekinesis_Wobble_Cycle_Time` | 0x2c8 | 0x8 | stored | 1 | lift visual family (TELEKINESIS_TARGET behaviour 0x30) |
| `Telekinesis_Wobble_Fade_Time` | 0x2cc | 0x8 | stored | 1 | lift visual family (TELEKINESIS_TARGET behaviour 0x30) |
| `Terrain_Resurface_Rand` | 0x11790 | 0x6 | stored | 1 |  |
| `Terrain_Resurface_Tolerance` | 0x11794 | 0x8 | stored | 1 |  |
| `Use_Neutral_UI_Color` | 0x321 | 0x0 | stored | 1 | UI relation colours family |
| `WaypointLineTextureName` | 0xb0 | 0x17 | stored | 1 | waypoint family (with T14 Waypoint*) |
| `Win_Message_Color` | 0x360 | 0x16 | stored | 1 | with T14 Win_Lose_Message_Font* |

## GameObjectType (39)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Apply_Y_Turret_Rotate_To_Axis` | 0x5b0 | 0x6 | stored | 33 | object turret attach 0x632120 (TACTICAL_COMBAT l.689 covers hardpoints only): narrow |
| `Apply_Z_Turret_Rotate_To_Axis` | 0x5ac | 0x6 | stored | 33 | object turret attach 0x632120 (TACTICAL_COMBAT l.689 covers hardpoints only): narrow |
| `Barrel_Bone_Name` | 0x560 | 0x17 | stored | 36 | object turret attach 0x632120 (TACTICAL_COMBAT l.689 covers hardpoints only): narrow |
| `Camera_Aligned` | 0x2f | 0x0 | stored | 65 |  |
| `Cinematic_Anim_Index` | 0x1e40 | 0x6 | stored | 41 |  |
| `Death_Explosions_End` | 0xcf8 | 0x1e | stored | 20 | phase 2 of 0x58cf90, caller not traced [M] |
| `Dynamic_Transform_Delay_Seconds` | 0x2400 | 0x8 | stored | 5 | DYNAMIC_TRANSFORM behaviour 0x5A family |
| `Dynamic_Transform_End_Color` | 0x2420 | 0x11 | stored | 5 | DYNAMIC_TRANSFORM behaviour 0x5A family |
| `Dynamic_Transform_End_Scale` | 0x2408 | 0x8 | stored | 5 | DYNAMIC_TRANSFORM behaviour 0x5A family |
| `Dynamic_Transform_Seconds` | 0x240c | 0x8 | stored | 5 | DYNAMIC_TRANSFORM behaviour 0x5A family |
| `Dynamic_Transform_Start_Color` | 0x2410 | 0x11 | stored | 5 | DYNAMIC_TRANSFORM behaviour 0x5A family |
| `Dynamic_Transform_Start_Scale` | 0x2404 | 0x8 | stored | 5 | DYNAMIC_TRANSFORM behaviour 0x5A family |
| `Exclude_From_Distance_Fade` | 0x86 | 0x0 | stored | 393 | no effect in the background scene; main-scene object fade reader? |
| `GUI_Model_Name` | 0x1b18 | 0x17 | stored | 131 | GUI 3D-model family (with T14 GUI_*) |
| `Glory_Cinematics` | 0x21b8 | 0x1b | stored | 10 |  |
| `Highlight_Blob_Material_Name` | 0x348 | 0x17 | stored | 22 | Highlight_<faction>_Hero blob materials |
| `Land_Terrain_Model_Mapping` | 0x220 | 0x34 | stored | 3 |  |
| `No_Reflection_Below_Detail_Level` | 0x1810 | 0x6 | stored | 553 | likely detail-level only: confirm, then OUT_OF_SCOPE (graphic detail levels) |
| `No_Refraction_Below_Detail_Level` | 0x1814 | 0x6 | stored | 553 | likely detail-level only: confirm, then OUT_OF_SCOPE (graphic detail levels) |
| `Occlusion_Silhouette_Enabled` | 0xd9 | 0x0 | stored | 95 | per-type switch of the silhouette pass |
| `Particle_Attach_To_Collision` | 0xb7 | 0x0 | stored | 3 |  |
| `Pause_During_Cinematic_Anim` | 0x69 | 0x0 | stored | 33 | service pre-step skip [L]: confirm |
| `Pre_Lit` | 0x25 | 0x0 | stored | 68 |  |
| `Projectile_Cause_Invulnerability_Spawn_Effect` | 0x1f40 | 0x1d | stored | 1 | projectile spawn-effect family |
| `Projectile_Combat_Mod_Effect` | 0x2440 | 0x1d | stored | 1 | projectile spawn-effect family |
| `Projectile_Convert_Enemy_Spawn_Effect` | 0x1f18 | 0x1d | stored | 1 | projectile spawn-effect family |
| `Projectile_Instant_Heal_Spawn_Effect` | 0x1ed8 | 0x1d | stored | 1 | projectile spawn-effect family |
| `Projectile_Object_Creation_Particle` | 0x1a78 | 0x1d | stored | 1 | only 'unset' [M] |
| `Projectile_Weaken_Enemy_Spawn_Effect` | 0x1f68 | 0x1d | stored | 1 | projectile spawn-effect family |
| `Reinforcement_Region_Blob_Name` | 0x1e00 | 0x17 | stored | 4 |  |
| `Retreat_Self_Destruct_Explosion` | 0x1e70 | 0x1d | stored | 16 |  |
| `Shield_Sub_Mesh` | 0x23c0 | 0x17 | stored | 1 |  |
| `Terrain_Texture_Modifier_Join_Distance` | 0x1b74 | 0x8 | stored | 92 | structure footprint terrain blend |
| `Terrain_Texture_Modifier_Material` | 0x1b70 | 0x2 | stored | 92 | structure footprint terrain blend |
| `Tread_Scroll_Rate` | 0x1b6c | 0x8 | stored | 4 | TREAD_SCROLL behaviour 0x25 |
| `Wind_Disturbance_Radius` | 0x448 | 0x8 | stored | 25 | WIND_DISTURBANCE behaviour 0x0E |
| `Wind_Disturbance_Sphere_Alpha` | 0x450 | 0x8 | stored | 25 | WIND_DISTURBANCE behaviour 0x0E |
| `Wind_Disturbance_Strength` | 0x44c | 0x8 | stored | 25 | WIND_DISTURBANCE behaviour 0x0E |
| `Zoomed_Terrain_Index` | 0x1988 | 0x6 | stored | 64 |  |

## Faction (12)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Bombardment_Lighting_Color_List` | 0x27d0 | 0x14 | unread | 1 | bombardment light flashes |
| `Bombardment_Lighting_Intensity_List` | 0x27e8 | 0x13 | unread | 1 | bombardment light flashes |
| `Bombardment_Lighting_Seconds_List` | 0x2800 | 0x13 | unread | 1 | bombardment light flashes |
| `Bombardment_Shadow_Blob_Material_Name` | 0x2700 | 0x17 | unread | 3 | same targeting flow: narrow |
| `Bribed_Color` | 0x3088 | 0x16 | unread | 1 |  |
| `Land_Ability_Targeting_Range_Overlay_RGBA` | 0x2d58 | 0x16 | unread | 3 | with T14 overlay material / scale rows |
| `Land_Area_Effect_Range_Overlay_RGBA` | 0x2d20 | 0x16 | unread | 3 | with T14 overlay material / scale rows |
| `Land_Mode_Garrison_Selection_Blob_Material_Name` | 0x2868 | 0x17 | unread | 8 | garrison branch / blob colour; port reads the non-garrison material (app_hud.cpp:881) |
| `Land_Retreat_Countdown_Color_RGBA` | 0x219c | 0x16 | unread | 3 | with T14 *_Retreat_Countdown_Text_ID |
| `Selection_Blob_RGBA` | 0x28b4 | 0x16 | unread | 8 | garrison branch / blob colour; port reads the non-garrison material (app_hud.cpp:881) |
| `Space_Mode_Garrison_Selection_Blob_Material_Name` | 0x2890 | 0x17 | unread | 8 | garrison branch / blob colour; port reads the non-garrison material (app_hud.cpp:881) |
| `Space_Retreat_Countdown_Color_RGBA` | 0x20d0 | 0x16 | unread | 3 | with T14 *_Retreat_Countdown_Text_ID |

## Attack_Action (1)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Attack_Firing_Bone_Name` | 0xf0 | 0x17 | stored | 10 | Hero_Clash Attack_Action |

## Blast_Ability (2)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Charged_Effect` | 0x180 | 0x1d | stored | 1 |  |
| `Charging_Effect` | 0x190 | 0x1d | stored | 1 |  |

## Cable_Ability (6)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Cable_End_Speed_Fraction` | 0x194 | 0x8 | stored | 1 | cable draw family |
| `Cable_Fadeout_Time` | 0x19c | 0x8 | stored | 1 | cable draw family |
| `Cable_Length_Fraction` | 0x190 | 0x8 | stored | 1 | cable draw family |
| `Cable_Render_Mode` | 0x1c0 | 0x4b | stored | 1 | cable draw family |
| `Cable_Texture_Name` | 0x1a0 | 0x17 | stored | 1 | cable draw family |
| `Cable_Width` | 0x198 | 0x8 | stored | 1 | cable draw family |

## CommandBarComponent (3)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Offset_Render` | 0x3e3 | 0x50 | unread | 110 | CBC flag family (with T14) |
| `Should_Render_At_Drag_Pos` | 0x3e5 | 0x50 | unread | 20 | CBC flag family; port hard-codes 'icon follows cursor' (galactic_hud.cpp:1689); FoC writes False on 20 Reinforce buttons |
| `Swap_Texture` | 0x3e1 | 0x50 | unread | 29 | CBC flag family (with T14) |

## Decal (1)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Category` | 0x40 | 0x17 | unread | 227 | §4.2 does not say how Category is used (decal lookup?) |

## Eat_Attack_Ability (1)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Rotation_Base_Bone_Name` | 0x1d8 | 0x17 | stored | 1 |  |

## Flare (6)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Diffuse` | 0x0 | 0x10 | unread | 13 | lens-flare family (LENSFLARES.XML, 12 flares); port draws none |
| `Fraction` | 0x10 | 0x8 | unread | 12 | lens-flare family (LENSFLARES.XML, 12 flares); port draws none |
| `Num_Slots` | 0x1c | 0x6 | unread | 12 | lens-flare family (LENSFLARES.XML, 12 flares); port draws none |
| `Radius` | 0xc | 0x8 | unread | 19 | lens-flare family (LENSFLARES.XML, 12 flares); port draws none |
| `Slot_X` | 0x14 | 0x6 | unread | 12 | lens-flare family (LENSFLARES.XML, 12 flares); port draws none |
| `Slot_Y` | 0x18 | 0x6 | unread | 12 | lens-flare family (LENSFLARES.XML, 12 flares); port draws none |

## Force_Confuse_Ability (1)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Confuse_Effect` | 0x190 | 0x1d | stored | 2 |  |

## Force_Healing_Ability (10)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Owner_Light_Effect_Color` | 0xb4 | 0x10 | stored | 67 | shared ability light-pulse block (also Absorb_Blaster, Confuse, Whirlwind, Hack, Repair, Force_Lightning): one question |
| `Owner_Light_Effect_Color2` | 0xc0 | 0x10 | stored | 66 | shared ability light-pulse block (also Absorb_Blaster, Confuse, Whirlwind, Hack, Repair, Force_Lightning): one question |
| `Owner_Light_Effect_Duration` | 0xcc | 0x8 | stored | 66 | shared ability light-pulse block (also Absorb_Blaster, Confuse, Whirlwind, Hack, Repair, Force_Lightning): one question |
| `Owner_Light_Effect_Pulse_Count` | 0xd0 | 0x6 | stored | 66 | shared ability light-pulse block (also Absorb_Blaster, Confuse, Whirlwind, Hack, Repair, Force_Lightning): one question |
| `Owner_Light_Effect_Type` | 0xb0 | 0xe | stored | 66 | shared ability light-pulse block (also Absorb_Blaster, Confuse, Whirlwind, Hack, Repair, Force_Lightning): one question |
| `Target_Light_Effect_Color` | 0xd8 | 0x10 | stored | 15 | shared ability light-pulse block (also Absorb_Blaster, Confuse, Whirlwind, Hack, Repair, Force_Lightning): one question |
| `Target_Light_Effect_Color2` | 0xe4 | 0x10 | stored | 15 | shared ability light-pulse block (also Absorb_Blaster, Confuse, Whirlwind, Hack, Repair, Force_Lightning): one question |
| `Target_Light_Effect_Duration` | 0xf0 | 0x8 | stored | 15 | shared ability light-pulse block (also Absorb_Blaster, Confuse, Whirlwind, Hack, Repair, Force_Lightning): one question |
| `Target_Light_Effect_Pulse_Count` | 0xf4 | 0x6 | stored | 15 | shared ability light-pulse block (also Absorb_Blaster, Confuse, Whirlwind, Hack, Repair, Force_Lightning): one question |
| `Target_Light_Effect_Type` | 0xd4 | 0xe | stored | 15 | shared ability light-pulse block (also Absorb_Blaster, Confuse, Whirlwind, Hack, Repair, Force_Lightning): one question |

## Grenade_Attack_Ability (1)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Grenade_Spawn_Bone` | 0x1c8 | 0x17 | stored | 3 |  |

## Hack_Ability (2)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Lightning_Source_Bone_Name` | 0x1b0 | 0x17 | stored | 3 | hack / repair lightning, 'not read further' [M] |
| `Lightning_Target_Bone_Name` | 0x1d0 | 0x17 | stored | 3 | hack / repair lightning, 'not read further' [M] |

## Leech_Shields_Ability (1)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Leech_Effect` | 0x208 | 0x1d | stored | 1 |  |

## LightSource (6)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Auto_Destruct_Fade_Time` | 0x1c | 0x8 | unread | 1 | LightSources.xml family (one light, LightSourceSmallGreen); port reads no LightSources |
| `Auto_Destruct_Time` | 0x18 | 0x8 | unread | 1 | LightSources.xml family (one light, LightSourceSmallGreen); port reads no LightSources |
| `Blob_Color` | 0x20 | 0x10 | unread | 4 | LightSources.xml family (one light, LightSourceSmallGreen); port reads no LightSources |
| `Blob_Intensity` | 0x2c | 0x8 | unread | 1 | LightSources.xml family (one light, LightSourceSmallGreen); port reads no LightSources |
| `Blob_Radius` | 0x30 | 0x8 | unread | 1 | LightSources.xml family (one light, LightSourceSmallGreen); port reads no LightSources |
| `Falloff_Start` | 0x14 | 0x8 | unread | 1 | LightSources.xml family (one light, LightSourceSmallGreen); port reads no LightSources |

## LightningEffect (3)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Replace_Faded` | 0xd1 | 0x0 | unread | 1 | lightning draw (port beams.cpp reads the other LightningEffect fields) |
| `Texture_Repeat` | 0x70 | 0x8 | unread | 13 | lightning draw (port beams.cpp reads the other LightningEffect fields) |
| `Texture_Scroll` | 0x74 | 0x8 | unread | 13 | lightning draw (port beams.cpp reads the other LightningEffect fields) |

## Personal_Flame_Thrower_Ability (1)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Flame_Emitter_Model_Name` | 0x198 | 0x17 | stored | 2 |  |

## Tactical_Bribe_Ability (3)

| tag | offset | code | state | base uses | hint |
|---|---|---|---|---:|---|
| `Bribed_Effect` | 0x180 | 0x1d | stored | 1 |  |
| `Bribed_Effect_Small` | 0x190 | 0x1d | stored | 1 |  |
| `Effect_Z_Offset` | 0x1a0 | 0x8 | stored | 1 |  |

