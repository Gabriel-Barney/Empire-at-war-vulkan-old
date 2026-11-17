# storyb t17: Lua Highlight, Attach_Particle_Effect, Lua Take_Damage deaths, story Timeout expiry (static read, 2026-11-17)

Questions (roadmap storyb t17, tier 1): in the hands-off Kessel UM01 run the port differs from retail in four places:
the object that `Highlight(true)` makes, the object that `Attach_Particle_Effect` makes, what a Lua `Take_Damage` death
leaves in the primary store, and what an expired story `<Timeout>` does. Mission script:
`STORY_UNDERWORLD_ACTI_M01_LAND.LUA` (`extracted/foc_data_text.zip`).

Binary: FoC Steam x64 `StarWarsG.exe`. Addresses are RVAs (image base `0x140000000`). Read with ReVa (Ghidra 12.1.4,
`/StarWarsG.exe`) and REA (`batch_decompile`, `search_bytes` for struct displacements, `read_function_instructions`).
Retail checks: the hands-off trace `tools/probe_mod/q2_traces/out/20261109-053933-story-vanilla` (`STORY_T04_KESSEL_RUN.md`
§6; `20261109-064135-story-vanilla` is a byte-identical copy) and, for one combat death, the space skirmish trace
`20261116-173353-skirmish-vanilla`. Frame numbers below are mode frames of those traces. Trace object flags are obj
+0x3a0 (0x40 dying, 0x02 dead / detached, 0x80 death clone; `OBJECT_RUNTIME.md` §2.1).
Tags: **[H]** read in code, **[M]** likely (partly read, or read plus trace), **[L]** inferred, not read.

## Results

| Q | Question | Answer | Conf. |
|---|---|---|---|
| 1a | Owner of the `Highlight` object | The **neutral player**: the first player whose faction is the first faction with `Is_Neutral` (faction +0x108). Not the local player | H |
| 1b | Position, and where +15 comes from | Target position, plus (0, 0, zoffset) **only when the target has no model**, plus the highlight type's `Layer_Z_Adjust` (0 in vanilla). The +15 is not an offset: in UM01 `falcon` is the `STORY_TRIGGER_ZONE` marker "falcon" (script line 344), which sits at z 93.13, 15 above the Falcon that was later landed on it | H |
| 1c | Attached? | Only when the target has a model: render-attached to the target's bone 0 with the z offset as the attach transform. A model-less target (the marker) gets an unattached highlight | H (sim position not following: M) |
| 1d | Lifetime | No timer (type has only IDLE). Removed by `Highlight(false)`, by the removal of its attach parent, or with the mode. An unattached one outlives its target | H |
| 1e | Normal creation path, same id counter? | Yes: `Create_Object` 0x29f810 in the target's store, id from the primary-store counter shared with units | H |
| 2 | `Attach_Particle_Effect(type [, bone])` | Created in the **parent's store** (primary for a primary parent, whatever `Is_Decoration` says), owner = **parent's owner**, sim position and facing = the parent's root, normal id. Render-attached to the named bone (root when none or unknown). Lifetime: a type with the PARTICLE behaviour dies at creation + `Particle_Lifetime_Frames` whatever its parent does; a type without it (the landing dust) lives until its parent is removed | H |
| 3a | Is a death deferred? | **No. Removal is.** `Take_Damage` → `Die` → `Kill` run inside the Lua call (health 0, dying and dead flags, clone, explosion). The object leaves the store at the store's pending-delete flush, the last step of the store update. A death after that flush (the T9 story service, where mission scripts run) is flushed next frame, so an end-of-frame sample sees it once with health 0. A combat death inside the store update is flushed the same frame. This is general, not Lua-specific | H (M for the general list of callers) |
| 3b | Why no Mandalorian clone in the primary store | The **corpse path**: dying type is Infantry, and the clone type (its own type) has none of HIDE_WHEN_FOGGED / IMPOSING_PRESENCE / LAND_OBSTACLE / PROXIMITY_MINE **in its generic `<Behavior>` list** (the test does not read `LandBehavior`), is not a death-clone collision obstacle (0x5051d0), and the key is not 4. Such a clone is made in the mode's **secondary store** (own id counter, not in the CRC or the trace) | H |
| 3c | Death clone start health | The dying object's health when the clone is made, which `Die` has already set to **0**. Clone owner: the **neutral player** | H |
| 3d | When is a `Death_Explosions` entry a primary-store object? | It is spawned by the clone's DEATH behaviour at death (one synced pick). It goes to the **primary store only when its type is not `Is_Decoration`**; decoration types (all vanilla particle explosions, `Small_Explosion_Land` included) go to the secondary store | H |
| 4 | Story Timeout expiry | **Confirmed, stronger:** retail has no timeout mechanism at all. The plot's timeout register, cancel and per-T9 process calls all go to one empty function (a lone `ret`, 0x1fa90). Expiry fires nothing, marks nothing and calls no vf; the event stays armed. `<Timeout>` only adds a story-log line | H |

## 1. `GameObjectWrapper.Highlight(on [, zoffset])` (Lua_Call `0x57a6f0`)

### 1.1 Binding [H]

1 or 2 parameters (boolean, optional number, default 0). A dead receiver (wrapper +0x60 null) does nothing.
`true` calls `0x398860(target, offset (0, 0, zoffset), small = false)`; `false` calls `0x3ac0c0(target)`.
`Highlight_Small` (`0x57a860`) is the same call with small = true.

### 1.2 Creation, `0x398860` [H]

1. Type by name: `Object_Highlight`, or `Object_Highlight_Small` when small (`0x407e00`). Unknown type: nothing.
2. **Owner = the neutral player.** `0x331cc0` walks the faction list (`0x140a172d0`) and returns the first faction whose
   byte +0x108 is set. That byte is what Lua `PlayerWrapper.Is_Neutral` (`0x603a40`) returns, so it is the faction's
   neutral flag. `0x294d30` then returns the first player in the player list (`0x140a16fd0`) with that faction
   (player +0x68), and its id (player +0x4c) is the owner. In UM01 that is player index 3, the Neutral slot (trace: both
   highlights owner 3; the human is index 6).
3. **Position.** Start from the target's position (obj +0x78). Only when the target has **no model** (obj +0x2a0 null)
   is the offset (0, 0, zoffset) added to it. Facing = the target's (obj +0x84).
4. **Create** through `Create_Object` `0x29f810` in the target's own store (obj +0x2b8), with the first create flag
   set. That flag adds the new object's type `Layer_Z_Adjust` (type +0xdfc, default 0.0, `OBJECT_TYPES.md`) to z
   (object ctor `0x388b60`); `SPECIALEFFECTS.XML` sets no `Layer_Z_Adjust`, so it adds 0 in vanilla. The second flag (death
   clone bits) is clear.
5. **Attach**, only when the target has a model: `0x38d320(effect, target, bone 0, offset)` (§1.3).
6. The handle is stored in the target's helper pack (obj +0x1a8, created on first use, 0x40 bytes) at +0x30. A second
   `Highlight(true)` overwrites the handle and leaves the first effect alive and unreachable.

Id: `Create_Object` constructs through `0x388b60`, which takes the id from `0x2ac980`. For a primary store ("global
ids") that is the GameModeManager counter shared by all primary stores (`OBJECT_RUNTIME.md` §1.2). The highlight
therefore takes the next id after the units made before it (trace: 80146 at frame 7574 and 80147 at 7668, after 80145)
and is registered in the store's lists like any object. [H]

### 1.3 Attach `0x38d320(child, parent, bone, offset)` [H unless tagged]

- When the child has a model render object and the parent's scale is not 0, the child's model gets a local transform:
  a uniform scale s = child scale / parent scale (scale = type `Scale_Factor` +0x658, or the per-object scale pack
  obj +0x240 → +0x40 when present) and a translation s × offset. In the world the offset therefore ends up multiplied
  by the child's own scale and turned with the parent's bone frame [M for the world result].
- Then the child's late update runs once (`0x3ac530`), its model is detached from any old parent (`0x3a5840`), parent
  (obj +0x2e0) and bone index (obj +0x2f0) are stored, and `0x38c730` hangs the child's model on the parent model's bone
  (`0x12d770`) and registers the child in the store's serviced list. If the child type's field +0x23ec is positive the
  parent's counter +0x328 is raised [L: role].
- **This is a render attachment.** The object service `0x3a6b80` re-reads the parent bone transform for the model only
  when the two models sit in different scenes, and the late update `0x3ac530` skips setting the model transform from
  the object's own transform while a parent is set. No per-frame write of the child's sim position (obj +0x78) from
  the parent was found, so the sim position stays where the object was created [M]. Neither parent moved in the UM01
  run, so the trace cannot show the difference.

### 1.4 Lifetime [H]

`Object_Highlight` has only the IDLE behaviour (no PARTICLE, no timer). It is removed by:
- `Highlight(false)`: `0x3ac0c0` kills the effect held in the target's helper (`0x391310(effect, 1)`) and clears the
  handle; nothing happens when no handle is held;
- the removal of its attach parent: `Remove_Object` `0x2a6a00` calls `0x3920c0(other, dying)` on every object of the
  store, which kills any object whose attach parent (obj +0x2e0) is the dying one. (When the highlight itself is
  removed, the same scrub clears the target's handle to it);
- the end of the mode.
An **unattached** highlight (model-less target) has no parent, so it outlives its target. In the trace neither
highlight is removed through frame 18000: the script's `console7.Highlight(false)` (line 757) was not reached.

### 1.5 The two UM01 highlights [H code + trace]

| Frame | Lua | Target | Highlight |
|---|---|---|---|
| 7574 | `falcon.Highlight(true)` (line 1599) | `falcon = Find_Hint("STORY_TRIGGER_ZONE", "falcon")` (line 344): marker id 47858, owner 3, at (2340.82, 3447.91, **93.13**), no model in game | id 80146, owner 3, at the marker's exact position, attach none |
| 7668 | `console7.Highlight(true)` (line 1773) | `UMP_INTERFACE_NODE` id 69188, owner 4, model `W_Prison_Terminal.alo` | id 80147, owner 3, at the console's exact position, attach 69188 |

The `MILLENNIUM_FALCON_LANDING` (id 80136, z 78.13) is a different object: `Create_Cinematic_Transport` put it at the
same marker (`landing_spot`, line 821) but 15 lower. Where the transport's z comes from was not read [L: the transport
is placed on the ground under the marker]. So the port should highlight the marker, at the marker's own z.

### 1.6 Corrections to `STORY_PRESENTATION_LUA.md` §5.1

"Owned by the local player" → owned by the neutral player. "At the target's position with an offset of (0, 0,
zoffset)" → the sim position gets the offset only when the target has no model; with a model the offset is the
render-attach transform. Add `Layer_Z_Adjust` of the highlight type (0 in vanilla). "Calling `true` twice ... loses the
first handle" is now [H].

## 2. `GameObjectWrapper.Attach_Particle_Effect(type [, bone])` (`0x572e90`)

### 2.1 Binding [H]

1 or 2 parameters. Parameter 1: a GameObjectType wrapper (`0x6021a0`) or a type name (`0x407c00`); anything else, or an
unknown type, logs an error and returns nothing. Parameter 2, when given, must be a string (a bone name). The call
returns a GameObjectWrapper for the new object (`0x5751d0`), or logs "Failed to create" when creation fails.

### 2.2 Creation `0x38d230(parent, type, bone name)` [H]

- `Create_Object` `0x29f810` in the **parent's store** (obj +0x2b8), owner = **the parent's owner** (obj +0x58),
  position = the parent's position (obj +0x78), facing = the parent's (obj +0x84), **both create flags clear** (no
  `Layer_Z_Adjust`, no clone bits). The store is never switched by the particle type's `Is_Decoration`: a decoration
  particle attached to a primary-store object is a primary-store object with a normal primary id.
- Bone: when a name is given and the parent has a model render object, the bone index is looked up by name
  (`0x12d2a0`); no name, no model or an unknown name gives bone **0** (the root). The bone only affects the render
  attachment; the sim position is always the parent's root position.
- Then `0x38d320(particle, parent, bone, offset 0)` (§1.3).

UM01 (trace): `landing_falcon.Attach_Particle_Effect("TRANSPORT_LANDING_DUST_URBAN")` at frame 5361 gives id 80137, owner
3 (the Falcon's owner, the neutral player passed to `Create_Cinematic_Transport`), at the Falcon's exact position, attach
id 80136 (the Falcon), the id right after the Falcon's.

### 2.3 Lifetime [H]

- Type with the PARTICLE behaviour: `ParticleBehaviorClass` init (`0x635cb0`) sets its next service to mode frame +
  `Particle_Lifetime_Frames` (type +0x5e8); that service (`0x635d00`) kills the object. Neither tests the attach parent,
  so an attached particle with a lifetime dies on time even while its parent lives. (A behaviour byte +0x40 skips both;
  who sets it was not traced [L].)
- Type without it: no timer. It is killed by the removal of its attach parent (§1.4 cascade), by a script, or with the
  mode. `Transport_Landing_Dust_Urban` (`PARTICLES.XML`) has no `<Behavior>` (its comment says TransportLandingBehavior
  manages these), so the Lua-attached copy stays while the persistent Falcon stays: never removed through frame 18000 in
  the trace. Whether TransportLandingBehavior also kills Lua-attached children was not read [L: the trace says it does
  not within the run].

## 3. Lua `Take_Damage` deaths

### 3.1 Synchronous death, deferred removal [H unless tagged]

- Lua `Take_Damage(amount [, hard point])` (`0x581710`): a non-team object gets `Take_Damage` `0x3a9e30` with damage kind
  0 and no source; a team container passes it to each member. All synchronous.
- Health ≤ 0 → `Die` `0x39bdb0` inside that call: dying flag 0x40, health := 0, … step 7 `Kill` `0x391310(obj, 1)` →
  `Destroy_Object` `0x2a64f0`, which appends the object to the store's pending-delete vector and runs Detach (dead flag
  0x02, signal 0x10, so the Lua wrapper goes invalid at once). Step 8 makes the death clone (§3.2) and the clone's DEATH
  behaviour makes the death explosion (§3.4), still inside the Lua call.
- The object leaves the store's lists only at the **flush** `0x2a62d0`, the last step of the store update `0x2be640`
  (`OBJECT_RUNTIME.md` §2.2, §5.3).
- Mission plot scripts run in the T9 story service `0x3271d0`, after the mode tick that contains the store update
  (`STORY.md` §4.1). Their deaths are therefore flushed during the next frame's store update: the end-of-frame sample
  shows the object once with health 0 and flags 0x43, then it is gone. A death inside the store update (projectile
  hits, behaviours, object scripts serviced there) is flushed in the same frame and never sampled with health 0 [M for
  the list of such callers].
- Trace: `UMP_MANDALORIAN` 80141 health 0 flags 0x43 at 6914, removed 6915; `UMP_INTERFACE_NODE` 80143 created and killed
  in one Lua call at 7541 (health 0, flags 0x43), removed 7542. Skirmish: X-wing 67543 killed in combat is removed in
  frame 2305, the frame its clone 67588 appears.

### 3.2 Death clone `0x29f270(store, dying, key, sub-key)` [H]

Called from `Die` step 8 with the dying object's own store (`S9_DEATH_CLONE_SHIELDS.md` §1.1-§1.3 for the key and the
clone-type choice). Then, in order:

1. Remember the dying object's health (obj +0x5c; `Die` has already set it to 0) and copy its animation state.
2. **Owner = the neutral player** (same lookup as §1.2 step 2), not the dying object's player. Skirmish trace: the X-wing
   of player 1 leaves a clone owned by player 3.
3. Clone type: `Pick_Death_Clone(key, sub-key)`, else key 1, else the dying object's own type.
4. **Store choice (the corpse path).** The clone goes to the mode's **secondary store** (store +0x20 → mode, mode +0x20)
   instead of the given store when all of these hold, tested in this order:
   1. the **dying** type's `CategoryMask` (type +0x1648) contains `Infantry` (bit looked up by name through the category
      converter at `0x140b31020`);
   2. the **clone** type's generic `<Behavior>` bitset (type +0x790; `0x370320` with slot −1 reads that set only, not the
      Land / Space / Galactic sets) has no HIDE_WHEN_FOGGED (0x19), no IMPOSING_PRESENCE (0x40), no LAND_OBSTACLE (0x14);
   3. `0x5051d0(clone type, 1)` is false. That function is false unless `Avoidance_Disabled` (+0xd3) is false **and**
      `UnitCollisionClass` (+0x169c) is set (≠ −1); in that case it returns `IsDeathCloneObstacle` (+0xcf). So the
      test fails only for a collision-avoiding type that is also a death-clone obstacle;
   4. no PROXIMITY_MINE (0x55) in the same generic bitset;
   5. key ≠ 4 (`Damage_Force_Lightning`).
   Otherwise the clone goes to the given store (the dying object's, normally the primary).
5. `Create_Object` with the second create flag (obj +0x3a0 |= 0xc0), at the dying object's position and facing, first
   flag clear. Its id comes from that store's counter: the secondary store has its own counter, so a corpse-path clone
   takes **no primary id** and is not in the CRC or the trace.
6. Colorization (`0x3a9630`), behaviour strip (S9 §1.6), then **`Set_Health(clone, remembered health)`** (`0x3a89d0`:
   clamps to [0, max]). Through `Die` that is **0**, so every clone starts at health 0 (trace: node clone 80144 and the
   X-wing clone 67588, health 0.0, flags 0xc1).
7. Animation (death clip or idle per S9 §1.6), then the DEATH behaviour is created and attached (§3.4 runs here). The
   clone's DEATH pack gets the dying object's velocity, and for an Infantry dying type a persistence override from global
   `0x140b276b8` (DEATH service `0x58d430` uses it instead of `Death_Persistence_Duration` only in a store without
   global ids, that is the secondary store, and while global `0x140a13ed8` ≠ 3) [M for the roles of the two globals].
8. Land clone types (`Space_Layer` +0x16dc = 0x10) are registered with the land collision system (`0x504880`; MOVEMENT
   §1.3). If the dying object's parent container (obj +0x2b0) is a build pad (has TACTICAL_BUILD_OBJECTS, slot
   +0x35b), the clone is killed at once.

### 3.3 The two UM01 deaths [H code + data + trace]

| | `hunter` (`UMP_MANDALORIAN`, line 1256) | `broken_node` (`UMP_INTERFACE_NODE`, line 1334) |
|---|---|---|
| Dying type Infantry? | yes (`Mustafarian_Indigenous`: `CategoryMask` Infantry) | no (`Droid_Interface_Station`: Structure) |
| Clone type | own type (no `Death_Clone`) | own type (no `Death_Clone`) |
| Generic `<Behavior>` of the clone type | `SELECTABLE` only; HIDE_WHEN_FOGGED is in `LandBehavior`, which the test does not read | not tested |
| 0x5051d0 | `UnitCollisionClass` Infantry, `Avoidance_Disabled` and `IsDeathCloneObstacle` not set → false [M: both defaults false; vanilla XML only ever sets them true, and the trace shows the corpse path] | not tested |
| Key | 0 (Lua kind 0) | 0 |
| Clone store | **secondary**: no primary object, no primary id (next primary id after 80142 is 80143 at 7541) | **primary**: 80144, owner 3, health 0, flags 0xc1, stays (`Remove_Upon_Death` false, default persistence) |
| `Death_Explosions` | none | `Small_Explosion_Land` (`Is_Decoration` Yes) → secondary store, not in the trace |

### 3.4 `Death_Explosions` [H]

- Only DEATH's death-effects function `0x58cf90` (and the TSW target-dummy state, `TSW_PLANET_DESTROY.md`) read the
  list (type +0xcd8; data +0xce0, count +0xce8), by a whole-image search for those displacements. `Kill` itself spawns no
  explosion, so a removal without `Die` (Despawn, a lifetime end) makes none [M for "only"].
- DEATH Initialise (`0x58cdb0`), run when `0x29f270` attaches DEATH to the clone: plays the death clip; with
  `Spin_Away_On_Death` one synced `Get_Float(0, 1)` decides spin-away; then `0x58cf90(clone, phase)`: phase 1
  (`Spin_Away_On_Death_Explosion`, +0x2210) when spinning away, else phase 0 (`Death_Explosions`). A spin-away ends with
  phase 0 in the DEATH service. Phase 2 (`Death_Explosions_End`, +0xcf8, offset turned by the facing) is called from
  elsewhere, not traced [M].
- Phase 0: when the list is not empty, one synced `int(0, count − 1)` (no draw for one entry, `MAIN_LOOP.md` §4.1) picks
  the entry. It is created through `Create_Object` with the first flag set, at the clone's position + (0, 0, clone type
  `Ranged_Target_Z_Adjust` +0x4dc) (`0x398440`) plus the explosion type's `Layer_Z_Adjust`, with the clone's facing,
  owned by the **clone's owner** (the neutral player), in **mode +0x18 (primary) when the explosion type's
  `Is_Decoration` (+0x3b) is false, else mode +0x20 (secondary)**. If the new object has a projectile pack its source id
  and damage type are filled in. Then a camera shake for the explosion type (`0x2cdbb0`). The
  `Death_SFXEvent_Start_Die` sound plays with or without an explosion.
- So the explosion list is the **clone type's** list: the dying type's own list when the clone is its own type, the
  clone type's list otherwise. `Die`'s death projectile (step 9, `Death_Projectiles`) uses the same Is_Decoration store
  rule.

### 3.5 Where the port differs

1. It removes the object inside the Lua call; retail keeps it in the store, health 0 and flags dying / dead, until the
   next store-update flush (§3.1).
2. The corpse-path test must read the clone type's generic `<Behavior>` set (not `LandBehavior`) and include `0x5051d0`
   (§3.2 step 4); a corpse-path clone belongs in a non-CRC local store with its own ids.
3. Clones start at health 0 and are owned by the neutral player.
4. `Death_Explosions`: one synced pick (no draw for one entry), spawned from the clone's DEATH init, primary store only
   for non-decoration types.

## 4. Story `<Timeout>` expiry (T9 story service `0x3271d0`, step 3) [H]

- The "indirect plot call" of `STORY.md` §4.1 step 3 is a **direct call to `0x1fa90`**, after the inactivity timers
  (`0x52e7b0`) and the FLAG poll (`0x52e730`), with (plot, time). `0x1fa90` is a lone `ret`: an empty function the
  linker folded (Ghidra labels it `_guard_check_icall`, which is why it looked indirect).
- The same empty function is the target of every other Timeout call: registration when an event is armed (dependants loop
  in Fire `0x455db0` and RESET_BRANCH re-check `0x45c470`, after the story-log line "Adding timeout event %s, time %f,
  timeout %f"), cancellation in Fire step 3 (`0x455db0`) and in the Reset vf4 bodies (`0x45e480`, `0x45e4b0`,
  `0x45e550`, `0x45e5c0`). Each call is gated by Timeout (event +0x308) ≠ −1 (base ctor `0x4501d0` default −1.0; the
  SPEECH_DONE ctor `0x4562a0` sets 60 when none is given).
- So retail keeps no timeout state and nothing ever expires: **no fire, no "failed" mark, no vf call; the event stays
  armed** until its own trigger. The only trace of `<Timeout>` is the story-log line. This confirms `STORY.md` §8 item 4
  and replaces its [M] with [H]; the port can drop timeout bookkeeping entirely.

## 5. Open items

1. Where `Create_Cinematic_Transport` takes the transport's z from (78.13 under a marker at 93.13) [L].
2. Whether any code writes an attached child's sim position from its parent (§1.3, [M] "no") — only visible with a
   moving parent.
3. What sets `ParticleBehaviorClass` byte +0x40, and the roles of globals `0x140b276b8` / `0x140a13ed8` in the DEATH
   persistence override.

## Functions read

`0x57a6f0` / `0x57a860` Lua Highlight; `0x398860` highlight on; `0x3ac0c0` highlight off; `0x331cc0` first neutral
faction; `0x294d30` first player of a faction; `0x603a40` Lua `Is_Neutral`; `0x29f810` Create_Object; `0x388b60` object
ctor; `0x2ac980` id allocator; `0x38d320` attach; `0x38c730` model attach; `0x3a6b80` object service; `0x3ac530` late
update; `0x3920c0` reference scrub; `0x572e90` Lua Attach_Particle_Effect; `0x38d230` attach-new-object; `0x635cb0` /
`0x635d00` particle lifetime; `0x581710` Lua Take_Damage; `0x39bdb0` Die; `0x391310` Kill; `0x29f270` death clone;
`0x370320` behaviour bitset test; `0x5051d0` collision-obstacle test; `0x3a89d0` Set_Health; `0x3a9630` colorization;
`0x58cdb0` / `0x58cf90` / `0x58d430` DeathBehavior init / effects / service; `0x398440` target point; `0x3271d0` story
T9; `0x455db0` Fire; `0x45c470` RESET_BRANCH re-check; `0x45e480` / `0x45e4b0` / `0x45e550` / `0x45e5c0` Reset;
`0x4501d0` event ctor; `0x1fa90` empty function. Ghidra plate comments were added at `0x1fa90`, `0x29f270`, `0x398860`,
`0x58cf90`, `0x5051d0`.
