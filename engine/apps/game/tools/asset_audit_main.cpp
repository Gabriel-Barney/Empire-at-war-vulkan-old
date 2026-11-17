// alamo_asset_audit - mcx2 t01: the `assets` section of the mod load report (alamo/game/asset_audit.hpp) on its own.
// Decodes every art, audio, movie and map file the mounted mod layers serve and prints the section's JSON. Until the
// v4 report (mcx0) calls audit_mod_assets itself this tool is the seam; the section's JSON is the same either way.
//
//   alamo_asset_audit [--game DIR] [MODPATH=..] [STEAMMOD=..] [--strict] [--corpus-cache DIR] [--json FILE|-]
//   --game defaults to ALAMO_FOC_DATA. Exit 0: no rejection outside retail-equivalent cases, 1: some, 2: usage.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "alamo/game/asset_audit.hpp"
#include "alamo/vfs/steam_workshop.hpp"
#include "alamo/vfs/vfs.hpp"

int main(int argc, char** argv) {
    alamo::vfs::install_steam_workshop_lookup();
    std::string game, json = "-";
    std::vector<std::string> args;
    alamo::game::AssetAuditOptions opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--game" && i + 1 < argc) game = argv[++i];
        else if (a == "--json" && i + 1 < argc) json = argv[++i];
        else if (a == "--strict") opt.strict = true;
        else if (a == "--corpus-cache" && i + 1 < argc) opt.cache_dir = argv[++i];
        else if (a.find('=') != std::string::npos) args.push_back(a);
        else {
            std::fprintf(stderr, "usage: alamo_asset_audit [--game DIR] [MODPATH=..] [STEAMMOD=..] [--strict] "
                                 "[--corpus-cache DIR] [--json FILE|-]\n");
            return 2;
        }
    }
    if (game.empty())
        if (const char* env = std::getenv("ALAMO_FOC_DATA")) game = env;
    if (game.empty()) return 2;
    std::string err;
    auto v = alamo::vfs::mount_game(game, args, &err);
    if (!v) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    const auto a = alamo::game::audit_mod_assets(*v, opt);
    const std::string text = alamo::game::asset_audit_json(a, 0) + "\n";
    if (json == "-") {
        std::fputs(text.c_str(), stdout);
    } else if (FILE* f = std::fopen(json.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    } else {
        std::fprintf(stderr, "cannot write %s\n", json.c_str());
        return 2;
    }
    std::fprintf(stderr, "assets: %zu checked, %zu decoded, %zu salvaged, %zu rejected (%zu retail-equivalent), %zu cached\n",
                 a.checked, a.decoded, a.salvaged_count, a.rejected_count, a.retail_equivalent, a.cache_hits);
    return a.rejected_count > a.retail_equivalent ? 1 : 0;
}
