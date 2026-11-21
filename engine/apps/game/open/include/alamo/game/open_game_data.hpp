// ref4 t05: the one way every host opens the game's data - alamo_game (load_game_data) and each alamo_headless runner
// (--economy, --story, --map, --battles, --galactic-ai, --skirmish-ai foc, the save runs). It picks the FoC folder
// (`game_dir`, else ALAMO_FOC_DATA), opens it through vfs::ModEnvironment (the mod stack from the VFS arguments) and turns
// the D1 base-EaW refusal into a status, so no host can forget that check. What each host does with a failure (exit code,
// skip line, error text) stays the host's.
//
// Library alamo_game_open (vfs + core only): alamo_headless's Phase 2 main.cpp builds without alamo_sim, where
// alamo_game_core does not exist, so the function sits in this small library that alamo_game_core links publicly.
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "alamo/core/compat_issue.hpp"
#include "alamo/vfs/mod_environment.hpp"

namespace alamo::game {

// The FoC folder a host opens: `game_dir` when given, else the ALAMO_FOC_DATA environment variable ("" when neither).
std::string game_data_root(std::string_view game_dir);
// ALAMO_EAW_DATA as an explicit EaW tier (nullopt when unset or empty: the VFS auto-detects <game>\..\GameData).
std::optional<std::string> eaw_data_from_env();

struct OpenGameDataOptions {
    // vfs::ModEnvironmentOptions::eaw_root (nullopt = auto-detect). The headless galactic runs pass eaw_data_from_env() on
    // the environment route (no --game); everything else auto-detects.
    std::optional<std::string> eaw_root;
    // vfs::ModEnvironmentOptions::hash: each mod's content hash. alamo_game's saves need it; the headless runs read none.
    bool hash = false;
};

struct OpenedGameData {
    enum class Status : uint8_t {
        Opened,    // env is the game's files
        NoData,    // no folder: neither game_dir nor ALAMO_FOC_DATA
        VfsError,  // the folder does not open (error = the VFS reason)
        Refused,   // D1: a base-EaW mod in the stack (error = the refusal; env is kept so a report can name the mod)
    };
    Status status = Status::NoData;
    std::string root;                          // the folder tried ("" for NoData)
    std::unique_ptr<vfs::ModEnvironment> env;  // set for Opened and Refused
    std::string error;
    bool ok() const { return status == Status::Opened; }
};

// Opens game_data_root(game_dir) with the VFS arguments `args` (MODPATH= / STEAMMOD= / FALLBACKPATH=). `issues` receives the
// VFS findings while it opens.
OpenedGameData open_game_data(std::string_view game_dir, std::span<const std::string> args, IssueSink& issues,
                              OpenGameDataOptions options = {});

}  // namespace alamo::game
