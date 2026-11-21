// alamo_game session spec (ref2 t03): what a GameSession is built from, and the one builder that assembles it.
//
//   SessionSpec        every field GameSession::start reads (campaign / skirmish choice, sim switches, host options).
//   SessionChoice      what the player (or the story, the save, the attract controller) chose: the game kind and its
//                      kind's fields.
//   SessionOptions     the host's run options every session of one run shares (alamo_game: --seed, --battle-max-frames,
//                      --fleet-groups; the benchmark save: --bench-seed). session_options_of(spec) reads them back.
//   make_session_spec  choice + options -> SessionSpec. The front end's campaign / tutorial and skirmish, the attract demo,
//                      a story LOAD_CAMPAIGN reward, a loaded save (GameSession::spec_from_save) and the benchmark save
//                      all go through it, so a new spec field (gal1 t05's per-session seed, gai1 t06's difficulty names)
//                      is set in one place. Tests may still fill a SessionSpec by hand.
//   difficulty_name    difficulty 0..2 -> the DifficultyAdjustments row name; the one table of those names.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "alamo/game/game_data.hpp"
#include "alamo/sim/tactical_battle.hpp"

namespace alamo::game {

enum class SessionKind : uint8_t { Campaign = 0, Skirmish = 1 };

struct SessionSpec {
    SessionKind kind = SessionKind::Campaign;
    // Campaign: CampaignDef name and the human faction ("" = Starting_Active_Player), difficulty 0..2.
    std::string campaign, faction;
    int32_t difficulty = 1;
    // Skirmish: space map and the galaxy (factions / fleets).
    std::string skirmish_map;
    SkirmishSetup skirmish;
    std::shared_ptr<const terrain::MapData> skirmish_map_data;  // tests: used instead of loading skirmish_map
    // skm1 t01: a skirmish starts straight on its map (CampaignBattles::start_map_battle with BattleConfig::skirmish_map,
    // as alamo_headless --skirmish-ai skirmish and the retail skirmish start, SKIRMISH_SETUP §2): both slots' starting
    // forces (when skirmish_options.starting_units) are its units, the skirmish map rules apply (Income_Stream, per-slot
    // victory, team markers, the option credits). [port, tests only] skirmish_hop = the older scaffold kept for the
    // galactic conflict tests: the human fleet hops to the AI's planet and the battle is that conflict's (battle choice,
    // auto-resolve, retreat), without the skirmish map rules. The front end never sets it. Saved.
    bool skirmish_hop = false;
    // skm1 t02: the skirmish game options (the staging screen's IDD_INTERNET_STAGING_DIALOG_ADVANCED_SKIRMISH_OPTIONS);
    // set = false: the GameConstants MP_Default_* values (sim::skirmish_option_defaults). Saved (map battle only).
    sim::BattleConfig::SkirmishOptions skirmish_options;
    // Sim.
    uint32_t seed = 1;
    uint32_t aggressor_period = 600;  // CampaignAggressor for the AI players (0 = off)
    uint32_t battle_max_frames = 27000;
    uint32_t battle_end_hold_frames = 0;  // BattleConfig::end_hold_frames (alamo_game sets it; 0 for tests and the attract demo)
    uint32_t battle_order_interval = 60; // S10 stand-in cadence for AI sides without a tactical AI (human units skipped)
    bool tactical_ai = true;             // A1 BattleAi for the AI players of human battles (needs AI data)
    bool animation = true;               // SimAnimation (needs engine/anim)
    bool galactic_ai = true;             // A2 GalacticAiWorld for the AI players (else / without AI data: CampaignAggressor)
    bool story = true;                   // S4 StorySession for campaigns with story plots (attached to the galactic AI)
    // The pending-battle choice of a human conflict (fight / auto-resolve / retreat): false = always Fight (headless,
    // tests); true = the conflict waits (ConflictManager Vote state) for a battle_choice_event in the command stream.
    bool ask_battle_choice = false;
    // OI host: the main-menu attract demo (docs/re/MAIN_LOOP.md §1.5 / AUDIO.md §1.6 "AI-vs-AI attract demo") [port]: a
    // space skirmish with no human - the skirmish's player 0 is AI controlled too, AI-vs-AI conflicts are fought (both
    // sides by the tactical AI, else the stand-in cadence) and player 0's fleet hop is a direct setup operation (no
    // command). local_player() stays 0 (the viewpoint). Space maps only (a land map fails start()). The demo's objects run
    // their Lua_Scripts (object-script host on, modc t18): a DEMO_CONTROLLER runs Demo_Attract_Controller.lua.
    bool attract = false;
    // ui1 t06: the campaign's advanced game options (ui::GameOptionsSetup; game_options false = the campaign's own
    // Starting_Credits / tech levels). Credits, start and max tech apply to every player the campaign names; the
    // auto-resolve option (0 always / 1 never / 2 ask / 3 overwhelming odds) is the host's battle prompt rule; game_options
    // with random events off makes the galactic AI's Game.InterventionsEnabled 0.0 (mcx8 t07). Not saved [port]: a loaded
    // game asks again (ref4 t01: the battle prompt reads auto_resolve_option only when game_options is set, which a load
    // leaves false, so a loaded game falls back to "ask"; whether retail keeps the option across a load is not traced).
    bool game_options = false;
    int32_t start_credits = 6000, start_tech = 1, max_tech = 5, auto_resolve_option = 2;
    bool random_events = false;
    // mcx8 t07: Game.InterventionsEnabled was off in the saved game (save key spec.interventions_off; the options above are
    // not saved). interventions_enabled() = the galactic AI's Game.InterventionsEnabled.
    bool interventions_off = false;
    bool interventions_enabled() const { return !interventions_off && (!game_options || random_events); }
    // [port] fleet groups (docs/DECISIONS.md "Fleet groups: exception to the current focus"): off = retail. Copied into
    // CampaignBattleOptions::fleet_groups; the HUD hotkeys send fleet group commands only while it is on.
    bool fleet_groups = false;
};

// [port] Playtest QA D1: alamo_game holds a won battle 5 s before the summary (the last break-up plays).
inline constexpr uint32_t kBattleEndHoldFrames = 150;

// Difficulty 0..2 -> DifficultyAdjustments name ([port] Easy_Default / Normal_Default / Hard_Default); out of range clamps.
inline constexpr std::array<const char*, 3> kDifficultyNames = {"Easy_Default", "Normal_Default", "Hard_Default"};
const char* difficulty_name(int32_t difficulty);
int32_t clamp_difficulty(int32_t difficulty);  // 0..2

// The host's run options (see the top). Defaults = a SessionSpec's, except the end hold (alamo_game's).
struct SessionOptions {
    uint32_t seed = 1;
    uint32_t battle_max_frames = 27000;
    uint32_t battle_end_hold_frames = kBattleEndHoldFrames;
    uint32_t aggressor_period = 600;
    uint32_t battle_order_interval = 60;
    bool tactical_ai = true, animation = true, galactic_ai = true, story = true;
    // Campaigns only: the Fight / Auto-resolve / Retreat box. A skirmish map battle has no conflict to choose (off).
    bool ask_battle_choice = true;
    bool fleet_groups = false;
};
SessionOptions session_options_of(const SessionSpec& s);

// ui1 t06: the campaign's advanced game options (IDD_CAMPAIGN_ADVANCED_OPTIONS_DIALOG accepted = set).
struct CampaignOptions {
    bool set = false;
    int32_t credits = 6000, start_tech = 1, max_tech = 5, auto_resolve = 2;
    bool random_events = false;
};
CampaignOptions campaign_options_of(const SessionSpec& s);

enum class GameKind : uint8_t {
    Campaign,       // a campaign or tutorial from the front end (asks the battle choice when options say so)
    Skirmish,       // a skirmish map battle (front end staging -> buy units; the benchmark save)
    Attract,        // the main-menu AI-vs-AI demo: seed + start count, frames from the cycle, no end hold, no fleet groups
    StoryCampaign,  // modc t45: a story LOAD_CAMPAIGN reward (options = the running game's, session_options_of)
    Saved,          // a loaded save: every field from the save (saved_kind says campaign or skirmish)
};

struct SessionChoice {
    GameKind kind = GameKind::Campaign;
    // Campaign kinds: CampaignDef name and the human faction ("" = Starting_Active_Player). Saved: faction for both kinds.
    std::string campaign, faction;
    int32_t difficulty = 1;            // clamped to 0..2; StoryCampaign: negative = 1 (the reward's P3 unset)
    CampaignOptions game_options;      // Campaign, StoryCampaign
    bool interventions_off = false;    // StoryCampaign, Saved (mcx8 t07)
    // Skirmish kinds: the map and the slots (factions already resolved by the caller, or "" = the data's default).
    std::string map;
    SkirmishSetup skirmish;
    sim::BattleConfig::SkirmishOptions skirmish_options;
    bool skirmish_hop = false;         // Saved: an older save's galactic-hop skirmish
    // Attract: the controller's start count (added to the seed) and its cycle delay in seconds.
    uint64_t attract_start = 0;
    double attract_cycle_seconds = 0.0;
    SessionKind saved_kind = SessionKind::Campaign;  // Saved
};

SessionSpec make_session_spec(const SessionChoice& choice, const SessionOptions& options);

}  // namespace alamo::game
