# skai t15: what `Variable_Target.Location.EnemyForce` covers for a land structure (RE, 2026-11-16) [H unless tagged]

Answers lane skai-a's four questions about the vehicle-factory desire in `SKAI_T15_PLAN_RETEST.md` ("Why the port's
plan 14 holds"). Read with ReVa and REA on the FoC Steam x64 image (base 0x140000000; addresses below are RVAs). Map
numbers come from `_MP_LAND_UTAPAU.TED` in `Patch2.meg` (0x10B hint zones, 0x103 playable box), parsed by a scratch
script, and from the shipped XML. [H] = read in the binary or the shipped data; [M] = inferred.

Functions: 0x6d7370 (LandUnit evaluator registration: builds the `.Location` evaluator), 0x6d5620 (LandLocation
evaluator constructor), 0x7304d0 (attach an object to a location evaluator), 0x6d5690 (LandLocation slot 15,
`Friendly/EnemyForceUnnormalized`), 0x6d65d0 (LandLocation slot 13, link a zone), 0x730470 (TacticalLocation slot 13:
set zone and centre), 0x72cd00 (insert a threat cell into the evaluator's set), 0x6d66a0 (cell-set copy-assign),
0x72db60 (TacticalLocation slot 15, the shared force body), 0x6544a0 (zone-list force sum), 0x654290 (rect force sum),
0x6545d0 (CellForce), 0x655e50 (memory-entry value), 0x737ee0 (`Game.ForceUnnormalized`), 0x656400 (owner-list force
sum), 0x6537d0 (grid registration), 0x653e90 (zone danger), 0x72cf10 (`Location.ForceUnnormalized`). Land mode vf
+0x180 = 0x3b7f50 (position to zone, A1_READINGS §4).

## Summary

- **The factory's `Location` is its hint zone's threat cells.** A land structure gets the same LandUnit evaluator and
  the same unit-bound LandLocation evaluator as a mobile unit. The area is a cell set built from the hint zone that
  holds the object's position, never a rect and never the single cell. `U_GROUND_VEHICLE_FACTORY` at (4819, 1334) is in
  "New Zone 86" (zone index 85). That zone is small: 38 passability cells, which map to **12 threat cells** (columns
  68-70, rows 70-73, x 4687-4866, y 1176-1414).
- **An enemy counts if its grid square touches one of those 12 cells.** An enemy's force is spread over a square of
  half-side r (its max attack range, at least 42.1 and at most 500 on land). It counts `power × cellW × cellH / 4r²` in
  every touched cell of the area. The attached object (the factory itself) is excluded. For an AI `Variable_Self`,
  visibility plays no part with shipped data (`AIUsesFogOfWarLand` = False): every grid entry counts, seen or not.
- **The six merc teams can only add about 0.005-0.02 to the factory's EnemyForce.** A team is spread with r = 240 and
  power 24, so it adds 0.369 per touched cell. A team centred at (5100, 1260) touches only column 70 (4 cells), adding
  1.48. Six such teams add 8.9. Even all six squares covering all 12 cells, each counted twice through Self's memory
  lists, would add 53. Against a `Game.ForceUnnormalized` of about 1200-1700 [M], that moves `Should_Destroy_Structure` by
  about 0.08 (as placed) to 0.7 (the bound). **So these rules cannot make retail's factory desire fall far enough to explain
  the displacement at about 800 frames** [M: the cause is elsewhere].
- **The port should not read exactly 0 here.** By these rules a team centred west of x = 5105.8 (for example at
  (5100, 1260)) contributes 1.48, so `Should_Destroy_Structure` should be about 14.92, not 15.001. Check in the port, in
  this order: the merc team's grid
  radius (it must be 240, from the first company member, not the container's own 150); the team's projected position;
  and whether the factory's evaluation takes the `zero` branch (no zone). Details in "Port consequence".
- **Rules that differ in the port (they do not touch a structure):** D1, retail keeps a unit's last zone when the unit
  stands outside every zone. D2, retail's unit-location cell set is the union of every zone the unit has been in. D3,
  each relink overwrites the entered zone's pathing-danger cell list with that union. D4 (open), whether pre-placed base
  structures are in the grid's cell lists.

## 1. The area of `Variable_Target.Location` for a land object

- **Same path for structures and units.** Every land object with `Has_Land_Evaluator` gets a LandUnit evaluator
  (AI_TACTICAL_PERCEPTION §2). Its registration (0x6d7370) builds a LandLocation evaluator (0x6d5620). It attaches the
  object to it (0x7304d0: evaluator +0xa8 = the object) and stores it as the unit evaluator's `.Location`. Nothing in
  this path tests for a structure.
- **Zone lookup on every force evaluation** (0x6d5690, also used by the other land location tokens [M: the same
  wrapper pattern in 0x6d5a10 / 0x6d5950, not re-read here]). The steps, in order:
  1. If an object is attached, look up the zone at the object's position (obj +0x78/+0x7c) through land mode vf +0x180
     (0x3b7f50). The lookup returns the first zone in hint-zone list order whose cell set holds
     `((int)((x + 10) / 20), (int)((y + 10) / 20))`, else null.
  2. If the lookup is **non-null and differs** from the current zone (+0xf0), relink (slot 13, 0x6d65d0).
  3. **A null lookup keeps the current zone.** The value is 0 (success) only while the evaluator has **never** had a
     zone.
  4. Otherwise the shared body 0x72db60 runs.
- **What the relink builds** (0x6d65d0):
  1. 0x730470 sets +0xf0 = the zone and +0xf8..+0x100 = the zone centre.
  2. For each passability cell of the zone (the zone's cell hash set, list at zone +0x58), it takes the world point
     (x · 20, y · 20) and maps it to a threat cell (0x654210: truncate, clamp). It inserts `col | row << 16` into the
     evaluator's set +0xb0 (0x72cd00, stdext hash_set, no duplicates).
  3. **+0xb0 is not cleared first.** A moving unit's set is the union of every zone it was ever linked to.
  4. It copy-assigns +0xb0 into the zone's +0x90 list (0x6d66a0).
- **No rect on land.** 0x72db60 would build a `2R × 2R` rect around the attached object (R = max attack range of its
  representative type, 0x3749d0) when +0xf0 is null. The land wrapper returns 0 before reaching it, so a land
  unit-bound location always sums the cell set +0xb0 (0x6544a0). The rect is the space path.
- **A structure never moves**, so it is linked once, at its first location evaluation, to the zone at its centre. Its
  set is that zone's threat cells.

The vehicle factory on `_MP_LAND_UTAPAU` [H: parsed .ted]:

| Object | Position | Passability cell | Zone (index, name) | Zone cells | Threat cells of the zone |
|---|---|---|---|---|---|
| `U_GROUND_VEHICLE_FACTORY` | (4819, 1334) | (241, 67) | 85, "New Zone 86", centre (4782, 1292) | 38, x 4740-4820, y 1220-1360 | 12: columns 68-70 × rows 70-73 = x 4687.2-4865.8, y 1175.7-1413.7 |
| Merc teams | about (5100, 1260) | (255, 63) | 86, "New Zone 87" | 163 | 28: columns 70-76 × rows 70-73 |
| Palace | (5485, 1427) | (274, 71) | 89, "New Zone 90" | 884 | 116: columns 70-82 × rows 60-70 |

Grid: playable box (640, 640)-(5580, 5580). nFow = ceil(4940 / 15) = 330 per axis and nx = ny = ceil(330 / 4) = 83, so a
threat cell is 59.518 units square. The half diagonal is 42.09. "New Zone 86" and "New Zone 87" are among the map's
repeated names (TACTICAL_GAPS §6.8). That matters only for region targets: a unit location binds the zone by pointer
through the position lookup.

## 2. `EnemyForceUnnormalized` over that area

0x72db60 reads `Parameter_Category` (default all), `Parameter_Attenuator` (default 0) and `Parameter_Faction`. It
calls 0x6544a0 with the grid, the evaluator's set +0xb0, the mask, Self (bindings +0x58 → +0x50), the ally flag (0 for
Enemy), A, the attached object and the faction. 0x6544a0 returns 0 when the grid's category union (+0xc8) misses the
mask. Otherwise it sums CellForce over the set in its list order and multiplies by `AI_LandAreaThreatScaleFactor`
(shipped 1.0). The result is cached for 9 s (Enemy) / 10 s (Friendly).

**CellForce** (0x6545d0) of one threat cell = Σ over the cell's live entry list, then over Self's memory list for the
cell, of `(1 − (1 − health) × A) × (power / area) × cellH × cellW`, for entries whose object:
- is not the location's attached object. The test is a pointer compare, so every hard-point entry of the factory is
  skipped too;
- passes the faction filter if one is given, and has a type (0x396380) whose category mask meets the mask;
- has an owner player whose faction is null or not `Is_Neutral`, and for which 0x2823e0(owner, Self) equals the ally
  flag. For EnemyForce that means not allied with Self;
- is not fogged for Self (0x4f57d0; tested only when the object has UNIT_AI). The same test applies to memory entries,
  so with Self never fogging anything both lists count (A1_READINGS §10).

Power = `AI_Combat_Power` of 0x396380's type (a TRANSPORT object uses its own type), or the hard-point value for a
hard-point entry. Area = the entry's current 4r². A memory entry's value (0x655e50) uses the same power and the same
live area.

**How far one object's force spreads** (AI_TACTICAL_PERCEPTION §4.3-4.4, A1_READINGS §29.4):
- **Footprint:** a square of half-side r around the entry position. The object's position, or a turret hard point's
  world position.
- **Radius:** r = 0x3749d0 max attack range. For a TEAM container it is the first company member type's range; for a
  hard point, its weapon range. Then `+ speed × AI_LandThreatLookAheadTime` (shipped 0), `max(r, 42.09)`, `min(r, 500)`.
- **Spread:** the entry is put in the live list of every threat cell from column `(x − r − left) / cellW` to
  `(x + r − left) / cellW`, and likewise for rows (truncated, clamped). Each listed cell carries the same
  `power × cellW × cellH / 4r²`, whatever share of the cell the square covers.
- **Cadence:** re-projected at most every 300 frames, and only if the object's transform changed.

**Fog:** live entries are skipped only when the object has UNIT_AI and is marked fogged for Self. With shipped data the
AI players never mark anything fogged (`AIUsesFogOfWarLand` = False, AI_TACTICAL_PERCEPTION §4.7). So for an AI
`Variable_Self` the sum is omniscient: an object counts whether Self sees it, has seen it or never saw it. Memory
entries add a second copy of whatever was listed in a cell when that cell last refogged for Self.

## 3. `Game.ForceUnnormalized` (the divisor)

0x737ee0 reads Category (default all) and Attenuator (default 0), then calls 0x656400 with no player. The value is 0
without a grid. 0x656400 walks every player's grid owner list (+0x88[player], player list order, registration order).
It has **no relation, neutral-faction or fog filter**. Per object whose type's mask meets Category it adds
`AI_Combat_Power(0x396380 type, own type for TRANSPORT) × (1 − (1 − health) × A)`. Only UNIT_AI objects registered by
0x6537d0 are in those lists: TEAM containers count once, members never. Pre-placed base structures
(DUMMY_GROUND_STRUCTURE) are absent from the lists ([H] observed, SKAI_T06_LAND_RUNS §7 / §9; the mechanism is not
read). The normalised `EnemyForce` is `EnemyForceUnnormalized / Game.ForceUnnormalized` (0 when the numerator is 0, 1
when the numerator is larger; AI_TACTICAL_PERCEPTION §1.4).

## 4. Six idle merc squads about 300 from the factory

**Yes, they contribute once their squares touch the factory zone's cells, and only then.** Whether Self can see them
does not matter (§2, fog). Only geometry decides it.

| Quantity | Value | Source |
|---|---|---|
| Team power | `Underworld_Merc_Squad`: 12 × `Underworld_Merc` 6 = 72, × clamp(`Max_Squad_Size` 4 / 12) = **24** | XML, A1_READINGS §29.3 [M: 12 list entries, three of them separated by spaces only] |
| Team radius r | `Underworld_Merc` `Targeting_Max_Attack_Distance` **240** (the first company member, not the container's 150) | XML, A1_READINGS §29.4 |
| Per touched cell | 24 × 59.518² / (4 × 240²) = **0.369** | §2 |
| Touches column 70 (the zone's east column) | team centre x < 5105.8 (and x ≥ 4447 for column 68) | footprint rule |
| Touches rows 70-73 | 935.7 < team centre y ≤ 1653.7 | footprint rule |
| Team at (5100, 1260) | columns 70-78 × rows 68-76: **4** zone cells, **1.48** | |
| Six teams there | **8.9**; with Self's memory copies of those cells (if they refogged for the Empire) up to 17.7 | |
| Upper bound (all six covering all 12 cells, plus memory copies) | 26.6 (53.1) | |
| `Game.ForceUnnormalized` | about 1200-1700: Empire 4 stormtrooper teams 252, 4 AT-ST 280, Dark Troopers about 420, Boba Fett 500; Underworld 6 merc teams 144 plus any other units | [M: army list from SKAI_T15_PLAN_RETEST, shipped powers] |
| Factory `Location.EnemyForce` | 0.005-0.007 at (5100, 1260); at most about 0.04 | |
| `Should_Destroy_Structure` (factory) | `15.001 − 15 × EnemyForce`: about 14.92 at (5100, 1260), never below about 14.3 | equation, BASICLANDEQUATIONS.XML |

So the factory's desire can fall by tenths at most from this term. A displacement from the incumbent's 16.0 needs some
other cause [M]. The other terms of `Should_Destroy_Structure` for this target do not move with the merc fight. They
are Owner.IsFaction, the IsType lists (none matches `U_GROUND_VEHICLE_FACTORY`), `Allowed_As_Defender_Land` (1 in a
skirmish) and `May_Lose_Land_Control_Game` (not read here). That points at the incumbent's re-evaluation or at the
competing proposals, not at the factory's EnemyForce.

The palace and barracks read 0.63 in the port. Six merc teams cannot supply that: at most 144 of power exists, and only
rows 68-70 of their squares reach the palace zone. At 0.63 × about 1500 the port's palace zone must hold about 950 of
enemy power. That is about what the Underworld base structures would add if they are in the cell lists (D4).

## 5. Port comparison (`engine/ai/tactical/src/tactical_perception.cpp`, `threat_grid.cpp`)

Same as retail for a structure:
- Zone at the position: first zone with the cell (`ZoneLocator::zone_at`).
- The area is the zone's threat-cell list (`zcells`).
- The attached object is excluded (`q.exclude`).
- Category, neutral-faction, relation and faction filters.
- Live entries, plus memory entries while not fogged.
- The AI's fog test is off.
- Area scale factor; the per-cell value `power / area × cellW × cellH`.
- Footprint and radius rules, including the team radius from the first member's range.
- `Game.ForceUnnormalized` over the owner lists, with base structures left out (`player_force`).

| # | Retail | Port | Who it affects | Conf |
|---|---|---|---|---|
| D1 | Unit outside every zone: the evaluator **keeps its last zone**. 0 only if it never had one | `o->zone < 0` → `a.zero`, every force / concentration / time token 0 | mobile units crossing zone gaps (`location_area`) | H |
| D2 | Unit-location cell set +0xb0 = **union of every zone the unit has been linked to** (never cleared) | the current zone's cells only (`zcells(o->zone)`) | mobile unit targets' `Location.*` force, concentration, bitfield, time-last-seen and `Location.ForceUnnormalized` (0x72cf10 reads +0xb0) | H |
| D3 | Each relink **copy-assigns that union into the entered zone's +0x90 list**. Zone danger 0x653e90 reads zone +0x90 for the movement zone cost (0x5a6940), reachability (0x5a7390, 0x5a7880) and the path finder (0x6940e0). So a zone's pathing danger covers the union of the last unit that entered it | zone +0x90 = the zone's own cells, fixed at load (`zone_cells_`) | land pathing and reachability threat only (region-target evaluators sum their own +0xb0, which never changes) | H code, M effect size |
| D4 | Pre-placed base structures are not in the owner lists (observed). **Whether they are in the cell lists is not read** | registered in the grid (in the cell lists), skipped only in `player_force` | `Location.EnemyForce` / `FriendlyForce` of zones near a base (the palace / barracks 0.63) | open |
| D5 | CellForce multiplies `health factor × (power / area) × cellH × cellW` in that order | `power / area × cellW × cellH × health factor` | float rounding only | H, tier 3 |

## Port consequence

1. **The factory's 15.001 is not retail's rule.** With the mercs centred at about (5100, 1260), retail's
   `Location.EnemyForce` for the factory is about 0.006 (desire about 14.92). A team centred at x < 5105.8 always touches
   column 70. Check, with the port's AI log at a frame where the mercs stand there:
   - the merc team's `max_attack_range` / grid entry radius (expect 240);
   - its entry position and footprint columns (expect 70-78);
   - that the factory's `PerceptionObject::zone` is 85 and not −1 (the `a.zero` branch, for example when the object's
     zone is set before the zones are loaded);
   - that the team has a grid entry at all (`has_unit_ai` on the container).
   This is a rule fix, but too small to change plan 14 on its own.
2. **Do not look for retail's displacement in the factory's EnemyForce.** Its largest possible effect is a few tenths
   of desire. Compare the retail `TDESIRE` series (`analysis/desire_trace.py`) for the factory, and for the proposals
   that replaced plan 14, around the displacement frame.
3. **D4 can be settled from the same `TDESIRE` data without new RE.** For the palace (no IsType term, owner Underworld),
   retail `Should_Destroy_Structure` = `15.001 − 15 × EnemyForce`. A retail value near 5.5 means the base structures are
   in the cell lists (as in the port). A value near 14.9 means they are not, and the port's 0.63 is wrong.
4. **D1-D3 are tier-2 rule differences for mobile targets and pathing.** Port them as recorded: keep the zone on a null
   lookup; accumulate the cell set per unit location and never clear it; assign the union to the entered zone's danger
   list on each relink. Keep the stdext hash-set insertion order (A1_READINGS §12) for the float sums. They do not
   explain the factory.

## Open

- Why DUMMY_GROUND_STRUCTURE objects are missing from the owner lists, and whether they have cell entries (D4).
  0x6537d0 inserts into the owner list and creates the entries in one call, and projects them at the first service
  whose transform differs from the stored copy. So a structure registered normally would be in both. The owner-change
  path 0x38e5a0 unregisters (0x4f4ba0 → 0x658930) only when called without a player-list argument. Not traced further.
- **Answered (D4), SKAI_T15_TARGET_CHOICE §Q5:** the pre-built base structures are created by the skirmish setup's
  pad pre-build before the threat grid is deleted and rebuilt, so they are never in the battle grid's owner lists; they
  do reach its cell lists and threat layer at their first UNIT_AI service [M].

## Correction: the merc team radius is 178.6, not 240 (skai-a, 2026-11-16) [H]

0x3749d0 (REA read; LOB_SOLVER §4) checks the projectile before `Targeting_Max_Attack_Distance` (+0x500):
- It takes the type's `Projectile_Types` entry at the object's current weapon index (obj +0x324). The GameObject ctor 0x348a50 sets that index to 0 (TACTICAL_COMBAT §3).
- If that projectile is `Projectile_Category` 3, it returns Max_Speed² / (Lob_Mod × Gravity).

For a merc team, the call is 0x3749d0(`Underworld_Merc`, the team container):
- The container's +0x324 is 0, so the projectile is `Proj_Merc_Concussion_Grenade`, a grenade.
- So r = its lob range, 178.6, the value the port registers (`CombatSystem::attack_range`, measured on Utapau seed 1).
- The 240 in §2 / §4 and "Port consequence" 1 came from the XML alone.

Consequence:
- A team touches the factory's zone 85 only when its centre is at x < 4865.8 + 178.6 = 5044.4.
- Port seed 1 at 3056: the six teams stand at x 5080-5176, so the factory's `Location` force is 0. Measured: zone 85, 12 cells, EnemyForceUnnormalized 0.
- Retail reads 0 as well, unless its teams stand further west.
- So the port's 15.001 and its empty Destroy_Structure escort list (SKAI_T15_TARGET_CHOICE Q2) are not port errors.
