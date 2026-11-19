# Mod compatibility system: design

**Status: accepted with Gabriel's decisions D1-D7 (2026-11-17, §17, `DECISIONS.md` "Mod compatibility system
decisions"). The work is roadmap Stage 9 · Mod compatibility system (groups `mcx0`-`mcx9`, §16).** It turns the evidence in
[`MOD_AUDIT_2026-11-17.md`](MOD_AUDIT_2026-11-17.md) into a system whose goal is that **most, if not all, Forces of
Corruption mods play in the port as they do in retail**.

How this document relates to the others:
- It extends the binding contract in [`MODS.md`](MODS.md) and the decisions "Mod compatibility is agnostic" (2026-11-03)
  and "Mod compatibility target" (2026-11-13) in [`DECISIONS.md`](DECISIONS.md). It does not replace any of them.
- When Gabriel accepts a part, its rules move into `MODS.md`, the decision into `DECISIONS.md`, and the work onto the
  roadmap (§16). This file then keeps only the reasoning.

Contents:
1. Goal, scope and the definition of "compatible"
2. Principles
3. Where the port stands (audit summary)
4. Architecture
5. Mod registry, identity and stacks
6. Mount layer
7. Data layer: the tag ledger and retail value semantics
8. Limits policy
9. Scripting layer
10. Assets and presentation
11. Fault containment
12. Diagnostics: load report v4 and the compatibility grade
13. The compatibility corpus and its runner
14. Retail as the oracle
15. Saves, mod versions and the launcher
16. Delivery plan (roadmap groups with exit tests)
17. Decisions (Gabriel, 2026-11-17)
18. Risks
Appendix A. Glossary · Appendix B. Gap index (audit → design section)

---

## 1. Goal, scope and the definition of "compatible"

### 1.1 Goal

A mod that uses Forces of Corruption's data mechanisms (XML, Lua 5.0 scripts, MEG archives, Alamo asset formats, effect
files, maps, text, video) **loads, looks, sounds and plays in the port as in retail**. Where it does not, the port says
why, names the mechanism, and the gap is fixed once as a general mechanism, never for one mod.

"Most, if not all" is measured, not hoped for:
- over a **corpus** of mods (§13) that grows with what Gabriel installs;
- with a **grade per mod** (§1.3);
- and a **coverage number per mechanism** (§7.2, §9.1, §10.1).

The corpus target is in §16.

### 1.2 In scope, out of scope

| In scope | Out of scope (unchanged decisions) |
|---|---|
| FoC mods: loose `Data\`, own MEGs, `MODPATH=` / `STEAMMOD=`, submods and stacks of any depth | Multiplayer (no networking; DECISIONS) |
| Every XML database the retail boot list loads, and every tag in retail's descriptor tables | The original game's save files |
| Lua 5.0.2 scripts, every retail binding, story / AI / object / scoring / free-store scripts | 32-bit Lua bytecode (retail x64 rejects it too) |
| ALO, ALA, particles, DDS / TGA / BMP, `.ted`, `.fx` / `.fxo`, WAV / MP3, Bink 1, DAT text, fonts, GUI files | Mods that patch the game executable or inject native code (none found, audit §7) |
| `modinfo.json` (the community `eaw.modinfo` spec) for names, languages and dependencies | Matching retail's crashes or hangs (§2 P6) |
| Detecting base-EaW mods so they are refused with a clear reason (D1) | Base-EaW mods (`sweaw MODPATH=`): FoC mods only (D1) |
| | Speed while tier-3 work is paused: a too-slow mod is filed, not fixed (DECISIONS "Current focus") |

### 1.3 Compatibility grades

One grade per mod (and per stack), computed by the corpus runner (§13) from the load report (§12) and the headless
checklist (`MODS.md` §10.1). Each grade includes the ones below it.

| Grade | Name | Meaning | How it is checked |
|---|---|---|---|
| G0 | Refused | The port will not start it, with a reason. Only allowed reasons: §1.2 out-of-scope, or the mod's own data is unplayable in retail too | `alamo_mod_check` error |
| G1 | Loads | Load report has 0 errors. Every warning is either a retail-equivalent finding (§12.2) or a filed gap | `alamo_mod_check` |
| G2 | Runs | Headless space and land skirmish on the mod's maps, and one galactic campaign, 3,000 / 9,000 ticks. AI orders and decisions; no port-caused Lua error, crash or hang; deterministic on 0 / 4 workers and both compilers | `alamo_headless` (the §10.4 runs, automated) |
| G3 | Plays | G2, plus: front end reaches menu, staging and campaign list; hidden smokes reach a running skirmish and a running campaign; **no honour gap, unconsumed binding or asset reject in content the mod uses** (§12.3) | smokes + report v4 |
| G4 | Matches | G3, plus: retail differential checks on the mod agree (§14). Tier-1 rules exact, tier-2 metrics within the distribution | probe kits, Gabriel's played battle |

`modc`'s current exit test is roughly G2+ for four mods. This design raises the bar to **G3 for the whole corpus**,
with G4 on the reference set.

---

## 2. Principles

The six rules of `MODS.md` §1 stay as they are: no mod-specific code, no fixed faction lists, retail hard-codes cited,
mechanisms rather than identities, base FoC unmoved, general fixes. This design adds these:

**P1. Retail is the oracle, and the binary answers first.**
- Every behaviour a mod can reach is settled by `docs/re/` (through `re-question`) or a probe kit, never by guessing.
- A port choice where retail is unknown is marked `[port]`, and the load report lists it (§12.2) so it can be found
  later.

**P2. Nothing silent.**
- Every case where the port does less than retail is a recorded issue with a kind, a source file, `in_mod` and a
  detail: an unread tag, a stored-but-unused tag, an unconsumed binding, a rejected asset, a skipped reward, a clamped
  limit.
- An issue that does not reach the load report is a bug in this system.

**P3. Coverage is a number, kept by tools.**
- Each mechanism has a ledger (tags, bindings, reward types, event types, perception tokens, asset formats). Each entry
  has a state and a test.
- The ledgers are generated from the code and from `docs/re/`, never typed by hand, and a lint keeps them true (§7.2,
  §9.1).

**P4. Tolerant at run time, strict in tools.**
- Run time: a parser that meets a defect retail would survive salvages what it can and records an issue (§10.2).
- Tools (`formats_validate`, `alamo_mod_check --strict`): they still report every inconsistency, so authors and lanes
  see the defect.

**P5. No port limit below retail.**
- A fixed size in sim or presentation code is either retail's own limit, cited from `docs/re/`, or it grows with the
  data (§8).

**P6. Do not copy retail's failures, but report them.**
- Where retail crashes or hangs on a mod's data (an infinite Lua loop, a null hardpoint), the port does not have to
  crash. It may stop the offending script or skip the object, as long as it reports this and the outcome for a correct
  mod is unchanged.
- Where retail **rejects** data (BOM scripts, 32-bit bytecode, an undefined function), the port rejects it the same way:
  a mod tested in the port must not break in retail.

**P7. Determinism holds for mod data** exactly as for base data (CLAUDE.md rule 3). Every salvage and fallback is a pure
function of the bytes and the command line.

---

## 3. Where the port stands

Full numbers are in the audit. In one table:

| Layer | Strong | Gaps for an arbitrary mod |
|---|---|---|
| Discovery and mounting | `MODPATH` / `STEAMMOD` / `FALLBACKPATH` in retail order, Steam library resolver without the Steam API, submod hint, mod picker, mod `MegaFiles.xml` | Unsorted or `/`-named MEG entries not found; UTF-16 list files fatal; launcher does not add a submod's parent; `modinfo.json` dependencies unused |
| XML data | Object types parsed with retail's value rules; unread-tag report over 18 databases; lenient XML reader | ~95 object, ~140 GameConstants, ~24 ability, ~17 faction, ~7 campaign tags parsed but unused, and not reported; strict booleans and first-wins duplicates outside object types; element-tag dependence; multiple root elements dropped |
| Limits | No faction-name or count assumptions left (modc t02-t04) | 64 players / factions; AI levels 5; fleet distance 100; `.ted` 1,985; fog 512; Lua company views 256 |
| Lua | 466 / 466 retail bindings; mods call nothing unbound; GlobalValue, require, pooling, saves | Two land task-force no-ops; six presentation bindings recorded, never drawn; `require` marks `_LOADED` early; AI scripts `.lua` only; skirmish scoring has no player world |
| Story | 58 / 60 events; 121 / 139 rewards | 18 rewards skipped (SWITCH_SIDES, FORCE_RETREAT, SET_HEALTH, ...); WIN/LOSE_BATTLES composition after auto-resolve |
| AI | Data-driven goals, templates, equations; failure rule as retail | No perception-coverage report; hand-kept `implements()` list; `Game.TIMELINE` = 0 |
| Shaders | SM1-3 translation, `.fx` prebuild, issue kinds | SM3 leftovers (`modc` t35) |
| Assets | Unknown chunks skipped; magenta / flat / white fallbacks; miniaudio WAV / ADPCM / MP3 | Strict ALO / ALA / particle parse (73 of ~40,700 mod files rejected); bump, BC4/5, float, P8, volume textures; no `.dds` retry; Bink 2; fonts not from MEGs |
| Presentation | Camera, fades, letterbox, movies, dialogs, holocron consumed | `Hide_Sub_Object`, cinematic layer, hyperspace-in visual; language fixed at English; DAT all-or-nothing; GUI files fatal |
| Checking | Load report v3, fixture mod, name lint, four-mod checklist | No asset decode; no honour-gap section; no perception coverage; no corpus runner; fuzzing outside the gate |

---

## 4. Architecture

The system is not a new module. It is a set of contracts and ledgers across the existing ones, with three new parts:
- the **mod registry** (§5);
- the **issue sink** (§11.1);
- the **corpus runner** (§13).

```
                 ┌───────────────────────────────────────────────────────────┐
  command line → │ Mod registry (§5): descriptors, modinfo.json, stack order │ ← launcher, mod picker
                 └──────────────┬────────────────────────────────────────────┘
                                │ ordered mount list + fingerprint
                 ┌──────────────▼──────────────┐
                 │ VFS / mount layer (§6)      │  provenance: which layer served each file
                 └──────────────┬──────────────┘
         ┌──────────────────────┼──────────────────────┬─────────────────────────┐
┌────────▼────────┐  ┌──────────▼─────────┐  ┌─────────▼─────────┐  ┌────────────▼──────────┐
│ Data (§7)       │  │ Scripts (§9)       │  │ Assets (§10)      │  │ Presentation (§10.4)  │
│ value semantics │  │ Lua VM, bindings,  │  │ tolerant decoders │  │ GUI, text, fonts,     │
│ tag ledger      │  │ story, AI tokens   │  │ format ledger     │  │ binding consumers     │
└────────┬────────┘  └──────────┬─────────┘  └─────────┬─────────┘  └────────────┬──────────┘
         └──────────────────────┴───────────┬──────────┴─────────────────────────┘
                                ┌───────────▼───────────┐
                                │ Issue sink (§11.1)    │  one record type, de-duplicated, provenance-tagged
                                └───────────┬───────────┘
                ┌───────────────────────────┼───────────────────────────┐
     ┌──────────▼──────────┐     ┌──────────▼──────────┐     ┌──────────▼──────────┐
     │ alamo_mod_check v4  │     │ in-game: picker,    │     │ corpus runner (§13) │
     │ (§12)               │     │ compat panel (§12.4)│     │ grades, matrix      │
     └─────────────────────┘     └─────────────────────┘     └─────────────────────┘
```

Module ownership follows CLAUDE.md:
- The registry lives in `engine/vfs`: discovery, `modinfo.json`, order.
- The issue sink is a small header in `engine/core`. It is a public-header change, so it goes through the integrating
  agent.
- The ledgers are generated files next to the module they describe.
- The corpus runner is a tool under `tools/modcorpus/`.

---

## 5. Mod registry, identity and stacks

### 5.1 Mod descriptor

Every mounted mod gets one `ModDescriptor`, built from its folder before anything is read from it:

| Field | Source | Use |
|---|---|---|
| `kind` | workshop / local `Mods\` / absolute path / fallback | Report, picker |
| `id` | workshop id, else folder name | Stack args, dependencies |
| `root` | host path | VFS |
| `name`, `version`, `summary`, `icon` | `modinfo.json` (and `*-modinfo.json` variants), else `<Info Name=...>` in its MEG lists, else folder | Picker, report, save mismatch text |
| `languages` | `modinfo.json` `languages` (`code`, `support` bit flags 1 text / 2 speech / 4 SFX) | Text / speech language choice (§10.4) |
| `dependencies` | `modinfo.json` `dependencies` (ordered; `modtype` 0 local / 1 workshop / 2 virtual; optional resolve layout first element) | Stack resolution (§5.2) |
| `game_tag` | `steamdata.tags` contains `EAW` or `FOC`; else heuristics (§5.4) | Refuse base-EaW mods (D1, §17) |
| `content_hash` | hash of the sorted (path, size, mtime) list of `Data\` plus MEG headers | Save fingerprint, corpus cache key, "mod updated" detection |

`modinfo.json` parsing follows the AlamoEngine-Tools `eaw.modinfo` spec: comments and trailing commas allowed, unknown
keys ignored. A malformed file is a warning, and the folder name is used instead. The game itself never reads the
file, so nothing in **gameplay** may depend on it. It feeds only names, languages, dependencies and launch arguments.

### 5.2 Stack resolution

Retail's rule stays the authority: `MODPATH` / `STEAMMOD` entries form one list in command-line order, and the earliest
entry wins (`MODS.md` §3.1, `PHASE2_ANSWERS.md` §14). The registry adds **resolution before launch**, in the launcher,
the mod picker and `alamo_mod_check --resolve`. The engine never reorders an explicit command line.

1. Start from the mods the user picked.
2. For each mod, add its declared dependencies (`modinfo.json`, the spec's breadth-first order, nearest ancestor first),
   then the submod hint's likely parent (`MODS.md` §5, t16) when no dependency is declared.
3. Detect cycles. A cycle is an error naming the cycle.
4. Order: every mod comes before the mods it depends on (submod first). Ties keep the user's order.
5. Detect **conflicts**: two total conversions in one stack, i.e. two mods that each replace `GameObjectFiles.xml` and
   `FactionFiles.xml` without one depending on the other. Retail would run the earlier one's lists and ignore the
   other's. This is a warning, and the launcher shows it.
6. Emit the argument list and the stack fingerprint (ordered `content_hash`es).

### 5.3 Missing parents

When a submod's parent is not installed:
- The picker and the launcher show the parent's id and name from `modinfo.json`.
- The game still starts if the user insists. Retail does too, with hundreds of missing files.

Downloading a parent is outside the port: no Steam API, by decision. The launcher may open the workshop page.

### 5.4 Base-EaW mods

A folder tagged `EAW`, or one whose `GameObjectFiles.xml` names no FoC-only file and whose scripts call no FoC-only
binding, is a probable base-EaW mod. Retail FoC does not run those, and the port supports FoC mods only (D1): the
registry refuses it with "base-EaW mod: retail runs it with sweaw.exe", and the grade is G0 with that reason.

---

## 6. Mount layer

`engine/vfs` keeps retail's resolution order. These changes cover the audit's mount gaps:

| Change | Why | Retail check |
|---|---|---|
| Run `Meg::verify()` at mount. If the table is not sorted by CRC, build a sorted index in memory and record an issue `meg_unsorted` | Mod tools can write unsorted tables; today entries are lost silently | `re-question`: does retail binary-search too? If it does, keep the loss and report it (P6) |
| Normalise stored entry names: `/` → `\`, ASCII upper case; recompute the CRC when the stored one does not match the normalised name, and record `meg_name_mismatch` | Same | Same question |
| Duplicate names inside one MEG: decide by retail's rule (first / last in file order) and record `meg_duplicate` | Undefined today | `re-question` |
| `MegaFiles.xml` and every list file: accept UTF-16 LE / BE with a BOM (decode to UTF-8; non-ASCII kept, not `?`) | UTF-16 is fatal now | Probe: one UTF-16 XML in a probe mod |
| **Provenance**: every resolved file records its layer (mod index, loose / MEG name) | Report v4 (§12) marks findings `[mod]` / `[base]` precisely, and the compat panel can show "this file came from submod X" | — |
| Directory listings keep each spec's rule (`UI.md` §9.2) | Unchanged | — |

Encrypted v3 MEGs stay a warning. No EaW / FoC tool writes them.

---

## 7. Data layer: the tag ledger and retail value semantics

This is where most of the silent breakage is (audit §4), and where the biggest gain is.

### 7.1 One value-semantics module

All XML readers go through one module, `engine/data/xml_values`, which implements retail's conversion per **type code**
(`docs/re/xml_field_tables.md`, "Type codes"):

| Value kind | Retail rule (to confirm per type code by `re-question` where not already cited) |
|---|---|
| Boolean | `1`, or first character `Y` / `T` (any case) = true; everything else false (OBJECT_TYPES §4.5) |
| Integer / float | C `atoi` / `atof` prefix parse, trailing junk ignored, overflow clamped as the C runtime does |
| Enum, single | Case-insensitive name match. Unknown name: "Failed to convert", field unchanged |
| Mask | Tokens split on space, comma, tab, CR / LF, `\|`. An unknown token: retail rule (today: field unchanged) |
| List | Split on space, comma, tab, CR / LF. Type names resolved later, unknown names dropped |
| String | As written. Retail upper-cases type names on store (OBJECT_TYPES §3.1) |
| `TBD`, empty | Skipped |

Duplicate tags in one entry follow **retail's rule per class**:
- Object types and GameConstants: last wins (known).
- Factions, campaigns, abilities and the others: confirm through `re-question`, then implement.

The `XmlNode::child()`-based readers (first-wins) are replaced by a ledger accessor that applies the class's rule.

**Element tags carry no behaviour** (OBJECT_TYPES §1):
- Planets are any type with the `PLANET` behaviour, whatever element they are written in.
- Galactic roles come from behaviours and categories, not from element names. This closes the
  "[port ... open, needs a decision]" in `galactic_rules.cpp:121-170`. Any golden it moves is a base-FoC behaviour
  question, so it goes through Gabriel.

Files with more than one top-level element:
- Read every top-level element, if retail does (`re-question`); else keep the first and record `xml_extra_root`.
- Today's silent loss becomes either data or an issue.

### 7.2 The tag ledger

A generated table, `engine/objects/generated/tag_ledger.inc` plus a human view in `docs/status/tag-ledger.md`, with one
row per (class, tag) in retail's descriptor tables:

| Column | Meaning |
|---|---|
| `class`, `tag`, `type_code` | From `xml_field_tables.md` |
| `state` | `unread` (not parsed) → `stored` (parsed, no consumer) → `consumed` (a sim / presentation reader exists) → `verified` (a test checks its effect against a `docs/re` spec or probe) |
| `consumer` | The function(s) that read it, found by an annotation |
| `spec` | The `docs/re` section that defines its effect, or `none` |
| `base_uses`, `corpus_uses`, `corpus_mods` | How often base FoC and the corpus (§13) use it |
| `tier` | 1 if a player or modder sees it (formula, rule, visible state); 2 if it is distribution-level |

**How consumers are found.** Every read of a typed field goes through the field accessor
(`type.get<Tag::Fires_Forward>()` or the existing typed view), and the generator lists the call sites. A
`// ledger: consumes <Tag>` annotation is allowed only where a read is indirect, and the lint checks that such an
annotation sits next to a real read. A grep heuristic (the audit's) is not enough: it gave noise both ways.

**Lint** (`tools/lint/check_tag_ledger.py`, in `build_agent` and the gate):
- the ledger matches the code;
- no `consumed` row without a consumer;
- a tag moving `consumed` → `stored` fails the lint.

**Priority order.** The ledger is the work list for the data gaps. Order: `corpus_mods` × tier 1 first, then
`base_uses`. Tags that base FoC already uses but the port ignores are **base-FoC parity bugs**:
- `Special_Case_Production` (936 base uses), `Size_Value` (420), the level maxima;
- they also fix every mod that inherits them.

**Report.** `alamo_mod_check` v4 lists, per mod, the `stored` and `unread` tags **that the mod's data actually sets to
a non-default value**, with counts and the first file (§12.1 `honour_gaps`). The report stops saying "nothing unread"
for a mod that relies on 30 unused tags.

### 7.3 Tags no retail table lists

The audit found tags that several unrelated mods use heavily but that retail's tables do not list: `Autoresolve_Health`
(1,114 uses in one mod), `Facing_Adjust`, `Is_Bomber`, `Sensor_Range`, `DPS_is` (7,750), `Era_Start`, and others.

Before they are treated as dead:
- one `re-question` pass checks that the extracted tables are complete for the top 30 such tags. Their reader may be a
  code path the table extraction missed, e.g. a hand-written `Parse_Database_Entry`.
- Confirmed dead tags stay in the unread-tag report as **retail-equivalent** (§12.2) and do not count against the
  grade.
- Answered (mcx0 t07, `docs/re/MCX0_T07_UNLISTED_TAGS.md`): none of the 30 most used is a field-table tag in the binary;
  the extraction only missed three ability element names, now in the port's sub-object list.

### 7.4 Fatal-at-load cases

Every case in the audit's "fatal at load" list gets a retail answer (`re-question`) and the matching behaviour:
- Missing core list files and zero factions are probably fatal in retail too. Then the port keeps G0, with a clear
  message.
- An unknown skirmish faction or planet should fall back to the first valid one and record a warning, if retail lets
  the menu offer it.

---

## 8. Limits policy

**Rule** (P5): no fixed size in `engine/` sim or presentation code may be smaller than retail's, and every fixed size
is one of:
- **retail** (cited `docs/re` section and address);
- **data** (read from GameConstants or the type);
- **dynamic** (grows with the data);
- **guard** (a sanity bound far above any real data, e.g. 1,000,000, that records `limit_hit` and refuses, never clamps
  silently).

The modc t04 lint gains a second check that lists `constexpr` / `#define` integer limits in sim code and requires one of
the four tags in a comment (`// limit: retail SKIRMISH_SETUP §1`).

| Today | Change |
|---|---|
| 64 players, galactic refuses more than 64 factions | Dynamic player list. Commands keep a compact player index (`uint16_t`). The per-player arrays become vectors sized at session start. Retail has no cap (`MODS.md` §10.4) |
| AI level maxima 5 (`kMaxLevel`) | Read `MaximumStarbaseLevel`, `MaximumGroundbaseLevel`, `MaximumPoliticalControl` from GameConstants (they are in the table; the comment already cites the offsets) |
| Fleet distance 100 (`kMaxFleetDistance`) | Read `MaximumFleetMovementDistance` (already loaded) |
| Game-spec tech clamped 1..5 | Cite retail or remove |
| `.ted` grid 1,985 vertices | Retail's limit (`re-question`), else dynamic |
| Fog 512 cells per axis | Dynamic with the map, or cite |
| Lua `Company_Units` 256 views | Dynamic |
| Squadron resize `kPortSquadronSize` | Removed (D2): retail's `Squadron_Units` lists and prices for base FoC and mods. The wing HUD stays |

**Applied (mcx5, 2026-11-18; retail answers `docs/re/MCX5_RE_ANSWERS.md`).** Players: per-player state sized from the
session; command ids carry 13 bits (the wire is unchanged below 256); the sim's ids stay `uint8_t` under the guard
`sim::kMaxPlayers` = 256 [port], which `alamo_mod_check` reports as `limit_hit`. AI maxima and fleet distance: read from
GameConstants (absent tag: retail's defaults 1 / 1.0). Tech range: the dialog's campaign data (open: mcx4 t19).
`.ted`: no retail grid bound, so guard 7937 per side; materials past 64 kept for the sim. Fog 512: retail. Company views:
guard 65536. Story drain rounds: guard 1024 (retail cascades with no cap). Lint: `tools/lint/check_limits.py`.
Conformance mods: `engine/apps/game/testdata/conformance/limits/`.

---

## 9. Scripting layer

### 9.1 The binding effect ledger

All bindings exist (466 / 466). The remaining question is whether each one **has an effect**. The binding table
(`binding_table.inc`, generated from `lua_api.json`) gets an `effect` column:

| Effect state | Meaning |
|---|---|
| `full` | Does what retail does (spec cited) |
| `recorded` | Stores the request in the presentation log; consumer named, or `none` |
| `noop-retail` | A no-op in retail too (spec cited) |
| `noop-gap` | A no-op where retail acts |
| `partial` | Some arguments or paths missing (`[inferred]` sites count here) |

A test asserts that every `recorded` binding has a consumer in `alamo_game`. Allowed exception: a binding whose only
effect is headless-invisible, and the ledger says so.

Gaps to close first, by corpus use:
1. `Hide_Sub_Object` (806 mod calls, 279 in base). Render hides the named sub-object mesh per object; saved with the
   object; also honoured by the shadow and selection passes.
2. `Cinematic_Hyperspace_In` arrival visual (310). The same visual as a normal hyperspace arrival, per
   `BATTLE_TRANSITIONS.md`.
3. `Promote_To_Space_Cinematic_Layer`, `In_End_Cinematic`, `Do_End_Cinematic_Cleanup`, `Cancel_Fast_Forward`.
4. `LandTaskForce.Enable_Attack_Positioning` (48 calls) and `Release_Reinforcements`. `re-question` for the land
   bodies (`LUA_API.md` cites 0x6e8a70 / 0x6eaa80), then implement with retail's argument checks.
5. The `[inferred]` sites (~68): each gets a `re-question` answer or stays marked `partial`.

### 9.2 Runtime semantics

| Item | Change |
|---|---|
| `require` sets `_LOADED[name]` before the chunk runs | Set it as stock 5.0.2 does (after success). Keep cyclic-require protection through `_REQUIREDNAME`, as stock does. Confirm retail uses stock `require` (`MODC_LOADER.md` §6 says it does) |
| AI plan / FreeStore / Evaluator scripts `.lua` only | Same lookup as the other roots (`.lua`, then `.lc`), if retail does (`re-question`) |
| Skirmish scoring VM without a player world | Attach the world in skirmish as in galactic (`GAME_SCORING.md` §301 follow-up) |
| `Player_Quit_Event` never raised | Raise it on quit / surrender / elimination as retail does |
| A script that never yields (`while true do ... end` without `Sleep`) | Retail hangs. The port adds an **instruction budget per resume** (a debug hook count), far above any normal script (e.g. 50M instructions). When it is hit, the script is stopped and reported as `script_runaway`. P6: a correct mod is unchanged, and a broken one does not freeze the game. The budget is a pure count, so it is deterministic |

### 9.3 Story ledger

Event types and reward types get the same ledger as tags (`state`, `spec`, `corpus_uses`).

**Rewards.** Implement the 18 missing ones in corpus-use order:
- first SWITCH_SIDES and FORCE_RETREAT (used by TR);
- then SET_HEALTH, SET_SPAWN, SET_WEATHER, RANDOM_STORY, the RESTRICT_* and ENABLE_* rewards;
- the presentation ones last (FLASH_*, DUAL_FLASH, PICK_PLANET, INFORMATION, STATISTIC_CHANGE, ENABLE_COMBAT_CINEMATIC).

Several already have specs (STORY_REWARDS_GALACTIC §1.8, §3.2, §3.7; STORY_PRESENTATION_REWARDS §2). An unknown reward
name that is **not** in retail's enum stays a skip, reported as `retail-equivalent` (retail ignores it too). A known one
the port lacks is an `honour_gap`.

**Events.** Fill the WIN/LOSE_BATTLES Param3 composition after auto-resolve, and model Param5-7 of the Enter family from
the spec.

### 9.4 AI token coverage

- Replace the hand-kept `GalacticSimTokenResolver::implements()` list with registration: every token handler registers
  its path at start-up, so coverage is what the code evaluates.
- Point the coverage test at the in-game resolver, and regenerate the not-implemented list in `AI_GALACTIC_TOKENS.md`
  from it.
- `alamo_mod_check` gains `ai.token_coverage`. It parses every perception equation the mod's AI data defines and lists
  the leaves the port does not implement, with equation and file. A whole-equation failure is retail's rule, so a
  missing leaf silently turns a mod's AI decision into nil: this report is the only place it shows.
- Same for tactical perception leaves.
- `Game.TIMELINE`: `re-question`.

---

## 10. Assets and presentation

### 10.1 Format ledger

One table per asset family: ALO chunk / mini-chunk ids, ALA layouts, particle emitter properties, DDS pixel formats,
TGA types, WAV codecs, Bink revisions, `.ted` chunks. Columns: `state` (decoded / salvaged / rejected), `spec`,
`corpus_uses`. It is generated from the decoders' own tables (`known_chunks()`, the DDS format switch), and the corpus
runner fills `corpus_uses`.

### 10.2 Tolerant decoding (salvage)

The runtime decoders get a **salvage mode**; the tools keep strict mode. Each salvage records an `asset_salvaged` issue
with the rule that applied.

| Defect (audit §3) | Salvage | Count in reference mods |
|---|---|---:|
| ALA rotation / translation block size ≠ declared | Read the frames the data holds; missing frames repeat the last pose | 53 |
| Collision-tree child index out of bounds | Drop the collision tree (the model still draws; picking falls back to the bounding box) | 8 |
| No connections chunk | Treat every mesh as attached to bone 0 / root | 5 |
| Mesh without info chunk | Default info (no LOD, visible, no collision) | 2 |
| Skin bone index outside the bone map | Clamp to the root bone, issue | 1 |
| Chunk overruns parent | Truncate to the parent; parse what fits | 1 |
| Empty file | Missing asset (existing fallback) | 2 |
| Bone parent not before child | Topologically reorder | — |
| Particle spawn-shape / track size mismatch | Default the property; keep the emitter | — |

**Which defects retail survives is the open question**, and each rule is checked once against retail with a probe
kit: one mod with deliberately broken copies of each defect, units placed in a skirmish (`probe-kit`, P1).
- Where retail shows nothing, the port also shows nothing (and reports it).
- Where retail draws, the port draws.

### 10.3 Textures, audio, video, text, fonts

| Gap | Change |
|---|---|
| V8U8 / Q8W8V8U8 / CxV8U8 bump formats | Convert to RG8 (SNORM) on load. The effects that sample them get the same values D3D9 gave |
| ATI1 / ATI2 (BC4 / BC5), 16-bit per channel, float, P8, volume, partial cube | Decode or convert to the nearest Vulkan format; P8 through its palette |
| Colour-mapped TGA (types 1, 9) | Decode through the colour map |
| A candidate exists but fails to decode | Try the next candidate (`.dds` after `.tga`) before the magenta fallback, if retail's lookup does (`re-question`) |
| `.dds` substitute name cut at the first dot | Cut at the last dot |
| Bink 2 (`KB2`) | Retail FoC cannot play Bink 2 either (check), so report `retail-equivalent`. Otherwise skip the movie with an issue |
| Text language fixed at `ENGLISH` | Language from the game settings (retail's registry / options value), else English. A mod's `modinfo.json` `languages` lists what it supports, and the picker warns when the chosen language is not among them |
| DAT with one bad count rejects all text | Salvage the entries before the bad record; issue `text_salvaged` |
| Fonts only loose | Look fonts up through the VFS (MEGs too), as retail does (check) |

### 10.4 GUI and presentation consumers

- **GUI files** (`GUIDialogs.rc` / `.xml`, `MT_CommandBar.mtd` / `.tga`). When the mod's copy fails to load, fall back
  to the next layer's copy (base FoC) and record `gui_fallback`. Today the game does not start. Retail probably fails
  too; then the port is better and says so (P6).
- **`[port]` HUD stand-ins** (the shell band, credits position, galactic 8-row cap, RadarMap terrain colours read but
  unused): each becomes data-driven from the mod's GUI files, or a ledger row stating it is a port layout.
- The binding consumers of §9.1.

---

## 11. Fault containment

### 11.1 The issue sink

One record type for every finding of §6-§10, so the report, the compat panel and the corpus runner see the same thing:

```
struct CompatIssue {
  IssueKind kind;        // unread_tag, honour_gap, retail_equivalent, asset_rejected, asset_salvaged,
                         // binding_unconsumed, reward_missing, token_missing, limit_hit, meg_unsorted,
                         // gui_fallback, script_runaway, script_error, missing_file, ...
  Severity  severity;    // error / warning / info, by the rules of MODS.md §5
  bool      retail_equivalent;  // retail does the same (cite) -> does not lower the grade
  std::string subject;   // tag / binding / file / token
  std::string source;    // game path that caused it
  uint16_t  layer;       // provenance (§6): which mod
  std::string detail;
};
```

The sink de-duplicates by (kind, subject, source) and counts repeats. It is thread-safe: loaders run on jobs and append
to per-index buffers merged in index order (rule 3). It is not read by sim logic. It is diagnostics only and never
enters the CRC.

### 11.2 No-abort policy

- **Data, asset and script errors never abort the process.** The existing exceptions (Lua panic, state creation,
  wrong-thread asserts) stay, because they are programming errors.
- **A sim invariant broken by data** (an object without a required behaviour, a null hardpoint target): the object or
  command is skipped and an issue recorded.
- **Determinism.** The skip happens on every worker count and compiler alike (P7).
- **Tests.** The corpus runner (§13) runs every mod under the ASan build weekly to find the paths still left.

### 11.3 Fuzzing

- Add fuzz targets for TED, DAT, MTD, `GUIDialogs.rc`, anim clips, particle data, `.lc` chunks, WAV headers and Bink
  headers.
- The fuzzers run as a `slow`-labelled ctest entry with a short time budget per target, and in the gate with a longer
  one (`build_fuzz.cmd`).
- The corpus seeds come from the installed mods (§13).

---

## 12. Diagnostics: load report v4 and the compatibility grade

### 12.1 `alamo.mod_report` version 4

New keys. Existing keys are unchanged (`MODS.md` §5.1 versioning rule):

| Key | Content |
|---|---|
| `stack` | The resolved descriptors (§5.1) in order, with `content_hash` and declared / inferred dependencies; `conflicts` |
| `honour_gaps` | `[{class, tag, state (unread/stored), uses, entries, first_file, in_mod, tier}]`: tags the mod **sets** that the port does not honour (§7.2) |
| `retail_equivalent` | Findings retail shares (BOM scripts, 32-bit bytecode, unknown reward names, dead tags of §7.3, Bink 2, ...), counted apart |
| `assets` | `{checked, decoded, salvaged:[{path, rule}], rejected:[{path, reason}], by_family:{...}}`: every asset the mod's data references (models, animations, particles, textures named by materials, SFX samples, movies, maps, fonts, GUI textures) decoded headless, loose and in MEGs |
| `ai.token_coverage` | `{equations, leaves, missing:[{token, equations, first_file}]}` (§9.4) |
| `story` | `{rewards_used, rewards_missing:[...], events_used, events_partial:[...]}` |
| `bindings` | `{called, unconsumed:[{name, call_sites}]}`: calls to bindings whose effect state is `recorded` without a consumer, `noop-gap` or `partial` |
| `limits` | `[{limit, value, needed}]`: data that hits a guard or would exceed a cited retail limit |
| `grade` | `{grade: "G0".."G3", blocking:[issue refs]}`: what keeps it from the next grade. G4 needs retail runs, so the report never gives it |

`--strict` makes every salvage an error (for mod authors). `--corpus-cache DIR` reuses decoded-asset results by
`content_hash`, so rechecking an unchanged mod is fast.

### 12.2 What lowers the grade

- An **error** gives G0.
- An **honour gap**, `asset_rejected`, `binding_unconsumed`, `reward_missing` or `token_missing` **in content the mod
  uses** keeps it below G3. Each of them is a port gap with a ledger row.
- A `retail_equivalent` finding never lowers the grade.
- Salvages do not lower it once the retail check (§10.2) has confirmed the rule. Until then they keep G3 open, marked
  "salvage unconfirmed".

### 12.3 Grade computation

The corpus runner combines the following into the grade: the report's `grade` (G0-G1, and the G3 static part), the
headless runs (G2), the smokes (G3 front end), and recorded retail results (G4). The rules are written once in
`tools/modcorpus/grade.py` and tested on the fixture mods.

### 12.4 In-game

- **Mod picker** (`MODS.md` §5.2): shows the grade, the top blockers and the resolved stack. With b0479 (out-of-process
  check) it runs `alamo_mod_check --json` as a child process.
- **Compat panel** (F-key overlay in a running game, like the profiler overlay): the issues raised **during play**,
  each with its source mod: a story reward skipped, a binding unconsumed, a salvaged asset on screen.
- **Log.** Each issue is logged once per run to `logs/compat_<mod>.txt`.

---

## 13. The compatibility corpus and its runner

### 13.1 The corpus

| Set | What | Who maintains it |
|---|---|---|
| **Reference set** (`MODS.md` §10.3) | TR, FotR, EAW-Remake, Age of Legends | Must reach G4 |
| **Corpus** (D6) | The currently installed workshop mods and their local `Mods\` submods, with their stacks: 1125571106, 1976399102, 1770851727, 2858787558, 3622043265, and `Thrawns_Legacy_HD`, `Legeds_ships_transfer`, `AOW_Thrawns_Legacy_Compatibility` on TR. 3830298393 is an uploader test item without `Data\` (G0, nothing to mount) | Must reach G3, and lists the rest with blockers. A mod installed later joins automatically |
| **Conformance mods** | Small generated mods, one per mechanism, under `engine/apps/game/testdata/conformance/` | Lanes, with each mechanism |

**Conformance mods** extend today's fixture: one tiny mod per mechanism, generated by a script from a template and
checked into testdata. Each one tests a mechanism without game data where possible, and with base FoC through
`ALAMO_FOC_DATA` where not. Examples:
- every value kind of §7.1, including duplicate tags, UTF-16 and multiple roots;
- every story reward;
- one unit per salvage rule;
- a stack with a declared dependency;
- a cycle;
- 80 factions;
- a 100-level GameConstants;
- a runaway script;
- a GUI file that fails.

These run in ctest (fast tier), so a regression shows up in every build, not only in a corpus sweep.

### 13.2 The runner

`tools/modcorpus/run_corpus.py` (Python, standard library, `-I`), with a `run_corpus.cmd` wrapper:

1. Discover mods through the registry (§5) and resolve stacks (submods with their parents).
2. Per mod or stack, using the cached result when `content_hash` and the build are unchanged:
   1. `alamo_mod_check --json` (v4).
   2. `alamo_headless` space and land skirmish on the mod's first non-empty maps, 3,000 ticks, `--workers 0` and
      `--workers 4`, `--compare`.
   3. One galactic campaign, 9,000 ticks, `--story --scoring --object-scripts`. The campaign is the first the menu
      lists that has planets.
   4. The hidden smokes (skirmish, campaign).
   5. Optionally (`--asan`), steps ii-iii under ASan.
3. Write `logs/modcorpus/<date>/<mod>.json` and a matrix: mods × grade × blockers.
4. Exit 0 when no mod's grade dropped since the last recorded run. A drop is a regression and names the mod and the
   first new blocker.

Two test slots (CLAUDE.md) means about 4 minutes per big mod. A full sweep of the 10 installed setups is under an hour,
so it runs after `land_on_dev` on demand and before every `dev` → `main` merge. It does not run per build: the
conformance mods do that job.

**Where results go.**
- The matrix and per-mod JSON stay in `logs/`, as build artefacts.
- The roadmap gets only status: each grade drop is filed as a `modc` task through `backlog add --from modc` (Stage 6
  keeps follow-ups).
- `MODS.md` §10.4 keeps the last checklist table, regenerated by the runner (`--write-md`).

### 13.3 Determinism with mods

The runner's `--compare` already covers worker counts. In addition, **one mod golden per reference mod**: a short
headless skirmish CRC checkpoint file (`ALAMO_CRC 2`) under `tests/goldens/mods/`, which is skipped when the mod is not
installed. A behaviour change that moves a mod golden is then as visible as a base golden move. The goldens are
regenerated with `tools\regen_goldens.cmd --mods`, and the commit message names them.

---

## 14. Retail as the oracle

Three channels, cheapest first:

1. **`re-question`**: every rule this design marks "retail check". Batched by subject, so one Ghidra turn answers
   several:
   - MEG lookup and duplicates;
   - XML: duplicates per class, multiple roots, UTF-16, the tags of §7.3;
   - `.lc` for AI roots;
   - texture candidate retry;
   - font lookup;
   - `Game.TIMELINE`;
   - land task-force bodies;
   - `.ted` limit.
2. **Probe kits** (`probe-kit`, Claude runs retail, DECISIONS 2026-11-14) for what only the running game shows:
   - the salvage rules (§10.2);
   - a UTF-16 XML file;
   - GUI fallback;
   - the story rewards' visible effects;
   - Bink 2.

   One kit per subject, under `tools/probe_mod/compat_*`.
3. **Differential runs on corpus mods** (G4): for a reference mod, the same scenario in retail and in the port.
   - **Tier-1 checks** (the rule visibly applies: a unit's cost, a reward's effect, a sub-object hidden) by screenshot
     comparison at fixed camera keys.
   - **Tier-2 checks** (AI build order distribution, battle outcomes over seeds) using the existing parity harness
     (`tools/parity`) with mod data.

   Results go to the probe's `docs/re` spec and the runner's G4 record.

---

## 15. Saves, mod versions and the launcher

**Saves.** Today a save records the VFS arguments and `data_fingerprint`, and refuses any mismatch (`MODS.md` §3.3).
Additions:
- **Record the stack.** The save stores the stack's descriptors (name, version, `content_hash`).
- **Readable refusal.** The mismatch message names the mod and says "updated since this save (version a → b)" rather
  than the first differing hash.
- **Allow-different-data (D3).** The player may load anyway. The load goes ahead only when every type, faction and
  script the save references still exists, and the session is marked "loaded with different data". Retail lets players
  load saves across mod updates and often breaks; this is the friendlier version. Continuing an outdated save on a newer
  mod version is **not supported** (Gabriel, D3): the dialog says so, and bugs in such a session are not filed as port
  bugs.

**Launcher and mod picker.**
- The resolved stack of §5.2: a submod pulls in its declared or likely parent (today it is only checked in the crash
  bisect).
- The grade badge and top blockers from the corpus cache, or from a fresh check.
- The language choice and the mod's supported languages.
- Conflict warnings: two total conversions; a submod above its parent.
- Install discovery beyond Steam: a manual FoC folder already exists; GOG and disc install locations come from the
  `PetroglyphGameInfrastructure` conventions.

---

## 16. Delivery plan

The plan follows the roadmap rules:
- Groups have an exit test.
- Stage 6 and Stage 9 groups keep their follow-ups as tasks (S3 included).
- **Tier focus.** Everything below is tier 1 (how XML and Lua are read, visible states) or tier 2, except where marked.
  Tier-3 items (speed, memory) wait for the focus to lift.
- **Lanes.** The groups are sized for lanes. Parallel lanes are marked ∥.

| Order | Group (proposed id) | Contents (§) | Exit test |
|---|---|---|---|
| 1 | `mcx0` Ledgers and issue sink | Issue sink (§11.1); tag ledger generator, accessor annotation, lint (§7.2); binding effect column (§9.1); story and token registration (§9.3, §9.4); report v4 keys `honour_gaps`, `bindings`, `story`, `ai.token_coverage`, `retail_equivalent` | `alamo_mod_check` v4 on the 4 reference mods lists honour gaps, unconsumed bindings, missing rewards and tokens; ledger lint in the gate; base FoC report unchanged except the new keys |
| 2 ∥ | `mcx1` Value semantics and XML robustness | §7.1 module; per-class duplicate rule (`re-question`); UTF-16; multiple roots; element-tag independence (D4); retail squadrons (D2) | Conformance mod `values` passes; base goldens unchanged, or moved with Gabriel's approval (D4) |
| 2 ∥ | `mcx2` Assets: audit and salvage | Report `assets` section (§12.1, loose and MEG); salvage mode with the §10.2 rules; probe kit `compat_assets` for retail's behaviour; texture formats (§10.3) | Every asset referenced by the 4 reference mods decodes or salvages under a retail-confirmed rule; 0 `asset_rejected` outside retail-equivalent cases |
| 2 ∥ | `mcx3` Registry, stacks and saves | Descriptor, `modinfo.json`, stack resolution, conflicts (§5); base-EaW refusal (D1); MEG verify / normalise (§6); save load-anyway (D3); launcher and picker use it (§15) | Conformance mods `stack_dep`, `stack_cycle`, `meg_unsorted` pass; the AOW → Legacy HD → TR stack resolves from the AOW folder alone |
| 3 | `mcx4` Honour gaps: base-used tags | Ledger rows with `base_uses > 0` and tier 1, by use: `Special_Case_Production`, `Size_Value`, the GameConstants level maxima and taxation, turret target filters, `Fires_Forward`, blast dropoff, tech increment / decrement, bribe and cash points, ability modifiers (each needs its `docs/re` spec first) | Every tier-1 ledger row used by base FoC is `consumed` with a test; the remaining rows are filed by corpus use |
| 3 ∥ | `mcx5` Limits | §8 table: dynamic players, GameConstants maxima, fleet distance, `.ted`, fog, company views; limit lint | Conformance mods `factions_80`, `levels_10` run G2; lint passes |
| 3 ∥ | `mcx6` Story and bindings | The 18 rewards; WIN/LOSE composition; `Hide_Sub_Object` and the cinematic consumers; land task-force bodies; `require`; AI `.lc`; skirmish scoring world; `Player_Quit_Event`; runaway budget | Story ledger: 0 missing retail rewards; binding ledger: 0 `recorded` without a consumer, 0 `noop-gap`; TR's SWITCH_SIDES / FORCE_RETREAT plots run |
| 4 | `mcx7` Corpus runner and mod goldens | §13.2 runner, cache, matrix, regression exit code; §13.3 mod goldens; `MODS.md` §10.4 generated | One full sweep recorded; a deliberately broken conformance mod drops a grade and the runner exits non-zero |
| 5 | `mcx8` Corpus to G3 | Fix the blockers the matrix lists, mechanism by mechanism, on the installed corpus (D6) | Every installed mod and stack at G3, or G0 / G1 / G2 with only retail-equivalent or out-of-scope blockers (D7) |
| 6 | `mcx9` Reference set to G4 | Differential retail runs (§14.3) on the 4 reference mods; Gabriel's played battle (`modc` t51) | Tier-1 checks agree; tier-2 metrics inside the distribution |
| later (tier 3) | no group | Big-obstacle movement cost (`modc` t33, stays in `modc`); out-of-process picker check (backlog b0479) | When the tier focus lifts |

How the groups depend on each other:
- `mcx0` comes first: every later group's exit test reads its report.
- `mcx1-3` are independent of each other.
- `mcx4-6` need `mcx0`'s ledgers.
- `mcx7` can start after `mcx0`.

**On the roadmap** (Gabriel, 2026-11-17): the groups form their own stage, **Stage 9 · Mod compatibility system** (lane
`st9`), separate from Stage 6's `modc` and `q6`. The open `modc` tasks (t33, t35, t50, t51) stay in `modc`. Stage 9
keeps its follow-ups like Stage 6 (Gabriel, 2026-11-17): every follow-up of an `mcx` group, S3 included, becomes a task
of that group, not a backlog entry, and the groups have no task cap. Tier-3 findings still go to the backlog while the
current focus holds.

**Unified mod system refactor** (Gabriel, 2026-11-18): Stage 10 (`ums0`-`ums5`, `DECISIONS.md` "Unified mod system
refactor") puts the registry, stack resolution, mounting and layer provenance of §5-§6 behind one `vfs::ModEnvironment`
and moves the Command Center onto `alamo_mod_check` JSON. When it is done, `ums5` re-evaluates every open `mcx` task
against it, so a task here may be re-scoped or dropped then.

Boxes:

| Box | Label | Groups |
|---|---|---|
| `MCX1` | Ledgers and load report v4 | `mcx0` |
| `MCX2` | Data, values and limits | `mcx1`, `mcx4`, `mcx5` |
| `MCX3` | Scripts, story and presentation | `mcx6` |
| `MCX4` | Assets and salvage | `mcx2` |
| `MCX5` | Registry, stacks and saves | `mcx3` |
| `MCX6` | Corpus and grades | `mcx7`, `mcx8`, `mcx9` |

---

## 17. Decisions (Gabriel, 2026-11-17)

Recorded in `DECISIONS.md`, "Mod compatibility system decisions".

| # | Question | Decision | Where it lands |
|---|---|---|---|
| D1 | Base-EaW mods (run by retail through `sweaw.exe`) | **FoC mods only.** Keep the detection, so the refusal message says why | §5.4; `mcx3` |
| D2 | The squadron resize rule (2026-11-13) | **Back to retail's squadron handling** for base FoC and mods: no resize to 12, no price scaling. The wing HUD stays | §8; `mcx1` (base goldens move, named in the commit) |
| D3 | Loading a save made with a different mod version | **Load anyway after a check**: every referenced type, faction and script still exists; the session is marked "loaded with different data"; the message names the mod and both versions. Continuing an outdated save on a newer mod version is **not supported** | §15; `mcx3` |
| D4 | Element-tag independence (may move base-FoC goldens) | **Fix it everywhere.** Golden moves are approved once the lane shows the behaviour matches retail | §7.1; `mcx1` |
| D5 | Broken mod art | **Match retail**: salvage and draw where retail draws, show nothing and report where retail shows nothing. A "lenient art" setting only if players ask; `--strict` lists every defect | §10.2; `mcx2` |
| D6 | Corpus | **The currently installed workshop mods** (and their local submods); no extra subscriptions | §13.1; `mcx7`, `mcx8` |
| D7 | Corpus target | **G3 on every installed mod and stack**, lower only for retail-equivalent or out-of-scope reasons; G4 on the reference set | §1.3; `mcx8`, `mcx9` |
| — | Roadmap placement | **Its own numbered stage**, separate from the current mod groups: Stage 9 · Mod compatibility system | §16 |

---

## 18. Risks

| Risk | Mitigation |
|---|---|
| The ledgers become a second source of truth that drifts | Generated from code and `docs/re`; lint in the gate; no hand-kept list (the audit found the hand-kept `implements()` list stale) |
| Honour-gap work is large (~280 gameplay tags) | Ordered by base and corpus use. Base-used tags are parity bugs anyway. A tag nobody sets costs nothing |
| A salvage rule draws something retail does not | D5: the probe kit decides each rule before it lowers the grade |
| Base-FoC goldens move | Value-semantics and tag work may move them. Each move is a retail question first, then Gabriel's approval (`MODS.md` §1 rule 5) |
| Corpus runs are slow on this PC | Cache by `content_hash` and build; conformance mods carry the per-build load |
| Mods change on the workshop under the corpus | `content_hash` change → re-run; the matrix records which version each grade belongs to |
| Rule 1 creep: a fix that only makes sense for one mod | The name lint; reviewer and `spec-review` check "mechanism, not identity" |

---

## Appendix A. Glossary

| Term | Meaning |
|---|---|
| Honour gap | A retail-read tag or binding that the port parses but does not act on |
| Retail-equivalent | A finding retail has too (it rejects or ignores the same thing); it never lowers a grade |
| Salvage | Run-time recovery from a defective asset, recorded as an issue |
| Ledger | A generated table of every item of a mechanism with its port state, spec and use counts |
| Corpus | Every installed mod and stack; the reference set is its G4 subset |
| Conformance mod | A tiny generated mod that tests one mechanism in ctest |
| Stack | The ordered list of mods mounted together (submods before parents) |

## Appendix B. Gap index (audit → design section)

| Audit finding | Section |
|---|---|
| Tags parsed but unused (object, GameConstants, faction, ability, campaign) | §7.2, §16 `mcx4` |
| `Special_Case_Production` | §7.2 |
| Strict booleans, first-wins duplicates, overflow | §7.1 |
| UTF-16 XML, multiple roots | §6, §7.1 |
| Element-tag dependence | §7.1, D4 |
| Tags no retail table lists | §7.3 |
| 64 players, levels 5, fleet distance 100, `.ted`, fog, company views | §8 |
| Squadron resize | §8, D2 |
| Fatal-at-load cases | §7.4 |
| Land task-force no-ops, `[inferred]` sites | §9.1 |
| `Hide_Sub_Object`, cinematic bindings | §9.1, §10.4 |
| `require`, AI `.lc`, scoring world, `Player_Quit_Event` | §9.2 |
| 18 story rewards, WIN/LOSE composition | §9.3 |
| Token coverage tooling, `Game.TIMELINE` | §9.4 |
| MEG unsorted, `/` names, duplicates | §6 |
| ALO / ALA / particle strictness (73 files) | §10.2, D5 |
| Texture formats, no `.dds` retry | §10.3 |
| Language fixed, DAT all-or-nothing, fonts | §10.3 |
| GUI files fatal, HUD stand-ins | §10.4 |
| Launcher: submod parent, dependencies, discovery | §5, §15 |
| Fuzz gaps | §11.3 |
| No asset check in the report or the checklist | §12.1, §1.3 |
| Removed workshop submods still named in `MODS.md` §5 | `mcx3` (update `MODS.md` when the registry lands) |
