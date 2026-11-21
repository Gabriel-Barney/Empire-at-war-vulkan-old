// CMB3 tests (combat rule leftovers; see test_combat_common.hpp).
#include "test_combat_common.hpp"

namespace bo3split {

void test_shield_regen_gate() {
    // cmb3 t07 (b0454, TACTICAL_COMBAT §3.7, 0x549250 [H]): SHIELDED regeneration reads no base power (its player has none
    // here) and is skipped while the unit's ION_STUN_EFFECT runs. Regeneration is due every 90 frames (frames 90, 180, ...).
    World w;
    ProjectileDesc ion;
    ion.ion_stun = true;
    ion.ion_stun_duration = 4.0f;  // 120 frames from frame 0: the frame-90 regeneration is skipped, the frame-180 one runs
    w.data.projectiles.push_back(ion);
    UnitDesc s;
    s.tactical_health = 100.0f;
    s.shield_points = 100.0f;
    s.shield_refresh_rate = 10.0f;
    const uint32_t ds = push_unit(w.data, s);
    w.make();
    w.add(1, ds, 0, Vec3{});
    w.add(2, ds, 0, Vec3{100.0f, 0.0f, 0.0f});
    CHECK(!w.sys->has_base_power(0));
    w.sys->set_shield(1, 50.0f);
    w.sys->set_shield(2, 50.0f);
    CHECK(w.sys->apply_ion_stun(1, 0, w.frame));
    CHECK(w.sys->ion_stunned(1, w.frame));
    while (w.frame <= 150) w.tick();
    CHECK_F(w.sys->shield(1), 50.0f);  // stunned at the frame-90 regeneration
    CHECK_F(w.sys->shield(2), 60.0f);  // no base power, still regenerates
    CHECK(!w.sys->ion_stunned(1, w.frame));
    while (w.frame <= 200) w.tick();
    CHECK_F(w.sys->shield(1), 60.0f);
    CHECK_F(w.sys->shield(2), 70.0f);
}

}  // namespace bo3split
