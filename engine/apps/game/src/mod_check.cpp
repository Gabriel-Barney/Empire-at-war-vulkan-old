// check_mod_data (mod_check.hpp): loads the data as alamo_game does and runs each check family in its own file
// (mod_check_detail.hpp): files, content, XML tags, shaders, Lua, honour gaps, story, token coverage, grade. The text
// report is mod_report_text.cpp, the JSON documents mod_report_json.cpp.
#include "mod_check_detail.hpp"

#include <exception>
#include <string>
#include <vector>

#include "alamo/vfs/vfs.hpp"

namespace alamo::game {

using namespace mod_check_detail;

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
        r.mod_dirs = vfs::mod_layer_roots(*d.vfs);
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
        if (options.compat) {  // ums4 t07: the findings of a load that stopped (VFS, then the loader steps that ran)
            r.issues.merge(d.vfs_issues);
            loader_issues(d, r);
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
    if (options.files && options.submod) guarded("submod", [&] { check_submod(d, r, options); });
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
        compat_issues_and_grade(r, d.vfs, d.vfs_issues);
        loader_issues(d, r);  // ums4 t07
    } else {  // check_lua collects the binding findings with its scan; version 4 lists stay empty without compat
        r.binding_gaps.clear();
        r.retail_equivalent.clear();
        r.binding_calls = 0;
    }
    return r;
}

}  // namespace alamo::game
