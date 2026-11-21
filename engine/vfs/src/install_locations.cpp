// Install locations (install_locations.hpp).
#include "alamo/vfs/install_locations.hpp"

#include <filesystem>
#include <system_error>
#include <utility>

#include "alamo/core/host_fs.hpp"
#include "alamo/vfs/host_path.hpp"
#include "alamo/vfs/steam_workshop.hpp"

namespace alamo::vfs {
namespace fs = std::filesystem;

namespace {

bool dir_exists(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(long_path(p), ec);
}

bool path_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(long_path(p), ec);
}

std::string trim(std::string_view s) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"'; };
    size_t b = 0;
    while (b < s.size() && space(s[b])) ++b;
    size_t e = s.size();
    while (e > b && space(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

// <foc>\..\GameData when it has a Data folder.
std::string eaw_next_to(const std::string& foc) {
    if (foc.empty()) return {};
    const fs::path g = path_from_utf8(foc).parent_path() / "GameData";
    return dir_exists(g / "Data") ? path_to_utf8(g) : std::string();
}

void fill(InstallLocations& out, std::span<const std::string> extra_libraries) {
    out.eaw = eaw_next_to(out.foc);
    out.steam_libraries = find_steam_libraries(out.foc, out.steam_roots);
    for (const std::string& lib : extra_libraries) {
        bool dup = lib.empty();
        for (const std::string& have : out.steam_libraries) dup = dup || lexical_folder_key(have) == lexical_folder_key(lib);
        if (!dup) out.steam_libraries.push_back(lib);
    }
}

}  // namespace

bool is_foc_dir(const std::string& dir) {
    if (dir.empty()) return false;
    const fs::path d = path_from_utf8(dir);
    if (!dir_exists(d / "Data")) return false;
    return path_exists(d / "Data" / "MegaFiles.xml") || path_exists(d / "StarWarsG.exe");
}

std::string normalize_foc_dir(std::string_view text) {
    std::string t = trim(text);
    while (t.size() > 3 && (t.back() == '\\' || t.back() == '/')) t.pop_back();  // keep "C:\"
    if (t.empty()) return {};
    if (is_foc_dir(t)) return t;
    const std::string below = path_to_utf8(path_from_utf8(t) / "corruption");
    if (is_foc_dir(below)) return below;
    return {};
}

std::string InstallLocations::mods_dir() const { return foc.empty() ? std::string() : path_to_utf8(path_from_utf8(foc) / "Mods"); }

InstallLocations find_install(const InstallInputs& in) {
    InstallLocations out;
    out.steam_roots = in.steam_roots;
    out.user_folders = in.user_folders;
    for (const std::string& u : in.user_folders)
        if (out.foc.empty()) out.foc = normalize_foc_dir(u);
    if (out.foc.empty()) {
        // The roots' libraries, then the extra ones: the same list a found game gets, minus its own library.
        std::vector<std::string> libs = find_steam_libraries("", in.steam_roots);
        libs.insert(libs.end(), in.steam_libraries.begin(), in.steam_libraries.end());
        for (const std::string& lib : libs) {
            if (!out.foc.empty()) break;
            // [name-ok: Steam's install folder of the game, not a faction]
            out.foc = normalize_foc_dir(path_to_utf8(path_from_utf8(lib) / "steamapps" / "common" / "Star Wars Empire at War"));
        }
    }
    for (const std::string& f : in.fixed)
        if (out.foc.empty() && is_foc_dir(f)) out.foc = f;
    fill(out, in.steam_libraries);
    return out;
}

InstallLocations install_at(std::string_view foc, std::span<const std::string> steam_roots) {
    InstallLocations out;
    out.foc = std::string(foc);
    out.steam_roots.assign(steam_roots.begin(), steam_roots.end());
    fill(out, {});
    return out;
}

InstallLocations default_install_at(std::string_view foc) {
    const std::vector<std::string> roots = default_steam_roots();
    return install_at(foc, roots);
}

}  // namespace alamo::vfs
