// Prebuild of effect sources (roadmap modc t23): runs fxc.exe from the project toolchain's Windows SDK on
// `.fx` files and fills an FxCache (fx_source.hpp). Used by alamo_fx_prebuild, alamo_effect_audit
// --fx-prebuild and the materials tests; not part of alamo_materials (the engine never runs a compiler).
#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "alamo/materials/effect_loader.hpp"
#include "alamo/materials/fx_source.hpp"

namespace alamo::materials {

// $ALAMO_FXC if set, else fxc\fxc.exe beside the running exe (test builds ship it there with d3dcompiler_47.dll,
// modc t36), else the fxc.exe CMake found under toolchain\msvc\Windows Kits\10\bin\<ver>\x64 (empty path when none
// exists).
std::filesystem::path default_fxc_path();
// "fxc <SDK version> <size of fxc.exe>/<size of the d3dcompiler_47.dll beside it>"; "" if missing. The version is the
// first line of SDK_VERSION.txt beside fxc.exe when there is one (test builds), else the SDK's bin\<ver> folder name.
std::string fxc_identity(const std::filesystem::path& fxc);

// Writes `src` (the .fx and its includes, at their game paths) to a fresh temporary folder, runs
// `fxc /nologo /T fx_2_0 /I <tmp>\Data\Art\Shaders /Fo <tmp>\out.fxo <tmp>\<path>` and fills `out`
// (ok + fxo, or !ok + the log; folder names stripped from the log). False only when fxc could not be
// run at all (*err); a compile error is a successful call with out.ok false.
bool compile_fx_source(const FxSource& src, const std::filesystem::path& fxc, FxCacheEntry& out, std::string* err = nullptr);

// First "error" line of an fxc log (else its first non-empty line), for one-line reports.
std::string fx_log_summary(const std::string& log);

// The `.fx` paths the loader would take for the effects in `shader_files` (one per effect name, the
// first match of EffectLibrary::find_effect_source, sorted by path).
std::vector<std::string> fx_source_paths(const EffectLibrary& lib, std::span<const ShaderFileEntry> shader_files);

struct FxPrebuildItem {
    std::string path;     // game path of the .fx
    std::string key;      // FxSource::key() ("" when the source cannot be read)
    bool cached = false;  // an entry already existed (not compiled again)
    bool ok = false;      // compiled (now or before)
    std::string detail;   // fx_log_summary() of a failure, or why nothing ran
    double ms = 0;        // collect + compile time
};

// Compiles every path whose key has no cache entry (all of them with `force`) and stores the results.
// False when fxc is missing or a store fails (items still filled).
bool prebuild_fx_sources(const FileProvider& files, std::span<const std::string> fx_paths, const FxCache& cache,
                         const std::filesystem::path& fxc, bool force, std::vector<FxPrebuildItem>& items,
                         std::string* err = nullptr);

// modc t36: the whole prebuild of one data set, as alamo_game and alamo_mod_check run it when a mod is mounted:
// every `.fx` the loader would take for `shader_files` (shader_files_from_vfs(vfs, true)) through
// prebuild_fx_sources without force. With fxc missing the cached entries still count and the others say so.
struct FxPrebuildRun {
    std::string fxc;                   // fxc_identity(), "" when fxc.exe was not found
    std::vector<FxPrebuildItem> items;  // by path
    bool complete = false;             // prebuild_fx_sources returned true (fxc found, every store written)
    std::string error;                 // its first error otherwise
    double ms = 0;
};
FxPrebuildRun prebuild_effect_sources(const FileProvider& files, std::span<const ShaderFileEntry> shader_files,
                                      const FxCache& cache, const std::filesystem::path& fxc);

}  // namespace alamo::materials
