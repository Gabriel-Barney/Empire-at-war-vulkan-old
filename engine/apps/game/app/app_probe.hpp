// alamo_game host, ref6 t05: what the smoke script (app_script.cpp) asks of the running game and does to it through one
// narrow interface instead of reaching into the session's battle systems or the App's menu handling. The App implements
// it (app_probe.cpp); answers are sim reads only, so a scripted run's choices never depend on render interpolation.
#pragma once
#include <cstdint>
#include <vector>

#include "alamo/core/galaxy.hpp"
#include "alamo/objects/object_store.hpp"
#include "alamo/ui/gui_menus.hpp"

namespace alamo::sim {
struct BattleStats;
}

namespace alamo_game_app {

// One unit of the running battle: the movement system's units, live in the tactical store.
struct ProbeUnit {
    alamo::objects::ObjectId id = alamo::objects::kNoObjectId;
    int32_t owner = -1;
    alamo::Vec3 position{};
    bool fights = false;  // the combat system has it
};

class AppProbe {
public:
    virtual ~AppProbe() = default;
    // A battle is running (the session has a tactical battle).
    virtual bool in_battle() const = 0;
    // The battle's movement system has the unit (false without a battle).
    virtual bool unit_moves(alamo::objects::ObjectId id) const = 0;
    // The battle's combat system has the unit (false without a battle).
    virtual bool unit_fights(alamo::objects::ObjectId id) const = 0;
    // Players a and b are enemies in the battle's combat rules (false without a battle).
    virtual bool hostile(int32_t a, int32_t b) const = 0;
    // The battle's movable units, ascending id, live only; empty without a battle.
    virtual std::vector<ProbeUnit> battle_units() const = 0;
    // The battle's counters (shots, hits, deaths, ...); null without a battle.
    virtual const alamo::sim::BattleStats* battle_stats() const = 0;
    // Performs a front-end menu action as a dialog button would (StartCampaign, ...).
    virtual void menu_action(const alamo::ui::MenuAction& action) = 0;
};

}  // namespace alamo_game_app
