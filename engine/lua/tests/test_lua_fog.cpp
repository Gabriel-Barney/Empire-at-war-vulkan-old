// engine/lua fog follow-up: the FogOfWar family (bind_fog.cpp, alamo/lua/fog_bridge.hpp; spec docs/re/FOG_OF_WAR.md §6):
// Reveal (object form: own sight / enemy footprint; position form with radius / dense radius), the LuaFOWCellsClass
// snapshot and Undo_Reveal (second call a no-op, other epochs ignored), Reveal_All (returns nothing), Temporary_Reveal
// (flash), Disable_Rendering (fog off for game logic; carried flags outside a battle), the error messages, recount
// after a "load", and the family table itself.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "alamo/core/file_provider.hpp"
#include "alamo/core/host_fs.hpp"
#include "alamo/lua/ai_bridge.hpp"
#include "alamo/lua/bindings.hpp"
#include "alamo/lua/fog_bridge.hpp"
#include "alamo/lua/lua_vm.hpp"
#include "alamo/lua/object_bridge.hpp"
#include "alamo/lua/script_gaps.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/objects/object_store.hpp"
#include "alamo/objects/object_types.hpp"
#include "alamo/test/support.hpp"
#include "vm_internal.hpp"

#include <lua.h>

using namespace alamo;
using namespace alamo::lua;
namespace fs = std::filesystem;

using ::alamo::test::g_fail;

#ifdef ALAMO_LUA_HAVE_FOG
static std::string run(LuaVm& vm, const std::string& code) {
    std::string err;
    if (!vm.run(code, "test", &err)) return err.empty() ? "error" : err;
    return {};
}
static bool gnil(LuaVm& vm, const char* name) {
    lua_State* L = vm.state();
    lua_pushstring(L, name);
    lua_rawget(L, LUA_GLOBALSINDEX);
    const bool b = lua_isnil(L, -1);
    lua_pop(L, 1);
    return b;
}
static bool gtrue(LuaVm& vm, const char* name) {
    lua_State* L = vm.state();
    lua_pushstring(L, name);
    lua_rawget(L, LUA_GLOBALSINDEX);
    const bool b = lua_type(L, -1) == LUA_TBOOLEAN && lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return b;
}
static bool last_message_has(LuaVm& vm, const char* text) {
    return !vm.messages().empty() && vm.messages().back().find(text) != std::string::npos;
}
#endif

// ---- the family table (always built) ----
static void test_table() {
    for (const detail::ImplEntry& e : detail::impl_fog()) {
        int same = 0;
        for (const detail::ImplEntry& o : detail::implementations())
            if (o.kind == e.kind && std::strcmp(o.name, e.name) == 0 && std::strcmp(o.cls, e.cls) == 0) ++same;
        if (same != 1) {
            std::printf("  DUPLICATE binding %s.%s (%d implementations)\n", e.cls, e.name, same);
            ++g_fail;
        }
    }
    // Every RE-table row of the family is implemented (the global and its 4 methods, Undo_Reveal).
    int rows = 0;
    for (const BindingInfo& b : binding_table())
        if (std::strcmp(b.cls, "LuaFOWRevealCommandClass") == 0 || std::strcmp(b.cls, "LuaFOWCellsClass") == 0) {
            ++rows;
            CHECK(binding_implemented(b));
        }
    CHECK(rows == 6);
}

#ifdef ALAMO_LUA_HAVE_FOG
namespace fw = alamo::tactical::fog;

using ::alamo::test::write_file;

struct Db {
    fs::path root;
    std::unique_ptr<DirectoryProvider> dp;
    objects::ObjectTypeDatabase raw;
    objects::GameObjectTypes types;
    bool ok = false;
    Db() {
        root = fs::temp_directory_path() / "alamo_lua_fog_test";
        std::error_code ec;
        remove_all_long(root, ec);
        write_file(root / "Data/XML/GameObjectFiles.xml", "<L><File>T.xml</File></L>");
        write_file(root / "Data/XML/T.xml",
                   "<R>\n<GroundInfantry Name=\"Scout\"><Land_FOW_Reveal_Range>100</Land_FOW_Reveal_Range></GroundInfantry>\n"
                   "<Marker Name=\"Spot\"><Behavior>MARKER</Behavior></Marker>\n</R>");
        dp = std::make_unique<DirectoryProvider>(root.string());
        ok = raw.load(*dp) && types.build(raw);
    }
    ~Db() {
        std::error_code ec;
        remove_all_long(root, ec);
    }
    objects::TypeIndex t(const char* name) const { return raw.find(name); }
};

// A land fog over [0, 1500] x [0, 1500] (15-unit cells: 100 x 100), players 0 (human) and 1.
struct Env {
    objects::IdCounter ids;
    objects::ObjectStore store;
    ObjectBridge bridge;
    TacticalScriptHooks hooks;
    FogScriptHooks fog_hooks;
    fw::FogOfWar fog;
    std::unique_ptr<LuaVm> vm;
    bool globals_set = false, globals_active = true;
    explicit Env(const Db& db) : store(objects::ModeKind::Land, objects::StoreKind::Primary, db.types, &ids) {
        fw::FogSetup s;
        s.mode = fw::FogMode::Land;
        s.box = {0.0f, 0.0f, 1500.0f, 1500.0f};
        s.players = {{0, true, true}, {1, true, false}};
        fog.start(s);
        fog.set_frame(1);
        fog_hooks.fog = &fog;
        fog_hooks.epoch = 7;
        fog_hooks.object = [this](objects::ObjectHandle h, fw::FogObject& o) {
            if (!store.alive(h)) return false;
            const Vec3 p = store.position(h);
            o = fw::FogObject{};
            o.owner = store.owner(h);
            o.x = p.x;
            o.y = p.y;
            o.sight_range = 100.0f;
            o.has_model = true;
            o.box_hx = o.box_hy = 20.0f;
            return true;
        };
        fog_hooks.set_globals = [this](bool active, bool) {
            globals_set = true;
            globals_active = active;
        };
        hooks.fog = &fog_hooks;
        bridge.tactical = &hooks;
        LuaHost host;
        host.store = &store;
        host.types = &db.types;
        vm = std::make_unique<LuaVm>(host);
        attach_object_bridge(*vm, &bridge);
    }
    objects::ObjectHandle make(objects::TypeIndex t, int32_t owner, Vec3 p) {
        objects::SpawnParams sp;
        sp.type = t;
        sp.owner = owner;
        sp.position = p;
        sp.facing = {1.0f, 0.0f, 0.0f};
        return store.create(sp);
    }
    uint32_t cell(float x, float y) const { return fog.grid(0)->index_of(x, y); }
    uint16_t count0(float x, float y) const { return fog.grid(0)->count_at(cell(x, y)); }
};

static void test_reveal(const Db& db) {
    Env e(db);
    LuaVm& vm = *e.vm;
    e.make(db.t("Scout"), 0, {300, 300, 0});
    e.make(db.t("Scout"), 1, {1000, 1000, 0});
    e.make(db.t("Spot"), 1, {700, 300, 0});
    CHECK(run(vm, "spot = Find_First_Object('Spot') "
                  "for _, o in Find_All_Objects_Of_Type('Scout') do if o.Get_Owner().Get_ID() == 0 then own = o else enemy = o end end "
                  "p0 = own.Get_Owner()") == "");
    CHECK(!gnil(vm, "enemy") && !gnil(vm, "p0"));
    // Object form, allied: the object's own sight (radius 100 -> ~7 cells): a point 90 away is counted.
    CHECK(run(vm, "c1 = FogOfWar.Reveal(p0, own)") == "" && !gnil(vm, "c1"));
    CHECK(e.count0(300, 300) == 1 && e.count0(390, 300) == 1 && !e.fog.point_fogged(0, 390, 300, false));
    // Object form, enemy: its footprint only (box half extent 20): 90 away stays uncounted.
    CHECK(run(vm, "c2 = FogOfWar.Reveal(p0, enemy)") == "" && !gnil(vm, "c2"));
    CHECK(e.count0(1000, 1000) == 1 && e.count0(1090, 1000) == 0);
    // Position form: radius 200 around a marker; dense radius defaults to it.
    CHECK(run(vm, "c3 = FogOfWar.Reveal(p0, spot, 200)") == "" && e.count0(700, 300) == 1 && e.count0(880, 300) == 1);
    CHECK(e.fog_hooks.cells.entries().size() == 3);
    // Undo: decrements and empties; the second call does nothing; the state stays until the decay.
    CHECK(run(vm, "c1.Undo_Reveal()") == "" && e.count0(300, 300) == 0 && !e.fog.point_fogged(0, 300, 300, false));
    CHECK(run(vm, "c1.Undo_Reveal()") == "" && e.count0(300, 300) == 0 && e.fog_hooks.cells.entries().size() == 2);
    for (uint32_t f = 2; f <= 260; ++f) e.fog.decay_step(f);
    CHECK(e.fog.point_fogged(0, 300, 300, false) && !e.fog.point_fogged(0, 700, 300, false));
    // Recount after a "load": the held snapshots are counted again (c2, c3).
    for (uint32_t i = 0; i < e.fog.grid(0)->cells(); ++i) CHECK(e.fog.grid(0)->count_at(i) <= 1);
    e.fog_hooks.cells.recount(e.fog);
    CHECK(e.count0(700, 300) == 2 && e.count0(1000, 1000) == 2 && e.count0(300, 300) == 0);
    // A handle of another fog instance (epoch) is ignored.
    e.fog_hooks.epoch = 8;
    CHECK(run(vm, "c3.Undo_Reveal()") == "" && e.count0(700, 300) == 2);
    e.fog_hooks.epoch = 7;
    CHECK(run(vm, "c3.Undo_Reveal()") == "" && e.count0(700, 300) == 1);
    // Bad arguments: script errors (logged, no unwind), nothing returned.
    CHECK(run(vm, "r = FogOfWar.Reveal(p0)") == "" && gnil(vm, "r") && last_message_has(vm, "Requires at least 2 parameters"));
    CHECK(run(vm, "r = FogOfWar.Reveal(3, own)") == "" && gnil(vm, "r") && last_message_has(vm, "Expected player object as first parameter"));
    CHECK(run(vm, "r = FogOfWar.Reveal(p0, 'x')") == "" && gnil(vm, "r") && last_message_has(vm, "Expected 3rd parameter for reveal radius"));
    CHECK(run(vm, "r = FogOfWar.Reveal(p0, 'x', 50)") == "" && gnil(vm, "r") && last_message_has(vm, "Undable to get a valid position"));
    CHECK(run(vm, "r = FogOfWar.Reveal(p0, own, 'x')") == "" && gnil(vm, "r") && last_message_has(vm, "Expected number as 3rd parameter"));
    // The global called directly: nothing.
    CHECK(run(vm, "r = FogOfWar(p0)") == "" && gnil(vm, "r"));
}

static void test_others(const Db& db) {
    Env e(db);
    LuaVm& vm = *e.vm;
    e.make(db.t("Scout"), 0, {300, 300, 0});
    e.make(db.t("Scout"), 1, {1000, 1000, 0});
    CHECK(run(vm, "for _, o in Find_All_Objects_Of_Type('Scout') do if o.Get_Owner().Get_ID() == 0 then own = o else enemy = o end end "
                  "p0 = own.Get_Owner() p1 = enemy.Get_Owner()") == "");
    // Reveal_All: returns nothing (scripts calling .Undo_Reveal() on it raise a Lua error, as in the original).
    CHECK(run(vm, "ra = FogOfWar.Reveal_All(p1)") == "" && gnil(vm, "ra") && e.fog.grid(1)->reveal_all_active());
    CHECK(run(vm, "FogOfWar.Reveal_All(p1).Undo_Reveal()") != "");
    CHECK(run(vm, "FogOfWar.Reveal_All()") == "" && last_message_has(vm, "Requires at least 1 parameters"));
    // Temporary_Reveal: a flash (state 255, uncounted) at the enemy's footprint, radius 60 given.
    CHECK(run(vm, "FogOfWar.Temporary_Reveal(p0, enemy, 60)") == "");
    CHECK(e.fog.grid(0)->state_at(e.cell(1050, 1000)) == fw::kStateRevealed && e.count0(1050, 1000) == 0);
    CHECK(run(vm, "FogOfWar.Temporary_Reveal(p0, 5)") == "" && last_message_has(vm, "Expected object as 2nd parameter"));
    CHECK(run(vm, "FogOfWar.Temporary_Reveal(p0, enemy, 'x')") == "" && last_message_has(vm, "Expected number as 3rd parameter"));
    // Disable_Rendering(true): fog off for game logic too; no argument = off as well; false turns it back on.
    CHECK(e.fog.point_fogged(0, 50, 1400, false));
    CHECK(run(vm, "FogOfWar.Disable_Rendering(true)") == "" && !e.fog.fow_active() && !e.fog.render_option() &&
          !e.fog.point_fogged(0, 50, 1400, false));
    CHECK(run(vm, "FogOfWar.Disable_Rendering(false)") == "" && e.fog.fow_active() && e.fog.point_fogged(0, 50, 1400, false));
    CHECK(run(vm, "FogOfWar.Disable_Rendering()") == "" && !e.fog.fow_active());
    // Outside a tactical mode (fog null): the tactical commands refuse; Disable_Rendering sets the carried flags.
    e.fog_hooks.fog = nullptr;
    CHECK(run(vm, "r = FogOfWar.Reveal(p0, own)") == "" && gnil(vm, "r") && last_message_has(vm, "Command only valid in a tactical game"));
    CHECK(run(vm, "FogOfWar.Disable_Rendering(true)") == "" && e.globals_set && !e.globals_active);
    // No hooks at all (no bridge.tactical->fog): the same refusal, Undo_Reveal on an old handle is harmless.
    e.fog_hooks.fog = &e.fog;
    CHECK(run(vm, "keep = FogOfWar.Reveal(p0, own)") == "" && !gnil(vm, "keep"));
    e.hooks.fog = nullptr;
    CHECK(run(vm, "r = FogOfWar.Reveal_All(p0)") == "" && last_message_has(vm, "Command only valid in a tactical game"));
    CHECK(run(vm, "keep.Undo_Reveal() ok = true") == "" && gtrue(vm, "ok"));
}

// ref5 t03: an AI host with one region target (6, a position and no object) and one task force (3, its units).
struct RegionAi final : AiScriptHost {
    std::vector<objects::ObjectHandle> units;
    bool tf_valid(int32_t tf) override { return tf == 3; }
    int32_t tf_force_count(int32_t) override { return static_cast<int32_t>(units.size()); }
    void tf_set_flag(int32_t, AiTfFlag, bool) override {}
    int32_t tf_produce_force(int32_t, const AiArg&, bool) override { return -1; }
    int32_t tf_get_stage(int32_t) override { return -1; }
    bool tf_all_on_free_store(int32_t) override { return false; }
    int32_t tf_order(int32_t, AiTfOrder, const AiArg&) override { return -1; }
    bool tf_is_raid_capable(int32_t) override { return false; }
    void tf_release_forces(int32_t, double) override {}
    void tf_release_unit(int32_t, objects::ObjectHandle) override {}
    std::vector<objects::ObjectHandle> tf_units(int32_t tf) override { return tf == 3 ? units : std::vector<objects::ObjectHandle>{}; }
    std::string tf_goal_type_name(int32_t) override { return {}; }
    bool tf_collect_free_units(int32_t, uint64_t) override { return false; }
    bool status_finished(int32_t) override { return true; }
    AiStatusResult status_result(int32_t) override { return AiStatusResult::Nil; }
    bool target_valid(int32_t t) override { return t == 6; }
    objects::ObjectHandle target_object(int32_t) override { return {}; }
    bool target_position(int32_t t, Vec3& out) override {
        if (t != 6) return false;
        out = {700.0f, 300.0f, 0.0f};
        return true;
    }
    double target_distance(int32_t, const AiArg&) override { return -1.0; }
    double budget_op(int32_t, AiBudgetOp, double, std::string_view) override { return 0.0; }
    int32_t budget_wait(int32_t, AiBudgetOp, double) override { return -1; }
    int32_t free_store_count(int32_t, int) override { return 0; }
    bool free_store_has(int32_t, objects::ObjectHandle) override { return false; }
    bool unit_in_transit(objects::ObjectHandle) override { return false; }
    bool unit_safe(objects::ObjectHandle) override { return true; }
    bool free_store_move(int32_t, objects::ObjectHandle, const AiArg&) override { return false; }
    int32_t find_target(int32_t, const AiFindTargetRequest&) override { return -1; }
    int32_t find_stage_area(int32_t, const AiArg&, const AiArg&, const AiArg&) override { return -1; }
    AiGlobalValue global_value(std::string_view) override { return {}; }
    void set_global_value(std::string_view, const AiGlobalValue&) override {}
};

// ref5 t03: FogOfWar converts its position argument by the one rule every binding family uses (detail::position_arg):
// a region AI target reveals around its position, a task force around its first live unit, an empty task force is a
// dead argument.
static void test_ai_positions(const Db& db) {
    Env e(db);
    LuaVm& vm = *e.vm;
    e.make(db.t("Scout"), 0, {300, 300, 0});
    const objects::ObjectHandle gone = e.make(db.t("Scout"), 0, {1200, 300, 0});
    const objects::ObjectHandle far = e.make(db.t("Scout"), 0, {300, 1200, 0});
    RegionAi ai;
    attach_ai_host(vm, &ai, 1);
    set_ai_target_global(vm, "Region", 6);
    set_task_force_global(vm, "TF", 3, AiTaskForceKind::Land);
    CHECK(run(vm, "for _, o in Find_All_Objects_Of_Type('Scout') do p0 = o.Get_Owner() end") == "" && !gnil(vm, "p0"));
    CHECK(e.count0(700, 300) == 0);
    CHECK(run(vm, "r = FogOfWar.Reveal(p0, Region, 100)") == "" && !gnil(vm, "r"));
    CHECK(e.count0(700, 300) == 1 && e.count0(780, 300) == 1 && e.count0(900, 300) == 0);
    // Task force: its first live unit (the dead one is skipped).
    ai.units = {gone, far};
    CHECK(e.store.destroy(gone));
    CHECK(run(vm, "t = FogOfWar.Reveal(p0, TF, 50)") == "" && !gnil(vm, "t"));
    CHECK(e.count0(300, 1200) == 1 && e.count0(1200, 300) == 0);
    ai.units.clear();
    CHECK(run(vm, "n = FogOfWar.Reveal(p0, TF, 50)") == "" && gnil(vm, "n") && last_message_has(vm, "Undable to get a valid position"));
    attach_ai_host(vm, nullptr, -1);
}
#endif

int main() {
    test_table();
#ifdef ALAMO_LUA_HAVE_FOG
    {
        Db db;
        CHECK(db.ok);
        if (db.ok) {
            test_reveal(db);
            test_others(db);
            test_ai_positions(db);
        }
    }
#else
    std::printf("fog library not built: only the family table is checked\n");
#endif
    if (g_fail) {
        std::printf("%d check(s) FAILED\n", g_fail);
        return 1;
    }
    std::printf("all lua fog tests passed\n");
    return 0;
}
