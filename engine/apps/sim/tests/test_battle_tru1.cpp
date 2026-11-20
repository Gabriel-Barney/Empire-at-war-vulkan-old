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
//   * mcx6 story rewards on the battle (STORY_REWARDS_GALACTIC §1.11 - §1.13): FORCE_RETREAT refuses the local player and
//     starts the normal retreat for another, SET_HEALTH sets a percentage of the maximum (count 0 = all, n = the first n),
//     SET_SPAWN re-balances a spawner type by its upper-case name, SET_PLANET_SPAWN's entry decides the battle planet's
//     spawners at the first scan.
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
#include "alamo/test/galactic_dummies.hpp"

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
 <GroundInfantry Name="T_Ewok_B"><Variant_Of_Existing_Type>T_Ewok</Variant_Of_Existing_Type>
  <Is_Indigenous_Unit_Corruptible>False</Is_Indigenous_Unit_Corruptible></GroundInfantry>
 <GroundCompany Name="T_Ewok_Pack"><Company_Units>T_Ewok, T_Ewok, T_Ewok</Company_Units><Create_Team_Type>T_Ewok_Team</Create_Team_Type></GroundCompany>
 <Team Name="T_Ewok_Team"><Behavior>TEAM</Behavior></Team>
 <GroundStructure Name="T_Hut"><Tactical_Health>500</Tactical_Health><LandBehavior>SPAWN_INDIGENOUS_UNITS</LandBehavior>
  <Spawned_Indigenous_Units_Type>T_Ewok, T_Ewok_B</Spawned_Indigenous_Units_Type><Spawned_Indigenous_Units_Delay_Seconds>2.0</Spawned_Indigenous_Units_Delay_Seconds>
  <Spawned_Indigenous_Units_Quantity>3</Spawned_Indigenous_Units_Quantity><Spawn_Indigenous_Units_In_Packs>No</Spawn_Indigenous_Units_In_Packs>
  <Spawn_Indigenous_Units_Radius>90.0f</Spawn_Indigenous_Units_Radius><Spawn_Indigenous_Units_Chance>0.5f</Spawn_Indigenous_Units_Chance>
  <Max_Distance_From_Spawner>150</Max_Distance_From_Spawner></GroundStructure>
 <GroundInfantry Name="T_Ewok_Restless"><Variant_Of_Existing_Type>T_Ewok</Variant_Of_Existing_Type>
  <Can_Indigenous_Unit_Stop>False</Can_Indigenous_Unit_Stop></GroundInfantry>
 <GroundStructure Name="T_Restless_Hut"><Variant_Of_Existing_Type>T_Hut</Variant_Of_Existing_Type>
  <Spawned_Indigenous_Units_Type>T_Ewok_Restless</Spawned_Indigenous_Units_Type><Spawn_Indigenous_Units_Chance>100.0</Spawn_Indigenous_Units_Chance></GroundStructure>
 <GroundStructure Name="T_Pack_Hut"><Variant_Of_Existing_Type>T_Hut</Variant_Of_Existing_Type>
  <Spawned_Indigenous_Units_Type>T_Ewok</Spawned_Indigenous_Units_Type><Spawn_Indigenous_Units_In_Packs>Yes</Spawn_Indigenous_Units_In_Packs>
  <Spawned_Indigenous_Pack_Type>T_Ewok_Pack</Spawned_Indigenous_Pack_Type><Spawn_Indigenous_Units_Chance>100.0</Spawn_Indigenous_Units_Chance></GroundStructure>
</T>
)";

const char* kFactions = R"(<Factions>
 <Faction Name="Empire"><Is_Playable>Yes</Is_Playable><Can_Control_Planets>Yes</Can_Control_Planets><Space_Tactical_Unit_Cap>20</Space_Tactical_Unit_Cap>
  <Space_Retreat_Unit_Increment_Wait_Frames>3</Space_Retreat_Unit_Increment_Wait_Frames><Space_Retreat_Flight_Move_Increment>500.0</Space_Retreat_Flight_Move_Increment></Faction>
 <Faction Name="Underworld"><Is_Playable>Yes</Is_Playable><Can_Control_Planets>Yes</Can_Control_Planets><Space_Tactical_Unit_Cap>20</Space_Tactical_Unit_Cap>
  <Benefits_From_Corruption>Yes</Benefits_From_Corruption></Faction>
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
    write_file(x / "TTypes.xml", ::alamo::test::with_galactic_dummies(kTypes));
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

terrain::MapData land_map(const Data& d, int32_t extra_huts = 0, bool restless = false) {
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
    for (int32_t i = 0; i < extra_huts; ++i) hut("T_Hut", 200.0f + 100.0f * static_cast<float>(i), 1400.0f);
    if (restless) hut("T_Restless_Hut", 1200.0f, 900.0f);  // tru2 t01
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
    // mcx6: FORCE_RETREAT of the local player refused (no retreat), of the defender accepted, a second one not made;
    // SET_HEALTH 50 % of every frigate, then 100 % of the first only.
    bool force_local_refused = false, force_again_refused = false, health_half = false, health_first_only = false;
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
        if (f == 2) {  // mcx6 SET_HEALTH on the four S10_Frigate (T_Cruiser is another type)
            const auto frig = static_cast<objects::TypeIndex>(type_of(d, "S10_Frigate"));
            std::vector<objects::ObjectId> ids;  // creation order
            for (const objects::ObjectHandle o : st.all_objects())
                if (st.alive(o) && st.type(o) == frig && b->combat().has_unit(st.id(o))) ids.push_back(st.id(o));
            std::sort(ids.begin(), ids.end());
            bool full = !ids.empty();
            for (const objects::ObjectId id : ids) full = full && b->combat().health(id) == b->combat().max_health(id);
            out.health_half = full && b->reward_set_health(*tm, frig, 50, 0) == ids.size();
            for (const objects::ObjectId id : ids)
                out.health_half = out.health_half && b->combat().health(id) == b->combat().max_health(id) * 0.5f;
            out.health_first_only = b->reward_set_health(*tm, frig, 150, 1) == 1 && !ids.empty() &&
                                    b->combat().health(ids.front()) == b->combat().max_health(ids.front());
            for (size_t i = 1; i < ids.size(); ++i)
                out.health_first_only = out.health_first_only && b->combat().health(ids[i]) == b->combat().max_health(ids[i]) * 0.5f;
            b->reward_set_health(*tm, frig, 100, 0);  // back to full: the rest of the run is unchanged
        }
        if (!out.requested && b->retreat_block(0) == 0) {
            // mcx6 FORCE_RETREAT: player 0 is the local (human) player: refused, no retreat; with another local player the
            // request is the normal one; a second reward while it runs makes no request.
            bool accepted = true;
            out.force_local_refused = b->reward_force_retreat(0, gs->local_player(), &accepted) && !accepted && b->retreating_player() < 0;
            b->reward_force_retreat(0, 1, &out.requested);
            out.force_again_refused = !b->reward_force_retreat(0, 1);
        }
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
    std::printf("  mcx6 FORCE_RETREAT: local refused %d, second refused %d; SET_HEALTH half %d, first only %d\n", a.force_local_refused,
                a.force_again_refused, a.health_half, a.health_first_only);
    CHECK(a.force_local_refused && a.force_again_refused && a.health_half && a.health_first_only);
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
    // variant 3: T_Hut on after SET_SPAWN 0.3 and then 1.0; the other battle rewards through the battle host
    int32_t on_low = -1, on_full = -1;
    bool health_half = false, retreat_logged = false, invalid_logged = false, not_shield_silent = false;
    // variant 4 (mcx4 t05): spawned units of the active T_Hut on a Corrupt_Militia planet: T_Ewok (corruptible) owned by
    // the Underworld, T_Ewok_B (Is_Indigenous_Unit_Corruptible False) by the hut's owner; variant 5: a Kidnapping planet,
    // every unit kept
    uint32_t corrupt_converted = 0, corrupt_kept = 0, corrupt_wrong = 0;
    // tru2 t01 (INDIGENOUS_UNIT wander): move starts (a still unit that moves) of the active T_Hut's units and, variant 6,
    // of the T_Restless_Hut's (Can_Indigenous_Unit_Stop False); the farthest any of them got from its hut (sampled each
    // frame); unit-frames sampled per group
    uint32_t moves = 0, restless_moves = 0, unit_frames = 0, restless_unit_frames = 0;
    float max_home_dist = 0.0f;
};

// variant 0: the TRU1 run; 1: SET_SPAWN "T_HUT" 1.0 at frame 5 through the battle host's story reward (both T_Hut on;
// "t_hut" matches nothing); 2: a SET_PLANET_SPAWN entry (battle planet, "T_HUT", 0.0) in the story gates before the battle
// (both T_Hut off at the first scan); 3: eight T_Hut more, at frame 5 SET_SPAWN 0.3 (3 of 10 on, 7 off) then 1.0 (every one on:
// N = on + off, STORY_REWARDS_GALACTIC §1.13), and SET_HEALTH / DISABLE_SPECIAL_STRUCTURE / FORCE_RETREAT through
// CampaignBattles::story_tactical_reward. Variants stop at frame 20; 4 and 5 at 400; 6 (tru2 t01: a T_Restless_Hut more) runs
// as 0.
LandRun run_land(const Data& d, int32_t workers, int variant = 0) {
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
                            [dp, variant](const std::string& name, terrain::MapData& m) {
                                if (name != "T_Land_B") return false;
                                m = land_map(*dp, variant == 3 ? 8 : 0, variant == 6);
                                return true;
                            },
                            opt);
    CHECK(battles.attach());
    if (variant == 4) gs->set_corruption(1, 4, 1);  // the battle planet: Corruption_Corrupt_Militia by the Underworld
    if (variant == 5) gs->set_corruption(1, 2, 1);  // Corruption_Kidnapping: no conversion
    if (variant == 2) {
        galactic::StoryGates g;
        galactic::StoryGates::PlanetSpawn e;
        e.planet = 1;
        e.key = "T_HUT";
        e.crc = crc32(e.key.data(), e.key.size());
        e.chance = 0.0f;
        g.planet_spawn.push_back(e);
        gs->set_story_gates(g);
    }
    std::vector<objects::ObjectHandle> up;
    for (objects::ObjectHandle h : gs->planets()[2].surface)
        if (gs->store().owner(h) == 1) up.push_back(h);
    CHECK(gs->launch_units(1, 2, up) == commands::RejectReason::None);
    const std::vector<uint32_t> uf = gs->fleets_at(2, 1);
    CHECK(!uf.empty());
    if (!uf.empty()) CHECK(gs->move_fleet(1, uf[0], 1) == commands::RejectReason::None);
    bool invaded = false;
    const uint32_t hut = type_of(d, "T_Hut"), pack_hut = type_of(d, "T_Pack_Hut"), restless_hut = type_of(d, "T_Restless_Hut");
    std::vector<objects::ObjectId> pack_members;
    std::vector<objects::ObjectId> seen_units;                 // near_hut is checked where a unit was placed
    std::vector<std::pair<objects::ObjectId, Vec3>> last_pos;  // tru2 t01: the previous frame's position
    std::vector<objects::ObjectId> moving_units;               // moved since the previous frame
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
        auto huts_on = [&] {
            int32_t on = 0;
            for (size_t i = 0; i < b->indigenous_spawner_count(); ++i)
                if (static_cast<uint32_t>(st.type(st.find(b->indigenous_spawner_id(i)))) == hut && b->indigenous_spawner_state(i) == 1) ++on;
            return on;
        };
        auto logged = [&battles](const std::string& what) {
            for (const std::string& l : battles.log())
                if (l.find(what) != std::string::npos) return true;
            return false;
        };
        if (variant == 1 && f == 5) {
            CHECK(b->reward_set_spawn(*tm, host.random().synced(), "t_hut", 1.0f) == 0);  // the CRC of the upper-case name only
            battles.story_tactical_reward("SET_SPAWN", {"T_HUT", "1.0"});
        }
        if (variant == 3 && f == 5) {
            battles.story_tactical_reward("SET_SPAWN", {"T_HUT", "0.3"});
            out.on_low = huts_on();
            battles.story_tactical_reward("SET_SPAWN", {"T_HUT", "1.0"});
            out.on_full = huts_on();
            // SET_HEALTH: the first T_Hut (creation order) at half health.
            battles.story_tactical_reward("SET_HEALTH", {"T_Hut", "50"});
            objects::ObjectId first = objects::kNoObjectId;
            for (const objects::ObjectHandle o : st.all_objects())
                if (st.alive(o) && static_cast<uint32_t>(st.type(o)) == hut && (first == objects::kNoObjectId || st.id(o) < first)) first = st.id(o);
            if (first != objects::kNoObjectId)  // a combat unit's health, else the store's (Tactical_Health 500)
                out.health_half = b->combat().has_unit(first) ? b->combat().health(first) == b->combat().max_health(first) * 0.5f
                                                              : st.health(st.find(first)) == 250.0f;
            // DISABLE_SPECIAL_STRUCTURE: an unknown type is logged; a type without BASE_SHIELD is silently left alone.
            const size_t before = battles.log().size();
            battles.story_tactical_reward("DISABLE_SPECIAL_STRUCTURE", {"T_Hut", "1"});
            out.not_shield_silent = battles.log().size() == before;
            battles.story_tactical_reward("DISABLE_SPECIAL_STRUCTURE", {"No_Such_Structure", "1"});
            out.invalid_logged = logged("Invalid structure No_Such_Structure in DISABLE_SPECIAL_STRUCTURE");
            // FORCE_RETREAT of the Underworld (player 1, not the local player): the request is made and logged.
            battles.story_tactical_reward("FORCE_RETREAT", {"Underworld"});
            out.retreat_logged = logged("Force retreat of player Underworld");
        }
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
                    if ((variant == 4 || variant == 5) && f == 399) {
                        const uint32_t ewok = type_of(d, "T_Ewok");
                        for (const objects::ObjectId id : sp) {
                            const objects::ObjectHandle uh = st.find(id);
                            const bool corruptible = variant == 4 && static_cast<uint32_t>(st.type(uh)) == ewok;
                            if (corruptible && st.owner(uh) == 1 && b->combat().has_unit(id)) ++out.corrupt_converted;
                            else if (!corruptible && st.owner(uh) == st.owner(sh)) ++out.corrupt_kept;
                            else ++out.corrupt_wrong;
                        }
                    }
                    for (const objects::ObjectId id : sp) {
                        const objects::ObjectHandle uh = st.find(id);
                        const Vec3 p = st.position(uh), q = st.position(sh);
                        if (std::find(seen_units.begin(), seen_units.end(), id) == seen_units.end()) {
                            seen_units.push_back(id);
                            if (std::fabs(p.x - q.x) > 91.0f || std::fabs(p.y - q.y) > 91.0f) out.near_hut = false;
                        }
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
            if ((ty == hut && b->indigenous_spawner_state(i) == 1) || ty == restless_hut) {  // tru2 t01
                const bool restless = ty == restless_hut;
                const Vec3 q = st.position(sh);
                for (const objects::ObjectId id : sp) {
                    const Vec3 p = st.position(st.find(id));
                    out.max_home_dist = std::max(out.max_home_dist, std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y)));
                    ++(restless ? out.restless_unit_frames : out.unit_frames);
                    auto lp = std::find_if(last_pos.begin(), last_pos.end(), [id](const auto& e) { return e.first == id; });
                    if (lp == last_pos.end()) {
                        last_pos.emplace_back(id, p);
                        continue;
                    }
                    const bool moved = lp->second.x != p.x || lp->second.y != p.y;
                    auto mv = std::find(moving_units.begin(), moving_units.end(), id);
                    if (moved && mv == moving_units.end()) {
                        moving_units.push_back(id);
                        ++(restless ? out.restless_moves : out.moves);
                    } else if (!moved && mv != moving_units.end()) {
                        moving_units.erase(mv);
                    }
                    lp->second = p;
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
        if (f > 1600 || (variant != 0 && variant < 4 && f >= 20) || ((variant == 4 || variant == 5) && f >= 400)) break;
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
    // mcx6 SET_SPAWN / SET_PLANET_SPAWN
    const LandRun s1 = run_land(d, 0, 1), s2 = run_land(d, 0, 2), s1w = run_land(d, 4, 1);
    std::printf("  mcx6 SET_SPAWN 1.0: T_Hut decided %d active %d; SET_PLANET_SPAWN 0.0: decided %d active %d\n", s1.decided,
                s1.active, s2.decided, s2.active);
    CHECK(s1.fought && s1.decided == 2 && s1.active == 2);
    CHECK(s2.fought && s2.decided == 2 && s2.active == 0);
    CHECK(!first_divergence(s1.crc, s1w.crc).diverged);
    const LandRun s3 = run_land(d, 0, 3), s3w = run_land(d, 4, 3);
    std::printf("  mcx6 SET_SPAWN on 10 T_Hut: 0.3 -> %d on, then 1.0 -> %d on; SET_HEALTH %d, DISABLE_SPECIAL_STRUCTURE silent %d "
                "logged %d, FORCE_RETREAT logged %d\n",
                s3.on_low, s3.on_full, s3.health_half, s3.not_shield_silent, s3.invalid_logged, s3.retreat_logged);
    CHECK(s3.fought && s3.on_low == 3 && s3.on_full == 10 && s3.decided == 10);
    CHECK(s3.health_half && s3.not_shield_silent && s3.invalid_logged && s3.retreat_logged);
    CHECK(!first_divergence(s3.crc, s3w.crc).diverged);
    // mcx4 t05 (MCX4_RE_ANSWERS_TACTICAL §6, 0x5314f0): Is_Indigenous_Unit_Corruptible on a Corrupt_Militia planet
    // ledger: verifies GameObjectType.Is_Indigenous_Unit_Corruptible
    const LandRun c4 = run_land(d, 0, 4);
    std::printf("  mcx4 Corrupt_Militia: %u T_Ewok to the Underworld, %u T_Ewok_B kept, %u wrong\n", c4.corrupt_converted, c4.corrupt_kept,
                c4.corrupt_wrong);
    CHECK(c4.fought && c4.corrupt_converted + c4.corrupt_kept == 3 && c4.corrupt_converted > 0 && c4.corrupt_wrong == 0);
    const LandRun c5 = run_land(d, 0, 5);  // another corruption type: every unit stays with the hut's owner
    CHECK(c5.fought && c5.corrupt_kept == 3 && c5.corrupt_converted == 0 && c5.corrupt_wrong == 0);
    // tru2 t01 (INDIGENOUS_UNIT wander 0x621d80, MCX4_T11_T12_RE_ANSWERS "Max_Distance_From_Spawner"): idle spawned units
    // wander; the target is a point within Max_Distance_From_Spawner (150) of the hut, a step outside [50, 400] is redrawn
    // inside it, so no unit gets farther than 150 + 400 from its hut. Can_Indigenous_Unit_Stop (default true) keeps the unit
    // put on 90 % of the services: the restless type (False) starts many more moves per unit-frame.
    const LandRun r6 = run_land(d, 0, 6), r6w = run_land(d, 4, 6);
    const double stay_rate = a.unit_frames ? static_cast<double>(a.moves) * 1000.0 / a.unit_frames : 0.0;
    const double restless_rate =
        r6.restless_unit_frames ? static_cast<double>(r6.restless_moves) * 1000.0 / r6.restless_unit_frames : 0.0;
    std::printf("  tru2 wander: T_Hut units %u move starts in %u unit-frames (%.2f per 1000), restless %u in %u (%.2f per 1000), "
                "farthest from a hut %.1f\n",
                a.moves, a.unit_frames, stay_rate, r6.restless_moves, r6.restless_unit_frames, restless_rate, r6.max_home_dist);
    CHECK(a.moves > 0 && r6.restless_moves > 0);
    CHECK(restless_rate > 3.0 * stay_rate);
    CHECK(a.max_home_dist <= 550.0f && r6.max_home_dist <= 550.0f);
    CHECK(!first_divergence(r6.crc, r6w.crc).diverged);
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
