// alamo_tactical_movement_trn1_tests (no framework; non-zero exit on failure).
// trn1 t03 (docs/re/MOVEMENT.md §3.6, docs/re/TRN1_TERRAIN_GAMEPLAY.md §3): the land terrain conform 0x42bb40 - the water
// plane for Water-class units, the Overall_Length pitch and z lift, the Overall_Width roll and the passability cut of the
// sample points. Hand numbers follow from the rules in the comments.
#include <cmath>
#include <cstdio>

#include "alamo/core/dmath.hpp"
#include "alamo/core/sim_rng.hpp"
#include "alamo/tactical/movement/movement_system.hpp"
#include "alamo/test/support.hpp"

using namespace alamo;
using namespace alamo::tactical::movement;

using ::alamo::test::g_fail;

namespace {

constexpr float kRad = 57.295776f;

bool close_to(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

PassabilityGrid open_grid(uint32_t w, uint32_t h) {
    PassabilityGrid g;
    g.init(w, h);
    return g;
}

UnitDesc unit(ObjectId id, V2 pos, MovementClass cls, float yaw, float length, float width) {
    UnitDesc d;
    d.id = id;
    d.pos = pos;
    d.max_speed = 1.0f;
    d.turn_rate = 6.0f;
    d.alt_max_speed = 1.0f;
    d.alt_turn_rate = 6.0f;
    d.movement_class = static_cast<int32_t>(cls);
    d.collision_class = kUccVehicle;
    d.radius = 2.0f;
    d.type_name = "TRN1_UNIT";
    d.locomotor = LocomotorKind::Default;
    d.yaw_deg = yaw;
    d.overall_length = length;
    d.overall_width = width;
    return d;
}

struct Conformed {
    float z = 0.0f, pitch = 0.0f, roll = 0.0f;
};

// One unit alone on a 100 x 100 open grid (cells 20 wide), ground h(x, y), optional water plane, one tick.
template <class H>
Conformed run(const UnitDesc& d, H h, bool water = false, float water_z = 0.0f, V2 blocked = {-1.0f, -1.0f}) {
    MovementSystem sys{MovementConfig{}};
    PassabilityGrid g = open_grid(100, 100);
    if (blocked.x >= 0.0f) g.set_base(static_cast<int32_t>(blocked.x), static_cast<int32_t>(blocked.y), kPassImpassable);
    sys.set_grid(std::move(g));
    sys.set_height_fn(h);
    sys.set_water_plane(water, water_z);
    CHECK(sys.add_unit(d));
    SimRng rng(7);
    sys.tick(1, rng, serial_for);
    Conformed c;
    c.z = sys.view(d.id).z;
    CHECK(sys.attitude(d.id, c.roll, c.pitch));
    return c;
}

void test_length_pitch_and_lift() {
    ::alamo::test::section("Overall_Length: pitch and z lift");
    // Ground rises 0.1 per unit along +x. Facing +x (yaw 0), L 40: front = p + 20 x, back = p - 20 x, dh = 4 ->
    // pitch = -atan2(4, 40) degrees (nose up is negative), z = h(p) + 4 / 5. No width: no roll.
    const auto slope = [](float x, float) { return 0.1f * x; };
    const V2 p = cell_center(50, 50);
    const Conformed c = run(unit(1, p, MovementClass::Wheeled, 0.0f, 40.0f, 0.0f), slope);
    CHECK(close_to(c.pitch, -(dmath::atan2(4.0f, 40.0f) * kRad), 1e-4f));
    CHECK(close_to(c.z, 0.1f * p.x + 0.8f, 1e-3f));
    CHECK(c.roll == 0.0f);
    // Facing +y: the length samples see the same height -> pitch 0 and no lift; W 20 across (+y rotated +90 = -x):
    // plus = p - 10 x, minus = p + 10 x, dh(minus - plus) = 2 -> roll = -atan2(2, 20) degrees.
    const Conformed r = run(unit(1, p, MovementClass::Wheeled, 90.0f, 40.0f, 20.0f), slope);
    CHECK(close_to(r.pitch, 0.0f, 1e-4f));
    CHECK(close_to(r.z, 0.1f * p.x, 1e-3f));
    CHECK(close_to(r.roll, -(dmath::atan2(2.0f, 20.0f) * kRad), 1e-3f));
    // No Overall_Length / Width: level, z = the ground.
    const Conformed f = run(unit(1, p, MovementClass::Wheeled, 0.0f, 0.0f, 0.0f), slope);
    CHECK(f.pitch == 0.0f && f.roll == 0.0f && close_to(f.z, 0.1f * p.x, 1e-3f));
}

void test_passability_cut() {
    ::alamo::test::section("Overall_Length: the sample cut at the first impassable cell");
    // An impassable cell 1 ahead of the unit's cell: the front sample stops at that cell's entry (10 ahead of the unit's
    // centre), so dh = 0.1 x (10 + 20) = 3 -> pitch = -atan2(3, 30), z lift 3 / 5.
    const auto slope = [](float x, float) { return 0.1f * x; };
    const V2 p = cell_center(50, 50);
    const Conformed c = run(unit(1, p, MovementClass::Wheeled, 0.0f, 40.0f, 0.0f), slope, false, 0.0f, V2{51.0f, 50.0f});
    CHECK(close_to(c.pitch, -(dmath::atan2(3.0f, 30.0f) * kRad), 1e-3f));
    CHECK(close_to(c.z, 0.1f * p.x + 0.6f, 1e-3f));
}

void test_water_plane() {
    ::alamo::test::section("Water class rides the water plane");
    // Flat ground at 0, water plane at 5: a Hover class (Water bit) stands at 5, a Wheeled one at 0; no water plane: 0.
    const auto flat = [](float, float) { return 0.0f; };
    const V2 p = cell_center(50, 50);
    CHECK(close_to(run(unit(1, p, MovementClass::Hover, 0.0f, 0.0f, 0.0f), flat, true, 5.0f).z, 5.0f, 1e-6f));
    CHECK(close_to(run(unit(1, p, MovementClass::Wheeled, 0.0f, 0.0f, 0.0f), flat, true, 5.0f).z, 0.0f, 1e-6f));
    CHECK(close_to(run(unit(1, p, MovementClass::Hover, 0.0f, 0.0f, 0.0f), flat, false, 5.0f).z, 0.0f, 1e-6f));
    // Ground above the plane: the ground.
    const auto high = [](float, float) { return 9.0f; };
    CHECK(close_to(run(unit(1, p, MovementClass::Hover, 0.0f, 0.0f, 0.0f), high, true, 5.0f).z, 9.0f, 1e-6f));
    // A hover unit tilting over a shore: the water-raised samples (front over water at 5, back on ground 8) -> dh -3.
    const auto shore = [p](float x, float) { return x > p.x ? 0.0f : 8.0f; };
    const Conformed s = run(unit(1, p, MovementClass::Hover, 0.0f, 40.0f, 0.0f), shore, true, 5.0f);
    CHECK(close_to(s.pitch, -(dmath::atan2(-3.0f, 40.0f) * kRad), 1e-3f));
    CHECK(close_to(s.z, 8.0f + 0.6f, 1e-3f));  // G(p) = 8 (p is on the ground side) + |dh| / 5
}

}  // namespace

int main() {
    test_length_pitch_and_lift();
    test_passability_cut();
    test_water_plane();
    if (g_fail) {
        std::printf("%d check(s) FAILED\n", g_fail);
        return 1;
    }
    std::printf("all trn1 movement tests passed\n");
    return 0;
}
