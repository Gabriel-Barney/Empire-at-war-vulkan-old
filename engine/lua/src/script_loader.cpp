// Script lookup through the FileProvider (VFS) and the root list of the original.
#include <iterator>
#include <string>

#include "alamo/core/file_provider.hpp"
#include "alamo/core/ascii.hpp"
#include "alamo/lua/script_loader.hpp"

namespace alamo::lua {

namespace {

// mcx6 t07 (LUA_API.md "LUA_PATH" [H]): the registration order of the roots - boot init (Library, GameObject,
// FreeStore, Miscellaneous, Story), the AI plan scan (AI, AI\SpaceMode, AI\LandMode, AI\StoryArc, Interventions), the
// evaluator scan (Evaluators) [M: which scan registers first; it only orders those roots]. LUA_PATH puts "./" (the
// install folder) before them.
constexpr std::string_view kRoots[] = {
    "Data\\Scripts\\Library\\",       "Data\\Scripts\\GameObject\\",      "Data\\Scripts\\FreeStore\\",
    "Data\\Scripts\\Miscellaneous\\", "Data\\Scripts\\Story\\",           "Data\\Scripts\\AI\\",
    "Data\\Scripts\\AI\\SpaceMode\\", "Data\\Scripts\\AI\\LandMode\\",    "Data\\Scripts\\AI\\StoryArc\\",
    "Data\\Scripts\\Interventions\\", "Data\\Scripts\\Evaluators\\",
};
// The LUA_PATH directories in search order: "" (the install folder, "./"), then the roots.
constexpr std::string_view kLuaPath[] = {
    "",
    kRoots[0], kRoots[1], kRoots[2], kRoots[3], kRoots[4], kRoots[5], kRoots[6], kRoots[7], kRoots[8], kRoots[9], kRoots[10],
};
static_assert(std::size(kLuaPath) == std::size(kRoots) + 1);

}  // namespace

std::span<const std::string_view> script_roots() { return kRoots; }

bool is_bytecode(std::span<const uint8_t> bytes) {
    return bytes.size() >= 4 && bytes[0] == 0x1B && bytes[1] == 'L' && bytes[2] == 'u' && bytes[3] == 'a';
}

std::string read_script(const FileProvider& files, std::string_view name, std::vector<uint8_t>& out) {
    out.clear();
    std::string n(name);
    for (char& ch : n)
        if (ch == '/') ch = '\\';
    while (n.size() >= 2 && n[0] == '.' && n[1] == '\\') n.erase(0, 2);
    // LUA_PATH "./?.lua;./?.lc;R?.lua;R?.lc;..." [H]: per directory .lua before .lc. [port] A name that already ends in
    // .lua / .lc is tried as given in each directory (retail appends the template's extension to it).
    const bool has_ext = ascii_iends_with(n, ".lua") || ascii_iends_with(n, ".lc");
    for (const std::string_view root : kLuaPath) {
        if (has_ext) {
            const std::string p = std::string(root) + n;
            if (files.read(p, out)) return p;
            continue;
        }
        for (const char* ext : {".lua", ".lc"}) {
            const std::string p = std::string(root) + n + ext;
            if (files.read(p, out)) return p;
        }
    }
    out.clear();
    return {};
}

}  // namespace alamo::lua
