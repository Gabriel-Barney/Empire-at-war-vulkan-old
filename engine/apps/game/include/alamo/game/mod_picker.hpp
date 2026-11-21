// modc t08: the mod picker's data side (alamo_game's IDD_PORT_MODS_DIALOG; docs/MODS.md §5.2). Render-free and
// UI-free so the tests can drive it. The mods a player can pick are the rows of vfs::ModCatalog themselves (ums1 t03,
// ums4 t01: no adapter; one row per installed folder and modinfo variant, each identified by its catalog key).
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

// `args` without its mod arguments, followed by `mod_args` (empty = the base game).
std::vector<std::string> relaunch_args(std::span<const std::string> args, std::span<const std::string> mod_args);
// The arguments that start `mod` (a catalog row; see the file comment); `report` may be null (not checked yet). A
// resolved report gives its stack's vfs::ModStack::launch_args().
std::vector<std::string> mod_launch_args(const vfs::ModDescriptor& mod, const ModCheckReport* report);
// ums1 t04: the picker's check of one row (alamo_game runs it on a worker thread): `picked` is the row's catalog key or
// argument ("" = the base game). The stack is resolved first (resolve_mod_stack: declared and likely parents, submod
// first; mod_resolve.hpp) and the report is of what Launch starts (its launch_args; the picked mod alone when that is empty, so a
// refused base-EaW mod reports its refusal). The report is marked resolved with the stack.
ModCheckReport check_picked_mod(const std::string& game, const std::string& picked, const ModCheckOptions& options);
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
