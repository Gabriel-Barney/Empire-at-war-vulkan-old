// STY1: where every story Reward_Type of the shipped FoC / EaW story XML is handled (the exit test's coverage table).
//
// One row per reward name the shipped data uses. `owner` says which code applies it:
//   Session  StorySession::apply_reward (galactic / tactical game state: this lane's rewards and the older ones)
//   Model    lua::StoryModel::apply_reward (story-control rewards: flags, objectives, branches, events)
//   Ui       the game host's presentation of a fired event (apps/game story_ui: movies, speech, flashes, locks, camera)
//   Other    another STY1 lane / an object-state setter owned elsewhere (named in `note`)
//   Retail   retail itself ignores the name (a misspelling in the shipped data, an unused enum); the port ignores it too
//   Missing  a name of retail's Reward_Type enum that the port does not implement yet: skipped with a log line
// The coverage test (tests/test_story_rewards.cpp) reads the shipped XML, looks each name up here and fails for a name with no row.
// mcx0 t05 (story ledger, docs/MOD_COMPAT_DESIGN.md §9.3): the table also has a row for every name of retail's enum
// (retail_story_rewards()), and story_event_table() lists retail's event types with how far the port dispatches each.
// The load report (alamo_mod_check v4 `story`) reads both: a mod's Missing reward is a reward_missing issue, a name in
// neither table is retail-equivalent (retail's dispatcher ignores it too).
#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace alamo::sim {

enum class StoryRewardOwner : uint8_t { Session, Model, Ui, Other, Retail, Missing };

struct StoryRewardRow {
    std::string_view name;  // Reward_Type, upper case
    StoryRewardOwner owner;
    std::string_view note;  // handler / where the rest lives
};

std::span<const StoryRewardRow> story_reward_table();
// nullptr when the name has no row (case-insensitive).
const StoryRewardRow* find_story_reward(std::string_view name);
std::string_view story_reward_owner_name(StoryRewardOwner owner);

// retail's Reward_Type enum (the reward records of the story enum table, docs/re/STORY_REWARDS_GALACTIC.md §0.1).
struct RetailStoryReward {
    std::string_view name;  // upper case
    uint8_t code = 0;
};
std::span<const RetailStoryReward> retail_story_rewards();
const RetailStoryReward* find_retail_story_reward(std::string_view name);  // case-insensitive; nullptr if not retail's

// Story event types (docs/re/STORY.md §0.1): Full = dispatched as retail does; Partial = dispatched with a documented
// gap (note); Retail = retail drops the event too.
enum class StoryEventState : uint8_t { Full, Partial, Retail };
struct StoryEventRow {
    std::string_view name;  // "STORY_ENTER", ...
    uint8_t code = 0;
    StoryEventState state = StoryEventState::Full;
    std::string_view note;
};
std::span<const StoryEventRow> story_event_table();
const StoryEventRow* find_story_event(std::string_view name);  // case-insensitive
std::string_view story_event_state_name(StoryEventState state);  // "full", "partial", "retail-dropped"

}  // namespace alamo::sim
