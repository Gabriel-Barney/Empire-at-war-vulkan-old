// Mod conflicts (ums1 t05, alamo_mod_check --conflicts --json): which mod of a load order wins each file the mods share,
// decided by the VFS's own lookup, so the Command Center's conflicts page stops keeping first-match rules of its own.
//
// A stack is mounted exactly as alamo_game mounts it (vfs::mount_game: the command line as written, earliest mod wins,
// docs/MODS.md 3.1; the mod MEGs the winning MegaFiles.xml names). Each mod's files are its loose Data tree plus the
// records of its mounted MEGs that the game's MEG lookup reaches (meg::Archive::lookup; a record a malformed table hides
// is not the mod's file). Every path two layers have is resolved with Vfs::resolve: the winner is the layer the game
// reads, which is not always the earlier mod (a later mod's loose file beats an earlier mod's MEG record, and the base
// game's loose file beats any MEG record; docs/MODS.md 3.1).
//
// Findings, in this order (the launcher's ranks):
//   list_ignored  a list file (GameObjectFiles, HardPointDataFiles, TradeRouteFiles, CampaignFiles) that another
//                 layer wins: the game opens one copy, so files only the losing copy names never load (warning), or
//                 nothing is lost (info). `order` when the losing list names everything the winning one does.
//   submod_order  a mod sits below a mod that declares it as a parent (modinfo.json dependencies): the parent wins
//                 the files they share. `order`.
//   no_effect     every file of the mod is won by another layer. `order`.
//   no_data       the mod folder has no Data folder.
// Read only; launcher-side (nothing in the sim reads it). Output order is fixed: mods in load order, pairs by
// (winner, loser), paths in byte order of their normalised form.
#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace alamo::vfs {
struct ModCatalog;
}

namespace alamo::game {

struct ConflictMod {
    std::string key, name, root, arg;  // catalog key (ModDescriptor::catalog_key), display name, host folder, VFS argument
    bool exists = false, has_data = false;
    size_t files = 0;           // loose files plus reachable MEG records
    size_t megs = 0;            // the mod's MEGs the stack mounted
    size_t replaced_files = 0;  // of `files`, the ones another layer wins
    size_t base_wins = 0;       // of those, the ones the base game wins (its loose files beat MEG records)
};

// Files two mods both have; `winner` is the one the game reads them from.
struct ConflictPair {
    size_t winner = 0, loser = 0;     // indices into ConflictReport::mods
    std::vector<std::string> files;   // normalised paths ("DATA\\XML\\UNITS.XML"), byte order
    size_t xml_files = 0;             // how many of them end in .XML
};

struct ConflictFinding {
    std::string kind;       // "list_ignored", "submod_order", "no_effect", "no_data"
    bool warning = true;    // false: worth knowing, nothing to fix (info)
    std::string text;
    std::vector<size_t> mods;  // indices into ConflictReport::mods, the mod the finding is about first
    bool order = false;        // moving a mod is the likely fix
    std::vector<std::string> lost;  // list_ignored: the entries only the losing list names (as written)
};

struct ConflictReport {
    std::string game;
    std::vector<std::string> args;  // the load order as given
    std::vector<ConflictMod> mods;  // the mounted mods, in load order
    std::vector<ConflictPair> pairs;
    std::vector<ConflictFinding> findings;
    std::vector<std::string> errors;  // the stack did not mount, an argument names no mod
};

// `args`: MODPATH= / STEAMMOD= in load order (STEAMMOD= found in the catalog's Steam libraries).
ConflictReport mod_conflicts(const vfs::ModCatalog& catalog, std::span<const std::string> args,
                             std::span<const std::string> workshop_libraries);
// Schema "alamo.mod_conflicts", version 1: game, args, mods [{key, name, root, arg, exists, has_data, files, megs,
// replaced_files, base_wins}], pairs [{winner, loser, xml_files, files []}], findings [{kind, severity "warning" |
// "info", text, mods [], order, lost []}], errors [].
std::string format_conflicts_json(const ConflictReport& report);

}  // namespace alamo::game
