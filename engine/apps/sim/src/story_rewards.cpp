// STY1: the coverage table of the story rewards (story_rewards.hpp). Sorted by name; one row per Reward_Type the shipped
// FoC / EaW story XML uses, plus the reward names the port handles that no shipped file uses.
#include "alamo/sim/story_rewards.hpp"

namespace alamo::sim {

namespace {
using O = StoryRewardOwner;
constexpr StoryRewardRow kRewards[] = {
    {"ADD_OBJECTIVE", O::Model, "lua::StoryModel::apply_reward"},
    {"BOMBARD_DELAY", O::Session, "STY1 t06: tactical reward actions through the battle host"},
    {"BUILDABLE_UNIT", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"CHANGE_OWNER", O::Session, "STY1 t06: tactical reward actions through the battle host"},
    {"COMMANDBAR_MOVIE", O::Ui, "game host media cues (story_ui) or presentation only"},
    {"CREDITS", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"DESTROY_OBJECT", O::Session, "STY1 t06: tactical reward actions through the battle host"},
    {"DISABLE_AUTORESOLVE", O::Session, "StorySession::apply_reward (galactic reveal / corruption / autoresolve)"},
    {"DISABLE_BOUNTY_COLLECTION", O::Session, "STY1 t05: galactic::StoryGates"},
    {"DISABLE_BRANCH", O::Model, "lua::StoryModel::apply_reward"},
    {"DISABLE_BUILDABLE", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"DISABLE_DIRECT_INVASION", O::Session, "STY1 t02: StorySession::apply_reward + GalacticSim story_ops"},
    {"DISABLE_EVENT", O::Session, "STY1 t04: StorySession tutorial state"},
    {"DISABLE_MOVIES", O::Ui, "game host media cues (story_ui) or presentation only"},
    {"DISABLE_REINFORCEMENTS", O::Session, "STY1 t06: tactical reward actions through the battle host"},
    {"DISABLE_RETREAT", O::Session, "ui2 t06: per-player switch the campaign host copies into the battle; retreat_prevented refuses the request"},
    {"DISABLE_STORY_EVENT", O::Model, "lua::StoryModel::apply_reward"},
    {"DISABLE_STROY_EVENT", O::Retail, "misspelling in the shipped data: retail has no such reward and ignores it"},
    {"ENABLE_AUTORESOLVE", O::Session, "StorySession::apply_reward (galactic reveal / corruption / autoresolve)"},
    {"ENABLE_BOUNTY_COLLECTION", O::Session, "STY1 t05: galactic::StoryGates"},
    {"ENABLE_BUILDABLE", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"ENABLE_CAMPAIGN_VICTORY_MOVIE", O::Session, "STY1 t05: galactic::StoryGates"},
    {"ENABLE_EVENT", O::Session, "STY1 t04: StorySession tutorial state"},
    {"ENABLE_FLEET_COMBINE", O::Session, "STY1 t05: galactic::StoryGates"},
    {"ENABLE_FOW", O::Session, "StorySession::apply_reward (battle / galactic monitor hooks)"},
    {"ENABLE_GALACTIC_CORRUPTION_HOLOGRAM", O::Ui, "ui2 t06: StoryPresenter keeps the planet set (what the hologram shows is unknown, [L]); no sim state"},
    {"ENABLE_GALACTIC_REVEAL", O::Session, "StorySession::apply_reward (galactic reveal / corruption / autoresolve)"},
    {"ENABLE_INVASION", O::Session, "STY1 t05: galactic::StoryGates"},
    {"ENABLE_OBJECTIVE_DISPLAY", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"ENABLE_VICTORY", O::Session, "StorySession::apply_reward (battle / galactic monitor hooks)"},
    {"FINISHED_TUTORIAL", O::Session, "STY1 t04: StorySession tutorial state"},
    {"FLASH_ADVANCED_MAP_OBJECT", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"FLASH_FLEET_WITH_UNIT", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"FLASH_GUI", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"FLASH_PLANET_GUI", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"FLASH_PRODUCTION_CHOICE", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"FLASH_SPECIAL_ABILITY", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"FORCE_CLICK_GUI", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"FORCE_RESPAWN", O::Session, "STY1 t02: StorySession::apply_reward + GalacticSim story_ops"},
    {"GIVE_BLACK_MARKET", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"HIDE_AUTORESOLVE", O::Ui, "ui2 t06: dead in retail (no reader, STORY_PRESENTATION_REWARDS 5.3); StoryPresenter keeps the flag"},
    {"HIDE_CURSOR_ON_CLICK", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"HIDE_RAID_SLOT", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"HIDE_SMUGGLE_SLOT", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"HIDE_STEAL_SLOT", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"HIDE_TUTORIAL_CURSOR", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"HIGHLIGHT_OBJECT", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"INCREMENT_FLAG", O::Model, "lua::StoryModel::apply_reward"},
    {"INVADE_PLANET", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"LINK_TACTICAL", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"LOAD_CAMPAIGN", O::Session, "STY1 t04: StorySession tutorial state"},
    {"LOCK_CONTROLS", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"LOCK_PLANET_SELECTION", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"LOCK_UNIT", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"MOVE_FLEET", O::Session, "STY1 t02: StorySession::apply_reward + GalacticSim story_ops"},
    {"MULTIMEDIA", O::Ui, "game host media cues (story_ui) or presentation only"},
    {"NEW_POWER_FOR_ALL", O::Session, "STY1 t02: StorySession::apply_reward + GalacticSim story_ops"},
    {"OBJECTIVE_COMPLETE", O::Model, "lua::StoryModel::apply_reward"},
    {"OBJECTIVE_FAILED", O::Model, "lua::StoryModel::apply_reward"},
    {"PLANET_FACTION", O::Session, "STY1 t02: StorySession::apply_reward + GalacticSim story_ops"},
    {"POSITION_CAMERA", O::Ui, "camera / cinematic presentation (ui lanes); no sim state"},
    {"REMOVE_ALL_OBJECTIVES", O::Model, "lua::StoryModel::apply_reward"},
    {"REMOVE_CORRUPTION", O::Session, "StorySession::apply_reward (galactic reveal / corruption / autoresolve)"},
    {"REMOVE_OBJECTIVE", O::Model, "lua::StoryModel::apply_reward"},
    {"REMOVE_POWER_FROM_ALL", O::Session, "STY1 t02: StorySession::apply_reward + GalacticSim story_ops"},
    {"REMOVE_STORY_GOAL", O::Model, "lua::StoryModel::apply_reward"},
    {"REMOVE_UNIT", O::Session, "STY1 t02: StorySession::apply_reward + GalacticSim story_ops"},
    {"RESET_BRANCH", O::Model, "lua::StoryModel::apply_reward"},
    {"RESET_EVENT", O::Model, "lua::StoryModel::apply_reward"},
    {"RESET_GALACTIC_FILTERS", O::Session, "STY1 t04: StorySession tutorial state"},
    {"RESTRICT_AUTORESOLVE", O::Session, "StorySession::apply_reward (galactic reveal / corruption / autoresolve)"},
    {"RESTRICT_CORRUPTION", O::Session, "STY1 t05: galactic::StoryGates"},
    {"RESTRICT_SABOTAGE", O::Session, "STY1 t05: galactic::StoryGates"},
    {"REVEAL_ALL_PLANETS", O::Session, "StorySession::apply_reward (galactic reveal / corruption / autoresolve)"},
    {"REVEAL_PLANET", O::Session, "StorySession::apply_reward (galactic reveal / corruption / autoresolve)"},
    {"SABOTAGE_STRUCTURE", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"SCREEN_TEXT", O::Ui, "game host media cues (story_ui) or presentation only"},
    {"SCROLL_CAMERA", O::Ui, "camera / cinematic presentation (ui lanes); no sim state"},
    {"SCROLL_LOCK", O::Ui, "camera / cinematic presentation (ui lanes); no sim state"},
    {"SELECT_PLANET", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"SET_ADVISOR", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"SET_FLAG", O::Model, "lua::StoryModel::apply_reward"},
    {"SET_MAX_TECH_LEVEL", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"SET_PLANET_RESTRICTED", O::Session, "STY1 t02: StorySession::apply_reward + GalacticSim story_ops"},
    {"SET_PLANET_SPAWN", O::Session, "STY1 t02: StorySession::apply_reward + GalacticSim story_ops"},
    {"SET_PLANET_VISIBILITY_LEVEL", O::Session, "StorySession::apply_reward (galactic reveal / corruption / autoresolve)"},
    {"SET_SANDBOX_OBJECTIVES", O::Session, "STY1 t04: StorySession tutorial state"},
    {"SET_TACTICAL_MAP", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"SET_TECH_LEVEL", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"SHOW_COMMAND_BAR", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"SHOW_RAID_SLOT", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"SHOW_SMUGGLE_SLOT", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"SHOW_SPECIAL_SLOT", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"SHOW_STEAL_SLOT", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"SKIRMISH_RULES", O::Session, "STY1 t04: StorySession tutorial state"},
    {"SPAWN_HERO", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"SPEECH", O::Ui, "game host media cues (story_ui) or presentation only"},
    {"START_CINEMATIC_MODE", O::Ui, "camera / cinematic presentation (ui lanes); no sim state"},
    {"START_MOVIE", O::Ui, "game host media cues (story_ui) or presentation only"},
    {"STOP_CINEMATIC_MODE", O::Ui, "camera / cinematic presentation (ui lanes); no sim state"},
    {"STOP_COMMANDBAR_MOVIE", O::Ui, "game host media cues (story_ui) or presentation only"},
    {"STOP _COMMANDBAR_MOVIE", O::Retail, "misspelling (a space) in the shipped data: retail has no such reward and ignores it"},
    {"STORY_ELEMENT", O::Model, "lua::StoryModel::apply_reward"},
    {"STORY_GOAL_COMPLETED", O::Model, "lua::StoryModel::apply_reward"},
    {"SWITCH_CONTROL", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"TRIGGER_AI", O::Session, "STY1 §5.x: AI story flag (Check_Story_Flag) raised on the plot models"},
    {"TRIGGER_EVENT", O::Model, "lua::StoryModel::apply_reward"},
    {"TUTORIAL_DIALOG", O::Ui, "presentation / HUD (ui lanes); no sim state"},
    {"TUTORIAL_PLAYER", O::Session, "STY1 t04: StorySession tutorial state"},
    {"UNIQUE_UNIT", O::Session, "StorySession::apply_reward (earlier groups)"},
    {"UPDATE_OBJECTIVE", O::Model, "lua::StoryModel::apply_reward"},
    {"USE_RETRY_DIALOG", O::Ui, "ui2 t06: marks the retry dialog as the end-of-battle choice; StoryPresenter keeps the flag"},
    {"VICTORY", O::Session, "StorySession::apply_reward (battle / galactic monitor hooks)"},
    {"ZOOM_IN", O::Ui, "camera / cinematic presentation (ui lanes); no sim state"},
    {"ZOOM_OUT", O::Ui, "camera / cinematic presentation (ui lanes); no sim state"},
};
char upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }
}  // namespace

std::span<const StoryRewardRow> story_reward_table() { return kRewards; }

const StoryRewardRow* find_story_reward(std::string_view name) {
    for (const StoryRewardRow& r : kRewards) {
        if (r.name.size() != name.size()) continue;
        bool same = true;
        for (size_t i = 0; i < name.size() && same; ++i) same = upper(name[i]) == r.name[i];
        if (same) return &r;
    }
    return nullptr;
}

std::string_view story_reward_owner_name(StoryRewardOwner owner) {
    switch (owner) {
    case StoryRewardOwner::Session: return "session";
    case StoryRewardOwner::Model: return "model";
    case StoryRewardOwner::Ui: return "ui";
    case StoryRewardOwner::Other: return "other";
    case StoryRewardOwner::Retail: return "retail-ignored";
    }
    return "?";
}

}  // namespace alamo::sim
