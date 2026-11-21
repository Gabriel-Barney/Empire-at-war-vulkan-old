// format_mod_report (mod_check.hpp): the mod check's text report.
#include "alamo/game/mod_check.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/core/ascii.hpp"

namespace alamo::game {

namespace {

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

std::string format_mod_report(const ModCheckReport& r, bool verbose) {
    std::string s;
    if (r.resolved) {  // mcx3 t02
        append(s, "== stack (resolved, launch order; fingerprint %016llx)\n", static_cast<unsigned long long>(r.stack.fingerprint));
        for (const vfs::StackMod& m : r.stack.mods)
            append(s, "    %-24s %-40s %s%s%s\n", m.mod.arg().c_str(), m.mod.name.c_str(), m.reason_text().c_str(),
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

}  // namespace alamo::game
