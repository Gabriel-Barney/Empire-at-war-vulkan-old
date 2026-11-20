// alamo_game_open_tests (ref4 t05): open_game_data (open_game_data.hpp), the one way alamo_game and every alamo_headless
// runner open the game's data, on the synthetic mod fixture (testdata/mod_fixture, no game data): accepted (with and
// without a mod), refused (a base-EaW mod, written to a temp folder of this test), no folder (neither a directory nor
// ALAMO_FOC_DATA) and a folder that does not open.
//   alamo_game_open_tests <testdata/mod_fixture>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "alamo/core/host_fs.hpp"
#include "alamo/game/open_game_data.hpp"
#include "alamo/test/support.hpp"

namespace fs = std::filesystem;
using alamo::game::OpenedGameData;
using Status = alamo::game::OpenedGameData::Status;

namespace {

void set_env(const char* name, const std::string& value) {
#ifdef _MSC_VER
    _putenv_s(name, value.c_str());  // "" removes it
#else
    if (value.empty()) unsetenv(name);
    else setenv(name, value.c_str(), 1);
#endif
}

std::string get_env(const char* name) {
#ifdef _MSC_VER
    char* v = nullptr;
    size_t len = 0;
    std::string out;
    if (_dupenv_s(&v, &len, name) == 0 && v) {
        out = v;
        std::free(v);
    }
    return out;
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: alamo_game_open_tests <testdata/mod_fixture>\n");
        return 2;
    }
    const std::string foc = alamo::path_to_utf8(fs::path(alamo::path_from_utf8(argv[1])) / "corruption");
    const std::string saved_foc = get_env("ALAMO_FOC_DATA"), saved_eaw = get_env("ALAMO_EAW_DATA");
    alamo::IssueSink issues;

    alamo::test::section("accepted: the fixture's FoC folder, without and with its mod");
    {
        OpenedGameData d = alamo::game::open_game_data(foc, {}, issues);
        CHECK(d.ok() && d.status == Status::Opened && d.env && d.root == foc && d.error.empty());
        CHECK(d.env && d.env->mounted().empty());
        const std::vector<std::string> args{"MODPATH=Mods/FixtureMod"};
        OpenedGameData m = alamo::game::open_game_data(foc, args, issues);
        CHECK(m.ok() && m.env && m.env->mounted().size() == 1);
    }

    alamo::test::section("refused: a base-EaW mod in the stack (D1)");
    {
        const fs::path tmp = fs::temp_directory_path() / "alamo_game_open_tests" / "OldEaw";
        std::error_code ec;
        fs::remove_all(tmp.parent_path(), ec);
        CHECK(alamo::test::write_file(tmp / "Data" / "XML" / "Old.xml", "<Old/>"));
        CHECK(alamo::test::write_file(tmp / "modinfo.json", R"({"name": "Old EaW Mod", "steamdata": {"tags": ["EAW"]}})"));
        const std::vector<std::string> args{"MODPATH=" + alamo::path_to_utf8(tmp)};
        OpenedGameData d = alamo::game::open_game_data(foc, args, issues);
        CHECK(!d.ok() && d.status == Status::Refused);
        CHECK(d.env != nullptr && d.env->refused());  // kept, so a report can name the mod
        CHECK(d.error.find("Old EaW Mod") != std::string::npos);
        fs::remove_all(tmp.parent_path(), ec);
    }

    alamo::test::section("no data: no folder and no FoC folder variable; the variable is the fallback");
    {
        set_env("ALAMO_FOC_DATA", "");
        OpenedGameData d = alamo::game::open_game_data("", {}, issues);
        CHECK(d.status == Status::NoData && !d.env && d.root.empty());
        CHECK(alamo::game::game_data_root("").empty() && alamo::game::game_data_root("X:/foc") == "X:/foc");
        set_env("ALAMO_FOC_DATA", foc);
        CHECK(alamo::game::game_data_root("") == foc);
        OpenedGameData e = alamo::game::open_game_data("", {}, issues);
        CHECK(e.ok() && e.root == foc);
        set_env("ALAMO_EAW_DATA", "");
        CHECK(!alamo::game::eaw_data_from_env());
        set_env("ALAMO_EAW_DATA", "Y:/eaw");
        CHECK(alamo::game::eaw_data_from_env() == std::optional<std::string>("Y:/eaw"));
        set_env("ALAMO_FOC_DATA", saved_foc);
        set_env("ALAMO_EAW_DATA", saved_eaw);
    }

    alamo::test::section("a folder that does not open: the VFS error");
    {
        const std::string missing = alamo::path_to_utf8(fs::temp_directory_path() / "alamo_game_open_tests_no_such_folder");
        OpenedGameData d = alamo::game::open_game_data(missing, {}, issues);
        CHECK(!d.ok() && d.status == Status::VfsError && !d.env && !d.error.empty());
    }
    return alamo::test::report("alamo_game_open_tests");
}
