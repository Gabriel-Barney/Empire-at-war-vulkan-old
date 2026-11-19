// alamo_vfs_steam_tests (modc t10): the STEAMMOD= resolver without the Steam API (steam_workshop.hpp) on a synthetic
// Steam layout in its own temp folder: a client folder whose libraryfolders.vdf lists two more libraries, the FoC
// install inside one library, workshop items spread over the libraries. No real Steam folder is read (the process-wide
// lookup is installed with explicit roots). ums1 t01: InstallLocations (install_locations.hpp) on the same layout.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "alamo/core/file_provider.hpp"
#include "alamo/core/host_fs.hpp"
#include "alamo/vfs/install_locations.hpp"
#include "alamo/vfs/steam_workshop.hpp"
#include "alamo/vfs/vfs.hpp"
#include "alamo/test/support.hpp"

namespace fs = std::filesystem;
namespace av = alamo::vfs;

using ::alamo::test::g_fail;

namespace {

using ::alamo::test::write_file;

std::string u8(const fs::path& p) {
    std::string s = alamo::path_to_utf8(p);
    for (char& c : s)
        if (c == '/') c = '\\';
    return s;
}

// "C:\x" -> "C:\\x" (KeyValues escaping, as Steam writes it).
std::string vdf_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\\' || c == '"') o.push_back('\\');
        o.push_back(c);
    }
    return o;
}

std::string read_str(const av::Vfs& v, std::string_view path) {
    std::vector<uint8_t> b;
    if (!v.read(path, b)) return "<missing>";
    return std::string(b.begin(), b.end());
}

bool warned(const av::Vfs& v, const std::string& needle) {
    for (const auto& w : v.warnings())
        if (w.find(needle) != std::string::npos) return true;
    return false;
}

void test_parse() {
    // Current form (2021+): numbered blocks with "path", nested "apps" with numeric keys (not libraries).
    const std::string cur =
        "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n\t\t\"label\"\t\t\"\"\n"
        "\t\t\"apps\"\n\t\t{\n\t\t\t\"228980\"\t\t\"956015761\"\n\t\t}\n\t}\n"
        "\t\"1\"\n\t{\n\t\t\"PATH\"\t\t\"D:\\\\Steam \\\"Library\\\"\"\n\t}\n}\n";
    const auto a = av::parse_steam_library_folders(cur);
    CHECK(a.size() == 2);
    CHECK(a.size() == 2 && a[0] == "C:\\Program Files (x86)\\Steam");
    CHECK(a.size() == 2 && a[1] == "D:\\Steam \"Library\"");
    // Pre-2021 form: numeric keys with the path as the value, other settings beside them; // comments.
    const std::string old =
        "// written by Steam\n\"LibraryFolders\"\n{\n\t\"TimeNextStatsReport\"\t\t\"1600000000\"\n"
        "\t\"ContentStatsID\"\t\t\"-123\"\n\t\"1\"\t\t\"E:\\\\Games\\\\SteamLibrary\"\n}\n";
    const auto b = av::parse_steam_library_folders(old);
    CHECK(b.size() == 1 && b[0] == "E:\\Games\\SteamLibrary");
    // Garbage and truncation: whatever parsed so far, no crash.
    CHECK(av::parse_steam_library_folders("").empty());
    CHECK(av::parse_steam_library_folders("}}}{{\"path\"").empty());
    CHECK(av::parse_steam_library_folders("\"x\" { \"0\" { \"path\" \"F:\\\\L\" ").size() == 1);
}

void test_layout() {
    const fs::path T = fs::temp_directory_path() / "alamo_vfs_steam_tests";
    std::error_code ec;
    fs::remove_all(T, ec);
    const fs::path steam = T / "Steam", lib1 = T / "Lib One", lib2 = T / "lib2";
    const fs::path foc = lib1 / "steamapps" / "common" / "Star Wars Empire at War" / "corruption";
    write_file(foc / "Data" / "XML" / "Base.xml", "BASE");
    const auto item = [](const fs::path& lib, const char* id) {
        return lib / "steamapps" / "workshop" / "content" / "32470" / id;
    };
    write_file(item(lib1, "111") / "Data" / "XML" / "Both.xml", "LIB1");   // in the FoC library and in lib2
    write_file(item(lib2, "111") / "Data" / "XML" / "Both.xml", "LIB2");
    write_file(item(lib2, "222") / "Data" / "XML" / "Only2.xml", "TWO");   // only in a library the .vdf names
    write_file(item(steam, "333") / "Data" / "XML" / "Root.xml", "ROOT");  // in the client folder itself
    write_file(item(lib2, "444") / "readme.txt", "no Data folder");        // installed, but nothing to mount
    write_file(lib2 / "steamapps" / "workshop" / "content" / "99999" / "555" / "Data" / "x.xml", "other app");
    // The client's list names itself, lib2 (forward slashes, other case) and lib1 again: duplicates are dropped.
    std::string lib2_spelled = u8(lib2);
    for (char& c : lib2_spelled) c = c == '\\' ? '/' : c;
    write_file(steam / "steamapps" / "libraryfolders.vdf",
               "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" + vdf_escape(u8(steam)) + "\"\n\t}\n"
               "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" + vdf_escape(lib2_spelled) + "\\\\\"\n\t}\n"
               "\t\"2\"\n\t{\n\t\t\"path\"\t\t\"" + vdf_escape(u8(lib1)) + "\"\n\t}\n}\n");

    const std::vector<std::string> roots = {u8(steam)};
    const std::vector<std::string> libs = av::find_steam_libraries(u8(foc), roots);
    // FoC's own library first, then the client folder, then the .vdf entries not seen yet.
    CHECK(libs.size() == 3);
    if (libs.size() == 3) {
        CHECK(libs[0] == u8(lib1));
        CHECK(libs[1] == u8(steam));
        CHECK(libs[2] == lib2_spelled);  // kept as written, trailing separator dropped
    }
    // No steamapps ancestor and no roots: nothing to search.
    CHECK(av::find_steam_libraries(u8(T / "elsewhere"), {}).empty());

    // Item lookup: first library wins; ids must be digits; another app's items are not FoC's.
    CHECK(av::find_workshop_item(libs, "111") == std::optional<std::string>(u8(item(lib1, "111"))));
    CHECK(av::find_workshop_item(libs, "222").has_value());
    CHECK(av::find_workshop_item(libs, "333").has_value());
    CHECK(!av::find_workshop_item(libs, "555").has_value());
    CHECK(av::find_workshop_item(libs, "555", "99999").has_value());
    CHECK(!av::find_workshop_item(libs, "999").has_value());
    CHECK(!av::find_workshop_item(libs, "").has_value());
    CHECK(!av::find_workshop_item(libs, "..\\..\\steamapps").has_value());
    CHECK(!av::find_workshop_item(libs, "111\\Data").has_value());

    // Through the VFS with an explicit resolver: STEAMMOD= mounts the item at its command-line position.
    {
        av::VfsConfig c;
        c.foc_root = u8(foc);
        c.eaw_root = "";
        c.mount_megs = false;
        c.steam_resolver = av::make_steam_workshop_resolver(libs);
        CHECK(av::parse_command_line(std::vector<std::string>{"STEAMMOD=222", "steammod=\"111\"", "STEAMMOD=999"}, c));
        std::string err;
        const auto v = av::Vfs::create(c, &err);
        CHECK(v != nullptr);
        if (v) {
            CHECK(v->mod_dirs().size() == 2);
            CHECK(read_str(*v, "Data\\XML\\Only2.xml") == "TWO");
            CHECK(read_str(*v, "Data\\XML\\Both.xml") == "LIB1");
            CHECK(read_str(*v, "Data\\XML\\Base.xml") == "BASE");
            CHECK(warned(*v, "STEAMMOD=999: workshop item not found; nothing mounted"));
            CHECK(v->describe().find("STEAMMOD=222") != std::string::npos);
        }
    }
    // ums1 t01: InstallLocations. MegaFiles.xml makes it a FoC folder; a GameData folder beside it is the EaW install.
    write_file(foc / "Data" / "MegaFiles.xml", "<Mega_Files/>");
    write_file(foc.parent_path() / "GameData" / "Data" / "XML" / "x.xml", "EAW");
    {
        // A known game folder: the libraries are find_steam_libraries' (FoC's own first).
        const av::InstallLocations at = av::install_at(u8(foc), roots);
        CHECK(at.foc == u8(foc) && at.steam_libraries == libs);
        CHECK(at.eaw == alamo::path_to_utf8(foc.parent_path() / "GameData"));
        CHECK(at.mods_dir() == alamo::path_to_utf8(foc / "Mods"));
        CHECK(av::is_foc_dir(u8(foc)) && !av::is_foc_dir(u8(foc.parent_path())) && !av::is_foc_dir(""));
        CHECK(av::normalize_foc_dir("  \"" + u8(foc.parent_path()) + "\\\"  ") == alamo::path_to_utf8(foc.parent_path() / "corruption"));
        CHECK(av::normalize_foc_dir(u8(T / "nothing")).empty());
        // Search: the game is found through the client's libraries (lib1 is named by the client's .vdf).
        av::InstallInputs in;
        in.steam_roots = roots;
        const av::InstallLocations found = av::find_install(in);
        CHECK(found.foc == alamo::path_to_utf8(fs::path(u8(lib1)) / "steamapps" / "common" / "Star Wars Empire at War" / "corruption"));
        CHECK(found.steam_libraries.size() == 3 && !found.eaw.empty());
        // The user's folders come first; a stale one is skipped.
        const fs::path mine = T / "My FoC" / "corruption";
        write_file(mine / "StarWarsG.exe", "");
        fs::create_directories(mine / "Data");
        in.user_folders = {u8(T / "stale"), u8(mine.parent_path())};
        const av::InstallLocations user = av::find_install(in);
        CHECK(user.foc == alamo::path_to_utf8(mine.parent_path() / "corruption") && user.eaw.empty());
        CHECK(user.steam_libraries.size() == 3);  // no steamapps ancestor: the client's libraries only
        // Extra libraries (alamo_play's list) are searched after the roots' and kept once; fixed folders last.
        av::InstallInputs only;
        only.steam_libraries = {u8(T / "empty_lib"), u8(lib1)};
        CHECK(av::find_install(only).foc == alamo::path_to_utf8(fs::path(u8(lib1)) / "steamapps" / "common" / "Star Wars Empire at War" / "corruption"));
        CHECK(av::find_install(only).steam_libraries.size() == 2);
        av::InstallInputs fixed;
        fixed.fixed = {u8(T / "nope"), u8(mine)};
        CHECK(av::find_install(fixed).foc == u8(mine));
        CHECK(av::find_install(av::InstallInputs{}).foc.empty());
    }
    // The process-wide lookup: off by default (a bare Vfs warns "no workshop resolver"), on once installed, and the
    // caller's own resolver still wins.
    {
        av::uninstall_steam_workshop_lookup();
        av::VfsConfig c;
        c.foc_root = u8(foc);
        c.eaw_root = "";
        c.mount_megs = false;
        CHECK(av::parse_command_line(std::vector<std::string>{"STEAMMOD=333", "STEAMMOD=444"}, c));
        auto off = av::Vfs::create(c);
        CHECK(off && off->mod_dirs().empty() && warned(*off, "STEAMMOD=333: no workshop resolver"));
        CHECK(!av::default_steam_resolver(u8(foc)));

        av::install_steam_workshop_lookup(roots);
        auto on = av::Vfs::create(c);
        CHECK(on && on->mod_dirs().size() == 2);
        if (on) {
            CHECK(read_str(*on, "Data\\XML\\Root.xml") == "ROOT");
            CHECK(warned(*on, "mod path has no Data folder"));  // 444 is installed but has no Data
        }
        av::VfsConfig own = c;
        own.steam_resolver = [](std::string_view) { return std::optional<std::string>(); };
        auto mine = av::Vfs::create(own);
        CHECK(mine && mine->mod_dirs().empty() && warned(*mine, "STEAMMOD=333: workshop item not found"));
        // A FoC folder outside any library still finds the items of the client's libraries.
        av::VfsConfig loose = c;
        loose.foc_root = u8(T / "Loose FoC");
        fs::create_directories(T / "Loose FoC" / "Data");
        auto lv = av::Vfs::create(loose);
        CHECK(lv && lv->mod_dirs().size() == 2);
        av::uninstall_steam_workshop_lookup();
    }
    fs::remove_all(T, ec);
}

}  // namespace

int main() {
    test_parse();
    test_layout();
    std::printf(g_fail ? "alamo_vfs_steam_tests: %d FAILED\n" : "alamo_vfs_steam_tests: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
