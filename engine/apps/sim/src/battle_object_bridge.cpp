// ref2 t04: the battle lifecycle hooks of a Lua ObjectBridge (battle_object_bridge.hpp).
#include "alamo/sim/battle_object_bridge.hpp"

#include <memory>
#include <string_view>
#include <utility>

#include "alamo/sim/game_modes.hpp"
#include "alamo/sim/tactical_battle.hpp"

#ifdef ALAMO_SIM_HAVE_STORY
#include "alamo/lua/object_bridge.hpp"
#endif

namespace alamo::sim {

#ifdef ALAMO_SIM_HAVE_STORY
void make_object_bridge(TacticalBattle& battle, std::function<TacticalMode*()> mode, lua::ObjectBridge& out,
                        std::function<bool()> allowed) {
    TacticalBattle* bt = &battle;
    // The mode the call may act on: null when refused (no mode yet, or the gate says no).
    auto ctx = std::make_shared<std::pair<std::function<TacticalMode*()>, std::function<bool()>>>(std::move(mode), std::move(allowed));
    auto mode_for = [ctx]() -> TacticalMode* {
        if (ctx->second && !ctx->second()) return nullptr;
        return ctx->first ? ctx->first() : nullptr;
    };
    out.battle_lifecycle = true;
    out.spawn_unit = [bt, mode_for](const objects::SpawnParams& p) {
        TacticalMode* tm = mode_for();
        return tm ? bt->script_spawn(*tm, p) : objects::ObjectHandle{};
    };
    out.spawn_company = [bt, mode_for](const objects::SpawnParams& p) {
        TacticalMode* tm = mode_for();
        return tm ? bt->script_spawn_company(*tm, p) : objects::ObjectHandle{};
    };
    out.spawn_effect = [bt, mode_for](const objects::SpawnParams& p) {
        TacticalMode* tm = mode_for();
        return tm ? bt->script_spawn_effect(*tm, p) : objects::ObjectHandle{};
    };
    // A combat unit through combat; any other live object of the mode (a prop with health) by the battle's store damage.
    out.take_damage = [bt, mode_for](objects::ObjectHandle u, float amount, bool cannot_die) {
        TacticalMode* tm = mode_for();
        if (!tm || !tm->primary().alive(u)) return false;
        return bt->script_damage(*tm, tm->primary().id(u), amount, cannot_die) || bt->script_object_damage(*tm, u, amount, cannot_die);
    };
    out.take_hardpoint_damage = [bt, mode_for](objects::ObjectHandle u, float amount, bool cannot_die, std::string_view hp) {  // r1b t01
        TacticalMode* tm = mode_for();
        return tm && tm->primary().alive(u) && bt->script_hardpoint_damage(*tm, tm->primary().id(u), amount, cannot_die, hp);
    };
    // OI host (L1/S4 wave 1 integrator ask 2): store + TreeSet::set_owner + the battle systems, for every tactical VM.
    out.change_owner = [bt, mode_for](objects::ObjectHandle o, int32_t owner) {
        TacticalMode* tm = mode_for();
        return tm && bt->script_change_owner(*tm, o, owner);
    };
    out.destroy_object = [bt, mode_for](objects::ObjectHandle o) {
        TacticalMode* tm = mode_for();
        return tm && bt->script_despawn(*tm, o);
    };
}
#else
void make_object_bridge(TacticalBattle&, std::function<TacticalMode*()>, lua::ObjectBridge&, std::function<bool()>) {}
#endif

}  // namespace alamo::sim
