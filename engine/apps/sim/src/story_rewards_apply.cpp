// STY1 lane B: the story rewards that change campaign rules, the galaxy and the tutorial (StorySession::apply_sty1_reward),
// and the save / hash blocks of the state they keep. Behaviour: docs/re/STORY_REWARDS_GALACTIC.md (read from the retail
// dispatcher 0x456970); port choices are marked [port] / [M] / [L] as there. Kept in its own file so the older reward chain in
// story_session.cpp stays small.
//
// Planet parameters are planet TYPE names (first instance, STORY_REWARDS_GALACTIC §0). Numbers are read with atoi.
// Determinism: serial; vectors in write order; no RNG.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "alamo/core/ascii.hpp"
#include "alamo/core/bytes.hpp"
#include "alamo/core/hash.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/galactic/campaign.hpp"
#include "alamo/galactic/galactic_rules.hpp"
#include "alamo/galactic/special_data.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/objects/object_store.hpp"
#include "alamo/sim/story_session.hpp"

namespace alamo::sim {

namespace {

using galactic::StoryGates;

constexpr int32_t kGuiAll = 5;

// Reward_Param text of the tutorial gate's kind enum (table 0x00a29140): exact, case-sensitive.
int32_t gui_kind(std::string_view name) {
    if (name == "TUTORIAL_CLICK_GUI") return 1;
    if (name == "TUTORIAL_CLICK_PLANET") return 2;
    if (name == "TUTORIAL_DRAG_FLEET") return 3;
    if (name == "TUTORIAL_ZOOM") return 4;
    if (name == "TUTORIAL_ALL") return kGuiAll;
    return 0;
}

bool record_matches(const StoryGuiRecord& r, int32_t kind, std::string_view n1, std::string_view n2) {
    return r.kind == kind && (r.name1.empty() || (r.name1 == n1 && r.name2 == n2));
}

int32_t atoi_text(const std::string& s) { return static_cast<int32_t>(std::atoi(s.c_str())); }

std::string trimmed(const std::vector<std::string>& v, size_t i) { return i < v.size() ? std::string(ascii_trim(v[i])) : std::string(); }

void write_records(ByteWriter& w, const std::vector<StoryGuiRecord>& v) {
    w.u32(static_cast<uint32_t>(v.size()));
    for (const StoryGuiRecord& r : v) w.i32(r.kind).str(r.name1).str(r.name2);
}

bool read_records(ByteReader& r, std::vector<StoryGuiRecord>& v) {
    const uint32_t n = r.u32();
    if (n > (1u << 20)) r.fail();
    for (uint32_t i = 0; i < n && r.ok(); ++i) {
        StoryGuiRecord g;
        g.kind = r.i32();
        g.name1 = r.str();
        g.name2 = r.str();
        v.push_back(std::move(g));
    }
    return r.ok();
}

}  // namespace

bool StorySession::retreat_enabled(int32_t player) const {
    return player < 0 || static_cast<size_t>(player) >= retreat_off_.size() || retreat_off_[static_cast<size_t>(player)] == 0;
}

bool StorySession::reinforcements_enabled(int32_t player) const {
    return player < 0 || static_cast<size_t>(player) >= reinforce_off_.size() || reinforce_off_[static_cast<size_t>(player)] == 0;
}

bool StorySession::gui_event_allowed(int32_t kind, std::string_view name1, std::string_view name2) const {
    const StoryTutorialState& t = tutorial_;
    if (t.all_allowed) {
        for (const StoryGuiRecord& r : t.blocked)
            if (record_matches(r, kind, name1, name2)) return false;
        return true;
    }
    if (kind == 1 && (name1 == "b_option_g" || name1 == "b_option_t" || name1 == "b_story_arc_g" || name1 == "b_story_arc_t")) return true;
    for (const StoryGuiRecord& r : t.allowed)
        if (record_matches(r, kind, name1, name2)) return true;
    return false;
}

std::optional<StoryLoadCampaign> StorySession::take_load_campaign() {
    std::optional<StoryLoadCampaign> out = std::move(load_campaign_);
    load_campaign_.reset();
    return out;
}

PlanetIndex StorySession::reward_planet(const std::string& type_name) const {
    const std::string_view want = ascii_trim(type_name);
    if (want.empty()) return kInvalidPlanet;
    for (PlanetIndex i = 0; i < sim_->planets().size(); ++i)
        if (sim_->planets()[i].is_planet && ascii_iequals(planet_type_name(i), want)) return i;
    return kInvalidPlanet;
}

bool StorySession::apply_sty1_reward(int32_t plot_player, bool tactical, const std::string& type,
                                     const std::vector<std::string>& params) {
    const std::string p1 = trimmed(params, 0), p2 = trimmed(params, 1), p3 = trimmed(params, 2);
    const bool given2 = params.size() > 1 && !p2.empty();
    // Planet-bound rule rewards (§3): P2 absent = 1.
    auto planet_flag = [&](uint8_t bit, bool invert) {
        const PlanetIndex row = reward_planet(p1);
        if (row == kInvalidPlanet) {
            note(type + " failed due to non-existent planet '" + p1 + "'");
            return;
        }
        const bool flag = given2 ? atoi_text(p2) != 0 : true;
        gates_.set(row, bit, invert ? !flag : flag);
        sync_gates();
    };
    const auto global_flag = [&](bool& field) {
        field = !p1.empty() ? atoi_text(p1) != 0 : true;
        sync_gates();
    };

    // ---- galactic (§1) ----
    if (type == "SET_PLANET_RESTRICTED") {
        // §1.1: P1 planet and P2 flag required; the optional ability-list / look flags (P3, P4) have no port effect [port].
        if (p1.empty() || !given2) {
            note("SET_PLANET_RESTRICTED failed due to incomplete parameters");
            return true;
        }
        const PlanetIndex row = reward_planet(p1);
        if (row == kInvalidPlanet) {
            note("SET_PLANET_RESTRICTED failed due to non-existant planet " + p1);
            return true;
        }
        gates_.set(row, StoryGates::kRestricted, atoi_text(p2) != 0);
        // mcx6 (§1.1, §1.21): P3 atoi != 0 puts the planet in the all-abilities list; absent or 0 takes it out.
        gates_.set(row, StoryGates::kNoAbilities, !p3.empty() && atoi_text(p3) != 0);
        sync_gates();
    } else if (type == "PLANET_FACTION") {
        // §1.2: the planet's full Change_Owner (GalacticSim::change_owner, the Lua Change_Owner path).
        if (p1.empty() || p2.empty()) {
            note("PLANET_FACTION failed due to incomplete parameters");
            return true;
        }
        const PlanetIndex row = reward_planet(p1);
        const int32_t player = sim_->player_of_faction(p2);
        if (row == kInvalidPlanet) note("PLANET_FACTION failed due to non-existant planet " + p1);
        else if (player < 0) note("PLANET_FACTION failed due to invalid faction " + p2);
        else sim_->change_owner(sim_->planets()[row].object, player);
    } else if (type == "FORCE_RESPAWN") {
        // §1.3: the queued respawn record(s) of the hero type become due now; nothing is created by the reward.
        const uint32_t t = p1.empty() ? galactic::kNoIndex : sim_->rules().find_type(p1);
        if (p1.empty()) note("FORCE_RESPAWN failed due to incomplete parameters");
        else if (t == galactic::kNoIndex) note("FORCE_RESPAWN failed due to invalid hero unit type " + p1);
        else sim_->force_hero_respawn(t);
    } else if (type == "REMOVE_UNIT") {
        // §1.4: silent removal, any owner; any non-empty P2 = all instances. Unknown type: nothing, no log (retail).
        const uint32_t t = p1.empty() ? galactic::kNoIndex : sim_->rules().find_type(p1);
        if (t != galactic::kNoIndex) sim_->story_remove_units(t, given2);
    } else if (type == "NEW_POWER_FOR_ALL" || type == "REMOVE_POWER_FROM_ALL") {
        // §1.5: the plot player's (type, ability) table; the table value changes only when it differs (retail logs "may
        // already have been granted / revoked"). Units of the type read it through GalacticSim::ability_ready.
        const bool revoke = type == "REMOVE_POWER_FROM_ALL";
        const uint32_t t = p1.empty() ? galactic::kNoIndex : sim_->rules().find_type(p1);
        if (p1.empty() || p2.empty()) {
            note(type + " failed due to incomplete parameters");
        } else if (t == galactic::kNoIndex || plot_player < 0) {
            note("Unable to " + std::string(revoke ? "remove power " : "reward ") + p1 + " with power " + p2 + ". Unknown object type");
        } else {
            uint32_t crc = galactic::special_name_crc(p2);
            for (const galactic::SpecialAbilityDef& a : sim_->rules().special.abilities)  // the ability as the data spells it
                if (ascii_iequals(a.name, p2)) crc = a.name_crc;
            StoryGates::Power* hit = nullptr;
            for (StoryGates::Power& e : gates_.powers)
                if (e.player == plot_player && e.type == t && e.name_crc == crc) hit = &e;
            if (hit && hit->revoked == revoke) {
                note(p1 + ": power " + p2 + " may already have been " + (revoke ? "revoked" : "granted"));
            } else if (hit) {
                hit->revoked = revoke;
            } else {
                StoryGates::Power e;
                e.player = plot_player;
                e.type = t;
                e.name_crc = crc;
                e.revoked = revoke;
                gates_.powers.push_back(e);
            }
            sync_gates();
        }
    } else if (type == "MOVE_FLEET") {
        // §1.6: all three parameters required (silent otherwise). Only FILTER_FRIENDLY_ONLY (1) and FILTER_ENEMY_ONLY (3)
        // select fleets; they merge and travel along a found route (no teleport).
        if (p1.empty() || p2.empty() || p3.empty()) return true;
        const PlanetIndex from = reward_planet(p1), to = reward_planet(p2);
        if (from == kInvalidPlanet) note("Unable to find planet " + p1);
        else if (to == kInvalidPlanet) note("Unable to find planet " + p2);
        else {
            const uint32_t filter = p3 == "FILTER_FRIENDLY_ONLY" ? 1u : p3 == "FILTER_ENEMY_ONLY" ? 3u : 0u;
            if (sim_->story_move_fleets(plot_player, from, to, filter) != 0) note("Moving fleet from " + p1 + " to " + p2);
        }
    } else if (type == "DISABLE_DIRECT_INVASION" || type == "ENABLE_DIRECT_INVASION") {
        // §1.7: a global flag (default enabled). Its retail reader was not found [L]: the port refuses the Invade command.
        gates_.direct_invasion_disabled = type == "DISABLE_DIRECT_INVASION";
        sync_gates();
    } else if (type == "SET_PLANET_SPAWN") {
        // §1.8: P1 planet, P2 spawner type key (as written), P3 chance; all required (shipped data gives only P3: a no-op).
        // The entry is read at every later battle start at that planet (TacticalBattle::sp_planet_spawn).
        if (p1.empty() || p2.empty() || p3.empty()) {
            note("Set planet spawn failed due to incomplete parameters");
            return true;
        }
        const PlanetIndex row = reward_planet(p1);
        if (row == kInvalidPlanet) {
            note("Set planet spawn failed due to non-existant planet " + p1);  // [port] retail's lookup message not read
            return true;
        }
        StoryGates::PlanetSpawn e;
        e.planet = row;
        e.key = p2;
        e.crc = crc32(p2.data(), p2.size());
        e.chance = static_cast<float>(std::atof(p3.c_str()));
        auto& v = gates_.planet_spawn;
        const auto at = std::find_if(v.begin(), v.end(), [&e](const StoryGates::PlanetSpawn& x) {
            return x.planet > e.planet || (x.planet == e.planet && x.crc >= e.crc);
        });
        if (at != v.end() && at->planet == e.planet && at->crc == e.crc) *at = std::move(e);  // an existing key is overwritten
        else v.insert(at, std::move(e));
        sync_gates();
    } else if (type == "SET_WEATHER") {
        // §1.14: a retail no-op; only an empty P1 is logged.
        if (p1.empty()) note("Set weather failed due to incomplete parameters");
    } else if (type == "SWITCH_SIDES") {
        // §1.10: P1 optional faction. The port is single player (retail does nothing in a multiplayer game type).
        const int32_t before = sim_->local_player();
        const int32_t now = sim_->story_switch_sides(p1);
        if (now >= 0 && now != before) note("PlayerListClass::Switch_Sides: Switched to player index " + std::to_string(now));
    } else if (type == "STATISTIC_CHANGE") {
        // §1.17: P1 statistic, P2 value (atoi), both required (silent); P3 planet type (first instance), else the planet of
        // the notification that fired the event [M]; none: logged.
        if (p1.empty() || p2.empty()) return true;
        PlanetIndex row = p3.empty() ? kInvalidPlanet : reward_planet(p3);
        if (row == kInvalidPlanet && !reward_firing_planet_.empty()) row = reward_planet(reward_firing_planet_);
        if (row == kInvalidPlanet) {
            note("Reward Statistic Change - No planet specified");
            return true;
        }
        const int32_t value = atoi_text(p2);
        // Statistic enum (table 0x00a29140), exact names; unknown = 0 (nothing).
        if (p1 == "CREDIT_VALUE") {
            sim_->story_set_base_value(row, value);  // + the owner's player byte +0x181 := 1: no reader found [L], not kept
        } else if (p1 == "POLITICAL_CONTROL") {
            gates_.set_stat(gates_.political_control, row, value);
            sync_gates();
        } else if (p1 == "MAX_STARBASE_LEVEL") {
            gates_.set_stat(gates_.max_starbase, row, value);
            sync_gates();
        }  // MAX_SPECIAL_STRUCTS: nothing; MAX_GROUND_BASE_LEVEL: an empty function in retail
    } else if (type == "FORCE_RETREAT" || type == "SET_HEALTH" || type == "SET_SPAWN" || type == "DISABLE_SPECIAL_STRUCTURE") {
        // §1.11 - §1.13, §1.20: they act on the current mode. A running battle: the battle host applies them
        // (CampaignBattles::story_tactical_reward); the galactic mode: as below.
        if (type == "FORCE_RETREAT") {
            if (p1.empty()) {
                note("Force retreat failed due to incomplete parameters");
                return true;
            }
            if (sim_->rules().find_faction(p1) == galactic::kNoIndex) {
                note("Force retreat failed due to invalid faction " + p1);
                return true;
            }
            if (sim_->player_of_faction(p1) < 0) {
                note("Force retreat failed due to no player of faction " + p1);
                return true;
            }
        } else if (type == "SET_SPAWN") {
            if (p1.empty() || p2.empty()) {
                note("Set spawn failed due to incomplete parameters");
                return true;
            }
        } else if (p1.empty() || p2.empty()) {
            return true;  // SET_HEALTH / DISABLE_SPECIAL_STRUCTURE: silent
        }
        if (tactical_) {
            if (tactical_reward_) tactical_reward_(type, params);
            return true;
        }
        if (type == "FORCE_RETREAT") {
            note("Force retreat failed due to no retreat coordinator");  // the galactic mode has none
        } else if (type == "SET_HEALTH") {
            // Galactic objects: health := clamp(Tactical_Health x P2 / 100) [port: no mode / difficulty / ability factor
            // in the galactic mode; nothing reads a galactic object's health].
            note("Set health of " + ascii_upper(p1) + ", to " + std::to_string(atoi_text(p2)) + " percent");
            const uint32_t ti = sim_->rules().find_type(p1);
            if (ti == galactic::kNoIndex) {
                note("Can't find type " + ascii_upper(p1));
                return true;
            }
            const int32_t c = p3.empty() ? 1 : atoi_text(p3);
            const size_t count = c == 0 ? SIZE_MAX : c < 0 ? 1u : static_cast<size_t>(c);
            objects::ObjectStore& st = *store_;  // the galactic mode's primary store
            const std::vector<objects::ObjectHandle> found = st.find_all_of_type(static_cast<objects::TypeIndex>(ti));
            const float mx = ti < types_->size() ? types_->columns().tactical_health[ti] : 0.0f;
            const float f = static_cast<float>(atoi_text(p2)) / 100.0f;
            size_t n = 0;
            for (auto it = found.rbegin(); it != found.rend() && n < count; ++it)  // creation order
                if (st.alive(*it)) {
                    st.set_health(*it, std::clamp(mx * f, 0.0f, mx));
                    ++n;
                }
        } else if (type == "DISABLE_SPECIAL_STRUCTURE") {
            // No base shield service runs in the galactic mode: the bit has no reader there [port: not stored].
            const uint32_t ti = sim_->rules().find_type(p1);
            const bool any = ti != galactic::kNoIndex &&
                             !sim_->store().find_all_of_type(static_cast<objects::TypeIndex>(ti)).empty();
            if (!any) note("Invalid structure " + p1 + " in DISABLE_SPECIAL_STRUCTURE");
        }  // SET_SPAWN: the galactic mode has no spawner list
    }
    // ---- tutorial (§2) ----
    else if (type == "LOAD_CAMPAIGN") {
        // §2.1: retail stores a pending campaign for the end-of-game flow; the port queues the request (take_load_campaign).
        if (p1.empty() || p2.empty()) {
            note("LOAD_CAMPAIGN requires two parameters: campaign name and side index (0 or 1)");
            return true;
        }
        StoryLoadCampaign l;
        l.frame = frame_;
        l.name = p1;
        l.side = atoi_text(p2);
        l.upper_name = p1;
        ascii_upper_in_place(l.upper_name);
        // modc t45 (§2.1): P3 kept only when 0..2 (difficulty), P4 only when 0..5 (tech level); else -1.
        if (!p3.empty()) {
            const int32_t d = atoi_text(p3);
            if (d >= 0 && d < 3) l.difficulty = d;
        }
        if (const std::string p4 = trimmed(params, 3); !p4.empty()) {
            const int32_t t = atoi_text(p4);
            if (t >= 0 && t < 6) l.tech = t;
        }
        load_campaign_ = std::move(l);
    } else if (type == "TUTORIAL_PLAYER") {
        // §2.2: marks the faction's player (+0x3c0 := 1); never cleared.
        const int32_t player = p1.empty() ? -1 : sim_->player_of_faction(p1);
        if (p1.empty()) note("SET_TUTORIAL_PLAYER needs param1 = player faction");
        else if (player < 0) note("SET_TUTORIAL_PLAYER failed because of unknown player faction " + p1);
        else if (std::find(tutorial_.marked_players.begin(), tutorial_.marked_players.end(), player) == tutorial_.marked_players.end())
            tutorial_.marked_players.push_back(player);
    } else if (type == "FINISHED_TUTORIAL") {
        // §2.3: the name goes to the profile's played-tutorials list once (case-sensitive).
        if (p1.empty()) note("FINISHED_TUTORIAL takes one parameter: the name of the tutorial");
        else if (std::find(tutorial_.finished.begin(), tutorial_.finished.end(), p1) == tutorial_.finished.end())
            tutorial_.finished.push_back(p1);
    } else if (type == "DISABLE_EVENT" || type == "ENABLE_EVENT") {
        // §2.4: kind in P1; names in P2 / P3. TUTORIAL_ALL resets the gate; other records go to the blocked list (disable),
        // to the allowed list (enable in "only listed" mode) or out of the blocked list (enable in "all allowed" mode).
        const int32_t kind = gui_kind(p1);
        if (kind == 0) return true;
        const bool disable = type == "DISABLE_EVENT";
        StoryTutorialState& t = tutorial_;
        if (kind == kGuiAll) {
            t.all_allowed = !disable;
            t.allowed.clear();
            t.blocked.clear();
        } else {
            StoryGuiRecord r;
            r.kind = kind;
            r.name1 = p2;
            r.name2 = p3;
            auto& list = disable ? t.blocked : t.allowed;
            if (disable || !t.all_allowed) {
                if (std::find(list.begin(), list.end(), r) == list.end()) list.push_back(std::move(r));
            } else {
                t.blocked.erase(std::remove(t.blocked.begin(), t.blocked.end(), r), t.blocked.end());
            }
        }
    } else if (type == "SKIRMISH_RULES") {
        if (p1.empty()) note("SKIRMISH_RULES takes one parameter, 0 or 1");
        else tutorial_.skirmish_rules = atoi_text(p1) != 0;
    } else if (type == "RESET_GALACTIC_FILTERS") {
        ++tutorial_.filter_resets;  // §2.6: pure galaxy-GUI state; the host's galaxy screen resets its filters when this moves
    } else if (type == "SET_SANDBOX_OBJECTIVES") {
        ++tutorial_.sandbox_requests;  // §2.7: body not decoded [L]; recorded only
    }
    // ---- Underworld campaign rules (§3) ----
    else if (type == "ENABLE_INVASION") {
        planet_flag(StoryGates::kNoInvasion, true);  // flag 1 = invasion allowed; 0 = disabled there
    } else if (type == "RESTRICT_SABOTAGE") {
        planet_flag(StoryGates::kNoSabotage, false);
    } else if (type == "RESTRICT_CORRUPTION") {
        planet_flag(StoryGates::kNoCorruption, false);
    } else if (type == "ENABLE_FLEET_COMBINE") {
        global_flag(gates_.fleet_combine);
    } else if (type == "ENABLE_BOUNTY_COLLECTION" || type == "DISABLE_BOUNTY_COLLECTION") {
        gates_.bounty_collection = type == "ENABLE_BOUNTY_COLLECTION";
        sync_gates();
    } else if (type == "ENABLE_CAMPAIGN_VICTORY_MOVIE") {
        global_flag(gates_.campaign_victory_movie);
    } else if (type == "ENABLE_SABOTAGE") {
        global_flag(gates_.sabotage_enabled);  // §1.21, §3.7: P1 default 1
    } else if (type == "ENABLE_COMBAT_CINEMATIC") {
        global_flag(gates_.combat_cinematic);  // §1.21, §3.7: P1 default 1; the game host's cinematic-view key reads it
    } else if (type == "RESTRICT_BLACK_MARKET") {
        planet_flag(StoryGates::kNoBlackMarket, false);  // §3.2, §1.21
    } else if (type == "RESTRICT_ALL_ABILITIES") {
        planet_flag(StoryGates::kNoAbilities, false);  // §3.2, §1.21
    }
    // ---- tactical (§4) ----
    else if (type == "DISABLE_REINFORCEMENTS") {
        // §4.2: P1 atoi != 0 disables; P2 optional faction (absent: every player).
        const bool off = atoi_text(p1) != 0;
        if (reinforce_off_.size() < sim_->players().size()) reinforce_off_.resize(sim_->players().size(), 0);
        if (p2.empty()) {
            std::fill(reinforce_off_.begin(), reinforce_off_.end(), static_cast<uint8_t>(off ? 1 : 0));
        } else {
            const int32_t player = sim_->player_of_faction(p2);
            if (player < 0) note(std::string("Unable to ") + (off ? "disable" : "enable") + " reinforcements for player of faction " + p2);
            else reinforce_off_[static_cast<size_t>(player)] = off ? 1 : 0;
        }
    } else if (type == "DISABLE_RETREAT") {
        // ui2 t06 (STORY_PRESENTATION_REWARDS.md 5.2): P1 faction, P2 atoi, both required; a listed faction cannot retreat. Retail
        // keeps a faction list; the port keeps it per player of that faction [port], re-read each hook like the reinforcements.
        if (p1.empty() || p2.empty()) return true;
        const bool off = atoi_text(p2) != 0;
        const int32_t player = sim_->player_of_faction(p1);
        if (player < 0) {
            note("Unable to find player of faction " + p1 + " for DISABLE_RETREAT");
        } else {
            if (retreat_off_.size() < sim_->players().size()) retreat_off_.resize(sim_->players().size(), 0);
            retreat_off_[static_cast<size_t>(player)] = off ? 1 : 0;
        }
    } else if (type == "CHANGE_OWNER" || type == "DESTROY_OBJECT" || type == "BOMBARD_DELAY") {
        if (tactical) {
            if (tactical_reward_) tactical_reward_(type, params);  // the battle host applies it to the battle's objects
            return true;
        }
        // The galactic mode's object list (§4.1): the same rewards on galactic objects. BOMBARD_DELAY has no galactic target.
        if (type == "BOMBARD_DELAY" || p1.empty()) return true;
        const uint32_t t = sim_->rules().find_type(p1);
        if (t == galactic::kNoIndex) {
            note(type + " failed: unable to find object " + p1);
            return true;
        }
        const std::vector<objects::ObjectHandle> found = sim_->store().find_all_of_type(static_cast<objects::TypeIndex>(t));
        if (type == "DESTROY_OBJECT") {
            if (found.empty()) note("Unable to find object " + p1 + " to destroy");
            else sim_->destroy_object(found.front());
        } else {
            const int32_t player = p2.empty() ? -1 : sim_->player_of_faction(p2);
            if (player < 0) return true;
            const size_t count = p3.empty() ? 1u : atoi_text(p3) <= 0 ? found.size() : static_cast<size_t>(atoi_text(p3));
            for (size_t i = 0; i < found.size() && i < count; ++i) sim_->change_owner(found[i], player);
        }
    } else {
        return false;
    }
    return true;
}

// ---- save / hash --------------------------------------------------------------------------------------------------

void StorySession::write_sty1(ByteWriter& w) const {
    w.u32(static_cast<uint32_t>(gates_.planet.size()));
    for (const uint8_t b : gates_.planet) w.u8(b);
    w.boolean(gates_.direct_invasion_disabled).boolean(gates_.fleet_combine).boolean(gates_.bounty_collection).boolean(gates_.campaign_victory_movie);
    w.u32(static_cast<uint32_t>(gates_.powers.size()));
    for (const StoryGates::Power& p : gates_.powers) w.i32(p.player).u32(p.type).u32(p.name_crc).boolean(p.revoked);
    w.u32(static_cast<uint32_t>(reinforce_off_.size()));
    for (const uint8_t b : reinforce_off_) w.u8(b);
    const StoryTutorialState& t = tutorial_;
    w.boolean(t.all_allowed);
    write_records(w, t.allowed);
    write_records(w, t.blocked);
    w.u32(static_cast<uint32_t>(t.marked_players.size()));
    for (const int32_t p : t.marked_players) w.i32(p);
    w.u32(static_cast<uint32_t>(t.finished.size()));
    for (const std::string& f : t.finished) w.str(f);
    w.boolean(t.skirmish_rules).u32(t.filter_resets).u32(t.sandbox_requests);
    w.u32(static_cast<uint32_t>(retreat_off_.size()));  // v5
    for (const uint8_t b : retreat_off_) w.u8(b);
    // v6 (mcx6): sabotage / combat-cinematic flags, the planet spawn map, the written planet statistics. (The black-market
    // and all-abilities planet bits ride in gates_.planet above.)
    w.boolean(gates_.sabotage_enabled).boolean(gates_.combat_cinematic);
    w.u32(static_cast<uint32_t>(gates_.planet_spawn.size()));
    for (const StoryGates::PlanetSpawn& e : gates_.planet_spawn) w.u32(e.planet).u32(e.crc).str(e.key).f32(e.chance);
    for (const std::vector<int32_t>* v : {&gates_.political_control, &gates_.max_starbase}) {
        w.u32(static_cast<uint32_t>(v->size()));
        for (const int32_t x : *v) w.i32(x);
    }
}

bool StorySession::read_sty1(ByteReader& r, Sty1Save& out, uint32_t version) {
    constexpr uint32_t kMax = 1u << 20;
    uint32_t n = r.u32();
    if (n > kMax) r.fail();
    for (uint32_t i = 0; i < n && r.ok(); ++i) out.gates.planet.push_back(r.u8());
    out.gates.direct_invasion_disabled = r.boolean();
    out.gates.fleet_combine = r.boolean();
    out.gates.bounty_collection = r.boolean();
    out.gates.campaign_victory_movie = r.boolean();
    n = r.u32();
    if (n > kMax) r.fail();
    for (uint32_t i = 0; i < n && r.ok(); ++i) {
        StoryGates::Power p;
        p.player = r.i32();
        p.type = r.u32();
        p.name_crc = r.u32();
        p.revoked = r.boolean();
        out.gates.powers.push_back(p);
    }
    n = r.u32();
    if (n > kMax) r.fail();
    for (uint32_t i = 0; i < n && r.ok(); ++i) out.reinforce_off.push_back(r.u8());
    StoryTutorialState& t = out.tutorial;
    t.all_allowed = r.boolean();
    if (!read_records(r, t.allowed) || !read_records(r, t.blocked)) return false;
    n = r.u32();
    if (n > kMax) r.fail();
    for (uint32_t i = 0; i < n && r.ok(); ++i) t.marked_players.push_back(r.i32());
    n = r.u32();
    if (n > kMax) r.fail();
    for (uint32_t i = 0; i < n && r.ok(); ++i) t.finished.push_back(r.str());
    t.skirmish_rules = r.boolean();
    t.filter_resets = r.u32();
    t.sandbox_requests = r.u32();
    if (version >= 5) {  // DISABLE_RETREAT (ui2 t06)
        n = r.u32();
        if (n > kMax) r.fail();
        for (uint32_t i = 0; i < n && r.ok(); ++i) out.retreat_off.push_back(r.u8());
    }
    if (version >= 6) {  // mcx6
        out.gates.sabotage_enabled = r.boolean();
        out.gates.combat_cinematic = r.boolean();
        n = r.u32();
        if (n > kMax) r.fail();
        for (uint32_t i = 0; i < n && r.ok(); ++i) {
            StoryGates::PlanetSpawn e;
            e.planet = r.u32();
            e.crc = r.u32();
            e.key = r.str();
            e.chance = r.f32();
            out.gates.planet_spawn.push_back(std::move(e));
        }
        for (std::vector<int32_t>* v : {&out.gates.political_control, &out.gates.max_starbase}) {
            n = r.u32();
            if (n > kMax) r.fail();
            for (uint32_t i = 0; i < n && r.ok(); ++i) v->push_back(r.i32());
        }
    }
    return r.ok();
}

void StorySession::hash_sty1(StateHasher& h) const {
    bool off = false;
    for (const uint8_t b : reinforce_off_) off = off || b != 0;
    for (const uint8_t b : retreat_off_) off = off || b != 0;
    if (gates_.is_default() && tutorial_.is_default() && !off) return;  // runs without these rewards hash as before
    h.u32(0x53545931u);  // "STY1"
    h.u32(static_cast<uint32_t>(gates_.planet.size()));
    for (const uint8_t b : gates_.planet) h.u32(b);
    h.boolean(gates_.direct_invasion_disabled);
    h.boolean(gates_.fleet_combine);
    h.boolean(gates_.bounty_collection);
    h.boolean(gates_.campaign_victory_movie);
    h.u32(static_cast<uint32_t>(gates_.powers.size()));
    for (const StoryGates::Power& p : gates_.powers) {
        h.i32(p.player);
        h.u32(p.type);
        h.u32(p.name_crc);
        h.boolean(p.revoked);
    }
    h.u32(static_cast<uint32_t>(reinforce_off_.size()));
    for (const uint8_t b : reinforce_off_) h.u32(b);
    for (const uint8_t b : retreat_off_) h.u32(b);  // empty (hashes nothing) unless DISABLE_RETREAT ran
    h.boolean(tutorial_.all_allowed);
    h.u32(static_cast<uint32_t>(tutorial_.allowed.size()));
    h.u32(static_cast<uint32_t>(tutorial_.blocked.size()));
    for (const std::vector<StoryGuiRecord>* list : {&tutorial_.allowed, &tutorial_.blocked})
        for (const StoryGuiRecord& r : *list) {
            h.i32(r.kind);
            h.str(r.name1);
            h.str(r.name2);
        }
    h.u32(static_cast<uint32_t>(tutorial_.marked_players.size()));
    for (const int32_t p : tutorial_.marked_players) h.i32(p);
    h.u32(static_cast<uint32_t>(tutorial_.finished.size()));
    for (const std::string& f : tutorial_.finished) h.str(f);
    h.boolean(tutorial_.skirmish_rules);
    h.u32(tutorial_.filter_resets);
    h.u32(tutorial_.sandbox_requests);
    if (!gates_.mcx6_default()) {  // mcx6 (the planet bits are in the planet loop above); runs without them hash as before
        h.u32(0x4d433647u);  // "MC6G"
        h.boolean(gates_.sabotage_enabled);
        h.boolean(gates_.combat_cinematic);
        h.u32(static_cast<uint32_t>(gates_.planet_spawn.size()));
        for (const StoryGates::PlanetSpawn& e : gates_.planet_spawn) {
            h.u32(e.planet);
            h.u32(e.crc);
            h.str(e.key);
            h.f32(e.chance);
        }
        for (const std::vector<int32_t>* v : {&gates_.political_control, &gates_.max_starbase}) {
            h.u32(static_cast<uint32_t>(v->size()));
            for (const int32_t x : *v) h.i32(x);
        }
    }
}

}  // namespace alamo::sim
