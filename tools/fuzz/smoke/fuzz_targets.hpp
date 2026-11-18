// Fuzz targets for the mod-file readers not covered by tools/fuzz/targets (mcx2 t08, docs/MOD_COMPAT_DESIGN.md 11.3):
// TED maps, MasterTextFile DAT, MTD atlases, GUIDialogs.rc, animation clips, particle systems, precompiled Lua (.lc)
// chunks, WAV / MP3 audio and Bink headers. Each target runs one input through the same entry the game uses on a mod's
// file; a reader that crashes, hangs or lets an exception escape is the bug the fuzzers look for.
// Shared by the seeded mutation harness (fuzz_smoke.cpp, a slow ctest entry in every tree) and the libFuzzer targets
// (targets/fuzz_mod_formats.cpp, ALAMO_FUZZ trees, build_fuzz.cmd).
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace alamo::fuzz {

using Bytes = std::vector<uint8_t>;

struct Target {
    const char* name;        // "ted", "dat", ...
    const char* globs;       // ';'-separated VFS / mod-folder globs the real-file seeds come from
    void (*run)(std::span<const uint8_t> bytes);
    std::vector<Bytes> (*synthetic)();  // small valid inputs (seeds without game data)
};

// The targets this build has (a module that is not configured leaves its target out).
const std::vector<Target>& targets();
const Target* find_target(const std::string& name);

}  // namespace alamo::fuzz
