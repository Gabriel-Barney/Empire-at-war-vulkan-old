// CMB3 tests (combat rule leftovers): ability 47 through Set_Unit_Ability_Active, the gravity field and carried units,
// Redirect_Blaster's Move-clip hand-on to the next Take_Damage listener.
#include "test_abilities_common.hpp"

namespace bo3split {

void test_cmb3_lobbing_mode_is_inert() {
    // cmb3 t05 (b0451, ABILITIES_HARDPOINTS §2.5 [H]): 0x38a350 has no case for 47, so a command 43 (mode on or off) that
    // passes Can_Activate does nothing: no active byte, no recharge, no Activated / Unhandled event. (Lua Activate_Ability
    // refuses 47 as an unsupported unit ability before any command.)
    OiWorld w;
    AbilityObjectDesc la;
    la.unit_abilities = {rec(kFireLobbingSuperweapon, 0.0f, 30.0f)};
    const uint32_t lob = push(w.cd, w.ad, oi_unit("Lobber_Free", 1000.0f, 20.0f), la);
    w.make(GameMode::Land);
    w.add(1, lob, 0, {0, 0, 0});
    AbilitySystem& a = *w.ab;
    CHECK(a.can_activate(1, kFireLobbingSuperweapon, true, true, w.frame));
    const ObjectId ids[] = {1};
    a.set_unit_ability_mode(ids, kFireLobbingSuperweapon, true, w.frame);
    w.tick();
    CHECK(!a.is_active(1, kFireLobbingSuperweapon));
    CHECK(a.countdown_counter(1, kFireLobbingSuperweapon) == 0);
    CHECK(a.can_activate(1, kFireLobbingSuperweapon, true, true, w.frame));  // no recharge started
    CHECK(w.count(AbilityEventKind::Activated) == 0 && w.count(AbilityEventKind::Unhandled) == 0);
    a.set_unit_ability_mode(ids, kFireLobbingSuperweapon, false, w.frame);
    w.tick();
    CHECK(w.count(AbilityEventKind::Deactivated) == 0 && w.count(AbilityEventKind::Unhandled) == 0);
}

void test_cmb3_gravity_field_carried_units() {
    // cmb3 t06 (b0452, TRU1_GRAVITY_FIELD §1.5 [H]): the field query skips an object inside a container (garrisoned or
    // transported, obj +0x3a7) in both branches. A carried enemy in range is not slowed; once out it is. A slowed unit
    // that is carried keeps its slow while carried, even when its owner turns friendly (the release branch skips it too).
    OiWorld w;
    AbilityObjectDesc ga;
    ga.name = "Grav_Gen";
    ga.name_crc = 0x1234u;
    ga.gravity_field = true;
    ga.gravity_field_range = 300.0f;
    ga.gravity_field_slow_fraction = 0.9f;
    const uint32_t gen = push(w.cd, w.ad, oi_unit("Grav_Gen", 1000.0f, 20.0f), ga);
    AbilityObjectDesc ta;
    ta.affected_by_gravity_field = true;
    const uint32_t tank = push(w.cd, w.ad, oi_unit("Tank", 100.0f), ta);
    w.make(GameMode::Land);
    w.add(1, gen, 0, {0, 0, 0});
    w.add(2, tank, 1, {100, 0, 0});  // carried from the start
    w.add(3, tank, 1, {200, 0, 0});  // slowed, then carried
    w.sys->set_flag(2, combat::kUnitGarrisoned, true);
    w.sys->add_base_power(0, 900);
    AbilitySystem& a = *w.ab;
    for (int i = 0; i < 61; ++i) w.tick();
    CHECK(a.gravity_field_on(1));
    CHECK(!a.in_gravity_field(2) && w.sys->modifier(2, ModList::MaxSpeed) == 0.0f);
    CHECK(a.in_gravity_field(3));
    w.sys->set_flag(2, combat::kUnitGarrisoned, false);  // out of its container: picked up at the next service
    w.sys->set_flag(3, combat::kUnitGarrisoned, true);
    CHECK(a.set_owner(3, 0) && w.sys->set_owner(3, 0));  // now friendly, but carried: not released
    for (int i = 0; i < 60; ++i) w.tick();
    CHECK(a.in_gravity_field(2));
    CHECK(a.in_gravity_field(3) && w.sys->modifier(3, ModList::MaxSpeed) < 0.0f);
    w.sys->set_flag(3, combat::kUnitGarrisoned, false);  // out again, friendly and in range: released
    for (int i = 0; i < 60; ++i) w.tick();
    CHECK(!a.in_gravity_field(3) && w.sys->modifier(3, ModList::MaxSpeed) == 0.0f);
}

void test_cmb3_redirect_moving_next_listener() {
    // cmb3 t08 (SAP_On_Take_Damage 0x37d160 -> 0x37b580 [H]): Redirect_Blaster on a victim in the Move clip redirects the
    // shot but reports "not handled"; the dispatcher goes on to the victim's next Take_Damage ability, which may handle
    // the hit (here an Absorb_Blaster that always absorbs). Without one, the damage lands.
    for (const bool second : {true, false}) {
        World w;
        ProjectileDesc laser;
        laser.name = "Laser";
        laser.damage = 20.0f;
        laser.max_speed = 10.0f;
        laser.max_flight_distance = 1000.0f;
        laser.category = ProjectileCategory::Laser;
        w.cd.projectiles = {laser};
        SpecialAbilityDesc r;
        r.style = ActivationStyle::TakeDamage;
        r.applicable_categories = ~uint64_t{0};
        r.name = "Deflect";
        r.cls = SpecialClass::RedirectBlaster;
        r.redirect_chance = 1.0f;
        r.reaction_arc_deg = 180.0f;
        const uint32_t rd = add_special(w.ad, r);
        SpecialAbilityDesc ab;
        ab.style = ActivationStyle::TakeDamage;
        ab.applicable_categories = ~uint64_t{0};
        ab.name = "Absorb";
        ab.cls = SpecialClass::AbsorbBlaster;
        ab.absorb_chance = 1.0f;
        const uint32_t sd = add_special(w.ad, ab);
        UnitDesc jedi;
        jedi.name = "Jedi";
        jedi.tactical_health = 100.0f;
        AbilityObjectDesc ja;
        ja.specials = second ? std::vector<uint32_t>{rd, sd} : std::vector<uint32_t>{rd};
        const uint32_t jd = push(w.cd, w.ad, jedi, ja);
        UnitDesc trooper;
        trooper.name = "Trooper";
        trooper.tactical_health = 100.0f;
        trooper.category_mask = 0x1;
        const uint32_t td = push(w.cd, w.ad, trooper, AbilityObjectDesc{});
        w.make();
        AbilityHost h;
        h.primary_clip = [](ObjectId) { return 2; };  // Move
        w.ab->set_host(std::move(h));
        w.add(1, jd, 0, Vec3{0, 0, 0}, Vec3{1, 0, 0});
        w.add(2, td, 1, Vec3{100, 0, 0});
        ListenerShot s;
        s.projectile_desc = 0;
        s.flight = true;
        s.pos = Vec3{5, 0, 0};
        s.dir = Vec3{-1, 0, 0};  // frontal
        s.damage = 20.0f;
        const bool handled = w.ab->damage_listener(1, 2, s, w.frame, &w.rng);
        CHECK(handled == second);
        CHECK(w.sys->projectile_count() == 1u);  // the redirect's new shot either way
        CHECK(w.has_event(AbilityEventKind::Redirected, 1));
        CHECK(w.has_event(AbilityEventKind::Blocked, 1) == second);  // the absorb reports Blocked
    }
}

}  // namespace bo3split
