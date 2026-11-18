// mcx6 RANDOM_STORY (docs/re/STORY_REWARDS_GALACTIC.md §1.15): the plot the reward builds from code, as Story XML text.
// Internal to apps/sim (StorySession::random_story fills the input from the galaxy and adds the plot).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "alamo/core/sim_rng.hpp"

namespace alamo::sim {

struct RandomStoryInput {
    std::vector<std::string> triggers;  // Random_Story_Triggers (event type names)
    std::vector<std::string> rewards;   // Random_Story_Rewards (reward type names)
    int32_t max_triggers = 0;           // Random_Story_Max_Triggers
    // The local player's side (Rebel / Empire) lists.
    std::vector<std::string> construction, destroy, buildable, unit;
    struct Planet {
        std::string name;  // planet type name
        int32_t owner = -1;
        int32_t ground_level = 0, ground_max = 0;  // ground base level and its maximum (0x3f8aa0 / 0x372400)
        int32_t space_level = 0, space_max = 0;    // starbase level and its maximum (0x3f8b30 / 0x372780)
    };
    std::vector<Planet> own_planets;    // planets of the local player
    std::vector<Planet> other_planets;  // revealed planets of other owners
    std::vector<std::string> own_heroes, enemy_heroes;  // named hero type names (local player / others)
    int32_t local_player = -1;
    float local_credits = 0.0f;
};

struct RandomStoryPlot {
    std::string name;  // "Random_Plot_<k>"
    std::string xml;   // <Story> text for lua::StoryModel::add_plot
    std::vector<std::string> log;  // the generator's log lines (events, rewards, warnings)
    std::string dialog;            // the story-dialog text ([CHAPTER i], TITLE, IMAGE, DIALOG, TEXT): presentation only
};

// Every draw is on `rng`, in the order the retail builder makes them on its unsynced RNG 0x140a13e20 ([port] the caller
// passes the story session's saved RANDOM_STORY stream).
RandomStoryPlot build_random_story(uint32_t k, const RandomStoryInput& in, SimRng& rng);

}  // namespace alamo::sim
