// Mod registry (mcx3, docs/MOD_COMPAT_DESIGN.md 5): one ModDescriptor per installed mod, built from its folder before
// anything is mounted, and the stack resolution the launcher, the mod picker and `alamo_mod_check --resolve` use.
//
// Retail stays the authority on what a command line means: MODPATH= / STEAMMOD= entries form one list in command-line
// order and the earliest wins (docs/MODS.md 3.1). The registry only *builds* such a list before launch (submod first,
// then what it depends on); the VFS never reorders an explicit command line. Nothing in gameplay reads a descriptor:
// retail never opens modinfo.json, so names, languages and dependencies feed the launcher, the picker, the report and
// save headers only.
//
// modinfo.json follows the AlamoEngine-Tools eaw.modinfo spec: `name` (required), `summary`, `icon`, `version`,
// `languages` [{code, support: 1 text | 2 speech | 4 SFX}] (missing: English, full support), `dependencies` (optional
// first element: the resolve layout "ResolveRecursive" (default) / "ResolveLastItem" / "FullResolved"; then
// {modtype: 0 local path / 1 workshop / 2 virtual, identifier, name}), `steamdata` {publishedfileid, tags, ...}, `custom`.
// Unknown keys are ignored. The reader is lenient as mod authors write these by hand: // and /* */ comments, trailing
// commas and a missing comma between members are accepted (the last with a warning); a file that still does not parse
// is a warning and the folder name stands in. `<variant>-modinfo.json` files describe further mods in the same folder
// (one descriptor each, the main modinfo.json's values as defaults); a folder with only variant files has no main
// descriptor.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace alamo::vfs {

enum class ModKind : uint8_t { Workshop, LocalMods, Path };
enum class GameTag : uint8_t { Unknown, Foc, Eaw };
enum class DependencyLayout : uint8_t { ResolveRecursive, ResolveLastItem, FullResolved };

struct ModLanguage {
    std::string code;      // ISO 639-1, lower case ("en", "de")
    uint32_t support = 0;  // 1 text, 2 speech, 4 SFX
};
struct ModDependency {
    uint32_t modtype = 0;    // 0 local (a path, absolute or relative to <game>), 1 workshop id, 2 virtual
    std::string identifier;
    std::string name;
};

// What a modinfo.json says (parse_modinfo).
struct ModInfo {
    std::string name, summary, icon, version;
    std::vector<ModLanguage> languages;  // empty when the key is missing
    bool has_languages = false;
    std::vector<ModDependency> dependencies;
    bool has_dependencies = false;
    DependencyLayout layout = DependencyLayout::ResolveRecursive;
    std::vector<std::string> steam_tags;  // steamdata.tags (also a top-level "tags" array, written by some uploaders)
    std::string published_id;             // steamdata.publishedfileid
};
// Lenient read (see the file comment). nullopt when the text is not a JSON object even leniently; *warnings collects
// what was tolerated or ignored.
std::optional<ModInfo> parse_modinfo(std::string_view text, std::vector<std::string>* warnings = nullptr);

struct ModDescriptor {
    ModKind kind = ModKind::Path;
    std::string id;       // workshop id, else the folder name
    std::string variant;  // "" for the folder's main mod, else the <variant> of <variant>-modinfo.json
    std::string root;     // host folder
    std::string name;     // modinfo "name", else the folder name
    std::string version, summary, icon;
    std::vector<ModLanguage> languages;  // modinfo languages; English text + speech + SFX when not given
    std::vector<ModDependency> dependencies;
    DependencyLayout layout = DependencyLayout::ResolveRecursive;
    bool has_modinfo = false;
    bool has_data = false;  // root\Data exists (a mod without one has nothing to mount)
    GameTag game_tag = GameTag::Unknown;  // from the Steam tags: EAW without FOC = Eaw, FOC = Foc
    uint64_t content_hash = 0;            // mod_content_hash(root)
    std::vector<std::string> warnings;    // modinfo problems

    // "<id>" or "<id>:<variant>".
    std::string key() const;
    // The VFS argument that mounts it: STEAMMOD=<id> (workshop), MODPATH=Mods\<id> (local), MODPATH=<root> (path).
    std::string arg() const;
};

// Hash of the sorted (relative path, size, last write time) list of root\Data (every file, MEGs included): the save
// fingerprint, the corpus cache key and "mod updated" detection. 0 when there is no Data folder. FNV-1a 64.
uint64_t mod_content_hash(const std::string& root);

// The descriptors of one mod folder: its main mod (modinfo.json, or none at all), then one per variant file in name
// order. Always at least one unless the folder has variant files only. `hash`: compute content_hash.
std::vector<ModDescriptor> describe_mod_folder(const std::string& root, ModKind kind, const std::string& id, bool hash = true);

struct ModRegistry {
    std::string game;                  // the FoC folder (Mods\ lives under it)
    std::vector<ModDescriptor> mods;   // workshop items by id (first library wins), then <game>\Mods folders by name
    const ModDescriptor* find_key(std::string_view key) const;          // "<id>" / "<id>:<variant>"
    // A folder's main mod (case-insensitive path), else its first variant; find_workshop the same by workshop id.
    const ModDescriptor* find_root(std::string_view root) const;
    const ModDescriptor* find_workshop(std::string_view id) const;
    const ModDescriptor* find_dependency(const ModDependency& d) const;  // by modtype and identifier
    // The descriptor a VFS argument names: STEAMMOD=<id>, MODPATH=Mods\<name> / MODPATH=<path> (one outside the
    // registry is described on the spot into `scratch`), or a registry key ("<id>", "<id>:<variant>", also as
    // STEAMMOD=<id>:<variant>) for the picker to name one variant. nullptr for anything else.
    const ModDescriptor* find_arg(std::string_view arg, std::vector<ModDescriptor>& scratch) const;
};
// Workshop items with a Data folder under each library's content folder (folder name all digits), then the folders of
// <game>\Mods with a Data folder; each mod folder through describe_mod_folder. `hash`: compute content hashes.
ModRegistry scan_installed_mods(const std::string& game, std::span<const std::string> workshop_libraries, bool hash = true);

// True when `root` replaces `path` (game-relative, e.g. "Data\\XML\\GameObjectFiles.xml"): a loose file, or an entry of
// a MEG in its Data folder.
bool mod_provides(const std::string& root, std::string_view path);

// Stack resolution (design 5.2). `hint` is asked for a mod that declares no dependency: the argument (STEAMMOD= /
// MODPATH=) of its likely parent from the data (the load report's submod hint), or nullopt. It gets the stack's
// arguments so far, the mod first.
using ParentHint = std::function<std::optional<std::string>(const std::vector<std::string>& stack_args)>;
struct StackMod {
    ModDescriptor mod;
    std::string reason;  // "picked", "dependency of <key>", "likely parent of <key>"
};
struct ResolvedStack {
    std::vector<StackMod> mods;        // launch order: each mod before the mods it depends on; ties keep discovery order
    std::vector<std::string> args;     // the VFS arguments in that order
    std::vector<std::string> errors;   // a dependency cycle, a base-EaW mod, an unknown argument
    std::vector<std::string> warnings; // a missing parent, two total conversions, a mod without Data
    uint64_t fingerprint = 0;          // FNV-1a 64 over the ordered content hashes
    bool base_eaw = false;             // a mod of the stack is a base-EaW mod (D1: refused, grade G0)
};
// The message the port refuses a base-EaW mod with (D1).
inline constexpr const char* kBaseEawRefusal = "base-EaW mod: retail runs it with sweaw.exe";
ResolvedStack resolve_stack(const ModRegistry& registry, std::span<const std::string> picked_args, const ParentHint& hint = {});

}  // namespace alamo::vfs
