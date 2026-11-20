#include "alamo/game/open_game_data.hpp"

#include <cstdlib>
#include <utility>

namespace alamo::game {

namespace {

std::string env_text(const char* name) {
#ifdef _MSC_VER
    char* v = nullptr;
    size_t len = 0;
    std::string out;
    if (_dupenv_s(&v, &len, name) == 0 && v) {
        out = v;
        std::free(v);
    }
    return out;
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

}  // namespace

std::string game_data_root(std::string_view game_dir) { return game_dir.empty() ? env_text("ALAMO_FOC_DATA") : std::string(game_dir); }

std::optional<std::string> eaw_data_from_env() {
    std::string eaw = env_text("ALAMO_EAW_DATA");
    if (eaw.empty()) return std::nullopt;
    return eaw;
}

OpenedGameData open_game_data(std::string_view game_dir, std::span<const std::string> args, IssueSink& issues, OpenGameDataOptions options) {
    OpenedGameData out;
    out.root = game_data_root(game_dir);
    if (out.root.empty()) return out;  // NoData
    vfs::ModEnvironmentOptions mopt;
    mopt.eaw_root = std::move(options.eaw_root);
    mopt.hash = options.hash;
    out.env = vfs::ModEnvironment::open(out.root, args, issues, &out.error, std::move(mopt));
    if (!out.env) {
        out.status = OpenedGameData::Status::VfsError;
    } else if (out.env->refused()) {  // D1: FoC mods only
        out.status = OpenedGameData::Status::Refused;
        out.error = out.env->refusal();
    } else {
        out.status = OpenedGameData::Status::Opened;
        out.error.clear();
    }
    return out;
}

}  // namespace alamo::game
