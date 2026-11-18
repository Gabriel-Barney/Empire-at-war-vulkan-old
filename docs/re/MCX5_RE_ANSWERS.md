# mcx5: RE answers (limits policy: Q1.2, Q1.5-Q1.9, Q2, Q3, Q4)

Answers to `MCX5_RE_QUESTIONS.md` for lane mcx5 (roadmap group `mcx5`, box MCX2), for the limits policy in
`docs/MOD_COMPAT_DESIGN.md` §8. Q1.1, Q1.3 and Q1.4 were settled before this read (note at the top of Q1 in the
questions file). Retail `StarWarsG.exe` FoC x64 (Steam), Ghidra 12.1.4 through ReVa, 2026-11-18. Addresses are RVAs
(image base 0x140000000). Behaviour only, no decompiled code. **[H]** read and cross-checked, **[M]** read, one link
inferred, **[L]** inferred.

Kinds: **literal** = an immediate operand or a read-only constant in the code; **data** = an XML, GameConstants (GC)
or `.ted` field; **dynamic** = sized at run time from the content, with no fixed cap.

Ghidra: the functions below were renamed in the shared project (for example `PlanetType_MaxGroundbaseLevel` 0x372400,
`AI_NextStarbaseTeamType` 0x3f8fd0, `alTerrain_LoadMaterialTable` 0x1338b0, `Player_SetTechLevel` 0x288980,
`StoryEvent_Fire` 0x455db0), and each limit site has an EOL comment starting `mcx5 Q<n>`.

## Summary

| Q | Answer | Kind | Address | Conf |
|---|---|---|---|---|
| 1.2 | `Target.MaxGroundbaseLevel` divides min(5, ground slot maximum) by GC +0xF8. The min is in retail in both branches (type +0x92C without planet data, PD +0x230 with it), and its 5 is a literal, not GC +0xF8. The star base twin has no min | literal | 0x372400 (0x37240e, 0x37242d), 0x4b7eb0 (0x4b7eb6) | H |
| 1.5 | AI star base upgrade: when the current level **equals** 5, no upgrade is allowed. Otherwise the next level is allowed when it is ≤ the planet's maximum (data, unclamped). The 5 is a literal; GC `MaximumStarbaseLevel` is never read. The build-gate twins use the same literal | literal (maximum: data) | 0x3f8fd0 (0x3f91c6); 0x3f8f20 (0x3f8f94); 0x3f8b90 (0x3f8c04) | H |
| 1.6 | There are six fixed per-level slots, one for each galactic `AIGoalReachabilityType` value 0..5 (a code enum of 11 names). A level above 5 returns without doing anything; it is not clamped. No GC field | literal (code enum) | 0x665160, enum 0x5e70b0 | H |
| 1.7 | Both waits accept a level from 0.0 to 5.0 (a double constant). Outside that range they log the "between 0 and 5" error and return nil | literal | 0x69a6f0, 0x69a8b0, constant 0x1408b8570 | H |
| 1.8 | MagicPlan vetoes a type with tech > 5 (unsigned compare, literal). TechTree scores tech / 5.0 × 2 (literal). There is no other AI tech cap: `CanAdvanceTech` takes the next tech from data, and the player's max tech caps everything | literal; data | 0x726238, 0x7291ea, 0x6679a0 → 0x408040 | H |
| 1.9 | Defaults: +0xF0 = 1, +0xF4 = 1, +0xF8 = 1, +0xFC = 1, +0x100 = 3, +0x104 = 3, +0x108 = 1.0, +0x10C = 1.0. The load entry applies them before it parses the file | data default | 0x2c91c0 (0x2c94c9..0x2c950f), 0x2cb210 | H |
| 2.1 | There is no bound on the grid width or height. The header W/H resize the grid to 31·ceil((W−2)/31)+1, and every buffer is heap-sized from the file. The only implicit limits are the u16 cell coordinates (a side ≥ 65536 never ends the cell-grid fill: hang), 32-bit size products and memory. Mini 04, not W·H, sizes the vertex buffer, and nothing checks it | dynamic | 0x132a30, 0x131980, 0x5116c0, 0x133b10 | H (M hang) |
| 2.2 | The loader has no other fixed per-map array: patches, passability and cells are all dynamic. Fixed caps come later: the FOW grid is clamped to ≤ 512 cells per side (the cells get coarser), and the renderer's per-patch material scan has 64 entries | literal 512 (clamp), 64 | 0x4c0890 (0x4c099f), 0x1b62a0 (0x1b6a7c) | H |
| 2.3 | Mini 05 is read, but it only sets the material loader's reserve and loop count (0 → no materials at all). The table grows to hold every `3` record. Sim readers index it by the raw vertex byte without a check. The only 64 is the renderer's per-patch array, which an id ≥ 64 overruns | dynamic table; literal 64 (render) | 0x1338b0 (call 0x1330bb), 0x1b62a0, 0x38e0c0, 0x2696f0 | H (M table end) |
| 3.1 | Campaign: for each player, Set_Tech_Level(start), then max = max. With custom options, the options' start / max are used, minus 1 when the faction has `Displayed_Tech_Level_Adjustment`. Nothing clamps them on apply, except that Set_Tech_Level clamps tech to [0, current max]. The dialog's range is campaign `Min/Max_Custom_Tech_Level` (ctor 1 / 5) minus that adjustment. Tactical skirmish: tech 1, max = player default 5 | data (range, lists); literal (ctor defaults) | 0x292590, 0x288980, 0xed4b0, 0xed610, 0x4ae307, 0x27e04a | H |
| 3.2 | The top tech is data: campaign `Max_Tech_Level`, `Max_Custom_Tech_Level`, GC `MP_Default_Max_Tech_Level` and story SET_MAX_TECH_LEVEL (unclamped). The literal 5s are the player ctor's max default, the LOAD_CAMPAIGN tech option, MagicPlan / TechTree (1.8) and the hero respawn lists (exactly 5 entries) | data + literal defaults | 0x27df90 / 0x27e4a0, 0x456970 (0x457a4d, 0x458edc), 0x619d00 | H |
| 4.1 | A tactical battle has no fixed side count: the lobby slot vector, the player list and the per-player FOW grids are dynamic, and non-playable factions add players after the slots. Retail's 8 is the SP staging UI, which offers AI picks only while fewer than 8 rows are seated | literal (UI only) | 0x1013f0 (0x1401015e7), 0x2926c0, table 0x140a11f40 | M |
| 4.2 | Confirmed: CONSTRUCT_LEVEL tries at most 100 times (literal). After 100 failures it deletes the event and calls itself to roll a new one | literal | 0x4bec90 (0x4bee76; also 0x4bf770) | H |
| 4.3 | There is no round cap. Fire runs the rewards and, inside the same call, recursively fires armed STORY_TRIGGER dependants; game notifications raised by rewards dispatch synchronously. Only RESET_BRANCH has a depth limit (3). Notifications for a mode that is not running wait for that mode's next T9 | dynamic (recursion) | 0x455db0, 0x456970 (0x4582f7), 0x32e5f0 | H |

---

## Q1.2 Target.MaxGroundbaseLevel and the min(5, …)

The token's numerator comes from 0x372400 (planet type, planet object) [H]:
- **no planet object**, or an object without planet data (+0xB8 null): min(5, type +0x92C), unsigned;
- **with planet data**: 0x4b7eb0, min(5, PD +0x230), unsigned.

Type +0x92C is the planet type's ground-structure slot count, and PD +0x230 is its per-planet copy
(`MCX4_RE_ANSWERS_GALACTIC.md` Q2.1). **In both branches the 5 is an immediate operand.** Neither path reads GC +0xF8;
the token divides by GC +0xF8 afterwards (`AI_GALACTIC_TOKENS.md` row MaxGroundbaseLevel). So the port's min(5, …) is
retail's [H].

The star base twin 0x372780 returns PD +0x238 (with planet data) or type +0x668, with **no** clamp [H].

Other callers of 0x372400, so the same literal cap applies to all of them [H for the call list]:
- the galactic build gate 0x2804d0, through 0x3f8b90 (Q1.5);
- 0x39e920, 0x3f8c40 and the RANDOM_STORY CONSTRUCT_LEVEL builder 0x4bec90 (Q4.2);
- the two tokens 0x65e3e0 / 0x65e440.

Above 5: a planet type with more than 5 ground slots still reports a maximum ground base level of 5 everywhere.

**Port:** keep the min, tag it retail (0x372400 / 0x4b7eb0), and keep it a literal, not GC +0xF8.

## Q1.5 Star base upgrade choice (0x3f8fd0)

0x3f8fd0 maps a planet to the star base team type the AI should build there (`A2M_R3_RE_A.md` §1.2). The candidate is:
- the owner faction's `Rebel_Star_Base_1` / `Empire_Star_Base_1` when the planet has no star base;
- otherwise the current base's `Next_Level_Base` (+0x8C0).

The level gate [H, disassembly 0x3f916f..0x3f91e9]:
1. A planet whose owner field (+0x58) is −1, or that has a star base item in either production queue (0x401db0),
   allows level 0.
2. A star base with no owner (D +0x13C = −1) allows 1 when 0x372780 ≠ 0, else 0.
3. Otherwise, when the current level (D +0x148) **equals 5** (compare with the immediate 5, branch on equal), the
   allowed level stays 0. For any other level L, allowed = L + 1, or 0 when L + 1 > 0x372780 (PD +0x238 / type +0x668:
   data, unclamped).
4. The candidate is returned when its `Base_Level` (+0x8A0) ≤ the allowed level.

So the 5 is a **literal** and GC +0xF4 (`MaximumStarbaseLevel`) is not read here. The test is exact equality, so a level
6 base (possible only with mod data or a story spawn) may still go to 7 when the planet's maximum allows it. The
build-gate helpers have the same rule with the same literal [H]:
- 0x3f8f20: star base, level D +0x148, compare at 0x3f8f94;
- 0x3f8b90: ground base, level D +0x130 against 0x372400, compare at 0x3f8c04.

Those helpers are the human and AI build gate's side, which lane mcx4-gal covers.

**Port** (`galactic_ai.cpp` ~448 tests `starbase_level < 5`): the 5 is retail (cite 0x3f91c6). Retail's test is
`== 5`, which differs from `< 5` only for a level above 5.

## Q1.6 Source-planet levels (0x665160)

0x665160(reachability object, level m, force) [H]:
- It returns at once when m > 5. The compare is unsigned, so a negative m also returns. Nothing is computed and no
  list or timestamp is touched; the level is **not clamped**.
- The object holds **six fixed slots**, each a member of the object, not a vector:
  - a timestamp at +0x1D8 + 4m;
  - a flood request record at +0x120 + 0x10m;
  - an output list at +0x18 + 0x18m.
- The cache rule and the level flags are as in `FIXPASS_GALACTIC.md` §1.4 and `AI_GALACTIC_3.md` §8.

The level is a value of the code enum `AIGoalReachabilityType` (EnumConversion built in 0x5e70b0) [H]:

| Value | Name | Value | Name |
|---|---|---|---|
| 0 | Friendly_only | 6 | No_Threat |
| 1 | Friendly_Ignore_Threat | 7 | Low_Threat |
| 2 | Enemy_destination | 8 | Medium_Threat |
| 3 | Enemy_undefended | 9 | High_Threat |
| 4 | Single_Hop_Disconnected | 10 | Any_Threat |
| 5 | Any | | |

The six slots are the six galactic values. No GC field or XML sets the level range: it is the code enum. Callers test
the range themselves. For example Production Setup C's estimate 0x664210 runs the list only when the level is < 6
[H]; the other callers were not read [M].

**Port** (`galactic_ai.cpp` ~715, `std::clamp(level, 0, 5)`): the count of 6 is retail (cite 0x665160 and enum
0x5e70b0). Out of range, retail does nothing; it does not use level 5's list. So the port should return an empty
result instead of clamping.

## Q1.7 Lua WaitForStarbase / WaitForGroundbase bounds

`WaitForGroundbase` 0x69a6f0 and `WaitForStarbase` 0x69a8b0 [H]:
1. Exactly 2 arguments are required. The star base's count error string says "WaitForGroundBase" (a copy-paste in
   retail).
2. Argument 1 must be a planet game object (its object +0x335 ≠ −1).
3. Argument 2 must be a number with 0.0 ≤ value ≤ 5.0. The upper bound is the read-only double 5.0 at 0x1408b8570,
   shared by the two functions and one unrelated token clamp (0x6d3ff0). The message arguments are the immediates 0
   and 5.
4. Out of range: "Invalid value %d for parameter 2. Must be between 0 and 5." and the call returns nil (no wait object).
5. In range: a blocking object is created with the planet and the truncated level (+0x3C) [M for what it waits on].

Both bounds are **literals**; GC +0xF4 / +0xF8 are not read.

## Q1.8 MagicPlan tech test and the galactic AI tech cap

- **MagicPlan spawnable test** (Production marshaller Score 0x726010): the candidate is vetoed when the type's tech
  level (+0x89C) is above 5. The compare is with the immediate 5, branch unsigned-above, at 0x726238, so a negative
  tech is vetoed too. **Literal** [H].
- **TechTree score** 0x729190: a galactic produced unit with type tech > player tech (+0x84) is vetoed. Otherwise the
  score is tech / 5.0 × 2: the 5.0 is the float constant 0x140803520 and the 2.0 is 0x1408007d4. **Literal** [H].
- **No other cap in the AI:**
  - `CanAdvanceTech` 0x6679a0 asks 0x408040(tech + 1, faction) for the first type whose `Build_Advances_Tech_Level`
    (+0x894) equals tech + 1 and that is affiliated with the faction (data), then tests whether it is queued or
    buildable [H for 0x408040].
  - `HasTechToProduce` compares type tech with player tech (+0x84).
  - The `TechLevel` token returns player +0x84 unnormalised (`AI_GALACTIC_TOKENS.md`).
  - The effective top is the player's max tech +0x88 (Q3).

**Port** (`galactic_pp.cpp` 518 and 753): both 5s are retail (cite 0x7291ea and 0x726238). The MagicPlan compare is
unsigned.

## Q1.9 GameConstants defaults (0x2c91c0)

Written by `GameConstants_SetDefaults` 0x2c91c0, between 0x2c94c9 and 0x2c950f [H]:

| Field | Default | Shipped FoC `GameConstants.xml` |
|---|---|---|
| +0xF0 `MaximumPoliticalControl` | 1 | 5 |
| +0xF4 `MaximumStarbaseLevel` | 1 | 5 |
| +0xF8 `MaximumGroundbaseLevel` | 1 | 5 |
| +0xFC `MaximumSpecialStructures` | 1 | 5 |
| +0x100 `MaximumSpecialStructuresLand` | 3 | |
| +0x104 `MaximumSpecialStructuresSpace` | 3 | |
| +0x108 `MaximumFleetMovementDistance` | 1.0 (0x3F800000) | 100.0 |
| +0x10C (next float) | 1.0 | |

Order [H]:
- The ctor 0x2c4370 ends with this call.
- The GameConstants load entry 0x2cb210 calls it **first**, then parses the XML.

So a mod GameConstants that omits a tag gets these values, not the shipped ones. Effects:
- the AI divisors become 1;
- a missing `MaximumFleetMovementDistance` makes the AI's "near" radius 1.0 world unit, which leaves only
  route-connected neighbours.

`AITESTGAMECONSTANTS.XML` and `HYPERGAMECONSTANTS.XML` in the FoC data set 150.0. Which file a session loads was not
traced here.

**Port:** when a mod's GameConstants omits one of these tags, use these defaults, not the shipped values. (The
"tag absent -> 5 [port]" note at `galactic_ai.cpp` ~441 is about the planet type's star base maximum, type +0x668,
not GC; this read did not cover that type default.)

---

## Q2.1 Terrain grid limit

The terrain chunk `0x101` loader `alTerrain_LoadChunk` 0x132a30 (`TERRAIN_WATER_POST.md` §1.3) [H]:
1. **Header minis.** `00` / `01` (W / H), `04` (vertex count) and `05` (material count) go into locals. No range
   check is applied to any of them.
2. **Resize.** When W or H differs from the current grid, the loader calls `alTerrain_ResizeGrid` 0x131980 with
   (W − 2, H − 2). That rounds each side up to 31·ceil((W − 2)/31) + 1, which leaves every 31·n + 1 value unchanged.
   It then allocates W'·H' cells of 4 bytes (product in 32 bits; the ×4 is overflow-checked, and an overflow fails the
   allocation) and copies the overlap of the old grid. **No upper bound.**
3. **Child `5` (vertices).** The buffer is (re)sized to the header's mini `04` count, not to W'·H'. Mini `04` = 0
   frees it. Then the child's whole byte size is read into it. The chunk reader 0x220730 checks only that the read
   stays inside the chunk, not the destination size. So a child `5` larger than 4 × mini `04` overruns the heap
   buffer.
4. **Vertex lookup** 0x133b10 clamps (x, y) to the grid and indexes y·W + x into that buffer without comparing with
   its count (+0x28). So mini `04` < W·H gives out-of-bounds reads.

After the terrain chunk, the land loader 0x3b8cd0 builds the tactical cell grid (0x3b98a0 → `TacticalCellGrid_Init`
0x5116c0, 20 × 20 world units per cell) [H]:
- W·H cells of 0x1C bytes are allocated (product in 32 bits, ×0x1C overflow-checked).
- Each cell stores its column and row as **u16**.
- The fill loop's column and row counters are 16-bit, so a side of 65536 or more never ends the loop (hang). Below
  that, memory is the limit (65535² cells would need about 120 GB).

**Answer:** retail refuses, clamps and caps nothing. The only hard ceiling is 65535 vertices per side (the u16 cells),
beyond which the load hangs [M for the hang, H for the 16-bit counters]. Long before that, the 32-bit products and
memory end it.

**Port:** `ted.hpp` `kMaxGridVerts` = 1985 is not retail's. Under §8 it is a **guard**.

## Q2.2 Other fixed per-map arrays

The loader itself has none [H]:
- **Patches:** the renderer's patch list is a `DynamicVectorClass<alD3dTerrainPatch*>` (render ctor 0x18e060). There
  are ((W − 1)/31) × ((H − 1)/31) patches, each 32 × 32 vertices with 16-bit local indices (the patch size is fixed,
  the count is not).
- **Passability** `0x10A`: W·H × 4 bytes are allocated and exactly W·H × 4 bytes are read. A shorter chunk makes the
  read fail, and the loader reports failure [M for what the caller then does]. The legacy `0x104` reads W·H bytes the
  same way.
- **Cells:** dynamic (Q2.1), with u16 coordinates. The AI zones of `0x10B` also store (u16 x, u16 y) cells
  (`A1_READINGS.md`).
- **Materials:** dynamic (Q2.3).

Fixed sizes that a large map reaches downstream [H]:
- **Fog of war:** the `FOWGrid` ctor 0x4c0890 clamps W and H to 0..512 cells each (immediate 0x200 at 0x4c099f), then
  sets cell size = extent / count (`FOG_OF_WAR.md` §1.2). On land the box is the whole terrain [0, 20·W] with
  `DesiredLandFOWCellSize` 15, so vanilla's largest grid (373 vertices → 7460 units → 498 cells) is just under the
  clamp. From the next size up (404 vertices → 539 cells), **fog cells get coarser** instead of the map being refused.
  The threat grid follows: ceil(FOW count / 4) ≤ 128 per side.
- **Render per-patch layer scan:** 64 entries (Q2.3).

**Port:** fog at 512 per axis is retail (cite 0x4c0890, a clamp that coarsens the cells).

## Q2.3 `.ted` material slots

`alTerrain_LoadMaterialTable` 0x1338b0 is called for child `2` with the header's mini `05` value as its count (the
local stored by mini `05`, passed at 0x1330bb) [H]:
1. It clears the table (terrain +0xC0: data +0xC8, count +0xD0, capacity +0xD4, 0xB0-byte entries) and reserves
   capacity for `count` entries.
2. The outer loop runs `count` times. Inside, an inner loop reads material records (0x134b20) and appends each one,
   growing the vector by max(10, size/10), until a read fails. The first pass therefore takes **every** `3` record of
   child `2`, and later passes find nothing [M that 0x134b20 fails at the end of the child].
3. The count is never compared with the number of records. With mini `05` = 0, or missing, or child `2` placed before
   child `0`, **no materials are loaded at all**.

Readers index the table by the vertex material byte (0..255) with **no bounds check** [H]:
- the surface-type test in 0x38e0c0 (entry +0x84 compared with 9..11);
- 0x2696f0;
- the render patch build 0x1b62a0.

The render patch build marks the used materials of each patch in a **fixed 64-entry** int array: a 0x100-byte memset
at 0x1b6a7c, then a compaction loop over 0x40 entries. A material id ≥ 64 writes past that stack array. An id ≥ the
table size reads past the vector [H for the code; the effect is memory corruption, not a clamp].

**Answer:** the slot count comes from the file but only sizes the reserve and loop count. The table holds every record
the file has. The 64 is retail's renderer limit (0x1b62a0), not a loader limit. Retail has no defined behaviour for
ids ≥ 64.

**Port:** 64 is retail (cite 0x1b62a0, render per-patch scan). Dropping records past 64 and clamping ids ≥ 64 to 63
are a guard, because retail corrupts memory there.

---

## Q3.1 Starting and max tech on the game-options path

**Campaign start** (`Campaign_ApplyStartingTech` 0x292590, L10 in `A2I_T01_FRAME_ORDER.md`). For every player with a
faction, in PlayerList order [H]:
- **Default:** Set_Tech_Level(campaign `Starting_Tech_Level` for the faction, 0x4af410, sweep on), then max tech
  +0x88 = campaign `Max_Tech_Level` (0x4af2d0).
- **Custom options** (options record +0x20 bit 0x20): Set_Tech_Level(options +4 + a), then +0x88 = options +8 + a,
  where a = −1 when the faction's `Displayed_Tech_Level_Adjustment` (+0x2FB0) is non-zero, else 0.
- No range check at this step.

`Player_SetTechLevel` 0x288980 [H]:
- the level becomes max(level, 0) (anything below 1 becomes 0);
- then min(level, current +0x88);
- then it is stored in +0x84 and STORY_TECH_LEVEL is raised.

Because the start tech is set **before** +0x88 is written, it is clamped by the previous max. That is the player ctor's
5 at campaign start (0x27df90 / 0x27e4a0 write +0x88 = 5 and +0x84 = 0, immediate at 0x27e04a / 0x27e559). The max is
written unclamped.

**Custom settings dialog** [H]:
- The slider readers 0xed4b0 (start) and 0xed610 (max) map the slider's [0, 1] onto [campaign
  `Min_Custom_Tech_Level` (+0x22C) − adj, campaign `Max_Custom_Tech_Level` (+0x230) − adj], adj = the selected
  faction's `Displayed_Tech_Level_Adjustment`.
- The start is limited to ≤ the current max, and the max to ≥ the current start.
- The campaign ctor 0x4ae110 sets `Supports_Custom_Settings` 1, `Min_Custom_Tech_Level` **1**, `Max_Custom_Tech_Level`
  **5** (0x4ae307 / 0x4ae311), and `Max_Custom_Starting_Credits` 10000. No FoC campaign file sets the two tech tags,
  so 1..5 comes from these ctor defaults.
- The "reset" path 0xeda80 loads either the campaign's per-faction values or, with no faction selected, the GC
  `MP_Default_*` values (0x4acf00: GC +0x594 `MP_Default_Start_Tech_Level` 1, +0x598 `MP_Default_Max_Tech_Level` 5).
- The options record's GO_STARTTECHLEVEL / GO_MAXTECHLEVEL serialisation (0x74a90 / 0x757a0) parses integers without
  a range check.

**Tactical skirmish** (0x294fa0 / 0x295cf0 / 0x35cb10): every player gets Set_Tech_Level(1, no sweep) [H]. The max
stays the ctor's 5, because no skirmish path writes +0x88 [H for 0x294fa0; M for the others]. A
`Tactical_Build_Increments_Tech_Level` build raises allies to builder tech + 1 (0x498170), and the setter clamps that
to +0x88.

**Port** (`game_session.cpp` ~130, clamp to 1..5): retail takes the range from campaign `Min/Max_Custom_Tech_Level`
(default 1 / 5) shifted by the faction's adjustment. It is **data**, not a literal; the floor in the setter is 0.

## Q3.2 Data versus literal for the top tech

Data [H]:
- campaign `Max_Tech_Level` (per faction);
- campaign `Max_Custom_Tech_Level`;
- GC `MP_Default_Max_Tech_Level`;
- story SET_MAX_TECH_LEVEL (reward 0x5F in 0x456970). It writes the parsed number to +0x88 at 0x458edc with no clamp,
  so a story can lift the max above 5.

Other tech raises are capped by +0x88 through the setter:
- story SET_TECH_LEVEL raises +0x88 first when needed (0x461fc0);
- Lua `Set_Tech_Level` (0x604480) and production of a `Build_Advances_Tech_Level` item (0x402840) pass straight to the
  setter;
- the +1 path 0x282190 runs only while tech < max.

Literal 5s [H]:
- the player ctor's max default (above);
- the LOAD_CAMPAIGN story reward (0x456970, "Loading campaign %s, side %s"): its difficulty option is limited to ≤ 2,
  and its tech option is accepted only when 0..5 (unsigned compare at 0x457a4d). The mode-start code 0x44a3f0 then
  applies a pending tech < 6 to every player;
- MagicPlan's tech > 5 veto and the TechTree tech / 5 score (Q1.8);
- `Min_Respawn_Time_Per_Tech_Level` / `Max_Respawn_Time_Per_Tech_Level` (behaviour data validated by 0x619980 /
  0x619d00): each list must have **exactly 5** entries ("one entry per tech level. Expected 5 values"), otherwise both
  lists are dropped. The read of these lists by tech index was not traced [L].

UI: five `market_tech_level00..04` icon names exist [L for their use].

---

## Q4.1 Tactical battle sides

Dynamic in the battle itself:
- **Lobby slots** are a dynamic vector at 0x140b155e8: count +0x10, data +8, 0x1D8-byte slots. `0x2926c0` creates one
  player per slot from that count [H].
- **Extra players:** after the slots, one player per faction with `Create_Player_In_Multiplayer_Games` and not
  `Is_Playable` (`SKIRMISH_SETUP.md` §1; in FoC Pirates, Neutral, Hostile, Sarlacc, Hutts). So a battle with 8 slots
  has 13 players [H].
- **PlayerList** 0x140a16fd0 is a vector [H]. The per-player FOW grids sit at mode +0x198 by player id, with a count at
  +0x190 (`FOG_OF_WAR.md` §1.3) [M that the array is sized from the player count].
- No per-side array of 8 was found in the battle code [M: not an exhaustive search].

The 8 [M]:
- The single-player staging row rebuild 0x1013f0 walks the 9 row controls of the table at 0x140a11f40
  (`IDC_PLAYER_NAME_0` … `_8`, with matching `_FACTION_` / `_COLOR_` tables). It counts the rows whose state is a
  player (2..4), and it adds the Easy / Medium / Hard AI choices to the combos only while fewer than **8** rows are
  seated (immediate at 0x1401015e7).
- The results screen has 8 rows (`IDC_NAME_PLAYER_0..7`).

So 8 is a UI literal for seated players. The map's own start count and the MP host's `HO_MAXPLAYERS` were not traced
[L].

**Port** (`tactical_battle.hpp` `kMaxSides` = 8): retail's 8 limits seated lobby players (cite 0x1401015e7), not
battle sides. Per-side arrays indexed by player would need to hold the extra non-playable players too, so they should
be dynamic or sized from the player count.

## Q4.2 RANDOM_STORY CONSTRUCT_LEVEL retries

`RandomStory_BuildEvent` 0x4bec90, CONSTRUCT_LEVEL branch (switch case 5) [H]:
1. The loop runs while attempts < 100 (immediate 0x64 at 0x4bee76).
2. Each attempt:
   - draws a candidate planet (synced RNG 0x1ffb40);
   - draws ground or space (0..1);
   - compares the planet's current level (ground 0x3f8aa0, space 0x3f8b30 [M that these return the current level])
     with its maximum (0x372400, which includes the Q1.2 min(5, …), or 0x372780).
3. On the first planet below its maximum, the event takes the planet name as Param1 and a random level in
   [current + 1, maximum] as Param2.
4. **After 100 failures** the half-built event is deleted, and the builder calls itself to roll a new event type and
   trigger. There is no outer cap on that recursion: if no event can ever succeed, it recurses without end [L].

A second 100-try loop (0x4bf770) serves the Param2 planet pick of another trigger type (type 0xC needs a planet owned
by the local player), with the same delete-and-re-roll after 100 [H for the loop; M for the type's name].

**Port** (`story_random_story.cpp` `kMaxTries` = 100): the count is retail (cite 0x4bee76). Retail's failure path is
"re-roll a new event", not "give up".

## Q4.3 Story event cascades within one frame

There is no round or depth bound on cascades [H]:
- **`StoryEvent_Fire` 0x455db0:**
  1. It runs the rewards (0x456970).
  2. It walks the event's dependants in list order. A dependant that is not yet active and not triggered, with one
     prereq group fully triggered, is armed: active, and its timeout start stamped.
  3. A STORY_TRIGGER dependant (type 0x22) is **fired at once by a recursive call**; other types run their arm hook
     (vf +0x38).
  4. No counter limits this recursion.
- **Rewards that cause game events** (spawns, ownership changes, Lua `Story_Event`, TRIGGER_EVENT) dispatch
  synchronously inside the reward (`STORY.md` §4.1, §4.5). A notification for a story manager whose mode is not running
  is queued ("STORY DELAYED EVENT", 0x32e5f0) and replayed at the start of that mode's next T9. Tactical-only kinds are
  dropped instead.
- **The only limit** is RESET_BRANCH (reward 0x3C): a global nesting counter 0x140b3073c. When it is above 2 (a fourth
  nested reset), the reward logs "Event resets itself over and over forever" and does nothing (0x4582f7). Otherwise it
  increments the counter, runs, and decrements it (0x4583bd).
- A non-TRIGGER event armed by a cascade fires in the same frame only when its type is dispatched later in that frame
  (`STORY.md` §4.5).

**Port** (`story_session.hpp` `kMaxRounds` = 8, defer the rest one service): retail has no such bound. Its cascades
complete in the same call, limited only by the stack and the RESET_BRANCH depth of 3. Under §8 the 8 is not retail; it
is a guard only if it is far above real data and records `limit_hit`.
