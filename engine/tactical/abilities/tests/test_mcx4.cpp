// mcx4 (Stage 9, box MCX2): ability tags base FoC sets that the port stored without using. Expected values from
// docs/re/MCX4_RE_ANSWERS_TACTICAL.md.
#include "test_abilities_common.hpp"

namespace bo3split {

// ledger: verifies Absorb_Blaster_Ability.Damage_Absorb_Percentage
// ledger: verifies Absorb_Blaster_Ability.Damage_Absorb_Amount
void test_mcx4_absorb_blaster() {
    // MCX4_RE_ANSWERS_TACTICAL §7 (Absorb_Blaster 0x6ee840): a Take_Damage Absorb_Blaster_Ability absorbs a shot (not a
    // MISSILE or GRENADE) on a roll < Absorb_Chance and sets the victim's health to health + Projectile_Damage x
    // Damage_Absorb_Percentage + Damage_Absorb_Amount, clamped to the maximum.
    World w;
    ProjectileDesc laser;
    laser.name = "Laser";
    laser.damage = 20.0f;
    laser.category = ProjectileCategory::Laser;
    ProjectileDesc missile = laser;
    missile.name = "Missile";
    missile.category = ProjectileCategory::Missile;
    w.cd.projectiles = {laser, missile};
    SpecialAbilityDesc s;
    s.style = ActivationStyle::TakeDamage;
    s.applicable_categories = ~uint64_t{0};
    s.name = "Absorb";
    s.cls = SpecialClass::AbsorbBlaster;
    s.absorb_chance = 1.0f;
    s.damage_absorb_percentage = 0.2f;  // Damage_Absorb_Percentage (Yoda 0.2)
    s.damage_absorb_amount = 1.0f;      // Damage_Absorb_Amount (Yoda 1)
    const uint32_t absorb = add_special(w.ad, s);
    UnitDesc jedi;
    jedi.name = "Jedi";
    jedi.tactical_health = 100.0f;
    AbilityObjectDesc ja;
    ja.specials = {absorb};
    const uint32_t jd = push(w.cd, w.ad, jedi, ja);
    UnitDesc trooper;
    trooper.name = "Trooper";
    trooper.tactical_health = 100.0f;
    trooper.category_mask = 0x1;  // the target filter (0x48e290) needs a category in Applicable_Unit_Categories
    const uint32_t td = push(w.cd, w.ad, trooper, AbilityObjectDesc{});
    w.make();
    w.add(1, jd, 0, Vec3{0, 0, 0});
    w.add(2, td, 1, Vec3{100, 0, 0});
    const float mx = w.sys->max_health(1);
    w.sys->set_health(1, mx - 30.0f);
    CHECK(w.ab->damage_listener(1, 2, 0, w.frame, &w.rng));       // laser: absorbed
    CHECK_EQ(w.sys->health(1), mx - 30.0f + 20.0f * 0.2f + 1.0f);  // + 5
    CHECK(!w.ab->damage_listener(1, 2, 1, w.frame, &w.rng));      // a missile is never absorbed
    CHECK_EQ(w.sys->health(1), mx - 25.0f);
    w.sys->set_health(1, mx - 2.0f);
    CHECK(w.ab->damage_listener(1, 2, 0, w.frame, &w.rng));
    CHECK_EQ(w.sys->health(1), mx);  // clamped to the maximum
}

}  // namespace bo3split
