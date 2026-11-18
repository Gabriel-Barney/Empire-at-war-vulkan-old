// alamo_game_conformance_stacks_tests (mcx3 t07): the conformance mods stack_dep, stack_cycle and eaw_only
// (testdata/conformance/stacks, README there) through resolve_mod_stack, load_game_data and check_mod_data, no game data.
// Expectations follow docs/MOD_COMPAT_DESIGN.md 5.2 (stack resolution), 5.4 / D1 (base-EaW refusal) and docs/MODS.md 3.1
// (the command line is mounted as written, earliest wins). Read only: nothing is written.
//   alamo_game_conformance_stacks_tests <testdata/conformance/stacks>
#include <cstdio>
#include <string>
#include <vector>

#include "alamo/game/game_data.hpp"
#include "alamo/game/mod_check.hpp"
#include "alamo/game/mod_picker.hpp"
#include "alamo/vfs/mod_registry.hpp"
#include "alamo/test/support.hpp"

using ::alamo::test::g_fail;

namespace {

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
    const vfs::ResolvedStack s = game::resolve_mod_stack(root, {"MODPATH=Mods\\StackSub"}, o);
    CHECK(joined(s.args) == "MODPATH=Mods\\StackSub | MODPATH=Mods\\StackParent");
    CHECK(s.errors.empty() && s.warnings.empty());
    CHECK(s.mods.size() == 2 && s.mods[1].reason == "dependency of StackSub" && s.mods[1].mod.version == "2.0");
    std::printf("  declared: %s\n", joined(s.args).c_str());
    const game::ModCheckReport r = game::check_mod_data(root, s.args, o);
    CHECK(r.loaded && r.error_count() == 0);
    CHECK(r.missing_files.empty());
    CHECK(r.mods.size() == 2 && r.mods[0].name == "Stack Sub" && r.mods[1].name == "Stack Parent");
    CHECK(r.mods.size() == 2 && r.mods[0].kind == vfs::ModKind::LocalMods && r.mods[0].content_hash != 0);
    CHECK(r.base_eaw_suspect.empty() && !r.base_eaw);

    // Undeclared: StackHinted names two files only StackParent has; the submod hint finds it.
    const vfs::ResolvedStack h = game::resolve_mod_stack(root, {"MODPATH=Mods\\StackHinted"}, o);
    CHECK(joined(h.args) == "MODPATH=Mods\\StackHinted | MODPATH=Mods\\StackParent");
    CHECK(h.mods.size() == 2 && h.mods[1].reason == "likely parent of StackHinted");
    std::printf("  hinted: %s\n", joined(h.args).c_str());
    // Alone it loads with the two list entries missing (retail too); stacked, nothing is missing.
    CHECK(game::check_mod_data(root, {"MODPATH=Mods\\StackHinted"}, o).missing_files.size() == 2);
    CHECK(game::check_mod_data(root, h.args, o).missing_files.empty());

    // The exit test's shape (AOW patch -> Legacy HD -> TR), nothing declared: from the top folder alone, two hint rounds.
    const vfs::ResolvedStack t = game::resolve_mod_stack(root, {"MODPATH=Mods\\StackTop"}, o);
    CHECK(joined(t.args) == "MODPATH=Mods\\StackTop | MODPATH=Mods\\StackHinted | MODPATH=Mods\\StackParent");
    CHECK(t.mods.size() == 3 && t.mods[1].reason == "likely parent of StackTop" && t.mods[2].reason == "likely parent of StackHinted");
    CHECK(t.errors.empty());
    CHECK(game::check_mod_data(root, t.args, o).missing_files.empty());
    std::printf("  chain: %s\n", joined(t.args).c_str());

    // Picked in the wrong order: resolution puts the submod first...
    const vfs::ResolvedStack w = game::resolve_mod_stack(root, {"MODPATH=Mods\\StackParent", "MODPATH=Mods\\StackSub"}, o);
    CHECK(joined(w.args) == "MODPATH=Mods\\StackSub | MODPATH=Mods\\StackParent");
    // ...but an explicit command line is mounted as written (earliest wins, MODS.md 3.1).
    const game::ModCheckReport e = game::check_mod_data(root, {"MODPATH=Mods\\StackParent", "MODPATH=Mods\\StackSub"}, o);
    CHECK(e.mods.size() == 2 && e.mods[0].name == "Stack Parent" && e.mods[1].name == "Stack Sub");

    // Two total conversions without a dependency between them: a warning, not an error.
    const vfs::ResolvedStack c = game::resolve_mod_stack(root, {"MODPATH=Mods\\StackParent", "MODPATH=Mods\\OtherTc"}, o);
    CHECK(c.errors.empty());
    CHECK(has(c.warnings, "two total conversions in one stack: StackParent and OtherTc"));
}

void test_stack_cycle(const std::string& root) {
    alamo::test::section("stack_cycle");
    const vfs::ResolvedStack s = game::resolve_mod_stack(root, {"MODPATH=Mods\\CycleA"}, light());
    CHECK(has(s.errors, "dependency cycle: CycleA -> CycleB -> CycleA"));
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
    const vfs::ResolvedStack s = game::resolve_mod_stack(root, {"MODPATH=Mods\\EawOnly"}, light());
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
    vfs::ResolvedStack s = game::resolve_mod_stack(root, {arg}, o);
    game::ModCheckReport r = game::check_mod_data(root, s.errors.empty() ? s.args : std::vector<std::string>{arg}, o);
    r.resolved = true;
    r.stack = std::move(s);
    return r;
}

void test_picker(const std::string& root) {
    alamo::test::section("picker");
    game::InstalledMod top;
    top.id = "StackTop";
    top.arg = "MODPATH=Mods\\StackTop";
    const game::ModCheckReport t = resolved_report(root, top.arg);
    CHECK(joined(game::mod_launch_args(top, &t)) == "MODPATH=Mods\\StackTop | MODPATH=Mods\\StackHinted | MODPATH=Mods\\StackParent");
    const std::vector<std::string> ts = game::mod_report_summary(t);
    CHECK(has(ts, "Stack: StackTop > StackHinted > Stack Parent"));
    CHECK(!has(ts, "Launch stacks it on"));  // the stack line replaces the single-parent hint
    for (const std::string& l : ts) std::printf("  %s\n", l.c_str());

    game::InstalledMod eaw;
    eaw.id = "EawOnly";
    eaw.arg = "MODPATH=Mods\\EawOnly";
    const game::ModCheckReport e = resolved_report(root, eaw.arg);
    CHECK(game::mod_launch_args(eaw, &e).empty());  // refused: nothing starts
    const std::vector<std::string> es = game::mod_report_summary(e);
    CHECK(!es.empty() && es[0] == std::string("Refused (FoC mods only): EawOnly: ") + vfs::kBaseEawRefusal);
    // Unresolved (alamo_mod_check without --resolve): the load error says it.
    const game::ModCheckReport e2 = game::check_mod_data(root, {eaw.arg}, light());
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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: alamo_game_conformance_stacks_tests <testdata/conformance/stacks>\n");
        return 2;
    }
    const std::string root = std::string(argv[1]) + "/corruption";
    test_stack_dep(root);
    test_stack_cycle(root);
    test_eaw_only(root);
    test_picker(root);
    return alamo::test::report("alamo_game_conformance_stacks_tests");
}
