# trl2 lane A: RE answers (2026-11-20)

Answers to `TRL2_A_RE_QUESTIONS.md` (roadmap group trl2, tasks t01 / t02 / t05, and tru2 t02). Source: the Ghidra
decompilation of the FoC x64 `StarWarsG.exe` through ReVa (RVAs relative to 0x140000000) and the shipped FoC XML
(`SPECIALSTRUCTURES.XML`, `SPACEPROPS.XML`, `MISCOBJECTS.XML`, `MARKERS.XML`). Behaviour is described in prose; no
decompiled code is reproduced. Tags: [H] read directly; [M] one inferred link; [L] hypothesis.

## Index

| Q | answer (short) | port consequence |
|---|---|---|
| Q1 (t01) | [H] Only the campaign post-load placement 0x2a1eb0 does it, in its planet-structure pass 0. A ground structure (no DUMMY_ORBITAL_STRUCTURE slot) whose type has `Is_Special_Weapon_In_Space` gets its **own type** created for the **battle planet's owner**. It is placed at the first DUMMY_SPECIAL_WEAPON_SOURCE (0x63) object (else the origin) and registered when `Special_Weapon_Index` >= 0. Registration keeps one object per player and index; a duplicate is removed. The object has no galactic link, is not counted for population, is not victory relevant, cannot be targeted, and dies with its owner's space retreat. | add the rule to the campaign space setup (§1.6) |
| Q2 (t02) | [H] Yes. 0x2a00b0 registers every start-placed object in the population list with its company's share. Exceptions: space escorts, and land hero stand-ins / units put into a carrier. | the default stands; add the two exclusions |
| Q3 (t05) | [H] vf8 lists every live object of the player that has a locomotor, plus `Stay_In_Transport_During_Ground_Battle` objects. vf11 always keeps TRANSPORT objects. It keeps listed objects **only if the player (or an ally) holds an object with `Reinforcement_Enabling_Radius` > 0** (FoC: the reinforcement points). Everything else is killed. | change the default (§3.3) |
| Q4 (tru2 t02) | [H] On SIMPLE_SPACE_LOCOMOTOR, vf +0x218 is a turn in place over later frames. It takes \|angle\| / turn rate × the layer's TurnInPlaceSlowdown, with the yaw interpolated linearly. It clears the locomotor's path stack (stopped state) and then starts the turn path at the current frame. | the default stands; two details (§4.3) |

---

## Q1. Ground special weapon in a campaign space battle (t01, b0237)

### 1.1 Which function, and when [H]

- The only campaign path is the post-load placement **0x2a1eb0** (BATTLE_TRANSITIONS §2.1 step 4; its steps are listed
  in REMAINING_GAPS §3.2). It does this inside step 4 ("other structures"), in **pass 0** (pass 1 does nothing for
  these structures).
- The battle setup 0x28fda0, the force split 0x2bce30 / 0x2bd500 and the start-unit placement 0x2a00b0 do not create
  it. The split only moves galactic units; 0x2a00b0 places only the start lists.
- The skirmish paths do not run in a campaign battle:
  - 0x293d60 is called only from the skirmish setup (0x294fa0, 0x295cf0). Those two are called only by the SP skirmish,
    MP and demo starts (0xeed80, 0xc9e10, 0xbd380).
  - The `Load_Map` branch in 0x56480 needs 0x28ae40 ("standalone tactical"). 0x28ae40 returns false when the mode at
    the bottom of the stack is galactic, which is always the case in a campaign battle.
- Callers of the registration 0x35c520, for completeness:
  - 0x56480 and 0x293d60: skirmish / MP;
  - 0x2a1eb0: the starbase (step 1) and this branch;
  - 0x6aa790: Lua `Spawn_Special_Weapon`;
  - 0x38e5a0: owner change; it re-registers the object for its new owner;
  - 0x4ea5a0 / 0x4ea6c0: `LobbingSuperweaponBehaviorClass` vf9 / vf10 (vtable 0x879e98), the land lobbing superweapons.

  `SpecialWeaponBehaviorClass` (vtable 0x8756e0) does not register itself: its vf9 is the generic base.
- Order inside 0x2a1eb0 for a space battle:
  1. The map objects are already loaded, including the DUMMY_SPECIAL_WEAPON_SOURCE prop.
  2. The starbase is created. When there are 2 or more `Space Station Position` markers, one synced int picks the
     marker.
  3. Temporary `Tactical_Additional_Structure_Type` objects are created in the galactic store (`Ground_Ion_Cannon` names
     `Power_Generator_R`). They are killed again after placement.
  4. "Every specific marker" structures are placed.
  5. Structure pass 1 runs (structures with a remembered marker).
  6. **Pass 0 runs. The weapon is created at its structure's place in the planet list:** D's ground list (D +0x150) in
     order, then the orbital list (D +0x168), then the temporary objects. 0x4b81e0 builds this list. Orbital structures
     placed in the same pass are interleaved with the weapons.
  7. Planet-specific upgrades are created.
  8. The defender start units are placed (0x2a00b0).
  9. The attacker start units are placed.
  10. After 0x2a1eb0: 0x35cd90, 0x2aafb0, 0x2b3e00, then the victory conditions.
- Synced draws: the branch itself draws nothing. Object construction may draw (TACTICAL_GAPS §4.3) [M]. The only
  explicit draw in 0x2a1eb0 is the station-marker pick.

### 1.2 What is created, and how many [H]

- The test is made per entry S (type T) of the planet list, inside the structure loop. It has two branches:
  - S has the mode's structure slot (space: DUMMY_ORBITAL_STRUCTURE, obj +0x392; land: DUMMY_GROUND_STRUCTURE, +0x391):
    S is placed at a marker as before.
  - **Otherwise**, if all of these hold, the weapon branch runs:
    - pass 0;
    - the mode is space;
    - T has `Is_Special_Weapon_In_Space` (type +0x1e);
    - the battle planet exists (0x28a890) and its owner (+0x58) is not -1.

  So in a space battle, ground structures **without** +0x1e produce nothing.
- **Type: T itself**, the ground structure's own type, not a type it names. It is created in the tactical store with
  0x29f810 (flags 1, 0). In space the type's `SpaceBehavior` applies:
  - both FoC types get SPECIAL_WEAPON;
  - `Ground_Empire_Hypervelocity_Gun` also keeps SELECTABLE from `Behavior`;
  - neither type has a `Space_Model_Name` [H data].
- **One creation per qualifying structure; the registration then dedups.** 0x35c520 keeps a 3-slot table per owner
  (mode +0x1d8, one 0x18-byte entry per player index), indexed by `Special_Weapon_Index` (+0x1de4):
  - Empty slot: the object is stored.
  - Slot already holds a weapon whose weapon record (obj +0x158) exists: that record's int counter at +0x20 rises by 1.
    The **new object is removed silently** (Kill 0x391310, flag 1).
  - Index < 0, or index >= 3: the object is created but not registered.

  FoC allows two of each cannon per planet (`Build_Max_Instances_Per_Planet` 2). A second cannon of the same player
  therefore becomes +1 on the first one's counter, not a second object. What the counter changes was not read [L].
  Vanilla indexes: `Ground_Ion_Cannon` 0, `Ground_Empire_Hypervelocity_Gun` 1.

### 1.3 For which player [H]

- The weapon goes to the **battle planet's owner**: the planet object's +0x58, found through 0x28a890 (conflict record
  +0 → the galactic planet, which must have the planet slot +0x335). It does not go to the structure's owner, the
  record's defender or the record's attacker. Nothing tests whether that player fights in the battle.
- If the attacker owns the planet (for example when it breaks a blockade over its own planet), the attacker gets the
  weapon [H code; M scenario]. A planet with owner -1 gets none.
- The branch tests nothing about the structure itself: no dead or flag bits, no corruption, no power, no sabotage
  state.
  - **Under construction:** a queued structure is not an object yet. It joins D's ground list only when 0x400b40 creates
    the finished item (A2M_R2_RE_A §1.2), so it gives no weapon.
  - **Destroyed:** a destroyed structure is taken out of the list by Kill (0x4bb420), so it gives none either.

### 1.4 Where [H]

- The weapon takes the position (+0x78) and facing (+0x84) of the **first** object returned by the store query 0x2a9ff0
  for behaviour **0x63** (any owner, store order).
  - With no such object it is placed at (0, 0, 0) with facing (0, 0, 0).
  - 0x293d60 uses the same position rule. `Spawn_Special_Weapon` raises an error instead when the object is missing.
- 0x63 = DUMMY_SPECIAL_WEAPON_SOURCE, read from the behaviour name table. Consecutive 0x20-byte entries:
  - 0xa2b9d0 DUMMY_ORBITAL_STRUCTURE (0x60);
  - 0xa2b9f0 DUMMY_STARSHIP (0x61);
  - 0xa2ba10 DUMMY_LAND_BASE_LEVEL_COMPONENT (0x62);
  - 0xa2ba30 **DUMMY_SPECIAL_WEAPON_SOURCE (0x63)**;
  - 0xa2ba50 DUMMY_TACTICAL_SUPER_WEAPON_TARGET (0x64).
- FoC data:
  - all 37 planet backdrop `SpaceProp`s in SPACEPROPS.XML (e.g. `Alderaan_Backdrop_Large`) have
    DUMMY_SPECIAL_WEAPON_SOURCE (plus DUMMY_TACTICAL_SUPER_WEAPON_TARGET);
  - so does MISCOBJECTS `Space_Special_Weapon_Source_Marker`.

  On a campaign map the weapon therefore sits at the planet backdrop.

### 1.5 Is it a battle participant? [H unless tagged]

- **Victory:**
  - Both FoC types set `Space_Victory_Relevant No` and no `Victory_Relevant`, so 0x372230 is false for them.
  - So the weapon never blocks type 0xb, and its loss never triggers a 0xb test (BATTLE_TRANSITIONS §3.1a).
- **Damage:**
  - The valid-target test 0x5680b0 (A1_READINGS §33.2.1, test 6) refuses an object with SPECIAL_WEAPON unless it is
    also a star base, so the weapon is never a valid attack target.
  - Whether area or stray damage can reach it was not checked [L].
- **Write-back:**
  - The branch never sets +0x54 (placed structures get +0x54 := the galactic id), so +0x54 stays 0x3FFFFF
    (OBJECT_RUNTIME §1.4).
  - The parent-mode death marking (0x2b2cc0) therefore never runs for it.
  - **Whatever happens to the space object, the ground structure on the galactic map is untouched.**
- **Retreat:**
  - The weapon has no locomotor, so it is never in the space retreat list (vf8 0x532c80).
  - Space post-retreat (vf11 **0x5335f0**) keeps an object of the retreating player only when it is listed, is a FLEET
    (+0x336), or sits inside a listed flagship. Every other object of that player is killed (Kill flag 0).
  - So the weapon object is killed when the planet owner's retreat completes, with no galactic effect.
- **Population:**
  - 0x2a1eb0 never calls the population registration 0x29bcd0. Its callers are 0x29e960, 0x2a00b0, 0x2b5fe0,
    0x2bf2d0 and 0x6438a0.
  - The weapon is not a galactic unit either, so it never enters the force split. It costs nothing; `Population_Value`
    is unset (0) anyway.
- **Owner change:** 0x38e5a0 unregisters the weapon from the old owner and registers it for the new one.
- **AI:**
  - Space free-store admission takes SPECIAL_WEAPON objects of the AI player (A1_READINGS §6.1).
  - The task-force movement collection skips types with +0x1e (A1_READINGS §33).
  - So the AI owner can fire it but never moves it.

### 1.6 Port consequence

- In the campaign space battle setup, at the place of retail's structure pass 0 (after the starbase and the pass-1
  structures, before the start units), walk the planet's structures in list order: ground list, then orbital, then
  the temporary additional structures. For each structure that has no orbital-structure behaviour and whose type
  sets `Is_Special_Weapon_In_Space`:
  1. Create that type for the **planet owner**. Skip the step when the planet has no owner.
  2. Place it at the first DUMMY_SPECIAL_WEAPON_SOURCE object, else at the origin.
  3. Register it when `Special_Weapon_Index` >= 0, one slot per (player, index). A duplicate increments the kept
     weapon's counter and is removed without death effects.
- Give the object no galactic link and no population share. It is not victory relevant and not a valid target.
- Kill it when its owner's space retreat completes, with no galactic effect.
- The rule draws no synced random number of its own.
- Correction for REMAINING_GAPS §3.2 step 4: its "[M: DUMMY_TACTICAL_SUPER_WEAPON_TARGET / `Death_Star_Position`]" is
  wrong. Behaviour 99 is DUMMY_SPECIAL_WEAPON_SOURCE [H]. The step also omits the registration and the
  `Special_Weapon_Index` gate. That doc was not edited in this change.

---

## Q2. Campaign land start forces and the population list (t02)

### 2.1 Answer [H]: yes, they count

- 0x2a1eb0 places the start lists with two calls to 0x2a00b0, both with the 10th argument 0:
  - the defender at 0x2a48c1, with the 6th argument "not space", so 1 in land;
  - the attacker at 0x2a4d52, with the 6th argument 1, the look-at point in space, and outputs GOM +0x624 / +0x628.
- 0x2a00b0 calls 0x29bcd0 (population registration, BKG1_RE_ANSWERS §7) at three sites:
  - **0x2a1afa, single objects** (neither FLEET nor TRANSPORT): the created object, with the galactic object's own
    type as the company type.
  - **0x2a1ba8, container contents.** The members of a fleet in space, and in land the contents of a galactic
    transport, which are the company's member objects (A2K_T07_STORY_LOAD_RE).
    - Each member is created (0x2b3be0), gets +0x54 := the start entry's id and is collected.
    - After the container, the team helper 0x2a5a60 runs: always in space, in land when the first member's type has
      `Create_Team`.
    - Then each collected object is registered with the container's company type: 0x4f3bb0 for a transport, 0x432780
      for a fleet. So each member carries Population_Value / member count, as in BKG1 §7.
  - **0x2a0ed1, space only:** `Unique_Space_Unit` stand-ins.
- So in a campaign land battle, the attacker's first wave and the defender's surface units that 0x2a00b0 places count
  against the cap. Skirmish start forces do not count: they go through 0x29e960 with argument 8 = 0.

### 2.2 Not registered [H]

- Objects whose tactical +0x3a2 bit 2 is set. It is copied from the galactic object's +0x3a6, which marks the
  `Space_Escort_Unit_Types` objects spawned in 0x2a1eb0 steps 6 / 7. So space escorts cost nothing.
- Land contents taken by the special branches, which never reach the collected list:
  - a type with `Unique_Ground_Unit` (+0x1b60): the stand-in is created and the unit attached to it;
  - a type with `Stay_In_Transport_During_Ground_Battle` (+0x2d): it is put into the healthiest friendly
    `Can_Contain_Heroes_During_Ground_Battle` structure, else back into its transport (MCX4_T10 §18);
  - a content that itself has TRANSPORT behaviour: skipped when the 6th argument is set.
- Garrison units spawned later by structures (`Additional_Garrison_Units`, the garrison timer) take other paths, not
  read here.
- Side note: 0x2bf2d0 (space arrival) also registers its objects. This answers the "0x2a00b0 / 0x2bf2d0 not read" open
  item of BKG1 §7 for 0x2a00b0.

### 2.3 Port consequence

- The lane's default (land start forces count) is right.
- Do not register:
  - space escorts;
  - land hero stand-ins (`Unique_Ground_Unit`);
  - `Stay_In_Transport_During_Ground_Battle` units placed into a carrier or the transport.
- Members carry the share of the transport's company type.

---

## Q3. Land post-retreat (LandRetreatCoordinatorClass, vtable 0x8912b0) (t05)

### 3.1 vf8 0x534d90: the retreat list [H]

- The list (coordinator +0x30, data +0x38, count +0x40) is cleared. Then every object of the retreating player is
  queried in store order (0x2a9ff0, any behaviour, owner = that player).
- An object is added when its +0x3a0 bit 0x40 (dying) is clear and either of these holds:
  - it has a locomotor (slot +0x333, logged as "mobile");
  - its type sets `Stay_In_Transport_During_Ground_Battle` (+0x2d, logged as "safe-unit").
- There is no team / member, transport, garrison or hero test:
  - team members and garrisoned or transported units with a locomotor are listed;
  - team containers are listed too, since `LAND_TEAM_CONTAINER_LOCOMOTOR` is the container's locomotor (slot 1) [M].
- Structures, turrets, pads, markers and other objects without a locomotor stay out, unless their type sets +0x2d.

### 3.2 vf11 0x535900: who survives [H]

It runs only while a retreat is set (coordinator +0x1c != -1) and in state 3 (complete). R = the retreating player.

1. **Reinforcement-point test.** Walk the land store list GOM +0x2f0 and look for an object that meets both:
   - its type has `Reinforcement_Enabling_Radius` (+0x1e20) > 0;
   - it is friendly to R (0x39a2c0: owner R or an ally).

   FoC: only `Reinforcement_Point`, `Reinforcement_Point_Rebel`, `_Empire` and `_Underworld` (MARKERS.XML, 250.0) have
   it.
2. For every object of R (per-owner list 0x2acb70), in list order:
   - **listed** (0x340100) **and** the test in step 1 found such an object: preserved ("RETREATED");
   - has TRANSPORT behaviour (+0x337): **always preserved** ("TRANSPORT"), listed or not;
   - anything else: **killed**, Kill 0x391310 with flag 0, the normal kill, not the silent removal [M on the flag's
     effects].

Consequences:

| R's object | result |
|---|---|
| units with a locomotor (infantry, vehicles, heroes, team members, garrisoned or transported units), listed at start | kept if R or an ally holds a reinforcement point at completion, **else killed** |
| `Stay_In_Transport_During_Ground_Battle` objects without a locomotor | as the row above (they are listed) |
| landed transports / anything with TRANSPORT | kept |
| structures, turrets, build pads, captured points and markers R owns | killed |
| units created after the retreat started (not in the list) | killed |

- Killed tactical objects that have a galactic link (+0x54) mark their galactic counterpart for death (OBJECT_RUNTIME
  §2.5).
- Whether land state 1 prunes the list (dead units, lost locomotor) as space does was not read [M]. A listed unit that
  lost its locomotor would still be kept if the prune does not happen.

### 3.3 Port consequence

- Change the default ("everything without a locomotor dies except TRANSPORT; movers retreat"):
  1. Build the list at retreat start: objects with a locomotor plus `Stay_In_Transport_During_Ground_Battle` objects,
     in store order.
  2. At completion, keep the listed objects **only if R or an ally holds an object with `Reinforcement_Enabling_Radius`
     > 0**; otherwise kill them too.
  3. Always keep TRANSPORT objects.
  4. Kill everything else with the normal death.
- Corrections for BATTLE_TRANSITIONS §3.4 (not edited here):
  - "land preserves transports [M]" is now [H];
  - "retreated objects … are preserved" holds in land only under the reinforcement-point condition.
- Not read: whether the land request check (vf9 0x5354d0 → 0x2825e0) refuses a retreat without a reinforcement point.
  The parts read test only the conflict sides, the countdown, another retreating player and the story "retreat
  disabled" state (0x35f6d0).

---

## Q4. Space retreat start heading (0x533220, locomotor vf +0x218) (tru2 t02)

### 4.1 0x533220 [H]

- It queries the MARKER objects (behaviour 0x11) and takes the first one named `ATTACKER ENTRY POSITION`.
  - **Quirk:** with markers but none of that name, the loop leaves the **last** marker examined in hand and uses it.
  - No markers at all: nothing happens.
- It hands that marker to the in-game cinematic camera (0x4ab550).
- Heading = the marker's yaw + 180, wrapped into [0, 360), taken as the 2-D unit vector (cos, sin, 0).
- For every unit in the retreat list, in list order, it calls the unit's locomotor (behaviour slot 1) vf +0x218 with
  that vector.

### 4.2 vf +0x218 per locomotor [H]

- **SIMPLE_SPACE_LOCOMOTOR** (vtable 0x8aeaf8, the locomotor of every FoC capital ship, frigate and corvette; 61 uses
  in the XML) **= 0x625620**. It is a turn in place, not a heading write. In order:
  1. It builds a two-waypoint path at the unit's own position (zero length):
     - point 0 has the current yaw (+0x8c) and time **NOW** (the global logic frame 0x140b0a320);
     - point 1 has the wanted heading (z dropped, normalised) and time NOW + d;
     - d = \|angle between the two headings\| (0x20b100 / 0x20b6d0, then fabs) ÷ the turn rate 0x372560
       (`Max_Rate_Of_Turn` +0x38c × the mode multiplier) × a slowdown chosen by `Space_Layer` (type +0x16dc):

       | Space_Layer value | constant | FoC value |
       |---|---|---|
       | 4 | 0x140b16cc0 (TurnInPlaceSlowdownCorvette) | 2.0 |
       | 2 | 0x140b16cc4 (TurnInPlaceSlowdownFrigate) | 3.0 |
       | 1 or 0x20 | 0x140b16cc8 (TurnInPlaceSlowdownCapital) | 4.0 |
       | other | 1.0 | |

       The layer values are the `SpaceLayerType` bits: Capital 1, Frigate 2, Corvette 4, SuperCapital 0x20
       (OBJECT_TYPES). The constant names and values come from MOVEMENT §"Turn to face", which already has this table.
       A combined bit value (for example 5) gets 1.0 [H].
     - The path kind is 5.
  2. **Stop** (vf +0x140(0) = 0x625410): 0x42c920 clears the path stack, then the locomotor is set to the stopped
     state 0x16 when vf +0x170 agrees. With a collision owner on the pack, only the records below the owner's record
     are cleared (FIGHTER_FLYING §, Stop) [M].
  3. **Push** the turn path (vf +0x190 = 0x42c680).
  4. **Start** it (vf +0x148(0) = 0x42ced0): the top record's started flag +0x6d := 1 and the state := 0. Unless the
     cinematic camera is active, the type's sound at +0x1520 plays [M: field name not looked up].
  - The SimpleSpace update interpolates the yaw of a zero-length segment linearly between the two times (MOVEMENT
    §SimpleSpace update). So the unit turns at constant speed and reaches the heading at NOW + d.
- **FIGHTER_LOCOMOTOR** (0x5cb490): only when the fighter's locomotor state (pack +0x48) is 0x1f does it store the
  heading as the desired facing (pack +0x38..+0x40). In any other state it does nothing and keeps its path.
- **TEAM_LOCOMOTOR** (0x62fe10, squadrons): it forwards the call to each team member whose locomotor vf +0x220
  accepts.
- `LocomotorCommonClass` and `StarshipLocomotorBehaviorClass` (unused in the FoC XML): the slot returns 0 and does
  nothing.

### 4.3 Port consequence

- The default is right for SIMPLE_SPACE ships:
  - a turn in place at turn rate / layer slowdown (Corvette 2, Frigate 3, Capital and SuperCapital 4, else 1);
  - linear yaw;
  - the current path is dropped and the unit is stopped first.
- The turn path's start time is the request frame itself (NOW), so the first yaw change shows on the next locomotor
  update [M].
- Add two details:
  - fighters turn only in locomotor state 0x1f, and squadrons forward the turn to their members;
  - with no `ATTACKER ENTRY POSITION` marker, retail uses the last marker of the query, not none.
- The call does not touch higher-level orders (formation / AI move orders) [M].

---

## Function index

| RVA | what |
|---|---|
| 0x2a1eb0 | GOM post-load placement; the special-weapon branch is in structure pass 0 (registration call at 0x2a4011) |
| 0x4b81e0 | planet structure list: D +0x150 ground list, then D +0x168 orbital list |
| 0x28a890 / 0x28a7a0 | battle planet (galactic object) / conflict record of the current battle |
| 0x2a9ff0 | store query by behaviour / owner (used with 0x63, 0x11, owner filters) |
| 0x29f810 | Create_Object |
| 0x35c520 | register special weapon (per-owner 3-slot table, mode +0x1d8; dedup by Special_Weapon_Index) |
| 0x293d60, 0x56480 | skirmish / standalone space special weapons (not campaign; 0x28ae40 gate) |
| 0x6aa790 | Lua `Spawn_Special_Weapon` |
| 0x38e5a0 | owner change (re-registers special weapons) |
| 0x5680b0 | valid-target test (special weapons refused unless star base) |
| 0x2a00b0 | start-unit placement; population registration sites 0x2a1afa, 0x2a1ba8, 0x2a0ed1 |
| 0x29bcd0 | population-list registration |
| 0x534d90 / 0x535900 | land retreat vf8 (list) / vf11 (post-retreat) |
| 0x532c80 / 0x5335f0 | space retreat vf8 / vf11 |
| 0x533220 | space retreat start heading (marker lookup, vf +0x218 per unit) |
| 0x625620 / 0x625410 | SimpleSpace vf +0x218 turn in place / vf +0x140 stop |
| 0x42c680 / 0x42ced0 | locomotor push path (vf +0x190) / start path (vf +0x148) |
| 0x5cb490 / 0x62fe10 | Fighter / Team locomotor vf +0x218 |

## Open items

- What the weapon record counter +0x20 (incremented for a duplicate cannon) changes in firing or recharge [L].
- Whether the campaign weapon starts charged or charging; the record's ready frame +0xc at creation was not traced.
- Whether area / stray damage can reach the space weapon object (it is not a valid target) [L].
- Land retreat: whether state 1 prunes the list, and whether vf9 refuses a retreat without a reinforcement point.
