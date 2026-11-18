// L1 systems (owner: tactical-systems agent): the faction / GameConstants loaders of the tactical systems, defined in
// tactical_battle_l1.cpp and tactical_battle_l1_strikes.cpp (target alamo_sim_battle). Split out of campaign_battles.hpp
// (WF1b t01) so those battle sources no longer include the campaign layer; campaign_battles.hpp includes this header.
// cu5 t01: the faction loaders read the definitions `rules` holds (FactionRules::xml_fields, list order, a later
// definition's non-empty tag wins), not FactionFiles.xml again.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "alamo/data/game_database.hpp"
#include "alamo/galactic/galactic_rules.hpp"
#include "alamo/sim/tactical_battle.hpp"

namespace alamo::sim {

// Space_Retreat_Unit_Increment_Wait_Frames of every faction (by GalacticRules faction index; unset = 0) for
// CampaignBattleOptions::retreat_increment_frames.
std::vector<int32_t> load_retreat_increments(const galactic::GalacticRules& rules);
std::vector<float> load_retreat_flight_increments(const galactic::GalacticRules& rules);  // unset = 500 [port]
TacticalBattle::L1Constants load_l1_constants(const data::GameConstants& constants);  // missing tags keep the defaults
std::vector<TacticalBattle::L1Faction> load_l1_factions(const galactic::GalacticRules& rules);
// skm1 t02 (SKIRMISH_SETUP §0, 0x4acc60): the skirmish game options at their GameConstants defaults - MP_Default_Credits,
// MP_Default_Allow_Heroes / _Free_Starting_Units / _Allow_SuperWeapons / _Pre_Built_Base, and the win condition 0 (the
// map kind's MP_Default_Land_ / Space_Tactical_Win_Condition). `set` is true. Missing tags keep the BattleConfig defaults.
BattleConfig::SkirmishOptions skirmish_option_defaults(const data::GameConstants& constants);
// mcx4 t14 (SKIRMISH_SETUP §0, 0x4ad000): the win condition a skirmish battle registers: `want` when the kind's set
// holds it (land 0x12..0x14, space 0x12 / 0x16), else the kind's default (`land_default` / `space_default`) stepped
// to the next valid value of the set, wrapping to the lowest [M: step order].
uint8_t skirmish_win_condition(uint8_t want, bool space, uint8_t land_default, uint8_t space_default);
// A skirmish victory type as GameConstants writes it: a number, or a name of the 0x341ff0 table (SKIRMISH_SETUP §4:
// 0x12 SKIRMISH_ALL_ENEMY_UNITS_DESTROYED .. 0x16 SPACE_ENEMY_STARBASE_DESTROYED; FoC's XML adds a SKIRMISH_ prefix to
// some, so it is optional; case-insensitive). -1 when neither.
int32_t parse_skirmish_victory_type(std::string_view text);

}  // namespace alamo::sim
