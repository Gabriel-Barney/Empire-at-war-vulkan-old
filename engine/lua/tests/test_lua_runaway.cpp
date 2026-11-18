// mcx6 t08: the runaway-script guard (LuaVmOptions::script_budget, docs/MOD_COMPAT_DESIGN.md §10 P6).
//   * a script thread that never yields is stopped after the budget, reported as script_runaway, counted, and the
//     other threads and the VM go on; the same budget stops the same thread at the same instruction (pure count);
//   * a well-behaved thread that yields every resume runs far more instructions in total than one budget: the budget
//     is per resume, not per thread;
//   * a chunk whose main part never ends (run), a global call (call) and a host call through protected_call stop the
//     same way; budget 0 is no limit;
//   * no hook is left on the state afterwards: the VM still saves (vm_persist refuses a state with a debug hook).
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "alamo/core/bytes.hpp"
#include "alamo/lua/lua_vm.hpp"
#include "alamo/test/support.hpp"

#include <lua.h>

using alamo::lua::LuaHost;
using alamo::lua::LuaVm;
using alamo::lua::LuaVmOptions;
using ::alamo::test::g_fail;

namespace {

bool contains(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

std::unique_ptr<LuaVm> make_vm(int budget) {
    LuaVmOptions opt;
    opt.script_budget = budget;
    return std::make_unique<LuaVm>(LuaHost{}, opt);
}

double number(LuaVm& vm, const char* name) {
    double v = -1;
    vm.get_number(name, v);
    return v;
}

void test_threads() {
    auto vm = make_vm(100'000);
    CHECK(vm->run(R"(
        spins = 0
        steps = 0
        function Forever()
            while true do spins = spins + 1 end
        end
        function Polite()
            while true do
                for i = 1, 20000 do steps = steps + 1 end
                coroutine.yield()
            end
        end
    )", "runaway_threads"));
    const int32_t bad = vm->create_thread("Forever");
    const int32_t good = vm->create_thread("Polite");
    CHECK(bad >= 0 && good >= 0);
    for (int i = 0; i < 20; ++i) vm->service_threads();
    CHECK(!vm->thread_active(bad));
    CHECK(vm->thread_active(good));
    CHECK(vm->runaway_count() == 1);
    CHECK(vm->errors().size() == 1 && contains(vm->errors()[0], "script_runaway"));
    // 20 resumes of 20000 loop steps: several budgets' worth in total, never one budget in a single resume.
    CHECK(number(*vm, "steps") == 20 * 20000.0);
    const double spins = number(*vm, "spins");
    CHECK(spins > 0 && spins < 100'000);

    // Deterministic: a second VM stops the runaway at the same count.
    auto again = make_vm(100'000);
    CHECK(again->run("spins = 0 function Forever() while true do spins = spins + 1 end end", "runaway_again"));
    again->create_thread("Forever");
    again->service_threads();
    CHECK(number(*again, "spins") == spins);

    // No hook left behind: the state saves.
    alamo::ByteWriter w;
    vm->write(w);
    CHECK(vm->save_error().empty());
    if (!vm->save_error().empty()) std::printf("  save_error: %s\n", vm->save_error().c_str());
}

void test_host_calls() {
    auto vm = make_vm(100'000);
    std::string err;
    CHECK(!vm->run("local n = 0 while true do n = n + 1 end", "runaway_chunk", &err));
    CHECK(contains(err, "script_runaway"));
    CHECK(vm->run("function Spin() while true do end end function Fine() fine = 1 end", "runaway_defs"));
    err.clear();
    CHECK(!vm->call("Spin", &err) && contains(err, "script_runaway"));
    CHECK(vm->call("Fine") && number(*vm, "fine") == 1);
    lua_State* L = vm->state();
    lua_pushstring(L, "Spin");
    lua_rawget(L, LUA_GLOBALSINDEX);
    CHECK(vm->protected_call(L, 0, 0) != 0);
    CHECK(lua_isstring(L, -1) && contains(lua_tostring(L, -1), "script_runaway"));
    lua_pop(L, 1);
    CHECK(vm->runaway_count() == 3);

    // Budget 0: no limit (a long but finite loop runs to the end).
    auto open = make_vm(0);
    CHECK(open->run("n = 0 for i = 1, 300000 do n = n + 1 end", "no_budget"));
    CHECK(number(*open, "n") == 300000 && open->runaway_count() == 0);
}

}  // namespace

int main() {
    test_threads();
    test_host_calls();
    std::printf(g_fail ? "alamo_lua_runaway_tests: %d FAILED\n" : "alamo_lua_runaway_tests: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
