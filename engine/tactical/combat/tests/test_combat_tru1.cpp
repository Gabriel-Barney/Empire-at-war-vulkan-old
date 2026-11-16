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

}  // namespace bo3split
