// ref2 t03 (alamo_game_session_spec_tests): the one SessionSpec builder (session_spec.hpp), data-free, one check per game
// kind: front-end campaign (and its advanced options), front-end skirmish, the benchmark save's skirmish, the attract
// demo, a story LOAD_CAMPAIGN reward, a loaded save; plus the difficulty name table and session_options_of.
#include <cstdint>
#include <cstdio>
#include <string>

#include "alamo/game/session_spec.hpp"
#include "alamo/test/support.hpp"

using namespace alamo;
using ::alamo::test::g_fail;

namespace {

// Host options as alamo_game passes them (--seed 77 --battle-max-frames 9000 --fleet-groups).
game::SessionOptions app_options() {
    game::SessionOptions o;
    o.seed = 77;
    o.battle_max_frames = 9000;
    o.fleet_groups = true;
    return o;
}

void check_options(const game::SessionSpec& s, const game::SessionOptions& o) {
    CHECK(s.seed == o.seed);
    CHECK(s.battle_max_frames == o.battle_max_frames);
    CHECK(s.battle_end_hold_frames == o.battle_end_hold_frames);
    CHECK(s.aggressor_period == o.aggressor_period);
    CHECK(s.battle_order_interval == o.battle_order_interval);
    CHECK(s.tactical_ai == o.tactical_ai && s.animation == o.animation && s.galactic_ai == o.galactic_ai && s.story == o.story);
    CHECK(s.fleet_groups == o.fleet_groups);
}

void test_difficulty_names() {
    CHECK(std::string(game::difficulty_name(0)) == "Easy_Default");
    CHECK(std::string(game::difficulty_name(1)) == "Normal_Default");
    CHECK(std::string(game::difficulty_name(2)) == "Hard_Default");
    CHECK(std::string(game::difficulty_name(-4)) == "Easy_Default");  // out of range clamps
    CHECK(std::string(game::difficulty_name(9)) == "Hard_Default");
    CHECK(game::clamp_difficulty(-1) == 0 && game::clamp_difficulty(1) == 1 && game::clamp_difficulty(3) == 2);
}

void test_campaign() {
    const game::SessionOptions o = app_options();
    game::SessionChoice c;
    c.kind = game::GameKind::Campaign;
    c.campaign = "Sandbox_Empire";
    c.faction = "Empire";
    c.difficulty = 2;
    game::SessionSpec s = game::make_session_spec(c, o);
    CHECK(s.kind == game::SessionKind::Campaign);
    CHECK(s.campaign == "Sandbox_Empire" && s.faction == "Empire" && s.difficulty == 2);
    CHECK(s.ask_battle_choice);  // the front end asks Fight / Auto-resolve / Retreat
    CHECK(s.battle_end_hold_frames == game::kBattleEndHoldFrames);
    CHECK(!s.game_options && s.start_credits == 6000 && s.auto_resolve_option == 2);  // the campaign's own
    CHECK(!s.attract && !s.interventions_off && s.interventions_enabled());
    CHECK(s.skirmish_map.empty());
    check_options(s, o);

    c.game_options.set = true;  // ui1 t06: the advanced options dialog accepted
    c.game_options.credits = 12000;
    c.game_options.start_tech = 2;
    c.game_options.max_tech = 4;
    c.game_options.auto_resolve = 0;
    c.game_options.random_events = false;
    c.difficulty = 7;
    s = game::make_session_spec(c, o);
    CHECK(s.game_options && s.start_credits == 12000 && s.start_tech == 2 && s.max_tech == 4 && s.auto_resolve_option == 0);
    CHECK(!s.random_events && !s.interventions_enabled());  // options with random events off: InterventionsEnabled 0
    CHECK(s.difficulty == 2);                               // clamped
}

void test_skirmish() {
    const game::SessionOptions o = app_options();
    game::SessionChoice c;
    c.kind = game::GameKind::Skirmish;
    c.map = "_SPACE_PLANET_ALDERAAN_01";
    c.difficulty = -3;
    c.skirmish.human_faction = "Rebel";
    c.skirmish.ai_faction = "Empire";
    c.skirmish.difficulties = {0, 2};
    c.skirmish_options.set = true;
    c.skirmish_options.credits = 5000;
    c.faction = "ignored";  // a skirmish has no campaign faction
    const game::SessionSpec s = game::make_session_spec(c, o);
    CHECK(s.kind == game::SessionKind::Skirmish);
    CHECK(s.skirmish_map == "_SPACE_PLANET_ALDERAAN_01" && s.campaign.empty() && s.faction.empty());
    CHECK(s.skirmish.human_faction == "Rebel" && s.skirmish.ai_faction == "Empire" && s.skirmish.difficulties.size() == 2);
    CHECK(s.skirmish_options.set && s.skirmish_options.credits == 5000);
    CHECK(s.difficulty == 0);
    CHECK(!s.ask_battle_choice && !s.skirmish_hop && !s.attract);  // a map battle has no conflict to choose
    check_options(s, o);
}

void test_benchmark_skirmish() {
    // benchmark_save.cpp: a skirmish with alamo_game's default options and the benchmark's seed.
    game::SessionOptions o;
    o.seed = 12345;
    game::SessionChoice c;
    c.kind = game::GameKind::Skirmish;
    c.map = "_LAND_PLANET_TATOOINE_01";
    c.skirmish.ground_human = {"A", "A"};
    const game::SessionSpec s = game::make_session_spec(c, o);
    CHECK(s.seed == 12345 && s.battle_max_frames == 27000 && s.battle_end_hold_frames == 150);
    CHECK(!s.fleet_groups && !s.ask_battle_choice && s.difficulty == 1);
    CHECK(s.skirmish.ground_human.size() == 2 && !s.skirmish_options.set);
}

void test_attract() {
    const game::SessionOptions o = app_options();
    game::SessionChoice c;
    c.kind = game::GameKind::Attract;
    c.map = "_SPACE_PLANET_HOTH_01";
    c.attract_start = 3;
    c.attract_cycle_seconds = 120.0;
    game::SessionSpec s = game::make_session_spec(c, o);
    CHECK(s.kind == game::SessionKind::Skirmish && s.attract);
    CHECK(s.skirmish_map == "_SPACE_PLANET_HOTH_01");
    CHECK(s.seed == 80);                           // the run's seed + the controller's start count
    CHECK(s.battle_max_frames == 120u * 30u + 300u);  // past the cycle (the controller reloads first)
    CHECK(s.battle_end_hold_frames == 0 && !s.fleet_groups && !s.ask_battle_choice);
    c.attract_cycle_seconds = 5.0;  // at least 60 s
    s = game::make_session_spec(c, o);
    CHECK(s.battle_max_frames == 60u * 30u + 300u);
}

void test_story_campaign() {
    // The running game: a campaign with advanced options and InterventionsEnabled off from its save.
    game::SessionChoice first;
    first.kind = game::GameKind::Campaign;
    first.campaign = "FOC_Story_A";
    first.difficulty = 2;
    first.game_options.set = true;
    first.game_options.credits = 9000;
    first.game_options.random_events = true;
    game::SessionSpec running = game::make_session_spec(first, app_options());
    running.interventions_off = true;
    running.tactical_ai = false;  // any run option carries over

    game::SessionChoice c;
    c.kind = game::GameKind::StoryCampaign;
    c.campaign = "FOC_Story_B";
    c.faction = "Underworld";
    c.difficulty = -1;  // the reward's P3 unset
    c.game_options = game::campaign_options_of(running);
    c.interventions_off = running.interventions_off;
    game::SessionSpec s = game::make_session_spec(c, game::session_options_of(running));
    CHECK(s.kind == game::SessionKind::Campaign && s.campaign == "FOC_Story_B" && s.faction == "Underworld");
    CHECK(s.difficulty == 1);
    CHECK(s.game_options && s.start_credits == 9000 && s.random_events);
    CHECK(s.interventions_off && !s.interventions_enabled());
    CHECK(s.ask_battle_choice);
    check_options(s, game::session_options_of(running));
    CHECK(!s.tactical_ai && s.seed == 77 && s.fleet_groups);

    c.difficulty = 0;
    s = game::make_session_spec(c, game::session_options_of(running));
    CHECK(s.difficulty == 0);
}

void test_saved() {
    // A loaded save: every field is the save's (GameSession::spec_from_save fills the choice and options from the
    // descriptor); the end hold, the battle choice and the switches are the saved ones, not alamo_game's defaults.
    game::SessionOptions o;
    o.seed = 5;
    o.battle_end_hold_frames = 0;
    o.ask_battle_choice = false;
    o.galactic_ai = false;
    game::SessionChoice c;
    c.kind = game::GameKind::Saved;
    c.saved_kind = game::SessionKind::Campaign;
    c.campaign = "Sandbox_Rebel";
    c.faction = "Rebel";
    c.difficulty = 0;
    c.interventions_off = true;
    game::SessionSpec s = game::make_session_spec(c, o);
    CHECK(s.kind == game::SessionKind::Campaign && s.campaign == "Sandbox_Rebel" && s.faction == "Rebel" && s.difficulty == 0);
    CHECK(!s.ask_battle_choice && s.battle_end_hold_frames == 0 && !s.galactic_ai && s.seed == 5);
    CHECK(s.interventions_off && !s.game_options);  // the advanced options are not saved
    check_options(s, o);

    c.saved_kind = game::SessionKind::Skirmish;
    c.map = "_SPACE_PLANET_KUAT_01";
    c.campaign.clear();
    c.faction.clear();
    c.skirmish.human_faction = "Empire";
    c.skirmish_hop = true;  // an older save: the galactic-hop skirmish
    o.ask_battle_choice = true;
    s = game::make_session_spec(c, o);
    CHECK(s.kind == game::SessionKind::Skirmish && s.skirmish_map == "_SPACE_PLANET_KUAT_01" && s.campaign.empty());
    CHECK(s.skirmish_hop && s.skirmish.human_faction == "Empire" && !s.skirmish_options.set);
    CHECK(s.ask_battle_choice);  // as saved
}

void test_options_round_trip() {
    game::SessionOptions o = app_options();
    o.aggressor_period = 0;
    o.battle_order_interval = 30;
    o.animation = false;
    o.story = false;
    game::SessionChoice c;
    const game::SessionOptions back = game::session_options_of(game::make_session_spec(c, o));
    CHECK(back.seed == o.seed && back.battle_max_frames == o.battle_max_frames && back.battle_end_hold_frames == o.battle_end_hold_frames);
    CHECK(back.aggressor_period == 0 && back.battle_order_interval == 30 && !back.animation && !back.story && back.tactical_ai);
    CHECK(back.ask_battle_choice && back.fleet_groups);
}

}  // namespace

int main() {
    test_difficulty_names();
    test_campaign();
    test_skirmish();
    test_benchmark_skirmish();
    test_attract();
    test_story_campaign();
    test_saved();
    test_options_round_trip();
    if (g_fail) {
        std::printf("alamo_game_session_spec_tests: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("alamo_game_session_spec_tests: OK\n");
    return 0;
}
