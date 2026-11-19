// Play.exe (alamo_play): the test build's double-click launcher, the window-free twin of Play.cmd. This half is the
// part the tests drive (alamo_play_tests); play_main.cpp adds the Windows side (folder dialog, process, message boxes).
//   find_game      the Forces of Corruption "corruption" folder, in Play.cmd's order (launcher.ps1 Find-Game): the
//                  saved game_path.txt, ALAMO_FOC_DATA, the Steam libraries, a few fixed install folders; the search
//                  itself is vfs::find_install (ums1 t01, install_locations.hpp), shared with every launch surface
//   game_args      alamo_game's arguments: --game, --cache-dir, then the user's own (a dropped mod folder becomes
//                  MODPATH=<folder>, as Play_Mod.cmd does for a dragged folder)
//   stack_mod_args the mod arguments replaced by their resolved stack (ums1 t04: the in-game picker's list)
//   command_line   one CreateProcessW command line (CommandLineToArgvW quoting)
// Launcher only: nothing here is read by the engine or the sim.
#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace alamo::play {

// A Forces of Corruption folder: has Data, and Data\MegaFiles.xml or StarWarsG.exe (launcher.ps1 Test-Game).
bool is_game_dir(const std::filesystem::path& dir);

// `text` as a game folder: the "corruption" folder itself, or the "Star Wars Empire at War" folder above it. Quotes,
// spaces and trailing separators are dropped. Empty when it is neither.
std::filesystem::path normalize_game_dir(std::wstring text);

// The first line of a UTF-8 text file's bytes (a BOM dropped), as a path text. game_path.txt is written by
// launcher.ps1 (Set-Content -Encoding UTF8, with a BOM) or by Play.exe (the same form).
std::wstring first_line_utf8(std::string_view bytes);

struct FindInputs {
    std::filesystem::path saved_file;            // game_path.txt next to the exe (may not exist)
    std::wstring env;                            // ALAMO_FOC_DATA (may be empty)
    std::vector<std::filesystem::path> steam_roots;  // Steam client folders (vfs::default_steam_roots()); their libraries first
    std::vector<std::filesystem::path> steam;    // further Steam library folders, in search order
    std::vector<std::filesystem::path> fixed;    // last resort install folders (GOG)
};

// The game folder, or empty when none of the inputs names one. `steam_libraries` (when given) receives the Steam library
// folders the search found (UTF-8, vfs::InstallLocations::steam_libraries), so the caller does not walk Steam again
// (ums2 t07).
std::filesystem::path find_game(const FindInputs& in, std::vector<std::string>* steam_libraries = nullptr);

// --game <game> --cache-dir <cache>, then `user` in order; an argument that is a folder holding a Data folder (or a
// Data folder itself) becomes MODPATH=<that folder, absolute>.
std::vector<std::wstring> game_args(const std::filesystem::path& game, const std::filesystem::path& cache,
                                    const std::vector<std::wstring>& user);

// ums1 t04: resolves the picked mods to the stack every launch surface starts (play_main passes
// game::play_launch_args, the resolver the in-game picker and alamo_mod_check --resolve use). Gets the mod arguments in
// command-line order (UTF-8); returns the launch list, or nullopt when the stack is refused (a base-EaW mod, D1).
using ModStackResolver = std::function<std::optional<std::vector<std::string>>(const std::vector<std::string>& picked)>;
// `args` (game_args' output) with its MODPATH= / STEAMMOD= arguments (any case) replaced by resolve(them), placed
// last; the other arguments keep their order. Without a mod argument `args` comes back unchanged and resolve is not
// called. nullopt when resolve refuses.
std::optional<std::vector<std::wstring>> stack_mod_args(const std::vector<std::wstring>& args, const ModStackResolver& resolve);

// One argument quoted the way CommandLineToArgvW / the C runtime split it back.
std::wstring quote_arg(const std::wstring& arg);

// "<exe>" arg arg ...
std::wstring command_line(const std::filesystem::path& exe, const std::vector<std::wstring>& args);

}  // namespace alamo::play
