// The stack resolver every launch surface starts (ums1 t04; ums4 t05: its own library, alamo_mod_resolve, which needs
// only the VFS and the XML list reader, so alamo_play (Play.exe) links it without alamo_game_core):
//   resolve_mod_stack    the picked mods' vfs::ModStack: declared dependencies (modinfo.json), then likely parents from the
//                        files (the submod hint below); the in-game picker, alamo_play and alamo_mod_check --resolve
//   play_launch_args     alamo_play's resolver (play::stack_mod_args gets it): the stack's launch list
//                        (vfs::ModStack::launch_args)
//   submod_hint          the likely parent of a stack from its files alone (modc t16): the Data\XML files the mods' own
//                        list files name that no layer has, and the installed mods outside the stack that have them.
//                        The load report (mod_check.hpp, `submod`) gives the same answer for a stack whose data loads:
//                        it runs find_submod_parents on its own missing-file list (the same files, read during its
//                        check). For a stack whose data does not load the report has no `submod`, while this still
//                        hints (docs/MODS.md 3.1.2).
// No faction or mod name is known here (docs/MODS.md §1).
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace alamo::vfs {
class Vfs;
struct ModCatalog;
struct ModDescriptor;
struct ModStack;
}  // namespace alamo::vfs

namespace alamo::game {

// An installed mod outside the stack that has files the checked mod's own data names but no layer has (modc t16).
struct ModParentCandidate {
    std::string id;           // workshop id (the item folder's name) or the <game>\Mods folder name
    bool workshop = false;    // a Steam workshop item (else a <game>\Mods folder)
    std::string path;         // host folder (native separators)
    std::string arg;          // the argument that stacks it: "STEAMMOD=<id>" or "MODPATH=Mods\<id>"
    uint32_t provides = 0;    // how many of SubmodHint::missing it has
    bool mentioned = false;   // its workshop id appears in a mounted mod's modinfo.json (ModDescriptor::modinfo_numbers)
};
struct SubmodHint {
    static constexpr uint32_t kMinProvided = 2;
    bool likely = false;      // parents.front() has at least kMinProvided and at least half of `missing`
    uint32_t missing = 0;     // missing files named by the mod's own data (MissingFile::in_mod)
    std::vector<ModParentCandidate> parents;  // provides > 0: most first, then mentioned, then by id; at most 5
    std::vector<std::string> stacked_args;    // likely: the check's args followed by parents.front().arg
};

// The Data\XML files that the list files a mod layer serves (Data\XML\*Files.xml whose winning copy is a mod's loose
// file) name and no layer has, upper case, sorted, each once.
std::vector<std::string> missing_list_entries(const vfs::Vfs& v);
// Fills `h` for a stack mounted with `args` whose mods are `mounted` (ModEnvironment::mounted()): the catalog's workshop
// items and <game>\Mods folders outside the stack (one per folder, vfs::folder_key identity) that have some of
// `missing` in their own files, ranked as SubmodHint::parents says.
void find_submod_parents(const std::vector<std::string>& missing, std::span<const vfs::ModDescriptor> mounted,
                         const std::vector<std::string>& args, const vfs::ModCatalog& catalog, SubmodHint& h);
// The hint for `args` on `game` from the files alone (mounts them, missing_list_entries, find_submod_parents). Empty
// without a mod, or when the stack is a refused base-EaW stack (D1: the load report has no hint for it either).
SubmodHint submod_hint(const std::string& game, const std::vector<std::string>& args, const vfs::ModCatalog& catalog);

// The resolved stack of `picked` (MODPATH= / STEAMMOD= arguments or catalog keys, in pick order) on `game` with these
// Steam libraries: vfs::resolve over the install's catalog, with submod_hint as its parent hint.
vfs::ModStack resolve_mod_stack(const std::string& game, const std::vector<std::string>& picked,
                                std::span<const std::string> workshop_libraries);
// The same over a catalog the caller scanned already (its `game` is the FoC folder).
vfs::ModStack resolve_mod_stack(const vfs::ModCatalog& catalog, const std::vector<std::string>& picked);
// The catalog resolve_mod_stack scans: `game` as given, these Steam libraries, no extra mod folders, no content hashes.
vfs::ModCatalog stack_catalog(const std::string& game, std::span<const std::string> workshop_libraries);
// alamo_play's resolver: resolve_mod_stack(...).launch_args() (the MODPATH= / STEAMMOD= list a launch starts) with
// `steam_libraries` (when the caller already found them, as play::find_game does; else vfs::default_install_at(game)'s);
// nullopt when the stack is refused (base-EaW, D1). `stack` receives the stack, so the caller can show its errors.
std::optional<std::vector<std::string>> play_launch_args(const std::string& game, const std::vector<std::string>& picked,
                                                         vfs::ModStack* stack = nullptr,
                                                         const std::vector<std::string>* steam_libraries = nullptr);

}  // namespace alamo::game
