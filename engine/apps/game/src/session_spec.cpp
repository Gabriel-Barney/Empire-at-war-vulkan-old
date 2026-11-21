// ref2 t03: the SessionSpec builder (session_spec.hpp).
#include "alamo/game/session_spec.hpp"

#include <algorithm>

namespace alamo::game {

int32_t clamp_difficulty(int32_t difficulty) { return std::clamp(difficulty, 0, 2); }

const char* difficulty_name(int32_t difficulty) { return kDifficultyNames[static_cast<size_t>(clamp_difficulty(difficulty))]; }

SessionOptions session_options_of(const SessionSpec& s) {
    SessionOptions o;
    o.seed = s.seed;
    o.battle_max_frames = s.battle_max_frames;
    o.battle_end_hold_frames = s.battle_end_hold_frames;
    o.aggressor_period = s.aggressor_period;
    o.battle_order_interval = s.battle_order_interval;
    o.tactical_ai = s.tactical_ai;
    o.animation = s.animation;
    o.galactic_ai = s.galactic_ai;
    o.story = s.story;
    o.ask_battle_choice = s.ask_battle_choice;
    o.fleet_groups = s.fleet_groups;
    return o;
}

CampaignOptions campaign_options_of(const SessionSpec& s) {
    CampaignOptions c;
    c.set = s.game_options;
    c.credits = s.start_credits;
    c.start_tech = s.start_tech;
    c.max_tech = s.max_tech;
    c.auto_resolve = s.auto_resolve_option;
    c.random_events = s.random_events;
    return c;
}

namespace {

void apply_options(SessionSpec& s, const SessionOptions& o) {
    s.seed = o.seed;
    s.battle_max_frames = o.battle_max_frames;
    s.battle_end_hold_frames = o.battle_end_hold_frames;
    s.aggressor_period = o.aggressor_period;
    s.battle_order_interval = o.battle_order_interval;
    s.tactical_ai = o.tactical_ai;
    s.animation = o.animation;
    s.galactic_ai = o.galactic_ai;
    s.story = o.story;
    s.ask_battle_choice = o.ask_battle_choice;
    s.fleet_groups = o.fleet_groups;
}

void apply_campaign_options(SessionSpec& s, const CampaignOptions& c) {
    if (!c.set) return;  // the campaign's own Starting_Credits / tech levels (SessionSpec defaults)
    s.game_options = true;
    s.start_credits = c.credits;
    s.start_tech = c.start_tech;
    s.max_tech = c.max_tech;
    s.auto_resolve_option = c.auto_resolve;
    s.random_events = c.random_events;
}

void apply_skirmish(SessionSpec& s, const SessionChoice& c) {
    s.kind = SessionKind::Skirmish;
    s.skirmish_map = c.map;
    s.skirmish = c.skirmish;
    s.skirmish_options = c.skirmish_options;
}

}  // namespace

SessionSpec make_session_spec(const SessionChoice& c, const SessionOptions& o) {
    SessionSpec s;
    apply_options(s, o);
    s.difficulty = clamp_difficulty(c.difficulty);
    switch (c.kind) {
        case GameKind::Campaign:
            s.kind = SessionKind::Campaign;
            s.campaign = c.campaign;
            s.faction = c.faction;
            apply_campaign_options(s, c.game_options);
            break;
        case GameKind::StoryCampaign:
            s.kind = SessionKind::Campaign;
            s.campaign = c.campaign;
            s.faction = c.faction;
            s.difficulty = c.difficulty >= 0 ? clamp_difficulty(c.difficulty) : 1;
            apply_campaign_options(s, c.game_options);
            s.interventions_off = c.interventions_off;
            break;
        case GameKind::Skirmish:
            apply_skirmish(s, c);
            s.ask_battle_choice = false;
            break;
        case GameKind::Attract:
            apply_skirmish(s, c);
            s.attract = true;
            s.ask_battle_choice = false;
            s.fleet_groups = false;
            s.battle_end_hold_frames = 0;
            s.seed = o.seed + static_cast<uint32_t>(c.attract_start);
            // The controller reloads the demo at the end of its cycle, so the battle limit lies past it.
            s.battle_max_frames = static_cast<uint32_t>(std::max(c.attract_cycle_seconds, 60.0) * 30.0) + 300u;
            break;
        case GameKind::Saved:
            if (c.saved_kind == SessionKind::Campaign) {
                s.kind = SessionKind::Campaign;
                s.campaign = c.campaign;
            } else {
                apply_skirmish(s, c);
                s.skirmish_hop = c.skirmish_hop;
            }
            s.faction = c.faction;
            s.interventions_off = c.interventions_off;
            break;
    }
    return s;
}

}  // namespace alamo::game
