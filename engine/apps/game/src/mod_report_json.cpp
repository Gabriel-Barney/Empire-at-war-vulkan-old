// The mod check's JSON report writer: one writer (Json below) and the four documents the Command Center and the corpus
// runner read. Schemas, each with a `schema` / `version` pair; keys are written in the order listed there:
//   alamo.mod_report  v5  format_mod_report_json   docs/MODS.md §5.1 (the load report, alamo_mod_check [--resolve] --json)
//   alamo.mod_list    v1  format_mod_list_json     docs/MODS.md §5.1.1 (alamo_mod_check --list-mods: the corpus runner)
//   alamo.mod_catalog v1  format_mod_catalog_json  docs/MODS.md §5.1.1, mod_check.hpp (alamo_mod_check --list: launcher catalog.rs)
//   alamo.mod_conflicts v1 format_conflicts_json   docs/MODS.md §5.1.1, mod_conflicts.hpp (--conflicts: launcher conflicts.rs)
// A key is never removed or changed in meaning without raising that document's version (docs/MODS.md §5.1).
#include "alamo/game/mod_check.hpp"
#include "alamo/game/mod_conflicts.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "alamo/core/host_fs.hpp"
#include "alamo/vfs/install_locations.hpp"
#include "alamo/vfs/mod_registry.hpp"
#include "alamo/vfs/steam_workshop.hpp"  // kFocSteamAppId
#include "json_text.hpp"  // the JSON string escaper, shared with benchmark.cpp

namespace alamo::game {

namespace fs = std::filesystem;

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

std::string format_mod_list_json(const vfs::ModCatalog& registry) {
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
        j.str(vfs::mod_kind_name(m.kind));
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
        j.str(vfs::game_tag_name(m.game_tag));
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

std::string format_mod_catalog_json(const vfs::ModCatalog& catalog, const vfs::InstallLocations& where) {
    Json j;
    j.open('{');
    j.key("schema");
    j.str("alamo.mod_catalog");
    count(j, "version", 1);
    j.key("install");
    j.open('{');
    j.key("foc");
    j.str(where.foc);
    j.key("eaw");
    j.str(where.eaw);
    j.key("mods_dir");
    j.str(where.mods_dir());
    j.strings("steam_roots", where.steam_roots);
    j.strings("steam_libraries", where.steam_libraries);
    std::vector<std::string> workshop;
    for (const std::string& lib : where.steam_libraries) {  // UTF-8 paths (workshop_content_dir would read them as ANSI)
        const fs::path dir = path_from_utf8(lib) / "steamapps" / "workshop" / "content" / std::string(vfs::kFocSteamAppId);
        std::error_code ec;
        if (fs::is_directory(dir, ec)) workshop.push_back(path_to_utf8(dir));
    }
    j.strings("workshop_dirs", workshop);
    j.strings("mod_folders", where.mod_folders);
    j.close('}');
    j.key("mods");
    j.open('[');
    for (const vfs::ModDescriptor& m : catalog.mods) {
        j.open('{');
        j.key("key");
        j.str(m.catalog_key());
        j.key("save_key");
        j.str(m.key());
        j.key("arg");
        j.str(m.arg());
        j.key("kind");
        j.str(vfs::mod_kind_name(m.kind));
        j.key("id");
        j.str(m.id);
        j.key("variant");
        j.str(m.variant);
        j.key("root");
        j.str(m.root);
        j.key("name");
        j.str(m.name);
        j.key("named");
        j.boolean(m.named);
        j.key("summary");
        j.str(m.summary);
        j.key("version");
        j.str(m.version);
        j.key("icon");
        j.str(m.icon);
        j.key("published_id");
        j.str(m.published_id);
        j.key("has_modinfo");
        j.boolean(m.has_modinfo);
        j.key("has_data");
        j.boolean(m.has_data);
        j.key("game_tag");
        j.str(vfs::game_tag_name(m.game_tag));
        char hex[24];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(m.content_hash));
        j.key("content_hash");
        j.str(hex);
        j.key("languages");
        j.open('[');
        for (const vfs::ModLanguage& l : m.languages) {
            j.open('{');
            j.key("code");
            j.str(l.code);
            count(j, "support", l.support);
            j.close('}');
        }
        j.close(']');
        j.key("layout");
        j.str(m.layout == vfs::DependencyLayout::ResolveLastItem ? "ResolveLastItem"
              : m.layout == vfs::DependencyLayout::FullResolved  ? "FullResolved"
                                                                 : "ResolveRecursive");
        j.key("dependencies");
        j.open('[');
        for (const vfs::ModDependency& dep : m.dependencies) {
            j.open('{');
            count(j, "modtype", dep.modtype);
            j.key("identifier");
            j.str(dep.identifier);
            j.key("name");
            j.str(dep.name);
            j.close('}');
        }
        j.close(']');
        std::vector<std::string> dependency_roots;  // ums4: the installed folders the declared dependencies name
        for (const vfs::ModDependency& dep : m.dependencies)
            if (const vfs::ModDescriptor* p = catalog.find_dependency(dep)) dependency_roots.push_back(p->root);
        j.strings("dependency_roots", dependency_roots);
        j.strings("modinfo_numbers", m.modinfo_numbers);  // ums4: the digit runs of its modinfo.json (submod mentions)
        j.strings("warnings", m.warnings);
        j.close('}');
    }
    j.close(']');
    j.close('}');
    j.out.push_back('\n');
    return std::move(j.out);
}

std::string format_conflicts_json(const ConflictReport& r) {
    Json j;
    j.open('{');
    j.key("schema");
    j.str("alamo.mod_conflicts");
    count(j, "version", 1);
    j.key("game");
    j.str(r.game);
    j.strings("args", r.args);
    j.key("mods");
    j.open('[');
    for (const ConflictMod& m : r.mods) {
        j.open('{');
        j.key("key");
        j.str(m.key);
        j.key("name");
        j.str(m.name);
        j.key("root");
        j.str(m.root);
        j.key("arg");
        j.str(m.arg);
        j.key("exists");
        j.boolean(m.exists);
        j.key("has_data");
        j.boolean(m.has_data);
        count(j, "files", m.files);
        count(j, "megs", m.megs);
        count(j, "replaced_files", m.replaced_files);
        count(j, "base_wins", m.base_wins);
        j.close('}');
    }
    j.close(']');
    j.key("pairs");
    j.open('[');
    for (const ConflictPair& p : r.pairs) {
        j.open('{');
        count(j, "winner", p.winner);
        count(j, "loser", p.loser);
        count(j, "xml_files", p.xml_files);
        j.strings("files", p.files);
        j.close('}');
    }
    j.close(']');
    j.key("findings");
    j.open('[');
    for (const ConflictFinding& f : r.findings) {
        j.open('{');
        j.key("kind");
        j.str(conflict_kind_name(f.kind));
        j.key("severity");
        j.str(f.warning ? "warning" : "info");
        j.key("text");
        j.str(f.text);
        j.key("mods");
        j.open('[');
        for (const size_t i : f.mods) j.num(i);
        j.close(']');
        j.key("order");
        j.boolean(f.order);
        j.strings("lost", f.lost);
        j.close('}');
    }
    j.close(']');
    j.strings("errors", r.errors);
    j.close('}');
    j.out.push_back('\n');
    return std::move(j.out);
}

std::string format_mod_report_json(const ModCheckReport& r) {
    Json j;
    j.open('{');
    j.key("schema");
    j.str("alamo.mod_report");
    count(j, "version", 5);  // ums4 t02: files.vfs_warnings gone (the VFS findings are `issues` records)
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
        j.str(vfs::game_tag_name(m.game_tag));
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
    // ums1 t04: what a launch starts (vfs::ModStack::launch_args; empty when refused), the picker's and alamo_play's list.
    j.strings("launch_args", r.resolved ? r.stack.launch_args() : std::vector<std::string>{});
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
        j.str(m.reason_text());
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
        j.key("custom");
        j.boolean(m.custom);
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
    // ums2 t03: every CompatIssue record in first-add order (the version 4 lists above are views of it, plus the VFS's
    // findings). layer: the owning mod's index in the search list, "base", or "none".
    j.key("issues");
    j.open('[');
    for (const alamo::CompatIssue& i : r.issues.issues()) {
        j.open('{');
        j.key("kind");
        j.str(alamo::issue_kind_name(i.kind));
        j.key("severity");
        j.str(alamo::issue_severity_name(i.severity));
        j.key("subject");
        j.str(i.subject);
        j.key("source");
        j.str(i.source);
        j.key("layer");
        if (i.layer == alamo::kBaseLayer) j.str("base");
        else if (i.layer == alamo::kNoLayer) j.str("none");
        else j.num(i.layer);
        j.key("retail_equivalent");
        j.boolean(i.retail_equivalent);
        count(j, "count", i.count);
        j.key("detail");
        j.str(i.detail);
        j.close('}');
    }
    j.close(']');
    j.close('}');
    j.out.push_back('\n');
    return std::move(j.out);
}

}  // namespace alamo::game
