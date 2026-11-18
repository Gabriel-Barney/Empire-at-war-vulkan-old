// STY1 lane B t06: the tactical story rewards the battle host applies to the running battle (docs/re/STORY_REWARDS_GALACTIC.md
// §4): DISABLE_REINFORCEMENTS (per-player switch), CHANGE_OWNER, DESTROY_OBJECT, BOMBARD_DELAY; mcx6 (§1.11, §1.12, §1.20):
// FORCE_RETREAT, SET_HEALTH, DISABLE_SPECIAL_STRUCTURE (SET_SPAWN is in tactical_battle_spawners.cpp).
#include <algorithm>

#include "alamo/objects/object_store.hpp"
#include "tactical_battle_internal.hpp"

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

void TacticalBattle::set_retreat_off(int32_t player, bool off) {
    if (player < 0) return;
    const size_t i = static_cast<size_t>(player);
    if (i >= retreat_off_.size()) {
        if (!off) return;
        retreat_off_.resize(i + 1, 0);
    }
    retreat_off_[i] = off ? 1 : 0;
}

bool TacticalBattle::retreat_off(int32_t player) const {
    return player >= 0 && static_cast<size_t>(player) < retreat_off_.size() && retreat_off_[static_cast<size_t>(player)] != 0;
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

// FORCE_RETREAT §1.11: no request while a retreat runs (coordinator +0x1c); the request 0x340920 with the local-request flag
// 0 refuses the local player (code 1) before the checks request_retreat makes (participant, countdown, prevention).
bool TacticalBattle::reward_force_retreat(int32_t player, int32_t local_player, bool* accepted) {
    if (accepted) *accepted = false;
    if (finished_ || (retreat_side_ >= 0 && !retreat_complete_)) return false;
    const bool ok = player >= 0 && player != local_player && request_retreat(player);
    if (accepted) *accepted = ok;
    return true;
}

// SET_HEALTH §1.12: 0x3a8ad0(obj, P2 / 100) per instance, list order (creation order in the port).
uint32_t TacticalBattle::reward_set_health(TacticalMode& mode, objects::TypeIndex type, int32_t percent, int32_t count) {
    if (finished_) return 0;
    objects::ObjectStore& st = mode.primary();
    std::vector<objects::ObjectHandle> found = st.find_all_of_type(type);  // newest first
    std::reverse(found.begin(), found.end());
    const float fraction = static_cast<float>(percent) / 100.0f;
    const objects::GameObjectTypes& types = catalog_->types();
    uint32_t n = 0;
    for (const objects::ObjectHandle h : found) {
        if (count > 0 && static_cast<int32_t>(n) >= count) break;
        if (!st.alive(h)) continue;
        const objects::ObjectId id = st.id(h);
        bool team = false;
        for (const Container& c : containers_) team = team || (c.id == id && c.team);
        if (team) {  // [M] the container's own health field; its members keep theirs (team health = their mean)
            ++n;
            continue;
        }
        if (combat_ && combat_->has_unit(id)) {
            const float mx = combat_->max_health(id);
            combat_->set_health(id, std::clamp(mx * fraction, 0.0f, mx));
        } else {
            const float mx = type < types.size() ? types.columns().tactical_health[type] : 0.0f;
            st.set_health(h, std::clamp(mx * fraction, 0.0f, mx));
        }
        ++n;
    }
    return n;
}

// DISABLE_SPECIAL_STRUCTURE §1.20: the first live instance; only a BASE_SHIELD object takes the bit.
int32_t TacticalBattle::reward_disable_special_structure(TacticalMode& mode, objects::TypeIndex type, bool off) {
    static const objects::BehaviorTypeId kBaseShield = objects::behavior_type_id("BASE_SHIELD");
    const objects::ObjectStore& st = mode.primary();
    std::vector<objects::ObjectHandle> found = st.find_all_of_type(type);
    std::reverse(found.begin(), found.end());
    for (const objects::ObjectHandle h : found) {
        if (!st.alive(h)) continue;
        if (kBaseShield == objects::kNoBehavior || !st.has_behavior(h, kBaseShield)) return 1;
        if (combat_ && combat_->has_unit(st.id(h))) combat_->set_flag(st.id(h), tc::kUnitShieldStoryOff, off);
        return 2;
    }
    return 0;
}

}  // namespace alamo::sim
