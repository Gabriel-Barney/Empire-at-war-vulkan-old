// STY1: where every story Reward_Type of the shipped FoC / EaW story XML is handled (the exit test's coverage table).
//
// One row per reward name the shipped data uses. `owner` says which code applies it:
//   Session  StorySession::apply_reward (galactic / tactical game state: this lane's rewards and the older ones)
//   Model    lua::StoryModel::apply_reward (story-control rewards: flags, objectives, branches, events)
//   Ui       the game host's presentation of a fired event (apps/game story_ui: movies, speech, flashes, locks, camera)
//   Other    another STY1 lane / an object-state setter owned elsewhere (named in `note`)
//   Retail   retail itself ignores the name (a misspelling in the shipped data, an unused enum); the port ignores it too
// The coverage test (tests/test_story_rewards.cpp) reads the shipped XML, looks each name up here and fails for a name with no row.
#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace alamo::sim {

enum class StoryRewardOwner : uint8_t { Session, Model, Ui, Other, Retail };

struct StoryRewardRow {
    std::string_view name;  // Reward_Type, upper case
    StoryRewardOwner owner;
    std::string_view note;  // handler / where the rest lives
};

std::span<const StoryRewardRow> story_reward_table();
// nullptr when the name has no row (case-insensitive).
const StoryRewardRow* find_story_reward(std::string_view name);
std::string_view story_reward_owner_name(StoryRewardOwner owner);

}  // namespace alamo::sim
