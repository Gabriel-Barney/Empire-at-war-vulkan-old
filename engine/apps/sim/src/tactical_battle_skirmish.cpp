// --- skai (q2f t04): skirmish map battles. A skirmish started straight on its _MP_ map (CampaignBattles::start_map_battle with
// BattleConfig::skirmish, TacticalBattle::skirmish_map): the Income_Stream_Ability credit trickle (docs/re/SKIRMISH_SETUP.md).
// The skirmish victory types 0x13 / 0x16 are the victory monitor's (tactical_battle_victory.cpp, story t06). Serial code, no
// synced draws; credits go through the host's credit hooks.
#include <algorithm>
#include <span>
#include <vector>
#include <cstdlib>
#include <string>
#include <string_view>

#include "alamo/galactic/campaign.hpp"
#include "alamo/core/ascii.hpp"
#include "alamo/objects/behaviors.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/objects/type_fields.hpp"
#include "alamo/sim/l1_loaders.hpp"
#include "alamo/sim/tactical_battle.hpp"

namespace alamo::sim {

namespace {

constexpr float kFps = 30.0f;  // tactical frames per second (the income pay divides by it, 0x6951a0)

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
    if (const auto v = gc.get("MP_Default_Land_Tactical_Win_Condition"))
        if (const int32_t w = parse_skirmish_victory_type(*v); w >= 0) o.land_default_win = static_cast<uint8_t>(w);
    if (const auto v = gc.get("MP_Default_Space_Tactical_Win_Condition"))
        if (const int32_t w = parse_skirmish_victory_type(*v); w >= 0) o.space_default_win = static_cast<uint8_t>(w);
    return o;
}

int32_t parse_skirmish_victory_type(std::string_view text) {
    std::string_view s = text;
    const auto is_space = [](char c) { return c == 0x20 || c == 0x09 || c == 0x0d || c == 0x0a; };
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    if (s.empty()) return -1;
    if (s.front() >= '0' && s.front() <= '9') {
        int32_t v = 0;
        for (const char c : s) {
            if (c < '0' || c > '9' || v > 0xff) return -1;
            v = v * 10 + (c - '0');
        }
        return v <= 0xff ? v : -1;
    }
    if (s.size() > 9 && ascii_iequals(s.substr(0, 9), "SKIRMISH_")) s.remove_prefix(9);
    static constexpr std::pair<std::string_view, int32_t> kNames[] = {
        {"ALL_ENEMY_UNITS_DESTROYED", 0x12}, {"ALL_ENEMY_BASE_COMPONENTS_DESTROYED", 0x13}, {"ENEMY_COMMAND_HQ_DESTROYED", 0x14},
        {"CONTROL_POINT_DOMINATION", 0x15}, {"SPACE_ENEMY_STARBASE_DESTROYED", 0x16}};
    for (const auto& [name, v] : kNames)
        if (ascii_iequals(s, name)) return v;
    return -1;
}

uint8_t skirmish_win_condition(uint8_t want, bool space, uint8_t land_default, uint8_t space_default) {
    const auto valid = [space](uint8_t w) { return space ? (w == 0x12 || w == 0x16) : (w >= 0x12 && w <= 0x14); };
    if (valid(want)) return want;
    const uint8_t d = space ? space_default : land_default;
    if (valid(d)) return d;
    for (int w = d + 1; w <= 0x16; ++w)
        if (valid(static_cast<uint8_t>(w))) return static_cast<uint8_t>(w);
    return 0x12;  // the lowest of both sets
}

void TacticalBattle::refresh_ai_difficulty(int32_t player) {
    if (!combat_ || player < 0 || static_cast<uint32_t>(player) >= combat_->player_count()) return;
    const galactic::DifficultyRules* d = galaxy_->ai_difficulty(player);  // as the construction (cu8 t03): none = 1.0
    combat_->set_player_difficulty(player, d ? d->damage_multiplier : 1.0f, d ? d->health_multiplier : 1.0f, d ? d->shield_multiplier : 1.0f);
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
    for (int32_t s = 0; s < skirmish_slot_count(); ++s) {  // skm2 t02: every slot
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
                const auto& fs = galaxy_->rules().factions;
                if (static_cast<size_t>(g.player) < ps.size() && ps[static_cast<size_t>(g.player)].faction < fs.size())
                    if (const std::string_view v = fs[ps[static_cast<size_t>(g.player)].faction].xml_text("Garrison_Reinforcement_Delay_Seconds");
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

IncomeStreamDef parse_income_stream(const objects::GameObjectTypes& types, uint32_t type) {
    IncomeStreamDef out;
    if (type >= types.size()) return out;
    const objects::ObjectTypeDatabase& raw = types.raw();
    const objects::FieldColumns& fc = raw.fields();
    for (const objects::FieldIndex f : types.get_sub_objects(static_cast<objects::TypeIndex>(type), "Abilities")) {
        const std::string_view tag = raw.tag_name(f);
        const bool stream = ascii_iequals(tag, "Income_Stream_Ability"), mod = ascii_iequals(tag, "Income_Stream_Mod_Ability");
        if (!stream && !mod) continue;
        float value = 0.0f, interval = 0.0f, additive = 0.0f, multiplier = 1.0f, owner_pct = 0.0f, interval_mult = 1.0f;
        int32_t category = 0;
        bool split = false, favors = false, full = false, all_allied = false, reverse = false;
        std::string_view target;
        auto yes = [](std::string_view v) { return objects::parse_type_bool(ascii_trim(v)); };
        for (objects::FieldIndex c = f + 1; c < fc.size() && fc.depth[c] > fc.depth[f]; ++c) {
            if (fc.parent[c] != f) continue;
            const std::string_view k = raw.tag_name(c);
            if (ascii_iequals(k, "Base_Income_Value")) value = to_f(fc.text[c]);
            else if (ascii_iequals(k, "Base_Interval_In_Secs")) interval = to_f(fc.text[c]);
            else if (ascii_iequals(k, "Income_Additive_Value")) additive = to_f(fc.text[c]);
            else if (ascii_iequals(k, "Income_Multiplier")) multiplier = to_f(fc.text[c]);
            else if (ascii_iequals(k, "Interval_Multiplier")) interval_mult = to_f(fc.text[c]);
            else if (ascii_iequals(k, "Stacking_Category")) category = static_cast<int32_t>(std::atoi(std::string(ascii_trim(fc.text[c])).c_str()));
            else if (ascii_iequals(k, "Target_Stream_Source")) target = ascii_trim(fc.text[c]);
            else if (ascii_iequals(k, "Affects_All_Allied_Sources")) all_allied = yes(fc.text[c]);
            else if (ascii_iequals(k, "Reverse_Application_Logic")) reverse = yes(fc.text[c]);
            else if (ascii_iequals(k, "Split_Income_With_Allies")) split = yes(fc.text[c]);
            else if (ascii_iequals(k, "Split_Favors_Owner")) favors = yes(fc.text[c]);
            else if (ascii_iequals(k, "Full_Amount_To_Everyone")) full = yes(fc.text[c]);
            else if (ascii_iequals(k, "Owner_Income_Percentage")) owner_pct = to_f(fc.text[c]);
        }
        if (stream) {
            out.value = value;
            out.interval = interval;
            // MCX4_T09_RE_ANSWERS Q10 [H]: the validator 0x703d00 clears Split_Favors_Owner and Full_Amount_To_Everyone when
            // both are set, and Split_Favors_Owner with Owner_Income_Percentage when Split_Income_With_Allies is off; with
            // split on it clamps the percentage (< 0 -> 0, >= 1 -> 0.99). The create 0x703990 sets the mode (1 favors,
            // 2 full, else 0) and copies the percentage only when it is in [0, 1] (0x695420).
            if (favors && full) favors = full = false;
            if (favors && !split) {
                favors = false;
                owner_pct = 0.0f;
            }
            if (split) owner_pct = owner_pct < 0.0f ? 0.0f : (owner_pct >= 1.0f ? 0.99f : owner_pct);
            out.split = split;
            out.mode = favors ? uint8_t{1} : (full ? uint8_t{2} : uint8_t{0});
            out.owner_share = owner_pct >= 0.0f && owner_pct <= 1.0f ? owner_pct : 0.0f;
        } else {
            IncomeModDef m;
            const objects::TypeIndex t = target.empty() ? objects::kNone : types.resolve_name(target);  // resolved at +0x190
            m.target_type = t == objects::kNone ? 0xFFFFFFFFu : static_cast<uint32_t>(t);
            m.multiplier = multiplier;
            m.additive = additive;
            m.interval = interval_mult > 0.0f ? interval_mult : 1.0f;  // validator 0x704880: <= 0 is an error and becomes 1.0
            m.category = category;
            m.all_allied = all_allied;
            m.reverse = reverse;
            out.mods.push_back(m);
        }
    }
    return out;
}

float income_fold(std::span<const IncomeModEntry> entries, int pick) {
    // 0x33fb70: per stacking category the largest value; the category maxima summed in ascending category order.
    std::vector<std::pair<int32_t, float>> best;  // (category, max), kept sorted by category
    for (const IncomeModEntry& e : entries) {
        const float v = pick == 0 ? e.multiplier : pick == 1 ? e.additive : e.interval;
        if (v == 0.0f) continue;  // apply 0x703fe0 adds no entry for m == 1 / a == 0 / im == 1
        const auto it = std::lower_bound(best.begin(), best.end(), e.category,
                                         [](const std::pair<int32_t, float>& b, int32_t c) { return b.first < c; });
        if (it != best.end() && it->first == e.category) {
            if (v > it->second) it->second = v;
        } else {
            best.insert(it, {e.category, v});
        }
    }
    float sum = 0.0f;
    for (const auto& [c, v] : best) sum += v;
    return sum;
}

float income_stream_pay(float base_value, float base_interval, std::span<const IncomeModEntry> entries) {
    // 0x6951a0 / 0x694b30 (MCX4_T09_RE_ANSWERS Q10 [H]): float32, in this order.
    const float m = income_fold(entries, 0), a = income_fold(entries, 1), iv = income_fold(entries, 2);
    const float interval = std::max(1.0f, (1.0f + iv) * base_interval);  // ledger: consumes Income_Stream_Mod_Ability.Interval_Multiplier
    return ((m + 1.0f) * base_value + a) / interval / kFps;
}

float income_share(uint8_t mode, float owner_share, int32_t owner, int32_t recipient, size_t recipients, float pay) {
    if (recipients == 0) return 0.0f;
    const float n = static_cast<float>(recipients);
    if (mode == 2) return pay;  // Full_Amount_To_Everyone: every recipient gets the whole pay
    if (mode == 1) {
        // Split_Favors_Owner (0x694e90): owner s = clamp(pay x pct, 0, pay); every other recipient (pay - s) / N with N
        // counting the owner, so one share is never paid (retail quirk); shares <= 0 are skipped.
        float sh = pay * owner_share;
        sh = sh < 0.0f ? 0.0f : (sh > pay ? pay : sh);
        const float amount = recipient == owner ? sh : (pay - sh) / n;
        return amount > 0.0f ? amount : 0.0f;
    }
    return pay / n;  // equal split (0x694ba0)
}

bool income_mod_applies(const IncomeModDef& mod, uint32_t stream_type, int32_t stream_owner, int32_t mod_owner,
                        std::span<const int32_t> recipients) {
    if (mod.target_type != stream_type) return false;  // Target_Stream_Source (+0x190)
    if (std::find(recipients.begin(), recipients.end(), mod_owner) == recipients.end()) return false;  // 0x537a00 / 0x6950a0
    return mod.all_allied || mod_owner == stream_owner;  // ledger: consumes Income_Stream_Mod_Ability.Affects_All_Allied_Sources
}

const IncomeStreamDef& TacticalBattle::sk_income_of(uint32_t type) {
    if (sk_income_.size() <= type) sk_income_.resize(static_cast<size_t>(type) + 1);
    SkIncome& in = sk_income_[type];
    if (!in.parsed) {
        in.parsed = true;
        in.def = parse_income_stream(catalog_->types(), type);
    }
    return in.def;
}

void TacticalBattle::sk_income(const TacticalMode& mode) {
    // Income_Stream_Ability [trace]: every frame each recipient gets its share of ((M + 1) x Base_Income_Value + A) / I / FPS,
    // a float add into its credits. The retail Geonosis trace (q2f, 2026-10-30) shows the human's credits at 6000 + 299.7 /
    // 599.4 / 899.1 after 1800 / 3600 / 5400 frames: 50 per 10 s paid per frame (a per-interval payout would give exactly
    // 300 / 600 / 900; the 0.3 shortfall per 1800 frames is the float rounding of a 1/6 credit add near 6000).
    if (!skirmish_map() || !credits_add_) return;
    const objects::ObjectStore& st = mode.primary();
    // mcx4 tac2 (MCX4_T09_RE_ANSWERS Q10 "Interval_Multiplier" / "Affects_All_Allied_Sources" [H], apply 0x703fe0): the live
    // mod sources this frame (unit order), each mod with its owner. [port] Every mod of a live object counts (the supply-dock
    // mods, Initially_Enabled No, are up from frame 0 as in the trace; the hard point's loss is not modelled);
    // Reverse_Application_Logic mods (the power generators' penalties) are not applied.
    struct LiveMod {
        IncomeModDef mod;
        int32_t owner;
    };
    std::vector<LiveMod> mods;
    for (const Unit& u : units_) {
        if (!u.alive || u.retreated || !st.alive(u.h)) continue;
        const IncomeStreamDef& d = sk_income_of(u.type);
        for (const IncomeModDef& m : d.mods)
            if (!m.reverse && m.target_type != 0xFFFFFFFFu) mods.push_back({m, st.owner(u.h)});
    }
    std::vector<int32_t> recipients;
    std::vector<IncomeModEntry> entries;
    for (const Unit& u : units_) {
        if (!u.alive || u.retreated || !st.alive(u.h)) continue;
        const int32_t owner = st.owner(u.h);
        if (owner < 0 || skirmish_slot_of(owner) < 0) continue;  // skm2 t02: every slot
        const IncomeStreamDef& in = sk_income_of(u.type);
        if (in.value <= 0.0f || in.interval <= 0.0f) continue;
        // MCX4_T09_RE_ANSWERS Q10 [H] (0x7038b0): without Split_Income_With_Allies the owner only; with it the players allied
        // with the owner (the owner included [M]) whose faction Is_Playable, paid in ascending player id. [port] the set is
        // rebuilt every frame (skirmish alliances do not change during a battle).
        recipients.clear();
        if (!in.split || !galaxy_) {
            recipients.push_back(owner);
        } else {
            const int32_t np = static_cast<int32_t>(galaxy_->players().size());
            for (int32_t q = 0; q < np; ++q)
                if (bt_allied(owner, q) && vm_player_playable(q)) recipients.push_back(q);
        }
        if (recipients.empty()) continue;
        // The stream's mod entries: a mod on this source type whose owner is a recipient of the stream, and either
        // Affects_All_Allied_Sources or the stream owner is the mod owner (0x703fe0 / 0x537a00 / 0x6950a0).
        entries.clear();
        for (const LiveMod& lm : mods) {
            if (!income_mod_applies(lm.mod, u.type, owner, lm.owner, recipients)) continue;
            IncomeModEntry e;
            e.category = lm.mod.category;
            e.multiplier = lm.mod.multiplier != 1.0f ? lm.mod.multiplier - 1.0f : 0.0f;
            e.additive = lm.mod.additive;
            e.interval = lm.mod.interval != 1.0f ? lm.mod.interval - 1.0f : 0.0f;
            entries.push_back(e);
        }
        const float pay = income_stream_pay(in.value, in.interval, entries);
        for (const int32_t q : recipients) {
            const float amount = income_share(in.mode, in.owner_share, owner, q, recipients.size(), pay);
            if (amount > 0.0f || in.mode != 1) credits_add_(q, amount);
        }
    }
}

}  // namespace alamo::sim
