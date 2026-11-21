// Lua binding registry (roadmap L1 stage 1). The table of every binding the original registers is
// generated from docs/re/lua_api.json (engine/lua/tools/gen_bindings.py -> src/binding_table.inc):
// 143 globals + 323 wrapper methods.
//
// How the original exposes them (docs/re/LUA_API.md, "How the engine registers Lua bindings"), and
// how the port mirrors it:
//   * every engine global is a userdata object (one C++ LuaUserVar subclass per function); calling it
//     invokes its call operator. Here: a full userdata whose shared metatable (the global
//     LuaWrapperMetaTable) has __call / __index / __eq / __tostring.
//   * methods are looked up on the userdata by name and called with DOT syntax and no self
//     (`unit.Get_Type()`, `GameRandom.Get_Float()`): __index returns a closure bound to the object.
//     Lookup walks the class, its parent (Land/Space/GalacticTaskForceClass -> TaskForceClass), then
//     LuaUserVar (Is_Pool_Safe, which PGBase's Flush_G calls on every userdata global).
//   * a few globals are per-script VALUES the host sets (Object, Target, AITarget, PlayerObject,
//     FreeStore, LastService, LastUnitService, PlanDefinitionLoad): nil until set.
// Every other binding is registered. Those not implemented yet raise the Lua error
// "unbound: <name>" ("unbound: <Class>.<Method>" for methods) and are counted per name (LuaVm).
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace alamo::lua {

enum class BindingKind : uint8_t { Global, Method };

// What a binding does in the port compared with retail (docs/MOD_COMPAT_DESIGN.md §9.1, roadmap mcx0 t04), found from
// the code by gen_bindings.py: Recorded = appends a PresentationOp (BindingInfo::consumer = the alamo_game file that
// handles it, "" = none, "invisible: <reason>" = nothing to show by design); NoopRetail / NoopGap / Partial = an
// annotation "binding-effect:" in engine/lua/src (consumer = its spec or reason); Full = everything else.
enum class BindingEffect : uint8_t { Full, Recorded, NoopRetail, NoopGap, Partial };
const char* to_string(BindingEffect e);  // "full", "recorded", "noop-retail", "noop-gap", "partial"

struct BindingInfo {
    BindingKind kind;
    const char* cls;   // Method: owner wrapper class. Global: implementing class ("" if unknown).
    const char* name;  // Lua name
    uint32_t calls;    // call sites in the RE corpora (vanilla FoC + TR 3.3), for prioritising
    BindingEffect effect = BindingEffect::Full;
    const char* consumer = "";  // see BindingEffect
    // ref5 t04: the bridge hooks the binding reads, "Struct.hook" space separated (Struct = ObjectBridge,
    // RemainingScriptHooks or TacticalScriptHooks), found from the code by gen_bindings.py; "" = none.
    const char* hooks = "";
};
// A binding whose call may not get retail's effect in alamo_game: Recorded with no consumer, NoopGap or Partial (the load
// report's bindings.unconsumed, issue kind binding_unconsumed).
bool binding_effect_gap(const BindingInfo& b);

// The generated table: globals sorted by name, then methods sorted by (class, name).
std::span<const BindingInfo> binding_table();

// ref5 t04: every hook of the three hook structs between the bindings and the sim (generated: src/bridge_hooks.inc), with
// an is-set test; `hooks` points at a struct of type `bridge`.
class ObjectBridge;
struct RemainingScriptHooks;
struct TacticalScriptHooks;
struct BridgeHookInfo {
    const char* bridge;  // "ObjectBridge" / "RemainingScriptHooks" / "TacticalScriptHooks"
    const char* name;    // the member
    bool (*is_set)(const void* hooks);
};
std::span<const BridgeHookInfo> bridge_hook_table();
// The hook sets one or more hosts built (the story, AI and object-script VMs' bridges).
struct BridgeHookSets {
    std::vector<const ObjectBridge*> objects;
    std::vector<const RemainingScriptHooks*> remaining;
    std::vector<const TacticalScriptHooks*> tactical;
};
// True when some set of the hook's struct has it ("ObjectBridge.ship_name"); false for an unknown hook.
bool bridge_hook_wired(const BridgeHookSets& sets, std::string_view qualified);
// The Full bindings that read a hook no given set wires, one "Class.Name reads Struct.hook" line per pair (table order).
std::vector<std::string> unwired_full_binding_hooks(const BridgeHookSets& sets);

// Host-set value globals (see above).
bool is_host_value_global(std::string_view name);
// mcx8 t12: table globals the exe does not register (lua_api.json "registered": false; Declare_Goal_Type). The VM
// leaves them nil, as retail; their row counts as implemented (the retail behaviour - nil - is what the port does).
bool is_unregistered_global(std::string_view name);
// True when the port implements this binding (the rest are "unbound" stubs).
bool binding_implemented(const BindingInfo& b);

struct BindingCoverage {
    uint32_t globals = 0, methods = 0;                          // rows in the table (143 / 323)
    uint32_t host_values = 0;                                   // globals set by the host
    uint32_t globals_implemented = 0, methods_implemented = 0;  // the rest are unbound stubs
    uint32_t extra_methods = 0;  // implemented methods the RE table lacks (ThreadValue.Set)
    uint64_t calls_total = 0, calls_implemented = 0;            // weighted by RE call sites
};
BindingCoverage binding_coverage();
// Text report: totals, then every binding with its status (implemented / unbound / host value).
std::string binding_coverage_report();

}  // namespace alamo::lua
