// Q5 / modc t05 mod load report: what the port makes of a mod's data. Loads the data exactly as alamo_game does
// (load_game_data with the MODPATH= / STEAMMOD= / FALLBACKPATH= arguments) and reports, without running a session:
//   files      the VFS layers and warnings, and per Data\<folder> of the mod dirs how many files are new and how many
//              replace a base / MEG / fallback file
//   loaders    every load_game_data step (GameData::stages) with its time, result and diagnostics; an exception thrown by
//              a step is caught and reported as a failed step
//   content    counts the session will see (campaigns, planets, object types, factions, AI plans, skirmish maps)
//   campaigns  build_galaxy_map of every campaign: the campaigns with no planets or with skipped locations / routes
//   missing    files the data names that no layer has: entries of the Data\XML\*Files.xml lists, and the
//              Space_ / Land_ / Galactic_Model_Name models of the object types (Data\Art\Models)
//   xml tags   element tags the original does not read, per database: below an object type, tags that neither the
//              original's field tables nor its sub-object elements know (objects/coverage.hpp); below an entry of the
//              other databases the port loads (factions, campaigns, trade routes, hard points, GameConstants, SFX /
//              music / speech events, ...; kXmlDatabases in mod_check.cpp), tags that neither the original's table for
//              that class (objects/field_schema.hpp) nor the port's loader reads. The original ignores them, so does
//              the port; listed per database and tag with counts
//   lua        every Data\Scripts\**\*.lua compiled by the engine's Lua 5.0 VM; each entry script (under a script root
//              other than Library) that compiles also has its main chunk run once in its own VM (require through the VFS,
//              no session: what loading the script runs; one GlobalValue store shared by the runs, as in a session);
//              and a static scan of every script for engine functions / methods the port does not bind yet. A main
//              chunk that fails after reading a GlobalValue key that is unset here but that some script sets
//              (GlobalValue.Set with that literal key, e.g. in a Base_Definitions the session calls at startup) depends
//              on session state: it is listed apart (lua_session_errors), not as a load warning (modc t12). A main chunk
//              that fails on a require of a script no layer has fails in retail too (stock Lua 5.0.2 require raises
//              "could not load package"; docs/re/MODC_LOADER.md section 6): a warning when the data loads the script, an
//              unused-script lead (lua_unused_scripts, info) when it is a mod's own script that nothing names (modc t24)
//   shaders    with a mod mounted, first the .fx prebuild (prebuild_mod_effects, modc t36: per source compiled or why
//              not); then materials::audit_effects over every effect the shader directories reach (mod or base): each
//              materials::ShaderIssue (docs/MODS.md section 4.1) with, for a Data\Art\Shaders\ effect, how many of the
//              object types' models name it in a material, so the report says whether anything draws it (modc t21)
//   submod     a mod whose own data names files no layer has, while an installed mod (Steam workshop item or
//              <game>\Mods folder) outside the stack has most of them: likely a submod meant to be stacked on that
//              parent (modc t16). Found from the files only; a modinfo.json naming the parent's workshop id is noted
// Every finding of the xml tags, missing files and lua sections says whether it comes from a file in a mod folder (in_mod),
// so a mod's own problems can be told from the base game's (base FoC itself names missing cinematic models and unread tags).
// It does not run a session: run-time gaps past a script's main chunk (functions the session calls later) show up in
// alamo_headless --story / --galactic-ai runs.
//   compat     (version 4, mcx0 t06; docs/MOD_COMPAT_DESIGN.md §12.1) what the port does less than retail for this data:
//              honour gaps (tags the data sets that the tag ledger says the port does not act on), calls to bindings
//              with an effect gap, story rewards the port lacks and event types it dispatches partly, perception token
//              paths its galactic resolver does not evaluate, the findings retail shares (retail_equivalent), and the
//              static part of the compatibility grade. None of it counts as a warning.
// format_mod_report_json: the machine-readable summary, schema "alamo.mod_report" version 4 (docs/MODS.md §5.1).
// Used by alamo_mod_check (tools/mod_check_main.cpp) and its tests. Render-free.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "alamo/core/compat_issue.hpp"
#include "alamo/game/asset_audit.hpp"
#include "alamo/game/game_data.hpp"
#include "alamo/vfs/vfs.hpp"

namespace alamo::game {

struct ModCheckOptions {
    bool campaigns = true;  // build every campaign's galaxy map
    bool lua = true;        // compile every script and scan it for unbound engine calls
    bool lua_run = true;    // with lua: run each compiling script's main chunk in its own VM
    bool files = true;      // missing files named by list files and object type model tags
    bool xml_tags = true;   // unknown XML tags of the object types and the other databases
    bool submod = true;     // look for the parent of a mod whose own data names missing files
    bool shaders = true;    // audit every effect (materials::audit_effects; needs the materials module)
    bool compat = true;     // mcx0 t06: honour gaps, story ledger, perception token coverage, grade (report version 4)
    bool assets = true;     // with compat: decode every asset the mod layers serve (mcx2 t01, asset_audit.hpp)
    bool strict = false;    // --strict: an asset that loads only under a salvage rule counts as rejected (mod authors)
    std::string asset_cache_dir;  // --corpus-cache: the asset audit's content-hash cache folder; "" = none
    bool shader_compile = true;  // with shaders: also compile each translated pass with the runtime DXC when it loads
    bool fx_prebuild = true;     // with shaders and a mod mounted: prebuild_mod_effects first (modc t36)
    std::string fx_cache_dir;    // the FxCache the prebuild and the audit use; empty = the default (tests set their own)
    // Steam library folders searched for installed workshop items (submod parents), e.g.
    // vfs::find_steam_libraries(game, vfs::default_steam_roots()). Empty: no workshop item is considered (tests).
    // <game>\Mods\* is always searched.
    std::vector<std::string> workshop_libraries;
};

// A file the data names that no VFS layer has. Grouped by path: `references` counts the naming places, `referenced_by`
// is the first ("Data\XML\GameObjectFiles.xml" for a list entry, "<type> (Space_Model_Name)" for a model).
struct MissingFile {
    std::string path;  // game-relative as looked up, upper case ("DATA\ART\MODELS\X.ALO")
    std::string referenced_by;
    uint32_t references = 0;
    bool in_mod = false;  // at least one naming file is in a mod folder
};

// An element tag that the original does not read: below an object type (TagCoverage::unknown()), or below an entry of
// another database (not in the original's table for that class, not read by the port's loader).
struct UnknownXmlTag {
    std::string database;     // "object types", "factions", "campaigns", "game constants", ... (kXmlDatabases)
    std::string tag;          // first spelling seen
    uint32_t occurrences = 0;
    uint32_t types = 0;       // distinct entries using it (type definitions, factions, campaigns, ...)
    std::string first_file;   // first file using it
    bool in_mod = false;      // at least one file using it is in a mod folder
};

// A script that does not compile, or whose main chunk raises an error.
struct LuaIssue {
    LuaIssue() = default;
    LuaIssue(std::string p, std::string e, bool m) : path(std::move(p)), error(std::move(e)), in_mod(m) {}
    std::string path;     // game-relative, upper case
    std::string error;    // the VM's message
    bool in_mod = false;  // the script file is in a mod folder
    // Session-dependent run errors only (ModCheckReport::lua_session_errors): the GlobalValue key the chunk read while
    // it was unset (the first such key, by byte order, that a script sets) and the first script (path order) setting it.
    std::string global_value;
    std::string set_by;
    // Run errors only (modc t24): the script name of a require that found no file (the innermost one when a required
    // library requires it; "" for other errors), and the first place that names the failing script so the data loads
    // it: "<xml file> (<tag>)" for an XML script tag, "require in <script>", "AI plan folder" (scripts the original
    // enumerates), "base script" (the path is also in a base layer), "" when nothing does.
    std::string missing_require;
    std::string named_by;
    std::string text() const { return path + ": " + error; }
};

// An engine global / wrapper method the scripts call that the port registers as an "unbound" stub (lua/bindings.hpp).
struct UnboundLuaCall {
    std::string name;
    bool method = false;
    uint32_t call_sites = 0;  // over every script (static scan)
    uint32_t scripts = 0;     // scripts with at least one call site
};

// Files of the mod dirs under one Data\<folder> (the first folder below Data; "" for files directly in Data).
struct ModFolderStats {
    std::string folder;      // upper case, e.g. "XML", "SCRIPTS", "ART"
    uint32_t added = 0;      // no lower layer (FoC loose, MEGs, fallbacks) has the path
    uint32_t overrides = 0;  // a lower layer has the path: the mod's file wins
};

// An installed mod outside the stack that has files the checked mod's own data names but no layer has (modc t16).
struct ModParentCandidate {
    std::string id;           // workshop id (the item folder's name) or the <game>\Mods folder name
    bool workshop = false;    // a Steam workshop item (else a <game>\Mods folder)
    std::string path;         // host folder
    std::string arg;          // the argument that stacks it: "STEAMMOD=<id>" or "MODPATH=<host folder>"
    uint32_t provides = 0;    // how many of SubmodHint::missing it has
    bool mentioned = false;   // its workshop id appears in the checked mod's modinfo.json
};
struct SubmodHint {
    static constexpr uint32_t kMinProvided = 2;
    bool likely = false;      // parents.front() has at least kMinProvided and at least half of `missing`
    uint32_t missing = 0;     // missing files named by the mod's own data (MissingFile::in_mod)
    std::vector<ModParentCandidate> parents;  // provides > 0: most first, then mentioned, then by id; at most 5
    std::vector<std::string> stacked_args;    // likely: the check's args followed by parents.front().arg
};

// A materials::ShaderIssue of the effect audit (modc t21) and what draws the effect.
struct ShaderIssueEntry {
    std::string kind;      // materials::shader_issue_kind_name(): source_only, source_shadowed, load_failed, ...
    std::string effect;    // upper-case base name
    std::string path;      // game path of the file concerned
    bool in_mod = false;   // that file is reached through a mod
    std::string detail;    // loader / translator / DXC message ("" when the kind says it all)
    // "models": an effect in Data\Art\Shaders, which models name in their materials; "terrain": one in Shaders\Terrain,
    // which the terrain renderer selects by name; "engine": one in Shaders\Engine or one of the device-init engine
    // effects (materials::engine_effect_load_sequence(): GLOBAL, ALAMOENGINE, the Prim* effects, ...), the engine's own.
    std::string drawn_by;
    uint32_t models = 0;   // drawn_by "models": object-type models whose materials name the effect
    std::string first_model;  // the first of them (model path order)
};
// modc t36: the .fx prebuild that runs when a mod is mounted (prebuild_mod_effects; docs/MODS.md §4.1 "Effect sources").
struct FxPrebuildSource {
    std::string path;      // game path of the .fx the loader takes
    bool ok = false;       // compiled now or found compiled in the FxCache: the loader takes it in place of the .fxo
    bool cached = false;   // the cache already had the result (nothing ran)
    std::string error;     // !ok: the first fxc error line, or why nothing was compiled (fxc.exe not found, unreadable source)
};
struct FxPrebuild {
    bool ran = false;      // a mod is mounted and the build has the materials module (else nothing below is filled)
    std::string fxc;       // materials::fxc_identity of the fxc.exe used, "" when none was found (the t11 fallback stays)
    std::string cache;     // the FxCache folder
    std::vector<FxPrebuildSource> sources;  // by path
    double ms = 0;
};
// Compiles every .fx the loader would take for `d` into the FxCache (fx_cache_dir, empty = $ALAMO_FX_CACHE else
// %LOCALAPPDATA%\alamo\fx) with fxc.exe ($ALAMO_FXC, a test build's fxc\fxc.exe, else the toolchain's), when d has a
// VFS with at least one mod folder; an unchanged source is not compiled again. Without fxc the cached results still
// count and the other sources say they are not compiled (the loader keeps the t11 fallback for them). alamo_game runs
// it before the renderer loads effects, check_mod_data before the shader audit.
FxPrebuild prebuild_mod_effects(const GameData& d, const std::string& fx_cache_dir = {});

struct ShaderCheck {
    bool checked = false;  // the audit ran (built with the materials module and ModCheckOptions::shaders)
    FxPrebuild fx_prebuild;  // modc t36: run before the audit (ModCheckOptions::fx_prebuild)
    size_t effects = 0, mod_effects = 0, loaded = 0, translated_passes = 0, translated_ok = 0;  // materials::EffectAudit
    bool compiled = false;    // the runtime DXC compiled the translated passes
    size_t fx_sources = 0, fx_sources_compiled = 0;  // EffectAudit::sources / sources_compiled (modc t23 FxCache)
    size_t models_read = 0;   // object-type models read for the cross-reference (0 when no issue is a model effect)
    std::vector<ShaderIssueEntry> issues;  // audit order: by effect, then kind
};

// ---- mcx0 t06: report version 4 (docs/MOD_COMPAT_DESIGN.md §12.1). Not counted in errors / warnings: the findings
// below are what the port does less than retail (or what retail refuses too), and feed the compatibility grade.
// first_file / first_script of a finding with in_mod is the first file in a mod folder (a light mod's finding cites the
// mod's file even when base FoC sets the tag earlier in load order); without in_mod, the first file.

// A tag of retail's descriptor tables that the data sets (a value other than empty, TBD or the class default) but the
// port does not act on (tag ledger state unread / stored, objects/tag_ledger.hpp).
struct HonourGap {
    std::string cls;          // ledger class (descriptor segment owner)
    std::string tag;          // ledger spelling
    std::string state;        // "unread" / "stored"
    uint32_t uses = 0;        // elements setting it
    uint32_t entries = 0;     // distinct entries (types, factions, ...) setting it
    std::string first_file;
    bool in_mod = false;      // at least one setting file is in a mod folder
    uint32_t tier = 1;
    uint32_t base_uses = 0;   // ledger: how often base FoC writes it
    std::string spec;         // ledger: the docs/re page naming it
};
// A finding retail shares: it refuses or ignores the same thing, so it never lowers the grade.
struct RetailEquivalentFinding {
    std::string kind;     // "unread_tag", "script_error", "unknown_reward", "dropped_event", "noop_binding", "unknown_call",
                          // "unregistered_token"
    std::string subject;
    uint32_t uses = 0;
    std::string first_file;
    bool in_mod = false;
    std::string detail;   // why retail does the same
};
// Calls to a binding whose effect the port does not give (lua::binding_effect_gap): recorded with no consumer in
// alamo_game, a no-op where retail acts, or partial.
struct BindingGapCall {
    std::string name;      // "Hide_Sub_Object", "LandTaskForceClass.Enable_Attack_Positioning" (methods: every class with a gap)
    std::string effect;    // lua::to_string(BindingEffect)
    std::string detail;    // consumer "" / spec or reason
    uint32_t call_sites = 0, scripts = 0;
    std::string first_script;
    bool in_mod = false;
};
struct StoryUse {
    std::string name;      // reward or event type name, upper case
    uint32_t uses = 0;
    std::string first_file;
    bool in_mod = false;
    std::string note;
};
struct StoryCheck {
    uint32_t files = 0;            // XML files with story events
    uint32_t rewards_used = 0;     // distinct Reward_Type names
    uint32_t events_used = 0;      // distinct Event_Type names
    std::vector<StoryUse> rewards_missing;  // retail rewards the port lacks (sim::StoryRewardOwner::Missing)
    std::vector<StoryUse> events_partial;   // event types the port dispatches with a documented gap
};
struct TokenGap {
    std::string token;          // "Variable_Target.EnemyForce.Foo" (root and path as the equations write them)
    uint32_t equations = 0;     // equations using it
    std::string first_equation;
    std::string first_file;
    bool in_mod = false;
};
struct TokenCoverage {
    bool checked = false;       // AI data loaded and the build has the galactic AI
    uint32_t equations = 0, leaves = 0;  // parsed equations; distinct token paths
    std::vector<TokenGap> missing;       // paths neither the galactic registry nor the tactical leaf tables evaluate
};
struct LimitHit {
    std::string limit;
    uint64_t value = 0, needed = 0;
};
struct CompatGrade {
    std::string grade;                 // "G0" (errors) or "G1" (loads); G2+ need runs (corpus runner, mcx7)
    bool g3_static = false;            // no blocking finding in the mod's own content
    std::vector<std::string> blocking; // "<issue kind> <subject>" of the findings that block G3
    std::vector<std::string> check_errors;  // a version 4 check that threw (not counted as errors; G3 stays open)
};

struct ModCheckReport {
    std::string game;                        // the FoC folder checked
    std::vector<std::string> args;           // the VFS command line (MODPATH= ...)
    bool loaded = false;  // load_game_data succeeded and no step threw
    std::string error;    // why not
    std::string vfs_layers;                  // Vfs::describe()
    std::vector<std::string> vfs_warnings;   // Vfs::warnings()
    std::vector<std::string> mod_dirs;       // effective mod search list (host folders)
    std::vector<ModFolderStats> mod_folders; // sorted by folder
    std::vector<GameData::LoadStage> stages;
    std::vector<std::string> diagnostics;    // GameData::diagnostics (stages index into it)
    struct Counts {
        size_t campaigns = 0, planets = 0, trade_routes = 0, object_types = 0, factions = 0;
        size_t ai_plans = 0, tactical_space_plans = 0, tactical_land_plans = 0;
        size_t official_maps = 0, custom_maps = 0;
        bool ai = false;  // AI data loaded
    } counts;
    size_t campaigns_checked = 0;
    // No Locations / no planet resolves. Retail lists such a campaign in the Galactic Conquest menu (a header or notice
    // row; docs/re/MODC_LOADER.md section 4), so it is a warning: a menu entry that cannot be started. An error only
    // when no checked campaign has planets (no_startable_campaign()).
    std::vector<std::string> campaigns_without_planets;
    std::vector<std::string> campaign_warnings;  // build_galaxy_map diagnostics, one line per message with its campaigns
    size_t campaign_warning_count = 0;           // the diagnostics behind campaign_warnings (per campaign)
    std::vector<MissingFile> missing_files;      // sorted by path
    std::vector<UnknownXmlTag> unknown_xml_tags;  // most occurrences first, then by database and tag
    size_t lua_scripts = 0;
    std::vector<LuaIssue> lua_errors;            // does not compile
    size_t lua_run = 0;                          // main chunks run (entry scripts that compile)
    std::vector<LuaIssue> lua_run_errors;        // compiles, its main chunk raises an error (not session-dependent)
    std::vector<LuaIssue> lua_session_errors;    // the same after reading an unset GlobalValue that a script sets (info)
    std::vector<LuaIssue> lua_unused_scripts;    // fails on a missing require, a mod's own script nothing names (info)
    std::vector<UnboundLuaCall> unbound_lua_calls;  // most call sites first, then by name
    ShaderCheck shaders;                         // effect audit issues (warnings)
    SubmodHint submod;                           // empty unless the mod's own data names missing files
    // mcx3 (docs/MOD_COMPAT_DESIGN.md 5): the mounted mods (GameData::mods, mod_dirs order) and, when `args` came from
    // resolve_mod_stack (alamo_mod_check --resolve), that resolution (resolved = true): its errors count as errors, its
    // warnings as warnings.
    std::vector<vfs::ModDescriptor> mods;
    std::vector<vfs::Vfs::MegIssue> meg_issues;  // mcx3 t04: malformed MEG tables (also in vfs_warnings)
    bool resolved = false;
    vfs::ResolvedStack stack;
    bool base_eaw = false;         // D1: a mounted mod is tagged EAW and not FOC: refused before loading (error, G0)
    // 5.4 heuristic [port], a warning only: "<key>: ..." when a mod's own GameObjectFiles.xml names none of the files
    // FoC's list has and EaW's lacks (both lists read from the install; empty when either is missing).
    std::string base_eaw_suspect;
    std::vector<std::string> check_errors;       // a check above that threw: "<check>: exception: <what>"
    // mcx0 t06 (version 4). `issues` holds every finding below as core CompatIssue records; the lists are its views for
    // the report. Layer: 0 = any mod folder (the report does not tell stacked mods apart yet: mcx3 provenance),
    // kBaseLayer = base game.
    IssueSink issues;
    std::vector<HonourGap> honour_gaps;            // most uses first, then class and tag
    std::vector<RetailEquivalentFinding> retail_equivalent;  // by kind, then most uses
    uint32_t binding_calls = 0;                    // distinct engine bindings the scripts call
    std::vector<BindingGapCall> binding_gaps;      // most call sites first
    StoryCheck story;
    TokenCoverage token_coverage;
    std::vector<LimitHit> limits;                  // filled by the limit checks (mcx5); empty until then
    AssetAudit assets;                             // mcx2 t01: the `assets` section (ran = false when not checked)
    CompatGrade grade;

    // Errors: the load failed or a step threw / failed, no campaign has planets (one error), a check threw. Warnings:
    // campaigns without planets when another one has planets, loader and
    // VFS diagnostics, skipped campaign rows, missing files, unknown XML tags (one per database and tag), scripts that
    // do not compile or whose main chunk errors, unbound engine calls (one per name), shader issues. Not counted (info):
    // session-dependent main-chunk errors, unused-script leads and the submod hint. The original loads with all of these; its Lua
    // 5.0 rejects the same scripts, and mods ship unused ones, e.g. Thrawn's Revenge's Lua 5.1 unit-test specs.
    size_t error_count() const;
    bool no_startable_campaign() const { return campaigns_checked > 0 && campaigns_without_planets.size() == campaigns_checked; }
    size_t warning_count() const;
};

ModCheckReport check_mod_data(const std::string& root, const std::vector<std::string>& args, const ModCheckOptions& options = {});
// mcx3 t02: stack resolution with the data hint (design 5.2) for alamo_mod_check --resolve and the launcher. The
// registry holds the installed mods (options.workshop_libraries, <root>\Mods); declared modinfo dependencies come
// first, and a mod that declares none gets the likely parent of the submod check (check_mod_data on the stack so far
// with files and submod only, the other checks off). The game itself never resolves: an explicit command line is
// mounted as written.
vfs::ResolvedStack resolve_mod_stack(const std::string& root, const std::vector<std::string>& picked, const ModCheckOptions& options);
// Human-readable report (sections in the order of the file comment). verbose: every diagnostic, else the first 20 of each
// step and campaign / Lua list.
std::string format_mod_report(const ModCheckReport& report, bool verbose = false);
// The machine-readable summary (docs/MODS.md §5.1): one JSON object, every list complete, keys in a fixed order.
std::string format_mod_report_json(const ModCheckReport& report);

}  // namespace alamo::game
