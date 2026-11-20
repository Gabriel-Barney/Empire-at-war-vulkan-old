# mcx8 t12: retail behaviour of the `[inferred]` object, type and position Lua bindings

Purpose: the corpus mods (vanilla FoC, Thrawn's Revenge and others) call these bindings, and the port's versions in
`engine/lua/src/bind_objects.cpp` carry an `[inferred]` marker. Each section gives what retail StarWarsG.exe does and
whether the port matches. Sources: the decompiled retail functions in `re_reference/ghidra/StarWarsG_decompiled.zip`
(`Global.c`, read 2026-11-18; addresses confirmed against the registration functions 0x56d4c0 GameObjectWrapper,
0x604a10 GameObjectTypeWrapper, 0x60ebd0 PositionWrapper), the exact message texts from
`docs/re/ghidra/strings_xrefs.csv`, plus the specs cited per section. Behaviour only, in words.

Conventions as in `MCX8_BINDINGS_PLAYERS_STORY.md`: "error" = 0x248190 (no value, the script ends at the end of its
service pass), "warning" = 0x248620 (no value, the script continues). Lua class ids: GameObjectWrapper 0x140a43b18,
AITargetLocationWrapper 0x140a44280, GameObjectTypeWrapper 0x140a44270, PlayerWrapper 0x140a44260, string 0x140a157d0,
number 0x140a157c0, bool 0x140a157b0. Behaviour slot numbers: OBJECT_RUNTIME.md §4.3 ids (slot byte = obj +0x332 + id).
"Representative type" = 0x3973b0 (A1_READINGS §29.2; the port's `representative_type` in bind_objects.cpp). Retail
texts often have **two spaces** after a full stop; they are quoted exactly.

## GameObjectWrapper.Despawn (0x5752a0) [H]

- Dead wrapper: warning "GameObjectWrapper::Despawn -- this object is dead!".
- No TEAM behaviour: first detaches the game-scoring manager (TheGameScoringManager singleton 0x140a109a0, 0x488890)
  from the object's signals 0x1a, 0x25, 0x27, 0x22, and from those of any named or generic hero (type +0x13 / +0x14)
  in its contained-objects list (obj +0xF8); does the same for the object's parent-mode counterpart when it has one
  (obj +0x54 ≠ 0x3FFFFF). Then Kill 0x391310(obj, 1): the plain removal (no `Die`, so no death clone, death
  explosion, `Destruction_Survivors`, respawn or bounty; STATION_HANGARS_SOUND §10, STORY_REWARDS_GALACTIC).
- TEAM behaviour: the same for each member, **last member first**; the container itself is not killed here. A team
  with no members: nothing.
- Returns nothing.

Port: matches (reverse member order, removal without death effects, dead warning via `DeadRule::WarnNothing`). The
scoring detach means a despawned unit must not count as a loss or kill in game scoring [M for the signals' meaning];
check that the port's `destroy_object` path does not feed GameScoring.

## Spawn_Unit (0x6a1f90 tactical, 0x6a2520 galactic) [H]

Tactical (mode vf +0xF0 true):
- Fewer than 3 arguments: error "LuaSpawnUnitCommandClass -- Invalid number of parameters." (more are ignored).
- Argument 3 not a PlayerWrapper: error "LuaSpawnUnitCommandClass -- Invalid parameter for player." (tested first).
- Argument 1 not a GameObjectTypeWrapper (a type-name string is not accepted): error
  "LuaSpawnUnitCommandClass -- Invalid parameter for type.".
- Argument 2 through the position extractor 0x4990b0; no position: error
  **"LuaSpawnUnitCommandClass -- Invalid position parameter."**
- Placement: OBJECT_SCRIPTS §10.4.

Galactic (any other mode): the same order with the prefix "Galactic_Spawn_Unit --": "Invalid number of parameters."
(fewer than 3), "Invalid parameter for player.", "Invalid parameter for type.", and argument 2 must be a live game
object with a PRODUCTION behaviour (6), i.e. a planet, else "Galactic_Spawn_Unit -- Invalid planet parameter.". A
failed create is a warning "Galactic_Spawn_Unit -- Create returned NULL for %s at planet: %s." and returns nothing.

Port: differs:
- The location error text: use "LuaSpawnUnitCommandClass -- Invalid position parameter." in tactical, and
  "Galactic_Spawn_Unit -- Invalid planet parameter." in galactic (a non-planet location there is that error, not a
  free-position spawn).
- In galactic, use the `Galactic_Spawn_Unit --` texts for the count / player / type errors.
- Test the player (argument 3) before the type (argument 1); accept 3 or more arguments (`c.nargs < 3`).

## Create_Generic_Object (0x6a1d20) [H]

- Argument count ≠ 3: error "Create_Generic_Object -- Invalid number of parameters.  Expected 3, got %d.".
- Argument 3 not a PlayerWrapper: error "Create_Generic_Object -- Invalid type for parameter 3.  Expected player."
  (tested **before** the type and the position).
- Argument 1: a GameObjectTypeWrapper, or a string looked up as a type name (0x407c00). Neither, or an unknown name:
  error **"Create_Generic_Object -- Invalid type or unnown type name for parameter 1."** (sic, "unnown"). It never
  returns nil for an unknown name.
- Argument 2 through 0x4990b0; no position: error "Create_Generic_Object -- Could not deduce a creation position
  from parameter 2.".
- Creates one object with the generic create 0x29f810 (current mode's store, the player's id, the position, a
  zero facing vector, flag 1); no company expansion. A null result: error "Create_Generic_Object -- Internal game
  logic error creating %s.  Possibly check _logfile.txt.". Returns the object's wrapper.
- Facing: the create gets an all-zero facing vector; what orientation that yields is not settled here [L]. The port's
  +X (TACTICAL_COMBAT §3.1.1) stays.

Port: differs:
- An unknown type name (and a non-type, non-string argument 1) is the error above, not nil; replace the port's
  "Invalid type for parameter 1. Expected type or type name." text with it.
- Order: count, player, type, position.
- Texts: two spaces in "parameters.  Expected 3" and "parameter 3.  Expected player.".

## Hide_Object (0x542970) [H]

- Not tactical (mode vf +0xF0): error "LuaTransitionCinematicTargetKey -- Command only valid in a tactical game."
  (retail's copy-pasted prefix).
- Fewer than 2 arguments: error "LuaHideObject -- Expected gameobject pointer parameter 1, float parameter 2"
  (more are ignored).
- Argument 1 not a GameObjectWrapper: error "LuaHideObject -- Invalid parameter 1, expected a game object"; retail
  then carries on with a null object and writes through it (a crash) [M].
- Argument 2 not a number: error "LuaHideObject -- Invalid parameter 2, expected a float", and the object is
  **hidden anyway** (the flag defaults to true).
- Flag = number **> 0**. Story-manager hide 0x44b420 (model) and obj +0x3a2 bit 0 := flag (STORYB_T17_HIDDEN_UNITS
  §1.1). Returns nothing.

Port: differs:
- Hide only when the number is > 0 (the port hides on any non-zero, so a negative value hides instead of showing).
- Mode gate text: add `{"Hide_Object", "LuaTransitionCinematicTargetKey -- Command only valid in a tactical game."}`
  to `kGateTexts` (the dispatcher now prints the generic "… only supported in tactical modes").
- Accept 2 or more arguments. A non-number argument 2: report the error and still hide. A bad argument 1: keep the
  port's stop (avoids retail's crash; a `[port]` choice).

## PositionWrapper.Get_XYZ (0x60ed40) [H]

Returns three numbers: x, y, z of the position (floats +0x28, +0x2c, +0x30 widened to double). No arguments read.

Port: matches.

## GameObjectWrapper.Get_Name (0x578e00) [H body, M meaning]

- Reads the wide string at the object's SelectionDataPack (obj +0xB0, ctor 0x55be30) +0x40 and converts it to UTF-8.
  When the converted string is longer than 2 characters, its **first and last characters are dropped**.
- That string is the object's own **ship name**: the only writer found (0x3c2440, run once per object from a
  behaviour service 0x3c2310) copies the parent-mode object's name, or takes a random name for the type from
  TheShipNamesManager (GameConstants `ShipNameTextFiles`, e.g. Star_Destroyer, Calamari_Cruiser; none in multiplayer,
  0x4e5b30). The galaxy tooltip helper 0x371a50 shows this name, else the type's display text.
- An object without a ship name, or a dead wrapper: returns the **empty string** "" (not nil). It never returns the
  type name.

Port: differs: the port returns the object's type name. Retail returns the ship name ("" for nearly every object).
Change: return the object's ship name when the port has one (a typed name from the `ShipNameTextFiles` lists, first
and last character removed when longer than 2), else ""; a dead wrapper also returns "". Scripts that want the type
name call `Get_Type().Get_Name()` (the vanilla scripts do).

Port status (ref5 t04): the binding is written as above and reads ObjectBridge::ship_name; no host sets that hook yet, so
it returns "" for every object. The questions that settle the name source are in `REF5_T04_RE_QUESTIONS.md`.

## GameObjectWrapper.Is_Category (0x57b7e0) [H]

- Dead wrapper: returns nothing.
- Argument count ≠ 1: error "GameObjectWrapper::Is_Category -- invalid number of parameters. Expected 1, got %d.".
- Not a string: error "GameObjectWrapper::Is_Category -- parameter 1 is not a valid string.".
- The string goes through the GameObjectCategoryType converter (0x140b31020 vf +0x20). Conversion fails: error
  **"GameObjectWrapper::Is_Category -- unrecognized category %s."** (no value).
- Else a bool: converted mask & `CategoryMask` (+0x1648) of the object's **representative type** (0x3973b0) ≠ 0.

Port: differs:
- An unknown category is the error above, not `false` (a category that converts to 0 still gives false).
- Test the representative type's `CategoryMask` (`representative_type(c, bridge(c), h)`), not the store type: they
  differ for tactical team containers and transports.

## GameObjectWrapper.Take_Damage (0x581710) [H]

- Argument count not 1 or 2: error "GameObjectWrapper::Take_Damage -- invalid number of parameters.  Expected 1 or
  2, got %d." (tested **before** the dead check).
- Dead wrapper: warning "GameObjectWrapper::Take_Damage -- this object is dead!".
- Argument 1 not a number: error "GameObjectWrapper::Take_Damage -- invalid type for parameter 1.  Expected number.".
- With 2 arguments: argument 2 not a string: error "GameObjectWrapper::Take_Damage -- invalid type for parameter 2.
  Expected string." (two spaces after "2."). The string is looked up in the **global** HardPoint type table (0x471360
  on 0x140a2a0b0, CRC of the upper-cased name; OBJECT_TYPES "Hardpoint names"), not in the object's own list. Not
  found: error "GameObjectWrapper::Take_Damage -- unknown hard point %s." and no damage (R1_T01_HARDPOINT_RUN).
  Found: the record's own name is passed on as the hit name.
- Damage: `Take_Damage` 0x3a9e30 with damage kind 0, no source, the amount, the hit name (or none).
  A TEAM container passes the **full amount** to each member, first to last; the container takes none itself
  (STORYB_T17 §3.1). Returns nothing.

Port: differs:
- Texts: two spaces after "parameters." / "parameter 1." / "parameter 2." in the three argument errors.
- A hardpoint name that exists as a HardPoint type but is not on this object is not "unknown hard point" in retail; it
  is passed to the damage call. What 0x3a9e30 then does with a name the victim does not carry is not read here [L];
  the port's stop for such names is stricter than retail.
- Team containers: apply the amount to every member (in member order), not to the container. Check that
  `ObjectBridge::take_damage` / the direct health path does this for a TEAM object.
- Order: the argument-count error comes before the dead-wrapper warning (the port's dispatcher warns first).

## GameObjectWrapper.Turn_To_Face (0x581cd0) [H]

- Dead wrapper: returns nothing.
- Argument count ≠ 1: error "GameObjectWrapper::Turn_To_Face -- invalid number of parameters.  Expected 1, got %d.".
- No position from argument 1 (0x4990b0): error **"GameObjectWrapper::Turn_To_Face -- could not deduce a position to
  face."**
- Then the mode's movement coordinator system (mode vf +0x228; none: error "GameObjectWrapper::Turn_To_Face --
  unable to locate movement coordinator system.  This is very bad.") and a new coordinator (none: "… unable to create
  a movement coordinator for this attack.  This is also very bad."). A non-team object the coordinator system refuses
  (0x425e30): error "GameObjectWrapper::Move_Internal -- unable to make object %s move." (type name). A TEAM object
  enters each member. The facing move is built and a blocking status returned.

Port: differs (text only): use "GameObjectWrapper::Turn_To_Face -- could not deduce a position to face." and two
spaces in "parameters.  Expected 1".

## GameObjectWrapper.Override_Max_Speed (0x57e590) [H]

- Dead wrapper: returns nothing.
- No argument: error "GameObjectWrapper::Override_Max_Speed -- Expected a number or boolean parameter.".
- Argument 1 a bool: `false` clears the override (0x38f8b0: locomotor pack obj +0xA8, flag +0x29c := 0, value
  +0x2a0 := 0); **`true` does nothing**.
- Argument 1 a number: override := that value (0x3a8c90: +0x2a0 := value, +0x29c := 1).
- Any other type (string, nil, table): **nothing, no message**.
- An object without that pack (obj +0xA8 null): nothing. Returns nothing.

Port: differs: `true` must not clear the override; an argument that is neither number nor bool must do nothing
silently; only a call with no argument is the error.

## GameObjectWrapper.Get_Hull and Get_Health (both 0x578c30) [H]

- Both names are registered to the **same function** (0x56d4c0), so `Get_Health` is exactly `Get_Hull`, including the
  "Get_Hull" in its message.
- Not tactical (mode vf +0xF0): error "GameObjectWrapper::Get_Hull -- this command is only supported in tactical
  modes." (with the final full stop).
- Dead wrapper: returns nothing.
- Else `Get_Min_Health_Fraction` 0x396470 (ABILITIES_HARDPOINTS §1.3) as a number.

Port: differs:
- `Get_Health` has no mode gate: add `GameObjectWrapper.Get_Health` to `kMethodGates` (tactical).
- Gate texts: add `kGateTexts` entries for `Get_Hull` and `Get_Health`, both
  "GameObjectWrapper::Get_Hull -- this command is only supported in tactical modes." (the generic text lacks the full
  stop and would print "Get_Health").
- The non-combat fallback (health / Tactical_Health) only matters inside tactical modes now; it stays a port
  approximation of 0x396470 for objects outside the combat system.

## GameObjectWrapper.Get_Shield (0x579980) [H]

- Not tactical: error "GameObjectWrapper::Get_Shield -- this command is only supported in tactical modes.".
- Dead wrapper: returns **0** (a number), no message.
- Else 0x3976f0: 0 without a SHIELDED behaviour or its pack; else current shield (pack +0xfc) clamped to
  [0, max] divided by max shield (0x3725f0), clamped to [0, 1]; max ≤ 0 gives 0.

Port: differs:
- Dead wrapper: return 0 silently (a new dead rule, like `WarnZero` without the warning).
- Gate text: add `{"Get_Shield", "GameObjectWrapper::Get_Shield -- this command is only supported in tactical
  modes."}` (final full stop).
- The fallback "1 when the type has shields" for objects outside the combat system is a port approximation of
  current / max (full shields); keep it.

## GameObjectWrapper.Is_Ability_Ready (0x57b580) [H]

- Argument count ≠ 1: error "GameObjectWrapper::Is_Ability_Ready -- invalid number of parameters.  Expected 1, got
  %d." (tested before the dead check).
- Dead wrapper: returns nothing.
- Not a string: error "GameObjectWrapper::Is_Ability_Ready -- invalid type for ability 1.  Expected string.".
- Name not a UnitAbilityType (converter 0x140b310c0): error "GameObjectWrapper::Is_Ability_Ready -- unknown ability
  %s.".
- The object lacks the ability (0x3986b0): **warning** "GameObjectWrapper::Is_Ability_Ready -- Object %s does not have
  ability %s." (upper-case type name, the name as given), then returns `false`.
- Black-market gate 0x39bbd0 fails: `false`.
- `Can_Activate_Unit_Ability(obj, t, 1)` 0x39b480 true, or the object's pending targeted ability (obj +0xF0 → +0x154)
  is t: `true`; else `false`.

Port: differs:
- An unknown ability name is the error above (the port returns false).
- Not having the ability: emit the warning text, then return false.
- Texts: two spaces in "parameters.  Expected 1" and "ability 1.  Expected string.".
- The ready rule itself (ability system `is_ready`, fallback gate + not running) matches.

## GameObjectWrapper.Get_Game_Scoring_Type (0x57d0c0) [H]

- Dead wrapper: returns nothing.
- The type, first hit wins:
  1. TEAM behaviour with team data +0x58 set: the **company type** (e.g. `Imperial_Stormtrooper_Squad`, not
     `Stormtrooper_Team`; A1_READINGS §29.2);
  2. TRANSPORT behaviour: the carried company type (transport data +0x48, A1_READINGS §32.2); when that is null and the
     object has a parent-mode counterpart (obj +0x54 ≠ 0x3FFFFF), that counterpart's representative type;
  3. otherwise the object's representative type (fleet → its type +0x70, team → `Create_Team_Type`, else own type).
- Returns a GameObjectTypeWrapper of it.

Port: differs: the port returns the store type. Change: team container → its company type; transport → its carried
company type (else the parent-mode object's representative type); else `representative_type`. Galactic company
objects already have the company type, so galactic results do not change.

## GameObjectWrapper.Reset_Ability_Counter (0x57f430) [H]

- **Takes no arguments**: the arguments are never read (no count or type check, so no argument-count message exists).
- Dead wrapper, or no ABILITY_COUNTDOWN behaviour slot (0x46, obj +0x378 = −1): nothing.
- Slot set but the behaviour cannot be fetched: error "GameObjectWrapper::Reset_Ability_Counter -- could not obtain a
  pointer to AbilityCountdownBehaviorClass".
- For **every** ability id 1..76: when the counter is **not** zero (`Is_Counter_Zero` 0x42f8e0 false, i.e.
  counter ≥ 1), counter := maximum := 1 frame (0x42fa70). So every running recharge ends on the next frame; idle
  counters are left alone.
- Returns nothing.

Port: differs:
- Remove the count and string checks (and their messages); ignore all arguments.
- Reset all ability ids, not only the named one.
- Invert the condition in `AbilitySystem::reset_counter`: set counter = maximum = 1 when the counter is **not** zero
  (the port now sets it only when zero; ABILITIES_HARDPOINTS §5 row "when 0x42f8e0 allows" is wrong in direction).

## GameObjectTypeWrapper.Get_Combat_Rating (0x605690) [H]

`AI_Combat_Power` at run time (0x370e60, A1_READINGS §29.3): in a tactical mode (mode vf +0xE0 ≠ 0), when the type has
`Company_Units` (else `Squadron_Units`) and the first list entry's type has `Create_Team`:
clamp(`Max_Squad_Size` / list count, 0, 1) × the load-derived power (+0xdf0); otherwise the load-derived power
(a company's is its members' sum). Returned as a number.

Port: differs in tactical only: apply the squad-share factor there (the same rule as `battle_ai.cpp` near the 0x370e60
comment); galactic matches the `ai_combat_power` column.

## GameObjectTypeWrapper.Get_Tactical_Build_Cost (0x6059a0) [H]

0x3743f0: in skirmish (0x28afb0) `Tactical_Build_Cost_Multiplayer` (+0x9fc); otherwise `Tactical_Build_Cost_Campaign`
(+0x9f8), falling back to Multiplayer when Campaign < 1 (BKG1_RE_ANSWERS "Price functions"). Returned as a number.

Port: differs: use Campaign (Multiplayer when < 1) outside skirmish; Multiplayer only in skirmish.

## GameObjectTypeWrapper.Get_Max_Range / Get_Min_Range (0x605750 / 0x605810) [H]

The type's raw `Targeting_Max_Attack_Distance` (+0x500) / `Targeting_Min_Attack_Distance` (+0x504) as a number; no
deployed or ability factor.

Port: matches (as long as the columns hold the raw tag values).

## GameObjectTypeWrapper.Is_Hero (0x606520) [H]

H = 0x6c00c0(T): the type of the first `Company_Units` entry, else of the first `Squadron_Units` entry, else T itself.
Returns `true` when H has `Is_Named_Hero` (+0x13) **or** `Is_Special_Weapon_In_Space` (+0x1e); else `false`.

Port: differs: test the first company member (else first squadron member, else the type) instead of the type itself,
and also return true for `Is_Special_Weapon_In_Space`.

## GameObjectWrapper.Can_Garrison (0x573e80) [H]

Order (L1_S4_REMAINING §2.5 and §3.5 agree):
1. Dead wrapper: nothing.
2. The object has no GARRISON_UNIT (0x51): `false`. The object is already garrisoned / in limbo (obj +0x3a7): `false`.
   (Both before the argument checks.)
3. Argument count ≠ 1: error "GameObjectWrapper::Can_Garrison -- invalid number of parameters.  Expected 1, got %d.".
4. Argument 1 neither a GameObjectWrapper nor an AI target with an object: error "GameObjectWrapper::Can_Garrison --
   invalid type for parameter 1.  Expected game object or AI target." **and then** the error below as well.
5. Target object null (dead): error "GameObjectWrapper;:Can_Garrison - target is already dead." (sic, ";:" and one
   dash).
6. `false` when: the target has none of GARRISON_STRUCTURE (0x50), GARRISON_VEHICLE (0x4F), GARRISON_HOVER (0x56); or
   the target's owner exists, is not friendly to the object's owner (relation ≠ 0, 0x2823c0) and its faction is not
   `Is_Neutral` (+0x108); or the `Garrison_Category` masks (+0x1658) of the target type and the object type share no
   bit while both are non-zero (0x38f9a0; a zero mask on either side matches).
7. Capacity: free (0x3954b0: target `Num_Garrison_Slots` − Σ occupants' `Garrison_Value`, DUMMY_UPGRADE occupants
   skipped) − units already walking in (target +0x210 → +0x34) − the object's representative type `Garrison_Value`
   (+0x23ac) must be **> 0**, else `false`.
8. Else `true`.

Port: differs:
- Return false first when the object has no GARRISON_UNIT or is already garrisoned.
- Holder test: the three behaviours only (not the `CanContainGarrison` property).
- Owner test: false only for a non-friendly, non-neutral owner (the port requires the same owner or an empty host).
- Mask test: target `Garrison_Category` vs. the object's `Garrison_Category`, zero on either side passes (the port
  uses the object's `CategoryMask`).
- Add the capacity test (strictly more free slots than the unit's `Garrison_Value` after reservations).
- A dead target is the error text above, not `false`; a wrong argument type gives both errors.
- Texts: two spaces after "parameters." and "parameter 1.".

## GameObjectWrapper.Eject_Garrison (0x5759b0) [H]

Dead wrapper: nothing. Looks up the host behaviour in the order GARRISON_VEHICLE (0x4F), GARRISON_STRUCTURE (0x50),
GARRISON_HOVER (0x56); none: nothing. Else 0x444ab0(behaviour, host, 0): an eject request for every occupant
(L1_S4_REMAINING §3.5 "Leaving"). No arguments read. Returns nothing.

Port: matches in effect (a Leave_Garrison per occupant is the same eject request). Edge: an object that only has the
`CanContainGarrison` property and none of the three behaviours does nothing in retail.

## GameObjectWrapper.Get_Contained_Heroes (0x577fd0) [H]

- Dead wrapper: error "GameObjectWrapper::Get_Contained_Heroes -- this object is already dead." (final full stop).
- Builds a table, **one level only, no recursion**:
  1. each entry of the object's contained-objects list (obj +0xF8: garrison occupants; fleets keep their units here
     too [M]) whose **own type** has `Is_Named_Hero` (+0x13), list order;
  2. then, when the object has TRANSPORT (5), each entry of the transport contents (pack +0xC8 → +0x08) with that flag.
- An entry whose object is dead (obj +0x3a0 flag 0x02) is still added, as a dead wrapper.
- Returns the table (empty when nothing matches). (`Contains_Hero` 0x574fb0 is the recursive one, 0x38fa20.)

Port: differs:
- Do not recurse; take only direct contents (contents list, then transport contents) whose own type is
  `Is_Named_Hero` (drop the `Company_Units` member test: shipped hero companies carry the flag themselves).
- `DeadRule::Error` text: retail ends with a full stop ("… this object is already dead."); the port's generic text has
  none (the same holds for `Contains_Hero`).

## GameObjectWrapper.Destroy_Contained_Objects (0x575410) [H]

- Dead wrapper: nothing.
- Argument count ≠ 1: error "GameObjectWrapper::Destroy_Contained_Objects -- invalid number of parameters.  Expected
  1, got %d.". Not a number: error "… -- invalid type for parameter 1.  Expected number.".
- No FLEET behaviour (4): error "GameObjectWrapper::Destroy_Contained_Objects -- this function may only be used on
  fleets.".
- n = the fleet's unit count; keep = (int)((1 − f) × n + 0.5) (truncation). keep > n (f < 0): nothing. Else, with
  keep' = max(keep, 0), while n > keep': n −= 1, draw an index with the **synced RNG** `Get_Int(0, n)` inclusive
  (0x1ffb40 on 0x140a13e24), take that fleet unit, remove it from the fleet (0x434d30) and Kill it (0x391310; the flag
  argument is not visible in the decompile [L]). Stops early when the lookup returns no unit.
- So f = 1.0 (FleetEvents.lua) destroys all units; f = 0.5 on 3 units destroys 1 (keep = 2).

Port: differs:
- Fleets only: the error above for any non-fleet object.
- Count: destroy n − max((int)((1 − f) × n + 0.5), 0), nothing when f < 0 (the port destroys round(n × f), 2 of 3 at
  0.5).
- Victims: uniform random among the remaining fleet units, one synced draw per victim (tier 2: same distribution; the
  draw order is the port's choice), not the first ones in content order.
- Texts: two spaces after "parameters." and "parameter 1.".

## GameObjectWrapper.Move_To in galactic (0x57e520 → 0x57d8c0 "Move_Galactic") [H]

0x57e520 sends galactic modes (mode vf +0xE0 = 0) to 0x57d8c0 and tactical ones to Move_Internal 0x57da90.
Galactic:
- Dead wrapper: nothing.
- Argument count ≠ 1: error "GameObjectWrapper::Move_Galactic -- invalid number of parameters.  Expected 1, got %d.".
- Argument 1 must be an AI target with an object or a GameObjectWrapper whose object has PLANET (3); else error
  **"GameObjectWrapper::Move_Galactic -- invalid type for parameter 1.  Expected planet."**
- The calling object must have FLEET (4); else error "GameObjectWrapper::Move_Galactic -- this function is only valid
  on fleets.".
- Else a fleet move to that planet (0x6af230, kind 5) and its blocking status is returned.

Port: differs: in `galactic_move_to` use the Move_Galactic texts; require exactly one argument that is a planet (a
plain position or a non-planet object is the "Expected planet." error; no second, number argument); refuse callers
that are not fleets with the fleets-only error.
