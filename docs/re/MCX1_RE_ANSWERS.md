# mcx1 t01: answers from the binary (value semantics, XML robustness, fatal-at-load, element tags)

Answers to `MCX1_RE_QUESTIONS.md` (lane `mcx1`, Stage 9; design `docs/MOD_COMPAT_DESIGN.md` §7.1, §7.4). Retail
`StarWarsG.exe` FoC x64 (Steam 1.121), Ghidra 12.1.4 through ReVa, 2026-11-17. Addresses are RVAs (image base
0x140000000). Behaviour only, no decompiled code. Each answer keeps what was **read** in the decompiler apart from what is
**inferred**. Confidence as in `OBJECT_TYPES.md`: **[H]** read and cross-checked, **[M]** read, intent inferred, **[L]**
inferred, needs a probe.

Already settled and not repeated here: the `GameObjectType` rules of `OBJECT_TYPES.md` §4 (converters 0x51a280, last wins /
append, trim 0x23a4b0, `TBD` skip), duplicate names (`OBJECT_TYPES.md` §0 rule 3, `PHASE2_ANSWERS.md` §13).

## Summary for the port

| # | Question | Answer |
|---|---|---|
| Q1 | Which loaders share the per-child dispatch? | **All of them.** Faction, Campaign, GameConstants, TradeRoute, HardPoint and both ability kinds walk **every** child and send each through the one generic dispatcher 0x5180d0 → converter 0x51a280: repeated scalar = last wins, repeated list = append. No class uses a first-match named-child read. Values are trimmed for every file (all 62 calls of the XML reader pass trim on). `TBD` is skipped with a log line by Faction, Campaign, GameConstants, TradeRoute and HardPoint, **but not by the ability loaders**, which convert `TBD` like any value. Empty values never change a field. Abilities do **not** descend into nested elements; the other five do. [H] |
| Q2 | Bools outside `GameObjectType` | Same converter, same rule: true iff the value is exactly `1` or starts with `Y`/`y`/`T`/`t`. `On` → false, `10` → false, `Yes please` → true, `TBD` in an ability bool → **true**. [H] |
| Q3 | Integer / float overflow | `atoi` is the statically linked **UCRT** (VS 2019) one: clamps to INT_MAX / INT_MIN (`99999999999` → 2147483647, `-99999999999` → −2147483648, `2147483648` → 2147483647). int8 codes keep the low byte of the clamped value; code 0x4 clamps that **byte** (unsigned) to ≤ 100; code 0x7 compares the 32-bit value as **unsigned**, so negatives become 100. Float 0x8 stores `atof` as float with no check (`1e999` → +inf); 0x9 turns +inf into 1.0 (logged) and NaN into 0.0 (not logged). [H] (UCRT `atof` overflow result [M]) |
| Q4 | Second top-level element | (c): the reader shows a **message box** `xml parse error - File <path>: file has multiple root nodes. Aborting!` (title `Expect Strange Bugs...`) and returns failure, so the **whole file** is dropped. What "dropped" costs depends on the loader (table in Q6). No loader ever sees a second root. [H] |
| Q5 | UTF-16 | **Not decoded.** Any UTF-16 file (LE or BE, with or without BOM) fails the XML-declaration check: message box `... error reading xml header.`, file dropped. List files use the same reader. Also read: the `<?xml ...?>` declaration is **mandatory** for every XML file; bytes before the first `<` (a UTF-8 BOM, or anything else) are skipped. [H] |
| Q6 | Fatal at load | Missing or unparsable `GameConstants.xml`, `GameObjectFiles.xml`, `TradeRouteFiles.xml`, `CampaignFiles.xml`, `HardPointDataFiles.xml`: init fails and the process **terminates silently** (exit 0, no dialog of its own). The same for a *listed* trade-route, campaign or hardpoint file that opens but does not parse. `FactionFiles.xml` is **not** fatal: its failure is ignored, the game runs with zero (or fewer) factions. Zero factions: start-up survives; starting a galactic campaign with any planet dereferences a null neutral player (crash) [H read, M crash]. Skirmish fallback for an unknown faction: open (Q6.3). |
| Q7 | Element tags and galactic roles | The element-tag CRC (type +0x118) has **no reader other than** the `MultiplayerStructureMarker` test 0x295cf0 (tactical map load), found by scanning every instruction of the exe [H]. Galactic role comes only from the **generic `Behavior` list** DUMMY_* ids, tested in the fixed order 0x5B, 0x5C, 0x5F, 0x60, 0x5D, 0x5E, 0x61 by the create-at-planet routine 0x400b40; a type with none of them is not created ("Unknown type") [H]. A planet is any object that has the PLANET behaviour (id 3) attached, whatever its element [H]. |

---

## Q1. Which loaders share the table-driven per-child dispatch?

### Read [H]

**One dispatcher for every class.** 0x5180d0 is the generic DatabaseMapper dispatch. Each XML class has a small wrapper that
passes its own descriptor table to it (Faction 0x5187f0, Campaign 0x518080, GameConstants 0x518c00, TradeRoute 0x51a0a0,
HardPoint 0x518e80, SpecialAbility base 0x519c40, Unit_Ability 0x519c90, the `*_Ability` subclasses e.g. 0x517bd0 for
Absorb_Blaster; 123 call sites in all). 0x5180d0:
- empty tag → "not handled";
- empty value → "handled" with **no change** (no converter call);
- otherwise upper-cases the tag, CRCs it (0x215a30), binary-searches the class table and calls the type-code converter
  0x51a280 with the field offset and the variant flag (always 0 outside `GameObjectType`).

So the converter rules of `OBJECT_TYPES.md` §4.5 (and the list-append behaviour of §4.4 for a non-variant parse) hold for
every class below.

**Per-class entry parse.**

| Class | Entry parse | Walks | `TBD` | Unknown tag | Nested elements |
|---|---|---|---|---|---|
| Faction | 0x33e9a0 (from ctor 0x33a580) | every child of the `<Faction>` element | skipped, `Warning: FactionClass::Parse_Database_Entry() - %s key %s value is TBD.` | `... Unprocessed entry '%s' in object '%s'.` | descended, any depth |
| Campaign | 0x4af4a0 (from ctor 0x4ae110) | every child | skipped, `CampaignDataClass` warning | `Unprocessed entry` warning | descended |
| GameConstants | 0x2c8790, called by 0x2cb210 for **every top-level child of the root** | every element of the file | skipped, `GameConstantsClass` warning | `Unprocessed entry '%s'` warning | descended |
| TradeRoute | 0x5c3840 (from 0x5c3620) | every child | skipped, `TradeRouteClass` warning | `Unprocessed entry` warning | descended |
| HardPoint | 0x5401d0 (from 0x53e3b0) | every child | skipped, `HardPointDataClass` warning | `Unprocessed entry` warning | descended |
| `*_Ability` (galactic / tactical special abilities, `Abilities` sub-object list, code 0x38) | 0x48d340 (vtable +8 of each class, e.g. AbsorbBlasterAbilityClass vtable 0x1408c4c68) | every child, **one level** | **not skipped**: `TBD` goes to the converter | base table first, then the subclass table (vfunc +0xE0); neither → `Key-Value pair (%s, %s) is not recognized by this Special Ability ('%s')!` | **not descended** |
| `Unit_Ability` (`Unit_Abilities_Data` sub-object list, code 0x39) | 0x5659c0 | every child, one level | **not skipped** | same "not recognized" log | not descended |

Details read:
- The five data classes share one loop shape: at the top level the `Name` attribute is read; the element itself and then all
  of its children are visited in document order; a child that has element children is descended into (a static depth
  counter per class) and its children are parsed as fields of the same entry. This is the `GameObjectType` rule of
  `OBJECT_TYPES.md` §4.2 without the `SubObjectList` branch.
- The `TBD` test is the same everywhere it exists: exactly the 3 characters `TBD`, case-sensitive.
- The ability loader 0x48d340 needs a `Name` attribute; without one it writes
  `Error: Special Ability definitions must have a Name attribute!...` to the warning log and parses **no field** of that
  ability (the object is still created by the factory). 0x5659c0 has no `Name` check.
- Ability sub-objects are created by the `XMLLoadableFactory` (0x517990 → 0x68b300, class registry 0x68b500) from each child
  tag of the `Abilities` / `Unit_Abilities_Data` element; an unknown class tag logs
  `WARNING - Object Type '%s' not supported by XMLLoadableFactory!` and `WARNING - Unrecognized sub-object type '%s'...`
  and creates nothing.
- **Trim.** All 62 calls of the XML file reader 0x2397d0 pass trim = 1 (call list from the cross-references), so the value
  trim of `OBJECT_TYPES.md` §4.3 (0x23a4b0: leading / trailing space, tab, CR, LF) applies to every XML file the game reads,
  list files included.
- **List files.** The faction list (0x331d70) and campaign list (0x479fe0) read every child of the root as a file name and do
  not check the child's tag, like `GameObjectFiles.xml` (`OBJECT_TYPES.md` §1.2). The faction list keeps file order and
  duplicates (a file listed twice is parsed twice); the campaign, trade-route and hardpoint lists are keyed by the CRC of the
  listed name (de-duplicated, CRC order; `FALLBACK_CAMPAIGNS.md`).
- **Entry tags are not checked.** Every top-level child of a faction file becomes a faction (0x331d70), every top-level
  child of a campaign file a campaign (0x479fe0), whatever its tag, as for object types.

### Inferred
- Per-field results therefore follow from the converter alone: for these classes a repeated scalar tag is last wins and a
  repeated list tag appends (variant flag 0, so the "clear on every occurrence" kinds of `OBJECT_TYPES.md` §3.3 never clear).
  [H] by construction; no class-specific converter was found.
- Port consequence: `galactic_rules.cpp`, `ability_data.cpp` and `game_database.cpp` `load_campaigns` (first-wins named-child
  reads) differ from retail for any repeated tag. The ability loaders also differ in two retail quirks the port should
  copy: `TBD` is a real value there, and nested elements are ignored. [port decision for mcx1]

## Q2. Booleans in the non-type classes

### Read [H]
Converter 0x51a280 case 0x0, shared by every class (Q1): true when the value is exactly the one character `1`, or when its
first character is `Y`, `y`, `T` or `t`; otherwise false. The value is already trimmed.
- `On` → false, `10` → false, `0` → false, `True` / `yes` / `Y` → true, `t0` → true.
- In the ability classes `TBD` reaches this converter (Q1), so a `TBD` bool there is **true**; in the five data classes it is
  skipped and the field keeps its default.

### Inferred
- `GameConstants` bools in the port (`values.cpp` `parse_bool`: yes/no/true/false/on/off/1/0) differ for `On` (port true,
  retail false) and for unknown words (retail false). [H] from the read rule.

## Q3. Integer and float overflow

### Read [H]
- Codes 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7 call 0x775f84, which Ghidra's library ID matches as the Visual Studio 2019 UCRT
  `atoi` / `atol`: it calls `__crt_strtox::parse_integer<unsigned long>` (0x774aec) with base 10, signed result. That routine
  skips leading white space, takes one optional `+` / `-`, reads decimal digits only (`0x10` → 0), and **clamps on
  overflow**: a positive overflow returns 0x7FFFFFFF, a negative one 0x80000000 (errno ERANGE, ignored).
  - `99999999999` → 2147483647; `-99999999999` → −2147483648; `2147483648` → 2147483647; `-2147483648` → −2147483648.
- Per code, applied to that clamped 32-bit result:
  - 0x1, 0x5, 0x6: stored as int32.
  - 0x2, 0x3: the low byte is stored (`99999999999` → 0xFF, `-99999999999` → 0x00, `300` → 44).
  - 0x4: the low byte, read as **unsigned**, is clamped to ≤ 100 (`300` → 44, `-1` → 100, `99999999999` → 100).
  - 0x7: the 32-bit value, compared as **unsigned**, is clamped to ≤ 100 (any negative → 100).
- Codes 0x8, 0x9, 0xA, 0xB call 0x7857c4 → UCRT `common_atof_l`, giving a double.
  - 0x8: converted to float and stored, **no check**.
  - 0x9: converted to float; if it is < 0 or > 1 the `DatabaseMapper: Key(%s) needs to have a value between 0 and 1
    inclusive, but value is %f!` line is logged; then the stored value is the input when 0 ≤ v ≤ 1, else 1.0 when v ≥ 0
    is true, else 0.0. So +inf → 1.0 (logged), −inf → 0.0 (logged), **NaN → 0.0 (not logged)**.
  - 0xA: double stored as is; 0xB: double clamped like 0x9.

### Inferred [M]
- UCRT `atof` returns ±HUGE_VAL (±inf) on overflow, so `1e999` stores +inf in a code-0x8 float and nothing rejects it;
  a finite double above FLT_MAX (`1e39`) also becomes +inf on the float conversion. UCRT `atof` also accepts `inf`,
  `infinity`, `nan` and hexadecimal floats (`0x1p4`). (Library behaviour; the call chain is [H].)
- Port: `parse_int_prefix` clamping to the int32 range matches retail for codes 0x1/0x5/0x6. The narrow codes need the
  byte / unsigned rules above.

## Q4. Several top-level elements in one XML file

### Read [H] (0x2397d0, element parser 0x23a6b0, value reader 0x23a4b0)
1. The reader logs `xmlParseFile(%s);`, opens the file through the VFS and reads it whole into a buffer, raw.
2. It skips bytes up to the first `<`. The byte after it must be `?` (the XML declaration), which is skipped up to the next
   `>`. Otherwise (Q5) it fails.
3. It skips white space and `<!-- -->` comments, then parses **one** element as the root (the root node object is created
   here; there is no synthetic document node).
4. If the root has **no element children**, it fails with `file only has a root node with no other content.`
5. Otherwise it scans the rest of the file: comments are skipped, other text is ignored, but **any `<` that does not start a
   comment** (a second element, a stray end tag, a second `<?xml ...?>`) fails with `file has multiple root nodes.
   Aborting!`
6. On success the reader's cursor is set to the root's **first child**; every loader walks the root's children with the
   next-sibling call 0x239d40.

Every failure (also `file has syntax errors, see log file` from the element parser, and `error reading xml header.`) shows a
modal `MessageBoxA` with the text `xml parse error - File <path>: <reason>` and the caption `Expect Strange Bugs...`
(icon "error"), then returns a negative code (0x8064xxxx). The DOM is discarded by the caller. A file that cannot be opened
returns the same kind of code **without** a message box.

### What each loader does with a failed file [H]
| Loader | On a failed file |
|---|---|
| object-type file (0x408290, called by 0x408830) | `Error: Can't create objects from game object database file %s`, file skipped, loading goes on |
| faction file (0x331d70) | `Error: Can't create data entries from database file %s`; **the faction loader returns at once**: later faction files and the post-pass are skipped (factions already read are kept) |
| campaign / trade-route / hardpoint file (0x479fe0 / 0x4715e0 / 0x4738b0) | `Error: Can't create ... from database file %s`, loader returns false → **fatal** (Q6) |
| `GameConstants.xml` (0x2cb210) | `Error - can't open XML file %s`, `Error: Can't create database data from file %s` → **fatal** (Q6) |

### Inferred
- So the answer is (c), with a dialog, and the whole file is dropped; the port's "first root only, silent" reading keeps
  data retail throws away. [H]
- `OBJECT_TYPES.md` §6.3 said syntax errors skip the file "from the first error on" [M]; the read code drops the **whole**
  file (no type is created from it) [H].

## Q5. UTF-16 XML and list files

### Read [H] (0x2397d0, file read 0x2139d0)
- The file bytes are read with `ReadFile` or the archive reader and used as they are; nothing looks for a byte-order mark or
  converts an encoding.
- UTF-16 LE with BOM (`FF FE 3C 00 3F 00 ...`), UTF-16 BE with BOM (`FE FF 00 3C 00 3F ...`) and both without BOM: step 2 of
  Q4 finds the `<` byte and then a `00` byte instead of `?`. The reader logs `xml parse error: file %s does not appear to be
  an xml file, it's missing the standard xml header.`, shows the message box `xml parse error - File <path>: error reading
  xml header.` and fails. The file is then handled as in the Q4 loader table.
- List files go through the same reader, so a UTF-16 `GameObjectFiles.xml`, `TradeRouteFiles.xml` or `CampaignFiles.xml`
  is fatal and a UTF-16 `FactionFiles.xml` leaves the game without factions (Q6).
- UTF-8 with BOM works because the BOM bytes come before the first `<` and are skipped (agrees with `MODC_LOADER.md` §1).

### Read, beyond the question [H]
- **The XML declaration is required.** A file whose first `<` is not followed by `?` (for example one that starts directly
  with its root element, or with a comment) fails with the same "missing the standard xml header" message box. All 627 FoC
  and 366 EaW XML files in `extracted/` have a declaration (survey 2026-11-17), so vanilla never hits this.
- Anything before the first `<` is skipped, not only a BOM.
- The declaration's content is not checked (`encoding=` is ignored); it is skipped up to the first `>`.

## Q6. Fatal-at-load cases

### Q6.1 Missing or unreadable core files

Main init 0x4b250 checks each manager's result; on failure it writes one line to `_warn_message_popup.txt` (logger 0x214a70,
which writes only when logging is on), shuts the managers down (0x4da50) and returns false. WinMain (0x5d990) then ends the
timer period, releases the single-instance mutex and calls `TerminateProcess(self, 0)`. **No dialog of its own is shown**;
the game window (created before main init) just closes. [H]

| File | Handling | Log anchor | |
|---|---|---|---|
| `GameConstants.xml` missing or unparsable | fatal | `Error: Unable to initialize TheGameConstants.` | [H] |
| `GameObjectFiles.xml` missing or unparsable | fatal (0x408830 returns 0) | `Error: Unable to initialize GameObjectTypeManager.` | [H] |
| `TradeRouteFiles.xml` missing or unparsable, or a listed route file that opens but does not parse | fatal (0x4715e0 returns 0) | `Error: Unable to initialize TradeRouteManager.` | [H] |
| `CampaignFiles.xml` missing or unparsable, or a listed campaign file that opens but does not parse | fatal (0x479fe0 returns false) | `Error: Unable to initialize CampaignDataManager.` | [H] |
| `HardPointDataFiles.xml` missing or unparsable; a listed hardpoint file that does not parse | fatal (0x4738b0 returns false on the list; for a listed file it has the campaign loader's code shape) | `Error: Unable to initialize HardPointDataManager.` | [H] list, [M] listed file |
| `TradeRouteLines.xml` (0x467740) failure | fatal; the message is mislabelled | `Error: Unable to initialize FactionList.` | [H] |
| `FactionFiles.xml` missing or unparsable | **not fatal**: the return of 0x331d70 is ignored; the game goes on with no factions | none beyond the reader's | [H] |
| a listed file that cannot be opened (any of the lists) | skipped | `Error: Unable to open listed XML file '%s'` (faction, campaign, route, hardpoint lists); `Read_Parse_Game_Object_Databases failed to find specified file` (object types) | [H] |

A syntax error in any of these files additionally shows the reader's message box (Q4) before the process ends; a missing
file shows none. [H]

### Q6.2 Zero factions

Read [H]:
- Start-up survives: the faction list loader's result is ignored (0x4b250 at 0x4ca9d). A `FactionFiles.xml` with no entries
  fails the reader's "only a root node" check (message box), so it behaves like a missing list. A faction file with no
  element in its root fails the same way and stops the faction loader (Q4 table).
- With no faction whose `Is_Neutral` is set, object types with an empty `Affiliation` stay without a faction
  (`OBJECT_TYPES.md` §9.2).
- `PlayerListClass::Create_Default_Players` (0x293390) creates one default player per faction not already taken by a setup
  slot; with zero factions it creates none.
- The skirmish setup fills the faction combo boxes from the faction list (playable flag +0x20a8, 0xfa1c0); with zero
  factions they are empty.
- Galaxy creation 0x292050 (galactic mode) looks up the neutral faction (0x331cc0) and then the player owning it, and
  reads that player's id (+0x4C) **without a null check** when the campaign has at least one `Locations` entry.

Inferred:
- Starting a galactic campaign with zero factions, or with factions but **none marked neutral**, dereferences a null player
  and crashes (access violation) [M: the read is certain, nothing earlier was seen to block the start].
- Skirmish start with zero factions: not traced [L].

### Q6.3 Unknown skirmish faction or planet — open
- Read [H] (`SKIRMISH_SETUP.md` §2.8): the profile keeps the local skirmish faction as an **index** into the playable-faction
  list, not a name, so "a faction name that does not exist" cannot arise from the profile; the AI slot falls back to index
  0 when its index has no faction.
- Not traced: what the local slot does when the saved index is past the end of a shorter mod list, and what the map list
  does with a saved or multiplayer map name that does not exist. [L] Needs a further read of the staging setup 0xf1230 /
  0xfe7c0 and the map list, or the `modc_t09` probe kit.

## Q7. Element tags and galactic roles

### Q7.1 Readers of the element-tag CRC (type +0x118)

Read [H]:
- Scan of every instruction in every `.pdata` function of the exe (capstone, 2026-11-17) for a memory operand with
  displacement 0x118: 989 sites, 137 of them 4-byte accesses, 42 address-of (`lea`). In the `GameObjectType` code only these
  touch +0x118:
  - 0x376fae (parse 0x376f90): writes the CRC;
  - 0x36c000 (copy-assign, variant resolve): copies it;
  - 0x376d10 / 0x376e41 (variant resolve): saves and restores the child's own value;
  - 0x3712e0 (getter): takes its address to look the tag string up in the CRC → tag map 0x140b2c240.
- The getter's only caller is 0x295cf0, which compares the tag case-insensitively with `MultiplayerStructureMarker` while a
  tactical multiplayer map is set up (`OBJECT_TYPES.md` §2). It does not run on the galactic map.
- Every other 0x118 site belongs to another structure (inspected: member-wise copies of other classes, the campaign's
  vector count at 0x4af2d0, the object save reader 0x380cc0, a render object at 0x12c8d0, CRC tables, CRT code).

Inferred [M]: a reader that reaches +0x118 through a computed offset or a 16-byte block copy would not show in the scan;
none is known. With that caveat, `OBJECT_TYPES.md` §0 rule 2 "no other reader" moves from [M] to [H] for direct reads, and
**no reader affects the galactic map**.

### Q7.2 How a type's galactic role is decided

Read [H]:
- The routine that creates a type at a planet, 0x400b40 (callers: starting forces, production, story rewards, Lua spawns,
  AI hijack; `A2K_T07_STORY_LOAD_RE.md` §2), tests the behaviour ids with 0x370320 and set index −1, which selects the
  **generic `Behavior` bitset** (type +0x790; set 0 galactic +0x750, 1 land, 2 space, 3 union, 4 generic). The order is
  fixed and the **first match wins**:
  1. 0x5B DUMMY_GROUND_BASE: nothing created;
  2. 0x5C DUMMY_STAR_BASE: becomes the planet's starbase (level from `Base_Level`);
  3. 0x5F DUMMY_GROUND_STRUCTURE: ground structure slot (`Special ground structure '%s' constructed at %s.`);
  4. 0x60 DUMMY_ORBITAL_STRUCTURE: orbital structure slot;
  5. 0x5D DUMMY_GROUND_COMPANY: the planet's ground forces (0x400f30);
  6. 0x5E DUMMY_SPACE_FIGHTER_SQUADRON: an orbiting squadron (0x400650);
  7. 0x61 DUMMY_STARSHIP: a fleet of one (0x401660);
  8. none of them: `* * * Unknown type '%s' constructed at %s.`, **nothing is created**.
- The element tag is never read on this path (Q7.1). The other galactic readers already documented also test behaviours, not
  the tag: the space / ground power totals (0x61 / 0x5E / 0x5D, `AI_GALACTIC_TOKENS.md`), the production queue split (0x5F,
  `A2H_GALACTIC_AI_RE.md`), the ground-base level recount (0x5F, `A2M_R2_RE_A.md`).

So for `HeroUnit`, `UniqueUnit`, `GroundBuildable`, `SecondaryStructure`, `SpaceBuildable`, `SlaveCompany`, `Props_Story`,
`CIN_SpaceUnit` (or any tag): the role is the first DUMMY_* id in that type's generic `Behavior` list, in the order above. A
DUMMY_* id listed only under `GalacticBehavior`, `LandBehavior` or `SpaceBehavior` does **not** count for this dispatch.
A type with two of them takes the earlier one (for example DUMMY_GROUND_COMPANY wins over DUMMY_STARSHIP).

Inferred: the port's `galactic_unit_kind` should drop the element-tag table and the "vanilla unclassified elements stay
Other" exception and classify by these behaviours alone; per `OBJECT_TYPES.md` §2 this changes nothing for vanilla types
that the tag table already classified. [M: the port-side effect on vanilla is from that survey, not re-checked here]

### Q7.3 Is a planet any type with the PLANET behaviour?

Read [H] (galaxy creation 0x292050, object query 0x2a9ff0):
- Pass 1 creates one object for each campaign `Locations` entry at the type's `Galactic_Position` (type +0xDC0), owned by
  the neutral player, **without checking any behaviour or tag**.
- Pass 2 applies starting forces only to objects that have the PLANET behaviour (id 3) attached (object slot +0x335 ≠ −1),
  and the owner pass walks the object manager's PLANET list (GOM +0x560, behaviour 3). So a `Locations` entry without PLANET
  is created as a plain object but is not a planet: no starting forces, no owner, not in the planet list.
- Behaviours are attached from the generic `Behavior` list and then the mode list (`GalacticBehavior` in galactic mode)
  (`OBJECT_RUNTIME.md` §4.2, 0x38cb30).

Answer: yes. In galactic mode a planet is any object whose `Behavior` or `GalacticBehavior` list holds `PLANET`, whatever its
element tag, and planets exist only when a campaign's `Locations` (or a spawn) creates them. [H]

## Function index (RVAs)

| RVA | Role |
|---|---|
| 0x2397d0 | XML file reader (declaration, root, multiple-root and empty-root checks, message box) |
| 0x23a6b0 / 0x23a4b0 | element parser / value reader (trim) |
| 0x239d40 / 0x239bf0 / 0x239c50 | cursor: next sibling / tag / value |
| 0x5180d0 | generic per-field dispatch (empty value = no change) |
| 0x51a280 | type-code converters (bool, ints, floats, lists) |
| 0x775f84, 0x774aec | UCRT `atoi`, `parse_integer` (clamping) |
| 0x7857c4 | UCRT `atof` (`common_atof_l`) |
| 0x33e9a0, 0x4af4a0, 0x2c8790, 0x5c3840, 0x5401d0 | Faction, Campaign, GameConstants, TradeRoute, HardPoint entry parse |
| 0x48d340, 0x5659c0 | SpecialAbility, Unit_Ability parse |
| 0x517990, 0x68b300, 0x68b500 | sub-object list → XMLLoadableFactory, class registry |
| 0x331d70, 0x479fe0, 0x4715e0, 0x4738b0, 0x408830, 0x2cb210 | loaders: factions, campaigns, trade routes, hardpoints, object types, GameConstants |
| 0x4b250, 0x4da50, 0x5d990 | main init, shutdown, WinMain (TerminateProcess on init failure) |
| 0x293390 | Create_Default_Players |
| 0x292050, 0x2a9ff0 | galaxy creation, object query by behaviour |
| 0x400b40, 0x370320 | create type at planet (DUMMY_* dispatch), behaviour-bit test |
| 0x3712e0, 0x295cf0 | element-tag getter, its only caller |

ReVa plate comments were set on 0x2397d0, 0x5180d0, 0x48d340 and 0x5659c0.
