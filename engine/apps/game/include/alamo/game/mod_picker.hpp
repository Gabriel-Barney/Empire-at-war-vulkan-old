// modc t08: the mod picker's data side (alamo_game's IDD_PORT_MODS_DIALOG; docs/MODS.md §5.2). Render-free and
// UI-free so the tests can drive it:
//   list_installed_mods   the mods a player can pick: the rows of vfs::ModCatalog (ums1 t03; Steam workshop items of the
//                         FoC app id in the InstallLocations' library order, the first library holding an id wins as
//                         for the STEAMMOD= resolver, then the folders of <game>\Mods; one row per modinfo variant). A
//                         candidate needs a Data folder. Display name: the descriptor's (modinfo.json "name", else the
//                         folder name).
//   mod_report_summary    a few lines of the load report (mod_check.hpp, the alamo.mod_report figures) to show before
//                         launch: result, errors / warnings, content counts, Lua, missing files, the submod hint
//   mod_launch_args       the VFS arguments that start a mod: the report's resolved stack (mcx3 t06: declared and
//                         likely parents, submod first), else for a likely submod (SubmodHint::likely) the report's
//                         stacked arguments, else its own MODPATH= / STEAMMOD=; none for a refused base-EaW mod (D1)
//   mod_language_warnings mcx3 t09: the mods of the stack whose modinfo.json lists no text in the game's language
//   relaunch_args         a command line with its mod arguments replaced (alamo_game restarts itself: the data, the
//                         text database and the GUI are loaded once per process, as in the original, which takes mods
//                         only on its command line)
// No faction or mod name is known here (docs/MODS.md §1).
#pragma once
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace alamo::vfs {
struct ModCatalog;
struct ModDescriptor;
struct ModStack;
}  // namespace alamo::vfs

namespace alamo::game {

struct ModCheckReport;
struct ModCheckOptions;

// One picker row: one catalog row (vfs::ModCatalog, ums1 t03), so a folder with modinfo variants is one row per
// variant (2083545253:subA, 2083545253:subB).
struct InstalledMod {
    std::string key;        // the catalog key (ModDescriptor::catalog_key): the row's identity in the picker
    std::string id;         // workshop id (the item folder's name) or the <game>\Mods folder name
    std::string variant;    // "" for a folder's main mod, else the <variant> of <variant>-modinfo.json
    bool workshop = false;  // a Steam workshop item (else a <game>\Mods folder)
    std::string path;       // host folder
    std::string name;       // the descriptor's name (modinfo.json "name", UTF-8, else the folder name)
    std::string arg;        // the VFS argument that mounts its folder: "STEAMMOD=<id>" or "MODPATH=Mods\<id>"
};

// Sub-folders of `dir` that have a Data folder, by name (byte order); `digits_only`: workshop item folders only. An entry
// that cannot be read is skipped and the scan goes on. Shared by list_installed_mods and the load report's submod search
// (cu9 t03).
std::vector<std::filesystem::path> mod_folders(const std::filesystem::path& dir, bool digits_only);
// <library>\steamapps\workshop\content\<FoC app id>: where a Steam library keeps its FoC workshop items.
std::filesystem::path workshop_content_dir(std::string_view library);

// The picker row of one catalog row.
InstalledMod installed_mod(const vfs::ModDescriptor& mod);
// The catalog's rows, in catalog order (workshop items, then Mods folders; each folder's main mod, then its variants).
std::vector<InstalledMod> list_installed_mods(const vfs::ModCatalog& catalog);
// The same over vfs::scan_installed_mods(game, workshop_libraries) (no content hashes); forwards until ums4.
std::vector<InstalledMod> list_installed_mods(const std::string& game, std::span<const std::string> workshop_libraries);
// A VFS mod argument: MODPATH= or STEAMMOD= (any case). FALLBACKPATH= is not one (it stays).
bool is_mod_arg(std::string_view arg);
// `args` without its mod arguments, followed by `mod_args` (empty = the base game).
std::vector<std::string> relaunch_args(std::span<const std::string> args, std::span<const std::string> mod_args);
// The arguments that start `mod` (see the file comment); `report` may be null (not checked yet). A resolved report
// gives its stack's vfs::ModStack::launch_args().
std::vector<std::string> mod_launch_args(const InstalledMod& mod, const ModCheckReport* report);
// ums1 t04: the picker's check of one row (alamo_game runs it on a worker thread): `picked` is the row's catalog key or
// argument ("" = the base game). The stack is resolved first (resolve_mod_stack: declared and likely parents, submod
// first) and the report is of what Launch starts (its launch_args; the picked mod alone when that is empty, so a
// refused base-EaW mod reports its refusal). The report is marked resolved with the stack.
ModCheckReport check_picked_mod(const std::string& game, const std::string& picked, const ModCheckOptions& options);
// ums1 t04: the MODPATH= / STEAMMOD= list a launch of `picked` (arguments or catalog keys) starts:
// resolve_mod_stack(game, picked, options).launch_args(). alamo_play uses it; the picker reaches the same list through
// check_picked_mod and mod_launch_args. Empty for a refused base-EaW stack; `stack` (if given) receives the stack.
std::vector<std::string> launch_args_for(const std::string& game, const std::vector<std::string>& picked, const ModCheckOptions& options,
                                         vfs::ModStack* stack = nullptr);
// alamo_play's resolver (play::stack_mod_args gets it): launch_args_for with the install's Steam libraries
// (`steam_libraries` when the caller already found them, as play::find_game does; else vfs::default_install_at(game));
// nullopt when the stack is refused (base-EaW, D1). `stack` receives the stack, so the caller can show its errors.
std::optional<std::vector<std::string>> play_launch_args(const std::string& game, const std::vector<std::string>& picked,
                                                         vfs::ModStack* stack = nullptr,
                                                         const std::vector<std::string>* steam_libraries = nullptr);
// The picker's report lines (UTF-8, at most about a dozen, the most important first).
std::vector<std::string> mod_report_summary(const ModCheckReport& report);
// ISO 639-1 code of one of retail's text languages ("GERMAN" -> "de", ui::retail_language_for_langid's names); "" for
// another name.
std::string language_code(std::string_view retail_language);
// One line per mod of the report's stack (the resolved stack, else the mounted mods) that has a modinfo.json and lists
// no text support (support bit 1) for `retail_language` (the language the game picked, e.g. "GERMAN"); a modinfo.json
// without "languages" means English (the eaw.modinfo spec). Mods without modinfo.json say nothing either way.
std::vector<std::string> mod_language_warnings(const ModCheckReport& report, std::string_view retail_language);
// One Windows command line from `args` (quoted for CommandLineToArgvW / the CRT: spaces, tabs and quotes; backslashes
// before a quote doubled).
std::string windows_command_line(std::span<const std::string> args);

}  // namespace alamo::game
