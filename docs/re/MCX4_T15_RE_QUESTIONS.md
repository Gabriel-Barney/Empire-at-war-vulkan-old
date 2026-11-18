# mcx4 t15 RE questions: render rows of the honour gaps

Roadmap group `mcx4` task t15 (lane mcx4-pres); rows: `python -I tools/lint/check_tag_ledger.py --gaps --group render`
(184 rows, 2026-11-18). Answers go to `docs/re/MCX4_T15_RE_ANSWERS.md`, one greppable section per tag or
per family (a family section names every tag it settles in backticks), behaviour in words with [H] / [M] / [L] and
RVAs, no decompiled code. The method of `MCX4_RE_ANSWERS_GALACTIC.md` ("Method") applies.

For every tag: **every reader** (RVA; what it changes on screen or in the rules, when it runs, value shape and default),
or **retail-unused** with the cross-reference evidence (disp32 search of the field offset finding only ctor / dtor /
copy; the ledger then cites the section). A reader that is only debug / MP / hardware-detection is out of scope:
say so (`docs/DECISIONS.md` "Out-of-scope tag-ledger rows"). Group answers by reader function.

Port anchors: `engine/render` (map scene, particles, decals, lights), `engine/terrain`, `engine/materials`, the tactical camera `engine/ui/src/camera.cpp`. Detail-level-only readers (graphic detail settings) are out of scope; a reader that changes what is drawn at the port's settings is in scope.

Offsets are of the row's class (`xml_field_tables.md`; GameConstants offsets are GC offsets, object type offsets
GameObjectType offsets). Rows marked `stored` are kept by the port but nothing reads them.

## GameConstants (58)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `HardPoint_Target_Reticle_Enemy_Texture` | 0x700 | 0x41 | stored | 13 | docs/re/xml_tags.md |
| `HardPoint_Target_Reticle_Enemy_Tracked_Texture` | 0x838 | 0x41 | stored | 13 | docs/re/xml_tags.md |
| `HardPoint_Target_Reticle_Friendly_Disabled_Texture` | 0xd18 | 0x41 | stored | 13 | docs/re/xml_tags.md |
| `HardPoint_Target_Reticle_Friendly_Disabled_Tracked_Texture` | 0xe50 | 0x41 | stored | 13 | docs/re/xml_tags.md |
| `HardPoint_Target_Reticle_Friendly_Repairing_Texture` | 0xbe0 | 0x41 | stored | 13 | docs/re/xml_tags.md |
| `HardPoint_Target_Reticle_Friendly_Texture` | 0x970 | 0x41 | stored | 13 | docs/re/xml_tags.md |
| `HardPoint_Target_Reticle_Friendly_Tracked_Texture` | 0xaa8 | 0x41 | stored | 13 | docs/re/xml_tags.md |
| `Fleet_Movement_Line_Texture_Name` | 0x50 | 0x17 | stored | 3 | docs/re/xml_tags.md |
| `Battle_Pending_Message_Color` | 0x534 | 0x16 | stored | 1 | docs/re/GALACTIC_DIALOGS.md |
| `CB_Movie_Color` | 0x12d0 | 0x16 | stored | 1 | docs/re/xml_tags.md |
| `Droid_Date_Color` | 0x12a0 | 0x16 | stored | 1 | docs/re/P4_SIM_SYSTEMS.md |
| `Droid_Seperator_Color` | 0x12c0 | 0x16 | stored | 1 | docs/re/P4_SIM_SYSTEMS.md |
| `Droid_Text_Color` | 0x12b0 | 0x16 | stored | 1 | docs/re/P4_SIM_SYSTEMS.md |
| `Earthquake_Shake_Magnitude` | 0x2e0 | 0x10 | stored | 1 | docs/re/xml_tags.md |
| `Earthquake_Shake_Speed` | 0x2dc | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `Earthquake_Transition_Time` | 0x2d8 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `Enemy_Color` | 0x344 | 0x16 | stored | 1 | docs/re/xml_tags.md |
| `GUI_Cycle_Color` | 0x1210 | 0x10 | stored | 1 | docs/re/xml_tags.md |
| `Good_Ground_Color_Tint` | 0x115c | 0x16 | stored | 1 | docs/re/xml_tags.md |
| `High_Ground_Color_Tint` | 0x117c | 0x16 | stored | 1 | docs/re/xml_tags.md |
| `Hint_Text_Color` | 0x1250 | 0x16 | stored | 1 | docs/re/xml_tags.md |
| `Infantry_Ground_Color_Tint` | 0x11ac | 0x16 | stored | 1 | - |
| `Land_Collidable_Grid_Cull_Size` | 0x11724 | 0x8 | stored | 1 | - |
| `Land_Tactical_Camera_Locked` | 0x49 | 0x0 | stored | 1 | - |
| `Lava_Ground_Color_Tint` | 0x119c | 0x16 | stored | 1 | - |
| `LoopWaypointLineTextureName` | 0x90 | 0x17 | stored | 1 | - |
| `Lose_Message_Color` | 0x370 | 0x16 | stored | 1 | - |
| `Nebula_Effect_Color` | 0x11828 | 0x16 | stored | 1 | - |
| `Neutral_UI_Color` | 0x324 | 0x16 | stored | 1 | - |
| `Occlusion_Silhouettes_Enabled` | 0x11e0 | 0x0 | stored | 1 | - |
| `Player_Color` | 0x334 | 0x16 | stored | 1 | - |
| `Radar_Colorize_Multiplayer_Enemy` | 0x11e54 | 0x0 | stored | 1 | - |
| `Radar_Colorize_Selected_Units` | 0x11e40 | 0x0 | stored | 1 | docs/re/UI.md |
| `Radar_Multiplayer_Enemy_Color` | 0x11e58 | 0x16 | stored | 1 | - |
| `Radar_Selected_Units_Color` | 0x11e44 | 0x16 | stored | 1 | - |
| `ReinforcementOverlayBadColor` | 0x116f8 | 0x16 | stored | 1 | - |
| `ReinforcementOverlayGoodColor` | 0x116e8 | 0x16 | stored | 1 | - |
| `SetupPhaseInvalidDragColor` | 0x114c | 0x16 | stored | 1 | docs/re/FOG_OF_WAR.md |
| `Slow_Ground_Color_Tint` | 0x118c | 0x16 | stored | 1 | - |
| `SpaceFOWHeight` | 0x11bc | 0x8 | stored | 1 | docs/re/DECORATIONS.md |
| `Space_Collidable_Grid_Cull_Size` | 0x11720 | 0x8 | stored | 1 | - |
| `Space_Large_Ship_Grid_Cull_Size` | 0x11728 | 0x8 | stored | 1 | - |
| `Space_Tactical_Camera_Locked` | 0x48 | 0x0 | stored | 1 | - |
| `System_Text_Color` | 0x1260 | 0x16 | stored | 1 | - |
| `Task_Text_Color` | 0x1270 | 0x16 | stored | 1 | - |
| `Telekinesis_Hover_Height` | 0x2c0 | 0x8 | stored | 1 | - |
| `Telekinesis_Max_Bob_Height` | 0x2d4 | 0x8 | stored | 1 | - |
| `Telekinesis_Max_Wobble_Angle` | 0x2d0 | 0x8 | stored | 1 | - |
| `Telekinesis_Transition_Time` | 0x2c4 | 0x8 | stored | 1 | - |
| `Telekinesis_Wobble_Cycle_Time` | 0x2c8 | 0x8 | stored | 1 | - |
| `Telekinesis_Wobble_Fade_Time` | 0x2cc | 0x8 | stored | 1 | - |
| `Terrain_Resurface_Rand` | 0x11790 | 0x6 | stored | 1 | - |
| `Terrain_Resurface_Tolerance` | 0x11794 | 0x8 | stored | 1 | - |
| `Use_Neutral_UI_Color` | 0x321 | 0x0 | stored | 1 | - |
| `Water_Clip_Plane_Offset` | 0x11dc | 0x8 | stored | 1 | docs/re/TERRAIN_WATER_POST.md |
| `Water_Render_Target_Resolution` | 0x11d8 | 0x5 | stored | 1 | docs/re/TERRAIN_WATER_POST.md |
| `WaypointLineTextureName` | 0xb0 | 0x17 | stored | 1 | - |
| `Win_Message_Color` | 0x360 | 0x16 | stored | 1 | - |

## GameObjectType (48)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `No_Reflection_Below_Detail_Level` | 0x1810 | 0x6 | stored | 553 | - |
| `No_Refraction_Below_Detail_Level` | 0x1814 | 0x6 | stored | 553 | - |
| `Exclude_From_Distance_Fade` | 0x86 | 0x0 | stored | 393 | docs/re/SPACE_AND_OPEN.md |
| `GUI_Model_Name` | 0x1b18 | 0x17 | stored | 131 | docs/re/FALLBACK_CAMPAIGNS.md |
| `Occlusion_Silhouette_Enabled` | 0xd9 | 0x0 | stored | 95 | - |
| `Terrain_Texture_Modifier_Join_Distance` | 0x1b74 | 0x8 | stored | 92 | - |
| `Terrain_Texture_Modifier_Material` | 0x1b70 | 0x2 | stored | 92 | - |
| `Pre_Lit` | 0x25 | 0x0 | stored | 68 | - |
| `Camera_Aligned` | 0x2f | 0x0 | stored | 65 | docs/re/STRATEGIC_GAPS.md |
| `Zoomed_Terrain_Index` | 0x1988 | 0x6 | stored | 64 | - |
| `Cinematic_Anim_Index` | 0x1e40 | 0x6 | stored | 41 | docs/re/xml_tags.md |
| `Turret_Bone_Name` | 0x540 | 0x17 | stored | 41 | docs/re/LOB_SOLVER.md |
| `Barrel_Bone_Name` | 0x560 | 0x17 | stored | 36 | docs/re/TACTICAL_COMBAT.md |
| `Cinematic_Anim_Blend_Seconds` | 0x1e3c | 0x8 | stored | 34 | docs/re/ANIM_PARTICLES.md |
| `Cinematic_Anim_Speed` | 0x1e38 | 0x8 | stored | 34 | docs/re/ANIM_PARTICLES.md |
| `Apply_Y_Turret_Rotate_To_Axis` | 0x5b0 | 0x6 | stored | 33 | docs/re/xml_tags.md |
| `Apply_Z_Turret_Rotate_To_Axis` | 0x5ac | 0x6 | stored | 33 | docs/re/xml_tags.md |
| `Pause_During_Cinematic_Anim` | 0x69 | 0x0 | stored | 33 | - |
| `Wind_Disturbance_Radius` | 0x448 | 0x8 | stored | 25 | - |
| `Wind_Disturbance_Sphere_Alpha` | 0x450 | 0x8 | stored | 25 | - |
| `Wind_Disturbance_Strength` | 0x44c | 0x8 | stored | 25 | - |
| `Highlight_Blob_Material_Name` | 0x348 | 0x17 | stored | 22 | docs/re/xml_tags.md |
| `Death_Explosions_End` | 0xcf8 | 0x1e | stored | 20 | docs/re/STORYB_T17_SPAWNS_AND_DEATH.md |
| `Should_Death_Clone_Play_Idle` | 0x93 | 0x0 | stored | 17 | - |
| `Retreat_Self_Destruct_Explosion` | 0x1e70 | 0x1d | stored | 16 | - |
| `Glory_Cinematics` | 0x21b8 | 0x1b | stored | 10 | docs/re/xml_tags.md |
| `Dynamic_Transform_Delay_Seconds` | 0x2400 | 0x8 | stored | 5 | docs/re/xml_tags.md |
| `Dynamic_Transform_End_Color` | 0x2420 | 0x11 | stored | 5 | docs/re/xml_tags.md |
| `Dynamic_Transform_End_Scale` | 0x2408 | 0x8 | stored | 5 | docs/re/xml_tags.md |
| `Dynamic_Transform_Seconds` | 0x240c | 0x8 | stored | 5 | docs/re/xml_tags.md |
| `Dynamic_Transform_Start_Color` | 0x2410 | 0x11 | stored | 5 | docs/re/xml_tags.md |
| `Dynamic_Transform_Start_Scale` | 0x2404 | 0x8 | stored | 5 | docs/re/xml_tags.md |
| `Reinforcement_Region_Blob_Name` | 0x1e00 | 0x17 | stored | 4 | - |
| `Shield_Normal_Color` | 0x21d4 | 0x11 | stored | 4 | docs/re/TRU1_A_RE_ANSWERS.md |
| `Tread_Scroll_Rate` | 0x1b6c | 0x8 | stored | 4 | - |
| `Land_Terrain_Model_Mapping` | 0x220 | 0x34 | stored | 3 | - |
| `Particle_Attach_To_Collision` | 0xb7 | 0x0 | stored | 3 | - |
| `Shield_Upgraded_Color` | 0x21e4 | 0x11 | stored | 3 | docs/re/TRU1_A_RE_ANSWERS.md |
| `In_Midground` | 0x3f | 0x0 | stored | 2 | docs/re/SPACE_AND_OPEN.md |
| `Build_Countdown_Text_RGBA` | 0x918 | 0x16 | stored | 1 | docs/re/MCX4_T09_RE_ANSWERS.md |
| `In_Midforeground` | 0x40 | 0x0 | stored | 1 | docs/re/SPACE_AND_OPEN.md |
| `Projectile_Cause_Invulnerability_Spawn_Effect` | 0x1f40 | 0x1d | stored | 1 | - |
| `Projectile_Combat_Mod_Effect` | 0x2440 | 0x1d | stored | 1 | - |
| `Projectile_Convert_Enemy_Spawn_Effect` | 0x1f18 | 0x1d | stored | 1 | - |
| `Projectile_Instant_Heal_Spawn_Effect` | 0x1ed8 | 0x1d | stored | 1 | - |
| `Projectile_Object_Creation_Particle` | 0x1a78 | 0x1d | stored | 1 | docs/re/CU8F_T01_T02_T07_RE.md |
| `Projectile_Weaken_Enemy_Spawn_Effect` | 0x1f68 | 0x1d | stored | 1 | - |
| `Shield_Sub_Mesh` | 0x23c0 | 0x17 | stored | 1 | - |

## Faction (13)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Land_Mode_Garrison_Selection_Blob_Material_Name` | 0x2868 | 0x17 | unread | 8 | - |
| `Selection_Blob_RGBA` | 0x28b4 | 0x16 | unread | 8 | - |
| `Space_Mode_Garrison_Selection_Blob_Material_Name` | 0x2890 | 0x17 | unread | 8 | - |
| `Bombardment_Blob_Size` | 0x2720 | 0x8 | unread | 3 | docs/re/HUD_GAPS.md |
| `Bombardment_Shadow_Blob_Material_Name` | 0x2700 | 0x17 | unread | 3 | docs/re/xml_tags.md |
| `Land_Ability_Targeting_Range_Overlay_RGBA` | 0x2d58 | 0x16 | unread | 3 | - |
| `Land_Area_Effect_Range_Overlay_RGBA` | 0x2d20 | 0x16 | unread | 3 | - |
| `Land_Retreat_Countdown_Color_RGBA` | 0x219c | 0x16 | unread | 3 | - |
| `Space_Retreat_Countdown_Color_RGBA` | 0x20d0 | 0x16 | unread | 3 | - |
| `Bombardment_Lighting_Color_List` | 0x27d0 | 0x14 | unread | 1 | docs/re/xml_tags.md |
| `Bombardment_Lighting_Intensity_List` | 0x27e8 | 0x13 | unread | 1 | docs/re/xml_tags.md |
| `Bombardment_Lighting_Seconds_List` | 0x2800 | 0x13 | unread | 1 | docs/re/xml_tags.md |
| `Bribed_Color` | 0x3088 | 0x16 | unread | 1 | docs/re/xml_tags.md |

## Attack_Action (1)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Attack_Firing_Bone_Name` | 0xf0 | 0x17 | stored | 10 | docs/re/xml_tags.md |

## Blast_Ability (2)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Charged_Effect` | 0x180 | 0x1d | stored | 1 | docs/re/xml_tags.md |
| `Charging_Effect` | 0x190 | 0x1d | stored | 1 | docs/re/xml_tags.md |

## Cable_Ability (6)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Cable_End_Speed_Fraction` | 0x194 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `Cable_Fadeout_Time` | 0x19c | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `Cable_Length_Fraction` | 0x190 | 0x8 | stored | 1 | docs/re/xml_tags.md |
| `Cable_Render_Mode` | 0x1c0 | 0x4b | stored | 1 | docs/re/xml_tags.md |
| `Cable_Texture_Name` | 0x1a0 | 0x17 | stored | 1 | docs/re/xml_tags.md |
| `Cable_Width` | 0x198 | 0x8 | stored | 1 | docs/re/xml_tags.md |

## Campaign (3)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Camera_Distance` | 0xd4 | 0x8 | stored | 126 | docs/re/xml_tags.md |
| `Camera_Shift_Y` | 0xd0 | 0x8 | stored | 126 | docs/re/xml_tags.md |
| `Camera_Shift_X` | 0xcc | 0x8 | stored | 125 | docs/re/xml_tags.md |

## CommandBarComponent (3)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Offset_Render` | 0x3e3 | 0x50 | unread | 110 | docs/re/UI.md |
| `Swap_Texture` | 0x3e1 | 0x50 | unread | 29 | docs/re/UI.md |
| `Should_Render_At_Drag_Pos` | 0x3e5 | 0x50 | unread | 20 | docs/re/UI.md |

## Decal (3)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Category` | 0x40 | 0x17 | unread | 227 | docs/re/A1_READINGS.md |
| `Texture_Name` | 0x50 | 0x17 | unread | 59 | docs/re/DECORATIONS.md |
| `Z_Angle` | 0x6c | 0x8 | unread | 19 | docs/re/DECORATIONS.md |

## Eat_Attack_Ability (1)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Rotation_Base_Bone_Name` | 0x1d8 | 0x17 | stored | 1 | - |

## Flare (6)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Radius` | 0xc | 0x8 | unread | 19 | docs/re/AI_TACTICAL.md |
| `Diffuse` | 0x0 | 0x10 | unread | 13 | docs/re/SPACE_AND_OPEN.md |
| `Fraction` | 0x10 | 0x8 | unread | 12 | docs/re/FIXPASS_GALACTIC.md |
| `Num_Slots` | 0x1c | 0x6 | unread | 12 | - |
| `Slot_X` | 0x14 | 0x6 | unread | 12 | - |
| `Slot_Y` | 0x18 | 0x6 | unread | 12 | - |

## Force_Confuse_Ability (1)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Confuse_Effect` | 0x190 | 0x1d | stored | 2 | docs/re/xml_tags.md |

## Force_Healing_Ability (10)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Owner_Light_Effect_Color` | 0xb4 | 0x10 | stored | 67 | - |
| `Owner_Light_Effect_Color2` | 0xc0 | 0x10 | stored | 66 | - |
| `Owner_Light_Effect_Duration` | 0xcc | 0x8 | stored | 66 | - |
| `Owner_Light_Effect_Pulse_Count` | 0xd0 | 0x6 | stored | 66 | - |
| `Owner_Light_Effect_Type` | 0xb0 | 0xe | stored | 66 | docs/re/OBJECT_TYPES.md |
| `Target_Light_Effect_Color` | 0xd8 | 0x10 | stored | 15 | - |
| `Target_Light_Effect_Color2` | 0xe4 | 0x10 | stored | 15 | - |
| `Target_Light_Effect_Duration` | 0xf0 | 0x8 | stored | 15 | - |
| `Target_Light_Effect_Pulse_Count` | 0xf4 | 0x6 | stored | 15 | - |
| `Target_Light_Effect_Type` | 0xd4 | 0xe | stored | 15 | docs/re/OBJECT_TYPES.md |

## Grenade_Attack_Ability (1)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Grenade_Spawn_Bone` | 0x1c8 | 0x17 | stored | 3 | docs/re/xml_tags.md |

## Hack_Ability (2)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Lightning_Source_Bone_Name` | 0x1b0 | 0x17 | stored | 3 | - |
| `Lightning_Target_Bone_Name` | 0x1d0 | 0x17 | stored | 3 | - |

## HardPoint (2)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Turret_Bone_Name` | 0x1c0 | 0x17 | unread | 41 | docs/re/LOB_SOLVER.md |
| `Barrel_Bone_Name` | 0x1e0 | 0x17 | unread | 36 | docs/re/TACTICAL_COMBAT.md |

## HeroUnit (2)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Holster_Drawn_Bone_Translation` | 0x22b8 | 0x10 | stored | 7 | docs/re/CU8F_T01_T02_T07_RE.md |
| `Holster_Holstered_Bone_Translation` | 0x22c4 | 0x10 | stored | 7 | docs/re/CU8F_T01_T02_T07_RE.md |

## Leech_Shields_Ability (1)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Leech_Effect` | 0x208 | 0x1d | stored | 1 | - |

## LightSource (6)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Blob_Color` | 0x20 | 0x10 | unread | 4 | docs/re/ABILITY_PARTICLES.md |
| `Auto_Destruct_Fade_Time` | 0x1c | 0x8 | unread | 1 | docs/re/xml_tags.md |
| `Auto_Destruct_Time` | 0x18 | 0x8 | unread | 1 | docs/re/xml_tags.md |
| `Blob_Intensity` | 0x2c | 0x8 | unread | 1 | docs/re/xml_tags.md |
| `Blob_Radius` | 0x30 | 0x8 | unread | 1 | docs/re/xml_tags.md |
| `Falloff_Start` | 0x14 | 0x8 | unread | 1 | docs/re/xml_tags.md |

## LightningEffect (3)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Texture_Repeat` | 0x70 | 0x8 | unread | 13 | - |
| `Texture_Scroll` | 0x74 | 0x8 | unread | 13 | - |
| `Replace_Faded` | 0xd1 | 0x0 | unread | 1 | - |

## Personal_Flame_Thrower_Ability (1)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Flame_Emitter_Model_Name` | 0x198 | 0x17 | stored | 2 | docs/re/xml_tags.md |

## TacticalCamera (3)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Intensity` | 0x0 | 0x8 | unread | 20 | - |
| `Radius` | 0x10 | 0x8 | unread | 19 | docs/re/AI_TACTICAL.md |
| `Diffuse` | 0x4 | 0x10 | unread | 13 | docs/re/SPACE_AND_OPEN.md |

## Tactical_Bribe_Ability (3)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Bribed_Effect` | 0x180 | 0x1d | stored | 1 | docs/re/xml_tags.md |
| `Bribed_Effect_Small` | 0x190 | 0x1d | stored | 1 | docs/re/xml_tags.md |
| `Effect_Z_Offset` | 0x1a0 | 0x8 | stored | 1 | docs/re/xml_tags.md |

## Tractor_Beam_Attack_Ability (1)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Start_Lightning_Frame_Number` | 0x188 | 0x8 | stored | 1 | - |

## TradeRoute (4)

| tag | offset | code | state | base uses | spec today |
|---|---|---|---|---:|---|
| `Color` | 0x4d8 | 0x16 | unread | 200 | docs/re/P4_SIM_SYSTEMS.md |
| `Render_Mode` | 0x28 | 0x17 | unread | 63 | docs/re/AUDIO.md |
| `Texture_Name` | 0x50 | 0x17 | unread | 59 | docs/re/DECORATIONS.md |
| `Width` | 0x24 | 0x8 | unread | 17 | docs/re/DECORATIONS.md |

