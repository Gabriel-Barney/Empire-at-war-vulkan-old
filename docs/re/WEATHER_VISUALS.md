# Battle visuals: weather emitter, lightning, fog plane, object blob shadows (StarWarsG.exe FoC x64 v1.121) — behaviour spec

vis1 (2026-11-17). Read from the decompiled reference and REA's assembly of the functions named below; written in words.
Sim side of the weather (phases, timers, synced draws, modifiers): `L1_S4_REMAINING.md` §3.2 and
`CU8G_T02_ENV_PICK_RE.md` §4. Environment fields: `TERRAIN_WATER_POST.md` §5.2. Blob geometry and draw:
`DECORATIONS.md` §3. Port code: `engine/render/include/alamo/render/weather_fx.hpp`, `blob_shadows.hpp`,
`engine/apps/game/app/game_render.cpp` (BattleView).

## 1. Weather emitter (0x468a50 init, 0x468980 start phase, 0x46a0c0 service) [H]

- When the weather (re-)initialises with a scenario that has an `emitter_name`, the named model is loaded through the
  asset manager and kept only if it is a particle system; it is added to the land scene (scene vf 0xa8). A scenario
  change, or weather turning off, removes the previous system from the scene at once (vf 0xb0; its two "linger" bytes
  are cleared first, so no particles stay behind).
- Each phase start and each service step while transitioning hands the current emitter intensity (the phase's
  `Emitter_Intensity`, or the lerp between the current and next phase) to the system's scalar setter 0x173220 with
  index 0 and no smoothing (the value snaps).
- Index 0 of that setter re-evaluates, for every emitter of the system that is a weather particle (ALO `3D`), its
  active particle slots: active = round(pool × clamp(intensity, 0, 1)) (0x163c40). Intensity 0 (a CLEAR phase) leaves
  no active slot, so nothing spawns.
- The system sits at the scene origin; its weather emitters draw in the camera-relative cube (ANIM_PARTICLES B.4,
  CU8G_T02_ENV_PICK_RE §6).
- Port: `BattleView::sync_weather_emitter` — one ParticleWorld system per running scenario, created at identity,
  `set_scalar(Detail, intensity, 0)` every frame. [port] the port's Detail scalar also multiplies by the Particle Detail
  option (ParticleWorld rule), which retail's index-0 path does not.

## 2. Lightning flashes (0x277570, called from the light update 0x277830) [H]

The light update runs once per rendered frame (from the scene render wrapper 0x2754f0) and on every environment change.
With L = the active environment's lightning intensity (record +0x138: the TEE `21` value, overwritten by the weather's
`Lightning_Intensity` at every phase start and by the lerp while transitioning):

- L = 0: nothing.
- A flash is running (flash counter ≥ 1): the counter drops by one and the frame gets a flash: a white light of
  strength r × 0.5 × L in a random direction (three components uniform in [−1, 1], normalised) is added to the
  spherical harmonics (with fixed-function lighting, the strength is added to the ambient colour instead).
- Else a wait is running (wait counter ≥ 1): the counter drops by one, no flash.
- Else: wait := ⌊r × 500 × L⌋ frames, flash := ⌊r × 30 × L⌋ frames, no flash this frame.

r are draws in [0, 1] from the **free** LCG (0x140a13e20), so the flashes are presentation only. The SH9 the flash joins
is ambient + the two fill lights (TERRAIN_WATER_POST §5.3 item 1).

Port: `render::LightningFlash` (same schedule; own generator) and `Lighting::flash` (added to SPH_LIGHT_FILL and
SPH_LIGHT_ALL). [port] retail counts rendered frames; the port steps at 60 per second of wall clock
(`LightningFlash::kStepHz`, at most 8 steps per frame), so the schedule does not speed up with the frame rate; it keeps
running while the game is paused, as retail's frames do. No fixed-function ambient variant.

## 3. Fog plane alpha (0x277830) [H, assembly-checked]

When the scene uses the environment's fog and the record has fog enabled, the fog range set on the scene is
start' = (1 − α) × start + α × (end − 0.0001), end' = end, with α = record +0xfc, which the weather's start phase and
service write from `Fog_Plane_Interpolation_Alpha` (only for records with fog enabled). α = 0 keeps the environment's
fog; α → 1 pushes the fog onset to the far end (thinner fog). Shipped scenarios use 0.2 / 0.3 in light phases and 0 in
heavy ones. Port: `render::apply_fog_plane_alpha` in `BattleView::use_scene` (α = 0 when no weather runs).

## 4. Object blob shadows (0x373ac0, the object's model update around 0x3a5100, 0x264000, 0x266340) [H unless tagged]

**Mode** (0x373ac0, per object type): with D = the player's shadow detail option (detail table entry 7, 0..3):
- D = 0 → no shadow (stencil shadow off, blob removed);
- D < `Blob_Shadow_Below_Detail_Level`, or stencil shadows unavailable → **blob**: the model's stencil shadow is turned
  off and a terrain shadow blob is created;
- otherwise → stencil shadow on, blob removed.

Type defaults (type constructor): `Blob_Shadow_Scale` (1, 1), `Blob_Shadow_Intensity` 1.0, `Blob_Shadow_Below_Detail_Level`
0 (so a type without the tag never takes a blob while stencil shadows work). The material name resolves to an index in
the ShadowBlobMaterials table (DECORATIONS §3.1) when the type is set up.

**Creation** (0x264000): only for a model in a scene with a terrain (land); width / height = `Blob_Shadow_Scale` as
world units (not scaled by the object), intensity = `Blob_Shadow_Intensity`, the material index; `Blob_Shadow_Bone_Name`
is resolved to a bone index of the model (0x265f20).

**Placement** (0x266340, once per frame): centre = the bone's world translation when a bone was resolved and the model
is shown, otherwise the object position; z is forced to 0 by the blob (DECORATIONS §3.2); rotation = an object field in
degrees converted to radians [M: taken as the object's heading]; then the blob rebuilds its terrain grid when position,
size or rotation changed (DECORATIONS §3.3).

**Shipped data:** 311 FoC type entries name a blob material (Generic_Shadow, Tree_Shadow, Tree_Shadow_Light,
Rock_Shadow); levels 1 (76), 2 (115), 3 (90), 99 (27, plus one `99r` that reads as 99). At the shipped default
ShadowDetail 3 only the level-99 types (trees, bushes, rocks) draw a blob; units (levels 1-3) draw stencil shadows.

Port: `render::BlobShadowBuilder` → `MapScene::set_shadow_blobs` each frame (land), and
`SimInstanceBuilder::set_blob_shadow_detail` drops the shadow volume (`Instance::no_shadow`) of every type in blob mode.
Not reproduced: the "stencil shadows unavailable → blob" branch (the port always has its shadow volumes). [port] the port's Shadow detail
option is Off / On: Off = 0, On = 3 (`kPortShadowDetailOn`). [port] objects hidden by the fog of war, contained, Lua
hidden, dying or corpses draw no blob. Bone positions come from the drawn model's pose (`model_bone_source`).

## 5. Selection circles on land (UI.md §1.3) — port note

The land selection materials use `BlobStencilMasked.fx`: pass 0 writes stencil where `Texture0` (the cutout) has alpha
> 128 with no colour write, pass 1 blends `Texture1` (SRCALPHA / INVSRCALPHA) where the stencil was set, then a cleanup
pass (docs/shaders/CATALOG.md). Retail draws it on an `alTerrainShadowBlob` draped over the terrain grid. [port] vis1 t03:
the port draws the circle as a unit-quad instance with the blob's own material (per type / faction / hero, as
`ui::TacticalHud` resolves it) and lays it on the ground's local slope under the unit
(`ui::selection_blob_ground_instance`), because the terrain decoration path draws only an effect's first pass.
