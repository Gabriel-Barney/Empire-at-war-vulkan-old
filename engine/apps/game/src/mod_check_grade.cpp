// Mod check, grade family (mod_check_detail.hpp, report v4 / v5): the retail-equivalent findings, the CompatIssue
// records, the static grade (docs/MOD_COMPAT_DESIGN.md §12.2) and the loader diagnostics as issues (ums4 t07).
#include "mod_check_detail.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/core/ascii.hpp"
#include "alamo/vfs/vfs.hpp"

namespace alamo::game::mod_check_detail {

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

}  // namespace alamo::game::mod_check_detail

namespace alamo::game {

// ums4 t07 (mod_check.hpp): the path ends at a character no game path here has (':', ',', ';', a quote, a bracket, white
// space).
std::string loader_diagnostic_file(std::string_view text, const vfs::Vfs* v) {
    const auto stop = [](char c) {
        return c == ':' || c == ',' || c == ';' || c == '"' || c == '\'' || c == '(' || c == ')' || c == '[' || c == ']' ||
               c == '<' || c == '>' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    const auto word_start = [&](size_t i) { return i == 0 || stop(text[i - 1]); };
    for (size_t i = 0; i + 5 <= text.size(); ++i) {
        const bool after_sep = i > 0 && (text[i - 1] == '\\' || text[i - 1] == '/');  // the Data folder of a host path
        if (!(word_start(i) || after_sep) || !ascii_iequals(text.substr(i, 4), "data") || (text[i + 4] != '\\' && text[i + 4] != '/'))
            continue;
        size_t e = i;
        while (e < text.size() && !stop(text[e])) ++e;
        while (e > i && text[e - 1] == '.') --e;  // a sentence's full stop
        return normalize_name(text.substr(i, e - i));
    }
    for (size_t i = 0; i < text.size(); ++i) {  // a bare XML file name: the loaders' lists name Data\XML files
        if (!word_start(i)) continue;
        size_t e = i;
        while (e < text.size() && !stop(text[e])) ++e;
        const std::string_view w = text.substr(i, e - i);
        if (w.size() > 4 && ascii_iequals(w.substr(w.size() - 4), ".xml") && w.find_first_of("\\/") == std::string_view::npos) {
            std::string path = normalize_name("Data\\XML\\" + std::string(w));
            if (v && v->exists(path)) return path;
        }
        i = e;
    }
    return {};
}

}  // namespace alamo::game

namespace alamo::game::mod_check_detail {

// ums4 t07: GameData::diagnostics as CompatIssues, one record per distinct line (subject = the line, detail = its
// loader step), with the layer of the file the line names (loader_diagnostic_file), else kNoLayer. Warnings, as the
// text report counts them; added after the grade is set, so they change no grade (as the VFS's findings, ums2 t03).
void loader_issues(const GameData& d, ModCheckReport& r) {
    for (const GameData::LoadStage& s : d.stages)
        for (size_t i = s.first_diag; i < s.first_diag + s.diag_count && i < d.diagnostics.size(); ++i) {
            const std::string& text = d.diagnostics[i];
            const std::string file = loader_diagnostic_file(text, d.vfs);
            uint16_t layer = alamo::kNoLayer;
            if (d.vfs && !file.empty())
                if (const auto res = d.vfs->resolve(file))
                    if (const vfs::Layer* l = d.vfs->layer_of(*res)) layer = l->issue_layer();
            r.issues.add(alamo::IssueKind::LoaderDiagnostic, alamo::IssueSeverity::Warning, text, file, "loader step " + s.name,
                         false, layer);
        }
}

// The issue records of report v4 and the static grade (§12.2): an error gives G0, else G1 (G2 and up need runs). The
// static part of G3 holds when no honour gap, binding gap, missing reward or missing token comes from the mod's files.
// ums2 t05: a finding from the mod's files carries the layer that serves `file` (compat_issue.hpp: the owning mod's
// index in the search list, so AotR under AotCW > AotR is layer 1); 0 when the file does not resolve to a mod layer.
void compat_issues_and_grade(ModCheckReport& r, const vfs::Vfs* v, const alamo::IssueSink& vfs_issues) {
    using alamo::IssueKind;
    using alamo::IssueSeverity;
    const auto layer = [v](bool in_mod, std::string_view file) -> uint16_t {
        if (!in_mod) return alamo::kBaseLayer;
        if (v && !file.empty())
            if (const auto res = v->resolve(file))
                if (const vfs::Layer* l = v->layer_of(*res); l && l->from_mod()) return l->issue_layer();
        return 0;
    };
    for (const HonourGap& g : r.honour_gaps)
        r.issues.add(IssueKind::HonourGap, IssueSeverity::Info, g.cls + "." + g.tag, g.first_file,
                     g.state + ", " + std::to_string(g.uses) + " uses", false, layer(g.in_mod, g.first_file));
    for (const BindingGapCall& b : r.binding_gaps)
        r.issues.add(IssueKind::BindingUnconsumed, IssueSeverity::Info, b.name, b.first_script, b.effect, false, layer(b.in_mod, b.first_script));
    for (const StoryUse& u : r.story.rewards_missing)
        r.issues.add(IssueKind::RewardMissing, IssueSeverity::Info, u.name, u.first_file, u.note, false, layer(u.in_mod, u.first_file));
    for (const LimitHit& l : r.limits)  // mcx5 t05: a guard the data reaches (the session refuses it)
        r.issues.add(IssueKind::LimitHit, IssueSeverity::Error, l.limit, "", "value " + std::to_string(l.value) + ", needed " +
                     std::to_string(l.needed), false, alamo::kNoLayer);
    for (const TokenGap& t : r.token_coverage.missing)
        r.issues.add(IssueKind::TokenMissing, IssueSeverity::Info, t.token, t.first_file, t.first_equation, false, layer(t.in_mod, t.first_file));
    // mcx6 t08: a main chunk the runaway guard stopped (LuaVmOptions::script_budget; its error says script_runaway).
    for (const std::vector<LuaIssue>* list : {&r.lua_run_errors, &r.lua_session_errors})
        for (const LuaIssue& e : *list)
            if (e.error.find("script_runaway") != std::string::npos)
                r.issues.add(IssueKind::ScriptRunaway, IssueSeverity::Warning, e.path, e.path, e.error, false, layer(e.in_mod, e.path));
    std::stable_sort(r.retail_equivalent.begin(), r.retail_equivalent.end(),
                     [](const RetailEquivalentFinding& a, const RetailEquivalentFinding& b) {
                         if (a.kind != b.kind) return a.kind < b.kind;
                         if (a.uses != b.uses) return a.uses > b.uses;
                         return a.subject < b.subject;
                     });
    for (const RetailEquivalentFinding& f : r.retail_equivalent)
        r.issues.add(IssueKind::RetailEquivalent, IssueSeverity::Info, f.kind + " " + f.subject, f.first_file, f.detail, true,
                     layer(f.in_mod, f.first_file));
    // mcx2 t01: assets the mod layers serve (asset_audit.hpp). A retail-equivalent rejection never blocks G3.
    for (const AssetEntry& e : r.assets.rejected)
        r.issues.add(IssueKind::AssetRejected, e.retail_equivalent ? IssueSeverity::Info : IssueSeverity::Warning, e.path, e.path,
                     e.family + ": " + e.rule, e.retail_equivalent, layer(true, e.path));
    for (const AssetEntry& e : r.assets.salvaged)
        r.issues.add(IssueKind::AssetSalvaged, IssueSeverity::Info, e.path, e.path, e.family + ": " + e.rule, false, layer(true, e.path));
    r.grade.grade = r.error_count() ? "G0" : "G1";
    for (const alamo::CompatIssue& i : r.issues.issues())
        if (!i.retail_equivalent && i.layer != alamo::kBaseLayer &&
            (i.kind == IssueKind::HonourGap || i.kind == IssueKind::BindingUnconsumed || i.kind == IssueKind::RewardMissing ||
             i.kind == IssueKind::TokenMissing || i.kind == IssueKind::AssetRejected))
            r.grade.blocking.push_back(std::string(alamo::issue_kind_name(i.kind)) + " " + i.subject);
    for (const AssetEntry& e : r.assets.salvaged)  // design 12.2: a salvage no retail verdict backs keeps G3 open
        if (!asset_salvage_confirmed(e.rule)) r.grade.blocking.push_back("asset_salvaged " + e.path + " (salvage unconfirmed)");
    r.grade.g3_static = r.grade.grade != "G0" && r.grade.blocking.empty() && r.grade.check_errors.empty();
    // ums2 t03 / t05: the VFS's own findings (ModEnvironment::open), each with the layer that holds it: missing list
    // entries and folders, unreadable MEGs, and the malformed MEG tables the port reads as the game does (mcx3 t04,
    // retail-equivalent). Listed after the grade is set, so they change no grade (the refactor moves findings, it does
    // not regrade; whether a VFS finding should block G3 is for ums5's re-evaluation).
    r.issues.merge(vfs_issues);
}

}  // namespace alamo::game::mod_check_detail
