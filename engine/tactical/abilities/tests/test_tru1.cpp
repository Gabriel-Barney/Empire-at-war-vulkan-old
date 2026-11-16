// TRU1 t01 / t03: the FIRE_LOBBING_SUPERWEAPON (47) accept gate (0x39b480 case 0x2f) and the GRAVITY_CONTROL_FIELD
// service (docs/re/TRU1_GRAVITY_FIELD.md). Hand-computed frames on the OI sandbox.
#include "test_abilities_common.hpp"

namespace bo3split {

void test_tru1_lobbing_gate() {
    // 47 needs the owner's player; with Requires_Base_Power also that player's base power. No readiness gate here.
    OiWorld w;
    UnitDesc lob = oi_unit("Lobber", 1000.0f, 20.0f);
    lob.requires_base_power = true;
    AbilityObjectDesc la;
    la.unit_abilities = {rec(kFireLobbingSuperweapon, 0.0f, 30.0f)};
    const uint32_t powered = push(w.cd, w.ad, lob, la);
    const uint32_t unpowered = push(w.cd, w.ad, oi_unit("Lobber_Free", 1000.0f, 20.0f), la);
    w.make(GameMode::Land);
    w.add(1, powered, 0, {0, 0, 0});
    w.add(2, unpowered, 0, {100, 0, 0});
    w.add(3, unpowered, -1, {200, 0, 0});  // no owning player: refused
    AbilitySystem& a = *w.ab;
    CHECK(!a.can_activate(1, kFireLobbingSuperweapon, false, true, w.frame));  // Requires_Base_Power, no power
    CHECK(a.can_activate(2, kFireLobbingSuperweapon, false, true, w.frame));   // no power needed
    CHECK(!a.can_activate(3, kFireLobbingSuperweapon, false, true, w.frame));
    w.sys->add_base_power(0, 99);
    CHECK(a.can_activate(1, kFireLobbingSuperweapon, false, true, w.frame));
    w.sys->add_base_power(1, 98);  // another player's power does not count
    w.sys->remove_base_power(0, 99);
    CHECK(!a.can_activate(1, kFireLobbingSuperweapon, false, true, w.frame));
    // the accepted order's recharge (0x42fb70, Recharge_Seconds 30 = 900 frames): a running recharge blocks the
    // recharge-checking accept only
    w.sys->add_base_power(0, 99);
    CHECK(a.force_recharge(1, kFireLobbingSuperweapon));
    CHECK(a.countdown_counter(1, kFireLobbingSuperweapon) == 900);
    CHECK(!a.can_activate(1, kFireLobbingSuperweapon, true, true, w.frame));
    CHECK(a.can_activate(1, kFireLobbingSuperweapon, false, true, w.frame));
}

namespace {
float max_speed_mod(OiWorld& w, ObjectId id) { return w.sys->modifier(id, ModList::MaxSpeed); }
size_t slow_entries(OiWorld& w, ObjectId id) {
    size_t n = 0;
    if (const combat::ModifierSet* m = w.sys->unit_modifiers(id))
        for (const combat::ModifierEntry& e : m->entries()) n += e.list == static_cast<uint8_t>(ModList::MaxSpeed) ? 1u : 0u;
    return n;
}
}  // namespace

void test_tru1_gravity_field() {
    OiWorld w;
    AbilityObjectDesc ga;
    ga.name = "Grav_Gen";
    ga.name_crc = 0x1234u;
    ga.gravity_field = true;
    ga.gravity_field_range = 300.0f;
    ga.gravity_field_slow_fraction = 0.9f;
    const uint32_t gen = push(w.cd, w.ad, oi_unit("Grav_Gen", 1000.0f, 20.0f), ga);
    AbilityObjectDesc dead_ga = ga;  // Gravity_Control_Field_Slow_Fraction missing (0): the field does nothing
    dead_ga.name = "Grav_Gen_Zero";
    dead_ga.gravity_field_slow_fraction = 0.0f;
    const uint32_t gen0 = push(w.cd, w.ad, oi_unit("Grav_Gen_Zero", 1000.0f, 20.0f), dead_ga);
    AbilityObjectDesc ta;
    ta.affected_by_gravity_field = true;
    ta.unit_abilities = {rec(kDeploy, 0.0f, 0.0f)};
    const uint32_t tank = push(w.cd, w.ad, oi_unit("Tank", 100.0f), ta);
    const uint32_t rock = push(w.cd, w.ad, oi_unit("Rock", 100.0f), AbilityObjectDesc{});
    w.make(GameMode::Land);
    w.add(1, gen, 0, {0, 0, 0});
    w.add(2, tank, 1, {200, 0, 50});    // enemy, affected, in range (XY 200; z ignored)
    w.add(3, tank, 1, {400, 0, 0});     // enemy, out of range
    w.add(4, rock, 1, {100, 0, 0});     // enemy, not affected
    w.add(5, tank, 0, {100, 0, 0});     // own unit
    w.add(6, tank, 2, {0, 290, 0});     // second enemy player, in range
    w.add(7, gen0, 2, {0, 2000, 0});    // player 2's zero-fraction generator
    w.add(8, tank, 0, {0, 2100, 0});    // in its range
    AbilitySystem& a = *w.ab;
    // no base power: services pass, nobody is slowed, no power event
    for (int i = 0; i < 70; ++i) w.tick();
    CHECK(!a.gravity_field_on(1) && a.gravity_field_units(1).empty() && !a.in_gravity_field(2));
    CHECK(w.count(AbilityEventKind::GravityFieldPower) == 0);
    // base power: within one interval (60 frames) the in-range affected enemies get list-5 -0.9 (speed x 0.1)
    w.sys->add_base_power(0, 900);
    w.sys->add_base_power(2, 7);
    uint32_t on_frame = 0;
    for (int i = 0; i < 61 && on_frame == 0; ++i) {
        w.tick();
        if (a.gravity_field_on(1)) on_frame = w.frame - 1;
    }
    CHECK(on_frame != 0);
    CHECK(a.in_gravity_field(2) && a.in_gravity_field(6));
    CHECK(!a.in_gravity_field(3) && !a.in_gravity_field(4) && !a.in_gravity_field(5) && !a.in_gravity_field(8));
    CHECK_EQ(max_speed_mod(w, 2), -0.9f);
    CHECK_EQ(max_speed_mod(w, 3), 0.0f);
    CHECK_EQ(max_speed_mod(w, 5), 0.0f);
    CHECK(a.gravity_field_units(1).size() == 2 && a.gravity_field_units(1)[0] == 2 && a.gravity_field_units(1)[1] == 6);
    CHECK(a.gravity_field_on(7) && a.gravity_field_units(7).empty() && !a.in_gravity_field(8));  // fraction 0: on, nobody slowed
    if (const AbilityEvent* e = w.last(AbilityEventKind::GravityFieldPower, 1)) CHECK(e->value == 1);
    else CHECK(false);
    CHECK(w.count(AbilityEventKind::GravitySlow, 1) == 2);
    // a slowed unit cannot start a deploy (the stop still runs); an unslowed one can
    const ObjectId t2[] = {2}, t3[] = {3};
    a.set_unit_ability_mode(t2, kDeploy, true, w.frame);
    a.set_unit_ability_mode(t3, kDeploy, true, w.frame);
    CHECK(!a.is_active(2, kDeploy) && a.is_active(3, kDeploy));
    // leaving the range keeps the slow; a second service does not stack it; a unit that walks in is slowed
    w.move(2, {1000, 0, 0});
    w.move(3, {250, 0, 0});
    for (int i = 0; i < 60; ++i) w.tick();
    CHECK(a.in_gravity_field(2) && slow_entries(w, 2) == 1 && a.in_gravity_field(3));
    // a second generator never slows a unit already in a field
    w.add(9, gen, 0, {250, 0, 0});
    for (int i = 0; i < 120; ++i) w.tick();
    CHECK(a.gravity_field_units(9).empty() && slow_entries(w, 3) == 1);
    // owner change to the generator's side: released on the next service while in range
    CHECK(a.set_owner(6, 0) && w.sys->set_owner(6, 0));
    for (int i = 0; i < 60; ++i) w.tick();
    CHECK(!a.in_gravity_field(6) && slow_entries(w, 6) == 0);
    // power lost: every listed unit is released at the next service (out of range ones too)
    w.sys->remove_base_power(0, 900);
    for (int i = 0; i < 60; ++i) w.tick();
    CHECK(!a.gravity_field_on(1) && a.gravity_field_units(1).empty());
    CHECK(!a.in_gravity_field(2) && !a.in_gravity_field(3) && slow_entries(w, 2) == 0 && slow_entries(w, 3) == 0);
    if (const AbilityEvent* e = w.last(AbilityEventKind::GravityFieldPower, 1)) CHECK(e->value == 0);
    else CHECK(false);
    // power back, then the generator dies: its units are released at once
    w.sys->add_base_power(0, 900);
    for (int i = 0; i < 60; ++i) w.tick();
    CHECK(a.in_gravity_field(3));  // re-slowed by generator 1 or 9, whichever served first
    const ObjectId holder = a.gravity_field_units(1).empty() ? 9 : 1;
    CHECK(a.gravity_field_units(holder).size() == 1 && a.gravity_field_units(holder)[0] == 3);
    w.sys->kill_unit(holder, kNoObject, combat::kKindNormal, w.frame);
    w.tick();
    CHECK(!a.in_gravity_field(3) && slow_entries(w, 3) == 0);
    // save / load keeps the state (hash identical), with a non-empty list (the other generator picks unit 3 up)
    for (int i = 0; i < 60; ++i) w.tick();
    const ObjectId other = holder == 1 ? 9 : 1;
    CHECK(a.in_gravity_field(3) && a.gravity_field_units(other).size() == 1);
    StateHasher h1;
    a.hash(h1);
    ByteWriter wa;
    a.write(wa);
    ByteReader ra(wa.bytes());
    CHECK(a.read(ra));
    StateHasher h2;
    a.hash(h2);
    CHECK(h1.digest() == h2.digest() && a.in_gravity_field(3) && a.gravity_field_units(other).size() == 1);
    // host removal of the generator releases its unit at once (modifier entry and flag)
    a.remove(other);
    CHECK(!a.in_gravity_field(3) && slow_entries(w, 3) == 0);
}

}  // namespace bo3split
