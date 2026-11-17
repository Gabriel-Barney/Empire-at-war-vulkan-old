# TRN1: terrain gameplay (cover, SurfaceFX terrain modifiers, conform, speed terms)

Read from the decompiled retail FoC Steam x64 build (`re_reference/ghidra`) and the exe's vtable bytes, 2026-11-17, for
roadmap group trn1 (box MOVE). Behaviour in words; tags as in the other specs ([H] read in code, [M] inferred, [L] guess).
Port code: `engine/terrain/src/ted_read.cpp` (surface helpers), `engine/tactical/combat/src/surface_fx.cpp`,
`engine/tactical/movement/src/movement_system_part2.cpp` (`MovementSystem::conform`), `engine/apps/sim/src/tactical_battle_part2.cpp`
(`apply_speed_factors`). Port choices are listed in `docs/PHASE4.md`, "trn1".

## 1. Surface types and the cover factor (t01)

**Surface type enum** (Surface_Settings name parser 0x356f60) [H]: 0 default, 1 grass, 2 dirt, 3 snow, 4 water,
5 concrete, 6 mud, 7 ice, 8 lava, 12 good_ground, 13 high_ground, 14 slow_ground, 15 damage_ground, 16 infantry_ground,
18 space. 9, 10 and 11 have no name; 17 is set only by the SurfaceFX update for presentation (§2.1). The value of a
terrain cell is the TEM `surface_type` (mini 14, material +0x84) of the cell's material.

**Cell lookup** (0x133b10) [H]: grid index = floor(world / 20 + 0.5) on each axis, clamped to [0, size − 1]; the
cell's material byte (+2) selects the material slot (0xB0 bytes each, table at terrain +0xC8).

**Get_Damage_Multiplier** (0x38e0c0), its last factor [H]: when the victim type's `Surface_Type_Cover_Damage_Shield`
(+0x1e48) is not exactly 1.0, the game mode is land (mode vf +0xe0 = 1) and a terrain exists, the cell under the
victim's position is looked up as above (no water test); if its surface type is 9, 10 or 11 the product is multiplied
by that field. Order of the whole product: ((1 + attacker damage list) × (1 − victim defense list)) × AI difficulty ×
cover. FoC data: 12 infantry types 0.5, 4 types 0.25. No FoC map is known to paint surface 9-11 [M: not scanned].

## 2. SurfaceFX terrain modifiers (t02)

**Instances** [H]: `SurfaceFXBehaviorClass` (vtable 0x8b3428, update vf6 0x6450b0). Its first update builds one instance
per `SurfaceFX_Name` entry of the type (+0x630) and evaluates nothing; every later update calls the instance step
0x5efe50(instance, object, speed fraction) for each instance in list order. The behaviour sits in the land behaviour
list (`SURFACE_FX`; 123 FoC types) and is serviced every frame.

### 2.1 Instance step 0x5efe50 [H]

1. Surface s at the object's position: if the map has a water type (scene +0x204 ≠ 0) and the terrain height at
   (x, y) is **below the water height − 0.5**, s = the water type's surface (4 water, 8 lava, 7 ice); otherwise the
   cell surface of §1.
2. A presentation id: s, or 17 when the object has a parent (obj +0x2a0) for which 0x264cb0 holds and s ≠ 12. Used
   only by the track / particle part (0x5ef6d0, 0x5efff0, 0x5ee570). Not modelled.
3. The modifier step 0x5ee820(instance, object, s), then instance +0xc := s.

### 2.2 Modifier step 0x5ee820 [H]

Settings: the SurfaceFX entry's `Surface_Settings` block for a surface (0x108 bytes each from entry +0x20): Damage_Mod
+0xa0, Defense_Mod +0xa4, FOW_Reveal_Range_Mod +0xa8, Fire_Range_Mod +0xac, Speed_Mod +0xd0, Terrain_Damage +0xd4,
Terrain_Damage_Delay +0xd8 (seconds), status icon +0xb0 / +0xe0 / ... (presentation).

Let p = instance +0xc (the previous surface) and s the new one.

- **Same surface (p = s):** when p is 15 or 8 and the object's timer amount (data pack +0x188) > 0 and its next frame
  (+0x18c) ≤ now: next += int(fps × delay (+0x190)), then `Take_Damage`(object, kind 0x13, amount). The rest of the
  branch only refreshes the status icon. Nothing else.
- **Changed surface:** one block per family, in this order; each block is "leave p" else "enter s":
  - good ground (12): leave = remove every entry keyed by this instance from lists 0 and 1; enter (only when obj +0x3a2
    bit 4, "converted to the enemy", is clear) = add Defense_Mod to list 0 (defense) and Damage_Mod to list 1 (damage).
  - high ground (13): leave = remove from lists 7 and 8; enter (same converted test) = FOW_Reveal_Range_Mod to list 7
    (reveal range), Fire_Range_Mod to list 8 (fire range).
  - slow ground (14) **or water (4)**: leave = remove from list 5 (max speed); enter = Speed_Mod to list 5. Mud (6) has a
    Speed_Mod in the XML but **no block reads it**: mud does not slow. Because the block is leave-else-enter, going
    straight from slow ground to water (or back) only removes the entry: the new surface's slow is not added until the
    unit leaves and re-enters.
  - damage ground (15) **or lava (8)**: leave = zero the timer amount and delay, unless s is one of 4, 6, 8, 12, 14, 15
    (mask 0xf150); enter (Terrain_Damage > 0) = amount := Terrain_Damage, next := now + int(fps × delay), delay :=
    Terrain_Damage_Delay. The first damage comes one delay after entering.
  - infantry ground (16): status icon only.
  Each add is an entry {value, expiry −1 (permanent), stacking category 0, source key}. Adds with value 0 are skipped
  (the block tests the values ≠ 0 first). The removal walks every category of the list.
- **Source key** (0x5efa20) [H]: the object plus CRC32("Persistent_SurfaceFX_Unique_ID_" + the SurfaceFX name) plus the
  entry id: each instance removes only its own entries. Entries of two instances on one surface sit in the same
  category 0, so they stack by the max-then-sum rule (TACTICAL_COMBAT §4.2): the one type with both
  `Infantry_Terrain_Modifier` and `High_Ground` gets 0.25, not 0.5.
- The timer is per object (data pack), shared by the instances.

FoC data: `Infantry_Terrain_Modifier` (82 infantry types) good ground Defense 0.5 / Damage 1.0, high ground FOW 0.25 /
Fire range 0.25, slow ground / water / mud Speed −0.5, damage ground / lava 10 every 1.0 s. `Vehicle_Terrain_Modifier`
has no user in FoC. Status icons (`i_surface_*.tga`), `*_Ground_Color_Tint` and `Status_Icon_Name` are presentation.

## 3. Land terrain conform (t03)

Locomotor vf95 0x42d6a0 → 0x42bb40, land mode only [H] (MOVEMENT §3.6 summarises it):

1. G(q) = terrain height at q; for a movement class whose passability mask has the Water bit (0x20), G(q) = the water
   height when that is above the terrain (the code has no water-type test; the port applies it only on maps with a
   water plane [M]).
2. z = G(p).
3. d = (1, 0) rotated by the yaw (0x2bab90: cos / sin; an angle with a tiny exponent uses cos 1, sin = angle).
4. L = `Overall_Length` (+0x1a20) × `Scale_Factor` (+0x658). If L > 0: front = p + d L/2 and back = p − d L/2; each is cut
   back to the distance at which the static line test 0x5a4c10 (the unit's movement class) meets the first impassable
   cell. pitch = −atan2(G(front) − G(back), |front − back|xy) in degrees (0x20acd0; 0 when the x coordinates and the
   heights are both equal). z += |G(front) − G(back)| / 5.
5. W = `Overall_Width` (+0x1a24) × `Scale_Factor`. If W > 0: d' = d rotated by +π/2 (the float 1.5707964); plus =
   p + d' W/2, minus = p − d' W/2. A cut of the plus ray moves plus as above; **a cut of the minus ray writes p − d' t
   into the plus point** (the minus point keeps its full length) - a retail slip, kept. roll = the 0x20acd0 angle
   from plus to minus.
6. 0x42d6a0 then sets z (+ `Hover_Height` when the hover state is 1 "raised") and the attitude (roll, pitch, yaw).

Pitch and roll are presentation in the port (no sim reader); the z lift is sim state.

## 4. Speed chain additions (t05, t06)

The speed modifier chain 0x376990 (type, object, base) multiplies, in this order [H]: script override (replaces), mode
scale, fleeing bonus, SPEED unit-ability product, `Engines_Disabled_Speed_Modifier` (engines off), **retreat pursuit**,
(1 + list 5), `Base_Shield_Speed_Modifier` (AFFECTED_BY_SHIELD with a shield id, TRU1_A A2), **burning**.

- **Retreat pursuit** (t06) [H]: when the mode is tactical, the retreat coordinator (mode +0x30) has a retreating player
  (+0x1c ≠ −1), the object's owner is not the first player whose faction is `Is_Neutral` (0x294aa0) and not the
  retreating player, and the object is not on the coordinator's list (+0x38, matched by object id 0x3400c0): × the
  owner's faction `Space_Retreat_Pursue_Max_Speed_Mod_Factor` (+0x20b4) in space, `Land_Retreat_Pursue_Max_Speed_Mod_Factor`
  (+0x2194) on land. Faction constructor default 1.0 for both (FoC: Rebel / Empire 3.0 space, 2.0 land; Underworld 2.0 /
  2.0). So every non-neutral player other than the retreating one - allies of the retreating player included - is a
  pursuer.
- **Burning** (t05) [H]: obj +0x3a1 bit 2 (set by a kind-7 hit on a combustible object) → × `On_Fire_Speed_Modifier`
  (+0x2040; FoC 1.2 on 33 entries).

## 5. Destroy_When_Stunned_Over_Water (t04)

See ABILITIES_HARDPOINTS §4.12 (RE reading 2026-11-15). The water test's "terrain more than 0.5 below the water" is the
same comparison as §2.1 step 1. The ion-stun detonation 0x4cbbb0 gathers its victims with the projectile's
`Projectile_Stun_Radius` (+0x1e90) and needs `Projectile_Stun_Duration_Frames` (+0x1e8c) ≠ 0 [H, read 2026-11-17];
the port's ion stun reads `Projectile_Ion_Stun_Radius` instead (finding filed separately).
