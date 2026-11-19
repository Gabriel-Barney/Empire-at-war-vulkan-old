// ModEnvironment (ums2, Stage 10, docs/MODS.md 3.2 "One mod environment per process"): the one object that opens a
// game's files for a process. From the StarWarsG-style command line it builds the mount plan (VfsConfig through
// parse_command_line), creates the Vfs (findings into the caller's IssueSink with their layer), describes every mounted
// mod folder (describe_mounted_mod, Vfs::mod_dirs() order), applies the base-EaW refusal (D1) and keeps the layer table
// with each layer's owning ModDescriptor. alamo_game, alamo_headless, alamo_mod_check and session_run open through it, so
// "which mods are mounted and which mod serves this file" has one answer per process.
//
// Retail is still the authority on what the command line means (docs/MODS.md 3.1, 3.2): the environment never reorders
// or adds an argument; descriptors feed reports, the picker and save headers only, nothing in gameplay reads them.
//
// Thread safety: open() builds everything; every const method may be called concurrently afterwards (as Vfs).
#pragma once
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/core/file_provider.hpp"
#include "alamo/vfs/mod_registry.hpp"
#include "alamo/vfs/vfs.hpp"

namespace alamo {
class IssueSink;
}

namespace alamo::vfs {

struct ModEnvironmentOptions {
    // VfsConfig::eaw_root: nullopt = auto-detect (<game>\..\GameData), "" = no EaW, else that folder (mount_game's rule:
    // which hosts pass an explicit EaW tier stays each caller's decision).
    std::optional<std::string> eaw_root;
    // VfsConfig::steam_resolver for STEAMMOD= entries; unset = the process-wide lookup (steam_workshop.hpp).
    SteamModResolver steam_resolver;
    // Compute each mounted mod's content hash (ModDescriptor::content_hash: save fingerprints, the corpus cache).
    bool hash = true;
};

class ModEnvironment final : public alamo::FileProvider {
public:
    // Opens `game` (the FoC folder) with `args` (MODPATH= / STEAMMOD= / FALLBACKPATH=; switches with an empty value are
    // skipped, as every host did). `issues` receives the VFS findings (Vfs::create) during open() only.
    // nullptr and *error = "VFS: <reason>" when the FoC folder does not open. A base-EaW mod in the stack (D1): the
    // environment is still returned, so a report can name the mod, with refused() true and *error = refusal(); a host
    // that runs the game stops there.
    static std::unique_ptr<ModEnvironment> open(std::string_view game, std::span<const std::string> args, alamo::IssueSink& issues,
                                                std::string* error = nullptr, ModEnvironmentOptions options = {});
    ~ModEnvironment() override;
    ModEnvironment(const ModEnvironment&) = delete;
    ModEnvironment& operator=(const ModEnvironment&) = delete;

    // FileProvider: the Vfs's answers.
    bool read(std::string_view path, std::vector<uint8_t>& out) const override;
    bool exists(std::string_view path) const override;
    std::string origin(std::string_view path) const override;

    const Vfs& vfs() const;
    Vfs& vfs();
    const std::string& game() const;                // the FoC folder as given
    const std::vector<std::string>& args() const;   // the command line as given (save descriptors)
    const VfsConfig& plan() const;                  // the mount plan the Vfs was created from
    // One descriptor per mounted mod folder, Vfs::mod_dirs() order (= Layer::mod).
    const std::vector<ModDescriptor>& mounted() const;
    // The descriptor that owns `layer` (Layer::mod); nullptr for a base-game layer.
    const ModDescriptor* owner(const Layer& layer) const;
    // The descriptor of the mod that serves `path` (its top copy), nullptr when the base game serves it or no layer has it.
    const ModDescriptor* owner_of(std::string_view path) const;
    bool refused() const;                // D1: a mounted mod is a base-EaW mod
    const std::string& refusal() const;  // "<name> (<key>): base-EaW mod: retail runs it with sweaw.exe", else ""

private:
    struct Impl;
    explicit ModEnvironment(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace alamo::vfs
