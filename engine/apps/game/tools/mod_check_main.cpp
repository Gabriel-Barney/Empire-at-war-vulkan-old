// alamo_mod_check - Q5 / modc t05 mod load report (alamo/game/mod_check.hpp): loads the data as alamo_game does and
// reports files, loader steps, content counts, campaign maps, missing files, unknown XML tags, Lua compile / load errors
// unbound engine calls and shader issues; optionally the machine-readable summary (docs/MODS.md §5.1).
//
//   alamo_mod_check [--game DIR] [MODPATH=..] [STEAMMOD=..] [FALLBACKPATH=..] [--out FILE] [--json FILE|-] [--verbose]
//                   [--no-campaigns] [--no-lua] [--no-lua-run] [--no-files] [--no-xml-tags] [--no-submod]
//                   [--no-shaders] [--no-dxc] [--no-fx-prebuild]
//   --game defaults to ALAMO_FOC_DATA. --json - writes the JSON to stdout instead of the text report.
//   STEAMMOD=<id> is found in the Steam library folders (alamo/vfs/steam_workshop.hpp); the workshop items of the same
//   folders and <game>\Mods\* are the parents the submod check considers (modc t16).
//   --no-shaders skips the effect audit (modc t21); --no-dxc keeps it to translation (no runtime DXC compile).
//   With a mod mounted the audit first compiles the mod's .fx sources with fxc into the FxCache (modc t36);
//   --no-fx-prebuild skips that (the audit then sees only what an earlier prebuild left in the cache).
//   mcx2 t01: every asset the mod layers serve is decoded (the report's `assets`); --no-assets skips it, --strict counts
//   salvaged assets as rejected, --corpus-cache DIR keeps a content-hash cache so a recheck decodes nothing new.
//   mcx3 t02: --resolve treats the mod arguments as the picked mods and checks the stack resolve_mod_stack builds from
//   them (declared modinfo dependencies, the submod hint's likely parent, cycle / base-EaW errors, conflict warnings);
//   --resolve-only prints the stack and stops (exit 1 on a stack error).
//   mcx7 t01: --list-mods writes the installed mods (the registry: workshop items, then <game>\Mods folders, one entry
//   per modinfo variant) as alamo.mod_list JSON to --json FILE|- (default stdout) and stops: the corpus runner's discovery.
//   ums1 t05: --list writes the install and the mod catalog (one row per folder and modinfo variant, keyed by catalog
//   key) as alamo.mod_catalog JSON; --conflicts writes which mod wins each shared file of the given load order (the
//   VFS's lookup, MEG records included) as alamo.mod_conflicts JSON. Both to --json FILE|- (default stdout) and stop.
//   --mod-dir DIR (repeatable) adds a user mod folder (one mod, or a folder of mods); --no-hash skips content hashes.
//   Without --game or ALAMO_FOC_DATA, --list searches the Steam libraries for the game (vfs::find_install).
//   Exit 0: no errors (warnings allowed), 1: errors, 2: usage.
//   Example (a Steam workshop mod):
//     alamo_mod_check --game "...\corruption" STEAMMOD=1125571106 --json report.json
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "alamo/game/mod_check.hpp"
#include "alamo/game/mod_conflicts.hpp"
#include "alamo/vfs/install_locations.hpp"
#include "alamo/vfs/mod_registry.hpp"
#include "alamo/vfs/steam_workshop.hpp"

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: alamo_mod_check [--game DIR] [MODPATH=..] [STEAMMOD=..] [FALLBACKPATH=..] [--out FILE] [--json FILE|-]\n"
                 "                       [--verbose] [--no-campaigns] [--no-lua] [--no-lua-run] [--no-files] [--no-xml-tags]\n"
                 "                       [--no-submod] [--no-shaders] [--no-dxc] [--no-fx-prebuild] [--no-assets] [--strict]\n"
                 "                       [--corpus-cache DIR] [--resolve | --resolve-only] [--list-mods]\n"
                 "                       [--list | --conflicts] [--mod-dir DIR] [--no-hash]\n");
    return 2;
}

bool write_file(const std::string& path, const std::string& text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return false;
    }
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    return true;
}

// The arguments as UTF-8, which every path string of the vfs is: on Windows from the wide command line, so a folder name
// outside the ANSI code page (a Steam library under a user name in another script) reaches the check whole.
std::vector<std::string> utf8_args(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    int wargc = 0;
    if (LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc)) {
        for (int i = 0; i < wargc; ++i) {  // as typed: no path conversion (a MODPATH= keeps its backslashes)
            const int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(len > 0 ? static_cast<size_t>(len) : size_t{1}, '\0');
            if (len > 0) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), len, nullptr, nullptr);
            s.resize(s.size() - 1);  // the terminator
            out.push_back(std::move(s));
        }
        LocalFree(wargv);
        return out;
    }
#endif
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
    return out;
}

}  // namespace

int main(int raw_argc, char** raw_argv) {
    const std::vector<std::string> argv = utf8_args(raw_argc, raw_argv);
    const size_t argc = argv.size();
    alamo::vfs::install_steam_workshop_lookup();  // modc t10: STEAMMOD= from the Steam library folders
    std::string game, out, json;
    std::vector<std::string> args;
    bool verbose = false, resolve = false, resolve_only = false, list_mods = false, list = false, conflicts = false, hash = true;
    std::vector<std::string> mod_dirs;
    alamo::game::ModCheckOptions opt;
    for (size_t i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--game" && i + 1 < argc) game = argv[++i];
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--json" && i + 1 < argc) json = argv[++i];
        else if (a == "--verbose") verbose = true;
        else if (a == "--resolve") resolve = true;
        else if (a == "--resolve-only") resolve = resolve_only = true;
        else if (a == "--list-mods") list_mods = true;
        else if (a == "--list") list = true;
        else if (a == "--conflicts") conflicts = true;
        else if (a == "--no-hash") hash = false;
        else if (a == "--mod-dir" && i + 1 < argc) mod_dirs.emplace_back(argv[++i]);
        else if (a == "--no-campaigns") opt.campaigns = false;
        else if (a == "--no-lua") opt.lua = false;
        else if (a == "--no-lua-run") opt.lua_run = false;
        else if (a == "--no-files") opt.files = false;
        else if (a == "--no-xml-tags") opt.xml_tags = false;
        else if (a == "--no-submod") opt.submod = false;
        else if (a == "--no-shaders") opt.shaders = false;
        else if (a == "--no-dxc") opt.shader_compile = false;
        else if (a == "--no-fx-prebuild") opt.fx_prebuild = false;
        else if (a == "--no-assets") opt.assets = false;
        else if (a == "--strict") opt.strict = true;
        else if (a == "--corpus-cache" && i + 1 < argc) opt.asset_cache_dir = argv[++i];
        else if (a.find('=') != std::string::npos) args.push_back(a);  // MODPATH= etc.
        else return usage();
    }
    if (game.empty())
        if (const char* env = std::getenv("ALAMO_FOC_DATA")) game = env;
    if (list || conflicts) {  // ums1 t05: the catalog / conflicts JSON, for the Command Center
        alamo::vfs::InstallLocations where;
        if (!game.empty()) {
            where = alamo::vfs::default_install_at(game);
        } else {  // no game named: the same search alamo_play makes, without game_path.txt
            alamo::vfs::InstallInputs in;
            in.steam_roots = alamo::vfs::default_steam_roots();
            where = alamo::vfs::find_install(in);
        }
        where.mod_folders = mod_dirs;
        const alamo::vfs::ModCatalog catalog = alamo::vfs::scan_mod_catalog(where, hash);
        std::string js;
        if (list) {
            js = alamo::game::format_mod_catalog_json(catalog, where);
        } else {
            if (where.foc.empty()) return usage();
            js = alamo::game::format_conflicts_json(alamo::game::mod_conflicts(catalog, args, where.steam_libraries));
        }
        if (json.empty() || json == "-") std::fputs(js.c_str(), stdout);
        else if (!write_file(json, js)) return 1;
        return 0;
    }
    if (game.empty()) return usage();
    if (list_mods) {
        const auto libraries = alamo::vfs::default_install_at(game).steam_libraries;
        const std::string js = alamo::game::format_mod_list_json(alamo::vfs::scan_installed_mods(game, libraries, true));
        if (json.empty() || json == "-") std::fputs(js.c_str(), stdout);
        else if (!write_file(json, js)) return 1;
        return 0;
    }
    if (opt.submod || resolve) opt.workshop_libraries = alamo::vfs::default_install_at(game).steam_libraries;  // ums1 t01

    alamo::vfs::ModStack stack;
    if (resolve) {
        stack = alamo::game::resolve_mod_stack(game, args, opt);
        if (resolve_only || !stack.errors.empty()) {
            alamo::game::ModCheckReport r;
            r.game = game;
            r.args = stack.args;
            r.loaded = true;
            r.resolved = true;
            r.stack = stack;
            const std::string s = alamo::game::format_mod_report(r, verbose);
            const std::string stack_text = s.substr(0, s.find("== files"));
            if (json != "-") std::fputs(stack_text.c_str(), stdout);
            if (!out.empty() && !write_file(out, stack_text)) return 1;
            if (!json.empty()) {
                const std::string js = alamo::game::format_mod_report_json(r);
                if (json == "-") std::fputs(js.c_str(), stdout);
                else if (!write_file(json, js)) return 1;
            }
            return stack.errors.empty() ? 0 : 1;
        }
        args = stack.launch_args();  // ums1 t04: what a launch starts (each folder once)
    }

    alamo::game::ModCheckReport report = alamo::game::check_mod_data(game, args, opt);
    if (resolve) {
        report.resolved = true;
        report.stack = std::move(stack);
    }
    const std::string text = alamo::game::format_mod_report(report, verbose);
    if (json != "-") std::fputs(text.c_str(), stdout);
    if (!out.empty() && !write_file(out, text)) return 1;
    if (!json.empty()) {
        const std::string js = alamo::game::format_mod_report_json(report);
        if (json == "-") std::fputs(js.c_str(), stdout);
        else if (!write_file(json, js)) return 1;
    }
    return report.error_count() ? 1 : 0;
}
