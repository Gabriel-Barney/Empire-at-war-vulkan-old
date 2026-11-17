// r1b t10 tests: hardpoint turret slew and the Fire_At barrel pre-check (TACTICAL_COMBAT §2.9 Q2 "Hardpoint turret slew").
#include <algorithm>
#include <cmath>

#include "test_combat_common.hpp"

namespace bo3split {

namespace {

// One turret hardpoint (Turret_Rotate_Speed 5, elevation 45) on unit 1 at the origin facing +x; targets are owner 1.
struct TurretWorld {
    World w;
    explicit TurretWorld(bool model, float extent = 120.0f, float cone = 25.0f, bool manual = false) {
        ProjectileDesc p;
        p.max_speed = 20.0f;
        w.data.projectiles.push_back(p);
        HardpointDesc gun;
        gun.kind = HardpointKind::WeaponLaser;
        gun.health = 100.0f;
        gun.destroyable = gun.targetable = true;
        gun.projectile = 0;
        gun.randomize_between_fire_bones = false;
        gun.min_recharge = gun.max_recharge = 0.2f;
        gun.fire_range = 2000.0f;
        gun.fire_cone_width = cone;
        gun.turret = true;
        gun.turret_model = model;
        gun.turret_rotate_speed = 5.0f;
        gun.turret_rotate_extent = extent;
        gun.turret_elevate_extent = 45.0f;
        gun.manual_target = manual;
        w.data.hardpoints.push_back(gun);
        UnitDesc sh;
        sh.tactical_health = 100.0f;
        sh.max_attack_distance = 2000.0f;
        sh.hardpoints = {0};
        push_unit(w.data, sh);  // 0
        UnitDesc t;
        t.tactical_health = 1.0e7f;
        t.radius = 5.0f;
        push_unit(w.data, t);  // 1
        CombatConfig cfg;
        cfg.mode = GameMode::Space;
        cfg.object_max_health_multiplier = 1.0f;
        cfg.hardpoint_health_multiplier = 1.0f;
        cfg.inaccuracy_enabled = false;
        w.make(cfg);
        w.add(1, 0, 0, Vec3{0.0f, 0.0f, 0.0f});
    }
    float yaw() const {
        float p = 0.0f, y = 0.0f;
        CHECK(w.sys->hardpoint_turret_pose(1, 0, p, y));
        return y;
    }
    float pitch() const {
        float p = 0.0f, y = 0.0f;
        CHECK(w.sys->hardpoint_turret_pose(1, 0, p, y));
        return p;
    }
};

Vec3 at_bearing(float deg, float r) {
    const double a = static_cast<double>(deg) * 3.14159265358979323846 / 180.0;
    return Vec3{static_cast<float>(r * std::cos(a)), static_cast<float>(r * std::sin(a)), 0.0f};
}

}  // namespace

void test_turret_slew() {
    // A target 90 degrees to the left: the turret turns 2 x Turret_Rotate_Speed per update (the plain step, then the
    // wrapped one) and fires only once its barrel is within Fire_Cone_Width (25) of the target, reading the previous
    // update's pose; without a turret model it neither turns nor waits (as before).
    uint32_t first_shot[2] = {0, 0};
    for (const bool model : {false, true}) {
        TurretWorld t(model);
        t.w.add(2, 1, 1, at_bearing(90.0f, 500.0f));
        t.w.sys->order_attack(1, 2);
        float prev = 0.0f;
        for (uint32_t k = 1; k <= 40; ++k) {
            const size_t before = t.w.sys->projectile_count();
            t.w.tick();
            if (t.w.sys->projectile_count() > before && first_shot[model ? 1 : 0] == 0) {
                first_shot[model ? 1 : 0] = k;
                if (model) CHECK(prev >= 65.0f);
            }
            const float y = t.yaw();
            if (model) CHECK_NEAR(y, std::min(90.0, 10.0 * static_cast<double>(k)), 1.0e-3);
            else CHECK_F(y, 0.0f);
            prev = y;
        }
    }
    std::printf("  r1b t10: first shot at update %u without a turret model, %u with one (pose 70 after update 7)\n",
                first_shot[0], first_shot[1]);
    CHECK(first_shot[0] > 0 && first_shot[1] >= 8 && first_shot[1] <= 20 && first_shot[0] <= first_shot[1]);
    {   // Outside Turret_Rotate_Extent_Degrees (120) the target fails the turret cone: back to the rest pose.
        TurretWorld t(true);
        t.w.add(2, 1, 1, at_bearing(90.0f, 500.0f));
        t.w.add(3, 1, 1, at_bearing(180.0f, 500.0f));
        t.w.sys->order_attack(1, 2);
        for (int k = 0; k < 12; ++k) t.w.tick();
        CHECK_NEAR(t.yaw(), 90.0, 1.0e-3);
        t.w.sys->order_attack(1, 3);
        for (int k = 0; k < 9; ++k) t.w.tick();
        CHECK_NEAR(t.yaw(), 0.0, 1.0e-3);
    }
    {   // Elevation: pitch moves Turret_Rotate_Speed (5) per update, clamped to Turret_Elevate_Extent_Degrees (45).
        TurretWorld t(true);
        t.w.add(2, 1, 1, Vec3{300.0f, 0.0f, 600.0f});  // 63.4 degrees up, dead ahead
        t.w.sys->order_attack(1, 2);
        for (int k = 1; k <= 12; ++k) {
            t.w.tick();
            CHECK_NEAR(t.pitch(), std::min(45.0, 5.0 * k), 1.0e-3);
        }
        CHECK_NEAR(t.yaw(), 0.0, 1.0e-3);
    }
    {   // [port] Requires_Manual_Target_Assignment: the weapon's target stands in for the manual target (no cone test).
        TurretWorld t(true, 60.0f, 25.0f, true);
        t.w.add(2, 1, 1, at_bearing(90.0f, 500.0f));  // outside the 60 extent: aim clamped to 60
        t.w.sys->order_attack(1, 2);
        for (int k = 0; k < 10; ++k) t.w.tick();
        CHECK_NEAR(t.yaw(), 60.0, 1.0e-3);
    }
    {   // Retail quirk (0x381ff0): pose 170 and aim -170 straddle +-180; the plain step (-5) and the wrapped one (+5)
        // cancel, so the turret stays put (extent 360: no clamp).
        TurretWorld t(true, 360.0f, 10.0f);
        t.w.add(2, 1, 1, at_bearing(170.0f, 500.0f));
        t.w.add(3, 1, 1, at_bearing(-170.0f, 500.0f));
        t.w.sys->order_attack(1, 2);
        for (int k = 0; k < 20; ++k) t.w.tick();
        CHECK_NEAR(t.yaw(), 170.0, 1.0e-3);
        t.w.sys->order_attack(1, 3);
        for (int k = 0; k < 5; ++k) {
            t.w.tick();
            CHECK_NEAR(t.yaw(), 170.0, 1.0e-3);
        }
    }
}

}  // namespace bo3split
