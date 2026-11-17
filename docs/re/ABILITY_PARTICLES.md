# Ability and projectile status particles (StarWarsG.exe FoC x64 v1.121)

Written 2026-11-17 for roadmap group vis1, task t06 ("secondary ability / projectile particles never spawned"). It
covers `Target_Particle_Effect`, `Projectile_Stun_Spawn_Effect`, `Stunned_Effect`, `Drain_Effect_Name`,
`Base_Shield_Penetration_Particle`, `Infection_Particle`, `Heal_Range_Blob_Material` (with `Blob_Color`) and the
repair-facility `Lighting_Effect_Name` tag. It builds on ABILITY_VISUALS.md (owner particles, beams, jamming blob),
ANIM_PARTICLES.md §B.1 (particle GameObjects), TACTICAL_GAPS.md §1.10 / §2.3, REMAINING_GAPS.md §5.4,
CU8F_T04_T06_RE.md §3.5 (infection), TRU1_A_RE_ANSWERS.md A2 (base shields) and AUDIO.md (the SFX half of the same
helper), and does not repeat them. Clean-room: prose and tables only.

Conventions: addresses are RVAs (image base 0x140000000). [H] read directly (decompiler, call arguments checked in
the disassembly where noted), [M] inferred from strong evidence, [L] guess. GC = GameConstants (base 0x140b15800).
FPS = 30. SAP = the object's special-ability data block (obj +0x100). XML values are vanilla FoC.

## 0. Summary

| Effect | Trigger | Placement | Lifetime | Retail kind | Port hook (read-only) |
|---|---|---|---|---|---|
| `Target_Particle_Effect`, Force_Healing (heal / harm) | each target healed or harmed at a heal interval | attached to the target, first found `Target_Particle_Bone_Name`, else ROOT | the type's `Particle_Lifetime_Frames` (30-60 frames) | sim particle object | **missing**: `heal_pass` records nothing |
| `Target_Particle_Effect`, Earthquake / Telekinesis | apply on the target | attached to the target (ROOT / first found bone) | until that target's effect ends (destroyed explicitly) | sim particle object, id kept by the target's behaviour | `AbilitySystem::special_applied` + `special_target` |
| `Target_Particle_Effect`, Force_Lightning | bolts created at the cast | attached to the **primary** victim (`B_PELVIS`) | 105 frames | sim particle object | **missing**: victims and cast moment not exposed |
| `Target_Particle_Effect`, Hack / Repair (R2) | apply on the target | attached to the target (`B_Turret` / ROOT) | 105 frames | sim particle object | `special_applied` + `special_target` (start = applied 0 -> 1) |
| `Projectile_Stun_Spawn_Effect` | each STUNNABLE victim of a land stun detonation | attached to the victim at the first GC status bone found | 300-900 frames, **not** tied to the stun | sim particle object | **missing**: stun start and source not exposed (`CombatSystem::stunned` gives only the state) |
| `Stunned_Effect` | each STUNNABLE victim reached by the Stun_Ability wave | same as above | 270 frames | sim particle object | **missing**: as above |
| `Drain_Effect_Name` | Drain_Life apply, one per victim | a **LightningEffect** (not a particle) from the owner's `Drain_Source_Bone_Name` to the victim's `Drain_Target_Bone_Name` | while the drain runs; moved every update, destroyed at the end | render only | `special_applied` + `special_target` (first victim); other victims need an accessor |
| `Base_Shield_Penetration_Particle` | an AFFECTED_BY_SHIELD unit straddles a hostile active shield | world-placed at the dome hit point, oriented on the hit normal | while straddling; moved each frame; destroyed on leave | sim particle object (world, not attached) | `CombatSystem::base_shield_straddled`; hit point needs a render-side dome test or an accessor |
| `Infection_Particle` | INFECTION attached by a hit or a spread | attached to the victim at the first GC status bone found | until the infection expires (destroyed explicitly) | sim particle object, id in the infection pack | `CombatSystem::infection_attached` / `infection_suspend_count` (or `kUnitInfected`) |
| `Heal_Range_Blob_Material` + `Blob_Color` | the healer is selected, owned by the local player | flat square of side 2 x `Heal_Range` centred on the healer | while selected | render only (terrain decal / alBlob) | **missing**: no accessor for a Force_Healing special; selection is UI state |
| `Lighting_Effect_Name` | none | none | none | **ignored**: the tag is not in the exe | nothing to port |

## 1. Shared helpers

### 1.1 Particle objects [H]
- 0x38d230 "create attached particle": creates an object of the given type with Create_Object 0x29f810 in the
  **holder's own object store** (holder +0x2b8), owner = the holder's owner, at the holder's position and facing, then
  attaches it to the holder (0x38d320) at the named bone (index 0 when the name is empty or the holder has no model).
- 0x38d580 "create status particle": the same, but the bone is the first entry of GC
  `Object_Visual_Status_Particle_Attach_Bone_Names` (GC +0x119d8; vanilla `B_HEAD, Turret_00, B_Turret_00, B_Turret,
  B_Turret_Base, ROOT`) that the holder's model has. No model, or none of the names found: bone index 0. The GameConstants
  comment names the use: "visual status particles, like stun, heal, corrupted, invulnerable".
- 0x38d320 attach: stores the parent (child +0x2e0) and bone (child +0x2f0); when both have a render model the child's
  model is scaled by the ratio of their `Scale_Factor` / instance scales and parented to the bone. The child then
  follows the parent. Whether a child is removed when its parent dies was not read [L].
- These are ordinary GameObjects (they take an object id). Their lifetime comes from the PARTICLE behaviour (death
  frame = creation frame + `Particle_Lifetime_Frames`, 0x635cb0 / 0x635d00; ANIM_PARTICLES §B.1). A particle type
  **without** `<Behavior>PARTICLE` has no timer and lives until something destroys it (Kill 0x391310).
- No scene / render test guards any of these creations: retail makes them in headless-like modes too [H for the
  helpers read here].

### 1.2 Activation presentation 0x48e4d0 and Begin_Effect 0x48cca0 [H]
- 0x48e4d0(ability, owner, target), the shared presentation every special-ability class calls (73 call sites):
  - owner (when given): owner light pulse (`Owner_Light_Effect_*`, only when no light effect is running on it), owner
    particle at the first found `Owner_Particle_Bone_Name` else ROOT (0x48e6b0, ABILITY_VISUALS §3), `SFXEvent_Activate`;
  - target (when given): target light pulse (0x48e880, `Target_Light_Effect_*`), **`Target_Particle_Effect`** at the
    first `Target_Particle_Bone_Name` the target's model has, else ROOT (0x48e6b0 with the resolved type at ability
    +0x128 and the bone list at +0x130), `SFXEvent_Target_Affected`.
  - A null or unknown particle type does nothing (vanilla `HUGE_DUD`, `None`).
- 0x48cca0 (Begin_Effect: "has APPLIED his/her %s effect") runs the same target half when its "skip visuals" flag is
  clear, then notifies the owner's SAP. Most classes call it with the flag set and use 0x48e4d0 instead.
- Each call creates a **new** particle object; nothing remembers it unless the class does (§2.2).

## 2. `Target_Particle_Effect` (ability +0x120, resolved +0x128; bones `Target_Particle_Bone_Name` +0x130)

The field is in the shared special-ability block, so every class can carry it. 25 vanilla uses:

| Class (users) | Value | Bones | Lifetime of the type |
|---|---|---|---|
| Earthquake_Attack (6: Emperor, Obi-Wan, Vader, Luke, Yoda) | `Earthquake_Dust_Particles` | none -> ROOT | no PARTICLE behaviour |
| Force_Healing (4 repair facilities / satellite / UM12 marker) | `Shield_Sparks_Medium` | `B_TURRET, TURRET_00, B_TURRET_BASE` | 30 frames |
| Force_Healing (bacta tank, `Prop_DefenderInfantryAutoHeal`) | `Unit_Healed_Particles` | `B_HEAD` | 60 frames |
| Force_Healing harm (`Prop_InfantryAutoDamage`) | `Environmental_Fire_Damage_Particles` | `ROOT` | 10 frames |
| Force_Healing harm (Emperor `Emperor_AE_Force_Corruption`, Heal_Amount -3) | `Corrupt_Enemy` | `B_HEAD` | 60 frames |
| Force_Healing (story doorway) | `HUGE_DUD` | | not a type: nothing |
| Force_Telekinesis (2, Vader) | `Force_Telekinesis_Particles` | `Turret_00, B_Turret_00, B_Turret, B_Turret_Base, ROOT` | no PARTICLE behaviour |
| Force_Lightning (Emperor) | `Force_Lightning_Smoke` | `B_PELVIS` | 105 frames |
| Hack / Repair (R2-D2 team) | `Force_Lightning_Smoke` | `B_Turret` / none | 105 frames |
| Corrupt_Systems (IG-2000), Summon (Silri) | `Corrupt_Systems_Effect`, `Summon_Effect` | none | not read here |
| Planet_Destruction, Hack_Super_Weapon (galactic) | `Planet_Explosion_Galactic` | | galactic, out of scope |

### 2.1 Force_Healing (update 0x6fbb20, heal 0x6fbf80, harm 0x6fbc20) [H, arguments checked in the disassembly]
- Trigger: the heal pass that fires every `Heal_Interval_In_Secs` (TACTICAL_GAPS §2.3.1).
- `Heal_Range` <= 0 (self heal): when the owner is below full health it is healed, then the presentation runs with the
  **owner only** (no target), so `Target_Particle_Effect` is **not** spawned on a self-healer.
- `Heal_Range` > 0, all targets: for every candidate actually healed (combined fraction < 1 at the test), the
  presentation runs with **no owner and that target**: one target particle per healed unit per interval. After the
  loop, when at least one unit was healed, it runs once more with the owner only (owner light / particle / activate
  SFX).
- `Single_Target_Heal`: only the chosen unit is healed and the presentation runs once with owner **and** that target.
- Harm path: the same shape on each harmed enemy (`Environmental_Fire_Damage_Particles`, `Corrupt_Enemy`).
- Placement: attached to the healed unit at the first `Target_Particle_Bone_Name` its model has, else ROOT.
- Lifetime: the type's `Particle_Lifetime_Frames` (all healing values are PARTICLE types), independent of anything else.

### 2.2 Earthquake (apply 0x6f7240, remove 0x6f7660) and Telekinesis (apply 0x6fe1b0, remove 0x6fe570) [H]
- Trigger: a successful apply on the target (after the gates of ABILITIES_HARDPOINTS §4.14). The apply runs
  Begin_Effect with visuals skipped, then 0x48e4d0(owner, target): owner visuals and the target particle on the target.
- Then the target's EARTHQUAKE_TARGET behaviour (0x632eb0) / telekinesis target behaviour (0x63eba0) looks up the
  object of the ability's particle type whose parent is the target and **stores its id** in its pack (+0x50).
- Removal: when that target's effect ends (0x6330f0 / 0x63ee80, called from the class remove), the stored particle is
  destroyed (Kill, flag 1) and the id cleared. Both particle types have no PARTICLE timer, so this is their only end.
- Placement: attached to the target (Earthquake: ROOT; Telekinesis: the first of `Turret_00 ... ROOT` found).

### 2.3 Force_Lightning (visual 0x6fdc40, from the update 0x6fd880) [H]
- Trigger: when the bolts (one `Force_Lightning` LightningEffect per victim) are created, the presentation runs once
  with owner and the **first entry of the victim list** (the primary victim). Other victims get bolts, no particle.
- Placement: `Force_Lightning_Smoke` on the primary victim at `B_PELVIS` (else ROOT); 105 frames.

### 2.4 Hack (apply 0x7013d0) / Repair (apply 0x70c580) [H for the call, M for the moment]
- The apply calls 0x48e4d0 with owner and the hacked / repaired object: `Force_Lightning_Smoke` once per apply
  (Hack at `B_Turret`, Repair at ROOT), 105 frames. The same applies also create a lightning effect (0x481ce0;
  vanilla `Lightning_Effect_Name` `Hack_Electicity_Zaps` / `Repair_Electicity_Zaps`) [M: not read further, not part of
  this task].

### 2.5 Corrupt_Systems (0x71d4a0) [M]
- Each victim in the radius gets the CORRUPT_SYSTEMS behaviour and a `Corrupt_Systems_Effect` attached through
  0x38d230 (bone picked from `Target_Particle_Bone_Name`, empty in vanilla); its id is kept in the victim's pack (+0xc),
  so it is removed with the corruption [L: removal not read]. Summon was not read [L].

## 3. `Projectile_Stun_Spawn_Effect` (type +0x1e98, resolved +0x1ea0) [H]
- Trigger: the land stun detonation 0x4d01c0 (TACTICAL_GAPS §1.10): for each STUNNABLE victim it calls the stun apply
  0x647290 with the projectile type's resolved effect. Projectiles without the field stun with no particle.
- 0x647290 (REMAINING_GAPS §5.4) ends with 0x38d580: the effect is attached to the victim at the first GC status bone
  its model has (`B_HEAD` for infantry, the turret bones for vehicles, else ROOT).
- One particle **per stun call**: a re-stun of a stunned unit adds a second one. The release (0x647200) does not
  touch it; the particle lives its own `Particle_Lifetime_Frames` (vanilla 300 `Stunned_Infantry`, 450 / 510 / 840
  `Stunned_Object*`, 900 `Han_Solo_Target_Stun_Effect`), which the data authors matched roughly to the stun lengths.
- Victims without STUNNABLE get neither the stun nor the particle.
- 8 vanilla uses (Han's stun shot, the magnepulse cannons, the orbital stun bombardments).

## 4. `Stunned_Effect` (Stun_Ability +0x190, resolved +0x198) [H]
- Trigger: the Stun_Ability wave (Urai Fen; apply 0x711c50, wave step 0x712190): every STUNNABLE victim the wave reaches
  gets the stun apply 0x647290 with `int(Stun_Time_In_Secs x 30)` frames and this effect, so the same attach as §3
  (status bone list, one per stun, not removed at the release).
- Vanilla `Stunned_Particle_Effect` (`p_dazed.alo`), 270 frames. The apply's owner side is the usual 0x48e4d0 owner
  half: `Stun_Ability_Particles` (Owner_Particle_Effect, 30 frames) on Urai.

## 5. `Drain_Effect_Name` (Drain_Life_Ability +0x188) [H]
- It names a **LightningEffect** (LightningEffectTypes.xml: `Drain_Life_Effect`, `Lancet_Drain_Life_Effect`,
  `Turret_Drain_Life_Effect`), not a particle. The field is a plain string (type 0x17), looked up at use.
- Trigger: Drain_Life apply 0x71ad50 calls the visual 0x71b970 once per cast. For each victim with a model it creates
  one lightning effect (0x481ce0) from the owner's `Drain_Source_Bone_Name` (+0x1a8; else the owner's target point,
  0x398440) to that victim's `Drain_Target_Bone_Name` (+0x1c8; else its target point), adds it to the scene and keeps
  it with both bone indices in the SAP lists. With `Charging_Time_In_Seconds` = 0 the victim is also held (0x63f060).
  After the loop the shared presentation runs with owner and the first victim (target particle / light / SFX, none set
  in vanilla).
- Update 0x71b5d0 moves every stored lightning to the current bone positions; the removal paths (0x71af40, 0x71bcc0)
  destroy them. So the bolts last exactly while the drain runs (the `Duration_In_Seconds` counter, or a victim leaving
  `Drain_Radius`).
- Kind: render-only (LightningEffectClass, render LCG; ABILITY_VISUALS §2), no object ids.

## 6. `Base_Shield_Penetration_Particle` (type +0x21f8, resolved +0x2200) [H]
TRU1_A_RE_ANSWERS A2 has the full rule; in short:
- Trigger: the AFFECTED_BY_SHIELD service (every frame) finds the unit straddling the wall of an active hostile base
  shield (0x5031c0, dome ray test 0x502770).
- Placement: **world-placed** (not attached) at the dome hit point, orientation from the hit normal with pitch + 90.
  Created once (Create_Object in the primary store, owner = the unit's owner) and then **moved** to the new hit point
  each time the test runs.
- Lifetime: until the unit leaves the shield (0x502ce0 / 0x502e30 / the shield's removal signal), which destroys it
  (0x2a6280). Vanilla `Shield_Penetration_Effect_Particle` has no PARTICLE timer. Quirk: an inactive hostile shield
  earlier in the list makes the unit leave and re-enter each frame, so the particle is destroyed and re-created.
- 34 vanilla uses, all `Shield_Penetration_Effect_Particle` (`p_Shield_Penetration_Generic.alo`).

## 7. `Infection_Particle` (Infection_Ability +0x1d8, resolved +0x1e0) [H]
- Trigger 1, the first infection (projectile hit 0x4cb280, CU8F_T04_T06_RE §3.5): after INFECTION is attached or
  resumed on the hit victim, the infector's particle is attached through 0x38d580 (status bone list) and its id stored
  in the infection pack (+0x34); the particle type is copied into the pack (+0x28).
- Trigger 2, the spread (0x636ae0): a newly infected neighbour gets a new particle of the pack's type the same way. A
  neighbour that is already infected only has its counter reset (no particle).
- Removal: when the frames-left counter runs out (service 0x636e80) the behaviour suspends itself and the stored
  particle is destroyed. `Infection_Particle_Effect` (`p_urai_poison.alo`) has no PARTICLE timer.
- Quirk [M]: a second infecting **hit** on an already infected unit creates another particle and overwrites the stored
  id, so the first one is never destroyed by the expiry (it stays attached; whether it goes with the unit's death was
  not read [L]).
- One vanilla user (IG-88).

## 8. `Heal_Range_Blob_Material` (Force_Healing +0x190) and `Blob_Color` (+0x1b0) (0x6fc3d0) [H unless tagged]
- Called at the top of every Force_Healing update when `Heal_Range` >= 0 (TACTICAL_GAPS §2.3.1 "0x6fc3d0").
- **Owner not selected** (0x39b090, the object's selection block flag [M: "selected"]): the blob is removed (land: from
  the terrain decal list; space: from the scene) and freed.
- **Selected**, material name non-empty and found in ShadowBlobMaterials: created once (land mode: a terrain decal
  0x12f3e0 with that material; space mode: an `alBlob` 0x13a2d0 with colour `Blob_Color` and alpha 1).
- Each update while it exists: shown only when no cinematic is running (0x2cd0c0), a global UI-hide flag
  (0x140a28c91) is clear and the healer's **owner is the local player** (0x39b020); then it is placed as a square of
  side **2 x `Heal_Range`** centred on the healer's position. Otherwise hidden.
- Vanilla: `Reinforcement_Point_Radius_Overlay` (`i_circle01.tga`, ALPHA) on repair facilities, bacta tanks, the
  defense-satellite repair, the UM12 marker, and Urai's and Obi-Wan's self heals (`Heal_Range` 5 there, so a 10-unit
  square under the hero).
- Kind: render only, like the jammer blob (ABILITY_VISUALS §3).

## 9. `Lighting_Effect_Name` (repair facilities) [H]
- Used inside `Force_Healing_Ability` by `Abstract_Repair_Facility`, `Rebel_MDU_Repair_Facility`,
  `Rebel_Defense_Satellite_Repair` and the UM12 marker, value `Repair_Facility_Effect` (a LightningEffect type).
- The string `Lighting_Effect_Name` does not exist in StarWarsG.exe (ASCII or UTF-16 byte search), and the Force_Healing
  field list (shared block + Heal_* + blob) has no lightning name. The correctly spelled `Lightning_Effect_Name` is a
  field of other classes only (xml_field_tables: +0x190, read by Force_Lightning 0x6fdc40; two at +0x180 [M: Hack,
  Repair]; one at +0x1e8), never of Force_Healing. The loader skips the unknown tag, so
  **`Repair_Facility_Effect` is never drawn in retail**. Nothing to port.

## 10. Port mapping (state of `dev` at this worktree, 2026-11-17)

Common facts:
- `SpecialAbilityDesc` (engine/tactical/abilities/ability_data.hpp) parses none of these fields; only the jammer's
  `Owner_Particle_*` / `Blob_*` are read (ability_data.cpp, "R5 jamming visual"). Each hook below also needs the field
  parsed as presentation-only data (not hashed), as the jammer did. GameObjectType tags (`Projectile_Stun_Spawn_Effect`,
  `Base_Shield_Penetration_Particle`) are readable render-side through `GameObjectTypes::get_string`, as
  `BattleView::append_lob_chargeups` does.
- `AbilitySystem::events()` (incl. `EffectOn`, `Stunned`) is cleared inside the tick (`TacticalBattle::consume_events`,
  tactical_battle_part3.cpp, `abilities_->clear_events()`), so `BattleView::capture` cannot read it. Per-tick lists that
  survive to the host are `TacticalBattle::special_weapon_events()` / `ability_finished_events()` (cleared at the next
  tick start). [M] `GameAudioHost::object_events` reads `abilities().events()` after the tick for EffectOn SFX and may see
  an empty list for the same reason (not verified).
- The 2026-10-30 coordinator decision (ABILITY_VISUALS §6.4: owner particles are render particles, no sim objects, id
  divergence accepted) applies equally here: every row below can be done render-side without touching the CRC. Two
  render shapes exist: **state** effects (listed every capture while the state holds: Earthquake / Telekinesis dust,
  infection, shield penetration, drain bolts, heal blob) and **one-shot** effects (heal sparks, stun particles,
  lightning smoke) that need one system per occurrence that plays its `Particle_Lifetime_Frames`; the one-per-(object,
  percent) key of `SnapshotEffect` cannot re-trigger, so one-shots need a per-occurrence key (as `lua_seq` does).

| Effect | Port trigger | Read-only state for the render host | Gap |
|---|---|---|---|
| Force_Healing target particle | `AbilitySystem::heal_pass` (ability_classes.cpp) | none | trigger exists, nothing recorded: needs a per-tick presentation list (healer, target, ability) kept until the next tick, or a const "last healed" accessor |
| Earthquake / Telekinesis dust | `class_apply` cases `Earthquake` / `Telekinesis` (ability_classes.cpp) set `SpecialState::applied` / `target` | `AbilitySystem::special_applied(id, crc)` + `special_target(id, crc)` (crc = the special's name, e.g. via `record(id, type)->gui_name_crc` as `render::apply_beams` does) | none for the state; the particle / bone fields need parsing |
| Force_Lightning smoke | `class_apply` case `ForceLightning` (instant; victims in private `SpecialState::victims`) | none (only the owner's FL_Attack clip via `anim_type_at`) | needs the primary victim + cast frame exposed |
| Hack / Repair smoke | `class_apply` cases `Hack` / `Repair` | `special_applied` 0 -> 1 between captures + `special_target` | one-shot from a state edge; fine if no re-apply within one capture |
| Projectile_Stun_Spawn_Effect | `CombatSystem::stun_unit` from the detonation (combat_system_part2.cpp) | `CombatSystem::stunned(id, frame)` only | stun start and the source projectile type are not exposed; a re-stun is invisible; `stun_unit` has no effect parameter |
| Stunned_Effect | `StunWave` update calls `stun_unit` (ability_classes.cpp) | same as above | same; the source ability is not exposed |
| Drain_Effect_Name | `class_apply` case `DrainLife` (victims in `SpecialState::victims`) | `special_applied` + `special_target` (first victim) | other victims need a const accessor; bones / lightning name need parsing; draw fits the R5 beam / lightning pass (`render::apply_beams`) |
| Base_Shield_Penetration_Particle | `CombatSystem::service_base_shields` (base_shield.cpp; header says "not modelled: the penetration particle") | `CombatSystem::base_shield_straddled(id)` | the hit point / normal: recompute render-side with the same ellipsoid, or a const accessor over the private `dome_segment` |
| Infection_Particle | `CombatSystem::infection_service` / hit path (`kUnitInfected`) | `infection_attached(id)` + `infection_suspend_count(id) == 0` (or `flags(id) & kUnitInfected`) | `InfectionDesc` has no particle field and the victim does not keep its infector type (vanilla has one user) |
| Heal_Range_Blob_Material | Force_Healing exists (`heal_range` parsed) | none for the special; the selection is the game app's own state | needs a const accessor like `jamming_special` for an object's Force_Healing special (+ material / colour parsing); draw fits `render/jam_fx` |
| Lighting_Effect_Name | n/a | n/a | none: retail ignores it |

## 11. Function index

| RVA | Function |
|---|---|
| 0x48e4d0 | activation presentation (owner and target light, particle, SFX) |
| 0x48cca0 / 0x48e920 | Begin_Effect / End_Effect (log, SAP notification; Begin optionally runs the target visuals) |
| 0x48e6b0 / 0x48e880 | particle at the first found bone of a list, else ROOT / target light pulse |
| 0x38d230 / 0x38d580 / 0x38d190 / 0x38d320 | create attached particle (named bone) / status particle (GC bone list) / at a bone index / attach |
| 0x6fbb20 / 0x6fbf80 / 0x6fbc20 / 0x6fc3d0 | Force_Healing update / heal / harm / radius blob |
| 0x6f7240 / 0x6f7660 / 0x632eb0 / 0x6330f0 | Earthquake apply / remove / target start (dust id) / target end (dust destroyed) |
| 0x6fe1b0 / 0x6fe570 / 0x63eba0 / 0x63ee80 | Telekinesis apply / remove / target start / target end |
| 0x6fd880 / 0x6fdc40 | Force_Lightning update / bolts + primary-victim presentation |
| 0x7013d0 / 0x70c580 | Hack / Repair apply |
| 0x71d4a0 | Corrupt_Systems victims (particle id in the victim pack) |
| 0x4d01c0 / 0x647290 / 0x647200 | land stun detonation / STUNNABLE apply (+ effect) / release |
| 0x711c50 / 0x712190 | Stun_Ability apply / wave step |
| 0x71ad50 / 0x71b970 / 0x71b5d0 | Drain_Life apply / bolts / bolt move |
| 0x502f70 / 0x502770 / 0x502ce0 | AFFECTED_BY_SHIELD service / dome test + penetration particle / leave |
| 0x4cb280 / 0x636ae0 / 0x636e80 | infection hit / spread / service (expiry destroys the particle) |
| 0x39b090 / 0x39b020 / 0x2cd0c0 | selected [M] / owned by the local player / cinematic active |
