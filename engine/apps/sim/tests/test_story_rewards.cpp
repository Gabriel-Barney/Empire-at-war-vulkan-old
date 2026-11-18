// STY1 lane B tests: galactic / tutorial / Underworld / tactical story rewards (StorySession::apply_sty1_reward) and the
// exit-test coverage table of every Reward_Type / STORY_GENERIC name in the shipped FoC / EaW story XML.
//   * synthetic plots fire the rewards over the story_test galactic fixture: gates, refusals, tutorial state, save / hash,
//     identical results at 0 and 4 workers
//   * coverage (ALAMO_FOC_DATA / ALAMO_EAW_DATA, skipped when unset): every reward name has a row in story_reward_table()
//     and no Session-owned row lacks a handler; the table is printed (name, owner, note)
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "alamo/core/bytes.hpp"
#include "alamo/core/hash.hpp"
#include "alamo/core/file_provider.hpp"
#include "alamo/core/host_fs.hpp"
#include "alamo/core/log.hpp"
#include "alamo/galactic/campaign.hpp"
#include "alamo/galactic/galactic_rules.hpp"
#include "alamo/jobs/job_system.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/objects/object_types.hpp"
#include "alamo/vfs/vfs.hpp"
#include "alamo/sim/sim_host.hpp"
#include "alamo/lua/story_model.hpp"
#include "alamo/sim/scenario.hpp"
#include "alamo/sim/story_rewards.hpp"
#include "alamo/sim/story_session.hpp"
#include "alamo/test/support.hpp"

using namespace alamo;
using namespace alamo::sim;
namespace fs = std::filesystem;
using ::alamo::test::g_fail;
using ::alamo::test::write_file;

namespace story_test {
using namespace alamo::galactic;
enum : uint32_t { tPA, tPB, tPC, tPD, tSB1, tCorvette, tInfantry, tBarracks, tFighters, tCount };
enum : PlanetIndex { rCore = 0, rA = 1, rB = 2, rC = 3, rD = 4 };
enum : int32_t { pEmpire = 0, pUnder = 1, pNeutral = 2 };

static GalacticRules rules() {
    GalacticRules r;
    FactionRules emp;
    emp.name = "Empire";
    emp.basic_ai = "BasicEmpire";
    emp.can_control_planets = emp.is_playable = true;
    FactionRules und;
    und.name = "Underworld";
    und.basic_ai = "AI_Player_Underworld";
    und.can_control_planets = und.is_playable = und.benefits_from_corruption = true;
    FactionRules neu;
    neu.name = "Neutral";
    neu.is_neutral = true;
    emp.debug_switchable = und.debug_switchable = true;  // mcx6 SWITCH_SIDES (Is_Debug_Switchable_To)
    r.factions = {emp, und, neu};
    // mcx6 RANDOM_STORY constants (GameConstants Random_Story_*): one trigger type, at most 2 triggers, a credits reward.
    r.constants.random_story_triggers = {"STORY_CONSTRUCT"};
    r.constants.random_story_max_triggers = 2;
    r.constants.random_story_rewards = {"CREDITS"};
    r.constants.random_story_construction = {std::vector<std::string>{"Corvette"}, std::vector<std::string>{"Corvette"}};
    r.difficulties = {{"Normal", 1.0f, 1.0f}};
    r.types.resize(tCount);
    const char* names[] = {"PlanetA", "PlanetB", "PlanetC", "PlanetD"};
    for (uint32_t i = 0; i < 4; ++i) {
        r.types[i].name = names[i];
        r.types[i].kind = UnitKind::Planet;
        r.types[i].credit_value = 10;
        r.types[i].capture_bonus = 100;
    }
    auto unit = [&r](uint32_t id, const char* name, UnitKind k, int32_t cost, int32_t secs) -> TypeInfo& {
        TypeInfo& t = r.types[id];
        t.name = name;
        t.kind = k;
        t.build_cost = cost;
        t.build_time_seconds = secs;
        return t;
    };
    unit(tSB1, "SB1", UnitKind::StarBase, 1000, 15).base_level = 1;
    TypeInfo& co = unit(tCorvette, "Corvette", UnitKind::SpaceUnit, 800, 10);
    co.required_star_base = 1;
    co.hyperspace = true;
    co.hyperspace_speed = 2.0f;
    co.ai_combat_power = 600.0f;
    co.squadron_capacity = 2;
    unit(tInfantry, "Infantry", UnitKind::GroundUnit, 300, 10).required_ground_base = 1;
    unit(tBarracks, "Barracks", UnitKind::GroundStructure, 600, 20);
    unit(tFighters, "Fighters", UnitKind::Squadron, 200, 2).required_star_base = 1;
    return r;
}

static GalacticSetup setup() {
    GalacticSetup s;
    GalaxyMap& m = s.map;
    m.campaign_name = "StoryTest";
    const char* names[] = {"Core", "A", "B", "C", "D"};
    const Vec3 pos[] = {{500, 500, 0}, {0, 0, 0}, {60, 0, 0}, {120, 0, 0}, {60, 80, 0}};
    for (uint32_t i = 0; i < 5; ++i) {
        m.planets.add(names[i]);
        m.planets.position[i] = pos[i];
        m.planets.flags[i] = i == 0 ? 0u : static_cast<uint32_t>(kPlanetHasPlanetBehavior);
    }
    auto route = [&m](const char* n, PlanetIndex a, PlanetIndex b) {
        const uint32_t r = m.routes.add(n);
        m.routes.point_a[r] = a;
        m.routes.point_b[r] = b;
        m.routes.hs_speed_factor[r] = 8.0f;
    };
    route("A_B", rA, rB);
    route("B_C", rB, rC);
    route("B_D", rB, rD);
    s.planet_type = {kNoIndex, tPA, tPB, tPC, tPD};
    PlayerSetup e;
    e.faction = 0;
    e.human = true;
    e.credits = 5000.0f;
    PlayerSetup u;
    u.faction = 1;
    u.ai = true;
    u.difficulty = 0;
    u.credits = 5000.0f;
    PlayerSetup n;
    n.faction = 2;
    s.players = {e, u, n};
    s.forces = {{pEmpire, rA, tSB1}, {pEmpire, rA, tBarracks}, {pUnder, rD, tCorvette}, {pUnder, rD, tInfantry}};
    return s;
}


static const char* kPlotsFile =
    "<Story_Mode_Plots><Active_Plot>Story_Sty1.xml</Active_Plot><Suspended_Plot /></Story_Mode_Plots>\n";

static std::string ev(const char* name, const char* type, std::initializer_list<const char*> params, const char* prereq = "Begin") {
    std::string s = std::string(" <Event Name=\"") + name + "\"><Event_Type>STORY_TRIGGER</Event_Type><Reward_Type>" + type + "</Reward_Type>";
    int i = 1;
    for (const char* p : params) {
        s += "<Reward_Param" + std::to_string(i) + ">" + p + "</Reward_Param" + std::to_string(i) + ">";
        ++i;
    }
    return s + "<Prereq>" + prereq + "</Prereq></Event>\n";
}

static std::string sty1_plot() {
    std::string p = "<Story>\n <Event Name=\"Begin\"><Event_Type>STORY_ELAPSED</Event_Type><Event_Param1>0</Event_Param1></Event>\n";
    p += ev("Restrict_C", "SET_PLANET_RESTRICTED", {"PlanetC", "1"});
    p += " <Event Name=\"Wait\"><Event_Type>STORY_ELAPSED</Event_Type><Event_Param1>3</Event_Param1><Prereq>Restrict_C</Prereq></Event>\n";
    p += ev("Unrestrict_C", "SET_PLANET_RESTRICTED", {"PlanetC", "0"}, "Wait");
    p += ev("B_To_Under", "PLANET_FACTION", {"PlanetB", "Underworld"});
    p += ev("Bad_Faction", "PLANET_FACTION", {"PlanetB", "Nobody"});
    p += ev("Remove_Inf", "REMOVE_UNIT", {"Infantry"});
    p += ev("Force_Resp", "FORCE_RESPAWN", {"Corvette"});
    p += ev("Power_Grant", "NEW_POWER_FOR_ALL", {"Corvette", "Some_Ability"});
    p += ev("Power_Revoke", "REMOVE_POWER_FROM_ALL", {"Fighters", "Other_Ability"});
    p += ev("Move_Enemy", "MOVE_FLEET", {"PlanetD", "PlanetB", "FILTER_ENEMY_ONLY"});
    p += ev("No_Direct", "DISABLE_DIRECT_INVASION", {});
    p += ev("Spawn_Noop", "SET_PLANET_SPAWN", {"PlanetA"});
    p += ev("Load", "LOAD_CAMPAIGN", {"Tutorial_Campaign", "1", "2", "7"});  // modc t45: P3 kept (0..2), P4 7 dropped (0..5)
    p += ev("Tut_Player", "TUTORIAL_PLAYER", {"Empire"});
    p += ev("Tut_Done", "FINISHED_TUTORIAL", {"Tutorial_One"});
    p += ev("Tut_Done2", "FINISHED_TUTORIAL", {"Tutorial_One"}, "Tut_Done");
    p += ev("Block_All", "DISABLE_EVENT", {"TUTORIAL_ALL"});
    p += ev("Allow_Btn", "ENABLE_EVENT", {"TUTORIAL_CLICK_GUI", "b_retreat"}, "Block_All");
    p += ev("Skirmish", "SKIRMISH_RULES", {"1"});
    p += ev("Filters", "RESET_GALACTIC_FILTERS", {});
    p += ev("Sandbox", "SET_SANDBOX_OBJECTIVES", {});
    p += ev("No_Inv_A", "ENABLE_INVASION", {"PlanetA", "0"});
    p += ev("No_Sab_A", "RESTRICT_SABOTAGE", {"PlanetA"});
    p += ev("No_Cor_D", "RESTRICT_CORRUPTION", {"PlanetD", "1"});
    p += ev("No_Combine", "ENABLE_FLEET_COMBINE", {"0"});
    p += ev("No_Bounty", "DISABLE_BOUNTY_COLLECTION", {});
    p += ev("Movie", "ENABLE_CAMPAIGN_VICTORY_MOVIE", {});
    p += ev("No_Reinf", "DISABLE_REINFORCEMENTS", {"1", "Underworld"});
    p += ev("No_Retreat", "DISABLE_RETREAT", {"Underworld", "1"});
    p += ev("Destroy_Bar", "DESTROY_OBJECT", {"Barracks"});
    p += ev("Own_SB", "CHANGE_OWNER", {"SB1", "Underworld"});
    p += ev("Ai_Flag", "TRIGGER_AI", {"Flag_X", "Underworld", "PlanetC"});
    p += ev("Ai_Flag_Bad", "TRIGGER_AI", {"Flag_Y"});
    p += ev("Ai_Flag_None", "TRIGGER_AI", {"Flag_Z", "Underworld"});  // storyb t10: no planet
    return p + "</Story>\n";
}

struct TypesWorld {
    objects::ObjectTypeDatabase db;
    objects::GameObjectTypes types;
};

static fs::path make_root(const char* name, std::unique_ptr<DirectoryProvider>& files, TypesWorld& w) {
    const fs::path root = fs::temp_directory_path() / name;
    std::error_code ec;
    remove_all_long(root, ec);
    write_file(root / "Data/XML/GameObjectFiles.xml", "<GameObjectFiles><File>S.xml</File></GameObjectFiles>\n");
    write_file(root / "Data/XML/S.xml", "<S><SpaceUnit Name=\"Sim_Dummy\"><Tactical_Health>10</Tactical_Health></SpaceUnit></S>\n");
    write_file(root / "Data/XML/Story_Plots_Sty1.xml", kPlotsFile);
    write_file(root / "Data/XML/Story_Sty1.xml", sty1_plot());
    files = std::make_unique<DirectoryProvider>(path_to_utf8(root));
    CHECK(w.db.load(*files) && w.types.build(w.db));
    return root;
}

static uint32_t fleet_of(const GalacticSim& gs, int32_t owner) {
    uint32_t r = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < gs.fleets().size(); ++i)
        if (gs.fleets()[i].alive && gs.fleets()[i].owner == owner) r = i;
    return r;
}

static void test_sty1_rewards() {
    std::unique_ptr<DirectoryProvider> files;
    TypesWorld w;
    const fs::path root = make_root("alamo_sim_story_rewards_sty1", files, w);
    std::string first_hash;
    for (int32_t workers : {0, 4}) {
        jobs::JobSystem js(jobs::JobSystemConfig{workers});
        SimConfig cfg;
        cfg.seed = 7;
        cfg.story_crc = true;
        SimHost host(w.types, js, cfg);
        const GalacticRules rules_keep = rules();
        GalacticSim* gs = host.start_galactic(rules_keep, setup());
        CHECK(gs != nullptr);
        if (!gs) return;
        // A queued respawn of the Corvette far in the future (FORCE_RESPAWN makes it due now).
        ObjectHandle corvette;
        for (const FleetState& f : gs->fleets())
            if (f.alive && f.owner == pUnder)
                for (const ObjectHandle h : f.contents)
                    if (gs->store().type(h) == tCorvette) corvette = h;
        CHECK(gs->store().exists(corvette));
        CHECK(gs->schedule_hero_respawn(corvette, rD, 1000.0f));
        CHECK(!gs->planets()[rD].respawns.empty() && gs->planets()[rD].respawns.front().due > 1000);
        CHECK(gs->planets()[rB].owner != pUnder);
        StoryStart st;
        st.files = files.get();
        st.plot_sets = {{pEmpire, "Story_Plots_Sty1.xml"}};
        host.tick_once();
        CHECK(host.start_story(st, nullptr) && host.story());
        StorySession* ss = host.story();
        if (!ss) return;
        for (int t = 0; t < 20; ++t) host.tick_once();
        // SET_PLANET_RESTRICTED: C is restricted until the 3 s wait ends.
        CHECK(ss->gates().has(rC, StoryGates::kRestricted) && gs->story_gates().has(rC, StoryGates::kRestricted));
        for (int t = 0; t < 400; ++t) host.tick_once();
        CHECK(!ss->gates().has(rC, StoryGates::kRestricted) && !gs->story_gates().has(rC, StoryGates::kRestricted));
        // PLANET_FACTION: B belongs to the Underworld.
        CHECK(gs->planets()[rB].owner == pUnder);
        // REMOVE_UNIT Infantry: gone.
        CHECK(gs->store().find_all_of_type(static_cast<objects::TypeIndex>(tInfantry)).empty());
        // FORCE_RESPAWN: the respawn entry is due now.
        CHECK(gs->planets()[rD].respawns.empty() || gs->planets()[rD].respawns.front().due <= 500);  // due now (or already served)
        // Power table.
        CHECK(ss->gates().powers.size() == 2 && !ss->gates().powers[0].revoked && ss->gates().powers[1].revoked);
        // Flags.
        const StoryGates& g = ss->gates();
        CHECK(g.direct_invasion_disabled && !g.fleet_combine && !g.bounty_collection && g.campaign_victory_movie);
        CHECK(g.has(rA, StoryGates::kNoInvasion) && g.has(rA, StoryGates::kNoSabotage) && g.has(rD, StoryGates::kNoCorruption));
        CHECK(!g.has(rA, StoryGates::kRestricted) && !g.is_default());
        // Tutorial state.
        const StoryTutorialState& tu = ss->tutorial();
        CHECK(tu.marked_players.size() == 1 && tu.marked_players[0] == pEmpire);
        CHECK(tu.finished.size() == 1 && tu.finished[0] == "Tutorial_One");
        CHECK(!tu.all_allowed && tu.skirmish_rules && tu.filter_resets == 1 && tu.sandbox_requests == 1);
        CHECK(ss->gui_event_allowed(1, "b_retreat") && !ss->gui_event_allowed(1, "b_other") && !ss->gui_event_allowed(2, "x"));
        CHECK(ss->gui_event_allowed(1, "b_option_g"));  // always allowed in "only listed" mode (retail 0x446220)
        // LOAD_CAMPAIGN.
        CHECK(ss->pending_load_campaign() && ss->pending_load_campaign()->upper_name == "TUTORIAL_CAMPAIGN" && ss->pending_load_campaign()->side == 1);
        const auto taken = ss->take_load_campaign();
        CHECK(taken && !ss->pending_load_campaign() && taken->difficulty == 2 && taken->tech == -1);
        // DISABLE_REINFORCEMENTS (Underworld only).
        CHECK(!ss->reinforcements_enabled(pUnder) && ss->reinforcements_enabled(pEmpire));
        // DISABLE_RETREAT (ui2 t06): P1 faction, P2 flag; Underworld only.
        CHECK(!ss->retreat_enabled(pUnder) && ss->retreat_enabled(pEmpire));
        // Galactic DESTROY_OBJECT / CHANGE_OWNER (first instance): Barracks gone, SB1 handed over.
        CHECK(gs->store().find_all_of_type(static_cast<objects::TypeIndex>(tBarracks)).empty());
        for (const ObjectHandle h : gs->store().find_all_of_type(static_cast<objects::TypeIndex>(tSB1))) CHECK(gs->store().owner(h) == pUnder);
        {  // TRIGGER_AI: the flag is set for (Underworld, planet C); the one without a faction is dropped.
            const int32_t loc = static_cast<int32_t>(gs->store().id(gs->planets()[rC].object));
            CHECK(!ss->model(0).check_ai_flag(pUnder, "Flag_X", 0, false));
            CHECK(ss->model(0).check_ai_flag(pUnder, "Flag_X", loc, true) && !ss->model(0).check_ai_flag(pUnder, "Flag_X", loc, false));
            CHECK(!ss->model(0).check_ai_flag(pUnder, "Flag_Y", 0, false));
        }
        {  // storyb t10: a TRIGGER_AI without a planet is found by Check_Story_Flag with a nil location (-1)
            CHECK(ss->model(0).check_ai_flag(pUnder, "Flag_Z", -1, true) && !ss->model(0).check_ai_flag(pUnder, "Flag_Z", 0, false));
        }
        // Save: a deterministic image; hash identical across worker counts.
        ByteWriter w1, w2;
        ss->write(w1);
        ss->write(w2);
        CHECK(w1.bytes() == w2.bytes());
        StateHasher h1;
        ss->hash(h1);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h1.digest()));
        if (first_hash.empty()) first_hash = buf;
        CHECK(first_hash == buf);
        {  // ui2 t06: the DISABLE_RETREAT switch survives a save (v5) and moves the story hash (a 0 alone would not).
            ByteReader rd(w1.bytes());
            CHECK(ss->read(rd));
            CHECK(!ss->retreat_enabled(pUnder) && ss->retreat_enabled(pEmpire));
            StateHasher h2;
            ss->hash(h2);
            CHECK(h2.digest() == h1.digest());
        }
        std::printf("  sty1 rewards: workers %d ok, story hash %s\n", workers, buf);
    }
    std::error_code ec;
    remove_all_long(root, ec);
}

// A restricted destination refuses a move (sim level, no story needed).
static void test_sty1_gate_refusals() {
    std::unique_ptr<DirectoryProvider> files;
    TypesWorld w;
    const fs::path root = make_root("alamo_sim_story_rewards_gates", files, w);
    jobs::JobSystem js(jobs::JobSystemConfig{0});
    SimConfig cfg;
    SimHost host(w.types, js, cfg);
    const GalacticRules rules_keep = rules();
    GalacticSim* gs = host.start_galactic(rules_keep, setup());
    CHECK(gs != nullptr);
    if (!gs) return;
    host.tick_once();
    const uint32_t fleet = fleet_of(*gs, pUnder);
    CHECK(fleet != 0xFFFFFFFFu);
    if (fleet == 0xFFFFFFFFu) return;
    StoryGates g;
    g.set(rB, StoryGates::kRestricted, true);
    gs->set_story_gates(g);
    CHECK(gs->move_fleet(pUnder, fleet, rB) != RejectReason::None);
    g.set(rB, StoryGates::kRestricted, false);
    gs->set_story_gates(g);
    CHECK(gs->move_fleet(pUnder, fleet, rB) == RejectReason::None);
    std::error_code ec;
    remove_all_long(root, ec);
}


// Every Session-owned table row reaches a handler (the apply_reward chain notes "has no handler" for one that does not).
static void test_session_rows_have_handlers() {
    std::unique_ptr<DirectoryProvider> files;
    TypesWorld w;
    const fs::path root = make_root("alamo_sim_story_rewards_rows", files, w);
    std::string plot = "<Story>\n <Event Name=\"Begin\"><Event_Type>STORY_ELAPSED</Event_Type><Event_Param1>0</Event_Param1></Event>\n";
    size_t n = 0;
    for (const StoryRewardRow& row : story_reward_table()) {
        if (row.owner != StoryRewardOwner::Session) continue;
        const std::string name = "E" + std::to_string(n++);
        plot += ev(name.c_str(), std::string(row.name).c_str(), {});
    }
    plot += ev("EUnknown", "STORYB_NOT_A_REWARD", {});  // storyb t12: an unknown Reward_Type is logged, not silent
    plot += "</Story>\n";
    write_file(root / "Data/XML/Story_Sty1.xml", plot);
    jobs::JobSystem js(jobs::JobSystemConfig{0});
    SimConfig cfg;
    SimHost host(w.types, js, cfg);
    const GalacticRules rules_keep = rules();
    GalacticSim* gs = host.start_galactic(rules_keep, setup());
    CHECK(gs != nullptr);
    if (!gs) return;
    StoryStart st;
    st.files = files.get();
    st.plot_sets = {{pEmpire, "Story_Plots_Sty1.xml"}};
    host.tick_once();
    CHECK(host.start_story(st, nullptr) && host.story());
    if (!host.story()) return;
    for (int t = 0; t < 10; ++t) host.tick_once();
    size_t no_handler = 0;
    for (const std::string& m : host.story()->lua_messages())
        if (m.find("has no handler") != std::string::npos) {
            ++no_handler;
            std::printf("    %s\n", m.c_str());
        }
    CHECK(n > 40 && no_handler == 0);
    size_t unknown = 0;
    for (const std::string& m : host.story()->lua_messages())
        unknown += m.find("reward STORYB_NOT_A_REWARD unknown") != std::string::npos ? 1u : 0u;
    CHECK(unknown == 1);
    std::error_code ec;
    remove_all_long(root, ec);
}

// mcx6 (docs/re/STORY_REWARDS_GALACTIC.md §1.10 - §1.21): the former Missing rewards in the galactic mode, their save (v6)
// and hash, at 0 and 4 workers. The battle side (FORCE_RETREAT accepted / refused, SET_HEALTH, SET_SPAWN, SET_PLANET_SPAWN)
// is in test_battle_tru1.cpp.
static std::string mcx6_plot() {
    std::string p = "<Story>\n <Event Name=\"Begin\"><Event_Type>STORY_ELAPSED</Event_Type><Event_Param1>0</Event_Param1></Event>\n";
    // PICK_PLANET: the first planet (row order) hostile to the local player (Empire) is PlanetD (the Underworld's); the
    // placeholder in a later event's Reward_Param3 becomes PlanetD (upper-case compare).
    p += ev("Pick", "PICK_PLANET", {"FILTER_ENEMY_ONLY", "placeholder_x"});
    p += ev("Use_Pick", "STATISTIC_CHANGE", {"CREDIT_VALUE", "77", "Placeholder_X"}, "Pick");
    p += ev("Pick_None", "PICK_PLANET", {"FILTER_ENEMY_ONLY"});  // P2 missing: silent
    // STATISTIC_CHANGE: political control and max starbase level of PlanetA; no planet at all: logged.
    p += ev("Pol_A", "STATISTIC_CHANGE", {"POLITICAL_CONTROL", "3", "PlanetA"});
    p += ev("Sb_A", "STATISTIC_CHANGE", {"MAX_STARBASE_LEVEL", "2", "PlanetA"});
    p += ev("Stat_None", "STATISTIC_CHANGE", {"CREDIT_VALUE", "5"});
    // Galactic-mode FORCE_RETREAT / SET_SPAWN / SET_HEALTH / SET_WEATHER: logs; SET_HEALTH writes galactic health.
    p += ev("Retreat_Gal", "FORCE_RETREAT", {"Underworld"});
    p += ev("Retreat_None", "FORCE_RETREAT", {});
    p += ev("Retreat_Bad", "FORCE_RETREAT", {"Nobody"});
    p += ev("Spawn_Gal", "SET_SPAWN", {"T_HUT"});
    p += ev("Health_Gal", "SET_HEALTH", {"Corvette", "50"});
    p += ev("Health_Bad", "SET_HEALTH", {"No_Such_Type", "50"});
    p += ev("Weather", "SET_WEATHER", {});
    p += ev("Weather_Ok", "SET_WEATHER", {"Rain"});
    p += ev("Info", "INFORMATION", {"anything"});
    p += ev("Sfx", "SFX", {"Unit_Select_SFX"});
    // Story-manager flags and planet lists.
    p += ev("No_Sabotage", "ENABLE_SABOTAGE", {"0"});
    p += ev("No_Cine", "ENABLE_COMBAT_CINEMATIC", {"0"});
    p += ev("No_Bm_B", "RESTRICT_BLACK_MARKET", {"PlanetB"});
    p += ev("No_Ab_C", "RESTRICT_ALL_ABILITIES", {"PlanetC", "1"});
    p += ev("Spawn_B1", "SET_PLANET_SPAWN", {"PlanetB", "T_HUT", "0.25"});
    p += ev("Spawn_B2", "SET_PLANET_SPAWN", {"PlanetB", "T_HUT", "0.75"}, "Spawn_B1");  // the same key: overwritten
    p += ev("Spawn_D", "SET_PLANET_SPAWN", {"PlanetD", "T_HUT", "1.0"});
    p += ev("Random", "RANDOM_STORY", {});
    // SWITCH_SIDES last: one step to the Underworld (an AI player: the human flags stay), then back to the Empire by name.
    p += ev("Switch_1", "SWITCH_SIDES", {}, "Use_Pick");
    p += ev("Switch_2", "SWITCH_SIDES", {"Empire"}, "Switch_1");
    return p + "</Story>\n";
}

static void test_mcx6_rewards() {
    std::unique_ptr<DirectoryProvider> files;
    TypesWorld w;
    const fs::path root = make_root("alamo_sim_story_rewards_mcx6", files, w);
    write_file(root / "Data/XML/Story_Sty1.xml", mcx6_plot());
    std::string first_hash;
    for (int32_t workers : {0, 4}) {
        jobs::JobSystem js(jobs::JobSystemConfig{workers});
        SimConfig cfg;
        cfg.seed = 7;
        cfg.story_crc = true;
        SimHost host(w.types, js, cfg);
        const GalacticRules rules_keep = rules();
        GalacticSim* gs = host.start_galactic(rules_keep, setup());
        CHECK(gs != nullptr);
        if (!gs) return;
        CHECK(gs->local_player() == pEmpire);
        StoryStart st;
        st.files = files.get();
        st.plot_sets = {{pEmpire, "Story_Plots_Sty1.xml"}};
        host.tick_once();
        CHECK(host.start_story(st, nullptr) && host.story());
        StorySession* ss = host.story();
        if (!ss) return;
        const size_t plots_before = ss->model(0).plot_count();
        for (int t = 0; t < 20; ++t) host.tick_once();
        auto logged = [ss](std::string_view what) {
            size_t n = 0;
            for (const std::string& m : ss->lua_messages()) n += m.find(what) != std::string::npos ? 1u : 0u;
            return n;
        };
        // PICK_PLANET + STATISTIC_CHANGE CREDIT_VALUE through the substituted placeholder.
        CHECK(logged("Replacing all instances of PLACEHOLDER_X with planet PlanetD") == 1);
        CHECK(gs->planets()[rD].base_value == 77);
        CHECK(ss->definition_edits().size() >= 1 && ss->definition_edits()[0].kind == 1 && ss->definition_edits()[0].b == "PlanetD");
        // STATISTIC_CHANGE POLITICAL_CONTROL / MAX_STARBASE_LEVEL; none: logged.
        CHECK(gs->story_gates().stat(gs->story_gates().political_control, rA) == 3);
        {  // PlanetB keeps the map's value (absent / negative -> 5)
            const auto& msb = gs->map().planets.max_space_base;
            CHECK(gs->max_starbase_level(rA) == 2 && gs->max_starbase_level(rB) == (rB < msb.size() && msb[rB] >= 0 ? msb[rB] : 5));
        }
        CHECK(logged("Reward Statistic Change - No planet specified") == 1);
        // The galactic mode's FORCE_RETREAT / SET_SPAWN / SET_HEALTH / SET_WEATHER.
        CHECK(logged("Force retreat failed due to no retreat coordinator") == 1);
        CHECK(logged("Force retreat failed due to incomplete parameters") == 1);
        CHECK(logged("Force retreat failed due to invalid faction Nobody") == 1);
        CHECK(logged("Set spawn failed due to incomplete parameters") == 1);
        CHECK(logged("Set health of CORVETTE, to 50 percent") == 1 && logged("Can't find type NO_SUCH_TYPE") == 1);
        CHECK(logged("Set weather failed due to incomplete parameters") == 1);
        CHECK(logged("not implemented") == 0);  // no reward of the plot is skipped
        // Flags and planet lists (the sim's copy too).
        const StoryGates& g = gs->story_gates();
        CHECK(!g.sabotage_enabled && !g.combat_cinematic);
        CHECK(g.has(rB, StoryGates::kNoBlackMarket) && g.has(rC, StoryGates::kNoAbilities) && !g.has(rB, StoryGates::kNoAbilities));
        CHECK(g.planet_spawn.size() == 2);
        if (g.planet_spawn.size() == 2) {
            CHECK(g.planet_spawn[0].planet == rB && g.planet_spawn[0].key == "T_HUT" && g.planet_spawn[0].chance == 0.75f);
            CHECK(g.planet_spawn[1].planet == rD && g.planet_spawn[1].crc == crc32("T_HUT", 5));
        }
        // ENABLE_SABOTAGE off: the human's sabotage is refused (the AI's path does not read it).
        CHECK(gs->sabotage_execute_structure(pEmpire, rD, ObjectHandle{}, ObjectHandle{}, 0.0f, 0.0f) == RejectReason::NotAllowed);
        // RANDOM_STORY: Random_Plot_1 joined the local player's plot set (1 or 2 STORY_CONSTRUCT triggers on Corvette).
        CHECK(ss->random_story_count() == 1 && ss->model(0).plot_count() == plots_before + 1);
        if (ss->model(0).plot_count() == plots_before + 1) {
            const lua::StoryPlot& rp = ss->model(0).plot(static_cast<uint32_t>(plots_before));
            CHECK(rp.name == "Random_Plot_1" && rp.active && rp.events.size() >= 3);
            CHECK(!rp.events.empty() && rp.events[0].name == "RANDOM_TRIGGER_0" && rp.events[0].type == "STORY_CONSTRUCT");
            CHECK(!rp.events.empty() && rp.events.back().reward_type == "CREDITS");
        }
        // SWITCH_SIDES: Underworld (has an AI: the human flags stay), then back to the Empire (no AI: the only human).
        CHECK(logged("Switched to player index 1") == 1 && logged("Switched to player index 0") == 1);
        CHECK(gs->local_player() == pEmpire && gs->players()[pEmpire].human && !gs->players()[pUnder].human);
        // Save v6: a deterministic image; read back in place keeps the hash.
        ByteWriter w1, w2;
        ss->write(w1);
        ss->write(w2);
        CHECK(w1.bytes() == w2.bytes());
        StateHasher h1;
        ss->hash(h1);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h1.digest()));
        if (first_hash.empty()) first_hash = buf;
        CHECK(first_hash == buf);
        {
            ByteReader rd(w1.bytes());
            CHECK(ss->read(rd));
            StateHasher h2;
            ss->hash(h2);
            CHECK(h2.digest() == h1.digest());
            CHECK(!gs->story_gates().sabotage_enabled && gs->story_gates().planet_spawn.size() == 2);
        }
        {  // ... and into a freshly started session: the RANDOM_STORY plot and the substitution are replayed.
            SimHost host2(w.types, js, cfg);
            GalacticSim* gs2 = host2.start_galactic(rules_keep, setup());
            CHECK(gs2 != nullptr);
            if (!gs2) return;
            host2.tick_once();
            CHECK(host2.start_story(st, nullptr) && host2.story());
            StorySession* s2 = host2.story();
            if (!s2) return;
            ByteReader rd(w1.bytes());
            CHECK(s2->read(rd));
            CHECK(s2->model(0).plot_count() == plots_before + 1 && s2->definition_edits() == ss->definition_edits());
            CHECK(s2->random_story_count() == 1 && s2->gates().max_starbase == ss->gates().max_starbase);
            bool substituted = false;
            for (uint32_t p = 0; p < s2->model(0).plot_count(); ++p)
                for (const lua::StoryEventDef& e : s2->model(0).plot(p).events)
                    if (e.name == "Use_Pick") substituted = e.reward_params.size() > 2 && e.reward_params[2].text == "PlanetD";
            CHECK(substituted);
        }
        std::printf("  mcx6 rewards: workers %d ok, story hash %s\n", workers, buf);
    }
    std::error_code ec;
    remove_all_long(root, ec);
}

// ---------------------------------------------------------------------------------------------- coverage
static void grab_tag(const std::string& t, const char* tag, std::set<std::string>& out) {
    const std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
    size_t pos = 0;
    while ((pos = t.find(open, pos)) != std::string::npos) {
        const size_t b = pos + open.size(), e = t.find(close, b);
        if (e == std::string::npos) break;
        std::string v = t.substr(b, e - b);
        v.erase(0, v.find_first_not_of(" \t\r\n"));
        v.erase(v.find_last_not_of(" \t\r\n") + 1);
        for (char& c : v) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (!v.empty()) out.insert(v);
        pos = e;
    }
}

static void test_coverage() {
    const char* foc = std::getenv("ALAMO_FOC_DATA");
    const char* eaw = std::getenv("ALAMO_EAW_DATA");
    if (!foc && !eaw) {
        std::printf("  sty1 coverage: ALAMO_FOC_DATA / ALAMO_EAW_DATA unset, skipped\n");
        return;
    }
    std::set<std::string> rewards, generics;
    size_t xml_files = 0;
    for (const char* base : {foc, eaw}) {
        if (!base) continue;
        vfs::VfsConfig cfg;
        cfg.foc_root = base;
        cfg.eaw_root = std::string();  // this game's own files only
        std::string err;
        auto vfs_files = vfs::Vfs::create(cfg, &err);
        CHECK(vfs_files != nullptr);
        if (!vfs_files) continue;
        for (const std::string& path : vfs_files->list("Data\\XML\\*.XML")) {
            std::vector<uint8_t> bytes;
            if (!vfs_files->read(path, bytes)) continue;
            const std::string t(bytes.begin(), bytes.end());
            ++xml_files;
            if (t.find("Reward_Type") == std::string::npos && t.find("STORY_GENERIC") == std::string::npos) continue;
            grab_tag(t, "Reward_Type", rewards);
            // STORY_GENERIC events: the Event_Param1 names (listed for lane C, which owns their raising).
            size_t pos = 0;
            while ((pos = t.find("STORY_GENERIC", pos)) != std::string::npos) {
                const size_t end_ev = t.find("</Event>", pos);
                const size_t p1 = t.find("<Event_Param1>", pos);
                if (p1 != std::string::npos && end_ev != std::string::npos && p1 < end_ev) {
                    const size_t b = p1 + 14, e = t.find("</Event_Param1>", b);
                    if (e != std::string::npos) {
                        std::string v = t.substr(b, e - b);
                        for (char& c : v) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        if (!v.empty()) generics.insert(v);
                    }
                }
                pos += 13;
            }
        }
    }
    CHECK(xml_files > 100);
    std::printf("  sty1 coverage: %zu Reward_Type names, %zu STORY_GENERIC names in the shipped XML\n", rewards.size(), generics.size());
    size_t missing = 0;
    for (const std::string& r : rewards) {
        const StoryRewardRow* row = find_story_reward(r);
        if (!row) {
            ++missing;
            std::printf("    MISSING %s\n", r.c_str());
            continue;
        }
        CHECK(row->owner != StoryRewardOwner::Missing);  // mcx0 t05: base data uses no reward the port lacks
        const std::string_view owner = story_reward_owner_name(row->owner);
        std::printf("    %-34s %-8.*s %.*s\n", r.c_str(), static_cast<int>(owner.size()), owner.data(),
                    static_cast<int>(row->note.size()), row->note.data());
    }
    CHECK(missing == 0);
    std::printf("    STORY_GENERIC names (raised by lane C / the UI):");
    for (const std::string& g : generics) std::printf(" %s", g.c_str());
    std::printf("\n");
}

// mcx0 t05: the story ledger. Every name of retail's Reward_Type enum has a row; every row is a retail name or a Retail
// (ignored) one; the Missing rows are exactly the not-yet-implemented rewards (the list only shrinks: mcx6). Events: one
// row per retail event type, codes unique.
static void test_story_ledger() {
    size_t retail = 0, missing = 0;
    for (const RetailStoryReward& r : retail_story_rewards()) {
        ++retail;
        const StoryRewardRow* row = find_story_reward(r.name);
        if (!row) std::printf("    retail reward without a ledger row: %.*s\n", static_cast<int>(r.name.size()), r.name.data());
        CHECK(row != nullptr);
    }
    CHECK(retail == 140);
    for (const StoryRewardRow& row : story_reward_table()) {
        if (row.owner == StoryRewardOwner::Missing) ++missing;
        if (row.owner != StoryRewardOwner::Retail) CHECK(find_retail_story_reward(row.name) != nullptr);
    }
    CHECK(missing == 0);  // mcx6: every retail reward is implemented (the list started at 18, docs/MOD_COMPAT_DESIGN.md §9.3)
    CHECK(find_story_reward("SWITCH_SIDES") && find_story_reward("SWITCH_SIDES")->owner == StoryRewardOwner::Session);
    CHECK(find_story_reward("INFORMATION") && find_story_reward("INFORMATION")->owner == StoryRewardOwner::Retail);
    for (const char* ui : {"SFX", "FLASH_TERRAIN", "FLASH_UNIT", "DUAL_FLASH"})
        CHECK(find_story_reward(ui) && find_story_reward(ui)->owner == StoryRewardOwner::Ui);
    CHECK(find_story_reward("ENABLE_OVERWHELMING_ODDS") && find_story_reward("ENABLE_OVERWHELMING_ODDS")->owner == StoryRewardOwner::Retail);
    CHECK(find_retail_story_reward("no_such_reward") == nullptr && find_retail_story_reward("credits") != nullptr);
    std::set<uint8_t> codes;
    for (const StoryEventRow& e : story_event_table()) CHECK(codes.insert(e.code).second);
    CHECK(story_event_table().size() == 60);
    const StoryEventRow* win = find_story_event("story_win_battles");
    CHECK(win && win->state == StoryEventState::Partial && !win->note.empty());
    const StoryEventRow* inc = find_story_event("STORY_CORRUPTION_INCREASED");
    CHECK(inc && inc->state == StoryEventState::Retail);
    std::printf("  story ledger: %zu retail rewards, %zu missing; %zu event types\n", retail, missing, story_event_table().size());
}

}  // namespace story_test

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    log::set_min_level(log::Level::Error);
    story_test::test_sty1_gate_refusals();
    story_test::test_sty1_rewards();
    story_test::test_session_rows_have_handlers();
    story_test::test_mcx6_rewards();
    story_test::test_coverage();
    story_test::test_story_ledger();
    if (g_fail) std::printf("FAILED\n");
    else std::printf("story rewards tests passed\n");
    return g_fail ? 1 : 0;
}
