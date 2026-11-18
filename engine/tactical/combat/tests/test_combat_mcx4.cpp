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

}  // namespace bo3split

