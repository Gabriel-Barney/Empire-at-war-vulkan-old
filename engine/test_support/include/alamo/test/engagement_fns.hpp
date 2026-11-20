#pragma once
// ref6 t02: an EngagementQueries for the movement / combat unit tests, answering each query from a lambda the test sets
// (an unset one keeps the interface default; has_* is true once its lambda is set). Engine code implements the interface
// itself (TacticalBattle); this adapter is for tests only. The object must outlive the system it is handed to.
#include <cstdint>
#include <deque>
#include <functional>

#include "alamo/tactical/engagement.hpp"

namespace alamo::test {

struct FnEngagement final : tactical::EngagementQueries {
    std::function<bool(int32_t, int32_t)> hostile_fn;
    std::function<float(int32_t)> attack_range_fn, min_attack_distance_fn;
    std::function<bool(int32_t mover, float px, float py, int32_t target, bool los_only, float gx, float gy, float gz)> fire_fn;
    std::function<bool(int32_t id, tactical::Vec3& at)> turret_aim_fn;
    std::function<bool(int32_t key)> locked_fn;
    std::function<uint8_t(int32_t unit, int32_t candidate, float range, float& rr)> scan_range_fn;
    std::function<bool(tactical::Vec3 from, tactical::Vec3 to, bool shield_damage)> shield_sides_fn;

    bool hostile(int32_t a, int32_t b) const override { return hostile_fn ? hostile_fn(a, b) : EngagementQueries::hostile(a, b); }
    float attack_range(int32_t id) const override { return attack_range_fn ? attack_range_fn(id) : 0.0f; }
    float min_attack_distance(int32_t id) const override { return min_attack_distance_fn ? min_attack_distance_fn(id) : 0.0f; }
    bool has_fire_test() const override { return static_cast<bool>(fire_fn); }
    bool can_fire_from(int32_t mover, float px, float py, int32_t target, bool los_only, float gx, float gy, float gz) const override {
        return fire_fn ? fire_fn(mover, px, py, target, los_only, gx, gy, gz) : true;
    }
    bool turret_aim(int32_t id, tactical::Vec3& at) const override { return turret_aim_fn && turret_aim_fn(id, at); }
    bool movement_locked(int32_t key) const override { return locked_fn && locked_fn(key); }
    bool has_scan_range() const override { return static_cast<bool>(scan_range_fn); }
    uint8_t scan_range(int32_t unit, int32_t candidate, float range, float& rr) const override {
        if (!scan_range_fn) return EngagementQueries::scan_range(unit, candidate, range, rr);
        return scan_range_fn(unit, candidate, range, rr);
    }
    bool has_shield_sides() const override { return static_cast<bool>(shield_sides_fn); }
    bool opposite_shield_sides(tactical::Vec3 from, tactical::Vec3 to, bool shield_damage) const override {
        return shield_sides_fn && shield_sides_fn(from, to, shield_damage);
    }
};

// The FnEngagement of `sys` (a MovementSystem or CombatSystem): the one already handed to it, else a new one (kept for the
// whole test run, so a system never points at a destroyed object) installed with set_engagement. A test sets the lambdas
// it needs on it; the system's other queries keep their defaults.
template <class System>
FnEngagement& engagement_of(System& sys) {
    if (const auto* e = dynamic_cast<const FnEngagement*>(&sys.engagement())) return const_cast<FnEngagement&>(*e);
    static std::deque<FnEngagement> pool;
    FnEngagement& e = pool.emplace_back();
    sys.set_engagement(&e);
    return e;
}

// The movement tests' fire test in movement's own types (V2 = tactical::movement::V2).
template <class V2, class F>
void set_fire_test(FnEngagement& e, F f) {
    e.fire_fn = [f](int32_t mover, float px, float py, int32_t target, bool los_only, float gx, float gy, float gz) {
        return f(mover, V2{px, py}, target, los_only, V2{gx, gy}, gz);
    };
}

}  // namespace alamo::test
