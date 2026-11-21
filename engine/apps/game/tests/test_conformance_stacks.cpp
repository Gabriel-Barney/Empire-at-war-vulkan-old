// alamo_game_conformance_stacks_tests (mcx3 t07): the conformance mods stack_dep, stack_cycle and eaw_only
// (testdata/conformance/stacks, README there) through resolve_mod_stack, load_game_data and check_mod_data, no game data.
// Expectations follow docs/MOD_COMPAT_DESIGN.md 5.2 (stack resolution), 5.4 / D1 (base-EaW refusal) and docs/MODS.md 3.1
// (the command line is mounted as written, earliest wins). Nothing is written to the fixture.
// ums1 t06: the launch-argument table across the three surfaces (the in-game picker, alamo_play, alamo_mod_check
// --resolve-only run as an exe) for stack_dep, stack_cycle, eaw_only and the AOW -> Legacy HD -> TR shape, and the
// picker rows == catalog rows == alamo_mod_check --list rows. The exe's JSON goes to <work dir>.
//   alamo_game_conformance_stacks_tests <testdata/conformance/stacks> <alamo_mod_check exe> <work dir>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "alamo/game/game_data.hpp"
#include "alamo/game/mod_check.hpp"
#include "alamo/game/mod_conflicts.hpp"
#include "alamo/game/mod_picker.hpp"
#include "alamo/game/mod_resolve.hpp"
#include "alamo/vfs/install_locations.hpp"
#include "alamo/vfs/mod_registry.hpp"
#include "alamo/test/support.hpp"
#include "play_launch.hpp"

using ::alamo::test::g_fail;

namespace {

namespace fs = std::filesystem;
namespace game = alamo::game;
namespace vfs = alamo::vfs;

bool has(const std::vector<std::string>& v, const std::string& needle) {
    for (const std::string& s : v)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : " | ") + x;
    return s;
}

// Only what the stack checks need (the submod hint, the base-EaW heuristic): no Lua, shaders or compat pass.
game::ModCheckOptions light() {
    game::ModCheckOptions o;
    o.campaigns = o.lua = o.lua_run = o.xml_tags = o.shaders = o.compat = o.assets = false;
    o.shader_compile = o.fx_prebuild = false;
    return o;
}

void test_stack_dep(const std::string& root) {
    alamo::test::section("stack_dep");
    const game::ModCheckOptions o = light();
    // Declared: StackSub -> StackParent (modinfo.json with a comment and trailing commas).
    const vfs::ModStack s = game::resolve_mod_stack(root, {"MODPATH=Mods\\StackSub"}, o.workshop_libraries);
    CHECK(joined(s.args) == "MODPATH=Mods\\StackSub | MODPATH=Mods\\StackParent");
    CHECK(s.errors.empty() && s.warnings.empty());
    CHECK(s.mods.size() == 2 && s.mods[1].reason_text() == "dependency of Mods\\StackSub" && s.mods[1].mod.version == "2.0");
    std::printf("  declared: %s\n", joined(s.args).c_str());
    const game::ModCheckReport r = game::check_mod_data(root, s.args, o);
    CHECK(r.loaded && r.error_count() == 0);
    CHECK(r.missing_files.empty());
    CHECK(r.mods.size() == 2 && r.mods[0].name == "Stack Sub" && r.mods[1].name == "Stack Parent");
    CHECK(r.mods.size() == 2 && r.mods[0].kind == vfs::ModKind::LocalMods && r.mods[0].content_hash != 0);
    CHECK(r.base_eaw_suspect.empty() && !r.base_eaw);

    // Undeclared: StackHinted names two files only StackParent has; the submod hint finds it.
    const vfs::ModStack h = game::resolve_mod_stack(root, {"MODPATH=Mods\\StackHinted"}, o.workshop_libraries);
    CHECK(joined(h.args) == "MODPATH=Mods\\StackHinted | MODPATH=Mods\\StackParent");
    CHECK(h.mods.size() == 2 && h.mods[1].reason_text() == "likely parent of Mods\\StackHinted");
    std::printf("  hinted: %s\n", joined(h.args).c_str());
    // Alone it loads with the two list entries missing (retail too); stacked, nothing is missing.
    CHECK(game::check_mod_data(root, {"MODPATH=Mods\\StackHinted"}, o).missing_files.size() == 2);
    CHECK(game::check_mod_data(root, h.args, o).missing_files.empty());

    // The exit test's shape (AOW patch -> Legacy HD -> TR), nothing declared: from the top folder alone, two hint rounds.
    const vfs::ModStack t = game::resolve_mod_stack(root, {"MODPATH=Mods\\StackTop"}, o.workshop_libraries);
    CHECK(joined(t.args) == "MODPATH=Mods\\StackTop | MODPATH=Mods\\StackHinted | MODPATH=Mods\\StackParent");
    CHECK(t.mods.size() == 3 && t.mods[1].reason_text() == "likely parent of Mods\\StackTop" && t.mods[2].reason_text() == "likely parent of Mods\\StackHinted");
    CHECK(t.errors.empty());
    CHECK(game::check_mod_data(root, t.args, o).missing_files.empty());
    std::printf("  chain: %s\n", joined(t.args).c_str());

    // Picked in the wrong order: resolution puts the submod first...
    const vfs::ModStack w = game::resolve_mod_stack(root, {"MODPATH=Mods\\StackParent", "MODPATH=Mods\\StackSub"}, o.workshop_libraries);
    CHECK(joined(w.args) == "MODPATH=Mods\\StackSub | MODPATH=Mods\\StackParent");
    CHECK(has(w.warnings, "Mods\\StackParent is picked above its submod Mods\\StackSub"));
    // ...but an explicit command line is mounted as written (earliest wins, MODS.md 3.1).
    const game::ModCheckReport e = game::check_mod_data(root, {"MODPATH=Mods\\StackParent", "MODPATH=Mods\\StackSub"}, o);
    CHECK(e.mods.size() == 2 && e.mods[0].name == "Stack Parent" && e.mods[1].name == "Stack Sub");

    // Two total conversions without a dependency between them: a warning, not an error.
    const vfs::ModStack c = game::resolve_mod_stack(root, {"MODPATH=Mods\\StackParent", "MODPATH=Mods\\OtherTc"}, o.workshop_libraries);
    CHECK(c.errors.empty());
    CHECK(has(c.warnings, "two total conversions in one stack: Mods\\StackParent and Mods\\OtherTc"));
}

void test_stack_cycle(const std::string& root) {
    alamo::test::section("stack_cycle");
    const vfs::ModStack s = game::resolve_mod_stack(root, {"MODPATH=Mods\\CycleA"}, light().workshop_libraries);
    CHECK(has(s.errors, "dependency cycle: Mods\\CycleA -> Mods\\CycleB -> Mods\\CycleA"));
    CHECK(s.mods.size() == 2);
    std::printf("  %s\n", joined(s.errors).c_str());
    // In a report the stack's errors are errors.
    game::ModCheckReport r;
    r.loaded = true;
    r.resolved = true;
    r.stack = s;
    CHECK(r.error_count() == 1);
}

void test_eaw_only(const std::string& root) {
    alamo::test::section("eaw_only");
    // Tagged EAW: refused before anything loads (D1), with the mod's name.
    game::GameData d;
    std::string err;
    CHECK(!game::load_game_data(root, {"MODPATH=Mods\\EawOnly"}, d, &err));
    CHECK(err == std::string("Old EaW Mod (EawOnly): ") + vfs::kBaseEawRefusal);
    CHECK(d.stages.empty());  // no loader step ran
    std::printf("  refusal: %s\n", err.c_str());
    const game::ModCheckReport r = game::check_mod_data(root, {"MODPATH=Mods\\EawOnly"}, light());
    CHECK(!r.loaded && r.base_eaw && r.grade.grade == "G0");
    CHECK(has(r.grade.blocking, vfs::kBaseEawRefusal));
    CHECK(r.error_count() >= 1);
    // The stack refuses it too (the picker and the launcher read this).
    const vfs::ModStack s = game::resolve_mod_stack(root, {"MODPATH=Mods\\EawOnly"}, light().workshop_libraries);
    CHECK(s.base_eaw && has(s.errors, vfs::kBaseEawRefusal));

    // No tag, an object list without FoC's own file: loaded, with the heuristic's warning only.
    const game::ModCheckReport g = game::check_mod_data(root, {"MODPATH=Mods\\EawGuess"}, light());
    CHECK(g.loaded && !g.base_eaw && g.error_count() == 0);
    CHECK(g.base_eaw_suspect.rfind("EawGuess: ", 0) == 0);
    CHECK(g.base_eaw_suspect.find("FOCOBJECTS.XML") != std::string::npos);
    std::printf("  heuristic: %s\n", g.base_eaw_suspect.c_str());
    // A FoC mod whose list keeps FoC's file: no warning; the base game alone: nothing to judge.
    CHECK(game::check_mod_data(root, {"MODPATH=Mods\\StackParent"}, light()).base_eaw_suspect.empty());
    CHECK(game::check_mod_data(root, {}, light()).base_eaw_suspect.empty());
}

// The picker's side (mod_picker.hpp, mcx3 t03 / t06 / t09) on resolved reports, as App::check_mod builds them.
game::ModCheckReport resolved_report(const std::string& root, const std::string& arg) {
    const game::ModCheckOptions o = light();
    vfs::ModStack s = game::resolve_mod_stack(root, {arg}, o.workshop_libraries);
    game::ModCheckReport r = game::check_mod_data(root, s.errors.empty() ? s.args : std::vector<std::string>{arg}, o);
    r.resolved = true;
    r.stack = std::move(s);
    return r;
}

void test_picker(const std::string& root) {
    alamo::test::section("picker");
    vfs::ModDescriptor top;  // a picker row (a catalog row)
    top.kind = vfs::ModKind::LocalMods;
    top.id = "StackTop";
    const game::ModCheckReport t = resolved_report(root, top.arg());
    CHECK(joined(game::mod_launch_args(top, &t)) == "MODPATH=Mods\\StackTop | MODPATH=Mods\\StackHinted | MODPATH=Mods\\StackParent");
    const std::vector<std::string> ts = game::mod_report_summary(t);
    CHECK(has(ts, "Stack: StackTop > StackHinted > Stack Parent"));
    CHECK(!has(ts, "Launch stacks it on"));  // the stack line replaces the single-parent hint
    for (const std::string& l : ts) std::printf("  %s\n", l.c_str());

    vfs::ModDescriptor eaw;
    eaw.kind = vfs::ModKind::LocalMods;
    eaw.id = "EawOnly";
    const game::ModCheckReport e = resolved_report(root, eaw.arg());
    CHECK(game::mod_launch_args(eaw, &e).empty());  // refused: nothing starts
    const std::vector<std::string> es = game::mod_report_summary(e);
    CHECK(!es.empty() && es[0] == std::string("Refused (FoC mods only): Mods\\EawOnly: ") + vfs::kBaseEawRefusal);
    // Unresolved (alamo_mod_check without --resolve): the load error says it.
    const game::ModCheckReport e2 = game::check_mod_data(root, {eaw.arg()}, light());
    CHECK(game::mod_launch_args(eaw, &e2).empty());
    CHECK(has(game::mod_report_summary(e2), "Refused (FoC mods only): Old EaW Mod (EawOnly): "));

    // t09: languages. A modinfo.json without "languages" is English; a mod without modinfo.json says nothing.
    CHECK(game::language_code("German") == "de" && game::language_code("ENGLISH") == "en" && game::language_code("KLINGON").empty());
    const game::ModCheckReport sub = resolved_report(root, "MODPATH=Mods\\StackSub");
    CHECK(game::mod_language_warnings(sub, "ENGLISH").empty());
    const std::vector<std::string> de = game::mod_language_warnings(sub, "GERMAN");
    CHECK(de.size() == 2 && has(de, "Stack Sub lists no GERMAN text (modinfo.json languages: en)"));
    CHECK(game::mod_language_warnings(t, "GERMAN").size() == 1);  // StackTop and StackHinted have no modinfo.json
}

// ---- ums1 t06: the three launch surfaces start the same list ----

// The strings of the first `"<key>": [ ... ]` array in a JSON text (JSON escapes \\ and \" decoded); nullopt when absent.
std::optional<std::vector<std::string>> json_strings(const std::string& text, const std::string& key) {
    const size_t k = text.find("\"" + key + "\": [");
    if (k == std::string::npos) return std::nullopt;
    std::vector<std::string> out;
    for (size_t i = text.find('[', k) + 1; i < text.size() && text[i] != ']'; ++i) {
        if (text[i] != '"') continue;
        std::string s;
        for (++i; i < text.size() && text[i] != '"'; ++i) s.push_back(text[i] == '\\' && i + 1 < text.size() ? text[++i] : text[i]);
        out.push_back(std::move(s));
    }
    return out;
}

std::string read_all(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// alamo_mod_check's own output: runs the exe and returns the JSON file it wrote ("" when it did not run).
std::string run_mod_check(const std::string& exe, const std::string& args, const fs::path& out) {
    std::error_code ec;
    fs::remove(out, ec);
    const std::string cmd = "\"\"" + exe + "\" " + args + " --json \"" + out.string() + "\" >NUL 2>NUL\"";
    std::system(cmd.c_str());
    return read_all(out);
}

void test_launch_surfaces(const std::string& fixture, const std::string& root, const std::string& mod_check_exe, const fs::path& work) {
    alamo::test::section("launch surfaces (ums1 t06)");
    // One Steam library on every side, the fixture's (README): ALAMO_STEAM_ROOT makes default_steam_roots read nothing
    // else, so the exe, alamo_play's resolver and the picker (all through vfs::default_install_at) see the same install.
    _putenv_s("ALAMO_STEAM_ROOT", (fs::path(fixture) / "steamlib").string().c_str());
    const vfs::InstallLocations where = vfs::default_install_at(root);
    CHECK(where.steam_libraries.size() == 1);
    game::ModCheckOptions o = light();
    o.workshop_libraries = where.steam_libraries;
    const vfs::ModCatalog catalog = vfs::scan_mod_catalog(where, false);
    const std::vector<vfs::ModDescriptor>& rows = catalog.mods;  // ums4 t01: the picker's rows are the catalog's

    // Picker rows (catalog rows) == alamo_mod_check --list rows.
    const std::string list = run_mod_check(mod_check_exe, "--game \"" + root + "\" --list --no-hash", work / "list.json");
    CHECK(json_strings(list, "mod_folders").has_value());  // present: the file is ours
    std::vector<std::string> list_keys;
    for (size_t at = list.find("\"key\": \""); at != std::string::npos; at = list.find("\"key\": \"", at + 1)) {
        std::string k;
        for (size_t i = at + 8; i < list.size() && list[i] != '"'; ++i) k.push_back(list[i] == '\\' ? list[++i] : list[i]);
        list_keys.push_back(k);
    }
    std::vector<std::string> row_keys;
    for (const vfs::ModDescriptor& r : rows) row_keys.push_back(r.catalog_key());
    CHECK(joined(list_keys) == joined(row_keys));
    CHECK(joined(row_keys).rfind("900000201:subA | 900000201:subB | Mods\\CycleA", 0) == 0);  // variants: a row each
    std::printf("  rows: %s\n", joined(row_keys).c_str());

    // alamo_play gets the game folder spelled relative to the current folder (the catalog then holds relative roots) and
    // the dropped folder absolute: a folder is still one folder (spec review c1).
    std::error_code ec;
    const fs::path rel_root = fs::relative(fs::path(root), ec);
    const std::string play_game = ec || rel_root.empty() ? root : rel_root.string();

    struct Case {
        const char* name;
        const char* key;      // the picker row
        const char* expect;   // the launch list, " | "-joined ("" = refused)
    };
    const Case cases[] = {
        {"stack_dep (declared)", "Mods\\StackSub", "MODPATH=Mods\\StackSub | MODPATH=Mods\\StackParent"},
        {"stack_dep (hinted)", "Mods\\StackHinted", "MODPATH=Mods\\StackHinted | MODPATH=Mods\\StackParent"},
        {"stack_cycle", "Mods\\CycleA", "MODPATH=Mods\\CycleA | MODPATH=Mods\\CycleB"},
        {"eaw_only", "Mods\\EawOnly", ""},
        // AOW -> Legacy HD -> TR: the shape (nothing declared, two hint rounds); the real three are not installed here
        // (docs/MODS.md 10.4).
        {"AOW -> Legacy HD -> TR", "Mods\\StackTop", "MODPATH=Mods\\StackTop | MODPATH=Mods\\StackHinted | MODPATH=Mods\\StackParent"},
        {"workshop variant subA", "900000201:subA", "STEAMMOD=900000201 | MODPATH=Mods\\StackParent"},
        {"workshop variant subB", "900000201:subB", "STEAMMOD=900000201 | MODPATH=Mods\\OtherTc"},
    };
    for (const Case& c : cases) {
        const vfs::ModDescriptor* row = nullptr;
        for (const vfs::ModDescriptor& r : rows)
            if (r.catalog_key() == c.key) row = &r;
        CHECK(row != nullptr);
        if (!row) continue;
        // A variant is named by its key (a dropped folder means its first variant); other rows by their argument.
        const std::string named = row->variant.empty() ? row->arg() : "STEAMMOD=" + row->catalog_key();
        // 1. The in-game picker: App::check_mod's report, then Launch's list.
        const game::ModCheckReport report = game::check_picked_mod(root, row->catalog_key(), o);
        const std::string picker = joined(game::mod_launch_args(*row, &report));
        // 2. alamo_play: the folder dropped onto Play.exe (or the variant's argument), through play_main's resolver.
        const std::wstring user = row->variant.empty() ? fs::path(row->root).wstring() : fs::path(named).wstring();
        const std::vector<std::wstring> dropped = alamo::play::game_args(fs::path(play_game), work / "cache", {user});
        const auto stacked = alamo::play::stack_mod_args(
            dropped, [&](const std::vector<std::string>& picked) { return game::play_launch_args(play_game, picked); });
        std::vector<std::string> play_mods;
        if (stacked)
            for (const std::wstring& a : *stacked)
                if (vfs::is_mod_arg(fs::path(a).string())) play_mods.push_back(fs::path(a).string());
        const std::string play = joined(play_mods);
        // 3. alamo_mod_check --resolve-only (the exe): the stack section's launch list.
        const std::string js = run_mod_check(mod_check_exe, "--game \"" + root + "\" --resolve-only " + named,
                                             work / (std::string("resolve_") + std::to_string(&c - cases) + ".json"));
        const std::optional<std::vector<std::string>> resolved = json_strings(js, "launch_args");
        CHECK(resolved.has_value());
        const std::string check = resolved ? joined(*resolved) : std::string("<no JSON>");
        std::printf("  %-24s picker [%s]  play [%s]  mod_check [%s]\n", c.name, picker.c_str(), play.c_str(), check.c_str());
        CHECK(picker == c.expect);
        CHECK(play == c.expect);
        CHECK(check == c.expect);
        CHECK(stacked.has_value() == (std::string(c.expect) != ""));  // alamo_play refuses the base-EaW mod
    }
    // The full --resolve (a check of the stack, not --resolve-only) mounts the same launch list.
    {
        const std::string js = run_mod_check(mod_check_exe,
                                             "--game \"" + root + "\" --resolve --no-campaigns --no-lua --no-xml-tags --no-shaders --no-assets MODPATH=Mods\\StackSub",
                                             work / "resolve_full.json");
        const std::optional<std::vector<std::string>> la = json_strings(js, "launch_args");
        const std::optional<std::vector<std::string>> dirs = json_strings(js, "mod_dirs");
        CHECK(la && joined(*la) == "MODPATH=Mods\\StackSub | MODPATH=Mods\\StackParent");
        CHECK(dirs && dirs->size() == 2);
    }
    // The cycle still launches (an order exists), with its error shown beside it.
    CHECK(has(game::check_picked_mod(root, "Mods\\CycleA", o).stack.errors, "dependency cycle"));

    // ums1 t05: conflicts through the VFS's lookup, in-process and as the exe's JSON.
    {
        const std::vector<std::string> wrong = {"MODPATH=Mods\\StackParent", "MODPATH=Mods\\StackSub"};
        const game::ConflictReport w = game::mod_conflicts(catalog, wrong, {});
        CHECK(w.errors.empty() && w.mods.size() == 2);
        CHECK(w.mods.size() == 2 && w.mods[0].key == "Mods\\StackParent" && w.mods[0].files == 5 && w.mods[1].replaced_files == 1);
        CHECK(w.pairs.size() == 1 && w.pairs[0].winner == 0 && w.pairs[0].loser == 1 &&
              joined(w.pairs[0].files) == "DATA\\XML\\GAMEOBJECTFILES.XML" && w.pairs[0].xml_files == 1);
        bool list_found = false, sub = false;
        for (const game::ConflictFinding& f : w.findings) {
            std::printf("    %s %s: %s\n", f.warning ? "warning" : "info", game::conflict_kind_name(f.kind), f.text.c_str());
            list_found = list_found || (f.kind == game::ConflictKind::ListIgnored && f.mods == std::vector<size_t>{1, 0} && !f.lost.empty());
            sub = sub || (f.kind == game::ConflictKind::SubmodOrder && f.mods == std::vector<size_t>{1, 0} && f.order);
        }
        CHECK(list_found && sub);
        // Two total conversions: the second's two files are both won by the first (no effect); its object list names
        // nothing the first's does not, so that list finding is a note.
        const game::ConflictReport t = game::mod_conflicts(catalog, std::vector<std::string>{"MODPATH=Mods\\StackParent", "MODPATH=Mods\\OtherTc"}, {});
        bool no_effect = false, note = false;
        for (const game::ConflictFinding& f : t.findings) {
            no_effect = no_effect || (f.kind == game::ConflictKind::NoEffect && f.mods.front() == 1 && f.order);
            note = note || (f.kind == game::ConflictKind::ListIgnored && !f.warning && f.lost.empty());
        }
        CHECK(no_effect && note);
        CHECK(!t.findings.empty() && t.findings.back().kind == game::ConflictKind::ListIgnored);  // notes after warnings
        // The exe writes the same report.
        const std::string js = run_mod_check(mod_check_exe, "--game \"" + root + "\" --conflicts " + wrong[0] + " " + wrong[1], work / "conflicts.json");
        CHECK(js == game::format_conflicts_json(w));
        CHECK(js.find("\"schema\": \"alamo.mod_conflicts\"") != std::string::npos);
    }
    _putenv_s("ALAMO_STEAM_ROOT", "");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: alamo_game_conformance_stacks_tests <testdata/conformance/stacks> <alamo_mod_check exe> <work dir>\n");
        return 2;
    }
    const std::string root = std::string(argv[1]) + "/corruption";
    test_stack_dep(root);
    test_stack_cycle(root);
    test_eaw_only(root);
    test_picker(root);
    std::error_code ec;
    fs::create_directories(fs::path(argv[3]), ec);  // the exe's JSON goes there; a fresh build tree has no such folder
    test_launch_surfaces(argv[1], root, argv[2], fs::path(argv[3]));
    return alamo::test::report("alamo_game_conformance_stacks_tests");
}
