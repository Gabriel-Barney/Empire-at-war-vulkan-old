// TRU1 lane A tests (base shields, Immune_Faction area damage, ground detonation particle; see test_combat_common.hpp).
#include "test_combat_common.hpp"

namespace bo3split {

namespace {
// Three players: 0 throws the bomb (faction A), 1 is its enemy (faction B), 2 is neutral to it (faction C, relation not
// enemy). One 1000-health unit each, every one 50 from the bomb at the origin (blast radius 100, no dropoff: full damage).
struct ImmuneWorld {
    World w{3};
    uint32_t bomb = 0, body = 0;
    explicit ImmuneWorld(uint32_t immune_faction) {
        ProjectileDesc b;
        b.name = "Proj_Construction_Pod_Detonation";
        b.blast_area_damage = 300.0f;
        b.blast_area_range = 100.0f;
        b.blast_immune_faction = immune_faction;
        w.data.projectiles.push_back(b);
        bomb = static_cast<uint32_t>(w.data.projectiles.size() - 1);
        UnitDesc u;
        u.tactical_health = 1000.0f;
        u.radius = 5.0f;
        body = push_unit(w.data, u);
        CombatConfig cfg;
        cfg.mode = GameMode::Land;
        PlayerInfo pa, pb, pc;
        pa.faction = name_crc("Rebel");
        pb.faction = name_crc("Empire");
        pc.faction = name_crc("Neutral");
        w.make(cfg, {pa, pb, pc});
        w.sys->set_enemy(0, 1, true);
        w.sys->set_enemy(1, 0, true);
        w.sys->set_enemy(0, 2, false);
        w.sys->set_enemy(2, 0, false);
        w.add(1, body, 0, Vec3{50, 0, 0});
        w.add(2, body, 1, Vec3{-50, 0, 0});
        w.add(3, body, 2, Vec3{0, 50, 0});
    }
    void detonate() {
        ProjectileLaunch l;
        l.shooter = 1;
        l.owner = 0;
        l.projectile = bomb;
        l.from = Vec3{0, 0, 0};
        l.fuse_seconds = 0.0f;  // sits at `from` and goes off on the next tick (lifetime expiry, area damage)
        CHECK(w.sys->launch_projectile(l, w.frame));
        for (int i = 0; i < 4 && w.sys->projectile_count() > 0; ++i) w.tick();
        CHECK(w.sys->projectile_count() == 0u);
    }
    bool hit(ObjectId id) const { return w.sys->health(id) < w.sys->max_health(id); }
};
}  // namespace

void test_area_immune_faction() {
    // tru1 t04 (TACTICAL_COMBAT §3.5 area damage step 2): without Projectile_Blast_Area_Immune_Faction only enemies of the
    // owner are visited; with it every player is, the owner and neutrals included, except members of the named faction.
    {
        ImmuneWorld a(0);
        a.detonate();
        CHECK(!a.hit(1) && a.hit(2) && !a.hit(3));
    }
    {  // Immune_Faction Rebel (the shooter's own faction): the enemy and the neutral take it, the owner's unit does not
        ImmuneWorld a(name_crc("Rebel"));
        a.detonate();
        CHECK(!a.hit(1) && a.hit(2) && a.hit(3));
        CHECK(a.w.sys->health(2) == a.w.sys->health(3));  // same distance, same damage
    }
    {  // Immune_Faction Underworld (nobody's): all three take it, the bomb's own owner included
        ImmuneWorld a(name_crc("Underworld"));
        a.detonate();
        CHECK(a.hit(1) && a.hit(2) && a.hit(3));
    }
    {  // naming the enemy's faction spares the enemy
        ImmuneWorld a(name_crc("empire"));  // names compare case-insensitively (name_crc upper-cases)
        a.detonate();
        CHECK(a.hit(1) && !a.hit(2) && a.hit(3));
    }
}

namespace {
// tru1 t02: player 0 owns a base-shield generator (radius 500) at the origin; player 1 is its enemy. Unit 1 is the
// generator; others are added per case. Player 0 has base power from object 99 unless `power` is false.
struct ShieldWorld {
    World w;
    uint32_t gen = 0, walker = 0, body = 0, blaster = 0, slug = 0;
    explicit ShieldWorld(bool power = true, bool requires_power = true, bool always_off = false, bool neutral = false) {
        ProjectileDesc b;  // a blaster bolt: Projectile_Does_Shield_Damage
        b.name = "Proj_Blaster";
        b.max_speed = 40.0f;
        b.does_shield_damage = true;
        b.max_flight_distance = 3000.0f;
        w.data.projectiles.push_back(b);
        blaster = 0;
        ProjectileDesc m = b;  // a mass-driver slug: no shield damage
        m.name = "Proj_Mass_Driver";
        m.does_shield_damage = false;
        w.data.projectiles.push_back(m);
        slug = 1;
        UnitDesc g;
        g.name = "E_Ground_Base_Shield";
        g.tactical_health = 1000.0f;
        g.radius = 20.0f;
        g.base_shield = true;
        g.base_shield_radius = 500.0f;
        g.base_shield_requires_power = requires_power;
        g.base_shield_always_off = always_off;
        gen = push_unit(w.data, g);
        UnitDesc a;  // AFFECTED_BY_SHIELD (Dark Trooper)
        a.name = "Dark_Trooper";
        a.tactical_health = 1000.0f;
        a.radius = 10.0f;
        a.affected_by_shield = true;
        walker = push_unit(w.data, a);
        UnitDesc t;
        t.name = "Plain";
        t.tactical_health = 1000.0f;
        t.radius = 10.0f;
        body = push_unit(w.data, t);
        CombatConfig cfg;
        cfg.mode = GameMode::Land;
        cfg.inaccuracy_enabled = false;
        cfg.base_shield_vulnerability = 2.0f;
        PlayerInfo p0, p1;
        p0.active = !neutral;
        w.make(cfg, {p0, p1});
        if (power) w.sys->add_base_power(0, 99);
        w.add(1, gen, 0, Vec3{0, 0, 0});
    }
    void ticks(int n) {
        for (int i = 0; i < n; ++i) w.tick();
    }
    bool on() const { return w.sys->base_shield_active(1); }
    bool in(ObjectId id) const { return (w.sys->flags(id) & kUnitInBaseShield) != 0; }
    // a projectile of player 1 from `from` at unit `target`
    void fire(uint32_t projectile, Vec3 from, ObjectId target) {
        ProjectileLaunch l;
        l.owner = 1;
        l.projectile = projectile;
        l.from = from;
        l.target = target;
        l.damage = 50.0f;
        CHECK(w.sys->launch_projectile(l, w.frame));
    }
    // ticks until the first object hit; true when it struck `victim`, `absorbed` its shield flag
    bool hit_event(ObjectId victim, bool& absorbed) {
        for (int i = 0; i < 60 && w.sys->projectile_count() > 0; ++i) {
            w.tick();
            for (const HitEvent& e : w.sys->hit_events())
                if (e.victim != kNoObject) {
                    absorbed = e.absorbed;
                    return e.victim == victim;
                }
        }
        return false;
    }
};
}  // namespace

void test_base_shield_state() {
    // tru1 t02 (TRU1_A_RE_ANSWERS A1): on iff powered (Shield_Requires_Base_Power, default true), owner not neutral, not
    // Always_Off; the first evaluation has no delay; off -> on waits Base_Shield_Delay_Time after the last off service.
    {
        ShieldWorld s;
        s.ticks(1);
        CHECK(s.on());  // first evaluation at the first tick, no delay
        s.w.sys->remove_base_power(0, 99);
        s.ticks(15);  // the next 15-frame service finds it unpowered
        CHECK(!s.on());
        s.w.sys->add_base_power(0, 99);
        s.ticks(15);  // powered again; the wait counts from the last off service
        CHECK(!s.on());
        int frames = 15;
        while (!s.on() && frames < 400) {
            s.ticks(1);
            ++frames;
        }
        CHECK(s.on());
        CHECK(frames > 285 && frames <= 315);  // 300 frames after the last off service, on the 15-frame cadence
    }
    {
        ShieldWorld s(false);  // no base power and the default requirement: off
        s.ticks(20);
        CHECK(!s.on());
    }
    {
        ShieldWorld s(false, false);  // Shield_Requires_Base_Power no (the mobile shield generator): on without power
        s.ticks(1);
        CHECK(s.on());
    }
    {
        ShieldWorld s(true, true, true);  // Base_Shield_Always_Off
        s.ticks(20);
        CHECK(!s.on());
    }
    {
        ShieldWorld s(true, true, false, true);  // owner faction Is_Neutral
        s.ticks(20);
        CHECK(!s.on());
    }
    {  // a dead generator's shield is off
        ShieldWorld s;
        s.ticks(1);
        DamageRequest r;
        r.victim = 1;
        r.amount = 5000.0f;
        r.kind = kKindDebug;
        s.w.sys->take_damage(r, s.w.frame);
        s.ticks(1);
        CHECK(!s.on());
    }
}

void test_base_shield_straddle() {
    // tru1 t02 (A2 / A3): an AFFECTED_BY_SHIELD unit is "in the shield" only while it straddles the wall of an active
    // hostile shield: x Base_Shield_Vulnerability_Modifier on damage (step 6), speed x Base_Shield_Speed_Modifier, no
    // targeting / firing. Deep inside, outside, own shields, shields that are off and units without the behaviour: none.
    ShieldWorld s;
    s.w.add(2, s.walker, 1, Vec3{500, 0, 0});   // on the wall
    s.w.add(3, s.walker, 1, Vec3{100, 0, 0});   // deep inside
    s.w.add(4, s.walker, 1, Vec3{900, 0, 0});   // outside
    s.w.add(5, s.walker, 0, Vec3{-500, 0, 0});  // the shield's own side, on the wall
    s.w.add(6, s.body, 1, Vec3{0, 500, 0});     // on the wall, no AFFECTED_BY_SHIELD
    s.ticks(1);
    CHECK(s.on());
    CHECK(s.in(2) && !s.in(3) && !s.in(4) && !s.in(5) && !s.in(6));
    CHECK(s.w.sys->base_shield_straddled(2) == 1 && s.w.sys->base_shield_straddled(3) == kNoObject);
    CHECK(s.w.sys->base_shield_speed_factor(2) == 0.5f && s.w.sys->base_shield_speed_factor(3) == 1.0f);
    DamageRequest r;
    r.amount = 100.0f;
    r.victim = 2;
    CHECK_F(s.w.sys->take_damage(r, s.w.frame).after_multipliers, 200.0f);
    r.victim = 3;
    CHECK_F(s.w.sys->take_damage(r, s.w.frame).after_multipliers, 100.0f);
    r.victim = 6;
    CHECK_F(s.w.sys->take_damage(r, s.w.frame).after_multipliers, 100.0f);
    // power off -> the shield goes off at its next service -> the unit is out of the wall state
    s.w.sys->remove_base_power(0, 99);
    s.ticks(16);
    CHECK(!s.on() && !s.in(2));
    r.victim = 2;
    CHECK_F(s.w.sys->take_damage(r, s.w.frame).after_multipliers, 100.0f);
}

void test_base_shield_dome() {
    // tru1 t02 (A4 / A5): an enemy shot with Projectile_Does_Shield_Damage that crosses an active dome stops there and
    // hits the generator with the dome's name: Take_Damage step 15a absorbs everything (absorbed particle, the generator
    // loses nothing) and the target inside is not hit. A shot without shield damage crosses a shield that is not
    // upgraded and hits the target; an upgraded shield stops it too. A shield that is off stops nothing.
    {
        ShieldWorld s;
        s.w.add(2, s.body, 0, Vec3{200, 0, 0});  // inside the dome
        s.ticks(1);
        s.fire(s.blaster, Vec3{900, 0, 0}, 2);
        bool absorbed = false;
        CHECK(s.hit_event(1, absorbed) && absorbed);
        CHECK(s.w.sys->health(1) == s.w.sys->max_health(1) && s.w.sys->health(2) == s.w.sys->max_health(2));
    }
    {
        ShieldWorld s;
        s.w.add(2, s.body, 0, Vec3{200, 0, 0});
        s.ticks(1);
        s.fire(s.slug, Vec3{900, 0, 0}, 2);
        bool absorbed = true;
        CHECK(s.hit_event(2, absorbed) && !absorbed);
        CHECK(s.w.sys->health(2) < s.w.sys->max_health(2));
    }
    {
        ShieldWorld s;
        s.w.add(2, s.body, 0, Vec3{200, 0, 0});
        s.w.sys->add_base_shield_upgrade(0, 77);
        s.ticks(1);
        s.fire(s.slug, Vec3{900, 0, 0}, 2);
        bool absorbed = false;
        CHECK(s.hit_event(1, absorbed) && absorbed);
        CHECK(s.w.sys->health(2) == s.w.sys->max_health(2));
    }
    {
        ShieldWorld s(false);  // unpowered: off
        s.w.add(2, s.body, 0, Vec3{200, 0, 0});
        s.ticks(1);
        s.fire(s.blaster, Vec3{900, 0, 0}, 2);
        bool absorbed = true;
        CHECK(s.hit_event(2, absorbed) && !absorbed);
    }
    {  // step 15a alone: the generator hit with the dome's name loses nothing; with any other name it is damaged
        ShieldWorld s;
        s.ticks(1);
        DamageRequest r;
        r.victim = 1;
        r.amount = 100.0f;
        r.hardpoint = kHardpointBaseShieldDome;
        const DamageResult a = s.w.sys->take_damage(r, s.w.frame);
        CHECK(a.absorbed > 0.0f && a.to_hull == 0.0f && s.w.sys->health(1) == s.w.sys->max_health(1));
        r.hardpoint = -1;
        s.w.sys->take_damage(r, s.w.frame);
        CHECK(s.w.sys->health(1) < s.w.sys->max_health(1));
    }
    {  // save / load keeps the shield state, the upgrade list and the straddle (combat save 16)
        ShieldWorld a;
        a.w.add(2, a.walker, 1, Vec3{500, 0, 0});
        a.w.sys->add_base_shield_upgrade(0, 77);
        a.ticks(3);
        const std::vector<uint8_t> saved = combat_bytes(*a.w.sys);
        ShieldWorld b;
        b.w.data = a.w.data;
        b.w.make(CombatConfig{});
        ByteReader rd(saved);
        CHECK(b.w.sys->read(rd));
        CHECK(combat_bytes(*b.w.sys) == saved);
        CHECK(b.w.sys->base_shield_active(1) && b.w.sys->base_shield_straddled(2) == 1);
    }
}


namespace {
// tru1 t08: a straight no-target shot of player 0 from (0, 0, 1) toward (300, 0, 1) over a plateau at z 5 (x >= 160,
// TACTICAL_COMBAT §3.4 ground test) in land; space has no ground test, so the shot runs out its 200 flight distance.
// Unit 2 (player 1, enemy) sits 30 to the side of the ground hit / expiry area, inside the effect radius 100.
struct DetonationWorld {
    World w;
    explicit DetonationWorld(GameMode mode, ProjectileDesc shot, Vec3 unit_at) {
        w.data.projectiles = {shot};
        UnitDesc t;
        t.tactical_health = 1000.0f;
        t.radius = 5.0f;
        const uint32_t td = push_unit(w.data, t);
        CombatConfig cfg;
        cfg.mode = mode;
        w.make(cfg);
        GroundGrid g = flat_grid(20, 4, 0);
        for (uint32_t y = 0; y < g.height; ++y)
            for (uint32_t x = 8; x < g.width; ++x) g.heights[y * g.width + x] = 80;
        RocketTerrain rt;
        rt.grid = g;
        w.sys->set_rocket_terrain(std::move(rt));
        w.add(2, td, 1, unit_at);
        ProjectileLaunch l;
        l.owner = 0;
        l.from = Vec3{0, 0, 1};
        l.point = Vec3{300, 0, 1};
        CHECK(w.sys->launch_projectile(l, w.frame));
    }
    // ticks until the detonation; its event (victim none), `found` false when none came
    HitEvent run(bool& found) {
        found = false;
        HitEvent out;
        for (int i = 0; i < 100 && !found; ++i) {
            w.tick();
            for (const HitEvent& e : w.sys->hit_events()) {
                found = true;
                out = e;
            }
        }
        return out;
    }
};
}  // namespace

void test_ground_detonation_particle() {
    // tru1 t08 (TRU1_A_RE_ANSWERS B1): a land ground / water hit (0x4cb070) reports `ground` - the host spawns
    // Projectile_Ground_Detonation_Particle; every expiry (distance, lifetime, path end, commanded) does not - the
    // lifetime particle. The ground path runs ion stun but not weaken; the expiry paths run weaken but not ion stun.
    ProjectileDesc shot;
    shot.max_speed = 10.0f;
    shot.damage = 10.0f;
    shot.max_flight_distance = 200.0f;
    shot.weaken = true;
    shot.weaken_radius = 100.0f;
    shot.weaken_take_increase = 0.5f;
    shot.weaken_seconds = 10.0f;
    shot.ion_stun = true;
    shot.ion_stun_radius = 100.0f;
    shot.ion_stun_duration = 5.0f;
    {  // land: the ramp at x 140..160 stops it at x 144 -> ground; ion stun yes, weaken no
        DetonationWorld d(GameMode::Land, shot, Vec3{142, 30, 0});
        bool found = false;
        const HitEvent e = d.run(found);
        // z 1 meets the ramp (z = (x - 140) / 4) at x 144
        CHECK(found && e.victim == kNoObject && e.ground && std::fabs(e.position.x - 144.0f) < 0.01f);
        CHECK(d.w.sys->ion_stunned(2, d.w.frame));
        CHECK_F(d.w.sys->modifier(2, ModList::Defense), 0.0f);
    }
    {  // space: no ground test, the 200 flight distance runs out -> expiry; weaken yes, ion stun no
        DetonationWorld d(GameMode::Space, shot, Vec3{200, 30, 1});
        bool found = false;
        const HitEvent e = d.run(found);
        CHECK(found && e.victim == kNoObject && !e.ground && e.position.x >= 199.0f);
        CHECK(!d.w.sys->ion_stunned(2, d.w.frame));
        CHECK(d.w.sys->modifier(2, ModList::Defense) < 0.0f);
    }
    {  // grenade (0x4ceea0): its ground hit is the same ground path
        GroundGrid g = flat_grid(16, 4, 0);
        LobLaunch ll(GameMode::Land, spmat_grenade(), Vec3{200, 0, 0}, &g);
        bool found = false, ground = false;
        for (int i = 0; i < 100 && !found; ++i) {
            ll.tick();
            for (const HitEvent& e : ll.sys->hit_events()) {
                found = true;
                ground = e.ground && e.victim == kNoObject;
            }
        }
        CHECK(found && ground);
    }
    {  // grenade in space: lifetime expiry, not ground
        ProjectileDesc g = spmat_grenade();
        g.max_lifetime = 2.0f;
        LobLaunch ll(GameMode::Space, g, Vec3{200, 0, 0});
        bool found = false, ground = true;
        for (int i = 0; i < 200 && !found; ++i) {
            ll.tick();
            for (const HitEvent& e : ll.sys->hit_events()) {
                found = true;
                ground = e.ground;
            }
        }
        CHECK(found && !ground);
    }
}

}  // namespace bo3split
