#include "alamo/game/mod_check.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include "alamo/core/hash.hpp"
#include "alamo/core/host_fs.hpp"
#include "alamo/data/xml.hpp"
#include "alamo/data/xml_list.hpp"
#include "alamo/objects/coverage.hpp"
#include "alamo/objects/field_schema.hpp"
#include "alamo/objects/tag_ledger.hpp"
#include "alamo/objects/type_fields.hpp"
#include "alamo/sim/story_rewards.hpp"
#include "alamo/game/mod_picker.hpp"  // mod_folders, workshop_content_dir (cu9 t03)
#include "alamo/vfs/vfs.hpp"
#include "json_text.hpp"  // the JSON string escaper, shared with benchmark.cpp
#ifdef ALAMO_GAME_HAVE_MATERIALS
#include "alamo/assets/model.hpp"
#include "alamo/materials/effect_audit.hpp"
#include "alamo/materials/effect_loader.hpp"
#include "alamo/shaderxlat/dxc.hpp"
#include "fx_compile.hpp"  // materials/tools: the .fx prebuild (alamo_fxbuild, modc t36)
#endif
#ifdef ALAMO_GAME_HAVE_GALACTIC_AI
#include "alamo/ai/galactic_ai_tokens.hpp"
#include "alamo/ai/perception_equations.hpp"
#include "alamo/ai/tactical/tactical_perception.hpp"
#endif
#ifdef ALAMO_GAME_HAVE_LUA_VM
#include <lua.h>  // Lua 5.0 is built as C++ (engine/lua/CMakeLists.txt): no extern "C"

#include "alamo/lua/bindings.hpp"
#include "alamo/lua/lua_vm.hpp"
#include "alamo/lua/script_loader.hpp"
#include "alamo/lua/script_remaining.hpp"
#endif

namespace alamo::game {

namespace {

// "DATA\XML\UNITS.XML" -> "XML"; "DATA\MEGAFILES.XML" -> "".
std::string data_folder(std::string_view path) {
    constexpr std::string_view kData = "DATA\\";
    if (path.substr(0, kData.size()) != kData) return {};
    path.remove_prefix(kData.size());
    const size_t sep = path.find('\\');
    return sep == std::string_view::npos ? std::string() : std::string(path.substr(0, sep));
}

void list_mod_files(const vfs::Vfs& v, ModCheckReport& r) {
    constexpr uint32_t kLower = vfs::Vfs::kLayerBase | vfs::Vfs::kLayerMeg | vfs::Vfs::kLayerFallback;
    const std::vector<std::string> mod = v.list("Data\\**", vfs::Vfs::kLayerMod);
    const std::vector<std::string> lower = v.list("Data\\**", kLower);  // both sorted by byte order
    std::map<std::string, ModFolderStats> folders;
    for (const std::string& p : mod) {
        ModFolderStats& f = folders[data_folder(p)];
        if (std::binary_search(lower.begin(), lower.end(), p)) ++f.overrides;
        else ++f.added;
    }
    for (auto& [name, f] : folders) {
        f.folder = name;
        r.mod_folders.push_back(f);
    }
}

void count_content(const GameData& d, ModCheckReport& r) {
    r.counts.campaigns = d.db.campaigns().size();
    r.counts.planets = d.db.planets().size();
    r.counts.trade_routes = d.db.trade_routes().size();
    r.counts.object_types = d.types.size();
    r.counts.factions = d.rules.factions.size();
#ifdef ALAMO_GAME_HAVE_GALACTIC_AI
    if (d.ai) {
        r.counts.ai = true;
        r.counts.ai_plans = d.ai->plan_files.size();
#ifdef ALAMO_SIM_HAVE_TACTICAL_AI
        if (d.ai->tactical) {
            r.counts.tactical_space_plans = d.ai->tactical->space_plans.size();
            r.counts.tactical_land_plans = d.ai->tactical->land_plans.size();
        }
#endif
    }
#endif
    if (d.vfs) {
        const SkirmishMapCatalog cat = build_skirmish_map_catalog(*d.vfs);
        const std::vector<SkirmishMapEntry> rows = skirmish_map_rows(cat, false);
        r.counts.official_maps = rows.size();
        r.counts.custom_maps = skirmish_map_rows(cat, true).size();
        for (const SkirmishMapEntry& e : rows) {
            const auto res = d.vfs->resolve(e.path);
            r.skirmish_maps.push_back({e.map, e.kind == 1, e.players, res && res->layer == vfs::Vfs::Resolution::Mod});
        }
    }
}

// Mods ship one campaign per playable faction, so the same skipped route shows up in many: one line per message.
void check_campaigns(const GameData& d, ModCheckReport& r) {
    std::map<std::string, std::vector<std::string>> by_message;  // message -> campaigns, in database order
    for (const data::CampaignDef& c : d.db.campaigns()) {
        std::vector<std::string> diag;
        const auto map = data::build_galaxy_map(d.db, c.name, &diag);
        ++r.campaigns_checked;
        if (map.planets.size() == 0) r.campaigns_without_planets.push_back(c.name);
        else r.campaigns_startable.push_back(c.name);
        const std::string prefix = c.name + ": ";
        for (std::string_view msg : diag) {
            if (msg.starts_with(prefix)) msg.remove_prefix(prefix.size());
            by_message[std::string(msg)].push_back(c.name);
        }
    }
    for (const auto& [msg, camps] : by_message) {
        std::string line = msg + " (" + std::to_string(camps.size()) + (camps.size() == 1 ? " campaign: " : " campaigns: ");
        constexpr size_t kNamed = 3;
        for (size_t i = 0; i < camps.size() && i < kNamed; ++i) line += (i ? ", " : "") + camps[i];
        if (camps.size() > kNamed) line += ", ...";
        r.campaign_warnings.push_back(line + ")");
        r.campaign_warning_count += camps.size();
    }
}

using MissingMap = std::map<std::string, MissingFile>;  // by path: sorted, deterministic

// True when `path` resolves to a file of a mod folder (the VFS mod layer).
bool in_mod_layer(const vfs::Vfs& v, std::string_view path) {
    const auto res = v.resolve(path);
    return res && res->layer == vfs::Vfs::Resolution::Mod;
}

void note_missing(MissingMap& m, std::string path, std::string_view by, bool in_mod) {
    MissingFile& f = m[path];
    if (f.references++ == 0) {
        f.path = std::move(path);
        f.referenced_by = std::string(by);
    }
    f.in_mod = f.in_mod || in_mod;
}

// Entries of every Data\XML\*Files.xml list (GameObjectFiles, FactionFiles, CampaignFiles, SFXEventFiles, ...): each
// <File> names Data\XML\<entry> (the convention of every list the loaders read: game_database.cpp read_list,
// object_types.cpp). A list that fails to parse is reported by its loader, not here.
void check_list_files(const GameData& d, MissingMap& m) {
    std::vector<uint8_t> bytes;
    for (const std::string& list : d.vfs->list("Data\\XML\\*Files.xml")) {
        if (!d.files->read(list, bytes)) continue;
        const bool in_mod = in_mod_layer(*d.vfs, list);
        const data::XmlDocument doc = data::XmlDocument::parse(bytes, list);
        for (const data::ListedXmlEntry& en : data::listed_xml_entries(doc)) {  // cu5 t02 (none when !doc.ok())
            std::string path = normalize_name(en.path);
            if (!d.files->exists(path)) note_missing(m, std::move(path), list, in_mod);
        }
    }
}

// The model of each object type (first definitions only: a later duplicate never loads). A name without an extension
// is also tried with .ALO (PROVISIONAL: how the original completes such names is not traced; this avoids a false report).
void check_models(const GameData& d, MissingMap& m) {
    static constexpr std::string_view kTags[] = {"Space_Model_Name", "Land_Model_Name", "Galactic_Model_Name"};
    const objects::ObjectTypeDatabase& db = d.raw;
    const objects::TypeColumns& t = db.types();
    const objects::FieldColumns& f = db.fields();
    std::set<std::string> present;  // paths already found (types share models)
    for (objects::TypeIndex i = 0; i < t.size(); ++i) {
        if (t.first_definition[i] != i) continue;
        for (objects::FieldIndex fi = t.field_begin[i]; fi < t.field_end[i]; ++fi) {
            if (f.depth[fi] != 1 || f.text[fi].empty()) continue;
            const std::string_view tag = db.tag_name(fi);
            const auto* hit = std::find_if(std::begin(kTags), std::end(kTags), [&](std::string_view k) { return iequals(k, tag); });
            if (hit == std::end(kTags)) continue;
            std::string path = normalize_name(std::string("Data\\Art\\Models\\") + std::string(f.text[fi]));
            if (present.count(path)) continue;
            bool found = d.files->exists(path);
            if (!found && path.find('.', path.rfind('\\')) == std::string::npos) found = d.files->exists(path + ".ALO");
            if (found) {
                present.insert(std::move(path));
                continue;
            }
            note_missing(m, std::move(path), std::string(t.name[i]) + " (" + std::string(*hit) + ")", in_mod_layer(*d.vfs, db.file_of(i)));
        }
    }
}

void check_files(const GameData& d, ModCheckReport& r) {
    if (!d.vfs) return;
    MissingMap m;
    check_list_files(d, m);
    check_models(d, m);
    for (auto& [path, f] : m) r.missing_files.push_back(std::move(f));
}

// modc t15: the XML databases besides the object types that the port loads. Per database: where its entries are (the
// <File> entries of a Data\XML list, as the loader reads them, or one Data\XML file), the entry element below the root
// (or the root itself), and which child tags count as read: the rows of the original's descriptor table for the class
// (docs/re/xml_field_tables.md through objects::field_schema(), by segment owner) plus the tags the port's loader reads
// that no row lists (cited per database). Only direct children of an entry are checked, except for a walked file.
// modc t25 (docs/re/xml_field_tables.md "modc t25" sections): CommandBarComponents.xml, whose entries are every child of
// the root (the element name is the component name); RadarMap.xml, which the original walks as a whole (no element path
// checked, a known tag read at any depth, an element without text only a container).
struct XmlDatabase {
    std::string_view name;
    std::string_view list;     // Data\XML\<list> holds the entry files; "" = `file`
    std::string_view file;     // Data\XML\<file> holds the entries
    std::string_view element;  // entry element below the root; "" = the root element is the one entry; "*" = every child
    std::span<const std::string_view> owners;     // field_schema() segment owners
    std::span<const std::string_view> port_tags;  // read by the port's loader, no table row
    bool walk = false;  // every element with text at any depth below the root is checked; entries = its parents
};

constexpr std::string_view kFaction[] = {"Faction"};
constexpr std::string_view kCampaign[] = {"Campaign"};
constexpr std::string_view kTradeRoute[] = {"TradeRoute"};
constexpr std::string_view kHardPoint[] = {"HardPoint"};
constexpr std::string_view kHardPointPort[] = {"Type"};  // render/hardpoints.cpp (hard point type)
constexpr std::string_view kGameConstants[] = {"GameConstants"};
constexpr std::string_view kGameConstantsPort[] = {"Ships_Per_Stack"};  // apps/game app_screens.cpp (fleet stacks)
constexpr std::string_view kSfxEvent[] = {"SFXEvent"};
constexpr std::string_view kMusicEvent[] = {"MusicEvent"};
// SpeechEvent has no table in the doc: what audio_db.cpp speech_fields() reads.
constexpr std::string_view kSpeechEventPort[] = {"Files", "Text_ID", "Volume_Percent", "Overlap_Test"};
constexpr std::string_view kAudio[] = {"Audio"};
constexpr std::string_view kAudioPort[] = {"Ambient_Map_Sounds"};  // audio/ambience.hpp (map ambient beds)
constexpr std::string_view kTacticalCamera[] = {"TacticalCamera"};
constexpr std::string_view kTacticalCameraPort[] = {"MinHeightAboveTerrain"};  // ui/camera.cpp
constexpr std::string_view kMovie[] = {"Movie"};
constexpr std::string_view kDifficulty[] = {"Difficulty_Adjustment"};
constexpr std::string_view kLightning[] = {"LightningEffect"};
constexpr std::string_view kPrioritySet[] = {"Priority_Set"};
constexpr std::string_view kMousePointer[] = {"MousePointer"};
constexpr std::string_view kCommandBar[] = {"CommandBarComponent"};
constexpr std::string_view kRadarMap[] = {"RadarMapSettings", "Radar_Map_Event", "Passability_Color_Settings"};
// The walker's own names (xml_field_tables.md "Elements read outside the descriptor tables"): attributes select a block.
constexpr std::string_view kRadarMapSpecial[] = {"Passability_Color_Settings", "Color", "Radar_Map_Event"};

constexpr XmlDatabase kXmlDatabases[] = {
    {"factions", "FactionFiles.xml", "", "Faction", kFaction, {}},
    {"campaigns", "CampaignFiles.xml", "", "Campaign", kCampaign, {}},
    {"trade routes", "TradeRouteFiles.xml", "", "TradeRoute", kTradeRoute, {}},
    {"hard points", "HardPointDataFiles.xml", "", "HardPoint", kHardPoint, kHardPointPort},
    {"game constants", "", "GameConstants.xml", "", kGameConstants, kGameConstantsPort},
    {"sfx events", "SFXEventFiles.xml", "", "SFXEvent", kSfxEvent, {}},
    {"music events", "", "MusicEvents.xml", "MusicEvent", kMusicEvent, {}},
    {"speech events", "", "SpeechEvents.xml", "SpeechEvent", {}, kSpeechEventPort},
    {"audio", "", "Audio.xml", "", kAudio, kAudioPort},
    {"tactical cameras", "", "TacticalCameras.xml", "TacticalCamera", kTacticalCamera, kTacticalCameraPort},
    {"movies", "", "Movies.xml", "Movie", kMovie, {}},
    {"difficulty adjustments", "", "DifficultyAdjustments.xml", "Difficulty_Adjustment", kDifficulty, {}},
    {"lightning effects", "", "LightningEffectTypes.xml", "LightningEffect", kLightning, {}},
    {"targeting priority sets", "TargetingPrioritySetFiles.xml", "", "Priority_Set", kPrioritySet, {}},
    {"mouse pointers", "", "MousePointers.xml", "MousePointer", kMousePointer, {}},  // as app game_main.cpp reads it
    {"command bar components", "CommandBarComponentFiles.xml", "", "*", kCommandBar, {}},
    {"radar map", "", "RadarMap.xml", "", kRadarMap, kRadarMapSpecial, true},
};

// Every descendant of `n` with non-empty text, with its parent (walk databases).
void walk_text_elements(data::XmlNode n, std::vector<std::pair<data::XmlNode, data::XmlNode>>& out) {
    for (data::XmlNode c : n.children()) {
        if (!c.text_trimmed().empty()) out.emplace_back(n, c);
        walk_text_elements(c, out);
    }
}

// The Data\XML files of a database in load order, each once.
std::vector<std::string> database_files(const GameData& d, const XmlDatabase& db) {
    std::vector<std::string> out;
    if (db.list.empty()) {
        out.push_back("Data\\XML\\" + std::string(db.file));
        return out;
    }
    std::vector<uint8_t> bytes;
    if (!d.files->read("Data\\XML\\" + std::string(db.list), bytes)) return out;
    const data::XmlDocument doc = data::XmlDocument::parse(bytes, std::string(db.list));
    std::set<std::string> seen;
    for (data::ListedXmlEntry& en : data::listed_xml_entries(doc))  // cu5 t02 (none when !doc.ok())
        if (seen.insert(normalize_name(en.path)).second) out.push_back(std::move(en.path));
    return out;
}

void check_database_tags(const GameData& d, const XmlDatabase& db, ModCheckReport& r) {
    std::set<std::string, std::less<>> known;
    for (const objects::FieldSchema& row : objects::field_schema())
        for (const std::string_view o : db.owners)
            if (iequals(row.owner, o)) known.insert(ascii_lower(row.tag));
    for (const std::string_view t : db.port_tags) known.insert(ascii_lower(t));
    struct Seen {
        UnknownXmlTag u;
        std::set<std::string> entries;
    };
    std::map<std::string, Seen> unknown;  // by lower-case tag: sorted, deterministic
    std::vector<uint8_t> bytes;
    for (const std::string& path : database_files(d, db)) {
        if (!d.files->read(path, bytes)) continue;  // a missing list entry is reported by the files check
        const data::XmlDocument doc = data::XmlDocument::parse(bytes, path);
        if (!doc.ok()) continue;  // its loader reports the parse error
        const bool in_mod = d.vfs && in_mod_layer(*d.vfs, path);
        std::vector<std::pair<data::XmlNode, data::XmlNode>> checked;  // (entry, child)
        if (db.walk) {
            walk_text_elements(doc.root(), checked);
        } else {
            std::vector<data::XmlNode> entries;
            if (db.element.empty()) entries.push_back(doc.root());
            else
                for (data::XmlNode e : doc.root().children())
                    if (db.element == "*" || iequals(e.name(), db.element)) entries.push_back(e);
            for (const data::XmlNode e : entries)
                for (data::XmlNode c : e.children()) checked.emplace_back(e, c);
        }
        {
            for (const auto& [e, c] : checked) {
                const std::string entry = path + "#" + std::to_string(e.index());
                std::string key = ascii_lower(c.name());
                if (known.count(key)) continue;
                Seen& s = unknown[std::move(key)];
                if (s.u.occurrences++ == 0) {
                    s.u.database = std::string(db.name);
                    s.u.tag = std::string(c.name());
                    s.u.first_file = path;
                }
                s.u.in_mod = s.u.in_mod || in_mod;
                s.entries.insert(entry);
            }
        }
    }
    for (auto& [key, s] : unknown) {
        s.u.types = static_cast<uint32_t>(s.entries.size());
        r.unknown_xml_tags.push_back(std::move(s.u));
    }
}

// modc t37: tags with a field_schema() row that is not one of the hand-verified tables of another class, lower case.
// objects::find_field_schema falls back to any owner's row (the loader keeps that), so the report checks the owner here:
// a tag whose only rows are in the CommandBarComponent table (0xa38dd0: Hidden, Toggle, ...) or the RadarMapClass table
// (0xa39c50) is not read on an object type; each owner's Parse_Database_Entry reads only its own table
// (xml_field_tables.md, modc t25 sections, [H]). The other segment labels of the doc are its generator's guesses (see
// gen_field_schema.py OWNER_FIXES: "MousePointer" runs hold Land_Upgrade_Type, ...), so they do not make a tag unread.
constexpr std::string_view kVerifiedForeignOwners[] = {"CommandBarComponent", "RadarMapSettings", "Radar_Map_Event",
                                                       "Passability_Color_Settings"};

std::set<std::string, std::less<>> object_type_tags() {
    std::set<std::string, std::less<>> out;
    for (const objects::FieldSchema& row : objects::field_schema()) {
        bool foreign = false;
        for (const std::string_view o : kVerifiedForeignOwners) foreign = foreign || iequals(row.owner, o);
        if (!foreign) out.insert(ascii_lower(row.tag));
    }
    return out;
}

void check_xml_tags(const GameData& d, ModCheckReport& r) {
    const objects::CoverageReport cov = objects::build_coverage(d.raw, 0);
    const auto& files = d.raw.files();
    const std::set<std::string, std::less<>> object_tags = object_type_tags();
    for (const objects::TagCoverage& t : cov.tags) {
        if (t.occurrences == 0) continue;
        if (!t.unknown() && (t.sub_object || object_tags.count(ascii_lower(t.name)) != 0)) continue;
        UnknownXmlTag u;
        u.database = "object types";
        u.tag = std::string(t.name);
        u.occurrences = t.occurrences;
        u.types = t.types;
        if (!t.files.empty() && t.files.front() < files.size()) u.first_file = files[t.files.front()].path;
        for (const uint32_t fi : t.files)
            if (fi < files.size() && d.vfs && in_mod_layer(*d.vfs, files[fi].path)) {
                u.in_mod = true;
                break;
            }
        r.unknown_xml_tags.push_back(std::move(u));
    }
    if (d.files)
        for (const XmlDatabase& db : kXmlDatabases) check_database_tags(d, db, r);
    std::stable_sort(r.unknown_xml_tags.begin(), r.unknown_xml_tags.end(), [](const UnknownXmlTag& a, const UnknownXmlTag& b) {
        if (a.occurrences != b.occurrences) return a.occurrences > b.occurrences;
        if (a.database != b.database) return a.database < b.database;
        return a.tag < b.tag;
    });
}

// ---- modc t21: shaders ----
// "DATA\ART\SHADERS\TERRAIN\X.FX" -> "terrain"; ...\ENGINE\... -> "engine"; else "models".
[[maybe_unused]] std::string_view shader_drawn_by(std::string_view path) {
    const std::string p = normalize_name(path);
    if (p.find("\\SHADERS\\TERRAIN\\") != std::string::npos) return "terrain";
    if (p.find("\\SHADERS\\ENGINE\\") != std::string::npos) return "engine";
    return "models";
}

// "MeshGloss.fx" / "Data\Art\Shaders\MESHGLOSS.FXO" -> "MESHGLOSS" (the form ShaderIssue::effect has).
[[maybe_unused]] std::string effect_base(std::string_view name) {
    const size_t slash = name.find_last_of("\\/");
    if (slash != std::string_view::npos) name.remove_prefix(slash + 1);
    const size_t dot = name.rfind('.');
    if (dot != std::string_view::npos) name = name.substr(0, dot);
    return normalize_name(name);
}

#ifdef ALAMO_GAME_HAVE_MATERIALS
// The models the object types name (first definitions, every tag ending in Model_Name), found through the files, sorted.
std::vector<std::string> object_type_models(const GameData& d) {
    const objects::ObjectTypeDatabase& db = d.raw;
    const objects::TypeColumns& t = db.types();
    const objects::FieldColumns& f = db.fields();
    std::set<std::string> paths;
    constexpr std::string_view kSuffix = "MODEL_NAME";
    for (objects::TypeIndex i = 0; i < t.size(); ++i) {
        if (t.first_definition[i] != i) continue;
        for (objects::FieldIndex fi = t.field_begin[i]; fi < t.field_end[i]; ++fi) {
            if (f.depth[fi] != 1 || f.text[fi].empty()) continue;
            const std::string tag = normalize_name(db.tag_name(fi));
            if (tag.size() < kSuffix.size() || tag.compare(tag.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) continue;
            std::string path = normalize_name(std::string("Data\\Art\\Models\\") + std::string(f.text[fi]));
            if (path.find('.', path.rfind('\\')) == std::string::npos) path += ".ALO";
            if (path.size() < 4 || path.compare(path.size() - 4, 4, ".ALO") != 0) continue;
            if (d.files->exists(path)) paths.insert(std::move(path));
        }
    }
    return {paths.begin(), paths.end()};
}
#endif

void check_shaders(const GameData& d, ModCheckReport& r, bool compile, const std::string& fx_cache_dir) {
#ifdef ALAMO_GAME_HAVE_MATERIALS
    if (!d.vfs) return;
    ShaderCheck& sc = r.shaders;
    shaderxlat::RuntimeDxc dxc;
    const bool have_dxc = compile && dxc.load();
    // As alamo_effect_audit (materials/tools/effect_audit_tool.cpp): the library gets the D.1 preload list, the audit
    // every effect of the shader directories including Shaders\Engine.
    materials::LoadOptions lo;
    if (!fx_cache_dir.empty()) lo.fx_cache_dir = path_from_utf8(fx_cache_dir);
    materials::EffectLibrary lib(*d.files, lo);
    lib.set_shader_files(materials::shader_files_from_vfs(*d.vfs));
    const std::vector<materials::ShaderFileEntry> files = materials::shader_files_from_vfs(*d.vfs, true);
    const materials::EffectAudit a = materials::audit_effects(lib, files, have_dxc ? &dxc : nullptr, nullptr);
    sc.checked = true;
    sc.effects = a.effects;
    sc.mod_effects = a.mod_effects;
    sc.loaded = a.loaded;
    sc.translated_passes = a.translated_passes;
    sc.translated_ok = a.translated_ok;
    sc.compiled = a.compiled;
    sc.fx_sources = a.sources;
    sc.fx_sources_compiled = a.sources_compiled;
    std::map<std::string, size_t> model_effects;  // effect base name -> index of its first issue with drawn_by "models"
    for (const materials::ShaderIssue& i : a.issues) {
        ShaderIssueEntry e;
        e.kind = materials::shader_issue_kind_name(i.kind);
        e.effect = i.effect;
        e.path = i.path;
        e.in_mod = i.in_mod;
        e.detail = i.detail;
        e.drawn_by = std::string(shader_drawn_by(i.path));
        // The device-init engine effects (GLOBAL, ALAMOENGINE, the Prim* sprites, ...) are the engine's, wherever they lie.
        const std::vector<std::string>& engine = materials::engine_effect_load_sequence();
        if (std::find(engine.begin(), engine.end(), e.effect) != engine.end()) e.drawn_by = "engine";
        if (e.drawn_by == "models") model_effects.emplace(e.effect, sc.issues.size());
        sc.issues.push_back(std::move(e));
    }
    if (model_effects.empty()) return;  // vanilla: no issue, no model read
    // Which models name the issue effects: every material of every object-type model (render submeshes and the
    // others alike: the effect is loaded for them either way).
    std::map<std::string, std::pair<uint32_t, std::string>> users;  // effect -> (models, first model)
    for (const std::string& path : object_type_models(d)) {
        assets::ModelAsset m;
        if (!assets::load_model(*d.files, path, m)) continue;  // a broken model is not this check's finding
        ++sc.models_read;
        std::set<std::string> named;
        for (const assets::Submesh& sub : m.submeshes) {
            std::string base = effect_base(sub.material.effect);
            if (model_effects.count(base)) named.insert(std::move(base));
        }
        for (const std::string& eff : named) {
            auto& u = users[eff];
            if (u.first++ == 0) u.second = path;
        }
    }
    for (ShaderIssueEntry& e : sc.issues) {
        if (e.drawn_by != "models") continue;
        const auto it = users.find(e.effect);
        if (it == users.end()) continue;
        e.models = it->second.first;
        e.first_model = it->second.second;
    }
#else
    (void)d;
    (void)r;
    (void)compile;
    (void)fx_cache_dir;
#endif
}

// ---- modc t16: submods ----
namespace fs = std::filesystem;

// Data\XML files the mod's own list files name that no layer has (MissingFile::in_mod; models are not counted: a full
// mod misses art too, only a mod whose own database lists another mod's XML files is a submod).
std::vector<std::string> missing_list_entries(const ModCheckReport& r) {
    std::vector<std::string> out;
    for (const MissingFile& f : r.missing_files)
        if (f.in_mod && f.path.starts_with("DATA\\XML\\")) out.push_back(f.path);
    return out;
}

bool same_folder(const fs::path& a, const std::string& b) {
    std::error_code ec;
    return fs::equivalent(a, fs::path(b), ec) && !ec;
}

// How many of `paths` the candidate's own files have: a Vfs with the candidate as its only tree (its loose Data, the
// MEGs its MegaFiles.xml / Patch*.meg name; no EaW, no FoC), so every hit is the candidate's.
uint32_t provided_by(const fs::path& candidate, const std::vector<std::string>& paths) {
    vfs::VfsConfig cfg;
    cfg.foc_root = candidate.string();
    cfg.eaw_root = std::string();
    std::string err;
    const std::unique_ptr<vfs::Vfs> v = vfs::Vfs::create(cfg, &err);
    if (!v) return 0;
    uint32_t n = 0;
    for (const std::string& p : paths) n += v->exists(p) ? 1u : 0u;
    return n;
}

// Every digit run of the mod folders' modinfo.json files (workshop ids a submod names, e.g. "Requires ... 1125571106").
std::set<std::string> modinfo_numbers(const std::vector<std::string>& mod_dirs) {
    std::set<std::string> out;
    for (const std::string& dir : mod_dirs) {
        std::ifstream in(fs::path(dir) / "modinfo.json", std::ios::binary);
        if (!in) continue;
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        for (size_t i = 0; i < text.size();) {
            if (text[i] < '0' || text[i] > '9') {
                ++i;
                continue;
            }
            const size_t b = i;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
            out.insert(text.substr(b, i - b));
        }
    }
    return out;
}

void check_submod(const ModCheckReport& base, const ModCheckOptions& o, SubmodHint& h) {
    if (base.mod_dirs.empty()) return;
    const std::vector<std::string> missing = missing_list_entries(base);
    h.missing = static_cast<uint32_t>(missing.size());
    if (missing.empty()) return;
    const std::set<std::string> mentioned = modinfo_numbers(base.mod_dirs);
    std::vector<ModParentCandidate> cands;
    const auto consider = [&](const fs::path& dir, bool workshop) {
        for (const std::string& m : base.mod_dirs)
            if (same_folder(dir, m)) return;  // already in the stack
        ModParentCandidate c;
        c.id = dir.filename().string();
        c.workshop = workshop;
        c.path = dir.string();
        c.arg = workshop ? "STEAMMOD=" + c.id : "MODPATH=Mods\\" + c.id;
        c.mentioned = workshop && mentioned.count(c.id) != 0;
        c.provides = provided_by(dir, missing);
        if (c.provides > 0) cands.push_back(std::move(c));
    };
    std::set<std::string> seen_items;  // a workshop id found in an earlier library wins (find_workshop_item order)
    for (const std::string& lib : o.workshop_libraries)
        for (const fs::path& item : mod_folders(workshop_content_dir(lib), true))  // mod_picker.hpp (cu9 t03)
            if (seen_items.insert(item.filename().string()).second) consider(item, true);
    for (const fs::path& local : mod_folders(fs::path(base.game) / "Mods", false)) consider(local, false);
    std::sort(cands.begin(), cands.end(), [](const ModParentCandidate& a, const ModParentCandidate& b) {
        if (a.provides != b.provides) return a.provides > b.provides;
        if (a.mentioned != b.mentioned) return a.mentioned;
        if (a.workshop != b.workshop) return a.workshop;
        return a.id < b.id;
    });
    constexpr size_t kListed = 5;
    if (cands.size() > kListed) cands.resize(kListed);
    h.parents = std::move(cands);
    if (h.parents.empty()) return;
    const ModParentCandidate& top = h.parents.front();
    h.likely = top.provides >= SubmodHint::kMinProvided && 2 * top.provides >= h.missing;
    if (h.likely) {
        h.stacked_args = base.args;
        h.stacked_args.push_back(top.arg);
    }
}

// ---- mcx3 t03: base-EaW heuristic (design 5.4) ----

// The entries of `list`'s copies in the base layers: FoC's (FoC loose or a FoC MEG) and EaW's (an EaW MEG or the EaW
// GameData fallback). A copy counts once per side, the highest one; false when either side has none.
bool base_list_entries(const vfs::Vfs& v, std::string_view list, std::set<std::string>& foc, std::set<std::string>& eaw) {
    bool have_foc = false, have_eaw = false;
    std::vector<uint8_t> bytes;
    vfs::Vfs::Resolution where{};
    for (size_t n = 0; v.read_nth(list, n, bytes, &where); ++n) {
        bool is_foc = where.layer == vfs::Vfs::Resolution::Base;
        bool is_eaw = where.layer == vfs::Vfs::Resolution::Fallback;
        if (where.layer == vfs::Vfs::Resolution::Meg && where.layer_index >= 0 &&
            static_cast<size_t>(where.layer_index) < v.mounts().size()) {
            const std::string& reason = v.mounts()[static_cast<size_t>(where.layer_index)].reason;
            is_foc = reason.rfind("FoC", 0) == 0;
            is_eaw = reason.rfind("EaW", 0) == 0;
        }
        if ((is_foc && have_foc) || (is_eaw && have_eaw) || (!is_foc && !is_eaw)) continue;
        std::set<std::string>& out = is_foc ? foc : eaw;
        const data::XmlDocument doc = data::XmlDocument::parse(bytes, std::string(list));
        for (const data::ListedXmlEntry& en : data::listed_xml_entries(doc)) out.insert(normalize_name(en.path));
        (is_foc ? have_foc : have_eaw) = true;
    }
    return have_foc && have_eaw;
}

void check_base_eaw_suspect(const GameData& d, ModCheckReport& r) {
    static constexpr std::string_view kList = "Data\\XML\\GameObjectFiles.xml";
    if (!d.vfs || d.mods.empty()) return;
    const std::optional<vfs::Vfs::Resolution> top = d.vfs->resolve(kList);
    if (!top || top->layer != vfs::Vfs::Resolution::Mod || top->layer_index < 0 ||
        static_cast<size_t>(top->layer_index) >= d.mods.size())
        return;  // the list is the base game's: nothing to judge
    std::set<std::string> foc, eaw;
    if (!base_list_entries(*d.vfs, kList, foc, eaw)) return;
    std::vector<std::string> foc_only;
    for (const std::string& e : foc)
        if (!eaw.count(e)) foc_only.push_back(e);
    if (foc_only.empty()) return;
    std::vector<uint8_t> bytes;
    if (!d.vfs->read(kList, bytes)) return;
    const data::XmlDocument doc = data::XmlDocument::parse(bytes, std::string(kList));
    if (!doc.ok()) return;
    for (const data::ListedXmlEntry& en : data::listed_xml_entries(doc))
        if (std::binary_search(foc_only.begin(), foc_only.end(), normalize_name(en.path))) return;
    r.base_eaw_suspect = d.mods[static_cast<size_t>(top->layer_index)].key() +
                         ": its GameObjectFiles.xml names none of the " + std::to_string(foc_only.size()) +
                         " object files FoC adds to EaW's list (first: " + foc_only.front() +
                         "); probably a base-EaW mod, which retail runs with sweaw.exe (a guess: not refused)";
}

#ifdef ALAMO_GAME_HAVE_LUA_VM
// Engine bindings the port registers as "unbound" stubs, by name (globals and methods separately; a method name counts
// as unbound only when no class implements it, as lua::scan_report does).
struct UnboundNames {
    std::set<std::string, std::less<>> globals, methods;
    UnboundNames() {
        std::set<std::string, std::less<>> method_impl;
        for (const lua::BindingInfo& b : lua::binding_table()) {
            if (b.kind == lua::BindingKind::Method) {
                if (lua::binding_implemented(b)) method_impl.insert(b.name);
                else methods.insert(b.name);
            } else if (!lua::binding_implemented(b) && !lua::is_host_value_global(b.name)) {
                globals.insert(b.name);
            }
        }
        for (const std::string& n : method_impl) methods.erase(n);
    }
};

// Scripts the original starts as scripts of their own: those under a script root other than Library (story plots, AI
// plans, GameObject scripts, evaluators, free stores, interventions, miscellaneous; lua/script_loader.hpp). Library
// scripts are only reached through require from those, and often rely on globals their requirer set, so running one on
// its own reports errors the game never hits.
bool is_entry_script(std::string_view path) {
    bool entry = false;
    for (const std::string_view root : lua::script_roots()) {
        if (path.size() <= root.size() || !iequals(path.substr(0, root.size()), root)) continue;
        if (iequals(root, "Data\\Scripts\\Library\\")) return false;
        entry = true;
    }
    return entry;
}

using UnboundMap = std::map<std::pair<std::string, bool>, UnboundLuaCall>;  // (name, method): sorted, deterministic

// modc t12. Run before an entry script's main chunk, in its VM: GlobalValue becomes a table whose Get / Set call the
// engine's (dot or colon call) and which records, in __alamo_mod_check_unset, every string key Get found unset.
constexpr std::string_view kGlobalValueProbe = R"lua(
local gv = GlobalValue
if gv == nil then return end
local get, set = gv.Get, gv.Set
local unset = {}
__alamo_mod_check_unset = unset
local proxy = {}
proxy.Get = function(a, b)
  local k = a
  if a == proxy then k = b end
  local v = get(k)
  if v == nil and type(k) == "string" then unset[k] = true end
  return v
end
proxy.Set = function(a, b, c)
  if a == proxy then return set(b, c) end
  return set(a, b)
end
GlobalValue = proxy
)lua";

// The keys the probe recorded, sorted.
std::vector<std::string> unset_global_values(lua::LuaVm& vm) {
    std::vector<std::string> keys;
    lua_State* L = vm.state();
    lua_pushstring(L, "__alamo_mod_check_unset");
    lua_gettable(L, LUA_GLOBALSINDEX);
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING) keys.emplace_back(lua_tostring(L, -2), lua_strlen(L, -2));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    std::sort(keys.begin(), keys.end());
    return keys;
}

// Literal keys of GlobalValue.Set("KEY", ...) / GlobalValue:Set('KEY', ...) calls outside line comments.
void scan_global_value_sets(std::string_view src, const std::string& path, std::map<std::string, std::string>& set_by) {
    constexpr std::string_view kName = "GlobalValue";
    for (size_t at = src.find(kName); at != std::string_view::npos; at = src.find(kName, at + 1)) {
        const size_t nl = src.rfind('\n', at);
        const size_t bol = nl == std::string_view::npos ? 0 : nl + 1;
        if (src.substr(bol, at - bol).find("--") != std::string_view::npos) continue;
        size_t i = at + kName.size();
        const auto skip_ws = [&] {
            while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) ++i;
        };
        skip_ws();
        if (i >= src.size() || (src[i] != '.' && src[i] != ':')) continue;
        ++i;
        skip_ws();
        if (src.substr(i, 3) != "Set") continue;
        i += 3;
        skip_ws();
        if (i >= src.size() || src[i] != '(') continue;
        ++i;
        skip_ws();
        if (i >= src.size() || (src[i] != '"' && src[i] != '\'')) continue;
        const char q = src[i++];
        const size_t end = src.find(q, i);
        if (end == std::string_view::npos || src.substr(i, end - i).find('\n') != std::string_view::npos) continue;
        set_by.emplace(std::string(src.substr(i, end - i)), path);  // the first script (path order) keeps it
    }
}

// Literal script names of require("X") / require "X" / require('X') calls outside line comments, by upper-case base name
// (the part after the last '/' or '\'): name -> the first script (path order) requiring it.
void scan_requires(std::string_view src, const std::string& path, std::map<std::string, std::string>& required_by) {
    constexpr std::string_view kName = "require";
    const auto ident = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; };
    for (size_t at = src.find(kName); at != std::string_view::npos; at = src.find(kName, at + 1)) {
        if ((at > 0 && ident(src[at - 1])) || (at + kName.size() < src.size() && ident(src[at + kName.size()]))) continue;
        const size_t nl = src.rfind('\n', at);
        const size_t bol = nl == std::string_view::npos ? 0 : nl + 1;
        if (src.substr(bol, at - bol).find("--") != std::string_view::npos) continue;
        size_t i = at + kName.size();
        const auto skip_ws = [&] {
            while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) ++i;
        };
        skip_ws();
        if (i < src.size() && src[i] == '(') {
            ++i;
            skip_ws();
        }
        if (i >= src.size() || (src[i] != '"' && src[i] != '\'')) continue;
        const char q = src[i++];
        const size_t end = src.find(q, i);
        if (end == std::string_view::npos || end == i || src.substr(i, end - i).find('\n') != std::string_view::npos) continue;
        std::string_view name = src.substr(i, end - i);
        const size_t sep = name.find_last_of("/\\");
        if (sep != std::string_view::npos) name.remove_prefix(sep + 1);
        required_by.emplace(normalize_name(name), path);
    }
}

// The script name of the require that found no file, from the port's message (engine/lua lua_vm.cpp load_script:
// "cannot find script '<name>' in Data\Scripts"); "" for any other error.
std::string missing_require_of(std::string_view err) {
    constexpr std::string_view kPrefix = "cannot find script '";
    const size_t b = err.find(kPrefix);
    if (b == std::string_view::npos) return {};
    const size_t from = b + kPrefix.size();
    const size_t e = err.find("' in Data\\Scripts", from);
    return e == std::string_view::npos ? std::string() : std::string(err.substr(from, e - from));
}

// XML elements whose text names a script the original loads by that name (with or without .lua): an object type's or a
// story plot's Lua_Script (OBJECT_SCRIPTS.md, STORY.md), an AI player's free store scripts, GameConstants'
// Game_Scoring_Script_Name (GAME_SCORING.md). Matched case-insensitively.
constexpr std::string_view kScriptNameTags[] = {"LUA_SCRIPT", "GALACTICFREESTORESCRIPT", "SPACEFREESTORESCRIPT", "LANDFREESTORESCRIPT",
                                                "GAME_SCORING_SCRIPT_NAME"};

// For each upper-case script base name in `wanted`, the first Data\XML file (path order) with a kScriptNameTags element
// naming it: "<file> (<tag>)". A byte scan of the upper-cased text, so it also sees files no loader reads.
std::map<std::string, std::string> xml_script_names(const GameData& d, const std::set<std::string>& wanted) {
    std::map<std::string, std::string> found;
    std::vector<uint8_t> bytes;
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    for (const std::string& path : d.vfs->list("Data\\XML\\**.xml")) {
        if (found.size() == wanted.size()) break;
        if (!d.files->read(path, bytes)) continue;
        std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        ascii_upper_in_place(text);
        for (const std::string_view tag : kScriptNameTags) {
            const std::string open = "<" + std::string(tag);
            for (size_t at = text.find(open); at != std::string::npos; at = text.find(open, at + 1)) {
                const size_t after = at + open.size();
                if (after >= text.size() || (text[after] != '>' && !space(text[after]))) continue;
                const size_t gt = text.find('>', after);
                if (gt == std::string::npos) break;
                const size_t lt = text.find('<', gt + 1);
                if (lt == std::string::npos) break;
                std::string_view v = std::string_view(text).substr(gt + 1, lt - gt - 1);
                while (!v.empty() && space(v.front())) v.remove_prefix(1);
                while (!v.empty() && space(v.back())) v.remove_suffix(1);
                if (v.size() > 4 && v.substr(v.size() - 4) == ".LUA") v.remove_suffix(4);
                const std::string key(v);
                if (wanted.count(key) && !found.count(key)) found.emplace(key, path + " (" + std::string(tag) + ")");
            }
        }
    }
    return found;
}

// The script root of `path` (lua::script_roots(), longest match) or "".
std::string_view script_root_of(std::string_view path) {
    std::string_view best;
    for (const std::string_view root : lua::script_roots())
        if (path.size() > root.size() && root.size() > best.size() && iequals(path.substr(0, root.size()), root)) best = root;
    return best;
}

// modc t24. A main chunk that fails on a require no layer can satisfy fails the same way in retail: its Lua 5.0.2
// require raises "could not load package" (docs/re/MODC_LOADER.md section 6), so the script does not load. That matters
// when the data loads the script; a mod's own script that nothing names never runs (e.g. Thrawn's Revenge's
// GameScoring_old.lua beside the GameScoring.lua that GameConstants names). Fills missing_require / named_by and moves
// the unnamed ones to lua_unused_scripts. Only the roots whose scripts are loaded by a name the data gives count as
// unnamed (GameObject, Story, FreeStore, Miscellaneous); the AI folders are enumerated, Evaluators / Interventions not
// traced, Library is never an entry.
void classify_missing_requires(const GameData& d, ModCheckReport& r, const std::map<std::string, std::string>& required_by) {
    // The game without the mods (the same arguments minus MODPATH= / STEAMMOD=): a mod's MEGs mount in the MEG layer like
    // the base ones, so the base files are only told apart by a tree without the mods. Created on first need.
    std::unique_ptr<vfs::Vfs> base_tree;
    bool base_tried = false;
    const auto in_base_game = [&](const std::string& path) {
        if (!base_tried) {
            base_tried = true;
            std::vector<std::string> rest;
            for (const std::string& a : r.args)
                if (!(a.size() >= 8 && iequals(std::string_view(a).substr(0, 8), "MODPATH=")) &&
                    !(a.size() >= 9 && iequals(std::string_view(a).substr(0, 9), "STEAMMOD=")))
                    rest.push_back(a);
            vfs::VfsConfig cfg;
            cfg.foc_root = r.game;
            if (vfs::parse_command_line(rest, cfg)) base_tree = vfs::Vfs::create(cfg, nullptr);
        }
        return base_tree && base_tree->exists(path);
    };
    constexpr std::string_view kByName[] = {"Data\\Scripts\\GameObject\\", "Data\\Scripts\\Story\\", "Data\\Scripts\\FreeStore\\",
                                            "Data\\Scripts\\Miscellaneous\\"};
    std::set<std::string> unresolved;  // base names left for the XML scan
    for (LuaIssue& e : r.lua_run_errors) {
        e.missing_require = missing_require_of(e.error);
        if (e.missing_require.empty()) continue;
        const std::string_view root = script_root_of(e.path);
        const std::string base = effect_base(e.path);
        if (std::none_of(std::begin(kByName), std::end(kByName), [&](std::string_view k) { return iequals(k, root); })) {
            e.named_by = "script root " + std::string(root);
        } else if (in_base_game(e.path)) {
            e.named_by = "base script";
        } else if (const auto it = required_by.find(base); it != required_by.end() && !iequals(it->second, e.path)) {
            e.named_by = "require in " + it->second;
        } else {
            unresolved.insert(base);
        }
    }
    if (unresolved.empty()) return;
    const std::map<std::string, std::string> named = xml_script_names(d, unresolved);
    std::vector<LuaIssue> kept;
    for (LuaIssue& e : r.lua_run_errors) {
        if (!e.missing_require.empty() && e.named_by.empty()) {
            const auto it = named.find(effect_base(e.path));
            if (it != named.end()) e.named_by = it->second;
        }
        (!e.missing_require.empty() && e.named_by.empty() ? r.lua_unused_scripts : kept).push_back(std::move(e));
    }
    r.lua_run_errors = std::move(kept);
}

void add_unbound(UnboundMap& calls, const std::map<std::string, uint32_t>& found, const std::set<std::string, std::less<>>& unbound,
                 bool method) {
    for (const auto& [name, sites] : found) {
        if (!unbound.count(name)) continue;
        UnboundLuaCall& c = calls[{name, method}];
        c.name = name;
        c.method = method;
        c.call_sites += sites;
        ++c.scripts;
    }
}
// mcx0 t06: calls to engine bindings, by binding; the ones with an effect gap (lua::binding_effect_gap) or a retail no-op
// become report v4 entries. Methods are matched by name (the static scan does not know the receiver's class).
struct BindingCallAcc {
    uint32_t call_sites = 0, scripts = 0;
    std::string first_script, first_mod_script;  // path order; the first one in a mod folder
    bool in_mod = false;
};
using BindingCallMap = std::map<std::string, BindingCallAcc>;  // Lua name (global) / method name: sorted, deterministic

void note_binding_calls(BindingCallMap& m, const std::map<std::string, uint32_t>& found, const std::string& path, bool in_mod) {
    for (const auto& [name, sites] : found) {
        BindingCallAcc& a = m[name];
        if (a.scripts++ == 0) a.first_script = path;
        if (in_mod && a.first_mod_script.empty()) a.first_mod_script = path;
        a.call_sites += sites;
        a.in_mod = a.in_mod || in_mod;
    }
}

void binding_findings(const BindingCallMap& globals, const BindingCallMap& methods, ModCheckReport& r) {
    std::set<std::string> called;
    for (const lua::BindingInfo& b : lua::binding_table()) {
        const bool global = b.kind == lua::BindingKind::Global;
        const BindingCallMap& m = global ? globals : methods;
        const auto it = m.find(b.name);
        if (it == m.end()) continue;
        const std::string key = global ? std::string(b.name) : std::string(b.cls) + "." + b.name;
        called.insert(global ? key : std::string(b.name));
        const BindingCallAcc& a = it->second;
        if (b.effect == lua::BindingEffect::NoopRetail) {
            r.retail_equivalent.push_back(
                {"noop_binding", key, a.call_sites, a.in_mod ? a.first_mod_script : a.first_script, a.in_mod, b.consumer});
            continue;
        }
        if (!lua::binding_effect_gap(b)) continue;
        BindingGapCall g;
        g.name = key;
        g.effect = lua::to_string(b.effect);
        g.detail = b.consumer;
        g.call_sites = a.call_sites;
        g.scripts = a.scripts;
        g.first_script = a.in_mod ? a.first_mod_script : a.first_script;  // a mod's finding cites the mod's file
        g.in_mod = a.in_mod;
        r.binding_gaps.push_back(std::move(g));
    }
    r.binding_calls = static_cast<uint32_t>(called.size());
    std::stable_sort(r.binding_gaps.begin(), r.binding_gaps.end(), [](const BindingGapCall& a, const BindingGapCall& b) {
        if (a.call_sites != b.call_sites) return a.call_sites > b.call_sites;
        return a.name < b.name;
    });
}
#endif

void check_lua(const GameData& d, ModCheckReport& r, bool run_chunks, int script_budget) {
#ifdef ALAMO_GAME_HAVE_LUA_VM
    if (!d.vfs) return;
    lua::LuaHost host;
    host.files = d.files.get();
    host.types = &d.types;
    lua::LuaVm compiler(host);
    const UnboundNames unbound;
    UnboundMap calls;
    BindingCallMap binding_globals, binding_methods;  // mcx0 t06
    std::vector<uint8_t> bytes;
    // One GlobalValue store for every run, as the session has one: a value a main chunk sets is seen by later ones.
    lua::GlobalValueStore global_values;
    std::map<std::string, std::string> set_by;  // GlobalValue key -> first script (path order) that sets it
    std::map<std::string, std::string> required_by;  // required script base name -> first script requiring it
    struct Failed {
        LuaIssue issue;
        std::vector<std::string> unset;  // GlobalValue keys the chunk read while unset
    };
    std::vector<Failed> failed;
    for (const std::string& path : d.vfs->list("Data\\Scripts\\**.lua")) {
        ++r.lua_scripts;
        std::string err;
        const bool in_mod = in_mod_layer(*d.vfs, path);
        if (!d.files->read(path, bytes)) {
            r.lua_errors.push_back({path, "unreadable", in_mod});
            continue;
        }
        const std::string_view chunk(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!compiler.compile(chunk, path, &err)) {
            r.lua_errors.push_back({path, err, in_mod});
            continue;
        }
        lua::ScriptScan scan;
        lua::scan_script(chunk, scan);
        scan_global_value_sets(chunk, path, set_by);
        scan_requires(chunk, path, required_by);
        add_unbound(calls, scan.global_calls, unbound.globals, false);
        add_unbound(calls, scan.method_calls, unbound.methods, true);
        note_binding_calls(binding_globals, scan.global_calls, path, in_mod);
        note_binding_calls(binding_methods, scan.method_calls, path, in_mod);
        if (!run_chunks || !is_entry_script(path)) continue;
        // What loading the script runs: its main chunk (requires, definitions, top-level calls), in a VM of its own as
        // the original gives every script (lua_vm.hpp). No objects, players or session values exist here.
        lua::LuaVmOptions opt;
        if (script_budget >= 0) opt.script_budget = script_budget;
        const size_t slash = path.rfind('\\');
        opt.script_name = path.substr(slash == std::string::npos ? 0 : slash + 1);
        lua::LuaVm vm(host, opt);
        lua::attach_global_values(vm, &global_values);
        std::string probe_err;
        const bool probed = vm.run(kGlobalValueProbe, "=mod_check_global_value_probe", &probe_err);
        ++r.lua_run;
        if (!vm.run(chunk, path, &err)) {
            Failed f{LuaIssue(path, err, in_mod), {}};
            if (probed) f.unset = unset_global_values(vm);
            failed.push_back(std::move(f));
        }
    }
    // A failed chunk that read a key some script sets (in its main chunk after this one, or in a function the session
    // calls) depends on session state; the others are load warnings.
    for (Failed& f : failed) {
        for (const std::string& key : f.unset) {
            const auto it = set_by.find(key);
            if (it == set_by.end()) continue;
            f.issue.global_value = key;
            f.issue.set_by = it->second;
            break;
        }
        (f.issue.global_value.empty() ? r.lua_run_errors : r.lua_session_errors).push_back(std::move(f.issue));
    }
    classify_missing_requires(d, r, required_by);
    binding_findings(binding_globals, binding_methods, r);
    for (auto& [key, c] : calls) r.unbound_lua_calls.push_back(std::move(c));
    std::sort(r.unbound_lua_calls.begin(), r.unbound_lua_calls.end(), [](const UnboundLuaCall& a, const UnboundLuaCall& b) {
        if (a.call_sites != b.call_sites) return a.call_sites > b.call_sites;
        if (a.name != b.name) return a.name < b.name;
        return a.method < b.method;
    });
#else
    (void)d;
    (void)r;
    (void)run_chunks;
    (void)script_budget;
#endif
}

// ---- mcx0 t06: report version 4 (docs/MOD_COMPAT_DESIGN.md §12.1) ----

// True when `text` leaves the field at `def` (the class default; "" = zero / false / empty): empty, TBD, or the same
// value after the type code's conversion (objects/type_fields.hpp). A tag set to its default does not need honouring.
bool default_value(std::string_view text, uint8_t code, std::string_view def) {
    text = ascii_trim(text);
    if (text.empty() || iequals(text, "TBD")) return true;
    if (objects::kind_of_code(code) == objects::FieldKind::Bool)  // 0x0 and the flag-bit booleans 0x50
        return objects::parse_type_bool(text) == (!def.empty() && objects::parse_type_bool(def));
    switch (objects::conv_of_code(code)) {
        case objects::Conv::Bool: return objects::parse_type_bool(text) == (!def.empty() && objects::parse_type_bool(def));
        case objects::Conv::Int32:
        case objects::Conv::Int8:
        case objects::Conv::UIntClamp100: return objects::c_atoi(text) == objects::c_atoi(def.empty() ? "0" : def);
        case objects::Conv::Float:
        case objects::Conv::Float01:
        case objects::Conv::Double:
        case objects::Conv::Double01: return objects::c_atof(text) == objects::c_atof(def.empty() ? "0" : def);
        default: return !def.empty() && iequals(text, def);
    }
}

struct GapAcc {
    HonourGap g;
    std::set<std::string> entries;
};
using GapMap = std::map<std::pair<std::string, std::string>, GapAcc>;  // (class, tag lower): sorted, deterministic

void note_gap(GapMap& m, const objects::TagLedgerRow& row, const std::string& entry, const std::string& file, bool in_mod) {
    GapAcc& a = m[{std::string(row.cls), ascii_lower(row.tag)}];
    if (a.g.uses++ == 0) {
        a.g.cls = std::string(row.cls);
        a.g.tag = std::string(row.tag);
        a.g.state = objects::to_string(row.state);
        a.g.first_file = file;
        a.g.tier = row.tier;
        a.g.base_uses = row.base_uses;
        a.g.spec = std::string(row.spec);
    }
    if (in_mod && !a.g.in_mod) a.g.first_file = file;  // a mod's finding cites the mod's file
    a.g.in_mod = a.g.in_mod || in_mod;
    a.entries.insert(entry);
}

bool object_type_owner(std::string_view owner) {
    return owner == "GameObjectType" || owner == "SpaceProp" || owner == "HeroUnit";
}

void honour_gaps_object_types(const GameData& d, GapMap& m) {
    const objects::ObjectTypeDatabase& db = d.raw;
    const objects::FieldColumns& f = db.fields();
    const auto& files = db.files();
    std::map<std::string, std::string_view, std::less<>> defaults;  // lower tag -> constructor default text
    for (const auto& [tag, value] : objects::type_field_defaults()) defaults.emplace(ascii_lower(tag), value);
    const auto rows = objects::field_schema();
    std::vector<const objects::TagLedgerRow*> ledger(rows.size(), nullptr);
    std::vector<uint8_t> looked(rows.size(), 0);
    std::vector<int8_t> file_in_mod(files.size(), -1);
    for (size_t i = 0; i < f.size(); ++i) {
        const uint32_t sr = f.schema[i];
        if (sr >= rows.size()) continue;
        if (!looked[sr]) {
            looked[sr] = 1;
            const objects::TagLedgerRow* row = objects::find_tag_ledger(rows[sr].owner, rows[sr].tag);
            ledger[sr] = row && objects::is_honour_gap(row->state) ? row : nullptr;
        }
        const objects::TagLedgerRow* row = ledger[sr];
        if (!row) continue;
        std::string_view def;
        if (object_type_owner(rows[sr].owner)) {
            const auto it = defaults.find(ascii_lower(rows[sr].tag));
            if (it != defaults.end()) def = it->second;
        }
        if (default_value(f.text[i], rows[sr].code, def)) continue;
        const objects::TypeIndex t = f.type[i];
        const uint32_t fi = db.types().file[t];
        if (fi >= files.size()) continue;
        if (file_in_mod[fi] < 0) file_in_mod[fi] = static_cast<int8_t>(d.vfs && in_mod_layer(*d.vfs, files[fi].path) ? 1 : 0);
        note_gap(m, *row, "type#" + std::to_string(t), files[fi].path, file_in_mod[fi] != 0);
    }
}

void honour_gaps_databases(const GameData& d, GapMap& m) {
    std::vector<uint8_t> bytes;
    for (const XmlDatabase& db : kXmlDatabases) {
        for (const std::string& path : database_files(d, db)) {
            if (!d.files->read(path, bytes)) continue;
            const data::XmlDocument doc = data::XmlDocument::parse(bytes, path);
            if (!doc.ok()) continue;
            const bool in_mod = d.vfs && in_mod_layer(*d.vfs, path);
            std::vector<std::pair<data::XmlNode, data::XmlNode>> checked;
            if (db.walk) {
                walk_text_elements(doc.root(), checked);
            } else {
                std::vector<data::XmlNode> entries;
                if (db.element.empty()) entries.push_back(doc.root());
                else
                    for (data::XmlNode e : doc.root().children())
                        if (db.element == "*" || iequals(e.name(), db.element)) entries.push_back(e);
                for (const data::XmlNode e : entries)
                    for (data::XmlNode c : e.children()) checked.emplace_back(e, c);
            }
            for (const auto& [e, c] : checked) {
                for (const std::string_view owner : db.owners) {
                    const objects::TagLedgerRow* row = objects::find_tag_ledger(owner, c.name());
                    if (!row) continue;  // the first owner that lists the tag decides
                    if (objects::is_honour_gap(row->state) && !default_value(c.text_trimmed(), row->code, {}))
                        note_gap(m, *row, path + "#" + std::to_string(e.index()), path, in_mod);
                    break;
                }
            }
        }
    }
}

void check_honour_gaps(const GameData& d, ModCheckReport& r) {
    GapMap m;
    honour_gaps_object_types(d, m);
    if (d.files) honour_gaps_databases(d, m);
    for (auto& [key, a] : m) {
        a.g.entries = static_cast<uint32_t>(a.entries.size());
        r.honour_gaps.push_back(std::move(a.g));
    }
    std::stable_sort(r.honour_gaps.begin(), r.honour_gaps.end(), [](const HonourGap& a, const HonourGap& b) {
        if (a.uses != b.uses) return a.uses > b.uses;
        if (a.cls != b.cls) return a.cls < b.cls;
        return a.tag < b.tag;
    });
}

// Every <Event_Type> / <Reward_Type> value of the Data\XML files (the story plots; a plain text scan, so a mod's plot
// counts whether or not a campaign names it).
void scan_story_values(std::string_view text, std::string_view lower, std::string_view open, std::vector<std::string>& out) {
    size_t pos = 0;
    while ((pos = lower.find(open, pos)) != std::string_view::npos) {
        pos += open.size();
        const size_t end = text.find('<', pos);
        if (end == std::string_view::npos) break;
        std::string v = normalize_name(ascii_trim(text.substr(pos, end - pos)));
        if (!v.empty()) out.push_back(std::move(v));
        pos = end;
    }
}

void check_story(const GameData& d, ModCheckReport& r) {
    if (!d.vfs || !d.files) return;
    std::map<std::string, StoryUse> rewards, events;  // upper-case name: sorted, deterministic
    std::vector<uint8_t> bytes;
    std::vector<std::string> found;
    for (const std::string& path : d.vfs->list("Data\\XML\\**.xml")) {
        if (!d.files->read(path, bytes)) continue;
        std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        for (size_t c = text.find("<!--"); c != std::string::npos; c = text.find("<!--", c)) {  // commented-out events
            const size_t e = text.find("-->", c + 4);
            const size_t end = e == std::string::npos ? text.size() : e + 3;
            std::fill(text.begin() + static_cast<std::ptrdiff_t>(c), text.begin() + static_cast<std::ptrdiff_t>(end), ' ');
            c = end;
        }
        const std::string lower = ascii_lower(text);
        if (lower.find("<event_type>") == std::string::npos) continue;
        ++r.story.files;
        const bool in_mod = in_mod_layer(*d.vfs, path);
        for (const bool reward : {true, false}) {
            found.clear();
            scan_story_values(text, lower, reward ? "<reward_type>" : "<event_type>", found);
            std::map<std::string, StoryUse>& m = reward ? rewards : events;
            for (const std::string& v : found) {
                StoryUse& u = m[v];
                if (u.uses++ == 0) {
                    u.name = v;
                    u.first_file = path;
                }
                if (in_mod && !u.in_mod) u.first_file = path;  // a mod's finding cites the mod's file
                u.in_mod = u.in_mod || in_mod;
            }
        }
    }
    r.story.rewards_used = static_cast<uint32_t>(rewards.size());
    r.story.events_used = static_cast<uint32_t>(events.size());
    for (auto& [name, u] : rewards) {
        const sim::StoryRewardRow* row = sim::find_story_reward(name);
        if (row && row->owner == sim::StoryRewardOwner::Missing) {
            u.note = std::string(row->note);
            r.story.rewards_missing.push_back(u);
        } else if (!row || row->owner == sim::StoryRewardOwner::Retail) {
            std::string why = row ? std::string(row->note)
                                  : std::string("not in retail's Reward_Type enum: its dispatcher ignores it (STORY_REWARDS_GALACTIC.md 0.1)");
            r.retail_equivalent.push_back({"unknown_reward", name, u.uses, u.first_file, u.in_mod, std::move(why)});
        }
    }
    for (auto& [name, u] : events) {
        const sim::StoryEventRow* row = sim::find_story_event(name);
        if (row && row->state == sim::StoryEventState::Partial) {
            u.note = std::string(row->note);
            r.story.events_partial.push_back(u);
        } else if (!row || row->state == sim::StoryEventState::Retail) {
            std::string why = row ? std::string(row->note) : std::string("not a retail event type: retail creates no event for it");
            r.retail_equivalent.push_back({"dropped_event", name, u.uses, u.first_file, u.in_mod, std::move(why)});
        }
    }
}

#ifdef ALAMO_GAME_HAVE_GALACTIC_AI
// Which resolvers evaluate each equation (bit 0 galactic, bit 1 tactical; 0 = not known). Retail loads every
// PerceptualEquations file into one set, and the caller decides the mode: the template mode for a template's trigger /
// untrigger / budget equations, and the caller's mode for an equation another one names. Goal proposal functions and
// equations Lua plans name stay 0 (either resolver counts): a set's entry names its goal, not the mode it is evaluated in
// (EAW-Remake parks land functions on the galactic AlwaysOff goal), and the plans are not traced.
constexpr uint8_t kModeGalactic = 1, kModeTactical = 2;
std::vector<uint8_t> equation_modes(const ai::EquationSet& set, const ai::AiDefinitions& defs) {
    std::vector<uint8_t> modes(set.size(), 0);
    const auto bit = [](ai::AiMode m) { return m == ai::AiMode::Galactic ? kModeGalactic : kModeTactical; };
    const auto seed = [&](std::string_view name, uint8_t b) {
        const uint32_t i = name.empty() ? ai::kUnresolved : set.find(name);
        if (i < modes.size()) modes[i] = static_cast<uint8_t>(modes[i] | b);
    };
    for (const ai::AiPlayerTypeDef& p : defs.players)
        for (uint32_t m = 0; m < ai::kAiModeCount; ++m)
            for (const std::string& name : p.templates[m])
                if (const ai::TemplateDef* t = defs.find_template(name)) {
                    const uint8_t b = bit(static_cast<ai::AiMode>(m));
                    seed(t->trigger, b);
                    seed(t->untrigger, b);
                    for (const auto& [category, eq] : t->budget) seed(eq, b);
                }
    // An equation named by another one runs in its caller's modes (index order until nothing changes).
    for (bool changed = true; changed;) {
        changed = false;
        for (uint32_t i = 0; i < static_cast<uint32_t>(set.size()); ++i) {
            if (!modes[i]) continue;
            for (const ai::ExprNode& n : set.equation(i).expr.nodes) {
                if (n.op != ai::Op::Function || n.ref >= modes.size()) continue;
                const uint8_t m = static_cast<uint8_t>(modes[n.ref] | modes[i]);
                if (m != modes[n.ref]) {
                    modes[n.ref] = m;
                    changed = true;
                }
            }
        }
    }
    return modes;
}
#endif

void check_token_coverage(const GameData& d, ModCheckReport& r) {
#ifdef ALAMO_GAME_HAVE_GALACTIC_AI
    if (!d.ai) return;
    const ai::EquationSet& set = d.ai->equations;
    TokenCoverage& tc = r.token_coverage;
    tc.checked = true;
    struct Acc {
        TokenGap g;
        bool unregistered = false;
        bool galactic = false, tactical = false;  // the resolver evaluates the path
        bool implemented = true;                  // every equation using it runs in a mode that evaluates it
        bool cited = false, cited_in_mod = false; // g.first_equation / first_file are set (from a mod file)
    };
    std::map<std::string, Acc> paths;  // display path: sorted, deterministic
    std::map<std::string, int8_t> file_in_mod;
    const std::vector<uint8_t> modes = equation_modes(set, d.ai->defs);
    for (uint32_t i = 0; i < static_cast<uint32_t>(set.size()); ++i) {
        const ai::Equation& e = set.equation(i);
        if (!e.parsed) continue;
        ++tc.equations;
        std::set<std::string> seen;  // an equation counts once per path
        for (const ai::ExprNode& n : e.expr.nodes) {
            if (n.op != ai::Op::Token) continue;
            const auto path = std::span<const ai::TokenId>(e.expr.path).subspan(n.path_begin, n.path_len);
            std::string shown = ai::root_name(n.root);  // as the equations write it: Variable_Target.EnemyForce.X
            for (const ai::TokenId t : path) shown += "." + std::string(set.tokens().name(t));
            if (!seen.insert(shown).second) continue;
            Acc& a = paths[shown];
            if (a.g.equations++ == 0) {
                a.g.token = shown;
                a.g.first_equation = e.name;
                a.g.first_file = e.file;
                std::string key;
                bool registered = true;
                for (const ai::TokenId t : path) {
                    registered = registered && ai::TokenTable::is_registered(t);
                    if (!key.empty()) key += '.';
                    key += normalize_name(set.tokens().name(t));
                }
                // The port evaluates it: fine whatever the token table says. Not evaluated and a name the token table does
                // not know: retail fails the equation too (retail-equivalent). Else a port gap.
                a.galactic = ai::GalacticSimTokenResolver::path_registered(n.root, key);
                a.tactical = ai::tactical::TacticalTokenResolver::path_implemented(n.root, path, set.tokens());
                a.unregistered = !a.galactic && !a.tactical && !registered;
            }
            // Each mode the equation runs in needs its resolver; an equation of unknown mode either one.
            const uint8_t mode = modes[i];
            const bool covered = mode == 0 ? a.galactic || a.tactical
                                           : (!(mode & kModeGalactic) || a.galactic) && (!(mode & kModeTactical) || a.tactical);
            if (!covered) a.implemented = false;
            int8_t& im = file_in_mod[e.file];
            if (im == 0) im = static_cast<int8_t>(d.vfs && in_mod_layer(*d.vfs, e.file) ? 1 : -1);
            // The finding cites the first equation that shows it (one that misses the path, or any for an unregistered
            // name), and the mod's first one when a mod's equation does.
            if ((!covered || a.unregistered) && (!a.cited || (im > 0 && !a.cited_in_mod))) {
                a.g.first_equation = e.name;
                a.g.first_file = e.file;
                a.cited = true;
                a.cited_in_mod = im > 0;
            }
            a.g.in_mod = a.g.in_mod || (im > 0 && (!covered || a.unregistered));
        }
    }
    tc.leaves = static_cast<uint32_t>(paths.size());
    for (auto& [shown, a] : paths) {
        if (a.unregistered) {
            r.retail_equivalent.push_back({"unregistered_token", shown, a.g.equations, a.g.first_file, a.g.in_mod,
                                           "a token retail's token table does not register: retail fails the whole equation too"});
            continue;
        }
        if (!a.implemented) tc.missing.push_back(std::move(a.g));
    }
    std::stable_sort(tc.missing.begin(), tc.missing.end(), [](const TokenGap& a, const TokenGap& b) {
        if (a.equations != b.equations) return a.equations > b.equations;
        return a.token < b.token;
    });
#else
    (void)d;
    (void)r;
#endif
}

// Findings retail shares that the earlier checks already list: the unread tags (no retail descriptor table lists them;
// mcx0 t07: the most used ones are not even strings in the binary) and the scripts the shared Lua 5.0.2 rejects.
void retail_equivalent_from_checks(ModCheckReport& r) {
    for (const UnknownXmlTag& u : r.unknown_xml_tags)
        r.retail_equivalent.push_back({"unread_tag", u.database + ": " + u.tag, u.occurrences, u.first_file, u.in_mod,
                                       "no retail descriptor table lists the tag: retail ignores it too"});
    for (const LuaIssue& e : r.lua_errors)
        r.retail_equivalent.push_back({"script_error", e.path, 1, e.path, e.in_mod,
                                       "retail's Lua 5.0.2 rejects it too (MODS.md section 9): " + e.error});
}

// The issue records of report v4 and the static grade (§12.2): an error gives G0, else G1 (G2 and up need runs). The
// static part of G3 holds when no honour gap, binding gap, missing reward or missing token comes from the mod's files.
void compat_issues_and_grade(ModCheckReport& r) {
    using alamo::IssueKind;
    using alamo::IssueSeverity;
    const auto layer = [](bool in_mod) { return in_mod ? uint16_t{0} : alamo::kBaseLayer; };
    for (const HonourGap& g : r.honour_gaps)
        r.issues.add(IssueKind::HonourGap, IssueSeverity::Info, g.cls + "." + g.tag, g.first_file,
                     g.state + ", " + std::to_string(g.uses) + " uses", false, layer(g.in_mod));
    for (const BindingGapCall& b : r.binding_gaps)
        r.issues.add(IssueKind::BindingUnconsumed, IssueSeverity::Info, b.name, b.first_script, b.effect, false, layer(b.in_mod));
    for (const StoryUse& u : r.story.rewards_missing)
        r.issues.add(IssueKind::RewardMissing, IssueSeverity::Info, u.name, u.first_file, u.note, false, layer(u.in_mod));
    for (const TokenGap& t : r.token_coverage.missing)
        r.issues.add(IssueKind::TokenMissing, IssueSeverity::Info, t.token, t.first_file, t.first_equation, false, layer(t.in_mod));
    // mcx6 t08: a main chunk the runaway guard stopped (LuaVmOptions::script_budget; its error says script_runaway).
    for (const std::vector<LuaIssue>* list : {&r.lua_run_errors, &r.lua_session_errors})
        for (const LuaIssue& e : *list)
            if (e.error.find("script_runaway") != std::string::npos)
                r.issues.add(IssueKind::ScriptRunaway, IssueSeverity::Warning, e.path, e.path, e.error, false, layer(e.in_mod));
    std::stable_sort(r.retail_equivalent.begin(), r.retail_equivalent.end(),
                     [](const RetailEquivalentFinding& a, const RetailEquivalentFinding& b) {
                         if (a.kind != b.kind) return a.kind < b.kind;
                         if (a.uses != b.uses) return a.uses > b.uses;
                         return a.subject < b.subject;
                     });
    for (const RetailEquivalentFinding& f : r.retail_equivalent)
        r.issues.add(IssueKind::RetailEquivalent, IssueSeverity::Info, f.kind + " " + f.subject, f.first_file, f.detail, true,
                     layer(f.in_mod));
    // mcx2 t01: assets the mod layers serve (asset_audit.hpp). A retail-equivalent rejection never blocks G3.
    for (const AssetEntry& e : r.assets.rejected)
        r.issues.add(IssueKind::AssetRejected, e.retail_equivalent ? IssueSeverity::Info : IssueSeverity::Warning, e.path, e.path,
                     e.family + ": " + e.rule, e.retail_equivalent, uint16_t{0});
    for (const AssetEntry& e : r.assets.salvaged)
        r.issues.add(IssueKind::AssetSalvaged, IssueSeverity::Info, e.path, e.path, e.family + ": " + e.rule, false, uint16_t{0});
    // mcx3 t04: the port reads a malformed MEG as the game does (meg::Archive::lookup): retail-equivalent findings.
    for (const vfs::Vfs::MegIssue& m : r.meg_issues) {
        using MK = vfs::Vfs::MegIssue::Kind;
        if (m.kind == MK::Index) continue;  // no issue kind of its own: a VFS warning only
        const IssueKind k = m.kind == MK::Unsorted    ? IssueKind::MegUnsorted
                            : m.kind == MK::Duplicate ? IssueKind::MegDuplicate
                                                      : IssueKind::MegNameMismatch;
        const std::string archive = alamo::normalize_name(m.archive);
        bool in_mod = false;
        for (const std::string& dir : r.mod_dirs) {
            std::string d = alamo::normalize_name(dir);
            if (!d.empty() && d.back() != '\\') d.push_back('\\');  // a folder, not a name prefix (Mods\Foo vs Mods\FooBar)
            in_mod = in_mod || archive.rfind(d, 0) == 0;
        }
        r.issues.add(k, IssueSeverity::Info, m.archive, m.archive,
                     std::string(vfs::Vfs::MegIssue::kind_name(m.kind)) + ": " + std::to_string(m.count) + " record(s), first " + m.first,
                     true, layer(in_mod));
    }
    r.grade.grade = r.error_count() ? "G0" : "G1";
    for (const alamo::CompatIssue& i : r.issues.issues())
        if (!i.retail_equivalent && i.layer != alamo::kBaseLayer &&
            (i.kind == IssueKind::HonourGap || i.kind == IssueKind::BindingUnconsumed || i.kind == IssueKind::RewardMissing ||
             i.kind == IssueKind::TokenMissing || i.kind == IssueKind::AssetRejected))
            r.grade.blocking.push_back(std::string(alamo::issue_kind_name(i.kind)) + " " + i.subject);
    for (const AssetEntry& e : r.assets.salvaged)  // design 12.2: a salvage no retail verdict backs keeps G3 open
        if (!asset_salvage_confirmed(e.rule)) r.grade.blocking.push_back("asset_salvaged " + e.path + " (salvage unconfirmed)");
    r.grade.g3_static = r.grade.grade != "G0" && r.grade.blocking.empty() && r.grade.check_errors.empty();
}

void append(std::string& s, const char* fmt, auto... args) {
    char buf[512];
    const int n = std::snprintf(buf, sizeof buf, fmt, args...);
    if (n > 0) s.append(buf, std::min(static_cast<size_t>(n), sizeof buf - 1));
}

void append_list(std::string& s, const std::vector<std::string>& items, size_t first, size_t count, bool verbose) {
    constexpr size_t kShown = 20;
    const size_t shown = verbose ? count : std::min(count, kShown);
    for (size_t i = 0; i < shown; ++i) s += "    " + items[first + i] + "\n";
    if (shown < count) append(s, "    ... %zu more (--verbose lists all)\n", count - shown);
}

}  // namespace

size_t ModCheckReport::error_count() const {
    size_t n = loaded ? 0 : 1;
    for (const auto& s : stages) n += s.ok ? 0 : 1;
    if (resolved) n += stack.errors.size();
    return n + (no_startable_campaign() ? 1u : 0u) + check_errors.size();
}

size_t ModCheckReport::warning_count() const {
    const size_t menu_only = no_startable_campaign() ? 0 : campaigns_without_planets.size();
    return vfs_warnings.size() + diagnostics.size() + menu_only + campaign_warning_count + missing_files.size() +
           unknown_xml_tags.size() + lua_errors.size() + lua_run_errors.size() + unbound_lua_calls.size() + shaders.issues.size() +
           (resolved ? stack.warnings.size() : 0u) + (base_eaw_suspect.empty() ? 0u : 1u);
}

FxPrebuild prebuild_mod_effects(const GameData& d, const std::string& fx_cache_dir) {
    FxPrebuild out;
#ifdef ALAMO_GAME_HAVE_MATERIALS
    if (!d.vfs || !d.files || d.vfs->mod_dirs().empty()) return out;
    const materials::FxCache cache(fx_cache_dir.empty() ? materials::FxCache::default_directory() : path_from_utf8(fx_cache_dir));
    const std::vector<materials::ShaderFileEntry> files = materials::shader_files_from_vfs(*d.vfs, true);
    const materials::FxPrebuildRun run = materials::prebuild_effect_sources(*d.files, files, cache, materials::default_fxc_path());
    out.ran = true;
    out.fxc = run.fxc;
    out.cache = path_to_utf8(cache.directory());
    out.ms = run.ms;
    for (const materials::FxPrebuildItem& it : run.items) {
        FxPrebuildSource src;
        src.path = it.path;
        src.ok = it.ok;
        src.cached = it.cached;
        if (!it.ok) src.error = it.detail.empty() ? std::string("not compiled") : it.detail;
        out.sources.push_back(std::move(src));
    }
#else
    (void)d;
    (void)fx_cache_dir;
#endif
    return out;
}

ModCheckReport check_mod_data(const std::string& root, const std::vector<std::string>& args, const ModCheckOptions& options) {
    ModCheckReport r;
    r.game = root;
    r.args = args;
    GameData d;
    std::string err;
    try {
        r.loaded = load_game_data(root, args, d, &err);
        if (!r.loaded) r.error = err;
    } catch (const std::exception& e) {
        r.error = std::string("exception in the step after '") + (d.stages.empty() ? "VFS" : d.stages.back().name) + "': " + e.what();
    } catch (...) {
        r.error = std::string("unknown exception in the step after '") + (d.stages.empty() ? "VFS" : d.stages.back().name) + "'";
    }
    r.stages = d.stages;
    r.diagnostics = d.diagnostics;
    if (d.vfs) {
        r.vfs_layers = d.vfs->describe();
        r.vfs_warnings = d.vfs->warnings();
        r.mod_dirs = d.vfs->mod_dirs();
        r.meg_issues = d.vfs->meg_issues();
        list_mod_files(*d.vfs, r);
    }
    r.mods = d.mods;
    if (!r.loaded) {
        r.grade.grade = "G0";  // version 4: the data does not load
        for (const vfs::ModDescriptor& m : r.mods)
            if (m.game_tag == vfs::GameTag::Eaw) {  // D1: refused before loading (load_game_data)
                r.base_eaw = true;
                r.grade.blocking.push_back(m.key() + ": " + vfs::kBaseEawRefusal);
            }
        return r;
    }
    // The checks below read what loaded; a throw in one is reported as that check's error, the others still run.
    const auto guarded = [&](const char* what, auto&& fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            r.check_errors.push_back(std::string(what) + ": exception: " + e.what());
        }
    };
    guarded("content", [&] { count_content(d, r); });
    if (options.campaigns) guarded("campaigns", [&] { check_campaigns(d, r); });
    if (options.files) guarded("files", [&] { check_files(d, r); });
    if (options.xml_tags) guarded("xml tags", [&] { check_xml_tags(d, r); });
    if (options.lua) guarded("lua", [&] { check_lua(d, r, options.lua_run, options.lua_script_budget); });
    if (options.shaders && options.fx_prebuild)
        guarded("fx prebuild", [&] { r.shaders.fx_prebuild = prebuild_mod_effects(d, options.fx_cache_dir); });
    if (options.shaders) guarded("shaders", [&] { check_shaders(d, r, options.shader_compile, options.fx_cache_dir); });
    if (options.files && options.submod) guarded("submod", [&] { check_submod(r, options, r.submod); });
    if (options.files) guarded("base-EaW heuristic", [&] { check_base_eaw_suspect(d, r); });
    if (options.compat) {  // mcx0 t06: report version 4 (after the checks it reads)
        // A throw here is not a load error (version 4 never changes errors / warnings): grade.check_errors, G3 stays open.
        const auto compat_guarded = [&](const char* what, auto&& fn) {
            try {
                fn();
            } catch (const std::exception& e) {
                r.grade.check_errors.push_back(std::string(what) + ": exception: " + e.what());
            }
        };
        compat_guarded("honour gaps", [&] { check_honour_gaps(d, r); });
        compat_guarded("story", [&] { check_story(d, r); });
        compat_guarded("token coverage", [&] { check_token_coverage(d, r); });
        if (options.assets && d.vfs)
            compat_guarded("assets", [&] {
                AssetAuditOptions ao;
                ao.strict = options.strict;
                ao.cache_dir = options.asset_cache_dir;
                r.assets = audit_mod_assets(*d.vfs, ao);
            });
        retail_equivalent_from_checks(r);
        compat_issues_and_grade(r);
    } else {  // check_lua collects the binding findings with its scan; version 4 lists stay empty without compat
        r.binding_gaps.clear();
        r.retail_equivalent.clear();
        r.binding_calls = 0;
    }
    return r;
}

vfs::ResolvedStack resolve_mod_stack(const std::string& root, const std::vector<std::string>& picked, const ModCheckOptions& options) {
    const vfs::ModRegistry registry = vfs::scan_installed_mods(root, options.workshop_libraries, false);  // the stack's mods get hashed
    ModCheckOptions light;
    light.campaigns = light.lua = light.lua_run = light.xml_tags = light.shaders = light.compat = light.assets = false;
    light.fx_prebuild = light.shader_compile = false;
    light.files = light.submod = true;
    light.workshop_libraries = options.workshop_libraries;
    const vfs::ParentHint hint = [&](const std::vector<std::string>& stack_args) -> std::optional<std::string> {
        const ModCheckReport r = check_mod_data(root, stack_args, light);
        if (!r.submod.likely || r.submod.parents.empty()) return std::nullopt;
        return r.submod.parents.front().arg;
    };
    return vfs::resolve_stack(registry, picked, hint);
}

std::string format_mod_report(const ModCheckReport& r, bool verbose) {
    std::string s;
    if (r.resolved) {  // mcx3 t02
        append(s, "== stack (resolved, launch order; fingerprint %016llx)\n", static_cast<unsigned long long>(r.stack.fingerprint));
        for (const vfs::StackMod& m : r.stack.mods)
            append(s, "    %-24s %-40s %s%s%s\n", m.mod.arg().c_str(), m.mod.name.c_str(), m.reason.c_str(),
                   m.mod.version.empty() ? "" : ", version ", m.mod.version.c_str());
        for (const std::string& e : r.stack.errors) append(s, "    error: %s\n", e.c_str());
        for (const std::string& w : r.stack.warnings) append(s, "    warning: %s\n", w.c_str());
    }
    if (r.base_eaw) s += "== refused (D1): a base-EaW mod; retail FoC does not run it, the port supports FoC mods only\n";
    if (!r.base_eaw_suspect.empty()) append(s, "== base-EaW heuristic (warning): %s\n", r.base_eaw_suspect.c_str());
    s += "== files\n";
    s += r.vfs_layers;
    if (!r.vfs_layers.empty() && r.vfs_layers.back() != '\n') s += '\n';
    append(s, "VFS warnings: %zu\n", r.vfs_warnings.size());
    append_list(s, r.vfs_warnings, 0, r.vfs_warnings.size(), verbose);
    if (r.mod_dirs.empty()) {
        s += "mod dirs: none (base game)\n";
    } else {
        uint32_t added = 0, overrides = 0;
        for (const auto& f : r.mod_folders) added += f.added, overrides += f.overrides;
        append(s, "mod files: %u new, %u replace a base / MEG / fallback file\n", added, overrides);
        for (const auto& f : r.mod_folders)
            append(s, "    Data\\%-16s %6u new %6u replaced\n", f.folder.empty() ? "." : f.folder.c_str(), f.added, f.overrides);
    }

    s += "== loaders\n";
    for (const auto& st : r.stages) {
        append(s, "%-16s %s %8.1f ms  %zu diagnostics\n", st.name.c_str(), st.ok ? "ok    " : "FAILED", st.ms, st.diag_count);
        append_list(s, r.diagnostics, st.first_diag, st.diag_count, verbose);
    }
    if (!r.loaded) s += "LOAD FAILED: " + r.error + "\n";

    if (r.loaded) {
        const auto& c = r.counts;
        s += "== content\n";
        append(s, "campaigns %zu, planet types %zu, trade routes %zu, object types %zu, factions %zu\n", c.campaigns, c.planets,
               c.trade_routes, c.object_types, c.factions);
        if (c.ai)
            append(s, "AI: %zu galactic plans, %zu space / %zu land tactical plans\n", c.ai_plans, c.tactical_space_plans,
                   c.tactical_land_plans);
        else
            s += "AI: no AI data\n";
        append(s, "skirmish maps: %zu official, %zu custom\n", c.official_maps, c.custom_maps);

        append(s, "== campaigns\n%zu checked, %zu without planets (%s), %zu skipped rows (warnings)\n", r.campaigns_checked,
               r.campaigns_without_planets.size(),
               r.no_startable_campaign() ? "ERROR: no campaign can be started" : "warnings: menu entries that cannot be started",
               r.campaign_warning_count);
        append_list(s, r.campaigns_without_planets, 0, r.campaigns_without_planets.size(), verbose);
        append_list(s, r.campaign_warnings, 0, r.campaign_warnings.size(), verbose);
        append(s, "== missing files\n%zu files named by the data that no layer has (warnings)\n", r.missing_files.size());
        {
            std::vector<std::string> lines;
            for (const MissingFile& f : r.missing_files)
                lines.push_back(std::string(f.in_mod ? "[mod] " : "      ") + f.path + "  (" + f.referenced_by +
                                (f.references > 1 ? ", +" + std::to_string(f.references - 1) + " more" : std::string()) + ")");
            append_list(s, lines, 0, lines.size(), verbose);
        }
        {
            std::map<std::string, size_t> per_db;
            for (const UnknownXmlTag& t : r.unknown_xml_tags) ++per_db[t.database];
            std::string dbs;
            for (const auto& [db, n] : per_db) append(dbs, "%s%s %zu", dbs.empty() ? ": " : ", ", db.c_str(), n);
            append(s, "== xml tags\n%zu tags the original does not read (warnings)%s\n", r.unknown_xml_tags.size(), dbs.c_str());
        }
        {
            std::vector<std::string> lines;
            for (const UnknownXmlTag& t : r.unknown_xml_tags) {
                std::string line;
                append(line, "%s%-14.14s %-40s %6u uses in %u entries, first in %s", t.in_mod ? "[mod] " : "      ", t.database.c_str(),
                       t.tag.c_str(), t.occurrences, t.types, t.first_file.c_str());
                lines.push_back(std::move(line));
            }
            append_list(s, lines, 0, lines.size(), verbose);
        }
        const auto issue_lines = [](const std::vector<LuaIssue>& v) {
            std::vector<std::string> lines;
            for (const LuaIssue& e : v) lines.push_back(std::string(e.in_mod ? "[mod] " : "      ") + e.text());
            return lines;
        };
        append(s, "== lua\n%zu scripts compiled, %zu do not compile (warnings)\n", r.lua_scripts, r.lua_errors.size());
        const std::vector<std::string> compile_lines = issue_lines(r.lua_errors);
        append_list(s, compile_lines, 0, compile_lines.size(), verbose);
        append(s, "%zu entry scripts' main chunks run, %zu raise an error (warnings)\n", r.lua_run, r.lua_run_errors.size());
        std::vector<std::string> run_lines = issue_lines(r.lua_run_errors);
        for (size_t i = 0; i < run_lines.size(); ++i)
            if (!r.lua_run_errors[i].missing_require.empty()) run_lines[i] += "  [missing require; loaded: " + r.lua_run_errors[i].named_by + "]";
        append_list(s, run_lines, 0, run_lines.size(), verbose);
        append(s, "%zu more fail on a require of a missing script and are the mod's own scripts nothing names: unused-script leads (info)\n",
               r.lua_unused_scripts.size());
        const std::vector<std::string> unused_lines = issue_lines(r.lua_unused_scripts);
        append_list(s, unused_lines, 0, unused_lines.size(), verbose);
        append(s, "%zu more fail only for session state: they read a GlobalValue a script sets in the session (info)\n",
               r.lua_session_errors.size());
        {
            std::map<std::pair<std::string, std::string>, size_t> by_key;  // (key, setter) -> scripts
            for (const LuaIssue& e : r.lua_session_errors) ++by_key[{e.global_value, e.set_by}];
            std::vector<std::string> lines;
            for (const auto& [k, n] : by_key)
                lines.push_back("GlobalValue \"" + k.first + "\" (set by " + k.second + "): " + std::to_string(n) + " scripts");
            if (verbose)
                for (const LuaIssue& e : r.lua_session_errors) lines.push_back(std::string(e.in_mod ? "[mod] " : "      ") + e.text());
            append_list(s, lines, 0, lines.size(), verbose);
        }
        append(s, "%zu unbound engine functions / methods called (warnings)\n", r.unbound_lua_calls.size());
        {
            std::vector<std::string> lines;
            for (const UnboundLuaCall& u : r.unbound_lua_calls) {
                std::string line;
                append(line, "%-44s %-6s %6u call sites in %u scripts", u.name.c_str(), u.method ? "method" : "global", u.call_sites,
                       u.scripts);
                lines.push_back(std::move(line));
            }
            append_list(s, lines, 0, lines.size(), verbose);
        }
    }
    if (r.loaded && r.shaders.checked) {
        const ShaderCheck& sc = r.shaders;
        append(s, "== shaders\n%zu effects (%zu from mods), %zu loaded, %zu track-B passes, %zu %s; %zu issues (warnings)\n", sc.effects,
               sc.mod_effects, sc.loaded, sc.translated_passes, sc.translated_ok, sc.compiled ? "compiled" : "translated (no DXC)",
               sc.issues.size());
        std::vector<std::string> lines;
        for (const ShaderIssueEntry& e : sc.issues) {
            std::string line;
            append(line, "%s%-17s %-28s %s", e.in_mod ? "[mod] " : "      ", e.kind.c_str(), e.effect.c_str(), e.path.c_str());
            if (e.drawn_by != "models") line += "  (" + e.drawn_by + " effect)";
            else if (e.models) line += "  (named by " + std::to_string(e.models) + " models, first " + e.first_model + ")";
            else line += "  (no object type's model names it)";
            if (!e.detail.empty()) line += "  " + e.detail;
            lines.push_back(std::move(line));
        }
        append_list(s, lines, 0, lines.size(), verbose);
        if (sc.fx_prebuild.ran) {
            const FxPrebuild& fp = sc.fx_prebuild;
            append(s, "    .fx prebuild: %zu sources, fxc %s, cache %s, %.0f ms\n", fp.sources.size(),
                   fp.fxc.empty() ? "not found (only cached results load)" : fp.fxc.c_str(), fp.cache.c_str(), fp.ms);
            for (const FxPrebuildSource& f : fp.sources)
                append(s, "      %-14s %s%s%s\n", f.ok ? (f.cached ? "ok (cached)" : "ok (compiled)") : "not compiled", f.path.c_str(),
                       f.error.empty() ? "" : "  ", f.error.c_str());
        }
        if (sc.fx_sources) append(s, "    .fx sources %zu, %zu loaded compiled (alamo_fx_prebuild cache)\n", sc.fx_sources, sc.fx_sources_compiled);
        if (sc.models_read) append(s, "    (%zu object type models read for the cross-reference)\n", sc.models_read);
    }
    if (r.submod.missing > 0) {
        const SubmodHint& h = r.submod;
        append(s, "== submod\n%u XML files the mod's own lists name are in no layer; %s\n", h.missing,
               h.likely ? "likely a submod (info)" : "no installed mod has most of them");
        for (const ModParentCandidate& c : h.parents)
            append(s, "    %-14s %5u of %u%s  %s\n", c.arg.c_str(), c.provides, h.missing, c.mentioned ? "  (named in modinfo.json)" : "",
                   c.path.c_str());
        if (h.likely) {
            std::string args;
            for (const std::string& a : h.stacked_args) args += " " + a;
            s += "    check it stacked:" + args + "\n";
        }
    }
    {  // mcx0 t06 (version 4): none of this counts as a warning
        const size_t limit = verbose ? SIZE_MAX : 20;
        size_t gaps_in_mod = 0;
        for (const HonourGap& g : r.honour_gaps) gaps_in_mod += g.in_mod ? 1 : 0;
        append(s, "== compat (report v4; info): grade %s, G3 static part %s, %zu blocking\n", r.grade.grade.c_str(),
               r.grade.g3_static ? "ok" : "open", r.grade.blocking.size());
        append(s, "honour gaps: %zu tags set but not honoured (%zu from mod files)\n", r.honour_gaps.size(), gaps_in_mod);
        for (size_t i = 0; i < r.honour_gaps.size() && i < limit; ++i) {
            const HonourGap& g = r.honour_gaps[i];
            append(s, "    %6u %-8s %s.%s  %s%s\n", g.uses, g.state.c_str(), g.cls.c_str(), g.tag.c_str(), g.first_file.c_str(),
                   g.in_mod ? " [mod]" : "");
        }
        append(s, "bindings: %u called, %zu with an effect gap\n", r.binding_calls, r.binding_gaps.size());
        for (size_t i = 0; i < r.binding_gaps.size() && i < limit; ++i) {
            const BindingGapCall& b = r.binding_gaps[i];
            append(s, "    %6u %-9s %s  %s%s\n", b.call_sites, b.effect.c_str(), b.name.c_str(), b.first_script.c_str(),
                   b.in_mod ? " [mod]" : "");
        }
        append(s, "story: %u files, %u reward types (%zu the port lacks), %u event types (%zu partial)\n", r.story.files,
               r.story.rewards_used, r.story.rewards_missing.size(), r.story.events_used, r.story.events_partial.size());
        for (const StoryUse& u : r.story.rewards_missing)
            append(s, "    %6u reward %s  %s%s\n", u.uses, u.name.c_str(), u.first_file.c_str(), u.in_mod ? " [mod]" : "");
        for (const StoryUse& u : r.story.events_partial)
            append(s, "    %6u event  %s  %s%s\n", u.uses, u.name.c_str(), u.first_file.c_str(), u.in_mod ? " [mod]" : "");
        append(s, "ai tokens: %u equations, %u paths, %zu not evaluated by the port\n", r.token_coverage.equations,
               r.token_coverage.leaves, r.token_coverage.missing.size());
        for (size_t i = 0; i < r.token_coverage.missing.size() && i < limit; ++i) {
            const TokenGap& t = r.token_coverage.missing[i];
            append(s, "    %6u %s  %s (%s)%s\n", t.equations, t.token.c_str(), t.first_file.c_str(), t.first_equation.c_str(),
                   t.in_mod ? " [mod]" : "");
        }
        append(s, "retail-equivalent findings (retail does the same; never lower the grade): %zu\n", r.retail_equivalent.size());
        if (r.assets.ran) {
            append(s, "assets: %zu checked, %zu decoded, %zu salvaged, %zu rejected (%zu retail-equivalent)\n", r.assets.checked,
                   r.assets.decoded, r.assets.salvaged_count, r.assets.rejected_count, r.assets.retail_equivalent);
            size_t shown = 0;
            for (const AssetEntry& e : r.assets.rejected)
                if (!e.retail_equivalent && shown++ < limit)
                    append(s, "    rejected %s %s: %s\n", e.family.c_str(), e.path.c_str(), e.rule.c_str());
        }
        for (size_t i = 0; i < r.grade.blocking.size() && i < limit; ++i)
            append(s, "    blocks G3: %s\n", r.grade.blocking[i].c_str());
    }
    if (!r.check_errors.empty()) {
        append(s, "== check errors: %zu\n", r.check_errors.size());
        append_list(s, r.check_errors, 0, r.check_errors.size(), true);
    }
    append(s, "== result: %s, %zu errors, %zu warnings%s\n", r.error_count() ? "ERRORS" : "OK", r.error_count(), r.warning_count(),
           r.submod.likely ? " (likely a submod: check it stacked, see == submod)" : "");
    return s;
}

namespace {

// Minimal writer: objects and arrays with comma handling, two-space indent, keys in call order.
class Json {
public:
    std::string out;
    void open(char c) {
        sep();
        out.push_back(c);
        first_.push_back(true);
    }
    void close(char c) {
        const bool empty = first_.back();
        first_.pop_back();
        if (!empty) newline();
        out.push_back(c);
    }
    void key(std::string_view k) {
        sep();
        json_text::json_str(out, k);
        out += ": ";
        pending_value_ = true;
    }
    void str(std::string_view v) {
        sep();
        json_text::json_str(out, v);
    }
    void num(uint64_t v) {
        sep();
        out += std::to_string(v);
    }
    void real(double v) {
        sep();
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.1f", v >= 0 && v < 1e15 ? v : 0.0);
        out += buf;
    }
    void boolean(bool v) {
        sep();
        out += v ? "true" : "false";
    }
    // key: [the strings v[first, first + count)]
    void strings(std::string_view k, const std::vector<std::string>& v, size_t first = 0, size_t count = SIZE_MAX) {
        key(k);
        open('[');
        const size_t begin = std::min(first, v.size());
        const size_t end = begin + std::min(count, v.size() - begin);
        for (size_t i = begin; i < end; ++i) str(v[i]);
        close(']');
    }

private:
    void newline() {
        out.push_back('\n');
        out.append(2 * first_.size(), ' ');
    }
    void sep() {
        if (pending_value_) {
            pending_value_ = false;
            return;
        }
        if (first_.empty()) return;
        if (!first_.back()) out.push_back(',');
        first_.back() = false;
        newline();
    }
    std::vector<bool> first_;
    bool pending_value_ = false;
};

enum class LuaIssueKeys { Plain, Session, Require };
void lua_issues(Json& j, std::string_view k, const std::vector<LuaIssue>& v, LuaIssueKeys keys = LuaIssueKeys::Plain) {
    j.key(k);
    j.open('[');
    for (const LuaIssue& e : v) {
        j.open('{');
        j.key("path");
        j.str(e.path);
        j.key("error");
        j.str(e.error);
        j.key("in_mod");
        j.boolean(e.in_mod);
        if (keys == LuaIssueKeys::Require) {
            j.key("missing_require");
            j.str(e.missing_require);
            j.key("named_by");
            j.str(e.named_by);
        }
        if (keys == LuaIssueKeys::Session) {
            j.key("global_value");
            j.str(e.global_value);
            j.key("set_by");
            j.str(e.set_by);
        }
        j.close('}');
    }
    j.close(']');
}

void count(Json& j, std::string_view k, uint64_t v) {
    j.key(k);
    j.num(v);
}

}  // namespace

std::string format_mod_list_json(const vfs::ModRegistry& registry) {
    Json j;
    j.open('{');
    j.key("schema");
    j.str("alamo.mod_list");
    count(j, "version", 1);
    j.key("game");
    j.str(registry.game);
    j.key("mods");
    j.open('[');
    for (const vfs::ModDescriptor& m : registry.mods) {
        j.open('{');
        j.key("key");
        j.str(m.key());
        j.key("arg");
        j.str(m.arg());
        j.key("kind");
        j.str(m.kind == vfs::ModKind::Workshop ? "workshop" : m.kind == vfs::ModKind::LocalMods ? "local" : "path");
        j.key("id");
        j.str(m.id);
        j.key("variant");
        j.str(m.variant);
        j.key("root");
        j.str(m.root);
        j.key("name");
        j.str(m.name);
        j.key("version");
        j.str(m.version);
        j.key("has_data");
        j.boolean(m.has_data);
        j.key("game_tag");
        j.str(m.game_tag == vfs::GameTag::Foc ? "FOC" : m.game_tag == vfs::GameTag::Eaw ? "EAW" : "");
        char hex[24];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(m.content_hash));
        j.key("content_hash");
        j.str(hex);
        j.key("dependencies");
        j.open('[');
        for (const vfs::ModDependency& dep : m.dependencies) {
            j.open('{');
            count(j, "modtype", dep.modtype);
            j.key("identifier");
            j.str(dep.identifier);
            j.close('}');
        }
        j.close(']');
        j.close('}');
    }
    j.close(']');
    j.close('}');
    j.out.push_back('\n');
    return std::move(j.out);
}

std::string format_mod_report_json(const ModCheckReport& r) {
    Json j;
    j.open('{');
    j.key("schema");
    j.str("alamo.mod_report");
    count(j, "version", 4);
    j.key("result");
    j.str(r.error_count() ? "errors" : "ok");
    count(j, "errors", r.error_count());
    count(j, "warnings", r.warning_count());
    j.key("game");
    j.str(r.game);
    j.strings("args", r.args);
    j.key("loaded");
    j.boolean(r.loaded);
    j.key("load_error");
    j.str(r.error);
    // mcx3: the mounted mods' descriptors and, with --resolve, the stack resolution.
    j.key("mods");
    j.open('[');
    for (const vfs::ModDescriptor& m : r.mods) {
        j.open('{');
        j.key("key");
        j.str(m.key());
        j.key("name");
        j.str(m.name);
        j.key("version");
        j.str(m.version);
        j.key("root");
        j.str(m.root);
        j.key("game_tag");
        j.str(m.game_tag == vfs::GameTag::Eaw ? "EAW" : m.game_tag == vfs::GameTag::Foc ? "FOC" : "");
        char hash[24];
        std::snprintf(hash, sizeof hash, "%016llx", static_cast<unsigned long long>(m.content_hash));
        j.key("content_hash");
        j.str(hash);
        j.close('}');
    }
    j.close(']');
    j.key("base_eaw");
    j.boolean(r.base_eaw);
    j.key("base_eaw_suspect");
    j.str(r.base_eaw_suspect);
    j.key("stack");
    j.open('{');
    j.key("resolved");
    j.boolean(r.resolved);
    j.strings("args", r.resolved ? r.stack.args : std::vector<std::string>{});
    j.key("mods");
    j.open('[');
    for (const vfs::StackMod& m : r.stack.mods) {
        j.open('{');
        j.key("key");
        j.str(m.mod.key());
        j.key("arg");
        j.str(m.mod.arg());
        j.key("name");
        j.str(m.mod.name);
        j.key("reason");
        j.str(m.reason);
        j.close('}');
    }
    j.close(']');
    j.strings("errors", r.stack.errors);
    j.strings("warnings", r.stack.warnings);
    char fp[24];
    std::snprintf(fp, sizeof fp, "%016llx", static_cast<unsigned long long>(r.stack.fingerprint));
    j.key("fingerprint");
    j.str(r.resolved ? fp : "");
    j.close('}');

    j.key("files");
    j.open('{');
    j.strings("mod_dirs", r.mod_dirs);
    j.strings("vfs_warnings", r.vfs_warnings);
    uint64_t added = 0, replaced = 0;
    for (const auto& f : r.mod_folders) added += f.added, replaced += f.overrides;
    count(j, "added", added);
    count(j, "replaced", replaced);
    j.key("folders");
    j.open('[');
    for (const auto& f : r.mod_folders) {
        j.open('{');
        j.key("folder");
        j.str(f.folder);
        count(j, "added", f.added);
        count(j, "replaced", f.overrides);
        j.close('}');
    }
    j.close(']');
    j.close('}');

    j.key("loaders");
    j.open('[');
    for (const auto& st : r.stages) {
        j.open('{');
        j.key("name");
        j.str(st.name);
        j.key("ok");
        j.boolean(st.ok);
        j.key("ms");
        j.real(st.ms);
        j.strings("diagnostics", r.diagnostics, st.first_diag, st.diag_count);
        j.close('}');
    }
    j.close(']');

    const auto& c = r.counts;
    j.key("content");
    j.open('{');
    count(j, "campaigns", c.campaigns);
    count(j, "planets", c.planets);
    count(j, "trade_routes", c.trade_routes);
    count(j, "object_types", c.object_types);
    count(j, "factions", c.factions);
    j.key("ai");
    j.boolean(c.ai);
    count(j, "ai_plans", c.ai_plans);
    count(j, "tactical_space_plans", c.tactical_space_plans);
    count(j, "tactical_land_plans", c.tactical_land_plans);
    count(j, "official_maps", c.official_maps);
    count(j, "custom_maps", c.custom_maps);
    j.key("skirmish_maps");  // mcx7 t01
    j.open('[');
    for (const ModCheckReport::SkirmishMap& m : r.skirmish_maps) {
        j.open('{');
        j.key("map");
        j.str(m.map);
        j.key("kind");
        j.str(m.land ? "land" : "space");
        count(j, "players", m.players);
        j.key("in_mod");
        j.boolean(m.in_mod);
        j.close('}');
    }
    j.close(']');
    j.close('}');

    j.key("campaigns");
    j.open('{');
    count(j, "checked", r.campaigns_checked);
    j.strings("without_planets", r.campaigns_without_planets);
    j.strings("startable", r.campaigns_startable);  // mcx7 t01
    count(j, "skipped_rows", r.campaign_warning_count);
    j.strings("warnings", r.campaign_warnings);
    j.close('}');

    j.key("missing_files");
    j.open('[');
    for (const MissingFile& f : r.missing_files) {
        j.open('{');
        j.key("path");
        j.str(f.path);
        j.key("referenced_by");
        j.str(f.referenced_by);
        count(j, "references", f.references);
        j.key("in_mod");
        j.boolean(f.in_mod);
        j.close('}');
    }
    j.close(']');

    j.key("unknown_xml_tags");
    j.open('[');
    for (const UnknownXmlTag& t : r.unknown_xml_tags) {
        j.open('{');
        j.key("database");
        j.str(t.database);
        j.key("tag");
        j.str(t.tag);
        count(j, "occurrences", t.occurrences);
        count(j, "types", t.types);
        j.key("first_file");
        j.str(t.first_file);
        j.key("in_mod");
        j.boolean(t.in_mod);
        j.close('}');
    }
    j.close(']');

    j.key("lua");
    j.open('{');
    count(j, "scripts", r.lua_scripts);
    lua_issues(j, "compile_errors", r.lua_errors);
    count(j, "chunks_run", r.lua_run);
    lua_issues(j, "run_errors", r.lua_run_errors, LuaIssueKeys::Require);
    lua_issues(j, "session_errors", r.lua_session_errors, LuaIssueKeys::Session);
    lua_issues(j, "unused_scripts", r.lua_unused_scripts, LuaIssueKeys::Require);
    j.key("unbound_calls");
    j.open('[');
    for (const UnboundLuaCall& u : r.unbound_lua_calls) {
        j.open('{');
        j.key("name");
        j.str(u.name);
        j.key("kind");
        j.str(u.method ? "method" : "global");
        count(j, "call_sites", u.call_sites);
        count(j, "scripts", u.scripts);
        j.close('}');
    }
    j.close(']');
    j.close('}');

    const ShaderCheck& sc = r.shaders;
    j.key("shaders");
    j.open('{');
    j.key("checked");
    j.boolean(sc.checked);
    count(j, "effects", sc.effects);
    count(j, "mod_effects", sc.mod_effects);
    count(j, "loaded", sc.loaded);
    count(j, "translated_passes", sc.translated_passes);
    count(j, "translated_ok", sc.translated_ok);
    j.key("compiled");
    j.boolean(sc.compiled);
    count(j, "fx_sources", sc.fx_sources);
    count(j, "fx_sources_compiled", sc.fx_sources_compiled);
    j.key("fx_prebuild");  // modc t36 (additive)
    j.open('{');
    j.key("ran");
    j.boolean(sc.fx_prebuild.ran);
    j.key("fxc");
    j.str(sc.fx_prebuild.fxc);
    j.key("sources");
    j.open('[');
    for (const FxPrebuildSource& f : sc.fx_prebuild.sources) {
        j.open('{');
        j.key("path");
        j.str(f.path);
        j.key("ok");
        j.boolean(f.ok);
        j.key("cached");
        j.boolean(f.cached);
        j.key("error");
        j.str(f.error);
        j.close('}');
    }
    j.close(']');
    j.close('}');
    count(j, "models_read", sc.models_read);
    j.key("issues");
    j.open('[');
    for (const ShaderIssueEntry& e : sc.issues) {
        j.open('{');
        j.key("kind");
        j.str(e.kind);
        j.key("effect");
        j.str(e.effect);
        j.key("path");
        j.str(e.path);
        j.key("in_mod");
        j.boolean(e.in_mod);
        j.key("detail");
        j.str(e.detail);
        j.key("drawn_by");
        j.str(e.drawn_by);
        count(j, "models", e.models);
        j.key("first_model");
        j.str(e.first_model);
        j.close('}');
    }
    j.close(']');
    j.close('}');

    j.key("submod");
    j.open('{');
    j.key("likely");
    j.boolean(r.submod.likely);
    count(j, "missing_list_entries", r.submod.missing);
    j.key("parents");
    j.open('[');
    for (const ModParentCandidate& p : r.submod.parents) {
        j.open('{');
        j.key("id");
        j.str(p.id);
        j.key("kind");
        j.str(p.workshop ? "workshop" : "local");
        j.key("path");
        j.str(p.path);
        j.key("arg");
        j.str(p.arg);
        count(j, "provides", p.provides);
        j.key("mentioned");
        j.boolean(p.mentioned);
        j.close('}');
    }
    j.close(']');
    j.strings("stacked_args", r.submod.stacked_args);
    j.close('}');

    j.strings("check_errors", r.check_errors);

    // ---- version 4 (mcx0 t06, docs/MOD_COMPAT_DESIGN.md §12.1) ----
    j.key("honour_gaps");
    j.open('[');
    for (const HonourGap& g : r.honour_gaps) {
        j.open('{');
        j.key("class");
        j.str(g.cls);
        j.key("tag");
        j.str(g.tag);
        j.key("state");
        j.str(g.state);
        count(j, "uses", g.uses);
        count(j, "entries", g.entries);
        j.key("first_file");
        j.str(g.first_file);
        j.key("in_mod");
        j.boolean(g.in_mod);
        count(j, "tier", g.tier);
        count(j, "base_uses", g.base_uses);
        j.key("spec");
        j.str(g.spec);
        j.close('}');
    }
    j.close(']');
    j.key("retail_equivalent");
    j.open('{');
    count(j, "count", r.retail_equivalent.size());
    j.key("items");
    j.open('[');
    for (const RetailEquivalentFinding& f : r.retail_equivalent) {
        j.open('{');
        j.key("kind");
        j.str(f.kind);
        j.key("subject");
        j.str(f.subject);
        count(j, "uses", f.uses);
        j.key("first_file");
        j.str(f.first_file);
        j.key("in_mod");
        j.boolean(f.in_mod);
        j.key("detail");
        j.str(f.detail);
        j.close('}');
    }
    j.close(']');
    j.close('}');
    j.key("bindings");
    j.open('{');
    count(j, "called", r.binding_calls);
    j.key("unconsumed");
    j.open('[');
    for (const BindingGapCall& b : r.binding_gaps) {
        j.open('{');
        j.key("name");
        j.str(b.name);
        j.key("effect");
        j.str(b.effect);
        j.key("detail");
        j.str(b.detail);
        count(j, "call_sites", b.call_sites);
        count(j, "scripts", b.scripts);
        j.key("first_script");
        j.str(b.first_script);
        j.key("in_mod");
        j.boolean(b.in_mod);
        j.close('}');
    }
    j.close(']');
    j.close('}');
    const auto story_uses = [&](std::string_view k, const std::vector<StoryUse>& v) {
        j.key(k);
        j.open('[');
        for (const StoryUse& u : v) {
            j.open('{');
            j.key("name");
            j.str(u.name);
            count(j, "uses", u.uses);
            j.key("first_file");
            j.str(u.first_file);
            j.key("in_mod");
            j.boolean(u.in_mod);
            j.key("note");
            j.str(u.note);
            j.close('}');
        }
        j.close(']');
    };
    j.key("story");
    j.open('{');
    count(j, "files", r.story.files);
    count(j, "rewards_used", r.story.rewards_used);
    story_uses("rewards_missing", r.story.rewards_missing);
    count(j, "events_used", r.story.events_used);
    story_uses("events_partial", r.story.events_partial);
    j.close('}');
    j.key("ai");
    j.open('{');
    j.key("token_coverage");
    j.open('{');
    j.key("checked");
    j.boolean(r.token_coverage.checked);
    count(j, "equations", r.token_coverage.equations);
    count(j, "leaves", r.token_coverage.leaves);
    j.key("missing");
    j.open('[');
    for (const TokenGap& t : r.token_coverage.missing) {
        j.open('{');
        j.key("token");
        j.str(t.token);
        count(j, "equations", t.equations);
        j.key("first_equation");
        j.str(t.first_equation);
        j.key("first_file");
        j.str(t.first_file);
        j.key("in_mod");
        j.boolean(t.in_mod);
        j.close('}');
    }
    j.close(']');
    j.close('}');
    j.close('}');
    j.key("limits");
    j.open('[');
    for (const LimitHit& l : r.limits) {
        j.open('{');
        j.key("limit");
        j.str(l.limit);
        count(j, "value", l.value);
        count(j, "needed", l.needed);
        j.close('}');
    }
    j.close(']');
    if (r.assets.ran) {  // mcx2 t01 (asset_audit.hpp, MODS.md 5.1)
        const AssetAudit& a = r.assets;
        j.key("assets");
        j.open('{');
        count(j, "checked", a.checked);
        count(j, "decoded", a.decoded);
        count(j, "salvaged_count", a.salvaged_count);
        count(j, "rejected_count", a.rejected_count);
        count(j, "retail_equivalent", a.retail_equivalent);
        j.key("salvaged");
        j.open('[');
        for (const AssetEntry& e : a.salvaged) {
            j.open('{');
            j.key("path");
            j.str(e.path);
            j.key("family");
            j.str(e.family);
            j.key("rule");
            j.str(e.rule);
            j.close('}');
        }
        j.close(']');
        j.key("rejected");
        j.open('[');
        for (const AssetEntry& e : a.rejected) {
            j.open('{');
            j.key("path");
            j.str(e.path);
            j.key("family");
            j.str(e.family);
            j.key("reason");
            j.str(e.rule);
            j.key("retail_equivalent");
            j.boolean(e.retail_equivalent);
            j.close('}');
        }
        j.close(']');
        j.key("by_family");
        j.open('{');
        for (const auto& [fam, fc] : a.by_family) {
            j.key(fam);
            j.open('{');
            count(j, "checked", fc.checked);
            count(j, "decoded", fc.decoded);
            count(j, "salvaged", fc.salvaged);
            count(j, "rejected", fc.rejected);
            j.close('}');
        }
        j.close('}');
        j.close('}');
    }
    j.key("grade");
    j.open('{');
    j.key("grade");
    j.str(r.grade.grade);
    j.key("g3_static");
    j.boolean(r.grade.g3_static);
    j.strings("blocking", r.grade.blocking);
    j.strings("check_errors", r.grade.check_errors);
    j.close('}');
    j.close('}');
    j.out.push_back('\n');
    return std::move(j.out);
}

}  // namespace alamo::game
