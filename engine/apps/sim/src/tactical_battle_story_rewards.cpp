// STY1 lane B t06: the tactical story rewards the battle host applies to the running battle (docs/re/STORY_REWARDS_GALACTIC.md
// §4): DISABLE_REINFORCEMENTS (per-player switch), CHANGE_OWNER, DESTROY_OBJECT, BOMBARD_DELAY.
#include <algorithm>

#include "alamo/objects/object_store.hpp"
#include "alamo/sim/tactical_battle.hpp"

namespace alamo::sim {

void TacticalBattle::set_reinforcements_off(int32_t player, bool off) {
    if (player < 0) return;
    const size_t i = static_cast<size_t>(player);
    if (i >= reinf_off_.size()) {
        if (!off) return;
        reinf_off_.resize(i + 1, 0);
    }
    reinf_off_[i] = off ? 1 : 0;
}

bool TacticalBattle::reinforcements_off(int32_t player) const {
    return player >= 0 && static_cast<size_t>(player) < reinf_off_.size() && reinf_off_[static_cast<size_t>(player)] != 0;
}

// CHANGE_OWNER §4.1: Reward_Param3 absent = the first instance, 0 = every instance, n = the first n (creation order).
uint32_t TacticalBattle::reward_change_owner(TacticalMode& mode, objects::TypeIndex type, int32_t owner, int32_t count) {
    if (finished_ || owner < 0) return 0;
    std::vector<objects::ObjectHandle> found = mode.primary().find_all_of_type(type);  // newest first
    std::reverse(found.begin(), found.end());
    uint32_t n = 0;
    for (const objects::ObjectHandle h : found) {
        if (count > 0 && static_cast<int32_t>(n) >= count) break;
        if (!mode.primary().alive(h)) continue;
        if (script_change_owner(mode, h, owner)) ++n;
    }
    return n;
}

// DESTROY_OBJECT §4.1: the first instance takes 1,000,000 damage, so the normal death path runs (a Debug-kind hit [port],
// as Lua Take_Damage: retail's kind 1 is not mapped).
bool TacticalBattle::reward_destroy_object(TacticalMode& mode, objects::TypeIndex type) {
    if (finished_) return false;
    std::vector<objects::ObjectHandle> found = mode.primary().find_all_of_type(type);
    std::reverse(found.begin(), found.end());
    for (const objects::ObjectHandle h : found)
        if (mode.primary().alive(h)) return script_damage(mode, mode.primary().id(h), 1000000.0f, false);
    return false;
}

}  // namespace alamo::sim
