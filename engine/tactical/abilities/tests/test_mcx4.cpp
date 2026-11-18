// mcx4 (Stage 9, box MCX2): ability tags base FoC sets that the port stored without using. Expected values from
// docs/re/MCX4_RE_ANSWERS_TACTICAL.md.
#include "test_abilities_common.hpp"

#include <utility>

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

// ledger: verifies Spawn_Ability.Recharge_Time_In_Seconds
// ledger: verifies Spawn_Ability.Spawn_Bone_Name
void test_mcx4_spawn_ability() {
    // MCX4_RE_ANSWERS_TACTICAL §14 (Spawn_Ability, Ewok_Handler): the apply holds the user and starts a creation countdown
    // max(1, int(FPS x Creation_Delay_In_Seconds)) and the class's own cooldown int(FPS x Recharge_Time_In_Seconds) from
    // the activation; at 0 the bomber Object_Type is created at Spawn_Bone_Name, ordered to attack and move to the target,
    // and detonated (Explosion_Type at its aim point, then 9999 damage) once it stops. A bomber removed first explodes not.
    // FoC values: delay 2.5 s (75 frames), recharge 3 s (90 frames), bone MuzzleA_00, blast 300 in 25.
    OiWorld w;
    ProjectileDesc boom;
    boom.name = "Bomb_Of_Doom";
    boom.blast_area_damage = 300.0f;
    boom.blast_area_range = 25.0f;
    w.cd.projectiles = {boom};
    SpecialAbilityDesc s = oi_special("Handler_Spawn", SpecialClass::Spawn, ActivationStyle::UserInput);
    s.min_range = 75.0f;
    s.max_range = 270.0f;
    s.creation_delay = 2.5f;
    s.recharge_time_in_seconds = 3.0f;  // Recharge_Time_In_Seconds
    s.spawn_bone = "MuzzleA_00";        // Spawn_Bone_Name
    s.projectile = 0;                   // Explosion_Type
    s.spawn_type = 77;                  // Object_Type (a host type index)
    const uint32_t sd = add_special(w.ad, s);
    AbilityObjectDesc handler;
    handler.unit_abilities = {oi_gui(kTargetedHack, 0.0f, 0.0f, "Handler_Spawn")};
    handler.specials = {sd};
    const uint32_t hd = push(w.cd, w.ad, oi_unit("Handler", 100.0f), handler);
    const uint32_t vd = push(w.cd, w.ad, oi_unit("Victim", 1000.0f, 5.0f, kCatFrigate), AbilityObjectDesc{});
    const uint32_t bd = push(w.cd, w.ad, oi_unit("Bomber", 50.0f), AbilityObjectDesc{});
    w.make(GameMode::Land);
    bool bomber_moving = false;
    AbilityHost h;
    h.moving = [&bomber_moving](ObjectId id) { return id == 50 && bomber_moving; };
    h.bone_position = [](ObjectId id, std::string_view bone, Vec3& pos) {
        if (id != 1 || bone != "MuzzleA_00") return -1;
        pos = Vec3{5.0f, 0.0f, 10.0f};
        return 0;
    };
    w.ab->set_host(std::move(h));
    w.add(1, hd, 0, {0, 0, 0});
    w.add(2, vd, 1, {200, 0, 0});
    const ObjectId users[] = {1};
    const uint32_t crc = name_crc("Handler_Spawn");
    // One host frame: the ability pass, then the host creates and binds the bomber a request asks for.
    uint32_t spawned_at = 0, applied_at = 0;
    ObjectId next_bomber = 50;
    auto step = [&]() {
        const bool was = w.ab->special_applied(1, crc);
        w.ab->tick(w.frame, w.trees, w.rng, serial_for);
        if (!was && w.ab->special_applied(1, crc)) applied_at = w.frame;
        for (const SpawnRequest& q : w.ab->spawn_requests()) {
            if (q.purpose != SpawnPurpose::Bomber) continue;
            spawned_at = w.frame;
            CHECK(q.object_type == 77 && q.source == 1 && q.target == 2 && q.owner == 0);
            CHECK(q.position.x == 5.0f && q.position.z == 10.0f);  // the Spawn_Bone_Name position
            w.add(next_bomber, bd, 0, q.position);
            CHECK(w.ab->bind_spawn(q.serial, next_bomber));
        }
        for (const DeathEvent& d : w.sys->deaths()) w.trees.remove(static_cast<EntityHandle>(d.id));
        w.collect();
        ++w.frame;
    };
    // Issue_Auto_Target: a pending ability, retried at the targeting cadence until the apply succeeds.
    CHECK(w.ab->targeted_ability(users, kTargetedHack, 2, -1, w.frame) == 1);
    for (int i = 0; i < 100 && spawned_at == 0; ++i) step();
    const uint32_t a = applied_at;
    CHECK(a != 0);
    CHECK_EQ(spawned_at, a + 75u);  // max(1, int(30 x 2.5)) frames after the apply
    step();  // the orders: attack and move to the target
    const AbilityEvent* mv = w.last(AbilityEventKind::MoveOrder, 50);
    CHECK(mv != nullptr && mv && mv->point.x == 200.0f);
    bomber_moving = true;
    w.move(50, Vec3{190, 0, 0});
    for (int i = 0; i < 3; ++i) step();
    CHECK(w.sys->alive(50) && w.sys->health(2) == w.sys->max_health(2));
    bomber_moving = false;  // it reached the target and stopped: it detonates
    for (int i = 0; i < 3 && w.sys->alive(50); ++i) step();
    CHECK(!w.sys->alive(50));
    const uint32_t release = w.frame - 1;
    CHECK(!w.ab->special_applied(1, crc));
    for (int i = 0; i < 3; ++i) step();  // the explosion object detonates
    CHECK(w.sys->health(2) < w.sys->max_health(2));
    // The cooldown runs from the activation (90 frames) and a release blocks 1 s: the next apply comes no earlier than
    // max(a + 90, release + 30) (then at the pending's next retry).
    const float hp = w.sys->health(2);
    next_bomber = 51;
    spawned_at = applied_at = 0;
    CHECK(w.ab->targeted_ability(users, kTargetedHack, 2, -1, w.frame) == 1);
    for (int i = 0; i < 200 && spawned_at == 0; ++i) step();
    const uint32_t ready = std::max(a + 90u, release + 30u);
    CHECK(applied_at >= ready && applied_at < ready + 31u);
    step();
    w.ab->remove(51);  // the bomber is removed before it stops: no explosion (notice 0x71de50)
    for (int i = 0; i < 4; ++i) step();
    CHECK(w.sys->health(2) == hp && !w.ab->special_applied(1, crc));
}

// ledger: verifies GameObjectType.Projectile_Redirect_Chance_Modifier
// ledger: verifies GameObjectType.Projectile_Block_Chance_Modifier
void test_mcx4_redirect_chance_modifiers() {
    // TACTICAL_COMBAT §3.8 (Redirect_Blaster 0x709c90): redirect when a draw < Projectile_Redirect_Chance_Modifier x the
    // redirect chance, else block when a second draw < Projectile_Block_Chance_Modifier x the block chance (type ctor
    // 0x367250: both 1.0). A shot whose modifiers are 0 is neither redirected nor blocked even at chance 1.
    auto run = [](float redirect_mod, float block_mod) {
        World w;
        ProjectileDesc laser;
        laser.name = "Laser";
        laser.damage = 20.0f;
        laser.category = ProjectileCategory::Laser;
        laser.redirect_chance_modifier = redirect_mod;
        laser.block_chance_modifier = block_mod;
        w.cd.projectiles = {laser};
        SpecialAbilityDesc s;
        s.style = ActivationStyle::TakeDamage;
        s.applicable_categories = ~uint64_t{0};
        s.name = "Deflect";
        s.cls = SpecialClass::RedirectBlaster;
        s.redirect_chance = 1.0f;
        s.block_chance = 1.0f;
        const uint32_t sd = add_special(w.ad, s);
        UnitDesc jedi;
        jedi.name = "Jedi";
        jedi.tactical_health = 100.0f;
        AbilityObjectDesc ja;
        ja.specials = {sd};
        const uint32_t jd = push(w.cd, w.ad, jedi, ja);
        UnitDesc trooper;
        trooper.name = "Trooper";
        trooper.tactical_health = 100.0f;
        trooper.category_mask = 0x1;
        const uint32_t td = push(w.cd, w.ad, trooper, AbilityObjectDesc{});
        w.make();
        w.add(1, jd, 0, Vec3{0, 0, 0});
        w.add(2, td, 1, Vec3{100, 0, 0});
        const bool caught = w.ab->damage_listener(1, 2, 0, w.frame, &w.rng);
        return std::pair<bool, int>{caught, w.has_event(AbilityEventKind::Redirected, 1) ? 1 : (w.has_event(AbilityEventKind::Blocked, 1) ? 2 : 0)};
    };
    CHECK(run(1.0f, 1.0f) == (std::pair<bool, int>{true, 1}));   // redirected
    CHECK(run(0.0f, 1.0f) == (std::pair<bool, int>{true, 2}));   // no redirect: blocked
    CHECK(run(0.0f, 0.0f) == (std::pair<bool, int>{false, 0}));  // neither
}

// ledger: verifies HardPoint.Allow_Opportunity_Fire_When_Idle
// ledger: verifies HardPoint.Allow_Opportunity_Fire_When_Targeting
void test_mcx4_opportunity_flags() {
    // MCX4_T10_RE_ANSWERS §9 (HardPoint ctor 0x53e3b0 writes 1 at +0x3d5 / +0x3d6) and ABILITIES_HARDPOINTS §4.2 (0x385620,
    // 0x387400): Allow_Opportunity_Fire_When_Targeting lets a hardpoint shoot a nearby enemy while its owner pursues
    // another target; Allow_Opportunity_Fire_When_Idle lets an idle owner's hardpoint whose Fire_Max_Recharge_Seconds is
    // above Hardpoint_Recharge_Cutoff_For_Opportunity_Fire fire at all. Both default true.
    CHECK(HardpointDesc{}.allow_opportunity_fire_when_idle && HardpointDesc{}.allow_opportunity_fire_when_targeting);
    auto shots = [](bool idle_flag) {
        World w;
        ProjectileDesc p;
        p.name = "P";
        p.max_speed = 40.0f;
        p.damage = 1.0f;
        w.cd.projectiles = {p};
        HardpointDesc t;
        t.name = "HP_T";
        t.kind = HardpointKind::WeaponLaser;
        t.health = 100.0f;
        t.projectile = 0;
        t.min_recharge = 1.0f;
        t.max_recharge = 9.0f;  // above the 3.0 cutoff: only the idle flag allows it
        t.fire_range = 800.0f;
        t.allow_opportunity_fire_when_targeting = false;
        t.allow_opportunity_fire_when_idle = idle_flag;
        w.cd.hardpoints = {t};
        UnitDesc c;
        c.name = "C";
        c.tactical_health = 1e6f;
        c.radius = 20.0f;
        c.hardpoints = {0};
        c.max_attack_distance = 100.0f;  // the owner's own scan does not reach the enemy at 300: it stays idle
        const uint32_t ci = push(w.cd, w.ad, c, AbilityObjectDesc{});
        UnitDesc f;
        f.name = "F";
        f.tactical_health = 1e6f;
        f.radius = 5.0f;
        f.is_valid_target = true;
        const uint32_t fi = push(w.cd, w.ad, f, AbilityObjectDesc{});
        CombatConfig cfg;
        cfg.inaccuracy_enabled = false;
        cfg.opportunity_recharge_cutoff = 3.0f;
        w.make(cfg);
        w.add(1, ci, 0, {0, 0, 0});
        w.add(2, fi, 1, {300, 0, 0});
        for (int i = 0; i < 400; ++i) w.tick();  // no order: the owner is idle
        CHECK(w.sys->current_target(1) == kNoObject);
        return w.sys->stats().shots;
    };
    CHECK(shots(false) == 0);
    CHECK(shots(true) > 0);
    // MCX4_T10_RE_ANSWERS §10: the mode numbers the <Mode>_Mode_Weapon_Delay_Multiplier / Fire_When_In_<Mode>_Mode tags map to.
    CHECK(ability_type_from_name("POWER_TO_WEAPONS") == 0x1d);
    CHECK(ability_type_from_name("DEFEND") == 1);
    CHECK(ability_type_from_name("TURBO") == 9);
    CHECK(ability_type_from_name("FULL_SALVO") == 0x3e);
}

}  // namespace bo3split
