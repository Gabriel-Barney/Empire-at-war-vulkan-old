// Mod check, Lua family (mod_check_detail.hpp): every Data\Scripts\**\*.lua compiled, entry scripts' main chunks run,
// unbound engine calls, GlobalValue session dependence (modc t12), missing requires (modc t24) and the binding
// effect gaps (mcx0 t06).
#include "mod_check_detail.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "alamo/core/ascii.hpp"
#include "alamo/vfs/vfs.hpp"

#ifdef ALAMO_GAME_HAVE_LUA_VM
#include <lua.h>  // Lua 5.0 is built as C++ (engine/lua/CMakeLists.txt): no extern "C"

#include "alamo/lua/bindings.hpp"
#include "alamo/lua/lua_vm.hpp"
#include "alamo/lua/script_loader.hpp"
#include "alamo/lua/script_remaining.hpp"
#endif

namespace alamo::game::mod_check_detail {

namespace {

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

}  // namespace

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

}  // namespace alamo::game::mod_check_detail
