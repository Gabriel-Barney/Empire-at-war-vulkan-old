// trn1 tests: terrain cover (t01) and the SurfaceFX terrain modifiers (t02); docs/re/TRN1_TERRAIN_GAMEPLAY.md.
#include "alamo/data/xml.hpp"
#include "test_combat_common.hpp"

namespace bo3split {

namespace {

// Surface strips along x (the host's query): < 0 cover 10, [0, 100) default, then 100-wide strips of good ground,
// high ground, slow ground, water, mud, damage ground, cover 9, cover 11, lava.
uint32_t strip_surface(float x, float) {
    if (x < 0.0f) return 10;
    static constexpr uint32_t kStrips[] = {0, 12, 13, 14, 4, 6, 15, 9, 11, 8};
    const auto i = static_cast<size_t>(x / 100.0f);
    return i < std::size(kStrips) ? kStrips[i] : 0u;
}

constexpr const char* kSurfaceXml = R"(<?xml version="1.0"?>
<SurfaceEffects>
  <SurfaceFX name="Infantry_Terrain_Modifier">
    <Surface_Settings name="default"></Surface_Settings>
    <Surface_Settings name="good_ground"><Defense_Mod>0.5</Defense_Mod><Damage_Mod>1.0</Damage_Mod></Surface_Settings>
    <Surface_Settings name="high_ground"><FOW_Reveal_Range_Mod>0.25</FOW_Reveal_Range_Mod><Fire_Range_Mod>0.25</Fire_Range_Mod></Surface_Settings>
    <Surface_Settings name="slow_ground"><Speed_Mod>-0.5</Speed_Mod></Surface_Settings>
    <Surface_Settings name="water"><Speed_Mod>-0.5</Speed_Mod></Surface_Settings>
    <Surface_Settings name="mud"><Speed_Mod>-0.5</Speed_Mod></Surface_Settings>
    <Surface_Settings name="damage_ground"><Terrain_Damage>10</Terrain_Damage><Terrain_Damage_Delay>1.0</Terrain_Damage_Delay></Surface_Settings>
    <Surface_Settings name="lava"><Terrain_Damage>10</Terrain_Damage><Terrain_Damage_Delay>1.0</Terrain_Damage_Delay></Surface_Settings>
  </SurfaceFX>
  <SurfaceFX name="High_Ground">
    <Surface_Settings name="high_ground"><FOW_Reveal_Range_Mod>0.25</FOW_Reveal_Range_Mod><Fire_Range_Mod>0.25</Fire_Range_Mod></Surface_Settings>
  </SurfaceFX>
  <SurfaceFX name="Generic_Footprint">
    <Surface_Settings name="water"><Particle_Name>Splash_Wake_Continuous</Particle_Name></Surface_Settings>
  </SurfaceFX>
</SurfaceEffects>
)";

struct SurfaceWorld {
    World w;
    uint32_t infantry = 0, plain = 0, both = 0, five = 0;
    explicit SurfaceWorld(GameMode mode = GameMode::Land) {
        const data::XmlDocument doc = data::XmlDocument::parse(std::string_view(kSurfaceXml), "SurfaceFX.xml");
        CHECK(doc.ok());
        w.data.surface_fx = load_surface_fx(doc.root());
        CHECK(w.data.surface_fx.size() == 3u);
        CHECK(w.data.surface_fx[0].affects_sim() && w.data.surface_fx[1].affects_sim() && !w.data.surface_fx[2].affects_sim());
        UnitDesc u;
        u.tactical_health = 1000.0f;
        u.radius = 2.0f;
        u.cover_damage_shield = 0.5f;
        u.surface_fx = {0};
        infantry = push_unit(w.data, u);
        UnitDesc p;
        p.tactical_health = 1000.0f;
        p.radius = 2.0f;
        plain = push_unit(w.data, p);
        u.surface_fx = {0, 1};  // Infantry_Terrain_Modifier + High_Ground (the one FoC type with both)
        both = push_unit(w.data, u);
        u.surface_fx = {2, 2, 2, 2, 1};  // mcx4 t17: a mod type whose 5th SurfaceFX_Name entry is High_Ground
        five = push_unit(w.data, u);
        CombatConfig cfg;
        cfg.mode = mode;
        w.make(cfg);
        SurfaceQueries q;
        q.cell_surface = strip_surface;
        q.surface = strip_surface;
        w.sys->set_surface_queries(std::move(q));
    }
    float hit(ObjectId id, float amount) {
        DamageRequest r;
        r.victim = id;
        r.amount = amount;
        r.kind = kKindNormal;
        return w.sys->take_damage(r, w.frame).after_multipliers;
    }
};

}  // namespace

void test_terrain_cover() {
    // t01 (TACTICAL_COMBAT §3.6 step 12, Get_Damage_Multiplier 0x38e0c0): a victim standing on a cover cell (surface 9-11)
    // takes x its type's Surface_Type_Cover_Damage_Shield; other surfaces, types without the tag and space battles x 1.
    {
        SurfaceWorld s;
        s.w.add(1, s.infantry, 1, Vec3{-50, 0, 0});  // cover 10
        s.w.add(2, s.infantry, 1, Vec3{750, 0, 0});  // cover 9
        s.w.add(3, s.infantry, 1, Vec3{850, 0, 0});  // cover 11
        s.w.add(4, s.infantry, 1, Vec3{50, 0, 0});   // default
        s.w.add(5, s.plain, 1, Vec3{-60, 0, 0});     // cover, no tag
        s.w.add(6, s.infantry, 1, Vec3{950, 0, 0});  // lava (8): no cover
        CHECK_F(s.hit(1, 100.0f), 50.0f);
        CHECK_F(s.hit(2, 100.0f), 50.0f);
        CHECK_F(s.hit(3, 100.0f), 50.0f);
        CHECK_F(s.hit(4, 100.0f), 100.0f);
        CHECK_F(s.hit(5, 100.0f), 100.0f);
        CHECK_F(s.hit(6, 100.0f), 100.0f);
        // moving onto / off the cover cell changes it at once (the victim's current position)
        s.w.sys->set_position(4, Vec3{-10, 0, 0});
        CHECK_F(s.hit(4, 100.0f), 50.0f);
    }
    {  // space: no terrain test
        SurfaceWorld s(GameMode::Space);
        s.w.add(1, s.infantry, 1, Vec3{-50, 0, 0});
        CHECK_F(s.hit(1, 100.0f), 100.0f);
    }
}

void test_surface_fx_terrain_modifiers() {
    // t02 (SurfaceFX 0x5efe50 / 0x5ee820): the Infantry_Terrain_Modifier terms per surface; the first update only creates
    // the instance.
    SurfaceWorld s;
    s.w.add(1, s.infantry, 1, Vec3{150, 0, 0});  // good ground
    s.w.add(2, s.plain, 1, Vec3{150, 0, 0});     // no SURFACE_FX instance
    const auto mod = [&](ModList l) { return s.w.sys->modifier(1, l); };
    s.w.tick();
    CHECK_F(mod(ModList::Defense), 0.0f);  // first update: instances made, nothing evaluated
    s.w.tick();
    CHECK_F(mod(ModList::Defense), 0.5f);
    CHECK_F(mod(ModList::Damage), 1.0f);
    CHECK_F(s.w.sys->modifier(2, ModList::Defense), 0.0f);
    CHECK_F(s.hit(1, 100.0f), 50.0f);  // (1 - 0.5): the good ground defense applies through step 12
    s.w.sys->set_position(1, Vec3{250, 0, 0});  // high ground
    s.w.tick();
    CHECK_F(mod(ModList::Defense), 0.0f);
    CHECK_F(mod(ModList::Damage), 0.0f);
    CHECK_F(mod(ModList::RevealRange), 0.25f);
    CHECK_F(mod(ModList::FireRange), 0.25f);
    s.w.sys->set_position(1, Vec3{350, 0, 0});  // slow ground
    s.w.tick();
    CHECK_F(mod(ModList::RevealRange), 0.0f);
    CHECK_F(mod(ModList::MaxSpeed), -0.5f);
    s.w.sys->set_position(1, Vec3{450, 0, 0});  // water straight from slow ground: the slow entry goes, none is added
    s.w.tick();
    CHECK_F(mod(ModList::MaxSpeed), 0.0f);
    s.w.tick();
    CHECK_F(mod(ModList::MaxSpeed), 0.0f);  // same surface: nothing changes
    s.w.sys->set_position(1, Vec3{50, 0, 0});  // default, then water from there: the water slow applies
    s.w.tick();
    s.w.sys->set_position(1, Vec3{450, 0, 0});
    s.w.tick();
    CHECK_F(mod(ModList::MaxSpeed), -0.5f);
    s.w.sys->set_position(1, Vec3{550, 0, 0});  // mud: its Speed_Mod is never applied
    s.w.tick();
    CHECK_F(mod(ModList::MaxSpeed), 0.0f);
    // damage ground: 10 every 30 frames from 30 frames after entering
    s.w.sys->set_position(1, Vec3{650, 0, 0});
    const float h0 = s.w.sys->health(1);
    const uint32_t entered = s.w.frame;
    s.w.tick();
    while (s.w.frame < entered + 30) s.w.tick();
    CHECK_F(s.w.sys->health(1), h0);
    s.w.tick();  // frame entered + 30
    CHECK_F(s.w.sys->health(1), h0 - 10.0f);
    while (s.w.frame < entered + 60) s.w.tick();
    CHECK_F(s.w.sys->health(1), h0 - 10.0f);
    s.w.tick();
    CHECK_F(s.w.sys->health(1), h0 - 20.0f);
    s.w.sys->set_position(1, Vec3{50, 0, 0});  // leaving to default stops it
    for (int i = 0; i < 40; ++i) s.w.tick();
    CHECK_F(s.w.sys->health(1), h0 - 20.0f);
    CHECK_F(s.w.sys->health(2), s.w.sys->max_health(2));
}

void test_surface_fx_two_instances() {
    // t02 (TRN1 §2.2): two instances on one unit put their high-ground entries in the same category 0, so they stack by the
    // max (0.25, not 0.5); leaving takes each instance's own entries off.
    SurfaceWorld s;
    s.w.add(1, s.both, 1, Vec3{250, 0, 0});
    s.w.tick();
    s.w.tick();
    CHECK_F(s.w.sys->modifier(1, ModList::RevealRange), 0.25f);
    CHECK_F(s.w.sys->modifier(1, ModList::FireRange), 0.25f);
    CHECK(s.w.sys->unit_modifiers(1) && s.w.sys->unit_modifiers(1)->entries().size() == 4u);
    s.w.sys->set_position(1, Vec3{50, 0, 0});
    s.w.tick();
    CHECK(s.w.sys->unit_modifiers(1)->entries().empty());
}

void test_surface_fx_fifth_instance() {
    // mcx4 t17 (TRN1_TERRAIN_GAMEPLAY §2): retail builds one SURFACE_FX instance per SurfaceFX_Name entry, so a 5th entry
    // works like the first four (the port capped the list at 4); its surface memory survives save / load.
    SurfaceWorld s;
    s.w.add(1, s.five, 1, Vec3{250, 0, 0});
    s.w.tick();
    s.w.tick();
    CHECK_F(s.w.sys->modifier(1, ModList::FireRange), 0.25f);
    ByteWriter bw;
    s.w.sys->write(bw);
    SurfaceWorld t;
    t.w.add(1, t.five, 1, Vec3{250, 0, 0});
    ByteReader br(bw.bytes());
    CHECK(t.w.sys->read(br));
    ByteWriter bw2;
    t.w.sys->write(bw2);
    CHECK(bw.bytes() == bw2.bytes());
    s.w.sys->set_position(1, Vec3{50, 0, 0});
    s.w.tick();
    CHECK_F(s.w.sys->modifier(1, ModList::FireRange), 0.0f);  // the 5th instance takes its entry off again
}

void test_surface_fx_converted_and_save() {
    // t02: a converted unit (obj +0x3a2 bit 4) gets no good / high ground entries; the instance state survives save / load.
    SurfaceWorld s;
    s.w.add(1, s.infantry, 1, Vec3{150, 0, 0});
    s.w.add(2, s.infantry, 1, Vec3{650, 0, 0});
    s.w.sys->convert_unit(1, 0);
    s.w.tick();
    s.w.tick();
    CHECK_F(s.w.sys->modifier(1, ModList::Defense), 0.0f);
    for (int i = 0; i < 10; ++i) s.w.tick();
    ByteWriter bw;
    s.w.sys->write(bw);
    SurfaceWorld t;
    t.w.add(1, t.infantry, 1, Vec3{150, 0, 0});
    t.w.add(2, t.infantry, 1, Vec3{650, 0, 0});
    ByteReader br(bw.bytes());
    CHECK(t.w.sys->read(br));
    t.w.frame = s.w.frame;
    t.w.rng = s.w.rng;
    ByteWriter bw2;
    t.w.sys->write(bw2);
    CHECK(bw.bytes() == bw2.bytes());
    for (int i = 0; i < 40; ++i) {
        s.w.tick();
        t.w.tick();
    }
    CHECK_F(s.w.sys->health(2), t.w.sys->health(2));
    CHECK(s.w.sys->health(2) < s.w.sys->max_health(2));
}

void test_burning_speed_factor() {
    // t05 (MOVEMENT §3.4 item 9, 0x376990): while burning (a kind-7 hit on a combustible unit, obj +0x3a1 bit 2) the speed
    // chain takes On_Fire_Speed_Modifier; not burning, or the tag unset: 1.
    World w;
    UnitDesc u;
    u.tactical_health = 100.0f;
    u.radius = 2.0f;
    u.combustible = true;
    u.on_fire_speed = 1.2f;
    const uint32_t hot = push_unit(w.data, u);
    u.on_fire_speed = 1.0f;
    const uint32_t plain = push_unit(w.data, u);
    CombatConfig cfg;
    cfg.mode = GameMode::Land;
    w.make(cfg);
    w.add(1, hot, 1, Vec3{0, 0, 0});
    w.add(2, plain, 1, Vec3{50, 0, 0});
    CHECK_F(w.sys->burn_speed_factor(1), 1.0f);
    for (const ObjectId id : {ObjectId{1}, ObjectId{2}}) {
        DamageRequest r;
        r.victim = id;
        r.amount = 5.0f;
        r.kind = kKindFire;
        (void)w.sys->take_damage(r, w.frame);
    }
    CHECK(w.sys->burning(1) && w.sys->burning(2));
    CHECK_F(w.sys->burn_speed_factor(1), 1.2f);
    CHECK_F(w.sys->burn_speed_factor(2), 1.0f);
    CHECK_F(w.sys->burn_speed_factor(99), 1.0f);
}

}  // namespace bo3split
