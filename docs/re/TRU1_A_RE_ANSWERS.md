# TRU1 lane A: RE answers (base shields t02, ground detonation particle t08), 2026-11-15

Answers to `docs/re/TRU1_A_RE_QUESTIONS.md`. Source: Ghidra (ReVa) decompilation and disassembly of the retail FoC
Steam x64 `StarWarsG.exe` (addresses are RVAs, image base 0x140000000), the shipped FoC XML, and the shield model
`NB_BASESHIELDGEN.ALO` read with `tools/formats/python/alo.py`. Behaviour is described in words; no decompiled
code is reproduced. Tags: **[H]** read in the code, **[M]** one inferred link with good evidence, **[L]** guess.
Ghidra names set in this pass: `BaseShield_Evaluate_State` 0x56bb00, `BaseShield_Set_State` 0x56b540,
`BaseShield_Raster_Cells` 0x56af70, `BaseShield_Absorb_Damage` 0x56ba20, `AffectedByShieldBehavior_Service`
0x502f70, `AffectedByShield_Straddles_Shield` 0x5031c0, `Projectile_On_Ground_Detonate` 0x4cb070,
`Projectile_Move_And_Expire` 0x4c7050, `Projectile_Spawn_Detonation_Particle` 0x4ca6c0 (and a few more, below).

## Summary for the implementer

| Q | Rule in one line | Tag |
|---|---|---|
| A1 | Shield is on iff alive, powered (`Shield_Requires_Base_Power` **defaults to true**), owner faction not neutral, not `Base_Shield_Always_Off`, not story-disabled; off→on waits `Base_Shield_Delay_Time` after the last "off" service; stun / ion stun are not read | H |
| A1 | When on, the shield stamps its `SecondaryOccupationPassability` (Shield) into every pathing cell of a disc of round(R / 20) cells; the dome mesh becomes collidable with mask 2 (normal) or 4 (upgraded) | H |
| A2/A3 | AFFECTED_BY_SHIELD units count as "in the shield" only while they **straddle the wall** of an active **enemy** shield; then ×`Base_Shield_Vulnerability_Modifier` on damage, ×`Base_Shield_Speed_Modifier` on speed, and their TARGETING / WEAPON / TURRET behaviours are suspended | H |
| A4 | Step 15a: the victim is the shield generator itself and the hit name is the dome mesh `shield`; all damage is absorbed, the generator loses nothing, the faction's `SFXEvent_Base_Shield_Absorb_Damage` plays; no shield-state test | H |
| A5 | Enemy projectiles hit the dome mesh when the shield is on: all of them when upgraded, only `Projectile_Does_Shield_Damage = Yes` ones when not; they detonate on the dome (absorbed particle) and their blast still reaches units inside | H (dome shape M) |
| B1 | Ground / water hit → `Projectile_Ground_Detonation_Particle`; every expiry (lifetime, distance, rocket path end, target-radius) and the remote detonation → `Projectile_Lifetime_Detonation_Particle`; no fallback between them | H |
| B2 | Ground particle at the terrain hit point, yaw = projectile yaw; decoration particles are presentation only (detail > 0.6, visibility); no synced draws on either path itself | H |
| B3 | `Projectile_Ground_Detonation_SurfaceFX` is an int id fed to the SurfaceFX manager at the ground point, only when detail > 0.6: presentation | H gate / M role |

---

## A1. BASE_SHIELD service and the shield state machine

### Data

- **Shield data pack** (obj +0x1c8, `BaseShieldDataPackClass`, ctor 0x563580, created by the behaviour attach vf7
  0x56aba0 → 0x3902d0) [H]: +8 **state** (0 = never evaluated, **1 = on**, **2 = off**), +0xc **upgraded** byte,
  +0x10 **off stamp** (frame). Ctor: all 0. Save / load 0x563660 / 0x5635b0 write and read exactly these three.
- Type fields [H; offsets from `xml_field_tables.md`]: `Base_Shield_Radius` +0x21f4 (float), `Base_Shield_Always_Off`
  +0xe1, `Shield_Requires_Base_Power` +0x8c, `Shield_Normal_Color` +0x21d4, `Shield_Upgraded_Color` +0x21e4,
  `Shield_On_Anim` +0xde4, `Shield_Off_Anim` +0xde8, `SFXEvent_Powered_Active_Loop` +0x1430.
- **`Shield_Requires_Base_Power` defaults to true** [H]: the type ctor 0x367250 initialises the flag block at
  type +0x10 with 0x367100, which writes 1 to +0x8c. No FoC base shield sets the tag, so every FoC base shield
  needs its owner to have base power; only `Rebel_Mobile_Shield_Generator` (radius 200) sets it to `no`
  (GROUNDTURRETS.XML). `Base_Shield_Always_Off` defaults to false [H, same block].
- GameConstants [H]: `Base_Shield_Delay_Time` +0x1189c (10.0), `Base_Shield_Speed_Modifier` +0x11840 (0.5),
  `Base_Shield_Vulnerability_Modifier` +0x11844 (2.0). Each has exactly one reader (below).

### Entry points (vtable 0x89a130) [H]

| slot | function | does |
|---|---|---|
| vf5 0x56ac80 | interval | FPS / 2 = 15 frames |
| vf6 0x56b290 | service | reschedule (0x4c2f70), then `Evaluate_State`(force = 0) |
| vf7 0x56aba0 | attach | create the data pack |
| vf9 0x56aca0 | initialise (not on load) | base init 0x4c2f90 (**one synced `Get_Int(0, 14)`** for the service phase, because the interval is > 1), off stamp := 0, shield-mesh shader set-up (0x56ad00, render only), `Evaluate_State`(0) |
| vf10 0x56af30 | re-attach [M: role] | base 0x4c3000 (the same phase draw, only when the next-service frame is still 0), render set-up, `Evaluate_State`(0) |
| vf11 0x56ac00 | post-load | render set-up, `Evaluate_State`(force = 1); when the result is "on", start the powered loop SFX |
| vf15 0x56ba00 | detach | state := 2, then un-stamp the pathing cells (`Raster_Cells`); no SFX, no anim |

### `Evaluate_State` 0x56bb00 (one service), in order [H]

1. P = the owner's player (mode vf+0x220 with obj +0x58). No player or no data pack: nothing.
2. **powered** = true; when `Shield_Requires_Base_Power` is set, powered = "player has base power" (0x527520:
   the AND of the player's +0x198 bool list, **empty list = false**; writers are the `Base_Power_Ability` holders,
   TACTICAL_GAPS §7.3).
3. Dying (obj +0x3a0 bit 0x40) or health (obj +0x5c) ≤ 0 → powered = false.
4. **on** = powered, except: the owner's faction (player +0x68) `Is_Neutral` (faction +0x108) → off;
   `Base_Shield_Always_Off` → off. (When GameModeManager +0xb0 (0x140b15490) is set, on = true regardless of
   everything; it is never set in the shipped game, MOVEMENT §1.)
5. **Story disable:** obj +0x3a1 bit 0x04 set → off. The only writer is the story reward
   `DISABLE_SPECIAL_STRUCTURE` (0x456970, at 0x14045856b): it finds the first object of the named type and sets
   the bit to (parameter ≠ 0) [H].
6. **upgraded** = the AND-fold of the player's +0x1b0 bool list (empty → false), then raised to player +0x1c0 when
   player +0x1c2 is set, and lowered to player +0x1c1 when player +0x1c3 is set. The list is filled by
   `Battlefield_Modifier_Ability` entries with `Base_Shield_Upgrade` (ability +0x190): apply 0x6f02c0, remove
   0x6f13b0 (0x1403a5920 erases the entry), for every player the modifier reaches [H]. tru3 t03 (2026-11-20, read
   from `re_reference/`) [H]: an entry's key is (source object id and type, CRC of the ability's name), built by
   0x48e1e0 -> 0x4ad4c0 and compared by 0x4ad550; the apply appends one node per call without a duplicate test, the
   remove 0x3a5920 deletes every node whose key matches (same source and same ability), so two `Base_Shield_Upgrade`
   abilities on one object are two keys. Every value is true, so "upgraded" = the list is not empty. Shipped holders:
   `EL_/RL_Enhanced_Base_Shield_Upgrade[_NoPre]`. Writers of the +0x1c0..+0x1c3 clamps were not traced [M: none
   in shipped content].
7. When **on** is false: off stamp (pack +0x10) := current frame. This happens at **every** service whose result
   is off.
8. `Set_State`(on, upgraded, force).

**Not read:** stun and ion stun. `STUNNABLE` stuns suspend behaviours 1, 9, 0x0B, 0x35 (list at 0x8b38f0, 0x647290),
not 0x39; the ion stun only adds speed / fire-rate modifiers (ABILITIES_HARDPOINTS §4.4). A stunned or ion-stunned
generator keeps its shield [H for both lists and for what 0x56bb00 reads]. Owner change has no own hook: the next
service re-reads power and faction for the new owner [H by absence].

### `Set_State` 0x56b540 [H]

new state S = 1 if on, else 2.
1. **Early outs** (only when force = 0): S equals the current state and upgraded equals pack +0xc → return.
   **On-delay:** turning on (S = 1) while the current state is 2 and (now − off stamp) < FPS ×
   `Base_Shield_Delay_Time` (float compare, strict) → return, the shield stays off. Because the off stamp is
   refreshed at every off service (step 7 above), the shield comes back at the first service at least
   `Delay_Time` seconds after the **last service that found it off**: with FPS 30, 10 s and a 15-frame cadence that
   is exactly 300 frames after that service. From state 0 (first evaluation) there is no delay.
2. **HUD SFX** (presentation, local player only): 2 → 1 plays `SFXEvent_HUD_Base_Shield_Online` (local player's
   faction +0x23b8) when the shield's owner is the local player or allied (0x39a230), else
   `..._Enemy_Base_Shield_Online` (+0x23c8) when hostile (0x39a450); then starts the type's powered loop SFX on the
   object. 1 → 2: `..._Offline` (+0x2398) / `..._Enemy_..._Offline` (+0x23a8), and stops the loop. 0 → 1 or 0 → 2:
   no SFX.
3. Write state and upgraded into the pack.
4. **Dome mesh** (render model obj +0x2a0, sub-object at the model's shield-mesh index +0xa8) [H]:
   collision mask (sub-object +0xc0) := **0 when off, 2 when on, 4 when on and upgraded**; "collidable" bit
   (+0x30 bit 0x80) set iff on; "hidden" bit (+0x30 bit 0) set iff not on. The `shield_cone` mesh gets the same
   hidden bit. These are what the projectile and penetration ray tests read (A2, A5).
5. Object observers (obj +0x30 list) are notified (vf+0x20 with 0x3982b0(obj)) [M: presentation].
6. **Animation:** when both `Shield_On_Anim` and `Shield_Off_Anim` resolve, play SHIELD_ON (turning on) or
   SHIELD_OFF (turning off) through 0x3a92f0 with "any variant"; when the opposite clip is playing, the start frame
   is taken from its progress. **Synced draw:** 0x3a92f0 takes one `Get_Int(0, n − 1)` (n = number of variants of
   that clip on the model, n ≥ 1) unless obj +0x3a0 bit 0x08 is set [H]. This runs on every `Set_State` that gets
   past the early outs, so also on an upgrade-only change and on the post-load force call.
7. Mesh colour := `Shield_Upgraded_Color` or `Shield_Normal_Color` (0x56b2d0, render only).
8. Only when the state changed (or force): `Raster_Cells` 0x56af70.

### `Raster_Cells` 0x56af70 / 0x56ae30: the pathing stamp [H]

- Disc radius in cells r = int(`Base_Shield_Radius` / 20 + 0.5) (20 = the land pathing cell size, MOVEMENT §1.1);
  centre cell = trunc((x + grid offset) / cell size), same for y (globals 0x140b36b90..ba8). The disc is the
  midpoint-circle span set of radius r; spans are clipped to the grid.
- For each covered cell: state 1 → add the shield object to the cell's **soft-occupant** list (cell +8) if not
  already there (0x59efe0 / 0x59f430); state 2 → remove it (0x59f330). The cell's effective passability is
  recomputed (base | hard occupants | each soft occupant's `SecondaryOccupationPassability` type +0x1694; MOVEMENT
  §1.1) and a change sets the regions-dirty flag 0x140a2bed8.
- Every shipped base shield sets `SecondaryOccupationPassability` = `Shield` (0x40). **No owner test:** the bit is
  stamped for everyone.

### What it does not do [H]

- The service scans **no objects**: it neither queries the tree nor touches units. Units are handled by their own
  AFFECTED_BY_SHIELD behaviour (A2) and by the pathing bit.
- No events or particles besides the HUD SFX, loop SFX, animation and mesh changes above.

### Global shield list (used by A2) [H]

- 0x140a2bdf8 / count 0x140a2be00: objects with a BASE_SHIELD slot (obj +0x36b ≠ −1), appended in registration
  order by 0x502c50, which **skips types with `Base_Shield_Always_Off`**.
- Added by: the mode-start rescan of every object in the store (GameModeClass vf+0x2a8 0x35e7f0 with flag 1,
  called from 0x35f970; flag 0 from mode shutdown 0x365300 clears the list); starting structures placed at map
  markers (0x2a1eb0); tactical construction complete (0x42e890, "Tactical construction of final ... is complete");
  the build-pad "placed" broadcast (TacticalBuildObjectsBehavior vf13 0x42df80).
- Removed by `Die` 0x39bdb0, the object destructor 0x3893e0 and 0x5248b0 (all via 0x5031a0).
- A shield object created any other way (for example Lua `Spawn_Unit` mid-battle) is not in the list until a
  mode restart [M: no other caller of 0x502c50].
- This list is separate from GOM list +0x2f0 (FOG_OF_WAR §9), which `Are_On_Opposite_Sides_Of_Shield`, the bombing
  test and the special-weapon target tests (0x528140, 0x528a70) walk.

---

## A2. AFFECTED_BY_SHIELD (`AffectedByShieldBehaviorClass`, behaviour 0x47, vtable 0x87b710)

### Data and cadence [H]

- Pack obj +0x170 (`AffectedByShieldDataPackClass`, 0x20 bytes, ctor 0x53e0f0), created only by this behaviour's
  attach vf7 0x502600 → 0x3901e0: +8 **penetration particle** object (null = none), +0x18 **shield id**
  (0x3fffff = none). "In a shield" = pack +0x18 ≠ 0x3fffff (0x53e1c0).
- Interval vf5 0x18ceb0 = 1: serviced **every frame**; the inherited initialise takes **no** phase draw
  (interval 1).

### Service vf6 0x502f70, in order [H]

1. Empty global shield list → leave the current shield (0x502e30, below); done.
2. For each list entry in order; entries that are null, have no BASE_SHIELD slot or behaviour, or whose owner is
   **not hostile** to the unit's owner (0x39a450) are skipped (no leave, no enter):
   - **Shield inactive** (no data pack or state ≠ 1): leave **whatever shield is stored** (0x502e30), then go on
     to the next entry.
   - **Active and the unit straddles its wall** (`Straddles_Shield` 0x5031c0, below):
     - a different shield is stored and still exists → leave that one first (0x502ce0);
     - this shield already stored → stop (nothing else this frame);
     - otherwise **enter**: pack +0x18 := shield id; suspend the unit's behaviours **9 TARGETING, 10 WEAPON,
       11 TURRET and 0x20** (vf+0xc0, suspend counter +0x3c += 1); subscribe to the shield object's signal 0x28
       (0x220e90) [M: object removed]; stop.
   - **Active, not straddling:** leave this shield if it is the stored one (0x502ce0); go on.
3. **Leave** (0x502ce0, only when the stored id equals that shield): id := none; resume behaviours 9, 10, 11,
   0x20 (vf+0xb8, counter −1, floor 0); destroy the penetration particle (0x2a6280) and clear pack +8;
   unsubscribe from the shield's signal 0x28. 0x502e30 (leave current) does the same for the stored shield; when
   the stored id no longer resolves to an object it clears the id, resumes and destroys the particle itself.
   Signal 0x28 from the shield (handler 0x5026e0) also leaves.

**Quirk [H]:** an inactive hostile shield earlier in list order clears the stored shield every frame, and a later
active shield the unit straddles re-enters it in the same service (resume then suspend, particle destroyed then
re-created). The end-of-frame state is the same as staying inside; only presentation flickers.

### `Straddles_Shield` 0x5031c0 [H unless tagged]

1. r_u = √(hx² + hy²) of the unit's local bound half-extents (obj +0x28 vf+8, +0xc / +0x10); d = **3-D** distance
   between the unit's position and the shield's position; R = `Base_Shield_Radius`.
2. **Not straddling** when R < d − r_u (fully outside) or d + r_u < R (fully inside). So a unit deep inside the
   dome is **not** "in the shield".
3. Then the dome ray test 0x502770 must pass:
   - the shield must have a render model with a shield-mesh index (model +0xa8 ≠ −1);
   - segment through the unit's target point (0x398440, position + `Ranged_Target_Z_Adjust`) along the line to the
     shield's target point, from +e to −e, e = max of the unit type's two horizontal half-extents (0x371be0);
   - ray against the dome mesh (alRenderableMesh vf+0x80 0x14c520), with the mesh's own collision mask, so it can
     only hit while the shield is on (collidable bit set, mask ≠ 0).
   - On a hit: when pack +8 is empty, create the unit type's **`Base_Shield_Penetration_Particle`** (resolved
     pointer at unit type +0x2200; the tag is at +0x21f8) with Create_Object 0x29f810 in the **primary object
     store**, owner = the unit's owner, at the dome hit point, angles of the hit normal with pitch + 90. When it
     exists, move it to the new hit point and orientation instead. No hit → not straddling.
4. **Idle nudge:** when the unit is not moving (locomotor query 0x39ade0 false [M: "is moving"]), a group move
   0x2a7b80 orders the unit, and every member of its team container (obj +0x2b0 with a TEAM slot, members at
   container +0x110), to a point 1.25 × r_c ahead of the unit along its facing (yaw +0x8c, pitch +0x88), r_c = the
   container's (or the unit's) √(hx² + hy²). So units are not left parked inside the wall.
5. Returns true (straddling).

### Speed (`Base_Shield_Speed_Modifier`) [H; consumer M]

- The speed-modifier chain 0x376990 (type, object, base value), used by the max-speed getters 0x370f00 /
  0x371840 / 0x372ae0 / 0x372fe0 / 0x373080 / 0x374bd0 / 0x371060, multiplies by GC +0x11840 when the object has
  an AFFECTED_BY_SHIELD pack, the mode is tactical (mode vf+0xf0) and the pack holds a shield id. It is applied
  after the SPEED unit-ability multiplier and before the burning factor (obj +0x3a1 bit 2 → type +0x2040).
- It is a plain factor in the getter, **not** a ModifierSet entry: it disappears the moment the id is cleared.
  Which locomotor reads which getter belongs to the movement lane [M].

### Penetration SFX and other effects

- `SFXEvent_HUD_Base_Shield_Penetrated` / `..._Enemy_...` (`SFXEvent_HUD_Enemy_Base_Shield_Penetrated`; faction +0x23d8 / +0x23e8): **no reader** besides the
  faction loader and ctor (0x33a580, 0x33c160) in a byte scan of the image [M: never played].
- `Base_Shield_Penetration_Particle`: only the creation / update above (0x502770; the only reader of type
  +0x2200 that touches this field) [H].

### Units without AFFECTED_BY_SHIELD at a shield edge [H]

- Nothing in the shield code touches them. The only mechanism is the pathing bit: the shield stamps `Shield`
  (0x40) into its cells (A1), and a movement class whose mask lacks `Shield` (the `*NoShield` classes, MOVEMENT
  §1.2) cannot enter those cells; this applies to own and enemy shields alike.
- In shipped FoC data no unit uses a `*NoShield` class (Infantry, Wheeled, Walker, Tracked, Hover, LargeWalker,
  LargeHover, Spotter, Lancet, Flying, Space all include `Shield`), so in practice every ground unit walks through
  any shield, and units without the behaviour get no effect at all.

### Synced draws on these paths

- AFFECTED_BY_SHIELD attach and service: none [H, call-tree scan to depth 3 found no use of the synced LCG
  0x140a13e24].
- Create_Object of the penetration particle: the behaviour attach of a `PARTICLE` type takes no phase draw
  (interval 1) [M: no other draw in Create_Object for particle types].
- The idle nudge's group move may create a movement formation, whose ctor takes `Get_Int(1, FPS)`
  (`CU8G_T02_ENV_PICK_RE.md`, FormationClass ctor 0x40d0d0) [M].

---

## A3. `Take_Damage` step 6 (0x3a9e30, reader at 0x3aa309)

- **Condition [H]:** the victim has an AFFECTED_BY_SHIELD pack (obj +0x170 ≠ null), the current mode is tactical
  (mode vf+0xf0), and the pack's shield id ≠ 0x3fffff. Then amount × `Base_Shield_Vulnerability_Modifier`
  (GC +0x11844).
- **Writer [H]:** only the AFFECTED_BY_SHIELD service (0x53e180 has one caller, 0x502f70); cleared by the leave
  paths above.
- **Who it applies to [H]:** only AFFECTED_BY_SHIELD units, only while they straddle the wall of an active shield
  that is hostile to them. Not the shield's own side, not units standing well inside, not units without the
  behaviour.
- Edge case [H] (tru3 t04, 2026-11-20, read from `re_reference/`): the generator's death (`Die` 0x39bdb0) takes it off
  the global list at once (0x5031a0). When other hostile shields remain (all active and not straddled) or only friendly
  ones, no leave path runs, so the id stays until the shield's signal 0x28: `Remove_Object` 0x2a6a00 raises it first
  thing when the store flush at the end of the frame's object update unlinks the dead object (OBJECT_RUNTIME §2.2),
  and the handler 0x5026e0 leaves (0x502ce0). The factor applies until then. An empty list leaves at the next
  service (step 1).
- No synced draw.

---

## A4. `Take_Damage` step 15a, "a base-shield SHIELD hardpoint absorbs everything"

- **Condition [H]** (0x3aaa58..0x3aaaef): damage kind ≠ 22, x > 0 (after step 14's shield-armor factor), the
  **victim itself has a BASE_SHIELD slot** (obj +0x36b ≠ −1) and behaviour 0x39. Then `BaseShield_Absorb_Damage`
  0x56ba20(behaviour, victim, kind, x, source, name), where name is the hardpoint / mesh name **the caller passed
  to Take_Damage** (not the step-10 re-resolved name).
- **Absorb [H]:** when name equals `SHIELD` case-insensitively, it returns all of x; otherwise 0. When the source
  object exists, the shield owner's faction `SFXEvent_Base_Shield_Absorb_Damage` (faction +0x690) is played at the
  source's position (for a projectile, the projectile object).
- **Report [H]:** absorbed ≥ x → out-absorbed := true. x −= absorbed = 0, so the SHIELDED absorb, the energy pool,
  armor conversion and the hull steps all see 0. **The generator loses nothing**: no health, no shield pool, no
  timer. There is **no shield-state test** in this step.
- **Where the name comes from [H]:** the projectile segment query returns the name of the mesh it hit (ray record
  +0x28, 0x2abcf0 → 0x38f900 → alRenderableMesh vf+0x80), and `Process_Hit` 0x4cb280 passes it to the direct
  damage 0x4c73d0 → Take_Damage. `NB_BASESHIELDGEN.ALO` has a mesh named **`shield`** (MeshShield.fx, hidden by
  default, 408 vertices); it is the dome. So step 15a fires exactly when an enemy projectile's segment hits the
  dome mesh (A5). Hits on the generator's body meshes (`base_ALT0..3`, `SHADOW_dish_ALT*`) are not absorbed and
  damage it normally, so units inside the dome, or shots that pass the dome, can destroy the generator.
- **Visuals [H]:** with out-absorbed set, `Process_Hit`'s direct-damage step 0x4c73d0 takes the absorbed branch:
  `Projectile_Absorbed_By_Shields_Particle` (type +0x1a70) in the decoration store at the hit point, facing the
  hit normal flipped against the projectile's velocity (the generator has a shield-mesh index), then the
  generator type's `Shield_Hit_Particles` pick: **one synced `Get_Int(0, n − 1)` only when n > 0**; no shipped
  base-shield type lists `Shield_Hit_Particles`, so no draw [H for the code, M for the data scan].

---

## A5. Projectiles and the dome

### Is the shot stopped? [H]

- The sweep's object query (0x4c97a0 → 0x2abcf0) visits **hostile players only** and runs the precise model test
  on each candidate; a mesh is hit only when (query mask & mesh mask) ≠ 0 and the mesh's collidable bit is set
  (0x14c520). Query mask = **7** when the projectile type has `Projectile_Does_Shield_Damage`, else **5**. Dome mask
  (A1 `Set_State`): **0 off, 2 on, 4 on and upgraded**.

| shield state | `Does_Shield_Damage` = Yes (mask 7) | `Does_Shield_Damage` = No (mask 5) |
|---|---|---|
| off | passes | passes |
| on | **stopped at the dome** | passes |
| on + upgraded | **stopped** | **stopped** |

- Own and allied projectiles never test the dome (the hostile-player query does not see the shield). The own-player
  variant (0x2acb90, for `Friendly_Damage_Amount` > 0 or `Causes_Infection`) accepts a hit only on the projectile's
  own target, so an own dome blocks only when the shield object is the target [H, TACTICAL_COMBAT §3.4].
- The shooter's AFFECTED_BY_SHIELD plays no part, and nothing tests "target inside / shooter outside": any enemy
  shot whose step segment crosses the dome surface hits it, from outside or from inside [H for the absence of such
  a test; L whether the mesh test is two-sided, so whether shots fired from inside are stopped too].
- Shield pack +0xc in `Are_On_Opposite_Sides_Of_Shield` (L1_S4 §1.2) is this **upgraded** byte: the Lua helper
  mirrors the table (shots without shield damage only count upgraded shields) [H].
- Shipped examples [H, XML]: `Does_Shield_Damage` = No on mass drivers (MAL, Mass Driver Tank, turrets), missiles
  and torpedoes (T4B, Dark Trooper, concussion, proton), sticky / remote / cluster bombs, lightsabers, Ewok rocks;
  Yes on blasters, lasers, turbolasers, ion cannons, SPMAT grenade, bombing-run bombs.

### Where it detonates and what shows [H]

- The dome is an object hit on the **generator**: `Process_Hit` at the dome hit point (ray record +0x18), hit name
  `shield` → Take_Damage step 15a absorbs it all (A4) → absorbed particle; detonation SFX (type +0x12b0, or
  +0x12c0 when the armor factor ≤ 0.75) at the hit point; then area damage and on-detonation effects as for any hit;
  the projectile is destroyed by Move_And_Expire with "hit".
- **Area damage reaches units inside** [H]: `Process_Hit` runs area damage (0x4c7f70) when blast damage and range
  are > 0 and the hit was not cancelled by a damage listener; absorption does not cancel it. Area damage has no
  shield test; its usual victims (TACTICAL_COMBAT §3.5) within R (2-D) of the dome point are hit, inside or outside
  the dome. The generator
  itself is the directly hit object and is skipped unless it has hardpoints. A projectile that passes the dome
  (table above) detonates normally on its target or the ground inside.
- Area damage's per-victim direct damage (0x4c73d0 with "no visuals") takes a synced `Get_Float(0.25 d, d)` only
  when `Projectile_Damage_Delay_Secs` d > 0, and the random-hardpoint draw only with
  `Projectile_Damages_Random_Hard_Points` (TACTICAL_COMBAT §3.5) [H].

### Dome shape for the port [M]

- `NB_BASESHIELDGEN.ALO` mesh `shield` (model space, before `Scale_Factor` 0.75): horizontal radius ≈ 985 at
  z ≈ 0, ≈ 916 at z = −228 and z = 100, ≈ 840 at z = 150, ≈ 700 at z = 200, top at z ≈ 301. Scaled: radius ≈ 739
  (= `Base_Shield_Radius` 740) at ground level and a top ≈ 226 above the generator. It is a **flattened cap**,
  close to an ellipsoid with horizontal semi-axis R and vertical semi-axis ≈ 0.3 R, not a sphere of radius R.
- Port choice: test the projectile segment against that ellipsoid (or load the mesh); the small shields
  (`NB_BASESHIELDGEN_small.ALO`, R 370) and `RV_MDU_ShieldGen.alo` (R 200) were not measured [L: same proportions].
- `Are_On_Opposite_Sides_Of_Shield`, the bombing test and the straddle pre-test use a 3-D sphere of radius R;
  only the projectile and penetration tests use the mesh [H].

---

## B1. Which detonation path spawns which particle [H]

Type fields: `Projectile_Ground_Detonation_Particle` tag +0x1a48 (resolved object type at +0x1a50),
`Projectile_Lifetime_Detonation_Particle` +0x1a58 (+0x1a60), `Projectile_Ground_Detonation_SurfaceFX` +0x1a88 (int,
−1 = none). Byte scans of the image: +0x1a50 and +0x1a88 are read only by 0x4cb070; +0x1a60 only by 0x4c7050 and
0x4caea0.

| path | function | particle | position |
|---|---|---|---|
| ground or water hit in the sweep (land only; every category, the straight-shot skip rule of TACTICAL_COMBAT §3.4 applies) | `Projectile_On_Ground_Detonate` 0x4cb070, from 0x4d0080 | **Ground** (+0x1a50) + SurfaceFX | the terrain / water hit point |
| grenade (category 3) ground or water hit | 0x4cb070, from 0x4ceea0 | **Ground** + SurfaceFX | the hit point |
| sticky grenade ground hit (`Grenade_Sticks_On_Collision`) | none; it sticks (LOB_SOLVER §6.4) | — | — |
| object hit | `Process_Hit` 0x4cb280 | object / armor-reduced / absorbed (TACTICAL_COMBAT §3.5) | the hit point |
| distance travelled ≥ max, or lifetime exceeded (category 3 with a lifetime, or no max distance) | `Projectile_Move_And_Expire` 0x4c7050, no hit | **Lifetime** (+0x1a60) | the projectile's new position |
| rocket / spline path end (categories 4, 6, 8: spline evaluation fails) | 0x4cfc40 / 0x4cf460 → 0x4c7050 forced expiry | **Lifetime** | the projectile's current position |
| `Explode_When_Reached_Target_Radius` (type +0x7e): categories 4 / 6 / 8 once the travelled budget +0x54 ≥ +0x38; category 0 once the step moves away from the stored target point (pack +0x80) [M: that point is the aim point] | 0x4c7050 | **Lifetime** | new position |
| commanded detonation of a projectile object (0x716230 [M: remote bomb]) | 0x4caea0 | **Lifetime** | the projectile's position |
| space (no ground test) | — | never the ground particle | — |

- **No fallback** in either direction: an empty ground field spawns nothing on a ground hit (the lifetime particle
  is not used), and an empty lifetime field spawns nothing on expiry.
- **Fire-at-position arrival** has no own detonation: the flight goes on until a ground / object hit or expiry
  (LOB_SOLVER §6.2), so a shot aimed at the ground normally ends in the **ground** particle.
- Other differences between the two paths [H]: the ground path always plays the detonation SFX (+0x12b0); the
  expiry paths play it only when the lifetime particle object was actually created. Area damage is centred on
  the ground point vs the projectile position. On-detonation effects: both paths run stun, distract, heal,
  convert, invulnerability and infection; `Projectile_Weaken_Enemy_On_Detonation` (+0x70, 0x4d0800) runs on expiry
  (0x4c7050) but not on a ground hit; the ion-stun radius effect (0x4cbbb0, when its radius +0x4d8 > 0) runs on a
  ground hit but not on expiry. 0x4caea0 runs the expiry list (weaken last, no ion stun).

## B2. Placement, owner, draws [H unless tagged]

- **Gate in 0x4cb070:** the ground particle is spawned when the graphics detail level (0x25e500 → +0x14) > 0.6, **or**
  the particle type is not `Is_Decoration` (type +0x3b = 0). A null particle spawns nothing.
- **Spawn (`Projectile_Spawn_Detonation_Particle` 0x4ca6c0):**
  - **Non-decoration** particle: Create_Object 0x29f810 in the **primary store**, owner = the projectile's owner,
    at the ground hit point, angles (0, 0, projectile yaw +0x8c). No detail or visibility gate. Shipped example:
    `Empire_Bombing_Run_Explosion_Land` (`Is_Decoration` No).
  - **Decoration** particle (`Projectile_Dirt_Collide`, `Missile_Explosion_Ground_Hit`, ...): created in the
    secondary (decoration) store, owner = projectile owner, with the projectile's full orientation (+0x84). It is
    skipped when the local player is hostile to the projectile's owner unless both the particle type and the
    projectile type have `Model_Visible_To_Enemy` (+0xa2); when the point fails 0x20bd00 [M: inside the map / view];
    when the fog switch (0x140a284c4) is on and the point / projectile is fogged for the local player (0x35f3b0 /
    0x35f470); and when 0x3751f0 rejects the type (`No_Particle_Below_Detail_Level` +0x1818 against the detail
    level). Presentation only.
- The lifetime particle uses the same spawner at the expiry position, without the 0.6 detail gate in the caller.
- **Synced draws:** none in 0x4cb070, 0x4c7050 or 0x4ca6c0 themselves (call-tree scan for the synced LCG
  0x140a13e24). Order on the ground path: particle → SurfaceFX → SFX → area damage → effects; on expiry: particle
  → SFX → area damage → effects. The draws that can occur are inside area damage (the delay / random-hardpoint
  draws named in A5) and inside the effects (their own rules), plus the destroy 0x391310, which draws only for
  types with a death-spawn list (type +0x1d30) [M: no projectile type has one]. A non-decoration particle's
  Create_Object takes no behaviour phase draw (`PARTICLE` interval 1) [M].

## B3. `Projectile_Ground_Detonation_SurfaceFX` [H gate, M role]

- An int id (+0x1a88, −1 = none), used only by 0x4cb070: when ≠ −1 **and** detail > 0.6, 0x470720(SurfaceFX
  manager 0x140a29fa0, id, transform at the ground point raised by 0.1, the ground hit's surface / material from the
  hit record) is called. It depends on the graphics detail setting, so it cannot be sim state: presentation; the
  port may leave it to the renderer [M].

---

## Open / [L]

- Whether the dome mesh test is two-sided (enemy shots fired from **inside** stopped on the way out) [L]; a probe
  would settle it (an enemy AT-ST inside an active shield firing at a target outside).
- Exact dome shape of the small shield and the mobile shield models (A5) [L: same proportions].
- Meaning of 0x39ade0 ("is moving") [M]. Signal 0x28 is "object removed" [H, tru3 t04: raised only by
  `Remove_Object` 0x2a6a00].
- Writers of the upgrade clamps (player +0x1c0..+0x1c3) not traced [M: none in shipped content].
