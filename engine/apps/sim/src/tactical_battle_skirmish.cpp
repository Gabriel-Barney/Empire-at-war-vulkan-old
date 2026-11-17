// --- skai (q2f t04): skirmish map battles. A skirmish started straight on its _MP_ map (CampaignBattles::start_map_battle with
// BattleConfig::skirmish, TacticalBattle::skirmish_map): the Income_Stream_Ability credit trickle (docs/re/SKIRMISH_SETUP.md).
// The skirmish victory types 0x13 / 0x16 are the victory monitor's (tactical_battle_victory.cpp, story t06). Serial code, no
// synced draws; credits go through the host's credit hooks.
#include <algorithm>
#include <vector>
#include <cstdlib>
#include <string>
#include <string_view>

#include "alamo/galactic/campaign.hpp"
#include "alamo/core/ascii.hpp"
#include "alamo/objects/behaviors.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/sim/l1_loaders.hpp"
#include "alamo/sim/tactical_battle.hpp"

namespace alamo::sim {

namespace {

constexpr float kFrameSeconds = 1.0f / 30.0f;  // tactical frames are 1/30 s

float to_f(std::string_view v) { return static_cast<float>(std::atof(std::string(ascii_trim(v)).c_str())); }

}  // namespace

BattleConfig::SkirmishOptions skirmish_option_defaults(const data::GameConstants& gc) {
    BattleConfig::SkirmishOptions o;
    o.set = true;
    o.credits = std::max(0, gc.get_int("MP_Default_Credits", o.credits));
    o.heroes = gc.get_bool("MP_Default_Allow_Heroes", o.heroes);
    o.starting_units = gc.get_bool("MP_Default_Free_Starting_Units", o.starting_units);
    o.super_weapons = gc.get_bool("MP_Default_Allow_SuperWeapons", o.super_weapons);
    o.prebuilt_base = gc.get_bool("MP_Default_Pre_Built_Base", o.prebuilt_base);
    return o;
}

void TacticalBattle::apply_skirmish_start_credits() {
    // SKIRMISH_SETUP §2 step 3 (0x291d40): EVERY player (Neutral and the other non-playable ones included) gets credits set
    // to the option credits when the skirmish starts. The port's skirmish galaxy may have paid scaffold income before the
    // battle was pushed, so the account is set, not added to.
    if (!skirmish_map() || !cfg_.skirmish_options.set || !credits_add_ || !credits_get_) return;
    const auto want = static_cast<float>(std::max(0, cfg_.skirmish_options.credits));
    for (size_t p = 0; p < galaxy_->players().size(); ++p) {
        const auto pi = static_cast<int32_t>(p);
        credits_add_(pi, want - credits_get_(pi));
    }
}

void TacticalBattle::sk_garrison_setup() {
    sk_garrisons_.clear();
    if (!skirmish_map()) return;
    for (int32_t s = 0; s < 2; ++s) {
        const int32_t p = skirmish_slot_player(s);
        if (p < 0) continue;
        SkGarrison g;
        g.player = p;
        for (uint32_t i = 0; i < parts_.size(); ++i) {
            const Participant& pt = parts_[i];
            if (pt.owner != p || pt.reserve || pt.starbase || pt.structure || pt.map || pt.built || !pt.deployed) continue;
            g.types.push_back(pt.type);
            for (const Unit& u : units_)
                if (u.participant == i) g.ids.push_back(u.id);
        }
        std::sort(g.ids.begin(), g.ids.end());
        if (!g.ids.empty()) sk_garrisons_.push_back(std::move(g));
    }
}

void TacticalBattle::sk_garrison_tick() {
    for (SkGarrison& g : sk_garrisons_) {
        if (!g.ids.empty() && g.timer < 0 && g.pending.empty()) {
            // 0x2a6a00 -> 0x280c50: the garrison list became empty (skirmish, nothing pending, timer not armed).
            const bool any = std::any_of(g.ids.begin(), g.ids.end(), [this](objects::ObjectId id) {
                const Unit* u = unit_by_id(id);
                return u && u->alive && !u->retreated;
            });
            if (!any) {
                g.ids.clear();
                float delay = 20.0f;
                const auto& ps = galaxy_->players();
                if (static_cast<size_t>(g.player) < ps.size())
                    if (const std::string_view v = rules_->faction(ps[static_cast<size_t>(g.player)].faction).xml_text("Garrison_Reinforcement_Delay_Seconds");
                        !v.empty())
                        delay = std::max(0.0f, to_f(v));
                g.timer = static_cast<int32_t>(frame_) + static_cast<int32_t>(delay * 30.0f);
            }
        }
        if (g.timer >= 0 && static_cast<int32_t>(frame_) >= g.timer) {
            // 0x287e70: on expiry with nothing pending, the start-force list is delivered again.
            g.timer = -1;
            if (space_) {
                g.pending = g.types;
            } else {
                for (const uint32_t t : g.types) {
                    objects::ObjectId key = objects::kNoObjectId;
                    (void)add_built_reserve(g.player, t, key);
                }
            }
        }
    }
}

int32_t TacticalBattle::sk_pending_delivery(int32_t player, bool pop) {
    for (SkGarrison& g : sk_garrisons_) {
        if (g.player != player || g.pending.empty()) continue;
        const auto t = static_cast<int32_t>(g.pending.front());
        if (pop) g.pending.erase(g.pending.begin());
        return t;
    }
    return -1;
}

const TacticalBattle::SkIncome& TacticalBattle::sk_income_of(uint32_t type) {
    if (sk_income_.size() <= type) sk_income_.resize(static_cast<size_t>(type) + 1);
    SkIncome& in = sk_income_[type];
    if (in.parsed) return in;
    in.parsed = true;
    const objects::GameObjectTypes& types = catalog_->types();
    if (type >= types.size()) return in;
    const objects::ObjectTypeDatabase& raw = types.raw();
    const objects::FieldColumns& fc = raw.fields();
    const std::string_view self = raw.types().name[type];
    for (const objects::FieldIndex f : types.get_sub_objects(static_cast<objects::TypeIndex>(type), "Abilities")) {
        const std::string_view tag = raw.tag_name(f);
        const bool stream = ascii_iequals(tag, "Income_Stream_Ability"), mod = ascii_iequals(tag, "Income_Stream_Mod_Ability");
        if (!stream && !mod) continue;
        float value = 0.0f, interval = 0.0f, additive = 0.0f, multiplier = 1.0f;
        bool enabled = true, own = false;
        for (objects::FieldIndex c = f + 1; c < fc.size() && fc.depth[c] > fc.depth[f]; ++c) {
            if (fc.parent[c] != f) continue;
            const std::string_view k = raw.tag_name(c);
            if (ascii_iequals(k, "Base_Income_Value")) value = to_f(fc.text[c]);
            else if (ascii_iequals(k, "Base_Interval_In_Secs")) interval = to_f(fc.text[c]);
            else if (ascii_iequals(k, "Income_Additive_Value")) additive = to_f(fc.text[c]);
            else if (ascii_iequals(k, "Income_Multiplier")) multiplier = to_f(fc.text[c]);
            else if (ascii_iequals(k, "Initially_Enabled")) enabled = !ascii_iequals(ascii_trim(fc.text[c]), "No") && !ascii_iequals(ascii_trim(fc.text[c]), "False");
            else if (ascii_iequals(k, "Target_Stream_Source")) own = ascii_iequals(ascii_trim(fc.text[c]), self);
        }
        if (stream) {
            in.value = value;
            in.interval = interval;
        } else if (own) {
            // [M] a mod on the object's own stream (the skirmish stations' Supply Dock bonus, Initially_Enabled No, switched on
            // by the station's supply-dock hard point): counted while the object lives. The retail trace pays the Empire
            // station 30 + 20 per 10 s from frame 0 (q2f Geonosis trace), so the hard point is up at the start; its
            // destruction is not modelled. Mods on other sources (Mineral Processor x2 on the Mining Facility, the power
            // generators' reverse mods, upgrade objects) are [port] not applied.
            (void)enabled;
            in.multiplier *= multiplier;
            in.additive += additive;
        }
    }
    return in;
}

void TacticalBattle::sk_income(const TacticalMode& mode) {
    // Income_Stream_Ability [trace]: every frame the owner gets (Base_Income_Value x mods + additive) / Base_Interval_In_Secs
    // x 1/30 s, a float add into its credits. The retail Geonosis trace (q2f, 2026-10-30) shows the human's credits at
    // 6000 + 299.7 / 599.4 / 899.1 after 1800 / 3600 / 5400 frames: 50 per 10 s paid per frame (a per-interval payout would
    // give exactly 300 / 600 / 900; the 0.3 shortfall per 1800 frames is the float rounding of a 1/6 credit add near 6000).
    if (!skirmish_map() || !credits_add_) return;
    const objects::ObjectStore& st = mode.primary();
    const int32_t slots[2] = {skirmish_slot_player(0), skirmish_slot_player(1)};
    for (const Unit& u : units_) {
        if (!u.alive || u.retreated || !st.alive(u.h)) continue;
        const int32_t owner = st.owner(u.h);
        if (owner < 0 || (owner != slots[0] && owner != slots[1])) continue;
        const SkIncome& in = sk_income_of(u.type);
        if (in.value <= 0.0f || in.interval <= 0.0f) continue;
        const float per_interval = in.value * in.multiplier + in.additive;
        credits_add_(owner, per_interval / in.interval * kFrameSeconds);
    }
}

}  // namespace alamo::sim
