// ref6 t02: the engagement questions movement and combat ask about each other - hostility, attack ranges, the movement's
// fire test, the turret aim point, the movement lock, combat's scan-range query and the land shield-sides test - as one
// interface the battle host (TacticalBattle) implements once and hands to both systems (MovementSystem::set_engagement,
// CombatSystem::set_engagement). It replaces the per-rule std::function hooks and their translating lambdas.
//
// The defaults are the answers of a system that has no host (unit tests, synthetic worlds): the old "hook unset" behaviour.
// The has_* queries say whether a host answers the test at all: the systems skip the whole step when it does not, exactly
// as they skipped it while the hook was unset. Every query is const and read-only: combat calls scan_range from its parallel
// gather phase, so an implementation must not write.
#pragma once
#include <cstdint>

#include "alamo/tactical/geometry.hpp"

namespace alamo::tactical {

class EngagementQueries {
public:
    virtual ~EngagementQueries() = default;

    // Combat relation of two owners (the player relation table); default: two different owners, both >= 0.
    virtual bool hostile(int32_t a, int32_t b) const { return a >= 0 && b >= 0 && a != b; }

    // ---- movement asks ----
    // Get attack range (0x3749d0) / Targeting_Min_Attack_Distance of mover `id` (a squad container through its lead); 0 none.
    virtual float attack_range(int32_t /*id*/) const { return 0.0f; }
    virtual float min_attack_distance(int32_t /*id*/) const { return 0.0f; }
    // The movement's fire test (MOVEMENT §4.3.2 items 6 / 7): can `mover` hit `target` from (px, py), to the goal point
    // (gx, gy, gz); los_only: the line of fire alone. Only called when has_fire_test().
    virtual bool has_fire_test() const { return false; }
    virtual bool can_fire_from(int32_t /*mover*/, float /*px*/, float /*py*/, int32_t /*target*/, bool /*los_only*/,
                               float /*gx*/, float /*gy*/, float /*gz*/) const {
        return true;
    }
    // A Deploys unit's turret aim point (its live combat target's position); false: none.
    virtual bool turret_aim(int32_t /*id*/, Vec3& /*at*/) const { return false; }
    // The movement lock of formation key `key` (a hyperspace arrival in progress).
    virtual bool movement_locked(int32_t /*key*/) const { return false; }

    // ---- combat asks ----
    // Get_Scan_Range 0x566f50 / 0x4160a0(F, u, c, &Rr) for a kUnitScanQuery unit (FIGHTER_FLYING §4.10 A, BKR1 Q8); c = -1:
    // the scan cube test. Only called when has_scan_range(); read-only (combat's parallel gather).
    virtual bool has_scan_range() const { return false; }
    virtual uint8_t scan_range(int32_t /*unit*/, int32_t /*candidate*/, float /*range*/, float& rr) const {
        rr = 0.0f;
        return 0;
    }
    // Land line of fire 0x3b5ad0: are `from` and `to` on opposite sides of an active base shield. Only called when
    // has_shield_sides().
    virtual bool has_shield_sides() const { return false; }
    virtual bool opposite_shield_sides(Vec3 /*from*/, Vec3 /*to*/, bool /*shield_damage*/) const { return false; }
};

// The answers of a system without a host.
inline const EngagementQueries& no_engagement() {
    static const EngagementQueries q;
    return q;
}

}  // namespace alamo::tactical
