// alamo_game host, ref6 t05: the App's AppProbe answers (app_probe.hpp) for the smoke script.
#include "game_app.hpp"

namespace alamo_game_app {

bool App::in_battle() const { return battle() != nullptr; }

bool App::unit_moves(objects::ObjectId id) const {
    const sim::TacticalBattle* b = battle();
    return b && b->movement().has_unit(id);
}

bool App::unit_fights(objects::ObjectId id) const {
    const sim::TacticalBattle* b = battle();
    return b && b->combat().has_unit(id);
}

bool App::hostile(int32_t a, int32_t b) const {
    const sim::TacticalBattle* tb = battle();
    return tb && tb->combat().is_enemy(a, b);
}

std::vector<ProbeUnit> App::battle_units() const {
    std::vector<ProbeUnit> out;
    const sim::TacticalBattle* b = battle();
    const sim::TacticalMode* tm = session_ ? session_->host().modes().top_tactical() : nullptr;
    if (!b || !tm) return out;
    const objects::ObjectStore& store = tm->primary();
    for (const objects::ObjectId id : b->movement().unit_ids()) {
        const objects::ObjectHandle h = store.find(id);
        if (!store.alive(h)) continue;
        out.push_back({id, store.owner(h), store.position(h), b->combat().has_unit(id)});
    }
    return out;
}

const sim::BattleStats* App::battle_stats() const {
    const sim::TacticalBattle* b = battle();
    return b ? &b->stats() : nullptr;
}

void App::menu_action(const ui::MenuAction& action) { handle_menu_actions({action}); }

}  // namespace alamo_game_app
