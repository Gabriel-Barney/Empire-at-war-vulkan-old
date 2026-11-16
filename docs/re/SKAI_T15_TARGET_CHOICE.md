# skai t15: what holds a land Destroy_Structure back near enemy units, and the land skirmish start structures in the threat grid (RE, 2026-11-16) [H unless tagged]

Answers `SKAI_T15_TARGET_CHOICE_QUESTIONS.md` Q1a, Q1, Q5, Q3, Q2, Q4 (asked in that priority order). Read with ReVa
(Ghidra, `/StarWarsG.exe`, FoC Steam x64, image base 0x140000000) and the shipped XML / Lua
(`extracted/foc_data_text.zip`). Addresses are RVAs. [H] = read in the binary or the shipped data; [M] = one inferred
link; [L] = not checked. No decompiled code is reproduced. Ghidra comments were added at 0x6b8710, 0x4f4c60 and 0xef12d.

Functions: 0x6b8710 (land goal slot 9), 0x724c40 (unit-target constructor), 0x6b8b40 (land perception slot 11),
0x17d070 (constant null), 0x665760 (region target vf+0x38), 0x7222c0 (commit step 1), 0x722a30 (maintenance),
0x692860 (AllowedTime), 0xeed80 (skirmish start), 0x295cf0 (team-game player setup), 0x42df80 (pad pre-build),
0x29f810 (generic create), 0x2b3670 (GOM add hook), 0x4f5950 (UNIT_AI part of the hook), 0x4f6070 (UNIT_AI service),
0x4f4c60 (UNIT_AI pack reset), 0x6537d0 (grid registration), 0x653100 (grid entry constructor), 0x659760
(re-projection), 0x656d10 / 0x656a10 (entry out of / into the cells), 0x6585f0 (grid signal handler), 0x658930 /
0x4f4ba0 (unregister), 0x365540 / 0x3b86f0 / 0x35eb40 (land mode vf+0x250 / vf+0x248 / FOW setup), 0x4dcfd0 / 0x4dd180
(threat grid create / delete), 0x293d60 (special weapons). Land mode vtable 0x8673a0 (vf+0x180 = 0x3b7f50, as in
A1_READINGS §4).

## Summary

- **Q1a: no.** Every game-object target in the tactical target list, structure or mobile unit, is a
  `TacticalUnitAITargetLocationClass` (vtable 0x8ce4a0). Its vf+0x38 is 0x17d070, which returns null. So land slot 9 never
  runs the flood for an `Enemy_Structure` or `Enemy_Unit` goal, and `Destroy_Structure`'s `Low_Threat` never skips the
  vehicle factory, whatever stands near it. Only hint-zone region targets carry a reachability object. The port's
  `has_reachability()` (region targets only) is the retail rule: no change.
- **Q1: moot.** Retail does not test structures in slot 9, so the zone danger of zone 85 does not matter for this goal.
  No estimate was made.
- **Q5: yes, land does the same, and it covers every base structure that is pre-built at the start.** The skirmish start
  0xeed80 is mode-generic. In land it runs the team-marker swap 0x295cf0, which creates the base pads and at once
  pre-builds each pad's structure through the generic create (0x42df80 → 0x29f810 → GOM add hook). Then it deletes the
  threat grid and builds a new one (land vf+0x250 = 0x365540, vf+0x248 = 0x3b86f0). The pre-built structures (Command
  Center / palace, barracks, factories, research facility, communications array, power generator, base shields,
  turbolaser towers) were registered in the deleted grid, so the UNIT_AI service never registers them again:
  - **owner lists (+0x88): absent for the whole battle** [H code; matches the three retail observations of SKAI_T06 §7 /
    §9]. Every reader of the owner lists misses them: `DistanceToNearest*`, `Game.Force[Unnormalized]`, player
    `Friendly/EnemyForce[Unnormalized]`, `Find_Nearest`.
  - **live cell lists (+0x10) and threat layer: present** from the structure's first UNIT_AI service [M]. The hook stored
    an uninitialised "last transform" (0x4f4c60), so that service's transform test fails and re-projects the entries into
    the new grid. This settles SKAI_T15_TARGET_LOCATION_FORCE D4 the port's way. It also contradicts SKAI_T22_SPACE_AI_RE
    Q1.1 ("not in the live cell lists") for the space start station.
  - Not covered: the start units (created after the rebuild), map-placed objects (no hook; registered at their first
    service in the new grid), and anything built or spawned during the battle, a rebuilt Command Center or barracks
    included.
- **Q3: neither.** Commit step 1 (0x7222c0) re-tests only `Applies` and then evaluates the bare equation: no slot 9 test,
  no learning terms. So the 7421 displacement needs a `Tactical_Targeted` proposal whose desire exceeded the incumbent's
  effective desire (16.00 at desire 15.0). Which proposal that was cannot be read from the binary.
- **Q2: the only enemy-near-target rule is the contrast escort list.** For a unit target the escort list is
  `Target.Location.EnemyForceUnnormalized`. It is used at the PotentialPlan solve (slot 10) and at the re-test at every
  commit (0x6c47c0), and it sizes the task force rather than forbidding the goal. In retail the factory's escort list
  holds the merc teams' share of zone 85's cells, plus any Underworld structure entries there. The port reads that
  location as 0 (SKAI_T15_TARGET_LOCATION_FORCE §5), so its escort list is empty. No other gate exists: the free-store
  stage choice is a preference, and `MinimumTotalForce` is not set.
- **Q4: no.** `Time_Limit` never ends or weakens a running tactical goal: the maintenance time test is galactic-only, and
  AllowedTime is `Time_Limit` only for a goal without a PotentialPlan. The port matches.

---

## Q1a. Does a game-object target carry a reachability object in land slot 9?

**No, for every game-object target** [H].

- Land slot 9 (0x6b8710) does two tests in order:
  1. it skips the candidate when the (type, target) goal is already in the active set;
  2. it calls the target's vf+0x38. Only when that returns an object does it call that object's slot 6 (flood pick, mask
     −1, no lists) at the goal type's `Reachability` (goal type +0x2c). It skips the candidate when the pick is null.
  The candidate is the instantiated goal: +0x48 holds the type, +0x58 the target (AI_GALACTIC §1.6).
- Two target classes are in the list (perception +0xe0):

  | Class | vtable | vf+0x38 | Made by |
  |---|---|---|---|
  | `TacticalUnitAITargetLocationClass` (0x38 bytes) | 0x8ce4a0 | 0x17d070: returns null | 0x724c40 only, called from land slot 11 (0x6b8b40), space slot 11 (0x6b9b60) and space init (0x6ba320) |
  | `TacticalRegionAITargetLocationClass` | 0x8ce548 | 0x665760: returns +0x30, the `ZoneReachabilityClass` made with the region | 0x725310 / 0x725340 / 0x7253d0 |

- **Structures and mobile units are the same class**, so neither is ever skipped by slot 9. Nothing is resolved, cached or
  flooded for them. The flood set (A1_READINGS §25.2, cached per level for 10 s) exists only for hint-zone regions.
- Where a game-object target does get a zone: the tactical free store's stage choice (Setup A 0x7287a0) uses the region
  at the goal target's position when the target has no reachability object (A1_READINGS §25.3). That is a stage
  preference, not a skip (Q2).

## Q1. Zone danger around the factory

Not needed: Q1a removes slot 9 from the question for structure targets. The port measurement in the questions file
(every structure target has no reachability object, so `blocked()` returns false at once) is retail's rule. The zone danger
of zone 85 matters only for region targets near the base: `Clear_Infantry`, `Clear_Vehicles` and `Land_Scout_Area` on
"New Zone 86" / "New Zone 87". With Q5, that danger includes the Underworld base structures' threat-layer values [M].

## Q5. Land skirmish start structures and the threat grid

### 5.1 The same delete and rebuild runs in land [H]

The skirmish start 0xeed80 acts on whatever mode is current (global mode pointer 0x140b15418). Its order, as for space
(SKAI_T22_SPACE_AI_RE §1.1.2):
1. player setup **0x295cf0** when the lobby record's team flag (+0xb5) is set, else 0x294fa0;
2. 0x2a74f0;
3. **mode vf+0x250(0)**;
4. **mode vf+0x248**;
5. credits, then the per-player start forces 0x2a4fc0.

For land, read in the land mode vtable 0x8673a0:
- **vf+0x250 = 0x365540**: frees the FOW grids and the FOW renderer, then 0x4dd180(0) deletes the threat grid and clears
  the global. This is the same function space reaches through 0x4d9850.
- **vf+0x248 = 0x3b86f0**: calls the FOW setup 0x35eb40. Behind vf+0x240 (land 0x17dc70, constant true), that setup
  calls vf+0x250(1), which keeps a grid, then 0x4dcfd0, which creates a new, empty grid.
- `Load_Map` also calls vf+0x248 (call site 0x56add). So the grid that exists during the player setup is the `Load_Map`
  grid, and the battle runs on the second one.

### 5.2 Which land objects miss the new grid's registration [H unless tagged]

**The mechanism** (as SKAI_T22 §1.1.1, re-read here for the land path):
- 0x29f810 calls the GOM add hook 0x2b3670 for any object that is not dying and has no TEAM slot. The call ignores
  0x29f810's last two arguments.
- The hook's UNIT_AI part 0x4f5950 registers the object (0x6537d0) in the grid of that moment.
- The UNIT_AI service 0x4f6070 registers only when the pack's entry count (+0x18) is 0. The land mode gate vf+0xf0 is
  0x17dc70, constant true.
- The grid destructor leaves the packs alone. An object registered in the `Load_Map` grid therefore keeps entries
  and is never registered in the battle grid.

**What 0x295cf0 creates before the rebuild in land:**
- The team-marker swap (BKG3_RE_ANSWERS Q3.1) turns each `Team_NN_*` `MultiplayerStructureMarker` into the first type
  of its `Marker_For_Specific_Object_Type` list for that player's faction. In land these are build pads
  (`MULTIPLAYER_STRUCTURE_MARKERS.XML`: `Imperial_Command_Center_Build_Pad`, `Underworld_Palace_Build_Pad`,
  `U_Ground_Merc_Outpost_Build_Pad`, `U_Ground_Droid_Works_Build_Pad`, `U_Ground_Arms_Depot_Build_Pad`, the Rebel /
  Empire factory, research, communications, power, shield and turbolaser pads, `<Faction>_Build_Pad`), plus
  `Reinforcement_Point_<Faction>`.
- For each created pad the swap runs pad vf13 **0x42df80**, the pre-build. In a single-player skirmish, or with the
  pre-built-bases option and the pad type's `Respects_Skirmish_Pre_Build_Bases_Option`, it creates the pad's pre-placed
  type through **0x29f810**, for the pad owner at the pad's build point:
  - the constructed type when the stored type is an under-construction one (behaviour 0x2a → type +0x9f0);
  - then it links the structure to the pad (0x3a8f80 sets obj +0x2b0) and copies the pad's +0x3CB.
  - That create goes through the hook, so the structure is registered in the `Load_Map` grid.
- 0x293d60 (special weapons) acts only in space (mode type 2), so it is not relevant in land.

| Land object | Created | In the battle grid's owner lists? | In its live lists / threat layer? |
|---|---|---|---|
| Base structures pre-built on the team pads (Command Center / `Underworld_Palace`, barracks / merc outpost, light and heavy factories / Droid Works / `U_Ground_Vehicle_Factory`, research facility, communications array, power generator, base shields, turbolaser towers) | 0x295cf0 → 0x42df80 → 0x29f810 → hook, **before** the rebuild | **no, all battle** | **yes, from its first service** [M, 5.3] |
| The pads themselves, `Reinforcement_Point_<Faction>` | same swap | no UNIT_AI (`REVEAL, REINFORCEMENT_POINT, HIDE_WHEN_FOGGED, DUMMY_TOOLTIP` for the points; pads per SKAI_T06 §9), so never in any grid | no |
| Start units (`*_Skirmish_*_Default_Forces`: stormtrooper / merc teams, AT-STs ...) | 0x2a4fc0 after the rebuild, team helper → hook | yes, at create | yes |
| Map-placed objects (neutral turrets, map units, mineral pads ...) | map loader, no hook | yes, at the first service | yes |
| Anything built or spawned in the battle, including a Command Center or barracks rebuilt on a base pad, and pad-built Mineral Processors and turrets | in the battle | yes | yes |

- Example: `U_Ground_Vehicle_Factory` has `DUMMY_GROUND_STRUCTURE` (Behavior) and `UNIT_AI` (LandBehavior), with
  `AI_Combat_Power` 300 (`SPECIALSTRUCTURES_UNDERWORLD.XML`). That it is the pre-built type of the Underworld heavy-factory
  pad is [M] (the port's `bt_skirmish_base` makes the same choice).
- The swap also sets +0x3a3 bit 0 on the pad and on its occupant. The owner remaps skip such objects, so no
  `Change_Owner` happens. A null-list `Change_Owner` (0x38e5a0 → 0x4f4ba0, unregister and clear the pack) is the only
  path that would let the service register them again.
- The rule depends on **when** the object was created, not on its type. The port keys it on `DUMMY_GROUND_STRUCTURE`
  instead. In shipped data the two sets agree at battle start: every pre-built pad type is a `DUMMY_GROUND_STRUCTURE`
  type with UNIT_AI (SKAI_T06 §9). They split once a base structure is built during the battle (5.5).

### 5.3 Live lists: the first service re-projects them [H code, M outcome]

- 0x6537d0 registers without projecting:
  - it creates the entries (constructor 0x653100 sets the entry radius +0x1c to −1, "not projected");
  - it ORs the category mask into the grid and appends the object to its owner's list;
  - it copies snapshots for players that have the object fogged, and subscribes the grid to the object's signal 0x28.
- Right after registering, both the hook (0x4f5950) and the service (0x4f6070) call **0x4f4c60**. That function writes
  the pack's "last transform" (+0x68..+0x94) from 48 bytes of its own **uninitialised stack** (`rsp+0x2c`, read before
  anything writes it). It does not copy the object's transform, and nothing later in the function writes those bytes.
- The service then compares the object's transform (+0x248..+0x274) with that copy. If they differ, it calls 0x659760:
  - remove each entry from its old cells (0x656d10, skipped while the radius is −1);
  - add each entry to its new cells and the threat layer (0x656a10);
  - store the real transform and push the next check 300 frames on.
- This is how every newly registered static object reaches the live lists. 0x6537d0 never projects, so if the stored
  copy equalled the transform, no static structure would ever be projected.
- For a pre-built base structure the compare happens at its first service in the new grid. The pack count is above 0, so
  it is not registered, but the compare still runs: the stack residue does not equal the transform, so the structure is
  re-projected into the **new** grid's live lists and threat layer. It stays out of the owner lists.
- The [M] is the residue: it is what 0x6537d0 and its callees left on that stack slot. None of them builds a transform,
  so a match is not credible, but it was not observed at run time.
- **SKAI_T22_SPACE_AI_RE Q1.1 needs the same correction.** It says the space start station is "not in the live cell
  lists (+0x10), not in the threat layer". By the code above the station also re-enters the new grid's live lists and
  threat layer at its first service. The owner-list part, and so `DistanceToNearest*` = 1e18, which retail showed, stands.

**Death of such a structure** [H code, L effect]:
- Entries leave the cells only through three functions:
  - the grid's signal handler 0x6585f0 (grid vtable 0x8b4470 slot 1, subscribed per object in 0x6537d0);
  - the unregister 0x658930 (only from 0x4f4ba0, which only the null-list `Change_Owner` calls);
  - the re-projection 0x659760.
  The entry destructor (0x653660 → 0x653470) only frees its own vectors.
- The battle grid never subscribed to a pre-built structure's signals. So when that structure dies and is removed, nothing
  takes its entries out of the live lists or its value out of the threat layer.
- What later reads of those cells return after the object and its pack are freed was not traced.

### 5.4 Which reads miss them [H, AI_TACTICAL_PERCEPTION, SKAI_T22 §1.1.4, LUA_API `Find_Nearest`]

| Reads | List | Pre-built base structures |
|---|---|---|
| `DistanceToNearestFriendly/Enemy` (0x72d000), player `Friendly/EnemyForce[Unnormalized]` and `Game.Force[Unnormalized]` (0x656400), Lua `Find_Nearest` (0x69e7c0, walks +0x88[Q]) | owner lists +0x88 | **missed**, for every player, the owner included |
| CellForce 0x6545d0 (`Location.FriendlyForce/EnemyForce[Unnormalized]`, `Get_Most_Defended_Position`, the contrast escort list of Q2), concentration 0x656800, bitfields 0x654d90, point / zone danger 0x653ff0 / 0x653e90 (slot 9 for regions, zone costs, pathing) | live lists +0x10, threat layer | **counted** [M, 5.3] |
| Target list, `Applies`, the structure's own unit tokens (`Health`, `IsType`, `BaseLevel` ...) | not grid-based | counted |

### 5.5 The port against this

The port's rule (skai t12 / b0422, dev 96257374) keeps every `DUMMY_GROUND_STRUCTURE` object in the threat grid's cells,
and skips it only in `player_force` (`tactical_perception.cpp`, about line 466) and in `g_find_nearest`
(`bind_objects.cpp`, about line 1003). Against retail:
- **Same:** force sums and `Find_Nearest` miss the start base; `Location.*` force, concentration and zone danger include
  it [M, 5.3].
- **Different 1:** `DistanceToNearest*` (`nearest`, about line 531) and `ForceVisibility` (about line 1072) walk
  `player_objects(q)` without that skip, so the port finds the start base there and retail does not.
- **Different 2:** the port skips a `DUMMY_GROUND_STRUCTURE` built during the battle (the AI's
  `Tactical_Multiplayer_Build_Command_Center` / `Build_Barracks` plans, or a human rebuilding on a base pad). Retail
  registers it normally and counts it in every owner-list read.
- **Different 3 (open):** the port removes a dead structure's cell entries. Retail leaves a pre-built structure's entries
  behind (5.3, [L] effect).

## Q3. Run 1's 7421 displacement

**Commit step 1 (0x7222c0) re-runs neither the slot 9 test nor the learning terms** [H]. For each active goal with a
function:
- **a target goal:** if the target's vf+0x20 (`Applies`) passes with the goal type's application flags for this AI
  player, it binds Self / Enemy / Human / Target and evaluates the goal function. When the evaluation succeeds it writes
  the result as the new desire (0x693730). **When the evaluation fails, the old desire stays.**
- **a global goal:** the same evaluation without a target, only when the type has the 0x10 flag.
- No slot 9 call and no reachability: an incumbent whose target becomes "unreachable" is not released for it (structures
  have no reachability object anyway, Q1a).
- No learning terms: `Per_Activation_Failure_Desire_Adjust` (c2 · w2) and the goal-failure term (c1 · w1) are added
  only to proposals (AI_GALACTIC §1.5).

So an incumbent `Destroy_Structure` keeps the bare `Should_Destroy_Structure` value, and with the hysteresis of 0x6927f0
its effective desire is about **16.00** at desire 15.0 (14.92 → 15.92 at the retail factory value of
SKAI_T15_TARGET_LOCATION_FORCE §4). It is not replaceable-protected (`DestroyStructure.lua` never calls
`Set_As_Goal_System_Removable(false)`). It goes to the old candidates when the best surviving `Tactical_Targeted`
proposal's effective desire is higher (Resolve_Category step 3, AI_GALACTIC §1.6). A proposal's effective desire is its
desire, its learning terms included.

"No plan started at that commit" fits that path [M]:
- the greedy phase stops on that proposal: its plan cannot be activated (0x6c4a50), it is `Is_Like` the old
  candidate, or `0 < AllowedTime < ETA`, each recording an activation failure;
- the weighted random phase finds nothing it can activate;
- at Finalize the displaced incumbent is released as a pool leftover that was active.

The proposal itself is not in the binary. It is a `Tactical_Targeted` goal (`Clear_Infantry`, `Clear_Vehicles`,
`Destroy_Land_Unit`, `Land_Scout_Area`, `Destroy_Turbolaser`, `Airstrike`, `Land_Bombing_Run`, `PreDefend_Structure`, ...,
or another `Destroy_Structure` target) with desire above about 16 that passed slot 10 at its proposal. With an empty
free store, slot 10 passes only for a plan whose PotentialPlan can be filled without free units. Which marshallers allow
that (reinforcement or production candidates) was not read [L]. The trace needs the proposal records (type, target,
desire) at the 7421 pass.

**Port difference found on the way** [H]: `GoalSystem::commit` step 1 (`engine/ai/src/ai_goals.cpp`, about line 463)
sets the desire **to 0** when the evaluation fails. Retail keeps the previous desire. A 0 then lets maintenance drop a
replaceable goal (effective desire ≤ 0).

## Q2. Other enemy-near-target gates between proposal and commit

Read from the existing specs (AI_TACTICAL §7.7.1-7.7.3, SKAI_T15_PLAN_RETEST, A1_READINGS §25.3) and the FoC
`DestroyStructure.lua`; nothing new was decompiled for Q2.

| Stage | Depends on enemies near the target? | For `Destroy_Structure` on the vehicle factory |
|---|---|---|
| Slot 9 | no (Q1a) | never skipped |
| Slot 10, PotentialPlan solve, contrast **main** list | the target's own power only (`Target.ForceUnnormalized`) | entry 0 = 300; the `Structure` row = 300 × `MaxContrastScale` 1.5 × `Land_AI_Contrast_Multiplier` (Medium 1.0) = 450 |
| Slot 10, contrast **escort** list (only because `GuardForce` is an `EscortForce`) | **yes**: `Target.Location.EnemyForceUnnormalized`, Self's enemies on the 12 threat cells of zone 85, the factory excluded, per category of the plan's `EnemyContrastTypes` (library default from `PGAICOMMANDS.LUA`, which includes `Infantry`, `Vehicle` and `Structure`) | `Infantry` row: the merc teams' share, 1.48 per team as placed (SKAI_T15_TARGET_LOCATION_FORCE §4), so 8.9 raw and about 13.3 scaled, up to about 80 scaled at the bound; `Structure` row: the entries of Underworld structures whose squares touch those cells [M, Q5 5.3] |
| Solver / FreeStore marshaller stage pick (Setup A 0x7287a0) | the flood around the region at the target's position at `Low_Threat` | chooses the stage zone (zone 85 when its danger is 0 or ≤ 1000, else the first zone of the best neighbour flood); with no zone the goal target. A preference, never a veto |
| `MinimumTotalForce` | - | not set in FoC `DestroyStructure.lua` (MainForce `Infantry \| Vehicle \| Air = 1,10`, `LandHero = 0,2`; GuardForce `Squad_Rebel_Trooper \| Squad_Stormtrooper = 0,2`) |
| Commit: re-test 0x6c47c0 at every commit (kept, old-candidate and maintenance paths) | **yes**: both lists are rebuilt from the current perception and compared with the PP's stored selection | fails, and releases the plan, only when the escort rows grow past what the stored selection covers (SKAI_T15_PLAN_RETEST §1) |
| Plan start (Instantiate 0x6bad60), `QuickReinforce` | no | - |

The escort list sizes the task force: escort and MainForce units are added until the escort rows are covered. It does
not forbid the goal while the free store can cover the rows. Six idle merc teams standing still give a constant escort
row. That row makes retail's plan carry cover units, and lets the re-test fail if the selection's cover is lost.

**In the port the factory's escort list is 0.** Its `Location.EnemyForce` for the factory reads exactly 0
(SKAI_T15_PLAN_RETEST "Why the port's plan 14 holds"), and by SKAI_T15_TARGET_LOCATION_FORCE §4-5 that is not retail's
value. The checks are listed there: merc entry radius 240, footprint columns 70-78, the factory's `PerceptionObject::zone`
= 85 and not −1. With 0, the port's plan needs no cover for the mercs, and its re-test can never fail on them. This is the
one enemy-near-target rule where the port is known to differ. Fixing it moves the plan's size and its re-test, not
whether the goal is proposed.

## Q4. Time_Limit 30 for a running Destroy_Structure

**No: Time_Limit never ends or weakens a running tactical goal** [H].
- AllowedTime 0x692860:
  - goal with a PotentialPlan (+0x68): AllowedTime = 0x6c0450(PP) × `Build_Time_Delay_Tolerance` (type +0x34);
  - goal without one: AllowedTime = `Time_Limit` (type +0x30).
  Every accepted tactical goal has a PotentialPlan, since land slot 10 is `Find_Plan_For_Goal` only.
- Maintenance 0x722a30 runs its "AllowedTime < ETA" drop only when the mode type (+0x34) is 0, galactic. A running land
  goal is dropped by maintenance only for: Released / no plan, the fund test, effective desire ≤ 0 while replaceable,
  or a failed re-test.
- Resolve_Category's `0 < AllowedTime < ETA` test applies only to activations (greedy and random phases), not to kept
  goals.
- Same as AI_TACTICAL §7.7.3 item 3. The port's `gcfg.nominal_allowed_time = true` and the galactic-only maintenance time
  drop (`uses_budget()`) match.

Retail's roughly 400-frame `Destroy_Structure` lives are the commit rules counted in SKAI_T15_PLAN_RETEST "Retail hook
runs": the re-test, displacement, `Purge_Goals` and the plan's own exit.

## Port rules

1. **Q1a / Q1: no change.** Keep `TacticalTarget::has_reachability()` = region targets only, and keep `blocked()` returning
   false for unit targets. Do not add a zone-danger skip for structures or units.
2. **Q5, the start-structure rule:** replace the `DUMMY_GROUND_STRUCTURE` test with a per-object flag set at the land
   skirmish setup. Every UNIT_AI object that the port's `bt_skirmish_base` (`tactical_battle_bt.cpp`) pre-builds on the
   team pads gets it. The space team-marker station gets the same flag (skai-b's rule).
   - A flagged object is **never added to the threat grid's owner list** (`TacticalPerceptionGrid::player_objects`) and
     stays in the cells and threat layer as now.
   - Drop the `dummy_ground_structure` skip in `player_force` and in `g_find_nearest`. Let `g_find_nearest` skip the
     flagged objects instead, since retail's `Find_Nearest` walks the owner list. `nearest` (DistanceToNearest*) and
     `ForceVisibility` then miss them as retail does.
   - Objects created later, including a rebuilt base structure, register normally and count in every read.
   - Keep removing a dead structure's cell entries in the port, and record that as a `[port]` choice until the retail
     check below has run.
3. **Q5, for skai-b:** the space start station should stay out of the owner lists only. Put it back in the cell lists and
   threat layer [M, 5.3], or hold that change for the retail check.
4. **Q3:** in `GoalSystem::commit` step 1 keep the previous desire when the evaluation fails, instead of setting 0 (retail
   0x7222c0). Nothing to change for slot 9 or for learning terms; the port already leaves both out.
5. **Q2:** fix the factory's `Location.EnemyForce[Unnormalized]` = 0 in the port first (SKAI_T15_TARGET_LOCATION_FORCE
   "Port consequence" 1), so the escort list of a `Destroy_Structure` beside enemy units is not empty. Then re-measure
   plan 14's size, its re-test results and the 4847-9896 hold.
6. **Q4: no change.**

## Open and retail check

- **5.3 at run time** (one read-only Frida hook in a land skirmish, Empire AI vs idle Underworld on Utapau) settles two
  things:
  - Log every 0x659760 call with the object's id and type name over the first 400 frames. Pre-built base structures should
    appear once each, which confirms the live-list rule.
  - Log 0x6585f0 over a structure's death. It should be absent for a pre-built structure and present for a pad-built
    Mineral Processor, which confirms the stale entries.
- What the stale entries of a destroyed pre-built structure do to later `Location.*` and zone-danger reads [L].
- The `Tactical_Targeted` proposal that displaced run 1's factory plan at 7421 (Q3): needs proposal records from a run.
