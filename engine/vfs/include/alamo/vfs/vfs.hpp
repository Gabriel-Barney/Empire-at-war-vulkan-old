// alamo_vfs - virtual file system implementing the verified Data\ resolution order
// (docs/PHASE2.md, docs/re/SUBSYSTEMS.md "File lookup priority", docs/re/FALLBACK_CAMPAIGNS.md).
//
// Lookup of a game-relative path, first match wins:
//   1. Mod  : one search list, <dir>\Data\<rest>, earliest entry wins. MODPATH= and STEAMMOD=
//             are interleaved in command-line order (PHASE2_ANSWERS.md Q14: the original adds
//             each token to the same search vector as it parses it). See VfsConfig::mod_search.
//             Only for paths starting with Data\ (after normalisation).
//   2. Base : FoC loose <foc_root>\Data\<rest>.
//   3. Meg  : mounted MEG archives, newest-mounted first. Evidence: statically, Add_Megafile
//             appends the archive at the TAIL of its list, so newest-wins depends on Open_File
//             iterating in reverse, which was not verified statically (PHASE2_ANSWERS.md,
//             "Corrections", SUBSYSTEMS.md MEG list); in-game probe v2 confirmed at runtime that
//             the later-listed MEG wins. Mount order (later wins):
//             <EaW>\Data\MegaFiles.xml entries (file order), <EaW>\Data\Patch.meg, patch2.meg,
//             64Patch.meg, then FoC Data\MegaFiles.xml entries, then FoC Data\Patch.meg,
//             patch2.meg, 64Patch.meg. FoC's MegaFiles.xml and every FoC-side .meg are opened
//             through the loose tiers (mod -> base -> FALLBACKPATH dirs; MegaFiles.xml also via
//             already-mounted MEGs), so a mod's own MegaFiles.xml / Patch*.meg replace FoC's.
//             EaW-side files are always taken from the EaW install itself.
//             MegaFiles.xml MEGs go through the normal file open (Q14), so a mod's
//             Data\Config.meg is mounted INSTEAD of FoC's (the base copy is never mounted). A
//             requested name that is already mounted is skipped (the original de-duplicates by
//             the requested path string, case-insensitively; here: alamo::normalize_path of the
//             request, separately for the EaW and FoC sides).
//   4. Fallback: FALLBACKPATH= dirs in order, then <EaW>\Data (GameData). Data\ paths only.
// Paths are ASCII case-insensitive, '/' == '\\', leading ".\\" ignored (alamo::normalize_path).
// Loose tiers index only the <root>\Data subtree, so non-Data paths resolve from MEGs only.
//
// Thread safety: create() builds immutable indices; every const method may be called
// concurrently from any number of threads afterwards (MEG reads use the thread-safe
// alamo::meg::Archive sources).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/core/file_provider.hpp"

namespace alamo::meg {
class Archive;
}
namespace alamo {
class IssueSink;
}

namespace alamo::vfs {

// One entry of the ordered mod search list.
struct ModSearchEntry {
    enum class Kind : uint8_t { Path, SteamWorkshop };
    Kind kind = Kind::Path;
    std::string value;  // Path: host dir; SteamWorkshop: the workshop id as written
};

// Maps a STEAMMOD= workshop id to the subscribed item's folder. Return nullopt (or "") when the
// item is not subscribed/installed: like the original, nothing is mounted for that entry.
using SteamModResolver = std::function<std::optional<std::string>(std::string_view workshop_id)>;

struct VfsConfig {
    std::string foc_root;                      // folder containing FoC's Data\ (the "corruption" dir)
    // The mod search list in command-line order (MODPATH= and STEAMMOD= interleaved), earliest
    // wins. parse_command_line appends here.
    std::vector<ModSearchEntry> mod_search;
    std::vector<std::string> fallback_paths;   // FALLBACKPATH= dirs, in order
    std::optional<std::string> eaw_root;       // nullopt = auto-detect (<foc_root>\..\GameData with
                                               // sweaw.exe), "" = no EaW, else explicit GameData dir
    bool mount_megs = true;
    // Resolves SteamWorkshop entries of mod_search when the Vfs is created. Unset: the process-wide
    // Steam library lookup if an app installed it (steam_workshop.hpp, modc t10), else those entries
    // are skipped with the warning "no workshop resolver".
    SteamModResolver steam_resolver;
};

// Parses StarWarsG-style switches: MODPATH=, FALLBACKPATH=, STEAMMOD= (keys case-insensitive,
// values may be quoted). MODPATH= and STEAMMOD= are appended to cfg.mod_search in argument
// order; FALLBACKPATH= to cfg.fallback_paths. Relative paths are resolved against cfg.foc_root (set it first);
// backslashes are converted for the host. Unknown arguments are ignored. Returns false if a
// recognised switch had an empty value (the other switches are still applied).
bool parse_command_line(std::span<const std::string> args, VfsConfig& cfg);

// One layer of the resolution order and where it came from (ums0, docs/MODS.md §3.2 "Layers and provenance"). Every
// Vfs::Resolution names exactly one layer (Vfs::layer_of); code that asks "did this file come from a mod, and which one"
// reads Layer::mod instead of comparing host paths with the mod folders.
struct Layer {
    enum class Kind : uint8_t { Mod, Base, Meg, Fallback };  // = Vfs::Resolution::Layer
    Kind kind = Kind::Base;
    // Position within its kind, the same number as Vfs::Resolution::layer_index: Mod = search-list position (earliest 0),
    // Base = 0, Meg = mount index (0 = first mounted, lowest priority), Fallback = FALLBACKPATH dirs, then GameData last.
    int index = 0;
    // Mod / Base / Fallback: the folder as configured (the layer is its Data\ subtree). Meg: the .meg host path.
    std::string host_root;
    // The mod that owns the layer: its position in the effective mod search list (Vfs::mod_dirs() order, which is also the
    // order of the per-mod descriptor lists, e.g. GameData::mods) for a Mod layer and for a MEG opened from a mod's own
    // folder (a mod MegaFiles.xml entry or Patch*.meg); -1 = the base game (FoC, EaW, FALLBACKPATH dirs, their MEGs).
    int mod = -1;
    bool from_mod() const { return mod >= 0; }
    // CompatIssue::layer for a finding in this layer (compat_issue.hpp): the mod's index, or kBaseLayer.
    uint16_t issue_layer() const;
};

class Vfs final : public alamo::FileProvider {
public:
    struct Resolution {
        enum Layer { Mod, Base, Meg, Fallback } layer;
        // Loose: host path of the file. Meg: "<archive host path>#<ENTRY NAME AS STORED>".
        std::string location;
        // Mod: index into mod_dirs(). Fallback: FALLBACKPATH dirs, then GameData last.
        // Base: 0. Meg: mount index (0 = first mounted = lowest priority), see mounts().
        int layer_index;
    };
    struct Mount {
        std::string path;    // host path of the .meg
        std::string reason;  // e.g. "EaW MegaFiles.xml", "FoC Patch", "FoC 64Patch"
    };

    // Returns nullptr (and *err) only if foc_root is not a directory. Missing EaW, missing
    // MegaFiles.xml entries and unreadable MEGs are recorded in warnings() and skipped.
    // `issues`, when given, also receives the malformed MEG tables of meg_issues() as retail-equivalent CompatIssues with
    // the archive's layer (Layer::issue_layer); it is only written during create(). Diagnostics only (compat_issue.hpp).
    static std::unique_ptr<Vfs> create(const VfsConfig& cfg, std::string* err = nullptr, alamo::IssueSink* issues = nullptr);
    ~Vfs() override;
    Vfs(const Vfs&) = delete;
    Vfs& operator=(const Vfs&) = delete;

    bool read(std::string_view path, std::vector<uint8_t>& out) const override;
    bool exists(std::string_view path) const override;
    std::string origin(std::string_view path) const override;  // Resolution::location or ""

    std::optional<Resolution> resolve(std::string_view path) const;
    // The n-th copy of `path` in resolution order (mcx2 t07): n = 0 is what read() returns, n = 1 the copy the next
    // layer down holds (a mod's file -> the base game's), and so on. False (out empty) when fewer copies exist or the
    // read fails. `where`, when given, receives the copy's layer and location. For a fallback when a mod's copy of a
    // file fails to load; normal reads never use it.
    bool read_nth(std::string_view path, size_t n, std::vector<uint8_t>& out, Resolution* where = nullptr) const;
    // Every copy of `path` in resolution order (ums0): [0] is resolve(path), [1] the copy the next layer down holds, ...
    // (the copies read_nth reads). Empty when no layer has the file.
    std::vector<Resolution> resolve_all(std::string_view path) const;

    // The layers in resolution order, highest priority first: the mods (search order), the base FoC folder, the MEGs
    // newest-mounted first, the fallback folders (FALLBACKPATH dirs, then GameData). Loose layers serve Data\ paths only.
    const std::vector<Layer>& layers() const;
    // The layer a resolution came from; nullptr when `r` names none (an index out of range).
    const Layer* layer_of(const Resolution& r) const;
    // The logical paths one layer holds (ums2 t08), normalised: a loose layer's Data tree in byte order, a MEG's records in
    // stored order, only the names the game's lookup reaches (meg::Archive::lookup). Empty for a layer of another Vfs.
    std::vector<std::string> layer_files(const Layer& layer) const;

    // Union of all logical paths matching `glob` over every layer, each once, normalised
    // (upper-case, '\\'), in retail's find order (mcx3 t13): the mod trees, the base tree (each in byte order), then
    // every MEG in mount order with its records in stored order, then the fallback trees; the first occurrence of a
    // path stays. Glob: '*' and '?' do not cross '\\'; '**' does.
    // Example: list("Data\\Scripts\\AI\\*.lua").
    std::vector<std::string> list(std::string_view glob) const;
    // Layer bits for the restricted list(): which layers' files are searched.
    //   kLayerMod       the mod search list's Data trees
    //   kLayerBase      FoC loose <foc_root>\Data
    //   kLayerMeg       every mounted MEG (EaW and FoC)
    //   kLayerFallback  FALLBACKPATH dirs and the EaW GameData folder
    static constexpr uint32_t kLayerMod = 1u, kLayerBase = 2u, kLayerMeg = 4u, kLayerFallback = 8u;
    static constexpr uint32_t kLayerAll = kLayerMod | kLayerBase | kLayerMeg | kLayerFallback;
    // The same listing over the selected layers only (list(glob) == list(glob, kLayerAll)): the logical paths that
    // exist in at least one selected layer, each once, normalised, in the same order. A name present in a selected layer is
    // listed whether or not an unselected higher-priority layer overrides it (this lists names, not resolutions).
    // Example: list("Data\\CustomMaps\\*.TED", kLayerBase | kLayerMeg). Unknown bits are ignored.
    std::vector<std::string> list(std::string_view glob, uint32_t layers) const;

    // Multi-line dump of the layers and MEG mount order (highest priority first).
    std::string describe() const;

    const std::vector<Mount>& mounts() const;           // mount order (index 0 = lowest priority)
    // The effective mod search list (host dirs, earliest wins): resolved mod_search entries
    // (unresolved STEAMMOD ids dropped). Resolution::layer_index indexes this.
    const std::vector<std::string>& mod_dirs() const;
    const std::vector<std::string>& warnings() const;
    // mcx3 t04: malformed MEG tables found at mount (meg::Archive::table_issues); lookups follow the game's rule
    // (meg::Archive::lookup), so what the game loses is lost here too, and each finding is also a warnings() line.
    struct MegIssue {
        enum class Kind : uint8_t { Unsorted, Duplicate, NameMismatch, Index };
        Kind kind = Kind::Unsorted;
        // "meg_unsorted", "meg_duplicate", "meg_name_mismatch", "meg_index"
        static const char* kind_name(Kind k);
        std::string archive;  // host path of the .meg
        uint32_t count = 0;   // records concerned (meg_unsorted: records the lookup cannot reach, 0 when none is lost)
        std::string first;    // one record's stored name
    };
    const std::vector<MegIssue>& meg_issues() const;
    const std::string& eaw_root() const;                // "" if none
    const std::string& foc_root() const;                // host path of the FoC folder (the Base layer's root)

    struct Impl;

private:
    explicit Vfs(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// Layer::Kind and Vfs::Resolution::Layer name the same four kinds with the same values.
static_assert(static_cast<int>(Layer::Kind::Mod) == Vfs::Resolution::Mod && static_cast<int>(Layer::Kind::Base) == Vfs::Resolution::Base &&
              static_cast<int>(Layer::Kind::Meg) == Vfs::Resolution::Meg &&
              static_cast<int>(Layer::Kind::Fallback) == Vfs::Resolution::Fallback);

// Tolerant <File>...</File> extractor for MegaFiles.xml (comments skipped, entries trimmed,
// UTF-8/ASCII or UTF-16LE with BOM). Exposed for tests/diagnostics.
std::vector<std::string> parse_megafiles_xml(std::span<const uint8_t> bytes);

// The host apps' common mount (CU2 t10): cfg.foc_root = foc_root, then parse_command_line(game_args) (its result is
// ignored, as every host did: switches with an empty value are skipped, the rest apply), then Vfs::create. eaw_root is
// VfsConfig::eaw_root, set before the switches are parsed: nullopt = auto-detect (<foc_root>\..\GameData), "" = no EaW,
// else that folder. Which hosts pass an explicit EaW tier (the ALAMO_EAW_DATA environment route of the galactic AI and
// save runs) stays each caller's decision: it changes which AI definitions load (ai.goals CRC). On failure returns
// nullptr and sets *err to "VFS: <reason>". Hosts that must act on a bad switch call parse_command_line themselves.
std::unique_ptr<Vfs> mount_game(std::string_view foc_root, std::span<const std::string> game_args, std::string* err = nullptr,
                                std::optional<std::string> eaw_root = std::nullopt);

// Glob used by Vfs::list on normalised paths (case-insensitive, '/' == '\\').
bool path_glob_match(std::string_view pattern, std::string_view path);

}  // namespace alamo::vfs
