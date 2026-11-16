# TRU1 (RE): GRAVITY_CONTROL_FIELD and the FIRE_LOBBING_SUPERWEAPON accept (2026-11-15)

Roadmap group TRU1, tasks t03 (gravity control field) and t01 (human Magnepulse fire-at-position). StarWarsG.exe FoC x64
(Steam), RVAs. Read through REA (Ghidra 12.1.4 decompiler). Tags: [H] read from code, [M] one inferred link, [L] unverified.
No decompiled code is reproduced. "TD" = the targeting data pack (obj +0xf0); "pack" = the behaviour data pack obj +0x158;
"tactical player" = the per-player tactical data the mode manager (global 0x140b15418, vf +0x220 with the owner id obj +0x58)
returns; "base power" = 0x527520 on that tactical player (TACTICAL_GAPS §7.3).

Port: `engine/tactical/abilities/src/gravity_field.cpp` (service, apply, release), `ability_system.cpp` (accept gate 47),
`engine/ui/src/targeting.cpp` (the 47 ground pick), `engine/apps/sim/src/tactical_battle_l1.cpp` (47 recharge on an accepted
lobbing order). Tests: `engine/tactical/abilities/tests/test_tru1.cpp`, `engine/ui/tests/test_gui_screens.cpp`.

---

## 1. GRAVITY_CONTROL_FIELD (behaviour id 0x3a)

### 1.1 Class and data [H]
- `GravityControlFieldBehaviorClass` (vtables 0x8ad4b0 / 0x8ad588 / 0x8ad5b0, 0x40-byte object, pooled). Type-id test vf3
  0x61beb0 answers only 0x3a. Name string `GRAVITY_CONTROL_FIELD` (0x877528) is referenced only by the behaviour name table.
- Service vf6 **0x61c700**; apply 0x61bec0; release-all 0x61c590; vf7 0x61bd50 (detach: frees the list without touching the
  units); vf15 0x61c8d0 (release-all, same body as 0x61c590 [M: which event calls it - owner death or change]); vf16
  0x61be10 (a listed object was destroyed: logs "Removing destroyed %s from the gravity control field list" and drops it
  from the list).
- Interval **60** (OBJECT_RUNTIME §9.1); the first due frame is drawn at attach like every interval ≥ 2 behaviour
  (TACTICAL_GAPS §6.4: one synced `Get_Int(0, 59)`).
- Generator type fields: `Gravity_Control_Field_Range` (type +0x1f98, float), `Gravity_Control_Field_Slow_Fraction`
  (+0x1f9c, float), `Gravity_Control_Field_Effect` (+0x1fa0 name, resolved object type at +0x1fa8). The type
  constructor leaves +0x1f9c at 0 (no constant store found; the only other writer is the variant copy 0x36c000), so
  **a generator without the tag does nothing** (§1.3 step 1).
- Victim type flag: `Is_Affected_By_Gravity_Control_Field` (bool, the flag byte at type +0x1d).
- Victim state: obj +0x3a1 bit **0x40** = "in a gravity control field".
- Pack (obj +0x158): +0x30 / +0x38 / +0x40 = the list of slowed objects (vector: data, count), +0x48 = powered state of
  the last service.

### 1.2 Service 0x61c700 [H]
Every 60 frames (base service 0x4c2f70 reschedules first):
1. Only in a land mode (mode type vf +0xe0 == 1). The owner must have a tactical player and the pack must exist; otherwise
   nothing else happens.
2. **Base power** of the owner's tactical player (0x527520) - asked for every generator, there is no
   `Requires_Base_Power` test here:
   - powered → apply (§1.3); new state 1;
   - not powered → if the list is not empty, release all (§1.4); new state 0.
3. When the new state differs from pack +0x48:
   - the **local** player's faction plays `SFXEvent_HUD_Gravity_Control_Generator_On` (faction +0x24b8) or `..._Off`
     (+0x24c8) when set (presentation);
   - turning on raises the story event **`Interdict_Activated`** with the generator as its object; turning off calls a
     story-side cleanup (0x358630) only when obj +0x200 is set [M: what that is].
4. Pack +0x48 := the new state.

### 1.3 Apply 0x61bec0 [H]
1. S = −`Gravity_Control_Field_Slow_Fraction`. S = 0 → return.
2. For every player in the global player list, index order:
   - **Hostile branch**: the player's faction is not `Is_Neutral` (faction +0x108), it is not the owner, and its relation
     to the owner is "enemy" (0x2824d0 == 1). Query that player's objects around the generator (0x2a8fb0, §1.5) with radius
     `Gravity_Control_Field_Range`. For each hit, in query order, when its type has `Is_Affected_By_Gravity_Control_Field`,
     its bit 0x40 is **clear** and it has a TD:
     1. append one entry to the TD's ModifierSet (TD +0x138 +0xa0), **list 5 = MaxSpeed** (key 5 at 0x8ad4a4): value S,
        expiry −1 (permanent), key CRC32 of an empty buffer (= 0), source = the generator. Speed is therefore
        × (1 − Slow_Fraction) through the MaxSpeed reduction (min −0.99);
     2. when the generator type's effect is set: create it on the first bone of the bone-name table that the victim's
        model has, keep its id in TD +0x19c (presentation);
     3. append the object to the list, set its bit 0x40, register a death listener (vf16 later).
   - **Release branch**: the player is not `Is_Neutral` and its relation to the owner is not "enemy" (own player and allies).
     Same query; for each hit with the flag set in its type, bit 0x40 **set** and a TD: remove this generator's entries from
     its ModifierSet (0x5efc90); only if something was removed: clear bit 0x40, destroy the effect object (TD +0x19c),
     drop it from the list (vf +0x80), unregister the listener.
3. Consequences:
   - **No stacking**: bit 0x40 is shared by all generators, so a unit slowed by one generator is skipped by every other.
   - **Leaving the range does not release** a unit; only power loss, the generator's release-all or a change to a
     non-enemy owner (while in range) does. With the shipped ranges (3000, 100000: "cover entire map") this rarely shows.
   - A unit that moves into range is picked up at the next service (≤ 60 frames).

### 1.4 Release all 0x61c590 [H]
For every listed object in list order: clear bit 0x40; remove this generator's ModifierSet entries; destroy the effect
object (TD +0x19c := none); unregister the listener. Then the list is emptied.

### 1.5 The field query 0x2a8fb0 [H]
Walks one player's object tree (or all players with −1) with an XY rectangle of half-size R around the point, z range
±1e18 (**z is ignored**). Skips objects with obj +0x3a7 set (garrison-style). Accepts an object whose XY position is
within R, else one whose bounding box overlaps the circle. Stops at the result cap (0x7fffffff here); unsorted.

### 1.6 Other readers of bit 0x40 [H]
- Walk locomotor `Deploy(true)` (0x61d300, MOVEMENT §3.2): refuses to start a deploy while the bit is set. The DEPLOY
  ability's stop / adopt (0x3a1ce0) has already run. The three Mobile Defense Units are on the affected list, so a
  slowed MDU cannot deploy.

### 1.7 Shipped data (FoC)
- `U_Ground_Gravity_Generator` (+ `Skirmish_Ground_Gravity_Generator_U`, a variant): range 3000, slow fraction 0.9,
  effect `Repulsor_Jammer_Effect`.
- `Ground_Gravity_Generator` (Empire): range 100000, **no slow fraction** → slows nothing (§1.1).
- Affected types (14): MAL_Rocket_Vehicle, Scout_Trooper, Abstract_Mobile_Defense_Unit, F9TZ_Cloaking_Transport,
  Lancet_Air_Artillery, Gargantuan_Battle_Platform, Snowspeeder, T2B_Tank, M1_Repulsor_Tank, Skiff, Swamp_Speeder,
  Land_Speeder, Gallofree_HTT_Transport, Canderous_Assault_Tank.

### 1.8 Port notes
- The slow is one list-5 entry in the victim's own ModifierSet with source (generator id, generator type-name CRC),
  category 0; `TacticalBattle` already multiplies land speed by 1 + list 5.
- [port] The query uses the unit centre only (the bounding-box overlap is not modelled); garrison-style objects
  (`AbilityObjectDesc::garrison_style`) are skipped.
- `Is_Neutral` players are skipped in both branches (the host's `neutral_player` hook: an owner whose faction
  Is_Neutral), so a slowed unit handed to a neutral player keeps the slow.
- [port] Within one player the units are visited in ascending id (retail: the unsorted object-tree walk of §1.5); this
  orders only the list and the events.
- [port] The generator's death releases its units (unit_dying), a removed generator likewise; which retail vfunc runs at
  death is [M].
- [port] A change of the generator's owner releases nothing by itself; the next service re-sorts the units in range
  (retail's vf15 may run on an owner change [M]).
- [port] The §1.6 deploy refusal is the movement layer's `deploy_block` hook (`MovementSystem::set_deploy_block_fn`, wired
  by `TacticalBattle` to `AbilitySystem::in_gravity_field`), so every Deploy(true) route is refused: DEPLOY on,
  auto-deploy, damage deploy, hover lowering.
- [port] Only in land modes (`AbilityConfig::mode`).
- [port] Presentation leaves as events: `GravityFieldPower` (HUD SFX on / off, `Interdict_Activated`) and `GravitySlow`
  (effect on / off per unit); nothing consumes them yet (backlog).

---

## 2. FIRE_LOBBING_SUPERWEAPON (ability 47) accept, `Can_Activate_Unit_Ability` 0x39b480 case 0x2f [H]

After the common gates (ABILITIES_HARDPOINTS §2.4):
1. The owner has no tactical player → false.
2. The type does not set `Requires_Base_Power` (type +0x8b) → true.
3. Otherwise true exactly when the owner's tactical player has base power (0x527520).

No readiness, range, ally or `SpecialWeaponsAllowed` test; command 25's execute checks readiness ≥ 0.99 itself
(TACTICAL_COMBAT §3.1.1).

`Fire_Lobbing_At_Position` 0x4ea3f0 ends by starting the ability countdown's recharge for 0x2f (AbilityCountdown
Start_Recharge 0x42fb70: `Recharge_Seconds` × (1 + list 9)) when the object has ABILITY_COUNTDOWN. The shipped cannons give
FIRE_LOBBING_SUPERWEAPON `Recharge_Seconds` 30, the same as `Projectile_Fire_Recharge_Seconds`.

Port: the HUD button's ground pick sends command 25 (at position) per selected user [port: the retail cursor code that
queues command 25 is not traced].
