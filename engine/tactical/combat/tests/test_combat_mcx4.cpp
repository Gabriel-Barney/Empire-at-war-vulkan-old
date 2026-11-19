// mcx4 (Stage 9, box MCX2): honour gaps - retail-read object tags that base FoC sets and the port stored without using.
// Each test cites the docs/re spec its expected values come from.
#include "test_combat_common.hpp"

namespace bo3split {

namespace {
// One shooter (player 0) at the origin facing +x, one target (player 1) at (500, 0, 0) (a grenade: 100); the shooter's own weapon
// fires projectile 0 with a fixed 40-unit inaccuracy radius, so the aim point is off the target line.
struct ForwardWorld {
    World w;
    explicit ForwardWorld(bool fires_forward, bool lobbed = false) {
        ProjectileDesc p;
        p.max_speed = 10.0f;
        p.damage = 1.0f;
        if (lobbed) {
            p.lobbed = true;
            p.category = ProjectileCategory::Grenade;
            p.max_speed = 30.0f;
            p.grenade_gravity = 1.0f;
        }
        w.data.projectiles.push_back(p);
        UnitDesc s;
        s.tactical_health = 100.0f;
        s.max_attack_distance = 1000.0f;
        s.weapon_projectile = 0;
        s.weapon_recharge = 1.0f;
        s.fire_inaccuracy_fixed_radius = 40.0f;
        s.fires_forward = fires_forward;
        UnitDesc t;
        t.tactical_health = 1e6f;
        t.radius = 10.0f;
        t.category_mask = kCatCapital;
        const uint32_t ds = push_unit(w.data, s), dt = push_unit(w.data, t);
        CombatConfig cfg;
        cfg.mode = GameMode::Land;
        PlayerInfo p0;
        p0.ai_accuracy = 1.0f;
        w.make(cfg, {p0, PlayerInfo{}});
        w.add(1, ds, 0, Vec3{0, 0, 0}, Vec3{1, 0, 0});
        w.add(2, dt, 1, Vec3{lobbed ? 100.0f : 500.0f, 0, 0});  // inside the lob reach (g = gravity x lob mod 5.02)
        w.sys->order_attack(1, 2);
    }
    bool fire() {
        for (int i = 0; i < 40 && w.sys->projectile_count() == 0; ++i) w.tick();
        return w.sys->projectile_count() >= 1;
    }
};
}  // namespace

// ledger: verifies GameObjectType.Fires_Forward
void test_fires_forward() {
    // LOB_SOLVER §5.3: the own-weapon aim 0x54a270 gives the facing F = angles(muzzle -> led, inaccurate FireAt point);
    // with Fires_Forward (+0x44) F = the shooter's own orientation. The shooter faces the target (+x), so a plain shot
    // leaves toward the drawn aim offset and a Fires_Forward shot leaves exactly along +x.
    {
        ForwardWorld f(false);
        CHECK(f.fire());
        const Vec3 d = f.w.sys->projectile(0).dir;
        CHECK(d.y != 0.0f);  // the 40-unit X / Y draws bend it off the axis
    }
    {
        ForwardWorld f(true);
        CHECK(f.fire());
        const Vec3 d = f.w.sys->projectile(0).dir;
        CHECK_F(d.x, 1.0f);
        CHECK_F(d.y, 0.0f);
        CHECK_F(d.z, 0.0f);
    }
    {  // §5.3 for category 3: orientation (F.roll, -theta, F.yaw) - the solver's pitch on the shooter's own heading
        ForwardWorld plain(false, true), fwd(true, true);
        CHECK(plain.fire() && fwd.fire());
        const Vec3 a = plain.w.sys->projectile(0).dir, b = fwd.w.sys->projectile(0).dir;
        CHECK(b.z > 0.0f);  // still lobbed upward
        CHECK_F(b.y, 0.0f);  // on the +x heading
        CHECK(a.y != 0.0f);
    }
}

// ledger: verifies GameObjectType.Projectile_Max_Scan_Range
void test_lost_target_no_rescan() {
    // TACTICAL_COMBAT §3.3 "Lost targets": a homing missile whose target is gone never re-acquires one (the scan's best
    // distance starts at 0), however large Projectile_Max_Scan_Range is: it flies on straight.
    World w;
    ProjectileDesc m;
    m.max_speed = 5.0f;
    m.homing = true;
    m.category = ProjectileCategory::Missile;
    m.max_rate_of_turn = 90.0f;
    m.max_flight_distance = 3000.0f;
    w.data.projectiles.push_back(m);
    UnitDesc t;
    t.tactical_health = 10.0f;
    t.radius = 5.0f;
    t.category_mask = kCatCapital;
    const uint32_t dt = push_unit(w.data, t);
    CombatConfig cfg;
    cfg.mode = GameMode::Space;
    w.make(cfg);
    w.add(2, dt, 1, Vec3{400, 0, 0});
    w.add(3, dt, 1, Vec3{100, 300, 0});  // a second enemy well inside any scan range, off the line of flight
    ProjectileLaunch l;
    l.owner = 0;
    l.projectile = 0;
    l.from = Vec3{0, 0, 0};
    l.target = 2;
    CHECK(w.sys->launch_projectile(l, w.frame));
    w.tick();
    w.tick();
    CHECK(w.sys->remove_unit(2));  // the target is gone (host removal)
    w.trees.remove(2u);
    const Vec3 before = w.sys->projectile(0).dir;
    for (int i = 0; i < 10; ++i) w.tick();
    CHECK(w.sys->projectile_count() >= 1);
    const ProjectileView v = w.sys->projectile(0);
    CHECK(same(v.dir, before));  // not turned toward unit 3
}

// ledger: verifies GameObjectType.Projectile_Blast_Area_Dropoff
// ledger: verifies GameObjectType.Projectile_Blast_Area_Dropoff_Tiers
void test_blast_dropoff() {
    // Area damage 0x4c7f70 (TACTICAL_COMBAT 3.5 steps 4-5, read 2026-11-18): only with Projectile_Blast_Area_Dropoff (and
    // R > 0) is d cut to the victim and factor = clamp(1 - floor(d x (tiers / R)) x (1 / (tiers + 1)), 0, 1) applied;
    // without it every victim in R takes the full blast. Bomb at the origin, R 100, 400 damage, Projectile_Blast_Area_
    // Dropoff_Tiers 3 (Projectile_Blast_Area_Dropoff_Tiers); victims of radius
    // 5 at 20 / 55 / 90: cut distances 15 / 50 / 85 -> floor 0 / 1 / 2 -> factors 1 / 0.75 / 0.5.
    auto run = [](bool dropoff, int32_t tiers, float out[3]) {
        World w;
        ProjectileDesc b;
        b.blast_area_damage = 400.0f;
        b.blast_area_range = 100.0f;
        b.blast_dropoff = dropoff;
        b.blast_dropoff_tiers = tiers;
        w.data.projectiles.push_back(b);
        UnitDesc u;
        u.tactical_health = 1000.0f;
        u.radius = 5.0f;
        const uint32_t body = push_unit(w.data, u);
        CombatConfig cfg;
        cfg.mode = GameMode::Land;
        w.make(cfg);
        w.add(1, body, 1, Vec3{20, 0, 0});
        w.add(2, body, 1, Vec3{0, 55, 0});
        w.add(3, body, 1, Vec3{-90, 0, 0});
        ProjectileLaunch l;
        l.owner = 0;
        l.projectile = 0;
        l.from = Vec3{0, 0, 0};
        l.fuse_seconds = 0.0f;
        CHECK(w.sys->launch_projectile(l, w.frame));
        for (int i = 0; i < 4 && w.sys->projectile_count() > 0; ++i) w.tick();
        for (int k = 0; k < 3; ++k) {
            const auto id = static_cast<ObjectId>(k + 1);
            out[k] = w.sys->max_health(id) - w.sys->health(id);
        }
    };
    float d[3], f[3];
    run(true, 3, d);
    CHECK_F(d[0], 400.0f);
    CHECK_F(d[1], 300.0f);
    CHECK_F(d[2], 200.0f);
    run(false, 3, f);  // no Dropoff: the tiers are not read
    CHECK(f[0] == 400.0f && f[1] == 400.0f && f[2] == 400.0f);
    CHECK(ProjectileDesc{}.blast_dropoff_tiers == 5);  // type ctor 0x367250 default
}

// ledger: verifies GameObjectType.Projectile_Bomb_Fall_Accel_Rate
void test_bomb_fall() {
    // MCX4_RE_ANSWERS_TACTICAL §12 (projectile service 0x4ce6a0): a BOMB / CLUSTER_BOMB shot moves by speed along its
    // direction, then drops by its fall speed f; f starts at 0 and grows by Projectile_Bomb_Fall_Accel_Rate after each
    // move, so the n-th move drops rate x (n - 1). Other categories ignore the rate. FoC bombing-run bomb: speed 4, rate 0.2.
    auto run = [](ProjectileCategory cat, float out_dz[6]) {
        World w;
        ProjectileDesc b;
        b.max_speed = 4.0f;
        b.category = cat;
        b.bomb_fall_accel = 0.2f;
        b.max_flight_distance = 5000.0f;
        w.data.projectiles.push_back(b);
        CombatConfig cfg;
        cfg.mode = GameMode::Space;  // no ground: no mode test in retail, and no ground hit here
        w.make(cfg);
        ProjectileLaunch l;
        l.owner = 0;
        l.projectile = 0;
        l.from = Vec3{0, 0, 0};
        l.point = Vec3{4000, 0, 0};
        CHECK(w.sys->launch_projectile(l, w.frame));
        float z = 0.0f;
        int moves = 0;
        for (int i = 0; i < 20 && moves < 6; ++i) {
            w.tick();
            CHECK(w.sys->projectile_count() == 1);
            const ProjectileView v = w.sys->projectile(0);
            if (v.pos.x == 0.0f) continue;  // not moved yet (spawn frame)
            out_dz[moves++] = v.pos.z - z;
            z = v.pos.z;
        }
        CHECK(moves == 6);
    };
    float bomb[6], cluster[6], laser[6];
    run(ProjectileCategory::Bomb, bomb);
    run(ProjectileCategory::ClusterBomb, cluster);
    run(ProjectileCategory::Laser, laser);
    for (int n = 0; n < 6; ++n) {
        CHECK(std::fabs(bomb[n] + 0.2f * static_cast<float>(n)) < 1e-4f);  // move n + 1 drops rate x n (z differences round)
        CHECK(std::fabs(cluster[n] + 0.2f * static_cast<float>(n)) < 1e-4f);
        CHECK_F(laser[n], 0.0f);
    }
}

// ledger: verifies HardPoint.Power_To_Weapons_Mode_Weapon_Delay_Multiplier
// ledger: verifies HardPoint.Defend_Mode_Weapon_Delay_Multiplier
// ledger: verifies HardPoint.Full_Salvo_Weapon_Delay_Multiplier
// ledger: verifies HardPoint.Fire_When_In_Power_To_Weapons_Mode
void test_mode_weapon_delay() {
    // MCX4_T10_RE_ANSWERS §10 (Fire_At 0x3825b0): while the owner's unit ability of slot 0 / 1 is active, the recharge factor
    // W takes the hardpoint's <Mode>_Mode_Weapon_Delay_Multiplier entry of that ability type (table +0x250 + 4 m); on the
    // full recharge W applies only when < 1 (TACTICAL_COMBAT §3.1 step 12). FoC tags: Defend_Mode_Weapon_Delay_Multiplier
    // (m 1), Power_To_Weapons_Mode_Weapon_Delay_Multiplier (m 0x1d), Full_Salvo_Weapon_Delay_Multiplier (m 0x3e; the
    // name lookup is checked in the abilities test). Fire_When_In_Power_To_Weapons_Mode No (the
    // +0x384 + 0x1d byte, 0x384850 step 4) stops the hardpoint firing while POWER_TO_WEAPONS is active.
    auto gap = [](uint8_t mode_type, float factor, bool active, bool no_fire, int& shots) {
        World w;
        ProjectileDesc p;
        p.max_speed = 50.0f;
        p.damage = 1.0f;
        w.data.projectiles.push_back(p);
        HardpointDesc hd;
        hd.kind = HardpointKind::WeaponLaser;
        hd.health = 100.0f;
        hd.projectile = 0;
        hd.min_recharge = hd.max_recharge = 1.0f;  // 30 frames, no draw
        hd.fire_range = 1000.0f;
        hd.weapon_delay = {{mode_type, factor}};
        if (no_fire) hd.no_fire_modes = {kAbilityPowerToWeapons};
        w.data.hardpoints.push_back(hd);
        UnitAbilityDesc ab;
        ab.ability_type = kAbilityPowerToWeapons;
        w.data.abilities.push_back(ab);
        UnitDesc sh;
        sh.tactical_health = 100.0f;
        sh.max_attack_distance = 1000.0f;
        sh.hardpoints = {0};
        sh.abilities = {0, -1};
        UnitDesc tg;
        tg.tactical_health = 1e6f;
        tg.radius = 10.0f;
        const uint32_t ds = push_unit(w.data, sh), dt = push_unit(w.data, tg);
        CombatConfig cfg;
        cfg.inaccuracy_enabled = false;
        w.make(cfg);
        w.add(1, ds, 0, Vec3{0, 0, 0});
        w.add(2, dt, 1, Vec3{300, 0, 0});
        if (active) w.sys->set_ability_active(1, 0, true);
        w.sys->order_attack(1, 2);
        int32_t first = -1, second = -1;
        for (int i = 0; i < 400 && second < 0; ++i) {
            const uint64_t before = w.sys->stats().shots;
            w.tick();
            if (w.sys->stats().shots > before) (first < 0 ? first : second) = i;
        }
        shots = static_cast<int>(w.sys->stats().shots);
        return first >= 0 && second >= 0 ? second - first : -1;
    };
    int n = 0;
    CHECK(gap(kAbilityPowerToWeapons, 0.5f, false, false, n) == 30);  // inactive: the table is not read
    CHECK(gap(kAbilityPowerToWeapons, 0.5f, true, false, n) == 15);   // int(30 x 0.5 + 0.5)
    CHECK(gap(kAbilityPowerToWeapons, 2.0f, true, false, n) == 30);   // W >= 1: not applied on the full recharge
    CHECK(gap(kAbilityDefend, 0.5f, true, false, n) == 30);           // another mode's entry: not the active ability's
    CHECK(gap(kAbilityPowerToWeapons, 1.0f, true, true, n) == -1 && n == 0);  // Fire_When_In_Power_To_Weapons_Mode No
    CHECK(gap(kAbilityPowerToWeapons, 1.0f, false, true, n) == 30);   // ... fires again once the mode is off
}

// ledger: verifies GameObjectType.Deployed_Max_Attack_Distance_Multiplier
void test_deployed_attack_distance() {
    // MCX4_T10_RE_ANSWERS §8 (getter 0x3749d0): Targeting_Max_Attack_Distance x Deployed_Max_Attack_Distance_Multiplier
    // while deployed (0x39a390), unless garrisoned (then the garrison multiplier, the host's FireRange entry); ctor 1.0.
    World w;
    UnitDesc u;
    u.tactical_health = 100.0f;
    u.max_attack_distance = 400.0f;
    u.deploys = true;
    u.has_locomotor = true;
    u.deployed_max_attack_distance_multiplier = 1.5f;
    const uint32_t d = push_unit(w.data, u);
    CHECK_F(UnitDesc{}.deployed_max_attack_distance_multiplier, 1.0f);
    w.make();
    w.add(1, d, 0, Vec3{0, 0, 0});
    CHECK_F(w.sys->attack_range(1), 400.0f);
    w.sys->set_flag(1, kUnitDeploying, true);
    CHECK_F(w.sys->attack_range(1), 600.0f);
    w.sys->set_flag(1, kUnitGarrisoned, true);
    CHECK_F(w.sys->attack_range(1), 400.0f);
}

// ledger: verifies GameConstants.Depleted_Shield_Damage_Increment
void test_depleted_shield_increment() {
    // MCX4_T10_RE_ANSWERS §16 (Shield::Absorb 0x549ac0): a hit with damage > 0 on a depleted shield extends its lockout by
    // int(FPS x Depleted_Shield_Damage_Increment) frames (FoC 0.0: no change). Lockout 2 s; a shield set back to 50 at
    // depletion + 70 frames absorbs again without the extension, and not when one hit at + 10 added 30 frames.
    auto absorbed_at_70 = [](float increment) {
        World w;
        UnitDesc v;
        v.tactical_health = 1e6f;
        v.shield_points = 100.0f;
        const uint32_t d = push_unit(w.data, v);
        CombatConfig cfg;
        cfg.depleted_shield_disable_time = 2.0f;
        cfg.depleted_shield_damage_increment = increment;
        w.make(cfg);
        w.add(1, d, 0, Vec3{0, 0, 0});
        DamageRequest r;
        r.victim = 1;
        r.amount = 150.0f;
        CHECK_F(w.sys->take_damage(r, 1000).absorbed, 100.0f);  // depletes at frame 1000
        r.amount = 5.0f;
        CHECK_F(w.sys->take_damage(r, 1010).absorbed, 0.0f);    // in the lockout
        w.sys->set_shield(1, 50.0f);
        // right after the extension the depletion frame lies ahead (1030 with 1.0 s): still locked, not wrapped around
        CHECK_F(w.sys->take_damage(r, 1011).absorbed, 0.0f);
        w.sys->set_shield(1, 50.0f);
        return w.sys->take_damage(r, 1070).absorbed;
    };
    CHECK_F(absorbed_at_70(0.0f), 5.0f);
    CHECK_F(absorbed_at_70(1.0f), 0.0f);
    CHECK_F(CombatConfig{}.depleted_shield_damage_increment, 0.0f);
}

// ledger: verifies GameObjectType.Out_Of_Combat_Defense_Adjustment
void test_out_of_combat_defense() {
    // MCX4_T10_RE_ANSWERS §6 / §18 (fighter states 0x5ce010 / 0x5cd8e0 / 0x5cbea0 and 0x5cc220): Out_Of_Combat_Defense_Adjustment
    // is a defence entry (list 0, damage x (1 - r)) set by source while the fighter flies out of combat and overwritten
    // with 0 in the combat states. FoC fighters: -1.0, so they take double damage out of combat.
    World w;
    UnitDesc f;
    f.tactical_health = 1e6f;
    f.out_of_combat_defense_adjustment = -1.0f;
    UnitDesc plain = f;
    plain.out_of_combat_defense_adjustment = 0.0f;
    const uint32_t df = push_unit(w.data, f), dp = push_unit(w.data, plain);
    w.make();
    w.add(1, df, 0, Vec3{0, 0, 0});
    w.add(2, dp, 0, Vec3{100, 0, 0});
    auto hit = [&](ObjectId v) {
        DamageRequest r;
        r.victim = v;
        r.amount = 10.0f;
        return w.sys->take_damage(r, w.frame).after_multipliers;
    };
    CHECK_F(hit(1), 10.0f);  // no entry yet
    w.sys->set_out_of_combat(1, true);
    CHECK_F(hit(1), 20.0f);
    w.sys->set_out_of_combat(1, true);  // replaced by source, not stacked
    CHECK_F(hit(1), 20.0f);
    w.sys->set_out_of_combat(1, false);  // a combat state writes 0
    CHECK_F(hit(1), 10.0f);
    w.sys->set_out_of_combat(2, true);  // adjustment 0: nothing
    CHECK_F(hit(2), 10.0f);
}

// ledger: verifies HardPoint.Projectile_Appearance_Delay_Frames
void test_projectile_appearance_delay() {
    // MCX4_T10_RE_ANSWERS §3 (Fire_At 0x3825b0, projectile service 0x4ce6a0): a hardpoint with Projectile_Appearance_Delay_Frames
    // N > 0 creates its shot at once, but the shot does not move until N frames later; the recharge is unchanged.
    auto first_move = [](int32_t delay, int& fired_at) {
        World w;
        ProjectileDesc p;
        p.max_speed = 10.0f;
        p.damage = 1.0f;
        w.data.projectiles.push_back(p);
        HardpointDesc hd;
        hd.kind = HardpointKind::WeaponLaser;
        hd.health = 100.0f;
        hd.projectile = 0;
        hd.min_recharge = hd.max_recharge = 5.0f;
        hd.fire_range = 1000.0f;
        hd.projectile_appearance_delay_frames = delay;
        w.data.hardpoints.push_back(hd);
        UnitDesc sh;
        sh.tactical_health = 100.0f;
        sh.max_attack_distance = 1000.0f;
        sh.hardpoints = {0};
        UnitDesc tg;
        tg.tactical_health = 1e6f;
        tg.radius = 10.0f;
        const uint32_t ds = push_unit(w.data, sh), dt = push_unit(w.data, tg);
        CombatConfig cfg;
        cfg.inaccuracy_enabled = false;
        w.make(cfg);
        w.add(1, ds, 0, Vec3{0, 0, 0});
        w.add(2, dt, 1, Vec3{900, 0, 0});
        w.sys->order_attack(1, 2);
        fired_at = -1;
        Vec3 start;
        for (int i = 0; i < 400; ++i) {
            w.tick();
            if (fired_at < 0 && w.sys->projectile_count() == 1) {
                fired_at = i;
                start = w.sys->projectile(0).pos;
            } else if (fired_at >= 0 && w.sys->projectile(0).pos.x != start.x) {
                return i - fired_at;
            }
        }
        return -1;
    };
    int a = 0, b = 0;
    const int plain = first_move(0, a), held = first_move(20, b);
    CHECK(plain >= 1);
    CHECK(held == plain + 20);
    CHECK(HardpointDesc{}.projectile_appearance_delay_frames == 0);
}

void test_weapon_delay_order() {
    // MCX4_T10_RE_ANSWERS §10 (Fire_At 0x3825b0) [H]: W = [slot-0 entry] x (unit-ability WEAPON_DELAY multiplier) x
    // [slot-1 entry], float32, in that order. Factors 0.1 / 0.3 / 0.7 give different bits for the other two orders.
    const float e0 = 0.1f, m = 0.3f, e1 = 0.7f;
    const float want = (e0 * m) * e1;
    CHECK(want != e0 * (m * e1) && want != (e0 * e1) * m);  // the order is visible in the bits
    World w;
    ProjectileDesc p;
    p.max_speed = 50.0f;
    p.damage = 1.0f;
    w.data.projectiles.push_back(p);
    HardpointDesc hd;
    hd.kind = HardpointKind::WeaponLaser;
    hd.health = 100.0f;
    hd.projectile = 0;
    hd.min_recharge = hd.max_recharge = 1.0f;
    hd.fire_range = 1000.0f;
    hd.weapon_delay = {{kAbilityPowerToWeapons, e0}, {kAbilityDefend, e1}};
    w.data.hardpoints.push_back(hd);
    UnitAbilityDesc ptw;  // slot 0, carries the WEAPON_DELAY multiplier
    ptw.ability_type = kAbilityPowerToWeapons;
    ptw.mult[static_cast<size_t>(ModKind::WeaponDelay) - 1] = m;
    UnitAbilityDesc def;  // slot 1
    def.ability_type = kAbilityDefend;
    w.data.abilities = {ptw, def};
    UnitDesc sh;
    sh.tactical_health = 100.0f;
    sh.hardpoints = {0};
    sh.abilities = {0, 1};
    const uint32_t ds = push_unit(w.data, sh);
    w.make();
    w.add(1, ds, 0, Vec3{0, 0, 0});
    CHECK(w.sys->weapon_delay_factor(1, 0) == 1.0f);
    w.sys->set_ability_active(1, 0, true);
    w.sys->set_ability_active(1, 1, true);
    const float got = w.sys->weapon_delay_factor(1, 0);
    CHECK(got == want);
    w.sys->set_ability_active(1, 1, false);  // slot 1 off: its entry drops out
    CHECK(w.sys->weapon_delay_factor(1, 0) == e0 * m);
}

// ledger: verifies GameConstants.Crouch_Move_Fire_Angle_Cutoff
void test_crouch_move_fire_cone() {
    // MCX4_T11_T12_RE_ANSWERS §B4 (WeaponBehavior 0x54d090): in the crouched-move clip (0x43) with a fire target, the own
    // weapon does not fire while |yaw to the target - facing yaw| > Crouch_Move_Fire_Angle_Cutoff (FoC 20). A moving unit
    // keeps its facing (targeting state 4 without a turn), so the cone is what gates it.
    struct Hooks final : CombatHooks {
        bool crouch = true;
        bool crouch_moving(ObjectId) const override { return crouch; }
    };
    auto shots = [](bool crouch, Vec3 target) {
        Hooks hk;
        hk.crouch = crouch;
        World w;
        ProjectileDesc p;
        p.max_speed = 10.0f;
        p.damage = 1.0f;
        w.data.projectiles.push_back(p);
        UnitDesc s;
        s.tactical_health = 100.0f;
        s.max_attack_distance = 1000.0f;
        s.weapon_projectile = 0;
        s.weapon_recharge = 1.0f;
        UnitDesc t;
        t.tactical_health = 1e6f;
        t.radius = 10.0f;
        t.category_mask = kCatCapital;
        const uint32_t ds = push_unit(w.data, s), dt = push_unit(w.data, t);
        CombatConfig cfg;
        cfg.mode = GameMode::Land;
        cfg.inaccuracy_enabled = false;
        w.make(cfg);
        w.sys->set_hooks(&hk);
        w.add(1, ds, 0, Vec3{0, 0, 0}, Vec3{1, 0, 0});
        w.add(2, dt, 1, target);
        w.sys->set_flag(1, kUnitMoving, true);
        w.sys->order_attack(1, 2);
        for (int i = 0; i < 60; ++i) w.tick();
        w.sys->set_hooks(nullptr);
        return w.sys->stats().shots;
    };
    CHECK(shots(true, Vec3{0, 300, 0}) == 0);  // 90 degrees off: no shot
    CHECK(shots(false, Vec3{0, 300, 0}) > 0);  // not crouch-moving: fires
    CHECK(shots(true, Vec3{300, 52, 0}) > 0);  // about 9.8 degrees off: inside the cone
    CHECK_F(CombatConfig{}.crouch_move_fire_angle_cutoff, 20.0f);
}

namespace {
// mcx4 (MCX4_T10_RE_ANSWERS §10 Round 4): a station (unit 1, player 0) at the origin facing +x with one manual-target
// hardpoint (fire cone 90 x 180, range 2000, minimum 100, 1 s recharge, one pulse, Fire_Category_Restrictions Fighter,
// cooldown 120 s), and a capital (unit 2, player 1) 500 ahead.
struct ManualWorld {
    World w;
    FogHooks fog;
    explicit ManualWorld(bool manual = true, bool opportunity = false, bool fires_undeployed = true) {
        ProjectileDesc p;
        p.max_speed = 50.0f;
        p.damage = 1.0f;
        w.data.projectiles.push_back(p);
        HardpointDesc hd;
        hd.kind = HardpointKind::WeaponSpecial;
        hd.health = 100.0f;
        hd.projectile = 0;
        hd.min_recharge = hd.max_recharge = 1.0f;
        hd.pulse_count = 1;
        hd.fire_range = 2000.0f;
        hd.fire_min_range = 100.0f;
        hd.fire_cone_width = 90.0f;
        hd.fire_cone_height = 180.0f;
        hd.fire_category_restrictions = kCatFighter;
        hd.allow_opportunity_fire_when_targeting = true;
        hd.allow_opportunity_fire_when_idle = true;
        hd.fire_when_undeployed = fires_undeployed;
        hd.manual_target = manual;
        hd.manual_cooldown_secs = 120.0f;
        w.data.hardpoints.push_back(hd);
        UnitDesc st;
        st.tactical_health = 1e6f;
        st.radius = 50.0f;
        st.hardpoints = {0};
        UnitDesc cap;
        cap.tactical_health = 1e7f;
        cap.radius = 10.0f;
        cap.category_mask = kCatCapital;
        UnitDesc fig = cap;
        fig.category_mask = kCatFighter;
        push_unit(w.data, st);   // 0
        push_unit(w.data, cap);  // 1
        push_unit(w.data, fig);  // 2
        CombatConfig cfg;
        cfg.mode = GameMode::Space;
        cfg.fps = 30;
        cfg.inaccuracy_enabled = false;
        cfg.object_max_health_multiplier = 1.0f;
        cfg.hardpoint_health_multiplier = 1.0f;
        w.make(cfg);
        if (opportunity) {  // World::make turns opportunity fire off; rebuild with it on
            cfg.opportunity_fire = true;
            w.sys = std::make_unique<CombatSystem>(w.data, cfg, std::vector<PlayerInfo>{PlayerInfo{}, PlayerInfo{}});
        }
        fog.human0 = false;
        w.sys->set_hooks(&fog);
        w.add(1, 0, 0, Vec3{0, 0, 0});
        w.add(2, 1, 1, Vec3{500, 0, 0});
    }
    ~ManualWorld() { w.sys->set_hooks(nullptr); }
    ManualWorld(const ManualWorld&) = delete;
    ManualWorld& operator=(const ManualWorld&) = delete;
};
}  // namespace

// ledger: verifies HardPoint.Manual_Hardpoint_Firing_Cooldown_Secs
void test_manual_no_fire_without_target() {
    // Tag Manual_Hardpoint_Firing_Cooldown_Secs: no manual shot, so no player stamp.
    // Round 4 point 2 (Update_Weapon 0x387400): a Requires_Manual_Target_Assignment hardpoint returns when it holds no
    // manual target - no shot at the weapon's (ordered) target and no opportunity fire, even with both allowed. The same
    // hardpoint without the flag fires.
    for (const bool manual : {true, false}) {
        ManualWorld m(manual, true);
        m.w.sys->order_attack(1, 2);
        for (int i = 0; i < 120; ++i) m.w.tick();
        if (manual) CHECK(m.w.sys->stats().shots == 0u);
        else CHECK(m.w.sys->stats().shots > 0u);
        CHECK(m.w.sys->manual_fire_cooldown(0).frames == 0);
    }
}

// ledger: verifies HardPoint.Manual_Hardpoint_Firing_Cooldown_Secs
void test_manual_assignment() {
    // Tag Manual_Hardpoint_Firing_Cooldown_Secs: the assignment the cooldown stamp belongs to (no cooldown test in it).
    // Round 4 point 1 / point 3 (the setter 0x3822e0): accepted only for a manual-flag hardpoint without a held target,
    // for an enemy that Fire_Category_Restrictions does not refuse, inside the cone, not hidden (force 1), within range +
    // radius and at least the minimum range; it stores target, player and frame. One pending target at most.
    ManualWorld m;
    ManualWorld plain(false);
    m.w.add(3, 2, 1, Vec3{600, 0, 0});     // fighter: Fire_Category_Restrictions
    m.w.add(4, 1, 1, Vec3{-500, 0, 0});    // behind: outside the 90-degree cone
    m.w.add(5, 1, 1, Vec3{2015, 0, 0});    // 2015 > 2000 + 10
    m.w.add(6, 1, 1, Vec3{2005, 0, 0});    // 2005 <= 2000 + 10: in range
    m.w.add(7, 1, 1, Vec3{90, 0, 0});      // inside Fire_Min_Range_Distance 100
    m.w.add(8, 1, 0, Vec3{400, 0, 0});     // own unit
    m.w.add(9, 1, 1, Vec3{700, 50, 0});    // hidden for the owner (force 1)
    m.fog.hidden = {9};
    m.w.tick();
    CHECK(!plain.w.sys->assign_manual_target(1, 0, 2, 0));  // no flag
    CHECK(!m.w.sys->assign_manual_target(1, 1, 2, 0));      // no such hardpoint
    CHECK(!m.w.sys->assign_manual_target(1, 0, 99, 0));     // no such target
    for (const ObjectId bad : {3, 4, 5, 7, 8, 9}) {
        CHECK(!m.w.sys->assign_manual_target(1, 0, bad, 0));
        CHECK(!m.w.sys->special_weapon_can_fire(1, bad));
    }
    CHECK(m.w.sys->hardpoint_manual_target(1, 0) == kNoObject && m.w.sys->hardpoint_manual_player(1, 0) == -1);
    CHECK(m.w.sys->special_weapon_hardpoint(1) == 0 && m.w.sys->special_weapon_can_fire(1, 6));
    const uint32_t at = m.w.frame - 1;  // the last combat tick
    CHECK(m.w.sys->assign_manual_target(1, 0, 6, 1));
    CHECK(m.w.sys->hardpoint_manual_target(1, 0) == 6 && m.w.sys->hardpoint_manual_player(1, 0) == 1);
    CHECK(m.w.sys->hardpoint_manual_frame(1, 0) == at);
    CHECK(!m.w.sys->assign_manual_target(1, 0, 2, 0));  // one pending target: refused, nothing replaced
    CHECK(!m.w.sys->special_weapon_fire(1, 2, 0));
    CHECK(m.w.sys->hardpoint_manual_target(1, 0) == 6 && m.w.sys->hardpoint_manual_player(1, 0) == 1);
    CHECK(m.w.sys->ordered_target(1) == kNoObject);  // not an attack order
    m.fog.hidden.clear();
    CHECK(m.w.sys->special_weapon_can_fire(1, 9));
}

// ledger: verifies HardPoint.Manual_Hardpoint_Firing_Cooldown_Secs
void test_manual_fire_and_cooldown() {
    // Tag Manual_Hardpoint_Firing_Cooldown_Secs (HardpointDesc::manual_cooldown_secs = 120): the stamp of a manual shot.
    // Round 4 point 3 / §10 (Fire_At 0x3825b0, 0x285d70): with a manual target the hardpoint fires at it once; the full
    // recharge stamps the assigning player's cooldown (frame of the shot, int(30 x 120) = 3600 frames) and clears the
    // player, and the shot releases the manual target. No more shots without a new assignment. Nothing in the sim refuses
    // a new assignment during the cooldown (only the HUD reads it). Saved and restored.
    ManualWorld m;
    for (int i = 0; i < 40; ++i) m.w.tick();  // the drawn first cooldown runs out
    CHECK(m.w.sys->stats().shots == 0u);
    CHECK(m.w.sys->special_weapon_fire(1, 2, 1));  // the station path: the setter with the player
    uint32_t shot_frame = 0;
    for (int i = 0; i < 10 && shot_frame == 0; ++i) {
        const uint32_t f = m.w.frame;
        m.w.tick();
        if (m.w.sys->stats().shots > 0u) shot_frame = f;
    }
    CHECK(shot_frame != 0 && m.w.sys->stats().shots == 1u);
    CHECK(m.w.sys->hardpoint_manual_target(1, 0) == kNoObject && m.w.sys->hardpoint_manual_player(1, 0) == -1);
    const ManualFireCooldown c = m.w.sys->manual_fire_cooldown(1);
    CHECK(c.frame == shot_frame && c.frames == 3600);
    CHECK(m.w.sys->manual_fire_cooldown(0).frames == 0 && m.w.sys->manual_fire_cooldown(7).frames == 0);
    for (int i = 0; i < 90; ++i) m.w.tick();
    CHECK(m.w.sys->stats().shots == 1u);  // one assignment, one shot
    // Save / load round trip keeps the stamp and a held target.
    CHECK(m.w.sys->assign_manual_target(1, 0, 2, 1));  // during the cooldown: accepted
    ByteWriter wa;
    m.w.sys->write(wa);
    ManualWorld b;
    ByteReader r(wa.bytes());
    CHECK(b.w.sys->read(r));
    CHECK(b.w.sys->hardpoint_manual_target(1, 0) == 2 && b.w.sys->hardpoint_manual_player(1, 0) == 1);
    CHECK(b.w.sys->manual_fire_cooldown(1).frame == shot_frame && b.w.sys->manual_fire_cooldown(1).frames == 3600);
    ByteWriter wb;
    b.w.sys->write(wb);
    CHECK(wa.bytes() == wb.bytes());
    StateHasher ha, hb;
    m.w.sys->hash_extended(ha);
    b.w.sys->hash_extended(hb);
    CHECK(ha.digest() == hb.digest());
    for (int i = 0; i < 10; ++i) m.w.tick();
    CHECK(m.w.sys->stats().shots == 2u && m.w.sys->manual_fire_cooldown(1).frame > shot_frame);
}

// ledger: verifies HardPoint.Manual_Hardpoint_Firing_Cooldown_Secs
void test_manual_target_drop() {
    // Tag Manual_Hardpoint_Firing_Cooldown_Secs: a dropped assignment stamps no cooldown.
    // Round 4 point 3: while the weapon-state gate (0x384850) refuses, the manual target is held until the frame is more
    // than 300 past the assignment, then target, player and frame are released. While the gate passes but the shot
    // fails (out of range), it is held with no limit. A removed target is released at once (signal 0x28); the player
    // stays (only +0xc0 is cleared).
    {
        ManualWorld m(true, false, false);  // Fire_When_Undeployed No: the gate refuses
        m.w.tick();
        const uint32_t at = m.w.frame - 1;
        CHECK(m.w.sys->assign_manual_target(1, 0, 2, 1));
        while (m.w.frame <= at + 300u) m.w.tick();  // last update: frame at + 300 (not > 300 past)
        CHECK(m.w.sys->hardpoint_manual_target(1, 0) == 2);
        m.w.tick();  // frame at + 301
        CHECK(m.w.sys->hardpoint_manual_target(1, 0) == kNoObject && m.w.sys->hardpoint_manual_player(1, 0) == -1 &&
              m.w.sys->hardpoint_manual_frame(1, 0) == 0u);
        CHECK(m.w.sys->stats().shots == 0u);
    }
    {
        ManualWorld m;
        m.w.tick();
        CHECK(m.w.sys->assign_manual_target(1, 0, 2, 1));
        m.w.sys->set_position(2, Vec3{5000, 0, 0});  // out of range: the shot fails, the gate passes
        for (int i = 0; i < 400; ++i) m.w.tick();
        CHECK(m.w.sys->hardpoint_manual_target(1, 0) == 2 && m.w.sys->stats().shots == 0u);
        m.w.sys->set_position(2, Vec3{500, 0, 0});  // back in range: it fires
        for (int i = 0; i < 5; ++i) m.w.tick();
        CHECK(m.w.sys->hardpoint_manual_target(1, 0) == kNoObject && m.w.sys->stats().shots == 1u);
    }
    {
        ManualWorld m(true, false, false);
        m.w.tick();
        CHECK(m.w.sys->assign_manual_target(1, 0, 2, 1));
        CHECK(m.w.sys->remove_unit(2));
        m.w.tick();
        CHECK(m.w.sys->hardpoint_manual_target(1, 0) == kNoObject && m.w.sys->hardpoint_manual_player(1, 0) == 1);
    }
}

void test_out_of_range_mod_values() {
    // mcx8 t11 (docs/MOD_COMPAT_DESIGN.md §13.3): mod values whose frame count is out of int32 range convert the x86 way
    // (cvttss2si: INT32_MIN), so the MSVC and clang builds give the same result (both run this test). A mod's 99999999999 s
    // pulse delay, stun or modifier duration: before mcx8 t11 a static_cast here was undefined behaviour.
    CHECK(weapon_pulse_delay(99999999999.0f, 1.0f, 30) == INT32_MIN);
    CHECK(expiry_frame(100u, 99999999999.0f, 30) == 100u + 0x80000000u);
    const Recharge p = hp_pulse_recharge(99999999999.0f, 3, 1.0f, 1.0f, 30);
    CHECK(p.frames == INT32_MIN);
    SimRng r(7u);
    CHECK(hp_first_cooldown(-99999999999.0f, 30, r) <= 0);  // Get_Int(0, INT32_MIN): the draw's own rule, no UB
}

}  // namespace bo3split

