// TRU1 t05 / t06 / t07 (docs/re/TRU1_C_RE.md): indigenous spawners, the space retreat jump-out flight and hardpoint
// breakoff debris, on a synthetic campaign (no game data needed).
//   * retreat: the Empire retreats from a space battle; at the countdown end its three frigates line up at the Attacker
//     Entry Position marker, creep 8.125 per frame along the flight direction (opposite the marker's facing) for 29
//     frames, then one is released every Space_Retreat_Unit_Increment_Wait_Frames (3) and flies at
//     Space_Retreat_Flight_Move_Increment (500); the retreat completes at cinematic frame 181 (retreat_complete then).
//   * debris: a DEBRIS prop moves Debris_Movement_Vector per frame, turns by Debris_Facing_Rotate_Vector, carries its
//     attached particle and dies at its lifetime (Death_Explosions, the particle removed).
//   * spawners: two map huts of one type with Chance 0.5 -> exactly one is active; it keeps Quantity units alive and
//     refills a killed one at the first service after Delay seconds; a pack hut makes its pack once and refills it only
//     when the whole pack is gone.
// Each run is repeated at 4 workers: identical CRC logs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "alamo/core/bytes.hpp"
#include "alamo/core/file_provider.hpp"
#include "alamo/core/hash.hpp"
#include "alamo/core/host_fs.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/data/game_database.hpp"
#include "alamo/galactic/campaign.hpp"
#include "alamo/galactic/conflicts.hpp"
#include "alamo/galactic/galactic_rules.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/objects/object_types.hpp"
#include "alamo/sim/campaign_battles.hpp"
#include "alamo/sim/sim_host.hpp"
#include "alamo/test/support.hpp"

using namespace alamo;
using namespace alamo::sim;
namespace fs = std::filesystem;

using ::alamo::test::g_fail;

namespace {

using ::alamo::test::write_file;

// Battle state save / load (state version 14: retreat flight, spawners, debris) in place: the state hash must survive.
bool state_round_trip(TacticalBattle& b) {
    StateHasher h0;
    b.hash(h0);
    ByteWriter w;
    b.write_part(TacticalBattle::SavePart::State, w);
    ByteReader r(w.bytes());
    if (!b.read_part(TacticalBattle::SavePart::State, r)) return false;
    StateHasher h1;
    b.hash(h1);
    return h0.digest() == h1.digest();
}

const char* kTypes = R"(<T>
 <Planet Name="S10_A"><Behavior>PLANET</Behavior><Terrain>Temperate</Terrain><Planet_Credit_Value>10</Planet_Credit_Value></Planet>
 <Planet Name="S10_B"><Behavior>PLANET</Behavior><Terrain>Temperate</Terrain><Planet_Credit_Value>10</Planet_Credit_Value>
  <Space_Tactical_Map>T_Space_B</Space_Tactical_Map><Land_Tactical_Map>T_Land_B</Land_Tactical_Map></Planet>
 <Planet Name="S10_C"><Behavior>PLANET</Behavior><Terrain>Desert</Terrain><Planet_Credit_Value>10</Planet_Credit_Value></Planet>
 <SpaceUnit Name="S10_Frigate"><Victory_Relevant>Yes</Victory_Relevant>
  <Tactical_Health>3000</Tactical_Health><Max_Speed>3.0</Max_Speed><Max_Rate_Of_Turn>2.0</Max_Rate_Of_Turn>
  <Hyperspace>Yes</Hyperspace><Hyperspace_Speed>1.5</Hyperspace_Speed><AI_Combat_Power>1000</AI_Combat_Power>
  <CategoryMask>Frigate</CategoryMask><Population_Value>1</Population_Value>
  <Behavior>SELECTABLE</Behavior><Collidable_By_Projectile_Living>Yes</Collidable_By_Projectile_Living><SpaceBehavior>SIMPLE_SPACE_LOCOMOTOR</SpaceBehavior>
  <Custom_Soft_Footprint_Radius>60</Custom_Soft_Footprint_Radius>
 </SpaceUnit>
 <SpaceUnit Name="T_Cruiser"><Variant_Of_Existing_Type>S10_Frigate</Variant_Of_Existing_Type><Tactical_Health>9000</Tactical_Health></SpaceUnit>
 <SpaceProp Name="T_Fire"><Behavior>PARTICLE</Behavior></SpaceProp>
 <SpaceProp Name="T_Boom"><Behavior>PARTICLE</Behavior></SpaceProp>
 <SpaceProp Name="T_Debris"><Tactical_Health>100</Tactical_Health><SpaceBehavior>DEBRIS</SpaceBehavior>
  <Debris_Movement_Vector>1.0, 2.0, -0.5</Debris_Movement_Vector><Debris_Facing_Rotate_Vector>0.0, 0.0, 3.0</Debris_Facing_Rotate_Vector>
  <Debris_Min_Lifetime_Seconds>2.0</Debris_Min_Lifetime_Seconds><Debris_Max_Lifetime_Seconds>2.0</Debris_Max_Lifetime_Seconds>
  <Debris_Attached_Particle>T_Fire</Debris_Attached_Particle><Death_Explosions>T_Boom</Death_Explosions><Remove_Upon_Death>true</Remove_Upon_Death>
 </SpaceProp>
 <GroundInfantry Name="S10_Trooper"><Victory_Relevant>Yes</Victory_Relevant><Tactical_Health>2000</Tactical_Health><Max_Speed>1.0</Max_Speed>
  <AI_Combat_Power>50</AI_Combat_Power><CategoryMask>Infantry</CategoryMask><MovementClass>Infantry</MovementClass>
  <Behavior>SELECTABLE</Behavior><Collidable_By_Projectile_Living>Yes</Collidable_By_Projectile_Living><LandBehavior>WALK_LOCOMOTOR</LandBehavior><Custom_Soft_Footprint_Radius>6</Custom_Soft_Footprint_Radius>
 </GroundInfantry>
 <GroundCompany Name="S10_Squad"><Company_Units>S10_Trooper, S10_Trooper</Company_Units>
  <Hyperspace>Yes</Hyperspace><Hyperspace_Speed>1.5</Hyperspace_Speed><Population_Value>1</Population_Value></GroundCompany>
 <GroundInfantry Name="T_Ewok"><Tactical_Health>40</Tactical_Health><Max_Speed>1.0</Max_Speed><CategoryMask>Infantry</CategoryMask>
  <MovementClass>Infantry</MovementClass><Behavior>SELECTABLE</Behavior><LandBehavior>WALK_LOCOMOTOR, INDIGENOUS_UNIT</LandBehavior>
  <Custom_Hard_XExtent>4</Custom_Hard_XExtent><Custom_Hard_YExtent>4</Custom_Hard_YExtent></GroundInfantry>
 <GroundInfantry Name="T_Ewok_B"><Variant_Of_Existing_Type>T_Ewok</Variant_Of_Existing_Type></GroundInfantry>
 <GroundCompany Name="T_Ewok_Pack"><Company_Units>T_Ewok, T_Ewok, T_Ewok</Company_Units><Create_Team_Type>T_Ewok_Team</Create_Team_Type></GroundCompany>
 <Team Name="T_Ewok_Team"><Behavior>TEAM</Behavior></Team>
 <GroundStructure Name="T_Hut"><Tactical_Health>500</Tactical_Health><LandBehavior>SPAWN_INDIGENOUS_UNITS</LandBehavior>
  <Spawned_Indigenous_Units_Type>T_Ewok, T_Ewok_B</Spawned_Indigenous_Units_Type><Spawned_Indigenous_Units_Delay_Seconds>2.0</Spawned_Indigenous_Units_Delay_Seconds>
  <Spawned_Indigenous_Units_Quantity>3</Spawned_Indigenous_Units_Quantity><Spawn_Indigenous_Units_In_Packs>No</Spawn_Indigenous_Units_In_Packs>
  <Spawn_Indigenous_Units_Radius>90.0f</Spawn_Indigenous_Units_Radius><Spawn_Indigenous_Units_Chance>0.5f</Spawn_Indigenous_Units_Chance>
  <Max_Distance_From_Spawner>150</Max_Distance_From_Spawner></GroundStructure>
 <GroundStructure Name="T_Pack_Hut"><Variant_Of_Existing_Type>T_Hut</Variant_Of_Existing_Type>
  <Spawned_Indigenous_Units_Type>T_Ewok</Spawned_Indigenous_Units_Type><Spawn_Indigenous_Units_In_Packs>Yes</Spawn_Indigenous_Units_In_Packs>
  <Spawned_Indigenous_Pack_Type>T_Ewok_Pack</Spawned_Indigenous_Pack_Type><Spawn_Indigenous_Units_Chance>100.0</Spawn_Indigenous_Units_Chance></GroundStructure>
</T>
)";

const char* kFactions = R"(<Factions>
 <Faction Name="Empire"><Is_Playable>Yes</Is_Playable><Can_Control_Planets>Yes</Can_Control_Planets><Space_Tactical_Unit_Cap>20</Space_Tactical_Unit_Cap>
  <Space_Retreat_Unit_Increment_Wait_Frames>3</Space_Retreat_Unit_Increment_Wait_Frames><Space_Retreat_Flight_Move_Increment>500.0</Space_Retreat_Flight_Move_Increment></Faction>
 <Faction Name="Underworld"><Is_Playable>Yes</Is_Playable><Can_Control_Planets>Yes</Can_Control_Planets><Space_Tactical_Unit_Cap>20</Space_Tactical_Unit_Cap></Faction>
 <Faction Name="Neutral"><Is_Neutral>Yes</Is_Neutral></Faction>
 <Faction Name="Hostile"><Is_Playable>No</Is_Playable></Faction>
</Factions>
)";

struct Data {
    fs::path root;
    std::unique_ptr<DirectoryProvider> files;
    data::GameDatabase db;
    objects::ObjectTypeDatabase raw;
    objects::GameObjectTypes types;
    galactic::GalacticRules rules;
    galactic::ConflictRules crules;
    std::unique_ptr<BattleCatalog> catalog;
};

std::unique_ptr<Data> make_data() {
    auto d = std::make_unique<Data>();
    d->root = fs::temp_directory_path() / "alamo_sim_tru1_tests";
    std::error_code ec;
    fs::remove_all(d->root, ec);
    const fs::path x = d->root / "Data/XML";
    write_file(x / "GameObjectFiles.xml", "<GameObjectFiles><File>TTypes.xml</File></GameObjectFiles>\n");
    write_file(x / "TTypes.xml", kTypes);
    write_file(x / "HardPointDataFiles.xml", "<Hard_Point_Files></Hard_Point_Files>\n");
    write_file(x / "FactionFiles.xml", "<FactionFiles><File>TFactions.xml</File></FactionFiles>\n");
    write_file(x / "TFactions.xml", kFactions);
    write_file(x / "TradeRouteFiles.xml", "<TradeRouteFiles></TradeRouteFiles>\n");
    write_file(x / "CampaignFiles.xml", "<CampaignFiles></CampaignFiles>\n");
    write_file(x / "GameConstants.xml",
               "<GameConstants>\n <Space_Retreat_Allowed_Countdown_Seconds>1.0</Space_Retreat_Allowed_Countdown_Seconds>\n"
               " <In_Game_Cinematics>True</In_Game_Cinematics>\n</GameConstants>\n");
    write_file(d->root / "Data/Scripts/Library/PGAICommands.lua",
               "function Set_Contrast_Values()\n\tEnemyContrastTypes[1] = \"Frigate\"\n\tFriendlyContrastTypeNames = {\"Frigate\"}\n"
               "\tFriendlyContrastWeights =\t{1.0}\n\tFriendlyContrastTypes[1].Parse(FriendlyContrastTypeNames, FriendlyContrastWeights)\nend\n");
    d->files = std::make_unique<DirectoryProvider>(path_to_utf8(d->root));
    std::vector<std::string> diag;
    CHECK(d->db.load(*d->files, {}, &diag));
    CHECK(d->raw.load(*d->files));
    CHECK(d->types.build(d->raw, d->files.get(), &diag));
    CHECK(galactic::load_galactic_rules(*d->files, d->db, d->types, d->rules, &diag));
    CHECK(galactic::load_conflict_rules(*d->files, d->db, d->types, d->rules, d->crules, &diag));
    d->catalog = std::make_unique<BattleCatalog>(d->types, d->files.get(), &d->db.constants(), &diag);
    return d;
}

uint32_t type_of(const Data& d, const char* name) { return static_cast<uint32_t>(d.types.resolve_name(name)); }

galactic::GalacticSetup make_setup(const Data& d) {
    galactic::GalacticSetup s;
    GalaxyMap& m = s.map;
    m.campaign_name = "TRU1";
    const char* names[] = {"S10_A", "S10_B", "S10_C"};
    for (uint32_t i = 0; i < 3; ++i) {
        m.planets.add(names[i]);
        m.planets.position[i] = {static_cast<float>(i) * 60.0f, 0.0f, 0.0f};
        m.planets.flags[i] = static_cast<uint32_t>(kPlanetHasPlanetBehavior);
    }
    auto route = [&m](const char* n, PlanetIndex a, PlanetIndex b) {
        const uint32_t r = m.routes.add(n);
        m.routes.point_a[r] = a;
        m.routes.point_b[r] = b;
        m.routes.hs_speed_factor[r] = 5.0f;
    };
    route("A_B", 0, 1);
    route("B_C", 1, 2);
    s.planet_type = {type_of(d, "S10_A"), type_of(d, "S10_B"), type_of(d, "S10_C")};
    galactic::PlayerSetup e;
    e.faction = d.rules.find_faction("Empire");
    e.human = true;
    galactic::PlayerSetup u;
    u.faction = d.rules.find_faction("Underworld");
    u.ai = true;
    galactic::PlayerSetup n;
    n.faction = d.rules.find_faction("Neutral");
    galactic::PlayerSetup h;
    h.faction = d.rules.find_faction("Hostile");
    s.players = {e, u, n, h};
    return s;
}

constexpr float kMarkerX = 1000.0f, kMarkerY = -2000.0f;

terrain::MapData space_map(bool with_marker) {
    terrain::MapData m;
    m.space.present = true;
    for (int i = 0; i < 3; ++i) {
        m.space.playable_min[i] = -6000.0f;
        m.space.playable_max[i] = 6000.0f;
    }
    m.playable.present = m.playable.has_outer_box = true;
    for (int i = 0; i < 3; ++i) {
        m.playable.outer_min[i] = -6000.0f;
        m.playable.outer_max[i] = 6000.0f;
    }
    if (with_marker) {  // yaw 0: facing (0, -1, 0) (map placement convention) -> flight direction (0, 1, 0)
        terrain::ObjectPlacement mk;
        mk.type_crc = name_crc("Attacker Entry Position");
        mk.owner = 1;
        mk.position[0] = kMarkerX;
        mk.position[1] = kMarkerY;
        m.objects.push_back(mk);
    }
    return m;
}

terrain::MapData land_map(const Data& d) {
    terrain::MapData m;
    m.terrain.present = true;
    m.terrain.width = m.terrain.height = 63;
    m.terrain.heights.assign(63u * 63u, 0);
    m.terrain.material.assign(63u * 63u, 0);
    const uint32_t hostile = d.rules.find_faction("Hostile");
    auto hut = [&](const char* type, float x, float y) {
        terrain::ObjectPlacement pl;
        pl.type_crc = name_crc(type);
        pl.owner = hostile;
        pl.position[0] = x;
        pl.position[1] = y;
        m.objects.push_back(pl);
    };
    hut("T_Hut", 400.0f, 400.0f);
    hut("T_Hut", 800.0f, 400.0f);
    hut("T_Pack_Hut", 600.0f, 900.0f);
    return m;
}

// ---- t06 + t07: space ----
struct SpaceRun {
    CrcLog crc;
    bool fought = false, requested = false, flight = false, completed = false;
    size_t listed = 0;
    bool lined_up = true, creep_ok = true, release_ok = true, dir_ok = true, complete_frame_ok = false, cruiser_on_marker = false;
    uint32_t flight_start = 0, complete_at = 0;
    std::vector<int32_t> release_counter;  // cinematic counter at which list[i] first moved 500
    // debris
    bool debris_tracked = false, debris_moved = true, debris_turned = false, debris_expired = false, particle_followed = true;
    bool particle_gone = false, boom_seen = false, saved = false;
    uint32_t debris_spawn_frame = 0, debris_gone_frame = 0;
    // trn1 t06: the pursuers' movement speed factor before the request and during the countdown, the retreating side's
    int32_t pursue_checks = 0;
    bool pursue_ok = true;
    int64_t debris_life = -1;
};

SpaceRun run_space(const Data& d, int32_t workers) {
    SpaceRun out;
    jobs::JobSystem js(jobs::JobSystemConfig{workers});
    SimConfig cfg;
    cfg.seed = 5;
    cfg.players = 4;
    SimHost host(d.types, js, cfg);
    galactic::ConflictRules crules = d.crules;
    const uint32_t empire = d.rules.find_faction("Empire");
    if (crules.factions.size() <= empire) crules.factions.resize(empire + 1);
    crules.factions[empire].space_retreat_countdown = 1.0f;
    // trn1 t06 (MOVEMENT §3.4 item 6): every other faction pursues at 3; the retreating Empire's own factor never applies
    for (galactic::ConflictFactionInfo& fi : crules.factions) fi.space_retreat_pursue = 3.0f;
    crules.factions[empire].space_retreat_pursue = 7.0f;
    galactic::GalacticSetup s = make_setup(d);
    const uint32_t fr = type_of(d, "S10_Frigate"), cr = type_of(d, "T_Cruiser");
    s.forces = {{0, 1, fr}, {0, 1, cr}, {0, 1, fr}, {1, 2, fr}, {1, 2, fr}};
    galactic::GalacticSim* gs = host.start_galactic(d.rules, std::move(s));
    CHECK(gs != nullptr);
    if (!gs) return out;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 3000;
    opt.battle.order_interval = 0;
    opt.battle.fog = false;
    opt.retreat_increment_frames = load_retreat_increments(d.rules);
    opt.retreat_flight_increments = load_retreat_flight_increments(d.rules);
    CampaignBattles battles(host, crules, *d.catalog,
                            [](const std::string& name, terrain::MapData& m) {
                                if (name != "T_Space_B") return false;
                                m = space_map(true);
                                return true;
                            },
                            opt);
    CHECK(battles.attach());
    const std::vector<uint32_t> uf = gs->fleets_at(2, 1);
    CHECK(!uf.empty());
    if (!uf.empty()) CHECK(gs->move_fleet(1, uf[0], 1) == commands::RejectReason::None);
    std::vector<Vec3> last;  // listed units' positions last frame
    objects::ObjectId debris = objects::kNoObjectId, fire = objects::kNoObjectId;
    Vec3 debris_last{};
    Vec3 debris_face0{};
    const uint32_t boom = type_of(d, "T_Boom"), fire_t = type_of(d, "T_Fire");
    for (uint64_t t = 0; t < 4000 && !(out.completed && (out.debris_expired || t > 3000)); ++t) {
        host.tick_once();
        TacticalBattle* b = battles.battle();
        TacticalMode* tm = host.tactical();
        if (!b || !tm) continue;
        out.fought = true;
        objects::ObjectStore& st = tm->primary();
        const uint32_t f = b->frame();
        if (f == 2) {  // the debris piece
            objects::SpawnParams sp;
            sp.type = static_cast<objects::TypeIndex>(type_of(d, "T_Debris"));
            sp.owner = 1;
            sp.position = Vec3{-3000.0f, 3000.0f, 0.0f};
            sp.facing = Vec3{1.0f, 0.0f, 0.0f};
            const objects::ObjectHandle h = tm->spawn(sp);
            debris = st.id(h);
            out.debris_spawn_frame = f;
        }
        if (debris != objects::kNoObjectId && !out.debris_expired) {
            const objects::ObjectHandle h = st.find(debris);
            if (st.alive(h)) {
                const int64_t e = b->debris_expiry(debris);
                if (e >= 0 && !out.debris_tracked) {  // registered at this tick's spawner step
                    out.debris_tracked = true;
                    out.debris_life = static_cast<int64_t>(f);  // the registration frame (life = death frame - this)
                    debris_face0 = st.facing(h);
                    for (const objects::ObjectHandle o : st.all_objects())
                        if (st.alive(o) && static_cast<uint32_t>(st.type(o)) == fire_t) fire = st.id(o);
                } else if (out.debris_tracked) {
                    const Vec3 p = st.position(h);
                    if (std::fabs(p.x - debris_last.x - 1.0f) > 1e-3f || std::fabs(p.y - debris_last.y - 2.0f) > 1e-3f ||
                        std::fabs(p.z - debris_last.z + 0.5f) > 1e-3f)
                        out.debris_moved = false;
                    const Vec3 fc = st.facing(h);
                    out.debris_turned = out.debris_turned || std::fabs(fc.y - debris_face0.y) > 0.01f;
                    const objects::ObjectHandle fh = st.find(fire);
                    if (!st.alive(fh) || st.position(fh).x != p.x || st.position(fh).y != p.y) out.particle_followed = false;
                }
                debris_last = st.position(h);
            } else if (out.debris_tracked) {
                out.debris_expired = true;
                out.debris_gone_frame = f;
                out.debris_life = static_cast<int64_t>(f) - out.debris_life;
                out.particle_gone = !st.alive(st.find(fire));
                for (const objects::ObjectHandle o : st.all_objects())
                    out.boom_seen = out.boom_seen || (st.exists(o) && static_cast<uint32_t>(st.type(o)) == boom);
            }
        }
        // trn1 t06: before the request every unit's factor is its own (1 here); during the countdown (the retreat runs, no
        // flight yet) the pursuers' (player 1) are x 3 and the retreating player's stay
        if (b->retreat_flight_state() == 0 && !out.flight)
            for (const objects::ObjectHandle o : st.all_objects()) {
                if (!st.alive(o) || !b->movement().has_unit(st.id(o))) continue;
                const float fac = b->movement().speed_factor(st.id(o));
                const bool running = b->retreating_player() >= 0;
                const float want = running && st.owner(o) == 1 ? 3.0f : 1.0f;
                if (running) ++out.pursue_checks;
                if (std::fabs(fac - want) > 1e-6f) out.pursue_ok = false;
            }
        if (!out.requested && b->retreat_block(0) == 0) out.requested = b->request_retreat(0);
        // mid-flight save / load (units released and creeping, the debris piece alive)
        if (b->retreat_flight_state() == 2 && b->retreat_flight_counter() == 32) out.saved = state_round_trip(*b);
        if (b->retreat_flight_state() != 0) {
            const std::span<const objects::ObjectId> list = b->retreat_flight_list();
            const int32_t c = b->retreat_flight_counter();
            if (!out.flight) {
                out.flight = true;
                out.flight_start = f;
                out.listed = list.size();
                out.release_counter.assign(list.size(), -1);
                for (const objects::ObjectId id : list) {
                    const Vec3 p = st.position(st.find(id));
                    const float dx = p.x - kMarkerX, dy = p.y - kMarkerY;
                    if (std::sqrt(dx * dx + dy * dy) > 2500.0f) out.lined_up = false;
                    const Vec3 fc = st.facing(st.find(id));
                    if (std::fabs(fc.x) > 1e-4f || std::fabs(fc.y - 1.0f) > 1e-4f) out.dir_ok = false;
                    // the cruiser (largest Tactical_Health) is placed first: on the marker's own spot
                    if (static_cast<uint32_t>(st.type(st.find(id))) == type_of(d, "T_Cruiser"))
                        out.cruiser_on_marker = p.x == kMarkerX && p.y == kMarkerY;
                }
            } else {
                for (size_t i = 0; i < list.size(); ++i) {
                    const Vec3 p = st.position(st.find(list[i]));
                    const float dx = p.x - last[i].x, dy = p.y - last[i].y;
                    if (std::fabs(dx) > 1e-3f) out.dir_ok = false;
                    const bool fast = std::fabs(dy - 500.0f) < 1e-2f, slow = std::fabs(dy - 8.125f) < 1e-3f;
                    if (c < 30 && !slow) out.creep_ok = false;
                    if (b->retreat_flight_state() == 2 && c >= 30 && !(fast || slow)) out.release_ok = false;
                    if (fast && out.release_counter[i] < 0) out.release_counter[i] = c;
                }
            }
            last.clear();
            for (const objects::ObjectId id : list) last.push_back(st.position(st.find(id)));
        }
        if (out.flight && !out.completed && b->retreat_flight_state() == 0) {
            out.completed = true;
            out.complete_at = f;
            out.complete_frame_ok = f == out.flight_start + 181;
        }
    }
    out.crc = host.crc_log();
    return out;
}

void test_space(const Data& d) {
    const SpaceRun a = run_space(d, 0);
    std::printf("  retreat: requested %d, flight %d (start %u, %zu listed, lined up %d, cruiser on the marker %d, dir %d), creep %d, release %d, completed %d at %u "
                "(181 frames: %d)\n",
                a.requested, a.flight, a.flight_start, a.listed, a.lined_up, a.cruiser_on_marker, a.dir_ok, a.creep_ok, a.release_ok, a.completed, a.complete_at,
                a.complete_frame_ok);
    for (size_t i = 0; i < a.release_counter.size(); ++i) std::printf("    list[%zu] released at counter %d\n", i, a.release_counter[i]);
    CHECK(a.fought && a.requested && a.flight && a.listed == 3);
    CHECK(a.lined_up && a.dir_ok && a.creep_ok && a.release_ok && a.cruiser_on_marker);
    CHECK(a.release_counter.size() == 3 && a.release_counter[0] == 30 && a.release_counter[1] == 31 && a.release_counter[2] == 34);
    CHECK(a.completed && a.complete_frame_ok);
    CHECK(a.saved);
    std::printf("  pursuit (trn1 t06): %d unit-frames checked during the countdown, ok %d\n", a.pursue_checks, a.pursue_ok);
    CHECK(a.pursue_checks > 0 && a.pursue_ok);
    std::printf("  debris: tracked %d (life %lld frames), moved %d, turned %d, particle followed %d, expired %d at %u (spawned %u), "
                "particle gone %d, explosion %d\n",
                a.debris_tracked, static_cast<long long>(a.debris_life), a.debris_moved, a.debris_turned, a.particle_followed,
                a.debris_expired, a.debris_gone_frame, a.debris_spawn_frame, a.particle_gone, a.boom_seen);
    CHECK(a.debris_tracked && a.debris_life == 60 && a.debris_moved && a.debris_turned && a.particle_followed);
    CHECK(a.debris_expired && a.particle_gone && a.boom_seen);
    const SpaceRun w = run_space(d, 4);
    const auto dv = first_divergence(a.crc, w.crc);
    std::printf("  workers 4: %s\n", dv.message.c_str());
    CHECK(!dv.diverged);
}

// ---- t05: land spawners ----
struct LandRun {
    CrcLog crc;
    bool fought = false;
    size_t spawners = 0;
    int32_t active = 0, decided = 0;
    size_t single_count = 0, pack_count = 0;
    bool kept_three = true, refilled = false, refill_delay_ok = false, pack_once = true, pack_refilled = false;
    bool near_hut = true, adopted = false, saved = false;
    uint32_t kill_frame = 0, refill_frame = 0;
};

LandRun run_land(const Data& d, int32_t workers) {
    LandRun out;
    jobs::JobSystem js(jobs::JobSystemConfig{workers});
    SimConfig cfg;
    cfg.seed = 9;
    cfg.players = 4;
    SimHost host(d.types, js, cfg);
    galactic::GalacticSetup s = make_setup(d);
    const uint32_t sq = type_of(d, "S10_Squad"), fr = type_of(d, "S10_Frigate");
    s.forces = {{0, 1, sq}, {1, 2, fr}, {1, 2, sq}};
    galactic::GalacticSim* gs = host.start_galactic(d.rules, std::move(s));
    CHECK(gs != nullptr);
    if (!gs) return out;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 3000;
    opt.battle.order_interval = 0;
    opt.battle.fog = false;
    const Data* dp = &d;
    CampaignBattles battles(host, d.crules, *d.catalog,
                            [dp](const std::string& name, terrain::MapData& m) {
                                if (name != "T_Land_B") return false;
                                m = land_map(*dp);
                                return true;
                            },
                            opt);
    CHECK(battles.attach());
    std::vector<objects::ObjectHandle> up;
    for (objects::ObjectHandle h : gs->planets()[2].surface)
        if (gs->store().owner(h) == 1) up.push_back(h);
    CHECK(gs->launch_units(1, 2, up) == commands::RejectReason::None);
    const std::vector<uint32_t> uf = gs->fleets_at(2, 1);
    CHECK(!uf.empty());
    if (!uf.empty()) CHECK(gs->move_fleet(1, uf[0], 1) == commands::RejectReason::None);
    bool invaded = false;
    const uint32_t hut = type_of(d, "T_Hut"), pack_hut = type_of(d, "T_Pack_Hut");
    std::vector<objects::ObjectId> pack_members;
    bool pack_killed = false;
    for (uint64_t t = 0; t < 4000; ++t) {
        host.tick_once();
        if (!invaded && host.modes().depth() == 1) {
            const std::vector<uint32_t> f = gs->fleets_at(1, 1);
            if (!f.empty() && !gs->fleets()[f[0]].moving) {
                CHECK(gs->invade(1, 1, {}) == commands::RejectReason::None);
                invaded = true;
            }
        }
        TacticalBattle* b = battles.battle();
        TacticalMode* tm = host.tactical();
        if (!b || !tm) continue;
        out.fought = true;
        objects::ObjectStore& st = tm->primary();
        const uint32_t f = b->frame();
        out.spawners = b->indigenous_spawner_count();
        int32_t active = 0, decided = 0;
        for (size_t i = 0; i < b->indigenous_spawner_count(); ++i) {
            const objects::ObjectHandle sh = st.find(b->indigenous_spawner_id(i));
            const auto ty = static_cast<uint32_t>(st.type(sh));
            const std::span<const objects::ObjectId> sp = b->indigenous_spawned(i);
            if (ty == hut) {
                decided += b->indigenous_spawner_state(i) >= 0 ? 1 : 0;
                active += b->indigenous_spawner_state(i) == 1 ? 1 : 0;
                if (b->indigenous_spawner_state(i) == 1) {
                    out.single_count = sp.size();
                    for (const objects::ObjectId id : sp) {
                        const objects::ObjectHandle uh = st.find(id);
                        const Vec3 p = st.position(uh), q = st.position(sh);
                        if (std::fabs(p.x - q.x) > 91.0f || std::fabs(p.y - q.y) > 91.0f) out.near_hut = false;
                        out.adopted = out.adopted || b->combat().has_unit(id);
                    }
                    if (f > 400 && f < 900 && sp.size() != 3 && out.kill_frame == 0) out.kept_three = false;
                    if (f == 900 && sp.size() == 3) {  // kill one: refilled at the first service after the 2 s delay
                        CHECK(b->script_damage(*tm, sp.front(), 1.0e6f, false));
                        out.kill_frame = f;
                    }
                    if (out.kill_frame && !out.refilled && f > out.kill_frame && sp.size() == 3) {
                        out.refilled = true;
                        out.refill_frame = f;
                        out.refill_delay_ok = f - out.kill_frame <= 60 + 30 && f - out.kill_frame >= 1;
                    }
                }
            }
            if (ty == pack_hut) {
                if (sp.size() == 3 && pack_members.empty()) pack_members.assign(sp.begin(), sp.end());
                out.pack_count = std::max(out.pack_count, sp.size());
                if (f == 1200 && !pack_killed && sp.size() == 3) {
                    // two of three die: no refill while one lives
                    CHECK(b->script_damage(*tm, sp[0], 1.0e6f, false));
                    CHECK(b->script_damage(*tm, sp[1], 1.0e6f, false));
                    pack_killed = true;
                }
                if (pack_killed && f > 1300 && f < 1400 && sp.size() > 1) out.pack_once = false;
                if (f == 1400 && sp.size() == 1) CHECK(b->script_damage(*tm, sp[0], 1.0e6f, false));
                if (f > 1400 && sp.size() == 3) out.pack_refilled = true;
            }
        }
        out.active = active;
        out.decided = decided;
        if (f == 1000) out.saved = state_round_trip(*b);  // live spawners with spawned units: save / load
        if (f > 1600) break;
    }
    out.crc = host.crc_log();
    return out;
}

void test_land(const Data& d) {
    const LandRun a = run_land(d, 0);
    std::printf("  spawners: %zu, T_Hut decided %d active %d (Chance 0.5 of 2 -> 1); single %zu (kept 3 %d, near %d, adopted %d), "
                "refilled %d at %u after the kill at %u (delay ok %d); pack %zu (no refill while one lives %d, refilled %d)\n",
                a.spawners, a.decided, a.active, a.single_count, a.kept_three, a.near_hut, a.adopted, a.refilled, a.refill_frame,
                a.kill_frame, a.refill_delay_ok, a.pack_count, a.pack_once, a.pack_refilled);
    CHECK(a.fought && a.spawners == 3 && a.decided == 2 && a.active == 1);
    CHECK(a.single_count == 3 && a.kept_three && a.near_hut && a.adopted);
    CHECK(a.kill_frame > 0 && a.refilled && a.refill_delay_ok);
    CHECK(a.pack_count == 3 && a.pack_once && a.pack_refilled);
    CHECK(a.saved);
    const LandRun w = run_land(d, 4);
    const auto dv = first_divergence(a.crc, w.crc);
    std::printf("  workers 4: %s\n", dv.message.c_str());
    CHECK(!dv.diverged);
}

}  // namespace

int main() {
    const auto d = make_data();
    std::printf("space: retreat jump-out flight, debris\n");
    test_space(*d);
    std::printf("land: indigenous spawners\n");
    test_land(*d);
    if (g_fail) {
        std::printf("alamo_sim_tru1_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_sim_tru1_tests: OK\n");
    return 0;
}
