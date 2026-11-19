// Install locations (ums1 t01, Stage 10 unified mod system): where this machine keeps Forces of Corruption, the EaW
// folder next to it, the Steam client and library folders and the folders the user named, found once and handed to
// every launch surface (alamo_game's mod picker, alamo_play, alamo_mod_check). Before ums1 each surface walked Steam on
// its own (find_steam_libraries call sites, play::find_game's library loop).
//
// The game search order is Play.cmd's (launcher.ps1 Find-Game): the user's folders in the given order (game_path.txt,
// then ALAMO_FOC_DATA), then <library>\steamapps\common\Star Wars Empire at War\corruption of each Steam library, then
// the fixed install folders (GOG, disc). A folder counts when it is a FoC folder (Data, and Data\MegaFiles.xml or
// StarWarsG.exe) or the "Star Wars Empire at War" folder above one. The Steam libraries of a found game are
// find_steam_libraries(foc, steam_roots): the library holding the game first, so its own workshop items are found even
// with no Steam client known.
//
// Read only: nothing here writes a file, the registry or calls Steam. The result depends only on the inputs and the
// files on disk (no enumeration order decides it). Launcher-side: nothing in the sim reads it.
#pragma once
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace alamo::vfs {

// A Forces of Corruption folder: has Data, and Data\MegaFiles.xml or StarWarsG.exe (launcher.ps1 Test-Game).
bool is_foc_dir(const std::string& dir);
// `text` as a game folder (UTF-8): the "corruption" folder itself, or the "Star Wars Empire at War" folder above it.
// Spaces, tabs, line ends and quotes around it and trailing separators are dropped. "" when it is neither.
std::string normalize_foc_dir(std::string_view text);

struct InstallInputs {
    std::vector<std::string> user_folders;     // folders the user named, in order (game_path.txt line, ALAMO_FOC_DATA)
    std::vector<std::string> steam_roots;      // Steam client folders (default_steam_roots())
    std::vector<std::string> steam_libraries;  // library folders searched after the roots' libraries (tests, odd setups)
    std::vector<std::string> fixed;            // last-resort install folders (GOG, disc)
};

struct InstallLocations {
    std::string foc;  // the FoC "corruption" folder; "" when none was found
    std::string eaw;  // <foc>\..\GameData when it has a Data folder (the EaW install retail's FoC reads), else ""
    std::vector<std::string> steam_roots;      // as given (or default_steam_roots())
    std::vector<std::string> steam_libraries;  // find_steam_libraries(foc, steam_roots), then the extra libraries
    std::vector<std::string> user_folders;     // as given
    // The user's extra mod folders (the Command Center's "extra folders"): each one mod (it has a Data folder) or a
    // folder of mods. Not searched for a game; scan_mod_catalog lists them after <foc>\Mods. Set by the caller.
    std::vector<std::string> mod_folders;
    // <foc>\Mods ("" without a game folder).
    std::string mods_dir() const;
};

// The search above. With no game found, `foc` and `eaw` are "" and the libraries are those of the roots alone.
InstallLocations find_install(const InstallInputs& in);
// The locations of a game folder the caller already has (alamo_game --game, alamo_mod_check --game): no search.
InstallLocations install_at(std::string_view foc, std::span<const std::string> steam_roots);
// install_at(foc, default_steam_roots()).
InstallLocations default_install_at(std::string_view foc);

}  // namespace alamo::vfs
