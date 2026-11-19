// alamo_game_mod_picker_tests (modc t08): the mod picker's data side (mod_picker.hpp) on the read-only fixture of
// testdata/mod_fixture (its Mods folders and fake Steam library): the installed-mod list (catalog rows), the report
// summary, the launch arguments of a mod and of a likely submod (stacked on its parent), and the relaunch command line.
// The modinfo.json reader is vfs's (alamo_vfs_registry_tests); the rows are vfs::ModCatalog's (ums1 t03).
//   alamo_game_mod_picker_tests <testdata/mod_fixture>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "alamo/game/mod_check.hpp"
#include "alamo/game/mod_picker.hpp"
#include "alamo/test/support.hpp"
#include "alamo/vfs/install_locations.hpp"
#include "alamo/vfs/mod_registry.hpp"
#include "alamo/vfs/steam_workshop.hpp"
#include "alamo/vfs/vfs.hpp"  // is_mod_arg

namespace game = alamo::game;
namespace fs = std::filesystem;

using ::alamo::test::g_fail;

namespace {

bool has_line(const std::vector<std::string>& lines, const std::string& needle) {
    for (const std::string& l : lines)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}

// The picker's rows (ums4 t01: the catalog's rows themselves): `foc` with these Steam libraries, no content hashes.
std::vector<alamo::vfs::ModDescriptor> picker_rows(const std::string& foc, std::vector<std::string> libs) {
    alamo::vfs::InstallLocations where;
    where.foc = foc;
    where.steam_libraries = std::move(libs);
    return alamo::vfs::scan_mod_catalog(where, false).mods;
}

void test_list(const std::string& fixture, const std::string& foc) {
    using alamo::vfs::ModKind;
    const std::vector<std::string> libs = {fixture + "/steamlib", fixture + "/steamlib"};  // a duplicate library: each id once
    const std::vector<alamo::vfs::ModDescriptor> mods = picker_rows(foc, libs);
    CHECK(mods.size() == 4);
    if (mods.size() != 4) return;
    CHECK(mods[0].id == "900000001" && mods[0].kind == ModKind::Workshop && mods[0].name == "900000001" &&
          mods[0].arg() == "STEAMMOD=900000001");
    CHECK(mods[1].id == "FixtureChecks" && mods[1].kind == ModKind::LocalMods && mods[1].arg() == "MODPATH=Mods\\FixtureChecks");
    CHECK(mods[2].id == "FixtureMod" && mods[2].name == "FixtureMod");
    CHECK(mods[3].id == "FixtureSub" && mods[3].name == "Fixture Sub");  // modinfo.json "name"
    // ums1 t03: the rows are the catalog's, keyed by catalog key.
    CHECK(mods[0].catalog_key() == "900000001" && mods[1].catalog_key() == "Mods\\FixtureChecks" && mods[3].variant.empty());
    CHECK(picker_rows(foc, {}).size() == 3);
    CHECK(picker_rows(fixture + "/no_such_game", {}).empty());
}

void test_args() {
    CHECK(alamo::vfs::is_mod_arg("MODPATH=Mods\\X") && alamo::vfs::is_mod_arg("modpath=x") && alamo::vfs::is_mod_arg("SteamMod=1"));
    CHECK(!alamo::vfs::is_mod_arg("FALLBACKPATH=x") && !alamo::vfs::is_mod_arg("--game") && !alamo::vfs::is_mod_arg("MODPATH"));
    const std::vector<std::string> argv = {"alamo_game.exe", "--game", "C:\\Games\\FoC", "MODPATH=Mods\\Old", "--no-intro",
                                           "steammod=123", "FALLBACKPATH=x"};
    const std::vector<std::string> mod = {"STEAMMOD=456", "STEAMMOD=789"};
    CHECK((game::relaunch_args(argv, mod) ==
           std::vector<std::string>{"alamo_game.exe", "--game", "C:\\Games\\FoC", "--no-intro", "FALLBACKPATH=x", "STEAMMOD=456", "STEAMMOD=789"}));
    CHECK((game::relaunch_args(argv, {}) ==
           std::vector<std::string>{"alamo_game.exe", "--game", "C:\\Games\\FoC", "--no-intro", "FALLBACKPATH=x"}));
    // CommandLineToArgvW quoting: spaces, quotes, trailing backslashes, empty arguments.
    const std::vector<std::string> q = {"C:\\Program Files\\a.exe", "plain", "", "say \"hi\"", "C:\\dir with space\\", "a\\\\b"};
    CHECK(game::windows_command_line(q) ==
          "\"C:\\Program Files\\a.exe\" plain \"\" \"say \\\"hi\\\"\" \"C:\\dir with space\\\\\" a\\\\b");
}

void test_report(const std::string& fixture, const std::string& foc) {
    const std::vector<alamo::vfs::ModDescriptor> mods = picker_rows(foc, {fixture + "/steamlib"});
    if (mods.size() != 4) return;
    // A plain mod: its own argument; the summary says it loads cleanly.
    const game::ModCheckReport plain = game::check_mod_data(foc, {mods[2].arg()});
    CHECK((game::mod_launch_args(mods[2], &plain) == std::vector<std::string>{"MODPATH=Mods\\FixtureMod"}));
    CHECK((game::mod_launch_args(mods[2], nullptr) == std::vector<std::string>{"MODPATH=Mods\\FixtureMod"}));
    const std::vector<std::string> ps = game::mod_report_summary(plain);
    CHECK(!ps.empty() && ps[0].rfind("Loads: OK, 0 errors", 0) == 0);
    CHECK(has_line(ps, "factions") && has_line(ps, "Lua: ") && !has_line(ps, "Submod"));
    CHECK(plain.shaders.checked == has_line(ps, "Shaders: "));  // report v3: the effect audit line when it ran
    // A likely submod: launched stacked on its parent (the report's stacked arguments).
    game::ModCheckOptions opt;
    opt.workshop_libraries = {fixture + "/steamlib"};
    const game::ModCheckReport sub = game::check_mod_data(foc, {mods[3].arg()}, opt);
    CHECK((game::mod_launch_args(mods[3], &sub) == std::vector<std::string>{"MODPATH=Mods\\FixtureSub", "MODPATH=Mods\\FixtureMod"}));
    const std::vector<std::string> ss = game::mod_report_summary(sub);
    CHECK(has_line(ss, "Submod of FixtureMod (Mods folder): it has 2 of 2 missing files"));
    CHECK(has_line(ss, "Launch stacks it on MODPATH=Mods\\FixtureMod"));
    CHECK(has_line(ss, "2 missing file(s), 2 named by the mod"));
    if (g_fail)
        for (const std::string& l : ss) std::printf("  %s\n", l.c_str());
    // A load that fails says so first.
    const game::ModCheckReport bad = game::check_mod_data(fixture + "/no_such_game", {});
    const std::vector<std::string> bs = game::mod_report_summary(bad);
    CHECK(!bad.loaded && !bs.empty() && bs[0].rfind("Does not load: ", 0) == 0);
}

// ums1 exit test, real install (ALAMO_FOC_DATA, skipped when unset): the picker's rows are the catalog's rows for every
// installed mod, a folder with modinfo variants (2083545253: subA / subB, variant files only) included.
void test_installed_rows() {
    const char* foc = std::getenv("ALAMO_FOC_DATA");
    if (!foc || !*foc) {
        alamo::test::print_skip("installed rows");
        return;
    }
    const alamo::vfs::InstallLocations where = alamo::vfs::default_install_at(foc);
    const alamo::vfs::ModCatalog catalog = alamo::vfs::scan_mod_catalog(where, false);
    const std::vector<alamo::vfs::ModDescriptor>& rows = catalog.mods;  // ums4 t01: the picker's rows are these
    size_t variants = 0;
    for (const alamo::vfs::ModDescriptor& d : rows) variants += d.variant.empty() ? 0u : 1u;
    std::printf("  installed: %zu rows (%zu variants) from %zu Steam libraries\n", rows.size(), variants, where.steam_libraries.size());
    // The two-variant folder, when this machine has it: one row per variant, no row for the folder itself.
    if (alamo::vfs::find_workshop_item(where.steam_libraries, "2083545253")) {
        size_t sub = 0, main = 0;
        for (const alamo::vfs::ModDescriptor& r : rows) {
            if (r.id != "2083545253") continue;
            const std::string key = r.catalog_key();
            sub += (key == "2083545253:subA" || key == "2083545253:subB") ? 1u : 0u;
            main += r.variant.empty() ? 1u : 0u;
            std::printf("    %s  %s  %s\n", key.c_str(), r.name.c_str(), r.arg().c_str());
        }
        CHECK(sub == 2 && main == 0);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: alamo_game_mod_picker_tests <testdata/mod_fixture>\n");
        return 2;
    }
    const std::string fixture = argv[1], foc = fixture + "/corruption";
    test_list(fixture, foc);
    test_args();
    test_report(fixture, foc);
    test_installed_rows();
    std::printf(g_fail ? "alamo_game_mod_picker_tests: %d FAILED\n" : "alamo_game_mod_picker_tests: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
