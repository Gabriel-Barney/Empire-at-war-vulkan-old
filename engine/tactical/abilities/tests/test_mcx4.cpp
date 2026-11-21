// mcx4 (Stage 9, box MCX2): ability tags base FoC sets that the port stored without using. Expected values from
// docs/re/MCX4_RE_ANSWERS_TACTICAL.md.
#include "test_abilities_common.hpp"

#include <cmath>
#include <utility>
#include <vector>

namespace bo3split {

namespace {
// A projectile hit without flight data (as a queued Damage_Delay hit).
ListenerShot shot_of(int32_t desc) {
    ListenerShot s;
    s.projectile_desc = desc;
    return s;
}
}  // namespace

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
    CHECK(w.ab->damage_listener(1, 2, shot_of(0), w.frame, &w.rng));       // laser: absorbed
    CHECK_EQ(w.sys->health(1), mx - 30.0f + 20.0f * 0.2f + 1.0f);  // + 5
    CHECK(!w.ab->damage_listener(1, 2, shot_of(1), w.frame, &w.rng));      // a missile is never absorbed
    CHECK_EQ(w.sys->health(1), mx - 25.0f);
    w.sys->set_health(1, mx - 2.0f);
    CHECK(w.ab->damage_listener(1, 2, shot_of(0), w.frame, &w.rng));
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
        const bool caught = w.ab->damage_listener(1, 2, shot_of(0), w.frame, &w.rng);
        return std::pair<bool, int>{caught, w.has_event(AbilityEventKind::Redirected, 1) ? 1 : (w.has_event(AbilityEventKind::Blocked, 1) ? 2 : 0)};
    };
    CHECK(run(1.0f, 1.0f) == (std::pair<bool, int>{true, 1}));   // redirected
    CHECK(run(0.0f, 1.0f) == (std::pair<bool, int>{true, 2}));   // no redirect: blocked
    CHECK(run(0.0f, 0.0f) == (std::pair<bool, int>{false, 0}));  // neither
}

// ledger: verifies Redirect_Blaster_Ability.Max_Projectile_Redirection_Angle_In_Degrees
// ledger: verifies Redirect_Blaster_Ability.Turn_To_Face_Unblockable_Shots
static void test_mcx4_redirect_blaster_shots() {
    // MCX4_T10_RE_ANSWERS §12 / §12a (RedirectBlaster vf30 0x709c90, 0x4cca10): the arc test compares the victim's yaw with
    // the projectile's yaw (d = wrap(victim - projectile); -h < d < h, h = (360 - Reaction_Arc_In_Degrees) / 2, is a shot
    // from behind: not blocked). A redirect sends a new shot of the same type from the projectile's position at the
    // shooter; a block sends one back at projectile yaw + 180 + U(+-Max_Projectile_Redirection_Angle_In_Degrees) (pitch
    // offset only on a second draw < 0.5). Turn_To_Face_Unblockable_Shots gives a facing order to the arc edge x 1.01
    // for a shot from behind. A victim in the Move clip (2) still sends the shot on, but the damage lands.
    struct Rig {
        World w;
        std::vector<std::pair<ObjectId, Vec3>> faced;
        int32_t clip = -1;
        Rig(float redirect, float block, float angle, bool turn) {
            ProjectileDesc laser;
            laser.name = "Laser";
            laser.damage = 20.0f;
            laser.max_speed = 10.0f;
            laser.max_flight_distance = 1000.0f;
            laser.category = ProjectileCategory::Laser;
            w.cd.projectiles = {laser};
            SpecialAbilityDesc s;
            s.style = ActivationStyle::TakeDamage;
            s.applicable_categories = ~uint64_t{0};
            s.name = "Deflect";
            s.cls = SpecialClass::RedirectBlaster;
            s.redirect_chance = redirect;
            s.block_chance = block;
            s.reaction_arc_deg = 180.0f;  // h = 90
            s.max_redirect_angle_deg = angle;
            s.turn_to_face_unblockable = turn;
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
            AbilityHost h;
            h.face_direction = [this](ObjectId id, Vec3 d) { faced.emplace_back(id, d); };
            h.primary_clip = [this](ObjectId) { return clip; };
            w.ab->set_host(std::move(h));
            w.add(1, jd, 0, Vec3{0, 0, 0}, Vec3{1, 0, 0});  // faces +x
            w.add(2, td, 1, Vec3{100, 0, 0});
        }
        bool hit(Vec3 dir) {
            ListenerShot s;
            s.projectile_desc = 0;
            s.flight = true;
            s.pos = Vec3{5, 0, 0};
            s.dir = dir;
            s.damage = 20.0f;
            return w.ab->damage_listener(1, 2, s, w.frame, &w.rng);
        }
    };
    {  // redirect: a frontal shot (projectile yaw 180, d = 180) comes back at the shooter, owned by the victim's player
        Rig r(1.0f, 0.0f, 10.0f, false);
        CHECK(r.hit(Vec3{-1, 0, 0}));
        CHECK(r.w.sys->projectile_count() == 1u);
        const ProjectileView v = r.w.sys->projectile(0);
        CHECK(v.target == 2 && v.owner == 0 && v.shooter == 1 && v.desc == 0u);
        CHECK(v.pos.x == 5.0f && v.dir.x > 0.99f);
        CHECK_EQ(v.damage, 20.0f);
        CHECK_EQ(v.max_travel, 1000.0f);  // the type's flight distance, not the aim distance
    }
    {  // block: back toward the shooter's side within +-10 degrees, level or with a pitch offset of at most 10 degrees
        Rig r(0.0f, 1.0f, 10.0f, false);
        const float c10 = std::cos(10.0f * 0.017453292f) - 1e-5f;
        bool turned = false, pitched = false;
        for (int i = 0; i < 24; ++i) {
            CHECK(r.hit(Vec3{-1, 0, 0}));
            const ProjectileView v = r.w.sys->projectile(r.w.sys->projectile_count() - 1u);
            CHECK(v.target == kNoObject && v.dir.x >= c10 * c10);
            const float lv = std::sqrt(v.dir.x * v.dir.x + v.dir.y * v.dir.y);
            CHECK(lv >= c10 && v.dir.x / lv >= c10);  // |yaw offset| <= 10, |pitch| <= 10
            turned = turned || v.dir.y != 0.0f;
            pitched = pitched || v.dir.z != 0.0f;
        }
        CHECK(turned && pitched);
        CHECK(r.w.sys->projectile_count() == 24u);
    }
    {  // from behind (projectile yaw 0, d = 0, inside -90 .. 90): not blocked, no shot; the turn order goes to yaw -90.9
        Rig r(1.0f, 1.0f, 10.0f, true);
        CHECK(!r.hit(Vec3{1, 0, 0}));
        CHECK(r.w.sys->projectile_count() == 0u);
        CHECK(r.faced.size() == 1u && r.faced[0].first == 1);
        const float t = -90.0f * 1.01f * 0.017453292f;
        CHECK(std::fabs(r.faced[0].second.x - std::cos(t)) < 1e-4f && std::fabs(r.faced[0].second.y - std::sin(t)) < 1e-4f);
        Rig q(1.0f, 1.0f, 10.0f, false);  // without Turn_To_Face_Unblockable_Shots: no order
        CHECK(!q.hit(Vec3{1, 0, 0}) && q.faced.empty());
    }
    {  // a victim in the Move clip: the shot is redirected and the damage still lands
        Rig r(1.0f, 0.0f, 10.0f, false);
        r.clip = 2;
        CHECK(!r.hit(Vec3{-1, 0, 0}));
        CHECK(r.w.sys->projectile_count() == 1u);
    }
    CHECK(SpecialAbilityDesc{}.max_redirect_angle_deg == 0.0f && !SpecialAbilityDesc{}.turn_to_face_unblockable);
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

// ledger: verifies Ability_Base.Terminate_Effect_On_Move_Command
static void test_mcx4_terminate_on_move() {
    // MCX4_T10_RE_ANSWERS §13 (slot 22 0x48e3c0 from the move-order path 0x4f6b60): a new move order of the user ends the
    // applied effects of its specials with Terminate_Effect_On_Move_Command at once; without the flag they run on. FoC
    // sets it on Cable (the snowspeeder's tow cable), Earthquake and Force_Telekinesis.
    auto applied_after_move = [](bool flag) {
        OiWorld w;
        SpecialAbilityDesc cb = oi_special("Cable", SpecialClass::Cable, ActivationStyle::UserInput);
        cb.min_range = 0.0f;
        cb.max_range = 1000.0f;
        cb.cable_drop_phase = 110.0f;
        cb.cable_pull_phase = 140.0f;
        cb.terminate_on_move = flag;  // Terminate_Effect_On_Move_Command
        const uint32_t cs = add_special(w.ad, cb);
        AbilityObjectDesc a;
        a.unit_abilities = {oi_gui(kCableAttack, 0.0f, 10.0f, "Cable")};
        a.specials = {cs};
        const uint32_t sd = push(w.cd, w.ad, oi_unit("Speeder", 100.0f), a);
        UnitDesc wd = oi_unit("Walker", 5000.0f);
        wd.has_locomotor = true;
        const uint32_t td = push(w.cd, w.ad, wd, AbilityObjectDesc{});
        w.make(GameMode::Land);
        w.add(1, sd, 0, {0, 0, 0});
        w.add(2, td, 1, {50, 0, 0});
        const ObjectId one[] = {1};
        CHECK(w.ab->targeted_ability(one, kCableAttack, 2, -1, w.frame) == 1);
        const uint32_t crc = name_crc("Cable");
        for (int i = 0; i < 20 && !w.ab->special_applied(1, crc); ++i) w.tick();
        CHECK(w.ab->special_applied(1, crc));
        w.ab->move_ordered(1, w.frame);
        return w.ab->special_applied(1, crc);
    };
    CHECK(!applied_after_move(true));
    CHECK(applied_after_move(false));
    CHECK(!SpecialAbilityDesc{}.terminate_on_move);
}

// ledger: verifies Personal_Flame_Thrower_Ability.Damage_Delay_In_Secs
static void test_mcx4_flame_damage_delay() {
    // MCX4_T10_RE_ANSWERS §15 (flame damage helper 0x705590): elapsed = Fire_Time_In_Secs - frames left / FPS; the ability's
    // target is hit only once elapsed > Damage_Delay_In_Secs, other enemies in the cone every frame, each by Damage_Amount /
    // ((Fire_Time - Damage_Delay) x FPS). Fire_Time 1 s, delay 0.5 s, amount 30: 2 per frame; the target takes 15 frames
    // (30), the bystander 30 frames (60). Ctor default 0.2 s.
    OiWorld w;
    SpecialAbilityDesc f = oi_special("Flame", SpecialClass::FlameThrower, ActivationStyle::UserInput);
    f.max_range = 100.0f;
    f.duration_secs = 1.0f;       // Fire_Time_In_Secs
    f.flame_damage_delay = 0.5f;  // Damage_Delay_In_Secs
    f.damage_amount = 30.0f;
    f.damage_arc = 90.0f;
    const uint32_t fs = add_special(w.ad, f);
    AbilityObjectDesc a;
    a.unit_abilities = {oi_gui(kFlameThrower, 0.0f, 10.0f, "Flame")};
    a.specials = {fs};
    const uint32_t ud = push(w.cd, w.ad, oi_unit("Bossk", 100.0f), a);
    const uint32_t vd = push(w.cd, w.ad, oi_unit("Victim", 1000.0f), AbilityObjectDesc{});
    w.make(GameMode::Land);
    w.add(1, ud, 0, {0, 0, 0});
    w.add(2, vd, 1, {50, 0, 0});  // the target
    w.add(3, vd, 1, {50, 5, 0});  // in the cone
    const ObjectId one[] = {1};
    CHECK(w.ab->targeted_ability(one, kFlameThrower, 2, -1, w.frame) == 1);
    for (int i = 0; i < 60; ++i) w.tick();
    CHECK(std::fabs((w.sys->max_health(2) - w.sys->health(2)) - 30.0f) < 1e-3f);
    CHECK(std::fabs((w.sys->max_health(3) - w.sys->health(3)) - 60.0f) < 1e-3f);
    CHECK_EQ(SpecialAbilityDesc{}.flame_damage_delay, 0.2f);
}

// ledger: verifies Hack_Ability.Team_Member_With_Ability
static void test_mcx4_hack_team_member() {
    // MCX4_T10_RE_ANSWERS §15 (Hack slots 10 / 11 0x701ab0 / 0x701ad0): for a user inside a team, Team_Member_With_Ability
    // names the member that acts - FoC Tactical_R2_3PO_Team: Droid_R2D2 hacks for the team. Here the team uses the
    // ability and the hack clip (0x5a) exists only on R2 (id 1): with the tag it plays there; without, the team itself
    // acts, has no clip, and none plays.
    auto played_on = [](bool tag) {
        OiWorld w;
        SpecialAbilityDesc hk = oi_special("Hack", SpecialClass::Hack, ActivationStyle::UserInput);
        hk.max_range = 1000.0f;
        if (tag) hk.team_member_type = 11;  // Team_Member_With_Ability: Droid_R2D2
        const uint32_t hs = add_special(w.ad, hk);
        AbilityObjectDesc r2;
        r2.object_type = 11;
        const uint32_t rd = push(w.cd, w.ad, oi_unit("Droid_R2D2", 100.0f), r2);
        AbilityObjectDesc tp;
        tp.object_type = 12;
        const uint32_t pd = push(w.cd, w.ad, oi_unit("Droid_C3PO", 100.0f), tp);
        AbilityObjectDesc team;
        team.name = "Tactical_R2_3PO_Team";
        team.unit_abilities = {oi_gui(kTargetedHack, 0.0f, 10.0f, "Hack")};
        team.specials = {hs};
        const uint32_t cd = push_container(w.cd, w.ad, team);
        const uint32_t td = push(w.cd, w.ad, oi_unit("T", 100.0f), AbilityObjectDesc{});
        w.make(GameMode::Land);
        AbilityHost h;
        h.anim_clip = [](ObjectId id, int32_t type, std::string_view, uint32_t, uint32_t& n, float& f) {
            if (id != 1 || type != 0x5a) return false;
            n = 40;
            f = 30.0f;
            return true;
        };
        w.ab->set_host(std::move(h));
        w.add(1, rd, 0, {0, 0, 0});
        w.add(2, pd, 0, {10, 0, 0});
        w.add(3, td, 1, {300, 0, 0});
        const ObjectId members[] = {1, 2};
        CHECK(w.ab->add_container(5, cd, 0, members, w.frame));
        std::vector<AbilityAnimPlay> plays;
        w.ab->take_anim_plays(plays);
        const ObjectId five[] = {5};
        CHECK(w.ab->targeted_ability(five, kTargetedHack, 3, -1, w.frame) == 1);
        ObjectId on = kNoObject;
        for (int i = 0; i < 5 && on == kNoObject; ++i) {
            w.tick();
            w.ab->take_anim_plays(plays);
            for (const AbilityAnimPlay& p : plays)
                if (p.type == 0x5a) on = p.id;
        }
        return on;
    };
    CHECK(played_on(true) == 1);
    CHECK(played_on(false) == kNoObject);
    CHECK(SpecialAbilityDesc{}.team_member_type == -1);
}

// ledger: verifies GameObjectType.Is_Force_Sensitive
static void test_mcx4_hunt_force_sensitive() {
    // MCX4_T10_RE_ANSWERS §5 (HUNT 0x627d40 / 0x628b40, read 2026-11-18): a hunter whose type has Is_Force_Sensitive skips
    // the random-point step (its draw is still made), picks among the force-sensitive objects first (one Get_Int) and
    // goes to the exact position (no 30..400 jitter draws). Objects 2 (plain) and 3 (force sensitive).
    OiWorld w;
    AbilityObjectDesc ha;
    ha.unit_abilities = {rec(kHunt, 0.0f, 0.0f)};
    ha.force_sensitive = true;
    const uint32_t hu = push(w.cd, w.ad, oi_unit("Vader", 100.0f), ha);
    const uint32_t td = push(w.cd, w.ad, oi_unit("T", 100.0f), AbilityObjectDesc{});
    AbilityObjectDesc ja;
    ja.force_sensitive = true;
    const uint32_t jd = push(w.cd, w.ad, oi_unit("Luke", 100.0f), ja);
    w.make();
    w.add(1, hu, 0, {0, 0, 0});
    w.add(2, td, 1, {400, 100, 0});
    w.add(3, jd, 1, {-200, 600, 0});
    const ObjectId one[] = {1};
    CHECK(w.ab->set_unit_ability_mode(one, kHunt, true, w.frame) == 1);
    SimRng pred = w.rng;
    (void)pred.range_float(0.0f, 1.0f);  // u: drawn, the point step skipped
    (void)pred.range_int(0, 0);          // the force-sensitive list {3}
    w.tick();
    CHECK(w.rng.state() == pred.state());
    const AbilityEvent* mv = w.last(AbilityEventKind::MoveOrder, 1);
    CHECK(mv && mv->point.x == -200.0f && mv->point.y == 600.0f);
    CHECK(!AbilityObjectDesc{}.force_sensitive);
}

// ledger: verifies GameConstants.Max_Remote_Bombs_Per_Player
static void test_mcx4_remote_bomb_cap() {
    // ABILITIES_HARDPOINTS §2.4 (Can_Activate 0x39b480 case 64): PLACE_REMOTE_BOMB needs the owner's remote-bomb count <
    // Max_Remote_Bombs_Per_Player (FoC 20). With a cap of 1, one tossed bomb blocks the next placement.
    auto blocked_after_one = [](int32_t cap) {
        OiWorld w;  // rb_world's bomber and target, with the cap in the AbilityConfig
        ProjectileDesc bomb = lob_sticky_bomb();
        bomb.name = "Proj_Remote_Bomb";
        bomb.friendly_damage_amount = 200.0f;
        w.cd.projectiles = {bomb};
        SpecialAbilityDesc r = oi_special("Remote", SpecialClass::RemoteBomb, ActivationStyle::UserInput);
        r.max_range = 200.0f;
        r.projectile = 0;
        r.spawn_frame = 12.0f;
        AbilityObjectDesc a;
        a.unit_abilities = {oi_gui(kPlaceRemoteBomb, 0.0f, 30.0f, "Remote"), rec(kDetonateRemoteBomb, 0.0f, 0.0f)};
        a.specials = {add_special(w.ad, r)};
        UnitDesc bd = oi_unit("Bomber", 100.0f);
        bd.ranged_target_z_adjust = 4.0f;
        const uint32_t ud = push(w.cd, w.ad, bd, a);
        UnitDesc tdesc = oi_unit("Tank", 1000.0f);
        tdesc.ranged_target_z_adjust = 6.0f;
        const uint32_t td = push(w.cd, w.ad, tdesc, AbilityObjectDesc{});
        w.make(GameMode::Land);
        AbilityConfig acfg;
        acfg.mode = GameMode::Land;
        acfg.corvette_category = kCatCorvette;
        acfg.max_remote_bombs_per_player = cap;
        w.ab.reset();
        w.ab = std::make_unique<AbilitySystem>(*w.sys, w.ad, acfg);
        w.ab->bind_rng(&w.rng);
        w.add(1, ud, 0, {0, 0, 0});
        w.add(2, td, 1, {120, 0, 0});
        w.tick();
        w.tick();
        CHECK(w.ab->can_activate(1, kPlaceRemoteBomb, false, false, w.frame));
        const ObjectId one[] = {1};
        CHECK(w.ab->targeted_ability(one, kPlaceRemoteBomb, 2, -1, w.frame) == 1);
        for (int i = 0; i < 40 && w.sys->projectile_count() == 0u; ++i) w.tick();
        CHECK(w.sys->projectile_count() == 1u);
        return !w.ab->can_activate(1, kPlaceRemoteBomb, false, false, w.frame);
    };
    CHECK(blocked_after_one(1));
    CHECK(!blocked_after_one(2));
    CHECK(AbilityConfig{}.max_remote_bombs_per_player == 20);
}

// ledger: verifies Tractor_Beam_Attack_Ability.Inaccuracy_Radius
static void test_mcx4_corrupt_inaccuracy() {
    // MCX4_T10_RE_ANSWERS §15 (apply 0x71d4a0 -> pack +0x14; own-weapon inaccuracy 0x549f80): while Corrupt_Systems corrupts
    // a unit, its own weapon's fixed inaccuracy radius is the ability's Inaccuracy_Radius (FoC IG-88: 500). The table row
    // is listed under Tractor_Beam_Attack_Ability (a field-table segment slip); the field is Corrupt_Systems'.
    OiWorld w;
    SpecialAbilityDesc c = oi_special("Corrupt", SpecialClass::CorruptSystems, ActivationStyle::UserInput);
    c.max_range = 1000.0f;
    c.duration_secs = 5.0f;
    c.inaccuracy_radius = 500.0f;  // Inaccuracy_Radius
    const uint32_t cs = add_special(w.ad, c);
    AbilityObjectDesc a;
    a.unit_abilities = {oi_gui(kCorruptSystems, 0.0f, 10.0f, "Corrupt")};
    a.specials = {cs};
    const uint32_t ud = push(w.cd, w.ad, oi_unit("IG88", 100.0f), a);
    const uint32_t vd = push(w.cd, w.ad, oi_unit("Victim", 1000.0f), AbilityObjectDesc{});
    w.make(GameMode::Land);
    w.add(1, ud, 0, {0, 0, 0});
    w.add(2, vd, 1, {100, 0, 0});
    float r = -1.0f;
    CHECK(!w.ab->corrupt_inaccuracy(2, r));
    const ObjectId one[] = {1};
    CHECK(w.ab->targeted_ability(one, kCorruptSystems, 2, -1, w.frame) == 1);
    for (int i = 0; i < 3; ++i) w.tick();
    CHECK(w.ab->corrupt_inaccuracy(2, r) && r == 500.0f);
    CHECK(!w.ab->corrupt_inaccuracy(1, r));  // the user is not corrupted
}

// ledger: verifies Buzz_Droids_Ability.Chase_Radius
// ledger: verifies Buzz_Droids_Ability.Own_Damage_Per_Second
// ledger: verifies Buzz_Droids_Ability.Enemy_Damage_Per_Second
static void test_mcx4_buzz_droids() {
    // MCX4_T10_RE_ANSWERS §15 (BuzzDroidsBehavior 0x6379a0 / 0x637540): the cloud damages itself by Own_Damage_Per_Second /
    // FPS every frame; enemies within Chase_Radius of the target point whose CategoryMask meets the cloud type's
    // Special_Weapon_Valid_Targets take Enemy_Damage_Per_Second / FPS while inside Damage_Radius of the cloud; with none
    // inside, the cloud moves its Max_Speed toward the nearest. Cloud at the capital (not a valid target) at 200; fighter
    // at 300: 10 per frame toward it, in range (< 50) after 6 moves; then 1 per frame on the fighter. A fighter at 600
    // (outside Chase_Radius 300 of the point) is never touched.
    OiWorld w;
    SpecialAbilityDesc b = oi_special("Buzz", SpecialClass::BuzzDroids, ActivationStyle::UserInput);
    b.max_range = 1000.0f;
    b.activation_time = 0.0f;
    b.chase_radius = 300.0f;            // Chase_Radius
    b.damage_range = 50.0f;             // Damage_Radius
    b.own_damage_per_second = 3.0f;     // Own_Damage_Per_Second
    b.enemy_damage_per_second = 30.0f;  // Enemy_Damage_Per_Second
    b.spawn_type = 77;
    const uint32_t bs = add_special(w.ad, b);
    AbilityObjectDesc a;
    a.unit_abilities = {oi_gui(kTargetedHack, 0.0f, 10.0f, "Buzz")};  // any targeted slot forwarding to the class
    a.specials = {bs};
    const uint32_t ud = push(w.cd, w.ad, oi_unit("Viper", 100.0f), a);
    AbilityObjectDesc cloud;
    cloud.special_weapon_valid_targets = kCatFighter;
    cloud.max_speed = 10.0f;
    const uint32_t cd = push(w.cd, w.ad, oi_unit("Buzz_Droids", 20.0f, 5.0f, kCatCorvette), cloud);
    const uint32_t capd = push(w.cd, w.ad, oi_unit("Capital", 1000.0f, 5.0f, kCatCapital), AbilityObjectDesc{});
    const uint32_t fd = push(w.cd, w.ad, oi_unit("Fighter", 1000.0f, 5.0f, kCatFighter), AbilityObjectDesc{});
    w.make();
    w.add(1, ud, 0, {0, 0, 0});
    w.add(2, capd, 1, {200, 0, 0});
    w.add(3, fd, 1, {300, 0, 0});
    w.add(4, fd, 1, {600, 0, 0});
    const ObjectId one[] = {1};
    CHECK(w.ab->targeted_ability(one, kTargetedHack, 2, -1, w.frame) == 1);
    bool bound = false;
    for (int i = 0; i < 60 && !bound; ++i) {  // one host frame: the ability pass, then the host binds the cloud
        w.ab->tick(w.frame, w.trees, w.rng, serial_for);
        for (const SpawnRequest& q : w.ab->spawn_requests())
            if (q.object_type == 77 && !bound) {
                CHECK(q.position.x == 200.0f);  // at the target's position, at the apply
                w.add(50, cd, 0, q.position);
                CHECK(w.ab->bind_spawn(q.serial, 50));
                bound = true;
            }
        w.collect();
        ++w.frame;
    }
    CHECK(bound);
    for (int i = 0; i < 6; ++i) w.tick();
    CHECK(w.sys->position(50).x == 260.0f);
    CHECK(w.sys->health(3) == w.sys->max_health(3));
    for (int i = 0; i < 10; ++i) w.tick();
    CHECK(w.sys->position(50).x == 260.0f);  // in range: it stays
    CHECK(std::fabs((w.sys->max_health(3) - w.sys->health(3)) - 10.0f) < 1e-3f);
    CHECK(w.sys->health(2) == w.sys->max_health(2) && w.sys->health(4) == w.sys->max_health(4));
    CHECK(std::fabs((w.sys->max_health(50) - w.sys->health(50)) - 16.0f * 0.1f) < 1e-3f);  // 3 / 30 per active frame
}

void test_mcx4_tac2() {
    test_mcx4_redirect_blaster_shots();
    test_mcx4_terminate_on_move();
    test_mcx4_flame_damage_delay();
    test_mcx4_hack_team_member();
    test_mcx4_hunt_force_sensitive();
    test_mcx4_remote_bomb_cap();
    test_mcx4_corrupt_inaccuracy();
    test_mcx4_buzz_droids();
}

}  // namespace bo3split
