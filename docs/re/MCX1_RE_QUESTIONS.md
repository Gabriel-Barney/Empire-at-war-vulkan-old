# mcx1 t01: questions for the binary (value semantics, XML robustness, fatal-at-load)

Asked by lane `mcx1` (Stage 9, group `mcx1`, task t01; design `docs/MOD_COMPAT_DESIGN.md` §7.1, §7.4). Answers go into
**`docs/re/MCX1_RE_ANSWERS.md`** (one section per question, RVAs named, read vs inferred kept apart), with a pointer line
in `OBJECT_TYPES.md` §4.4 / §4.5 where the answer widens a rule stated there for `GameObjectType` only.

What is already settled (do not re-derive):
- `GameObjectType`: converters by type code at 0x51a280 (`OBJECT_TYPES.md` §4.5); repeated tags: scalars last wins, lists
  append (§4.4); values trimmed by the XML reader 0x23a4b0 (§4.3); element tag stored as a CRC with one known reader (§0 rule 2,
  [M] for "no other reader").
- Duplicate *names*: GameObjectType and Faction first wins for lookups (§0 rule 3, §9.2); TradeRoute and Campaign last
  definition wins, whole-object replacement (`PHASE2_ANSWERS.md` §13).
- The only load-failure path for object types is a list file that cannot be read (§6.3).

## Q1. Which loaders share the table-driven per-child dispatch?

The descriptor tables in `xml_field_tables.md` have segments for `Faction` (310 fields), `Campaign` (51), `GameConstants`
(615), the `*_Ability` classes, `TradeRoute`, `HardPoint`, `SFXEvent`, `MusicEvent`, `Hero_Clash`, `RadarMapSettings`, ...

For each of **Faction, Campaign, GameConstants, the galactic / tactical ability classes (`Unit_Abilities_Data` sub-objects and
the `*_Ability` segments), TradeRoute, HardPoint**:
1. Is the entry parsed by iterating **every** child element and dispatching each through the type-code converter (0x51a280 or
   an equivalent), so that a repeated scalar tag is **last wins** and a repeated list tag **appends**? Or does the loader look up
   a named child (first match wins, later ones ignored)?
2. Are the values trimmed the same way (0x23a4b0 with trim enabled) for these loaders?
3. Is a value of exactly `TBD` skipped, and an empty value left unchanged, as for object types?

Name any class that deviates and how. Port today: faction, ability and campaign loaders use first-wins named-child reads
(`engine/galactic/src/galactic_rules.cpp`, `engine/tactical/abilities/src/ability_data.cpp`,
`engine/data/src/game_database.cpp` `load_campaigns`), and `GameConstants` reads its bools with a yes/no/true/false/on/off/1/0
parser (`engine/data/src/values.cpp` `parse_bool`).

## Q2. Booleans in the non-type classes

For a type-code 0x0 field of `GameConstants`, `Faction`, `Campaign` and the abilities: the same rule as object types (exactly
`1`, or first character `Y`/`y`/`T`/`t`, else false)? In particular: is `"On"` false and `"10"` false there too?

## Q3. Integer and float overflow

The int converters (codes 0x1, 0x5, 0x6, and the int8 / uint8 / uint32 codes) call C `atoi`. Which CRT routine is it (statically
linked, which version), and what does it return for `99999999999`, `-99999999999` and `2147483648`: clamped to INT_MAX /
INT_MIN (strtol-style), or wrapped? For codes 0x2 / 0x3 (int8) and 0x4 / 0x7 (clamp ≤ 100): is the clamp / truncation applied to
the already-clamped 32-bit value? For float code 0x8: `atof("1e999")` gives +HUGE_VAL as double; is it stored as float +inf, and
does any check reject it? Port today: `parse_int_prefix` clamps to the int32 range (`engine/data/src/values.cpp`).

## Q4. Several top-level elements in one XML file

The XML reader (find it from 0x23a4b0 or the `XML syntax error` style log strings) builds a DOM. When a file has a second
top-level element after the root closes (`<A>...</A><B>...</B>`), does the reader:
- (a) keep parsing and expose `<B>` to the loaders (as a sibling root, or as a child of a synthetic document node); or
- (b) stop at the end of the root silently; or
- (c) log an error and drop everything after it (or the whole file)?
And do the object-type loader (0x408290) and the faction / campaign loaders read only the first root's children? Port today:
`engine/data/src/xml.cpp` reads the first root only (silent).

## Q5. UTF-16 XML and list files

Does the XML reader recognise a UTF-16 LE (`FF FE`) or BE (`FE FF`) byte-order mark, or a UTF-16 file without a BOM, and decode
it? If not, what happens with such a file: a logged syntax error and the file skipped, or something else? Same question for the
list files (`GameObjectFiles.xml`, `FactionFiles.xml`, `CampaignFiles.xml`). Port today: refuses the file as fatal
(`engine/data/src/xml.cpp`, "UTF-16 text is not supported"). Known: UTF-8 with BOM is read (`MODC_LOADER.md` §1 t14).

## Q6. Fatal-at-load cases

1. A **missing core list file** (`GameObjectFiles.xml`, `FactionFiles.xml`, `CampaignFiles.xml`, `TradeRouteFiles.xml`) or a
   missing `GameConstants.xml`: does retail exit / show an error box / continue with an empty list? Quote the log or message
   string it uses (as a string anchor; the port writes its own wording).
2. **Zero factions** (`FactionFiles.xml` lists no file, or the files hold no `<Faction>`): what happens at start-up and when a
   campaign or skirmish starts?
3. **Unknown skirmish faction or planet**: the skirmish setup (`SKIRMISH_SETUP.md`) reads factions from the faction list and maps
   from the map list. When a saved skirmish setting (last-used faction / map in the profile or registry) or a multiplayer map's
   data names a faction or map that does not exist, does the menu fall back to the first valid one, show the name, or fail?

## Q7. Element tags and galactic roles (t05, D4)

`OBJECT_TYPES.md` §0 rule 2 rests on one known reader of the stored element-tag CRC (`MultiplayerStructureMarker`) and marks
"no other reader" [M]. Please:
1. List every reader of the element-tag CRC field (cross-references to its offset), and say whether any of them affects the
   galactic map: which types are planets, which are starbases, ground structures, squadrons, space units, ground units.
2. How does retail decide a type's galactic role (what a fleet, a planet's ground forces and its structure slots may hold) for a
   type whose element is `HeroUnit`, `UniqueUnit`, `GroundBuildable`, `SecondaryStructure`, `SpaceBuildable`, `SlaveCompany`,
   `Props_Story`, `CIN_SpaceUnit`? By behaviour only (DUMMY_* ids 0x5B..0x61, `A2K_T07_STORY_LOAD_RE.md`), by category mask, by
   another field? Port today: `engine/galactic/src/galactic_rules.cpp` `galactic_unit_kind` uses an element-tag table plus a
   "vanilla unclassified elements stay Other" exception marked `[port ... open, needs a decision]`.
3. Is a planet any type with the `PLANET` behaviour, whatever its element?
