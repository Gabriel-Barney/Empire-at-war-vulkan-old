# Mod-loading contract

What makes a mod loadable by the port, how its files are found, which data mechanisms it may use, what the loader reports,
and the rule that `engine/` has no mod-specific code. Written for roadmap group `modc` (task t01, 2026-11-06); the binding
decision is in [`DECISIONS.md`](DECISIONS.md) ("Mod compatibility is agnostic", Gabriel, 2026-11-03). Status of each part is
marked here so later lanes can point at it; whether a task is finished is on the roadmap only.

Status labels used below:

| Label | Meaning |
|---|---|
| **main** | Implemented on `main` today |
| **Q6 stack** | Implemented on the unmerged Q6 branches (`claude/initial-mod-compatibility-cdd6af`, `claude/q6-integration`; PRs #65, #67, #69); on `main` once that stack lands |
| **modc tN** | Planned by that `modc` task |

## 1. The rule

A mod works in the port when it uses the same data mechanisms as base Forces of Corruption. The goal is near-full
compatibility with FoC mods (Gabriel, 2026-11-13): what plays in retail plays in the port; the checklist, the allowed
failures and the reference set of mods are in §10. Thrawn's Revenge (roadmap Q6) is the first and hardest test case, but it
is a test case, not a target the engine is shaped around.

1. **No mod-specific code paths in `engine/`.** Engine code (every module, sim and presentation alike) contains no mod name,
   no workshop id, no faction, unit, planet, campaign or map name that changes behaviour, and no branch of the form "if this
   is mod X". Behaviour comes from the data files through the mechanisms in §4.
2. **No fixed faction lists or player counts.** Factions come from `FactionFiles.xml` in file order; players come from the
   campaign, the skirmish setup or the map. The only fixed limits are those the original executable has, cited from a
   `docs/re/` spec (for example the skirmish slot rules in `docs/re/SKIRMISH_SETUP.md` §1).
3. **Names the original itself hard-codes are allowed**, with a comment that cites the `docs/re/` spec and address: the
   original treats a mod the same way, so the port stays agnostic. Example: a GameConstants field, a Lua global, an engine
   effect file name. A base-game *content* name (`Empire`, `Rebel`, `Underworld`, `TIE_Fighter`, `Kuat`, ...) is never in this
   class unless the spec shows the executable looks it up.
4. **A port rule may depend on a mechanism, never on an identity.** Allowed conditions: "a mod layer is loaded", "the faction
   has no `Space_Skirmish_AI_Default_Forces`", "the type has no `Hyperspace`". Not allowed: "the faction is called Empire",
   "the mod folder is 1125571106". Each port rule that applies only when a mod layer is loaded is listed in §6, and with no
   mod loaded the port behaves exactly as base FoC (Gabriel, 2026-11-04, `skirmish_buy_list` `multiplayer_list_only`).
5. **Base FoC does not move.** Loader, Lua and mod fixes must leave base-FoC behaviour unchanged: the base-FoC CRC goldens
   stay byte-identical (DECISIONS.md, "Parity tail re-sequenced", 2026-10-31). Determinism rule 3 applies to mod data
   exactly as to base data: the same mod and command line give the same per-tick CRC on every thread count and compiler.
6. **Fixes are general.** A gap a mod exposes is fixed as a general mechanism (a tag the loader did not read, a Lua function
   not bound, a fallback the original has). A fix that only makes sense for one mod is not made; the gap is reported (§5).

Allowed places for mod and faction names: tests and their fixtures (`engine/*/tests/`, `tests/`), docs, `tools/probe_mod/`,
`tools/testbuild/` launchers, and comments that cite where a behaviour was observed. The `modc` t04 lint enforces rule 1 on
`engine/` sim code, the game host `engine/apps/game/app` and `engine/ui` (modc t20) with an allow-list for these.

## 2. What a mod is

A mod is a folder that contains a `Data\` tree laid out like FoC's own `corruption\Data\`. Anything under that `Data\`
replaces or adds to the game's files by path (§3). A mod needs nothing else: no manifest, no registration and no
port-specific file. A folder with no `Data\` subfolder mounts nothing; the VFS warns
`mod path has no Data folder: '<dir>'` (**main**, `engine/vfs/src/vfs.cpp`).

A mod is played on top of an installed FoC (the "corruption" folder), which in turn needs EaW (`GameData`) next to it, as
in retail. The port reads only; it never writes into a mod folder.

## 3. Finding the mod and resolving files

### 3.1 Command line

The port takes the original's switches (`docs/re/PHASE2_ANSWERS.md` §14, `docs/re/MAIN_LOOP.md` startup step 3). Keys are
case-insensitive and values may be quoted. `alamo_game`, `alamo_headless` and `alamo_mod_check` pass every `KEY=value`
argument to `vfs::parse_command_line`.

| Switch | Meaning | Status |
|---|---|---|
| `MODPATH=<dir>` | Adds `<dir>` to the mod search list. A relative `<dir>` is resolved against the FoC folder, so the retail convention `MODPATH=Mods\<Name>` finds `corruption\Mods\<Name>`. An absolute path works too, including a Steam workshop item folder | **main** |
| `STEAMMOD=<id>` | Adds the Steam workshop item `<id>` to the same list, at its command-line position. Retail asks the Steam client for the subscribed item's folder; if the item is not subscribed, nothing is mounted | **main** (modc t10): `alamo_game`, `alamo_headless` and `alamo_mod_check` install the Steam library lookup (`engine/vfs/include/alamo/vfs/steam_workshop.hpp`, below); an item not found warns `STEAMMOD=<id>: workshop item not found; nothing mounted`. A `Vfs` created by other code (tests, tools) without a `VfsConfig::steam_resolver` still warns `no workshop resolver`. `MODPATH=<item folder>` keeps working |
| `FALLBACKPATH=<dir>` | Adds `<dir>` to the fallback chain (below the MEGs, above EaW `GameData`) | **main** |

`MODPATH=` and `STEAMMOD=` entries go into **one** search list in command-line order: retail mounts each as it parses the
token, so they interleave (PHASE2_ANSWERS §14). The earliest entry wins. Several mods may be stacked this way; the port
supports it exactly as the original does, and a "submod" is just an earlier entry.

Where mods are found, for tools that list them (**modc t08** mod picker):

- local: `<FoC folder>\Mods\<Name>\` (each subfolder with a `Data\` folder is a candidate);
- Steam workshop: `<library>\steamapps\workshop\content\32470\<id>\` for every Steam library folder (32470 is the FoC app
  id). The port does not use the Steam API: an item folder present on disk counts as installed (**main**, modc t10).
  Library folders, first match wins: the library holding the FoC folder (the parent of its nearest `steamapps` ancestor),
  then for each Steam client folder the folder itself and every `path` of its `steamapps\libraryfolders.vdf` and
  `config\libraryfolders.vdf` (file order; the pre-2021 numeric-key form too). Client folders: `ALAMO_STEAM_ROOT`
  (`;`-separated; when set, nothing else), else the registry's `SteamPath` / `InstallPath` (read only), else
  `C:\Program Files (x86)\Steam`. An id that is not all digits is never found.

### 3.1.1 Mod registry and stacks (mcx3)

The game itself never reorders or completes a command line: it mounts the `MODPATH=` / `STEAMMOD=` list as written. The
tools that build a command line resolve a **stack** first (`engine/vfs/include/alamo/vfs/mod_registry.hpp`,
`docs/MOD_COMPAT_DESIGN.md` §5):

- **Registry.** One `ModDescriptor` per installed mod (the folders of §3.1): `kind` (workshop / `Mods\` / path), `id`
  (workshop id or folder name), `name`, `version`, `summary`, `icon`, `languages`, `dependencies`, the Steam game tag
  and a content hash (sorted relative path, size and write time of every file under `Data`, FNV-1a 64). `modinfo.json`
  follows the AlamoEngine-Tools `eaw.modinfo` spec and is read leniently: `//` and `/* */` comments, trailing commas, a
  missing comma between members (a warning; 2498889654's `sub-modinfo.json` has one), `<variant>-modinfo.json` files
  (one descriptor each, the main file's values as defaults; 2083545253 has only `subA` / `subB`). Missing `languages`
  means English. Nothing in gameplay reads a descriptor (retail never opens `modinfo.json`).
- **Resolution** (`vfs::resolve_stack`, `alamo_mod_check --resolve`, the launcher, the mod picker): the picked mods,
  then each one's declared dependencies (breadth-first, the spec's layouts `ResolveRecursive` / `ResolveLastItem` /
  `FullResolved`), and for a mod that declares none the submod hint's likely parent (§5). A cycle is an error naming it;
  a parent that is not installed is a warning; two total conversions (each with its own `GameObjectFiles.xml` and
  `FactionFiles.xml`, neither depending on the other) are a warning. Launch order: every mod before the mods it depends
  on (submod first), ties in the order found. The stack fingerprint is FNV-1a 64 over the ordered content hashes.
  `alamo_mod_check --resolve <mod args>` checks the resolved stack (the report's `stack` section); `--resolve-only`
  prints it and stops. The Command Center's profile page has **Resolve stack** (`launcher/src/stack.rs`: runs
  `alamo_mod_check --resolve` on the profile's mods, shows the stack, its errors and warnings and the grade badge) and
  **Use this order** (the profile's mods become the resolved stack).
- **Base-EaW mods (D1).** A mod whose `modinfo.json` Steam tags say `EAW` and not `FOC` is refused before anything
  loads: "<name> (<id>): base-EaW mod: retail runs it with sweaw.exe" (`load_game_data`: `alamo_game` does not start
  it; the report's `base_eaw`, grade G0; the picker shows "Refused" and has no Launch). [port] A mod without tags whose
  own `GameObjectFiles.xml` names none of the files FoC's list has and EaW's lacks is only a warning
  (`base_eaw_suspect`, `docs/MOD_COMPAT_DESIGN.md` §5.4; scripts are not looked at).

Conformance mods: `engine/apps/game/testdata/conformance/stacks` (`stack_dep`, `stack_cycle`, `eaw_only`;
`alamo_game_conformance_stacks_tests`) and the `meg_unsorted` case of `alamo_vfs_registry_tests`.

### 3.1.2 One catalog and one resolver (ums1, Stage 10)

Every launch surface (the in-game picker, `alamo_play`, `alamo_mod_check`, and through its JSON the Command Center)
reads the same data and starts the same list:

- **Install locations** (`vfs::InstallLocations`, `install_locations.hpp`): the FoC folder, the EaW `GameData` beside
  it, the Steam roots and libraries (§3.1), the folders the user named (`game_path.txt`, `ALAMO_FOC_DATA`) and the
  user's extra mod folders. The game search is Play.cmd's order; `alamo_play`'s `find_game` forwards to it.
- **Catalog** (`vfs::ModCatalog`, the registry's new name; `ModRegistry` is an alias until ums4): one row per (folder,
  variant). A row's **catalog key** is unique across kinds: `1125571106`, `2083545253:subA`, `Mods\AOW`, or the host
  path of an extra folder. `ModDescriptor::key()` (`AOW`, `2083545253:subA`) stays the key of save headers and the corpus
  matrix. The picker's rows are the catalog's rows (`game::list_installed_mods`), so a folder with only variant files
  (2083545253) shows one row per variant and none for the folder. Order: workshop items by numeric id across all
  libraries (the picker's order before ums1), then `Mods\` folders by name, then extra folders. Folders are compared as
  absolute, normalised paths (any case, either separator), so a game folder or mod folder spelled two ways is one folder
  (junctions and `subst` drives are not resolved). Two variants of one folder share one argument; the picker marks the
  one whose declared stack is the running command line as active.
- **Launch list** (`vfs::resolve` / `ModStack::launch_args`): the resolved stack's arguments, each folder once. Empty for
  a refused base-EaW stack (D1). [port] A cycle still launches in its fallback order (discovery order) with the error
  shown beside it, and an argument that names no installed mod is left out with an error: retail would mount either
  command line, so the port does not refuse more than retail does. The picker's Launch (`game::check_picked_mod`,
  `mod_launch_args`), `alamo_play` (a dropped folder or mod argument, `game::play_launch_args`; a base-EaW stack gets a
  message box and does not start, a stack with other errors asks "Start it anyway?") and `alamo_mod_check --resolve`
  (the report's `stack.launch_args`, which the full `--resolve` check also mounts) give the same list;
  `alamo_game_conformance_stacks_tests` checks the table for `stack_dep`, `stack_cycle`, `eaw_only`, the
  AOW -> Legacy HD -> TR shape (the real three are not installed here, §10.4) and a workshop item's two variants
  (`STEAMMOD=` lists).
- **JSON for the Command Center.** `alamo_mod_check --list [--mod-dir DIR] [--no-hash] --json FILE|-`: schema
  `alamo.mod_catalog` v1 (install locations and every row; without `--game` / `ALAMO_FOC_DATA` it searches the Steam
  libraries). `alamo_mod_check --conflicts <mod args in load order> --json FILE|-`: schema `alamo.mod_conflicts` v1,
  which mod wins each file two mods share, decided by `Vfs::resolve` (MEG records count only when the game's MEG lookup
  reaches them; a later mod's loose file beats an earlier mod's MEG record, and the base game's loose file beats any MEG
  record, §3.2), with the findings `list_ignored`, `submod_order` (from declared `modinfo.json` dependencies),
  `no_effect` and `no_data`. Field lists: `mod_check.hpp` `format_mod_catalog_json`, `mod_conflicts.hpp`.
  `--list-mods` (`alamo.mod_list`, the corpus runner's discovery) is unchanged.

### 3.2 File resolution order

Unchanged from Phase 2 (`docs/PHASE2.md` "Data\ resolution order", `docs/re/SUBSYSTEMS.md` "File lookup priority",
`engine/vfs/include/alamo/vfs/vfs.hpp`). First match wins:

1. **Mod search list**, earliest entry first: `<mod>\Data\<rest>`. Only paths starting with `Data\`.
2. **FoC loose** `corruption\Data\<rest>`.
3. **MEG archives**, newest-mounted first: FoC `64Patch` > `patch2` > `Patch` > FoC `MegaFiles.xml` entries (later-listed
   wins) > EaW `64Patch` > `patch2` > `Patch` > EaW `MegaFiles.xml` entries.
4. **Fallback chain**: `FALLBACKPATH=` folders in order, then EaW `GameData\Data`.

Consequences for mods (all **main**):

- **File granularity.** A mod replaces whole files, never parts of them. Shipping `Data\XML\SpaceUnitsCapital.xml` replaces
  that file; shipping `Data\XML\GameObjectFiles.xml` replaces the whole list. Two copies of a file are never merged
  (`docs/re/OBJECT_TYPES.md` §1.3).
- **Loose beats archived.** A mod's loose files beat every MEG, including `64Patch.meg`.
- **Mod archives.** `Data\MegaFiles.xml` and `Data\Patch*.meg` are opened through the search list, so a mod's own
  `MegaFiles.xml` replaces FoC's list and the MEGs it names are mounted in place of the base ones; a mod MEG with the same
  name as a base MEG (e.g. `Data\Config.meg`) is mounted *instead* of it. EaW-side archives always come from EaW.
- **Paths** are ASCII case-insensitive and `/` equals `\`.
- **Inside one MEG** the game binary-searches the record table by the CRC of the normalised name with the C runtime's
  `bsearch` and never compares the stored name; the record found names the data record through its index field
  (`docs/re/MCX3_MEG_LOOKUP.md`). The port reads the same way (`meg::Archive::lookup`), so a malformed mod MEG loses
  what retail loses: entries an unsorted table hides, all but one of several records with one CRC (the probe order
  picks), a record under its stored name when its CRC is another name's. Each finding is a VFS warning and a
  retail-equivalent report issue (`meg_unsorted`, `meg_duplicate`, `meg_name_mismatch`, `meg_index`; mcx3 t04).
- **Directory scans are not lookups.** Listings (skirmish maps, AI plan folders, scripts) follow the rules of the spec that
  defines them (`docs/re/UI.md` §9.2: mod-folder maps go to the *custom* map list; `PHASE2.md` note on `Vfs::list(glob, layers)`).

#### Layers and provenance (Stage 10, ums0)

Port rules for code that asks where a file came from; the resolution order above is unchanged (`DECISIONS.md`, "Unified
mod system refactor").

- **Every resolution names one layer.** `Vfs::layers()` lists the layers in the order above (mods, FoC loose, MEGs
  newest-mounted first, fallback folders); `Vfs::layer_of(resolution)` gives a file's `vfs::Layer`: its kind, its index
  within the kind, its host root (a folder, or the `.meg` path) and its **owning mod** (`Layer::mod`, the position in the
  mod search list, or -1 for the base game).
- **A MEG belongs to the folder it was opened from.** A MEG that a mod's own `MegaFiles.xml` names, or a mod's `Patch*.meg`,
  is owned by that mod, decided when it is mounted; a base archive a mod's list names (`Data\Config.meg` from FoC) stays
  base-owned. Before ums0 a MEG lying anywhere under a mod folder counted as the mod's, so the two rules differ only for
  odd setups (a FALLBACKPATH folder inside a mod folder, a mod folder that contains the FoC install): the effect audit
  now counts those archives as base.
- **Ask the layer, not the path.** "Is this file from a mod, and which one" reads `Layer::mod`, not a comparison of host
  paths with the mod folders. Moved so far: `shader_files_from_vfs` (ShaderFileEntry `from_mod` / `dir_order`) and the SFX
  archive list; the load report's MEG issues and the asset audit still test paths until ums2 / ums4 remove the last
  `mod_dirs()` callers.
- **One walk for every copy.** `Vfs::resolve_all(path)` returns every copy in resolution order (`[0]` is what `read`
  returns); code that needs the lower copies (the SFX archives, which a mod's copy layers over FoC's:
  `docs/re/AUDIO.md` §5, aud2 t05) takes them from there instead of building `<mod>\Data\...` paths by hand.
- **Findings carry their layer.** `Vfs::create` takes an optional `IssueSink`; malformed MEG tables go there as
  retail-equivalent issues with `Layer::issue_layer()` (the mod's index, or `kBaseLayer`).
- **Checked equal.** `alamo_audio_layer_equivalence_tests` compares the shader entries and the SFX archive list with the
  pre-ums0 code on base FoC and the installed reference mods.

### 3.3 What a save remembers

A save stores the VFS command line it was made with (`SessionDescriptor::data_args`), a fingerprint of the loaded
definitions (`data_fingerprint`) and (mcx3 t05) the mods of the stack (`SessionDescriptor::mods`: key, name, version,
content hash; written only when a mod is loaded, so a base-game save keeps its earlier bytes; a build from before
mcx3 refuses a save that has the block as unreadable). Loading a save rebuilds
the session from those arguments; a save made with different data (another mod, another mod version, no mod) is refused
with the first difference (`SessionDescriptor::mismatch`, `engine/apps/sim/include/alamo/sim/save_session.hpp`; **main**).
A data difference is named per mod: "<name> updated since this save (version a -> b)", "changed since this save",
"was in this save and is not loaded now", "is loaded now and was not in this save".

**Load anyway (D3).** When only the data differs, `alamo_game` offers "Load anyway" and says that continuing an outdated
save on a newer mod version is not supported. Loaded anyway, every chunk must still read: the object store refuses
another object type list (names and order), the rebuild needs the save's factions and campaign, the story its plot
scripts; a failure discards the session as before. The session is marked "loaded with different data"
(`GameSession::loaded_with_different_data`, a log line and a screen line); bugs in such a session are not port bugs.
Saves of the original game are out of scope (DECISIONS.md).

## 4. Mechanisms a mod may use

Everything below is reached through the VFS of §3, so a mod uses it by shipping the file. The port implements these as the
original does (the spec column), for base FoC and for mods alike; nothing here is special to mods.

| Mechanism | Files | Rules (spec) | Status |
|---|---|---|---|
| Faction list | `Data\XML\FactionFiles.xml` and the files it names | Factions appended in file order, upper-cased names, no duplicate check: the first definition of a name wins. The default faction of a type with an empty `Affiliation` is the first faction with `Is_Neutral` (`OBJECT_TYPES.md` "Faction list", "Empty list"). Map object owners are faction indices in this order (`GALAXY_MAP_PRESENTATION.md`, `REMAINING_GAPS.md` S10 probe) | **main** (galactic rules); no fixed list anywhere: **modc t02/t03** |
| AI player type | Faction `Basic_AI`; campaign `AI_Player_Control` pairs; `Data\XML\AI\` definitions | Skirmish: an AI slot gets its faction's `Basic_AI` (`SKIRMISH_SETUP.md` §2.1). Campaign: the campaign's `AI_Player_Control` pair; no pair = no AI. A type the AI definitions do not define = no AI ("Unrecognized AI player type", `AI_GALACTIC.md` §1.2) | Data-driven for base FoC and mods alike (`resolve_ai_type`, `load_faction_basic_ai`; modc t03). Campaign pairs read for base FoC too since **modc t17** (2026-11-06; before, only with a mod layer) |
| Skirmish starting forces | Faction `Space_Skirmish_AI_Default_Forces`, `Land_Skirmish_AI_Default_Forces` | `SKIRMISH_SETUP.md` §2 step 4, §2.2 | Data path for every faction (`skirmish_default_forces`); the port's built-in base-game lists are gone (**modc t17**, 2026-11-06). A faction without a list, or whose names the data does not define: the [port] fallback of §5 (up to three of its cheapest buildable units, `data_fallback_forces`). Space: [port] a hyperspace ship leads a list without one (the port hosts a skirmish as a galactic hop) |
| Game object types | `Data\XML\GameObjectFiles.xml` and the files it names | One list, no EaW/FoC merge; files in list order; first definition of a type name wins; `Variant_Of_Existing_Type`; unknown names in lists dropped (`OBJECT_TYPES.md` §1–§6) | **main** (engine/objects) |
| Galaxy and campaigns | `TradeRouteFiles.xml`, `CampaignFiles.xml`, the planet / route / campaign XML they name | One `CampaignFiles.xml` (first match, no merge); route and campaign lists de-duplicated and parsed in ascending CRC32 of the listed name; last definition wins as a whole (`PHASE2_ANSWERS.md` Q13, `FALLBACK_CAMPAIGNS.md`) | **main** (engine/data) |
| Story / campaign scripts | Story XML named by the campaign, `Data\Scripts\Story\*.lua` | `docs/re/STORY.md`, `L1_S4_REMAINING.md` | **main** (engine/lua, story session) |
| Lua overrides | `Data\Scripts\` under the original's fixed roots: `AI\`, `AI\SpaceMode\`, `AI\LandMode\`, `AI\StoryArc\`, `Interventions\`, `Evaluators\`, `GameObject\`, `Story\`, `Library\`, `FreeStore\`, `Miscellaneous\` | Scripts are looked up by name under the roots (`.lua`, then `.lc`); a loose mod script overrides an archived one (`SUBSYSTEMS.md` "Lua loading"). Language is Lua 5.0.2 (DECISIONS.md); precompiled chunks must match the port's VM | **main** (`engine/lua/src/script_loader.cpp`) |
| Object scripts | `Lua_Script` on a type, `Data\Scripts\GameObject\` | `docs/re/OBJECT_SCRIPTS.md` | **Q6 stack** (`engine/apps/sim/src/object_scripts.cpp`) |
| AI data | `Data\XML\AI\` (templates, goals, equations, difficulty), `Data\Scripts\AI\` plans | `AI_GALACTIC*.md`, `AI_TACTICAL*.md` | **main** |
| Game constants and the other boot databases | `GameConstants.xml` and every database in the boot list (`MAIN_LOOP.md` startup step 8: SFX, music, speech, abilities, hard points, movement classes, difficulty, scoring, ...) | Loaded once at startup through the VFS | **main** for those the port loads; the rest follow their phase |
| Maps | `Data\Art\Maps\*.ted` | Skirmish map catalog: official = FoC loose + every MEG, custom = `Data\CustomMaps` + each mod folder's `Data\Art\Maps` (`UI.md` §9.2) | **main** |
| Text and fonts | `Data\Text\MasterTextFile_<lang>.dat`, mod fonts | Through the VFS; font search includes the mod folders | **main** |
| Art, audio, video | Models, textures, animations, particles, SFX / music / speech, Bink movies | Loaded by name through the VFS when referenced | **main** |
| Effects (shaders) | `Data\Art\Shaders\**.fx`, `.fxo` | Retail adds each `MODPATH` / `STEAMMOD` folder's `Data\Art\Shaders\` and `\Terrain\` to the shader search (PHASE2_ANSWERS §14); the port's effect order already includes them (`engine/materials/include/alamo/materials/effect_loader.hpp`). The renderer draws with its own SPIR-V translation of the shipped effects. A mod `.fxo` that adds or replaces an effect goes through the same translation, shader model 3 included (modc t22); a `.fx` source is compiled ahead of time by `alamo_fx_prebuild` and then wins over the `.fxo` like in the original (modc t23, §4.1) | **main** (modc t11, t22, t23, §4.1) |

Not supported (by decision, for every mod): multiplayer (no networking), loading the original game's save files, and
retail 32-bit Lua bytecode (the x64 VM rejects it; retail's own `64Patch.meg` ships text sources for that reason,
`SUBSYSTEMS.md` "Lua loading"). Lua written for 5.1 or later fails to compile on the 5.0.2 VM, as in retail.

### 4.1 Effects (shaders) in mods

**Original** (`docs/re/RENDER.md` §6, `SPACE_AND_OPEN.md` D.3): each mod folder's `Data\Art\Shaders\` and `\Terrain\` are
searched before the base ones; every `*.fx`, then every `*.fxo`, is loaded at boot; a name is looked up as `<name>.fx`
first, then `<name>.fxo`, so **a `.fx` source wins over a `.fxo` of the same name** and is compiled with D3DX at run time.
The first technique (file order, LOD within Shader Detail) that passes `ValidateTechnique` is used. A missing effect draws
the built-in green default; a file that fails to compile or load draws the same green effect under its own name.

**Port** (no fixed shader set: every effect is looked up by name through the VFS, mod folders first;
`engine/materials/include/alamo/materials/effect_loader.hpp`, `effect_audit.hpp`):

| Case | What the port does | Issue kind |
|---|---|---|
| Mod `.fxo` replaces or adds an effect | Parsed like a shipped one; shader passes are translated (shaderxlat → HLSL → DXC → SPIR-V) on background jobs and cached on disk, `vs_3_0` + `ps_3_0` passes included (modc t22, below). Same result as the original | none |
| `.fx` source compiled by `alamo_fx_prebuild` (modc t23, below) | The compiled source is loaded in place of any `.fxo` of the same name, like the original; its passes go through track B as above | none |
| `.fx` and `.fxo` of the same name, the `.fx` not compiled (no prebuild entry, fxc error, or `LoadOptions::use_fx_sources` off; the detail says which) | The `.fxo` is loaded; the original compiles the `.fx` instead (same result when the `.fxo` was built from that source) | `source_shadowed` |
| `.fx` without a `.fxo`, same condition | Green default under the effect's name (= the original's result when its compile fails); before t11 it counted as missing (silent shared default) | `source_only` |
| `.fxo` that does not parse / load | Green default under its name (D.3) | `load_failed` |
| No selectable technique | Shared green default (D.3) | `no_technique` |
| Best technique has a shader pass the port has no program for (a VS without a PS or the reverse; shader model 3 mixed with an older stage, which D3D9 does not draw either) | [port] That technique is skipped like one failing `ValidateTechnique`; the next technique by LOD / file order whose passes all have a program is drawn (`MaterialEffect::skipped_technique`) | `technique_skipped` |
| No technique the port can draw | Submeshes draw unlit (base texture, the pass's blend); Heat / Shadow bucket submeshes are not drawn | `unsupported_pass` |
| Translation or DXC fails for a pass (also an SM3 feature outside the list below) | Same unlit fallback; logged once per program | `translate_failed` |

Nothing crashes and nothing is skipped silently: every case except the first is a `materials::ShaderIssue`
(kind, effect, path, `in_mod`, detail). Up front: `materials::audit_effects()` / `alamo_effect_audit <FoC dir> [MODPATH=..]
[--no-dxc] [--json FILE|-] [--fx-cache DIR] [--fx-prebuild] [--no-fx]` (JSON schema `alamo.effect_audit` v1: counts,
`fx_sources` / `fx_sources_compiled` since t23, + `issues`). At run time:
`render::EffectCache::issues()` and `Renderer::shader_issues()` (adds translation failures); each issue is also logged
once. The load report lists them too (`== shaders`, JSON `shaders`, §5; modc t21).

**Installed mods** (2026-11-06, base FoC + 64Patch / Patch / Patch2 = 82 effects; workshop folder
`F:\SteamLibrary\steamapps\workshop\content\32470`; the other 8 items ship no shaders). "Differs" = same path as a base
file, other bytes; most mods ship a full recompiled set. Audit with DXC, so every translated pass also compiled.

| Item | Shader files (loose + MEG) | Same as base / differs / new | `.fx` / `.fxh` sources | Effects (from the mod) | Issues |
|---|---|---|---|---|---|
| 1125571106 Thrawn's Revenge | 87 | 31 / 51 / 5 | `AlamoEngine.fx`, `.fxh` | 85 (84) | none after `alamo_fx_prebuild` (modc t23; before: `source_shadowed` AlamoEngine) |
| 3622043265 Thrawn's Revenge (other copy) | 88 | 31 / 51 / 6 | + `Terrain\_TerrainRenderBump.fx` | 86 (85) | `source_only` _TerrainRenderBump (fxc error X3014, a bug in the source, below); before t23 also `source_shadowed` AlamoEngine |
| 1976399102 Fall of the Republic | 88 | 31 / 51 / 6 | as 3622043265 | 86 (85) | as 3622043265 |
| 1770851727 | 93 | 31 / 51 / 11 | none | 93 (93) | none since modc t22 (before: `technique_skipped` Planet_Deathstar, `sph_t3` vs_3_0 / ps_3_0 → `sph_t2`) |
| 2858787558 | 55 | 0 / 49 / 6 | none | 88 (55) | none since modc t22 (before: `unsupported_pass` MeshFresnelGlow, only SM3 bytecode: unlit) |

All other mod effects (including 3-11 new `.fxo` per mod, e.g. MeshShieldEngines, MeshBumpSpecGlowColorize, DEFAULT.fxo) load
and translate: 0 `translate_failed`, 0 `load_failed` in every mod. `_TerrainRenderBump.fx` is preloaded by the original;
whether any model names it was not checked (t21's report cross-references issue effects with the models' materials).

**Shader model 3** (modc t22, 2026-11-06): `engine/shaderxlat` translates `vs_3_0` / `ps_3_0` and `resolve_pass` marks a
pass Translated when both stages are SM1-2 or both are SM3 (D3D9 does not draw a mixed pair). Covered: outputs `o0-o11` and
`ps_3_0` inputs `v0-v9` by dcl usage (POSITION0, COLOR0-1, TEXCOORD0-7, FOG, PSIZE: the varyings SM1-2 code uses, so no new
pipeline interface), loops / rep / if / call, predicates, `a0` / `aL` constant addressing, `texldl`, `texldd`, `vPos`,
`vFace`, `def` constants anywhere in c0-c223. Semantics that differ from SM1-2 (translate.cpp header table): SM3 colors and
fog are not clamped, `ps_3_0` gets no fixed-function fog (the alpha test still applies), `vPos` = pixel centre - 0.5. Rejected
with a message (the pass then counts as `translate_failed`): other output / input usages (NORMAL, TANGENT, ... as varyings),
vertex texture fetch, relative addressing of `v#` / `o#`, non-`def` reads of `ps_3_0` c32+ (the engine's PS register file has
32 registers); none of the installed mods uses them (follow-up tasks on the roadmap). The real SM3 programs in the
installed mods (4 blobs: MeshFresnelGlow, Planet_Deathstar) use only semantic I/O, `nrm`, `pow`, `lrp`, `log` / `exp`,
`texld` and `_pp`; they are part of the shaderxlat corpus (`ALAMO_WORKSHOP_DIR`, else the Steam library's workshop folder)
and of the GPU differential test, with two synthetic SM3 programs that cover the rest of the list.
Audit after t22: 1770851727 and 2858787558 report no issues (2858787558: 52 track-B passes, was 51). The project-local `fxc.exe` (Windows SDK) compiles `AlamoEngine.fx` with `/T fx_2_0`
(warning X4717 "Effects deprecated"), so a compile step for `.fx` sources is feasible (t23).

**Effect sources** (modc t23, 2026-11-06): the port compiles `.fx` files ahead of time, not at run time (no D3DCompiler in
the engine). `alamo_fx_prebuild <FoC dir> [MODPATH=..]... [--cache DIR] [--fxc EXE] [--force] [--json FILE|-]` finds
every `.fx` the loader would take (same VFS and search order, mod folders first), reads it and every file it
`#include`s (own folder first, then `Data\Art\Shaders\`; port choice, the original's include handler is not
recovered), and runs the toolchain's `fxc.exe /T fx_2_0` on a copy in a fresh temporary folder. The result (compiled effect,
or the fxc log on failure) goes to an `FxCache` entry keyed by a hash of the source and its includes
(`$ALAMO_FX_CACHE`, else `%LOCALAPPDATA%\alamo\fx`; never the game install; one file per key, written to a
temporary name and renamed, so concurrent runs share it), so an unchanged source is not compiled again. The loader
(`EffectLibrary`, `LoadOptions::use_fx_sources`, on by default; `fx_cache_dir`) looks the key up when an effect has a
`.fx`: a compiled entry is loaded in place of the `.fxo` (the effect's path is then the `.fx`); no entry or a failed
compile keeps the t11 fallback above, and the issue detail gives the fxc error line or says the prebuild has not run.
`alamo_effect_audit --fx-prebuild` compiles first; `--no-fx` audits the t11 behaviour.
Since modc t36 (2026-11-16) the prebuild also runs by itself whenever a mod is mounted (`game::prebuild_mod_effects`):
`alamo_game` runs it before the renderer loads its first effect (one stderr line per source fxc rejects, one summary
line), and `alamo_mod_check` before the shader audit (`--no-fx-prebuild` skips it); the report lists each `.fx` with
compiled / from cache / not compiled and the fxc error line (`shaders.fx_prebuild`, §5.1). fxc is `$ALAMO_FXC`, else
`fxc\fxc.exe` beside the exe (test builds ship it with `d3dcompiler_47.dll` and an `SDK_VERSION.txt`; Gabriel,
2026-11-16, `docs/DECISIONS.md`), else the toolchain's. Without fxc the cached results still load and every other
source keeps the t11 fallback, reported as not compiled ("fxc.exe not found").
Audit after t23 (prebuilt): 1125571106 no issues (AlamoEngine.fx compiled and loaded, 1 of 1 sources); 3622043265 and
1976399102 `source_only` _TerrainRenderBump only (1 of 2 sources compiled): its line 172 builds `float4(Out.Pos, 1.0f)` from
a `float4`, which every HLSL compiler rejects (X3014), so the original's compile fails too and it draws the green default
under that name, as the port does. Base FoC ships no `.fx`: 0 issues, unchanged.

## 5. Fallbacks and what the loader reports

The original loads with most data problems and only logs them; the port does the same and reports them. Nothing is guessed
from a mod's name. Two levels:

- **Error**: the data cannot be played. `alamo_game` refuses to start the session; `alamo_mod_check` exits 1.
- **Warning**: the original would load and run on; the port does too, and the report lists it.

| Situation | Original | Port | Level | Status |
|---|---|---|---|---|
| FoC folder missing / not a directory | Abort | VFS create fails, message names the folder | Error | **main** |
| EaW install not found | Abort | Warning, EaW MEGs and `GameData` skipped (PHASE2 "VFS: missing EaW install warns") | Warning | **main** |
| `MODPATH=` folder without `Data\` | Nothing found there | `mod path has no Data folder` | Warning | **main** |
| `STEAMMOD=` item not installed | Nothing mounted | `workshop item not found; nothing mounted` | Warning | **main** (modc t10) |
| `MegaFiles.xml` names a missing or unreadable MEG | Skipped | `MegaFiles.xml entry not found` / `cannot open MEG` | Warning | **main** |
| A list file (`GameObjectFiles.xml`, ...) names a missing file | Logged, skipped | Loader diagnostic, file skipped; the load report lists it under missing files | Warning | **main** |
| An object type's `Space_` / `Land_` / `Galactic_Model_Name` names a model no layer has | Draws nothing (not traced) | Listed under missing files (a name without an extension is also tried with `.ALO`) | Warning | **main** (modc t05) |
| A loader step throws | Crash or log, depends | Step reported as failed with the step name | Error | **Q6 stack** (`GameData::stages`, `alamo_mod_check`) |
| A campaign has no planets | Listed in the Galactic Conquest menu (menu-only rows, e.g. headers; starting one not tested: `re/MODC_LOADER.md` §4) | Listed by name as a menu entry that cannot be started (retail lists it: `docs/re/MODC_LOADER.md` §4, modc-loader); one error when no campaign has planets | Warning; Error if none has planets | **Q6 stack**; level: modc t14 |
| Campaign row names an unknown planet / route / faction | Row skipped | `build_galaxy_map` diagnostic, grouped by message | Warning | **Q6 stack** |
| Faction `Basic_AI` / `AI_Player_Control` names an undefined AI type | "Unrecognized AI player type", no AI | No AI for that player, diagnostic | Warning | **Q6 stack** |
| Faction has no skirmish default forces | Places nothing | [port] up to three of the faction's cheapest buildable units (`data_fallback_forces`), listed as a diagnostic | Warning | **Q6 stack** |
| A Lua script does not compile (also: a UTF-8 BOM at its start, which the original does not skip: `re/MODC_LOADER.md` §2) | `LuaScript Error in <name>.lua!`, script does not run | Listed with the compiler message | Warning | **Q6 stack** (`alamo_mod_check` compiles every `Data\Scripts\**.lua`) |
| A script calls an engine function the port does not bind, or reads a value that fails | Script error, the script stops | The load report runs the main chunk of every entry script (not `Library\`) once in its own VM and lists the errors, and lists the engine globals / methods the scripts call that the port registers as unbound stubs (static scan). Later failures show up in session runs (`alamo_headless --story` / `--galactic-ai`) | Warning | **main** (modc t05) |
| XML that is not well-formed | Its own reader is lenient; element names may start with a digit (`<181st>`: `re/MODC_LOADER.md` §3) | Digit-led names parse as elements (probe); other recoveries (`<?xml ...>` without `?`, mismatched close tags, a `<` that starts no markup) keep the file and are listed as loader diagnostics | Warning | **main** (engine/data `xml.cpp`; digit-led names: modc t14) |
| A script's main chunk fails on a `require` of a script no layer has | Stock Lua 5.0.2 `require` raises "could not load package": the chunk stops there and the script does not load (`re/MODC_LOADER.md` §6) | Listed with the missing name and what loads the script (below); a mod's own script under a by-name root that nothing names is an unused-script lead instead | Warning; lead: Info (not counted) | **main** (modc t24) |
| A script's main chunk fails after reading a `GlobalValue` another script sets during the session | Runs: the setter ran first | Listed apart as session-dependent, with the key and the setting script (below) | Info (not counted) | **modc t12** |
| An XML tag the port does not read | Ignored | Per database (object types, factions, campaigns, trade routes, hard points, GameConstants, SFX / music / speech events, Audio.xml, tactical cameras, movies, difficulty adjustments, lightning effects, targeting priority sets, mouse pointers, command bar components, RadarMap.xml): tags that neither the original's table for that class nor the port's loader reads, per tag with counts (below) | Warning | **main** (modc t05 object types, t15 the others) |
| A submod checked without its parent | Its lists name the parent's XML files: skipped, hundreds of missing files | Listed as missing files as before, plus a submod hint naming the installed mod that has them (below) | Info (not counted) | **modc t16** |
| A mod `.fxo` replaces or adds an effect | Loaded | Loaded and translated like a shipped effect (§4.1) | — | **main** (modc t11) |
| An effect only a mod ships as `.fx` source, or a `.fx` beside a `.fxo` | Compiled by D3DX | Compiled by `alamo_fx_prebuild` (fxc) and loaded like the original; not prebuilt or fxc error: `.fx` only: green default under its name; with a `.fxo`: the `.fxo` (§4.1, `source_only` / `source_shadowed`) | Warning (only when not compiled) | **main** (modc t11, t23); in the report: **main** (modc t21) |
| An effect's best technique uses shader model 3 | Drawn (SM3 hardware) | Translated and drawn (§4.1); an SM3 feature outside the covered list: unlit (`translate_failed`) | — | **main** (modc t11, t22) |
| An effect fails to load, has no technique, or a pass fails translation | Green default / validation failure | Green default (`load_failed`, `no_technique`) or unlit (`translate_failed`) | Warning | **main** (modc t11); report **main** (modc t21) |

The report: `alamo_mod_check [--game DIR] [MODPATH=..] [STEAMMOD=..] [FALLBACKPATH=..] [--out FILE] [--json FILE|-] [--verbose]
[--no-campaigns] [--no-lua] [--no-lua-run] [--no-files] [--no-xml-tags] [--no-submod] [--no-shaders] [--no-dxc]` loads the data exactly as
`alamo_game` does and prints files (VFS layers, warnings, per-folder new / replaced counts), loader steps, content counts,
campaign checks, missing files, unknown XML tags, Lua compile errors, main-chunk errors (and the session-dependent ones
apart, and the unused-script leads), unbound engine calls, shader issues and the submod hint, then `== result: OK|ERRORS, N errors, M warnings`
(**main**, `engine/apps/game/src/mod_check.cpp`; exit 0 without errors, 1 with, 2 usage). Each finding of the missing
files, XML tags and Lua sections is marked `[mod]` when it comes from a file in a mod folder: base FoC itself has some (49
missing cinematic models; 71 unread tags: 54 object type tags, 8 GameConstants, 3 hard point, 1 faction, 2 campaign, 1
tactical camera, 2 command bar; none of them read by the original, modc t25; mcx0 t07 took out `Base_Power_Ability` and
`Bounty_On_Faction_Ability`, ability elements the original reads, `docs/re/MCX0_T07_UNLISTED_TAGS.md`), so a mod's own problems are the marked ones. `--json` writes the summary below (`-`: to stdout instead
of the text). **modc t08** shows that summary in the game's mod picker.

A main-chunk error is a lead, not proof: the script runs with no session (no players or objects). Session state another
script sets is handled (modc t12): every entry script's main chunk runs with one `GlobalValue` store shared by the runs
(as the session's), and with `GlobalValue` wrapped so the check sees which keys a chunk read while they were unset. A
chunk that fails after reading such a key, when some script sets it (a `GlobalValue.Set` / `GlobalValue:Set` call with
that literal key outside a `--` comment, wherever it is, e.g. a `Base_Definitions` the session calls at startup), is
listed as session-dependent with the key and the first setting script (path order), not as a load warning. Example:
Thrawn's Revenge's `ModContentLoader` reads `GlobalValue.Get("MOD_ID")`, which its `GameScoring` sets in
`Base_Definitions`: 77 of TR's 80 main-chunk errors (Fall of the Republic: 107 of 111) are session-dependent; the 3 left
are `require` targets TR does not ship. A key no script sets stays an error.

A `require` of a script no layer has (modc t24): retail's stock Lua 5.0.2 `require` raises "could not load package"
there, so the chunk stops and the script does not load, as in the port (`re/MODC_LOADER.md` §6). Whether that matters
depends on whether the game loads the script, so each such run error carries the missing name and what loads the
script (`named_by`), checked in this order: a root the original enumerates or whose loading is not traced (the AI
folders, Evaluators, Interventions: stays a warning), a script of the same path in the game without the mods, a literal
`require` of its name in another script, an XML element that names scripts (`Lua_Script` of object types and story
plots, `GalacticFreeStoreScript` / `SpaceFreeStoreScript` / `LandFreeStoreScript`, `Game_Scoring_Script_Name`, any
file under `Data\XML`). A mod's own script under GameObject, Story, FreeStore or Miscellaneous that none of these names
never runs in retail either: it is listed as an **unused-script lead** (info, `lua.unused_scripts`). Thrawn's Revenge:
all 3 (`GameScoring_old.lua` beside the `GameScoring` that GameConstants names, `Targeting.lua`,
`ObjectScript_MicroJump.lua`) are leads, so TR has 0 main-chunk warnings left.

Shader issues (modc t21): the report runs `materials::audit_effects` like `alamo_effect_audit` (§4.1; with the runtime
DXC when it loads, `--no-dxc` translation only, `--no-shaders` off) and lists every issue as a warning with its kind,
effect, path, `[mod]` and detail, and what draws the effect: `terrain` (a `Shaders\Terrain\` effect the terrain
renderer picks by name), `engine` (`Shaders\Engine\` or a device-init engine effect such as `AlamoEngine`), else
`models` with how many object type models (every `*Model_Name` tag of the first definitions, `.alo` found) name it in
a material and the first one. The models are only read when an issue is a `models` effect (none in base FoC).

Unread XML tags (modc t15): for the object types, the field tables and sub-object elements as before; for the other
databases the port loads (`kXmlDatabases` in `mod_check.cpp`: where each one's entries are, the list file's `<File>`
entries or one file, and the entry element), a direct child of an entry counts as read when the original's descriptor
table for that class has a row for it (`docs/re/xml_field_tables.md` through `objects::field_schema()`, by segment owner)
or the port's loader reads it although no row lists it (named per database with the loader: hard point `Type`,
GameConstants `Ships_Per_Stack`, Audio.xml `Ambient_Map_Sounds`, tactical camera `MinHeightAboveTerrain`, and every
speech event tag, whose class has no table in the doc). Since modc t25 also CommandBarComponents (every child of the
root is a component, as the original's `Parse_Database_Entry` reads it) and RadarMap.xml, which the original walks as a
whole: no element path is checked, a known tag is read at any depth, an element without text is only a container, and
`Passability_Color_Settings`, `Color` and `Radar_Map_Event` are the walker's own names (`re/xml_field_tables.md`, the
"modc t25" sections, which also added the 59 command bar rows `field_tables.py` had missed). Tags match
case-insensitively in the original (the vanilla misspelling `Icon_ALternate_Texture_Name` is read). Of base FoC's 16
non-object tags that no table had, only `Faction` `Text_ID` is read; the other 15 (8 GameConstants, `Faction`
`Space_Retreat_Flight_Increment`, `Campaign` `Good_` / `Evil_Victory_Conditions`, 3 `HardPoint` `*_Hardpoint`,
`TacticalCamera` `Location_Height_Smooth_Time`) and the command bar's `Mega_Texture_Name` / `Disabled_Darken` are not
read by the original either, so they stay listed.

Submods (modc t16): when a mod's own list files (`Data\XML\*Files.xml`) name XML files that no layer has, the report
looks for them in every installed mod outside the stack: the workshop items of the Steam library folders (the same
folders as the STEAMMOD= resolver, §3.1) and the folders of `<game>\Mods`. Each candidate is opened as a tree of its own
(its loose `Data` and the MEGs its `MegaFiles.xml` / `Patch*.meg` name), so a hit is the candidate's own file. The
candidate with the most hits is the likely parent when it has at least 2 and at least half of them; ties go to the one
whose workshop id appears in the mod's `modinfo.json` (e.g. "Requires Thrawns Revenge 1125571106"), then to workshop
items, then by id. Models are not counted: full mods miss art too (TR: 27 models, 0 list entries), only a mod whose
database lists another mod's files is a submod. The hint gives the arguments to check it stacked (the mod's own, then
the parent's: `STEAMMOD=<sub> STEAMMOD=<parent>`, the order of Thrawn's Legacy's own launcher). Nothing is taken from a
mod's name. On the workshop mods installed here (2026-11-18), 2858787558's 18 missing list entries are in no installed
mod. The compatibility patch `AOW_Thrawns_Legacy_Compatibility` (no declared dependencies) resolves from its folder
alone to itself, then Thrawn's Revenge 1125571106 (Thrawn's Legacy HD is not installed here, so the chain stops at TR;
the conformance mod `StackTop` covers the full three-level shape).

### 5.1 The machine-readable summary (`alamo.mod_report`, version 4)

One JSON object, UTF-8 (bytes of the data that are not UTF-8 are written as the Latin-1 character of that value), keys in
the order below, every list complete. A new key may be added in a later version of the same number; a key is never
removed or changed in meaning without raising `version`. Paths are game-relative and upper case unless noted.

| Key | Type | Meaning |
|---|---|---|
| `schema`, `version` | string, number | `"alamo.mod_report"`, `4` |
| `result` | string | `"ok"` (no errors) or `"errors"` |
| `errors`, `warnings` | number | as the text report's result line (levels in the table above) |
| `game`, `args` | string, [string] | the FoC folder and the VFS command line as given |
| `loaded`, `load_error` | bool, string | whether the data loaded; why not |
| `files` | object | `mod_dirs` [host path], `vfs_warnings` [string], `added`, `replaced` (mod files with no lower-layer copy / replacing one), `folders` [{`folder` (the first folder below `Data`, `""` for `Data` itself), `added`, `replaced`}] |
| `loaders` | [object] | per loader step in load order: `name`, `ok`, `ms` (wall clock, varies per run), `diagnostics` [string] |
| `content` | object | `campaigns`, `planets`, `trade_routes`, `object_types`, `factions`, `ai` (bool: AI data loaded), `ai_plans`, `tactical_space_plans`, `tactical_land_plans`, `official_maps`, `custom_maps`, `skirmish_maps` [{`map`, `kind` (`"land"` / `"space"`), `players`, `in_mod`, `custom`}] (mcx7: the Official tab's rows, then the Custom tab's, in menu order; the corpus runner's map pick) |
| `campaigns` | object | `checked`, `without_planets` [campaign name] (warnings: menu entries that cannot be started; when it lists every checked campaign, one error), `startable` [campaign name] (mcx7: the checked campaigns with planets, database order), `skipped_rows` (number of skipped rows over all campaigns), `warnings` [one line per message with its campaigns] |
| `missing_files` | [object] | `path`, `referenced_by` (the first naming list file, or `"<type> (<tag>)"`), `references`, `in_mod`; sorted by path |
| `unknown_xml_tags` | [object] | `database` (`"object types"`, `"factions"`, `"campaigns"`, `"trade routes"`, `"hard points"`, `"game constants"`, `"sfx events"`, `"music events"`, `"speech events"`, `"audio"`, `"tactical cameras"`, `"movies"`, `"difficulty adjustments"`, `"lightning effects"`, `"targeting priority sets"`, `"mouse pointers"`, `"command bar components"`, `"radar map"`), `tag` (first spelling), `occurrences`, `types` (distinct entries using it), `first_file` (as listed, not upper-cased), `in_mod`; most occurrences first, then by database and tag |
| `lua` | object | `scripts` (compiled), `compile_errors` [{`path`, `error`, `in_mod`}], `chunks_run` (entry scripts run), `run_errors` [{`path`, `error`, `in_mod`, `missing_require` (the script name of a `require` that found no file, `""` for other errors), `named_by` (with `missing_require`: what loads the script: `"<xml file> (<TAG>)"`, `"require in <script>"`, `"base script"`, `"script root <root>"`)}] (not session-dependent), `session_errors` [{`path`, `error`, `in_mod`, `global_value` (the unset key read), `set_by` (the first script setting it)}] (info), `unused_scripts` [as `run_errors`, `named_by` `""`] (info: unused-script leads), `unbound_calls` [{`name`, `kind` (`"global"` / `"method"`), `call_sites`, `scripts`}] |
| `shaders` | object | `checked` (bool: the effect audit ran), `effects`, `mod_effects`, `loaded`, `translated_passes`, `translated_ok`, `compiled` (bool: the runtime DXC compiled the passes), `fx_sources`, `fx_sources_compiled` (effects with a `.fx`, and of those loaded from the `alamo_fx_prebuild` cache) as `alamo.effect_audit` (§4.1), `fx_prebuild` (modc t36, added in version 3: {`ran` (bool: a mod is mounted and the prebuild ran), `fxc` (the fxc used, `""` when none was found), `sources` [{`path` (game path of the `.fx`, as found), `ok` (compiled now or before: the loader takes it), `cached` (the result was already in the cache), `error` (when not ok: the first fxc error line, or why nothing was compiled)}] by path}), `models_read` (object type models read for the cross-reference), `issues` [{`kind` (§4.1 issue kinds), `effect` (upper-case base name), `path`, `in_mod`, `detail`, `drawn_by` (`"models"`, `"terrain"`, `"engine"`), `models` (with `"models"`: object type models whose materials name the effect), `first_model`}] (warnings), by effect then kind |
| `submod` | object | `likely` (bool: stack it on `parents[0]`), `missing_list_entries` (XML files the mod's own lists name that no layer has; 0: no hint), `parents` [{`id` (workshop id or `Mods` folder name), `kind` (`"workshop"` / `"local"`), `path` (host folder), `arg` (`"STEAMMOD=<id>"` / `"MODPATH=Mods\<name>"`), `provides` (of the missing list entries), `mentioned` (its id is in the mod's `modinfo.json`)}] (most `provides` first, at most 5), `stacked_args` [string] (when likely: `args` then the parent's `arg`) |
| `check_errors` | [string] | a check that threw (errors) |
| `honour_gaps` | [object] | (v4) tags of retail's descriptor tables that the data sets to a value other than empty, `TBD` or the class default, and that the tag ledger (`docs/status/tag-ledger.md`) says the port does not act on: `class` (descriptor segment owner), `tag`, `state` (`"unread"` / `"stored"`), `uses`, `entries` (distinct entries), `first_file`, `in_mod`, `tier`, `base_uses` (base FoC's uses, from the ledger), `spec` (the ledger's `docs/re` page, `""` when none); most uses first |
| `retail_equivalent` | object | (v4) findings retail shares, which never lower the grade: `count`, `items` [{`kind` (`"unread_tag"`, `"script_error"`, `"unknown_reward"`, `"dropped_event"`, `"noop_binding"`, `"unregistered_token"`), `subject`, `uses`, `first_file`, `in_mod`, `detail` (why retail does the same)}] by kind, then most uses |
| `bindings` | object | (v4) `called` (distinct engine bindings the scripts call), `unconsumed` [{`name` (`"Name"` or `"Class.Name"`), `effect` (binding ledger: `"recorded"` with no consumer in `alamo_game`, `"noop-gap"`, `"partial"`), `detail`, `call_sites`, `scripts`, `first_script`, `in_mod`}] (methods are matched by name), most call sites first |
| `story` | object | (v4) `files` (Data\XML files with story events), `rewards_used`, `rewards_missing` [{`name`, `uses`, `first_file`, `in_mod`, `note`}] (retail rewards the port lacks), `events_used`, `events_partial` [same shape] (event types the port dispatches with a documented gap) |
| `ai` | object | (v4) `token_coverage`: {`checked` (AI data loaded), `equations`, `leaves` (distinct token paths), `missing` [{`token` (as written: `Variable_Target.EnemyForce.X`), `equations`, `first_equation`, `first_file`, `in_mod`}]}: paths the resolver of the equation's mode does not evaluate (the galactic resolver's registry, the tactical leaf tables; retail loads one equation set and the caller picks the mode: the template mode for template triggers and budgets, followed through equations that name others; goal proposal functions and equations only Lua plans name count with either resolver), so the equation fails (retail's rule for an unknown leaf, `AI_GALACTIC_TOKENS.md` §9.1) |
| `limits` | [object] | (v4) `limit`, `value`, `needed`: data that hits a guard or a cited retail limit (filled by `mcx5`; empty until then) |
| `assets` | object | (v4, mcx2 t01; only with a mod mounted and the data loaded; `--no-assets` leaves it out) every model, particle, animation, texture, map, sound and movie a mod layer serves (loose or in the mod's MEGs; not in `.`-folders), decoded with the runtime decoders: `checked`, `decoded`, `salvaged_count`, `rejected_count`, `retail_equivalent`, `salvaged` [{`path`, `family`, `rule`}], `rejected` [{`path`, `family`, `reason`, `retail_equivalent`}], `by_family` {name: {`checked`, `decoded`, `salvaged`, `rejected`}}. A rejection retail shares (`docs/re/MCX2_T02_ASSET_SALVAGE.md`) never blocks G3; `--strict` counts every salvage as a rejection; `--corpus-cache DIR` caches verdicts by content hash |
| `grade` | object | (v4) `grade` (`"G0"` errors, `"G1"` loads; G2 and up need runs), `g3_static` (bool: no honour gap, binding gap, missing reward or missing token from the mod's own files; the gaps base FoC content has are the tag ledger's and mcx4's, not the mod's, so they do not count here), `blocking` [`"<issue kind> <subject>"`], `check_errors` [string] (a version 4 check that threw; not counted in `errors`) |

`in_mod` is true when the file the finding comes from resolves to a mod folder. The synthetic mods of
`engine/apps/game/testdata/mod_fixture` (modc t07, t12, t15, t16; README there) are checked through this summary in ctest
(`alamo_game_mod_fixture_tests`, `alamo_mod_check_fixture`).

Version 2 (modc t12, t15, t16, 2026-11-06): `run_errors` no longer holds the session-dependent errors (now in
`session_errors`), so `warnings` no longer counts them; `unknown_xml_tags` covers the other databases and has
`database`; `submod` is new; a campaign without planets is a warning unless no campaign has planets (then one error).

Version 3 (modc t21, t24, 2026-11-07): `shaders` is new and its issues count as warnings; `run_errors` entries have
`missing_require` and `named_by`, and the unused-script leads moved from `run_errors` to `lua.unused_scripts` (not
counted). Readers of version 2 keys need no change. `unknown_xml_tags` also covers `"command bar components"` and
`"radar map"` (modc t25).

Version 4 (mcx0 t06, 2026-11-17; `docs/MOD_COMPAT_DESIGN.md` §12.1): `honour_gaps`, `retail_equivalent`, `bindings`,
`story`, `ai`, `limits`, `assets` (mcx2 t01) and `grade` are new, after `check_errors`. None of their entries counts as an error or warning,
so `result`, `errors` and `warnings` are unchanged for the same data. Readers of version 3 keys need no change. The
findings are also core `alamo::CompatIssue` records (`ModCheckReport::issues`, `engine/core/include/alamo/core/compat_issue.hpp`).

Installed mods, 2026-11-07 (base FoC at F:): with modc t22 (SM3 translated) the shader issues left are
`AlamoEngine` `source_shadowed` (an engine effect: TR, its copy 3622043265, FotR) and `_TerrainRenderBump`
`source_only` (a terrain effect: 3622043265, FotR); with modc t23 and no `alamo_fx_prebuild` cache on the machine they stay
(the report says "no compiled entry ... run alamo_fx_prebuild"; `.fx sources` TR 1, FotR / 3622043265 2, 0 loaded
compiled). Before t22 the cross-reference found `Planet_Deathstar`
(1770851727, `technique_skipped`) named by 1 object type model (`EMPIRE_DEATH_STAR.ALO`) and `MeshFresnelGlow`
(2858787558, `unsupported_pass`) by none. Missing-require
run errors: Thrawn's Revenge 3 and Fall of the Republic 4 are all unused-script leads (0 left as warnings); 3622043265
keeps 2 (`InstantAction_*_Tech.lua`, named by `Templates_Instant_Action.xml`, `require("eawx-std/class")` which that copy
does not ship) and has 3 leads.

### 5.2 The mod picker in the game (modc t08)

`alamo_game`'s main menu has a **Mods** button (port dialog `IDD_PORT_MODS_DIALOG`, `engine/ui/src/gui_menus.cpp`;
host side `engine/apps/game/app/app_mods.cpp`, data side `alamo/game/mod_picker.hpp`). It is shown in live play only
(not in smoke scripts that do not ask for it, not in benchmark runs).

- **Rows**: "Base game (no mod)", then every Steam workshop item (the library folders of §3.1, numeric id order, first
  library wins), then every `<FoC folder>\Mods\<Name>` folder (name order); a folder needs a `Data` folder. The label is
  the `name` of the mod's `modinfo.json` (top-level key; else the folder name), the id only when two mods share a name,
  and where it is installed. The row of the running mod (the first `MODPATH=` / `STEAMMOD=` of this run) says "(running)".
- **Report**: opening the picker or selecting a row runs `check_mod_data` (the §5 report) for that row on a worker thread,
  one at a time (the newest request waits); the pane shows "Checking..." until it is done, then a summary: loads or not
  (error / warning counts), content counts, campaigns without planets, missing files (and how many the mod names), the Lua
  compile / main-chunk counts, unbound engine calls, the shader audit (effects, issues; [port] without the runtime DXC
  compile, `ModCheckOptions::shader_compile` off, to keep the check light in the running game), the submod hint (parent, how many missing files it has, named in
  modinfo.json) and the arguments it starts with. Reports are kept per row for the rest of the run.
- **Stack** (mcx3 t06): the row's stack is resolved first (§3.1.1) and the report is of that stack; the pane shows
  "Stack: A > B > C", stack errors and warnings, the grade badge (report v4), the base-EaW refusal or guess, and (t09)
  a line per mod whose `modinfo.json` lists no text in the game's language (the language retail picks from the Windows
  user language, `ui::retail_language_for_langid`; a `modinfo.json` without `languages` is English).
- **Launch** (enabled once the selected row has a report and is not the running one): this executable starts again with
  the same arguments, its `MODPATH=` / `STEAMMOD=` arguments replaced by the row's resolved stack (else, a likely submod
  stacked on its parent: `submod.stacked_args`), and this process quits; a refused base-EaW mod has no launch. [port] The data, text and GUI load once per process, and the original
  takes mods only on its command line, so switching needs a new process.

Tests: `alamo_game_mod_picker_tests` (rows, modinfo names, arguments, the summary on `testdata/mod_fixture`),
`alamo_ui_screens_tests` (the dialog flow), the skirmish smoke (opens the picker after the main menu, waits for the base
game's report, `game_mod_picker.png`).

## 6. Port rules that apply only when a mod layer is loaded

Each is keyed on "a mod layer is loaded", never on which mod (§1 rule 4). With no mod loaded the port behaves as base FoC.

| Rule | Where | Why | Decided |
|---|---|---|---|
| Skirmish buy list also requires the type to be named in some type's `Tactical_Buildable_Objects_Multiplayer` (`SkirmishBuyOptions::multiplayer_list_only`) | `engine/apps/game/rules` (**Q6 stack**) | Mods list option / marker / layer types as buildable; the interim rule drops them (`SKIRMISH_SETUP.md` §2.7 port notes) | Gabriel, 2026-11-04 |

Add a row here for every new rule of this kind, in the same change that adds it.

## 7. Known violations (to be removed)

Found while writing this contract; the full audit is **modc t02**, the replacements **modc t03**, the lint **modc t04**.

- None left from this list (2026-11-06). `ai_type_for`, `is_stock_faction`, the `Rebel` / `Empire` slot defaults and the
  `"Hostile"` lookup went in **modc t03**; the built-in skirmish unit lists and the base-game exception for campaign
  `AI_Player_Control` pairs in **modc t17**; the `Kuat` / `Coruscant` skirmish planet and the `_MP_SPACE_KUAT` default map in
  **modc t19** (the skirmish galaxy's planet is the map's own planet type when the data has it and it forbids no unit, else
  the first such `PLANET` type: `skirmish_galaxy_planet`; the staging dialog selects the remembered map, else row 0, as
  `UI.md` 9.7). The remaining kept rows of the audit (`docs/status/modc-audit.md`) are retail hard-codes or test hosts.

Each needs either a data-driven replacement or a `docs/re/` citation showing the original does the same (§1 rule 3).

## 8. Where the parts are implemented

| Part | Task |
|---|---|
| VFS, MODPATH / FALLBACKPATH, resolution order, saves remember the mod | done (Phase 2, N1) |
| Mod diagnostics `alamo_mod_check`, loader stages, `Basic_AI`, skirmish forces from the data, object scripts | Q6 stack |
| Audit of hard-coded names and counts | modc t02 |
| Data-driven replacements, base-FoC goldens unchanged | modc t03 |
| Lint: no mod / faction name literals in `engine/` sim code | modc t04 |
| Load report for any mod folder, machine-readable (§5, §5.1) | modc t05 |
| Second mod (not Thrawn's Revenge) loads and plays | modc t06 |
| Synthetic fixture mod in ctest through the real `MODPATH` route | modc t07 |
| Mod picker in the game UI (§5.2) | modc t08 |
| `STEAMMOD=` resolver from the Steam library folders, no Steam API (§3.1) | modc t10 |
| Mod-only effects: what the renderer does, issue record, `alamo_effect_audit` (§4.1) | modc t11 |
| Shader issues in `alamo_mod_check` and `alamo.mod_report` | modc t21 |
| Shader model 3 in shaderxlat | modc t22 |
| `.fx` source compilation (prebuild / cached) | modc t23 |
| VFS layers and provenance (`vfs::Layer`, `layer_of`, `resolve_all`); effects and SFX off `mod_dirs()` (§3.2) | ums0 |

## 9. Second mod (modc t06)

The proof that the loader is not shaped around Thrawn's Revenge: two other installed workshop mods, checked headless on
2026-11-06 (MSVC Release, `alamo_mod_check`, `alamo_headless`). Gabriel's in-game run is **modc t09**
(`tools/probe_mod/modc_t09/README.md`).

**Picks.** Of the workshop items on this PC (`steamapps\workshop\content\32470`), 1125571106 / 3622043265 are Thrawn's
Revenge, 1976399102 is Fall of the Republic (same team and script framework as TR, so it proves little), 3830298393 has no
`Data\`, and every local `Mods\` folder is a TR submod. That left:

| Mod (named from its own data; neither has a `modinfo.json`) | Id | Why |
|---|---|---|
| EAW-Remake (`<Info Name="EAW-Remake">` in its MEG lists) | 1770851727 | Total conversion by another team: 21 GB, own MEGs replacing base ones (`Config.meg`, `Shaders.meg`, `Maps.meg`), 93 own effects, 10,031 types, 30 factions, `Basic_AI` skirmish AI |
| Star Wars: Age of Legends, New Jedi Order era (its main menu title; `AoL_mod_files` root tags, `NJO.jpg`) | 2858787558 | Mostly loose XML, 13,449 types, 50 factions, 169 campaigns, its own root tags and a planet template; no TR framework |

Neither is small; no small non-TR mod is installed. The fixture mod (`testdata/mod_fixture`, modc t07) stands in for one.

**Results** (final binary; times are wall clock on this PC):

| Check | EAW-Remake 1770851727 | Age of Legends 2858787558 |
|---|---|---|
| Load report | OK, 0 errors, 187 warnings; 23 campaigns (8 menu-only), 694 planet types, 93/93 effects compiled, 0 Lua compile errors | OK, 0 errors, 1337 warnings; 169 campaigns, 455 planet types, 88 effects, 97 scripts do not compile (95 copied 32-bit bytecode, 2 with a UTF-8 BOM: the original rejects both), 18 listed XML files missing |
| Space skirmish, 3000 ticks | OK: 106 orders, 185 decisions, 0 Lua errors, but slow (133 s; a sim tick costs about 26 ms, 96% of it `tac.movement`, on the mod's Geonosis map with its huge asteroid obstacles; **modc t33**) | OK: 3.4 s, 59 orders, 132 decisions, 23 Lua errors (the mod's own scripts: `Try_Weapon_Switch` undefined, an unknown `FindAllObjectsOfType` filter) |
| Land skirmish `_MP_LAND_UTAPAU`, 3000 ticks | OK: 1.1 s, 60 orders, 34 decisions, 0 Lua errors (no `Land_Skirmish_AI_Default_Forces`: the [port] cheapest-units fallback) | OK: 5.7 s, 72 orders, 185 decisions, 0 Lua errors |
| Galactic AI, 9000 ticks, `--story --scoring --object-scripts` | `GC_Mini_Web_Way_Imperial`: OK, 34 s, 131 planets, 12 AI players, 157 decisions, 43 orders, 1 owner change, 828 story events, 0 Lua errors | `Sandbox_Balance_New_Republic`: OK, 22 s, 77 planets, 17 players (10 are `None` in the campaign's own `AI_Player_Control`), 384 decisions, 165 orders, 0 Lua errors |

`alamo_game --smoke skirmish --hidden --audio null STEAMMOD=<id>` (the TR smoke route) gets through the intro, the main
menu, the attract demo and the mod picker (it lists the running mod's report) for both, then stops in the single-player
flow the script was written for: on the Remake the Single Player click leaves the main menu up (its menu draws "Single
Player" as a heading with no button under it); on Age of Legends the skirmish staging dialog's game-type list has no row 1
to select. Age of Legends' main menu draws its own background and title. Both are **modc t34**; t09 looks at them in the
game.

**modc t34 (2026-11-17):** both stops were general gaps. The Remake's main menu writes the Single Player button as
`265,550,215,,22`; retail splits `.rc` words on runs of spaces, tabs and commas (UI.md 3.2), so it is a 22-unit-high
button, while the port read an empty height. Age of Legends' skirmish staging dialog has no `IDC_GAME_TYPE_LIST` at all:
the dialog keeps its default type, and the smoke now notes that instead of waiting for a row. With both fixed the Remake
smoke reaches a running space skirmish (it then stops at the smoke's control-group step: **t40**), and the Age of Legends
smoke reaches the buy screen as slot 0's default faction, the mod's list-header faction `Faction_Header_Major_Powers`,
which has nothing to buy (**t41**).

**Retail run (modc t09, 2026-11-17; results in `tools/probe_mod/modc_t09/README.md` "Results").** Neither mod has a
pre-battle buy screen in retail (SKIRMISH_SETUP §2.7: single-player skirmish never had one; the port's is a kept [port]
feature). The Remake's Single Player button and one-click menu match the port after t34. The slot factions come from the
player profile (SKIRMISH_SETUP §2.8: player = the saved index, 0 on a fresh profile; AI = that index XOR 1), so the port's
row 0 / row 1 default is retail's fresh-profile default and Age of Legends' header row is a legal retail choice; since
t47 the port keeps the last faction and colour too. Age of Legends' "Space Skirmish" plays a land battle (no type list:
Land) with no own units in retail too (the port starts it empty since t48). The kit asked for Ctrl+Y (the port's key);
retail's Select All is Ctrl+A, so its selection readings are void. Retail greys the Remake station's ship buttons (only
upgrades buildable, population 0/150).

Retail run `modc_t45` (2026-11-17, screenshots in the kit's `out\`, not committed):
- Fall of the Republic's progressive campaign (`CloneWars_Medium_Republic`) works in retail. It opens on a one-planet
  loader galaxy (empty radar, "Initializing Galactic Conquest"), and a Lua year popup (22 / 21 / 20 / 19 BBY) appears.
  The choice calls `StoryUtil.LoadCampaign` (the `Template_Load_Campaign` event's `LOAD_CAMPAIGN` reward, campaign
  `<PLANET>_Era_<n>_<FACTION>`). About 30 s later the full galaxy (about 100 planets, pop 66/826) replaces it, with no
  game end. The port's one-planet start is that loader. The load was missing: nothing took the queued `LOAD_CAMPAIGN`
  request. Since **t45** the host starts the named campaign on the next frame (STORY_REWARDS_GALACTIC §2.1, retail
  0x44a3f0). `GCProgressiveStateMachine.lua` (the missing `FotRProgressiveStates`) is a leftover that no campaign uses.
  Whether the year popup (option objects picked with the `Event_Trigger` fleet's ability) works in the port is a
  played-game check.
- EAW-Remake: the station's ship buttons stay grey and unbuildable at station level 2 too (clicking TIE does nothing);
  no timer unlock at 3.5 min. The tooltips name no missing requirement. The binary settles that tactical production never
  reads `Required_*`, `Tech_Level` or `Build_Initially_Locked` (SKIRMISH_SETUP §3), and the port does the same. Left in
  retail's code: a full `Tactical_Units` queue, or a build limit counting queued units; the mod sets `Pay_As_You_Go`, so
  credits cannot grey a button (**t50**, needs a trace of retail).

**General gaps found and fixed** (none names a mod; base-FoC goldens unchanged):

- `Find_Nearest` raised "invalid type for parameter 2" for a non-player in its player slot (a mod's free store passes the
  nil from a category helper). Now it converts like `Find_All_Objects_Of_Type`'s player arguments: no player filter, a
  non-bool keeps the default (`LUA_API.md`, [M]). Remake space: 58 errors before, 0 after.
- Battle AI attack with a TEAM-container target and a fighter attacker dereferenced a null object (crash after 8 s in Age
  of Legends space, 110 s in Remake space). The fighter block is now the container itself, as the human path
  (`TacticalBattle::exec_attack`) does.
- The story and plot loaders checked the root element's name (`Story`, `Story_Mode_Plots`); Age of Legends uses its own root
  tag in 562 files and its campaigns failed to start. The original reads children only (`STORY.md` §2.1, [M]).
- The planet table ignored `Variant_Of_Existing_Type`: all 434 of Age of Legends' planets are variants of one template
  planet, so none had a `PLANET` behaviour, no planet object was created and the mod's AI script spun in
  `while true do for _ in pairs(empty) ... end end`, which never yields (the original hangs on that loop too, but it has the
  planets). Planet columns now resolve the chain by the OBJECT_TYPES §3.3 / §3.4 rules (`test_planet_variants`).

**Open** (new `modc` tasks): **t33** space-skirmish movement cost on maps with very large obstacles (Remake Geonosis,
about 26 ms a sim tick); **t34** the two front-end stops above. The picker names a mod without `modinfo.json` by its
workshop id only (both mods here). The Lua C sources are built without frame pointers in sanitizer builds, so crash
stacks stop inside Lua (a debugging aid only, not filed).

## 10. Compatibility target (Gabriel, 2026-11-13)

The goal is **near-full compatibility with Forces of Corruption mods**: a mod that uses the original's data mechanisms (§4)
plays in the port as it does in retail. Decision: [`DECISIONS.md`](DECISIONS.md), "Mod compatibility target". The roadmap
group `modc` closes when every mod in the reference set below passes the checklist; whether it does is on the roadmap only.

### 10.1 What "plays" means

A mod passes when all of these hold (the headless runs are the ones §9 used):

1. **Load.** `alamo_mod_check` reports no errors. Every warning is something retail also rejects or skips (§5), or is filed as
   a `modc` task.
2. **Front end.** `alamo_game` with the mod reaches its main menu, its skirmish staging and its campaign selection; the hidden
   skirmish smoke (`alamo_game --smoke skirmish --hidden --audio null STEAMMOD=<id>`) gets into a running skirmish.
3. **Skirmish.** A space and a land skirmish on the mod's own maps run 3000 ticks AI against AI in `alamo_headless` with AI
   orders and decisions and no Lua error the port causes, and one skirmish is played to a result in the game.
4. **Galactic conquest.** Every campaign the mod's menus offer starts (none without planets, unless it is menu-only, §9), and
   one runs 9000 ticks with `--story --scoring --object-scripts` with AI decisions, orders and no Lua error the port causes.
5. **Story.** For a mod with story campaigns, one starts and reaches its first story events (objectives, handouts).
6. **Stable and deterministic.** No crash or hang in any of the above; the same mod and command line give the same per-tick
   CRC on every worker count and both compilers (§1 rule 5); the base-FoC goldens do not move.

Speed is not in the checklist while tier-3 work is paused (`DECISIONS.md`, "Current focus"); a mod whose sim is too slow to
play is filed as a `modc` task (as t33).

### 10.2 What may still fail ("near")

- What retail also rejects or fails on: copied 32-bit Lua bytecode, a script with a UTF-8 BOM, an error in the mod's own
  script (an undefined function it calls, a wrong argument retail also refuses), a listed file the mod does not ship.
- What is out of scope for every mod (§4): multiplayer and the original game's save files.
- Mods that change the game executable or load native code (DLLs) instead of data: they are outside the data mechanisms the
  port reads.
- Mods made for base Empire at War only (retail runs them with `sweaw.exe`, not FoC): FoC mods only (Gabriel, 2026-11-17,
  D1); the port detects them and refuses them with that reason (roadmap `mcx3`).

Anything else a mod needs is a gap in the port, fixed as a general mechanism (§1 rule 6).

The next step past this checklist, a graded corpus of every installed mod (grades G0-G4, coverage ledgers, asset salvage,
load report v4), is designed in [`MOD_COMPAT_DESIGN.md`](MOD_COMPAT_DESIGN.md) and tracked as roadmap Stage 9 · Mod
compatibility system (groups `mcx0`-`mcx9`); its decisions are in `DECISIONS.md`, "Mod compatibility system decisions".

### 10.3 Reference set

The mods installed on Gabriel's PC that §9 picked or that are the Q6 target; a submod is checked with its parent loaded, a
second copy of the same mod is not a separate entry. The set grows when Gabriel installs more mods; a new mod is added here
with its id and why it adds coverage.

| Mod | Workshop id | Why it is in the set |
|---|---|---|
| Thrawn's Revenge | 1125571106 | Q6: the largest and hardest; heavy Lua framework, own GC menu and victory handling |
| Fall of the Republic | 1976399102 | Same team and script framework as TR, different era and content |
| EAW-Remake | 1770851727 | Total conversion by another team: replaces base MEGs, 93 own effects, 30 factions, `Basic_AI` |
| Star Wars: Age of Legends (New Jedi Order) | 2858787558 | Mostly loose XML, own root tags, planet variants, 169 campaigns, no TR framework |

The synthetic fixture mod (`engine/apps/game/testdata/mod_fixture`, modc t07) keeps the agnostic loading route covered in ctest without game
data; it is not a substitute for the set.

### 10.4 Checklist run

The checklist of §10.1 for every installed mod and stack is automated by the corpus runner (mcx7,
`tools/modcorpus/README.md`; grades `MOD_COMPAT_DESIGN.md` §1.3, rules `tools/modcorpus/grade.py`). The table between the
markers is generated by `tools\modcorpus\run_corpus.cmd --write-md`; do not edit it by hand. Per-setup results, command
output and the load reports stay in `logs/modcorpus/<date>/`.

<!-- modcorpus:begin -->
Sweep `2026-11-18` (`tools/modcorpus/run_corpus.py`; build `e893b3fd5193fd76` from `build-wt-msvc`, stages check, runs, smokes; per-setup results in `logs/modcorpus/2026-11-18/`). Grades: `docs/MOD_COMPAT_DESIGN.md` §1.3, rules in `tools/modcorpus/grade.py`.

| Setup | Stack | Grade | First blocker | Space skirmish | Land skirmish | Galactic | Smokes |
|---|---|---|---|---|---|---|---|
| 2498889654 (sub) (`2498889654:sub`) | 2498889654:sub > 1125571106 | G1 | galactic: no AI orders | `_MP_SPACE_FIERY`: 19 orders, 64 decisions, 0 Lua errors, 0 = 4 workers | `_MP_LAND_BLOOD_GULCH`: 13 orders, 34 decisions, 0 Lua errors, 0 = 4 workers | `Thrawn_Empire`: 0 orders, 42 decisions, 0 Lua errors, 183 story events | skirmish ok 86/86, campaign ok 102/102 |
| AotCW 1.2.1 (patch notes in the item) (`3387971722`) | 3387971722 > 1397421866 | G2 | smoke_campaign: failed (exit 1): step 6 of 102; alamo_game smoke campaign: FAILED | `_MP_SPACE_BOTHAWUI`: 100 orders, 202 decisions, 0 Lua errors, 0 = 4 workers | `_LAND_PLANET_SULLUST_02`: 325 orders, 305 decisions, 0 Lua errors, 0 = 4 workers | `Story_Awakening_of_the_Clone_Wars_Confederacy`: 313 orders, 422 decisions, 0 Lua errors, 4041 story events | skirmish ok 86/86, campaign failed 6/102 (retried) |
| AotR 2.11 (patch notes in the item) (`1397421866`) | 1397421866 | G2 | smoke_skirmish: failed (exit 1): step 86 of 86; attract hulls are not oriented along their travel direction; select all: the first unit was selectable after 33 frames; reinforcement: no reserves in this battle; space combat: no procedural laser bolt drawn (12 model projectiles peak) | `_MP_SPACE_BOTHAWUI`: 126 orders, 237 decisions, 0 Lua errors, 0 = 4 workers | `_MP_LAND_ENDOR`: 310 orders, 290 decisions, 0 Lua errors, 0 = 4 workers | `Sandbox_Awakening_of_the_Rebellion_Rebel`: 67 orders, 209 decisions, 0 Lua errors, 2603 story events | skirmish failed 86/86, campaign failed 6/102 (retried) |
| EAW-Remake (`1770851727`) | 1770851727 | G2 | smoke_campaign: failed (exit 1): step 23 of 102; alamo_game smoke campaign: FAILED | `KUAT REPAIR YARDS`: 107 orders, 112 decisions, 0 Lua errors, 0 = 4 workers | `L FRONTLINE MOS ALAMOS`: 44 orders, 167 decisions, 0 Lua errors, 0 = 4 workers | `GC_Mini_Web_Way_Imperial`: 1014 orders, 1888 decisions, 0 Lua errors, 3424 story events | skirmish ok 86/86, campaign failed 23/102 (retried) |
| Fall of the Republic (`1976399102`) | 1976399102 | G2 | static: honour_gap Campaign.Markup_Filename | `_MP_SPACE_TRANSITORY_TWISTS`: 347 orders, 466 decisions, 0 Lua errors, 0 = 4 workers | `_MP_LAND_HOTH_ECHO_BASE`: 107 orders, 180 decisions, 0 Lua errors, 0 = 4 workers | `Sandbox_Rimward_Republic`: 1163 orders, 1715 decisions, 0 Lua errors, 3910 story events | skirmish ok 86/86, campaign ok 105/105 |
| Fall of the Republic Shield Pierce Submod (`2083545253:subB`) | 2083545253:subB > 1976399102 | G1 | galactic: 1952 Lua error(s) | `_MP_SPACE_TRANSITORY_TWISTS`: 347 orders, 466 decisions, 0 Lua errors, 0 = 4 workers | `_MP_LAND_HOTH_ECHO_BASE`: 65 orders, 112 decisions, 0 Lua errors, 0 = 4 workers | `Sandbox_Rimward_Republic`: 1177 orders, 1640 decisions, 1952 Lua errors, 3910 story events | skirmish ok 86/86, campaign ok 105/105 |
| Star Wars: Age of Legends (`2858787558`) | 2858787558 | G1 | land: 9 Lua error(s) | `_MP_SPACE_GEONOSIS`: 11 orders, 31 decisions, 0 Lua errors, 0 = 4 workers | `_MP_LAND_UTAPAU`: 375 orders, 442 decisions, 9 Lua errors, 0 = 4 workers | `Sandbox_Balance_New_Republic`: 265 orders, 496 decisions, 0 Lua errors, 54856 story events | skirmish failed 86/86, campaign ok 102/102 |
| Thrawn's Revenge (`1125571106`) | 1125571106 | G2 | static: honour_gap Campaign.Markup_Filename | `_MP_SPACE_FIERY`: 1359 orders, 1421 decisions, 0 Lua errors, 0 = 4 workers | `_MP_LAND_BLOOD_GULCH`: 132 orders, 198 decisions, 0 Lua errors, 0 = 4 workers | `Thrawn_Empire`: 1215 orders, 4311 decisions, 0 Lua errors, 1678 story events | skirmish ok 86/86, campaign ok 102/102 |
| Thrawn's Revenge Shield Pierce Submod (`2083545253:subA`) | 2083545253:subA > 1125571106 | G2 | static: honour_gap Campaign.Markup_Filename | `_MP_SPACE_FIERY`: 1317 orders, 1385 decisions, 0 Lua errors, 0 = 4 workers | `_MP_LAND_BLOOD_GULCH`: 132 orders, 198 decisions, 0 Lua errors, 0 = 4 workers | `Thrawn_Empire`: 1183 orders, 4255 decisions, 0 Lua errors, 1669 story events | skirmish ok 86/86, campaign ok 102/102 |

Grades: G0 0, G1 3, G2 6, G3 0 (9 setups).

Not installed, so not graded (corpus.json `not_installed`):
- Thrawn's Revenge (second copy) (`3622043265`): not installed (2026-11-18); mcx8 t06 dropped, reopen on reinstall
- AOW Thrawn's Legacy compatibility (local submod stack on Thrawns_Legacy_HD) (`AOW_Thrawns_Legacy_Compatibility`): not installed: corruption\Mods is empty (2026-11-18)
- Legends ships transfer (local submod of 1125571106) (`Legeds_ships_transfer`): not installed: corruption\Mods is empty (2026-11-18)
- Thrawn's Legacy HD (local submod of 1125571106) (`Thrawns_Legacy_HD`): not installed: corruption\Mods is empty (2026-11-18)
<!-- modcorpus:end -->

#### Corpus findings explained (mcx8 t01, 2026-11-18)

- **Age of Legends land Lua errors (retail-equivalent, accepted in `corpus.json`).** The mod ships its own
  `Data/Scripts/Library/PGCommands.lua`, which shadows FoC's and defines only `Try_Weapon_Switch_Ground`; its
  `BusyTacticalFreeStore.lua` and `PGEvents.lua` still call `Try_Weapon_Switch`. Retail resolves `require("pgcommands")`
  to the same mod file, so it raises the same nil-call error on every free-store unit service.
- **Fall of the Republic galactic Lua errors (port gap, fixed).** Its `GetSmugglingLevel` evaluator calls
  `Target.Get_Game_Object()`; the port handed galactic evaluators the planet object instead of an AI target
  (`docs/re/AI_GALACTIC_2.md` §4.4).
- **Not installed** (`docs/DECISIONS.md`, "Corpus graded on installed mods only"): listed under the table, not graded.

#### Hand-written run of modc t39 (2026-11-17)

The run before the runner existed, kept for its explanations (t42-t49 below).

MSVC Release of the lane-modc branch (dev + t29 / t30 / t32 / t34 / t39 fixes), `alamo_mod_check`, `alamo_headless` (skirmish
`--skirmish-ai skirmish --map M --ticks 3000`, once with `--workers 4` and once with `--workers 0`, CRCs compared with
`--compare`; galactic `--economy --galactic-ai --story --scoring --object-scripts --ticks 9000`) and the hidden
`alamo_game --smoke skirmish` for the front end. Several `_MP_*` entries in the TR / FotR `Maps.meg` are empty files (the
port skips them, the skirmish host says `not loaded (empty file)`), so the maps below are non-empty ones.

| Item | Thrawn's Revenge | Fall of the Republic | EAW-Remake | Age of Legends |
|---|---|---|---|---|
| 1 Load | OK, 0 errors, 216 warnings | OK, 0 errors, 103 warnings; the one effect issue is `_TERRAINRENDERBUMP.fx`, an unused leftover (t42 below) | OK, 0 errors, 187 warnings | OK, 0 errors, 1474 warnings (877 skipped campaign rows, 97 non-compiling scripts: §9) |
| 2 Front end (smoke) | not rerun (q6 t02 / t13 passed) | **passes** (91 / 91 steps) after t49: free starting units are off in the mod, so no buy screen; the battle starts with the pre-built bases only (select all finds only static stations: the unit steps are skipped) | **passes** (91 / 91 steps) after t43: the arriving ships were left Lua-hidden (`Hide(true)` then `Cinematic_Hyperspace_In`), and the smoke now clicks the enemy where its hull is (a shield-gate ring has a hole at its centre) | main menu, staging (no game-type list), the buy screen passed with nothing to buy (header faction as on a fresh retail profile, or New Republic with `ALAMO_SMOKE_SKIRMISH_FACTION=1`: also an empty land list); **passes** (land, 52 / 52 steps) after t48: the skirmish starts with no own units, as in retail (t09 A4), and runs to the battle end |
| 3 Space skirmish 3000 ticks | `_MP_SPACE_FIERY`: 833 orders, 1070 decisions, 0 Lua errors | `_MP_SPACE_TRANSITORY_TWISTS`: 724 orders, 860 decisions, 2 Lua errors AI vs AI; with `--human Rebel` 385 orders, 0 Lua errors (t44 below) | `_MP_SPACE_GEONOSIS`: 95 orders, 139 decisions, 0 Lua errors; 189 s (t33, speed not in the checklist) | `_MP_SPACE_GEONOSIS`: 13 orders, 45 decisions, 0 Lua errors |
| 3 Land skirmish 3000 ticks | `_MP_LAND_BLOOD_GULCH`: 120 orders, 200 decisions, 0 Lua errors | `_MP_LAND_HOTH_ECHO_BASE`: 181 orders, 213 decisions, 2 Lua errors AI vs AI; with `--human Rebel` 68 orders, 0 Lua errors (t44 below) | `_MP_LAND_UTAPAU`: 62 orders, 32 decisions, 0 Lua errors | `_MP_LAND_UTAPAU`: 374 orders, 445 decisions, 0 Lua errors |
| 4 Galactic 9000 ticks | `Thrawn_Empire`: 4338 decisions, 1267 orders, 3 owner changes, 0 Lua errors (after t45; before: 4384 / 1281 / 4) | `Sandbox_Rimward_Republic`: starts after the missing-plot fix, 400 story events; 0 AI orders headless by the mod's design (t45 below), 0 story Lua errors after t45 (was 1) | `GC_Mini_Web_Way_Imperial`: 456 decisions, 137 orders, 11 owner changes, 0 Lua errors | `Sandbox_Balance_New_Republic`: starts after the player-cap fix (50 players), 533 decisions, 298 orders, 0 Lua errors |
| 5 Story start | 1145 story events | 400 story events | (galactic run: story on) | 54,856 story events, 38 Lua notes |
| 6 Deterministic | 0 vs 4 workers identical (space, land) | identical (space, land) | identical (space, land) | identical (space, land) |

General gaps fixed in this run (no mod named in engine code): the `.rc` word splitter (§9, t34), a skirmish staging dialog
without a game-type list in the smoke, the smoke's select all waiting for start forces that arrive later (t40), the
`[port]` player cap 32 → 64 (`commands::kMaxPlayerSlots`, `sim::kMaxPlayers`; retail's PlayerList has no cap; Age of
Legends' sandboxes make one player per faction, 50), and a listed story plot file that does not exist is skipped with
retail's warning instead of failing the story start (STORY.md §2.1; FotR's Rimward sandbox lists a CIS plot it does not
ship).

Explained without a port change (2026-11-17):
- **t42** Fall of the Republic's `Data\Art\Shaders\Terrain\_TERRAINRENDERBUMP.fx` is an unused leftover: terrain effects
  are picked by fixed names, the mod ships the real `TerrainRenderBump.fxo` (it loads), nothing names the underscore file,
  and its line 172 builds a `float4` from a `float4` and a float, which no HLSL compiler accepts (retail's D3DX9 neither).
- **t44** The two Fall of the Republic free-store errors come from `Variable_Human.IsFaction` leaves: with no human player
  a `Variable_Human` leaf fails the whole equation (AI_GALACTIC_TOKENS §9.1), so `EvaluatePerception` gives nil in an
  AI-vs-AI headless run. Retail skirmishes always have a human; with `--human Rebel` both maps run with 0 Lua errors. The
  tactical AI stats now name the failing leaf (`eval FAIL <equation> at <site>`, `--ai-log`).
- **t45** (AI part) Fall of the Republic's sandbox starts every player at `AI_Player_Control None`; its GC master script
  (`GC_MasterScript_22_BBY_Rimward.lua`) publishes `INITIALIZE_AI` (the OptionsHandler then switches each AI type) only
  after the human answers its `HISTORICAL_GC_CHOICE` popup, which the mod's GenericPopup offers as option objects to
  build. A headless AI-only run never answers it, so 0 orders is the mod's design there. The hidden campaign smoke with
  `ALAMO_SMOKE_CAMPAIGN_SET=Sandbox_Rimward_Republic` gets from the galaxy into a battle (then stops at t43).
- **t45** (story Lua error, fixed) Fall of the Republic's news plugin names a conquered planet through `PlanetTable`,
  keyed by upper-case planet type names. The port gave story and AI scripts the type name as written (`Trandosha`); the
  original stores every type name upper-cased (OBJECT_TYPES §3.1), so `Get_Type().Get_Name()` is now upper-case in every
  Lua VM, not only in object scripts. No golden moved; Thrawn's Revenge's AI changed slightly (numbers in the table).
  Fixed in t45 too: `CloneWars_Medium_Republic` is a loader campaign whose year choice loads the era campaign through
  `LOAD_CAMPAIGN` (retail run modc_t45, §9). The host now starts that campaign (a general gap: every story
  `LOAD_CAMPAIGN`, the stock tutorials' included, was dropped before).
- **t49** (fixed) Fall of the Republic sets `MP_Default_Free_Starting_Units` to no: the slots start without forces, so the
  port's buy screen is skipped (a bought fleet would never reach the map; SKIRMISH_SETUP §2.7). Space smoke 91 / 91.
- **t48** (fixed) a map-battle slot whose faction has no forces starts empty (`SkirmishSetup::empty_slots_ok`); Age of
  Legends' land smoke plays to the battle end (52 / 52).
- **t47** (done) the staging dialog keeps the last faction and colour (`game_options.txt`) and applies the AI slot rule.

**Not passed yet:** t50 (Remake ship buttons grey in retail: needs a trace of retail); one skirmish played to a result in
the game, and FotR's year popup, need a person. The retail comparison (t09) ran on 2026-11-17 (§9).
