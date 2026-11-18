// Script files: lookup through the VFS and a static scan of what scripts call.
//
// The original resolves `require` / script names with LUA_PATH "./?.lua;./?.lc" followed by "R?.lua;R?.lc" for each
// script root R in registration order: Data\Scripts\{Library, GameObject, FreeStore, Miscellaneous, Story, AI,
// AI\SpaceMode, AI\LandMode, AI\StoryArc, Interventions, Evaluators}\ (mcx6 t07, docs/re/LUA_API.md "Script lookup and
// require" [H]; the order of the AI and Evaluators roots [M]). lua_load accepts source and Lua 5.0 bytecode alike
// (signature "\033Lua"); retail 32-bit bytecode is rejected by a 64-bit VM's header check, which is why the x64 patch
// ships plain sources (every FoC script in 64Patch.meg / extracted/foc_data_text.zip is text).
// The AI plan / evaluator discovery lists *.lua only (ai_lua_runtime.hpp), so a plan or evaluator that exists only as .lc
// is never found. [port] The plan, evaluator and free-store loaders read the discovered / named .lua file itself instead of
// going through LUA_PATH (retail's require: a same-named file in an earlier directory would shadow it; none in FoC / EaW).
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace alamo {
class FileProvider;
}

namespace alamo::lua {

std::span<const std::string_view> script_roots();  // "Data\\Scripts\\Library\\", ... (registration order)

// True if `bytes` is a precompiled Lua chunk (starts with "\033Lua").
bool is_bytecode(std::span<const uint8_t> bytes);

// Finds `name` (no extension; '/' or '\\' allowed) along LUA_PATH and reads it: NAME.lua then NAME.lc in the install
// folder, then in each root in order. Returns the game-relative path found ("" if none).
std::string read_script(const FileProvider& files, std::string_view name, std::vector<uint8_t>& out);

// Static scan of script sources (no execution): which globals / methods they call and define.
// A small Lua lexer (comments, long strings and strings skipped). Calls are `Name(`, `Name "s"`,
// `Name {`; method calls are `.Name(` / `:Name(` with the receiver name when it is a plain identifier.
struct ScriptScan {
    std::map<std::string, uint32_t> global_calls;   // name -> call sites
    std::map<std::string, uint32_t> method_calls;   // name -> call sites (any receiver)
    std::map<std::string, uint32_t> library_calls;  // "string.format" etc. (receiver is a std library)
    std::set<std::string> defined;                  // globals defined by scripts (function X / X = ...)
    uint32_t files = 0;
};
void scan_script(std::string_view source, ScriptScan& scan);

// Report: every called global / method classified as engine-implemented, engine-unbound, Lua
// builtin, script-defined or unknown, with call counts and totals.
struct ScanSummary {
    uint32_t engine_globals_implemented = 0, engine_globals_unbound = 0;
    uint32_t engine_methods_implemented = 0, engine_methods_unbound = 0;
    uint64_t unbound_call_sites = 0, implemented_call_sites = 0;
    uint32_t unknown_globals = 0;
};
std::string scan_report(const ScriptScan& scan, ScanSummary* summary = nullptr);

}  // namespace alamo::lua
