// ref5 t05 (slice 1): the PotentialPlan marshallers that run the same rules in every mode, written once over a per-mode
// adapter. Retail has one solver (0x6c3da0) and one marshaller of each kind for galactic and tactical (AI_GALACTIC_2 §1);
// the port's galactic (galactic_pp.cpp) and tactical (ai/tactical tactical_pp.cpp) solves each carried a copy, and fixes
// landed in one copy only. The mode's PotentialPlan scratch (GalPp, TacPp) is the adapter `W`; what differs by mode (the
// team expansion, the money a percent team may spend, the tech veto, the contrast lists) is a hook on it.
//
// Adapter members every marshaller here reads: def, recs (each: type, teams, start_team, team_slot, is_hero, free_unit),
// slots (each: tf, assigned, cost_sum, mask, tf_count, tf_force), sel_rec, sel_slot, selected_mask, main_met, escort_met,
// any_hero_attach, any_special, any_escort, team_def(slot), tf_def(tf), required_masks().
#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "alamo/ai/ai_plans.hpp"

namespace alamo::ai {

// 0x729830 first pass (AI_GALACTIC_TOKENS §9.10): a non-escort slot is skipped while the main contrast is met and the
// escort one is not, an escort slot while the escort contrast is met and the main one is not.
constexpr bool pp_place_skip(bool main_met, bool escort_met, bool escort_slot) {
    return escort_slot ? (escort_met && !main_met) : (main_met && !escort_met);
}

// Plan marshaller (0x72a3b0 Setup A, 0x729550 Setup B, 0x729cc0 Setup C, 0x72a170 Score, 0x72a720 / 0x729830 Commit,
// 0x72a060 Finalize) [H]. Hooks on W:
//   void pp_begin_slots()                                    Setup A, before the slots are built
//   void pp_init_slot(Slot& s, uint32_t tf, uint32_t team)   each new slot, in TF / team order, before it is stored
//   void pp_team_candidates(size_t slot, F add)              Setup B: add(type) for each candidate type of the slot's team
//   int32_t pp_draw_start_team(int32_t teams)                Setup C: one synced int in [0, teams - 1]
//   float pp_percent_money() const                           PP+0x3C: the money a percent team may spend
//   float pp_cost(type), uint64_t pp_mask(type), float pp_power(type)
template <class W>
class PlanMarshaller final : public PpMarshaller {
public:
    explicit PlanMarshaller(W& p) : p_(p) {}
    bool permanent_veto() const override { return true; }
    void setup_a() override {
        p_.slots.clear();
        p_.pp_begin_slots();
        for (uint32_t tf = 0; tf < p_.def.task_forces.size(); ++tf) {
            const TaskForceDef& d = p_.def.task_forces[tf];
            p_.any_hero_attach = p_.any_hero_attach || !d.deny_hero_attach;
            p_.any_special = p_.any_special || !d.deny_special_weapon_attach;
            p_.any_escort = p_.any_escort || d.escort;
            for (uint32_t team = 0; team < d.teams.size(); ++team) {
                typename decltype(p_.slots)::value_type s;
                s.tf = tf;
                s.team = team;
                p_.pp_init_slot(s, tf, team);
                p_.slots.push_back(std::move(s));
            }
        }
        for (auto& r : p_.recs) r.teams.clear();
    }
    void setup_b() override {
        for (size_t si = 0; si < p_.slots.size(); ++si)
            p_.pp_team_candidates(si, [&](auto t) {
                auto& r = p_.recs[p_.ensure_rec(t)];
                if (std::find(r.teams.begin(), r.teams.end(), static_cast<int32_t>(si)) == r.teams.end()) r.teams.push_back(static_cast<int32_t>(si));
            });
    }
    void setup_c() override {  // one synced int per candidate with teams (rejection sampling)
        for (auto& r : p_.recs) r.start_team = r.teams.empty() ? -1 : p_.pp_draw_start_team(static_cast<int32_t>(r.teams.size()));
    }
    float score(uint32_t i) override {
        auto& r = p_.recs[i];
        r.team_slot = -1;
        const auto n = static_cast<int32_t>(r.teams.size());
        for (int32_t k = 0; k < n; ++k) {
            const int32_t s = r.teams[static_cast<size_t>((r.start_team + k) % n)];
            const auto& slot = p_.slots[static_cast<size_t>(s)];
            if (p_.team_def(slot).percent || static_cast<int32_t>(slot.assigned.size()) < p_.team_def(slot).max) {
                r.team_slot = s;
                break;
            }
        }
        for (const int32_t s : r.teams) {
            const auto& slot = p_.slots[static_cast<size_t>(s)];
            const TeamDef& td = p_.team_def(slot);
            if (!td.percent) {
                if (static_cast<int32_t>(slot.assigned.size()) < td.max)
                    return 1.0f - static_cast<float>(slot.assigned.size()) / static_cast<float>(td.max);
            } else {
                if (!p_.def.ignore_target) return 1.0f;
                if (td.percent_value / 100.0f * p_.pp_percent_money() > p_.pp_cost(r.type) + slot.cost_sum) return 1.0f;
            }
        }
        if (p_.any_hero_attach && r.is_hero) return 0.0f;
        return kAiBigFloat;
    }
    void commit(uint32_t i) override {
        const auto sel = static_cast<uint32_t>(p_.sel_rec.size() - 1);
        p_.selected_mask |= p_.pp_mask(p_.recs[i].type);
        if (!place(sel, i, true)) place(sel, i, false);
    }
    void finalize(bool& success) override {
        bool required_tf = false;
        for (const TaskForceDef& d : p_.def.task_forces) required_tf = required_tf || d.required;
        if (p_.sel_rec.empty() && !required_tf) success = false;
        for (const auto& s : p_.slots) {
            const TeamDef& td = p_.team_def(s);
            const TaskForceDef& d = p_.tf_def(s.tf);
            if (!td.percent && static_cast<int32_t>(s.assigned.size()) < td.min) success = false;
            if (s.tf_force < d.minimum_total_force) success = false;
            if (s.tf_count < d.minimum_total_size) success = false;
        }
        for (const uint64_t m : p_.required_masks())
            if ((m & p_.selected_mask) == 0) success = false;
    }

private:
    // 0x729830: the first slot of the candidate's teams (from its start team) with room; the first pass respects the
    // contrast flags (pp_place_skip), the second ignores them.
    bool place(uint32_t sel, uint32_t rec, bool respect_met) {
        const auto& r = p_.recs[rec];
        const auto n = static_cast<int32_t>(r.teams.size());
        for (int32_t k = 0; k < n; ++k) {
            const int32_t s = r.teams[static_cast<size_t>((std::max(r.start_team, 0) + k) % n)];
            auto& slot = p_.slots[static_cast<size_t>(s)];
            if (respect_met && pp_place_skip(p_.main_met, p_.escort_met, p_.tf_def(slot.tf).escort)) continue;
            if (!(p_.team_def(slot).percent || static_cast<int32_t>(slot.assigned.size()) < p_.team_def(slot).max)) continue;
            slot.assigned.push_back(sel);
            slot.cost_sum += p_.pp_cost(r.type);
            slot.mask |= p_.pp_mask(r.type);
            for (auto& o : p_.slots)
                if (o.tf == slot.tf) {
                    ++o.tf_count;
                    o.tf_force += p_.pp_power(r.type);
                }
            p_.sel_slot[sel] = s;
            return true;
        }
        return false;
    }
    W& p_;
};

// TechTree marshaller 0x729190 [H]: tech level / 5 x 2. Hook: bool pp_tech_veto(const Rec& r, int32_t tech) (galactic:
// a produced candidate above the player's tech level; tactical: never).  int32_t pp_tech_level(type).
template <class W>
class TechTreeMarshaller final : public PpMarshaller {
public:
    explicit TechTreeMarshaller(W& p) : p_(p) {}
    float score(uint32_t i) override {
        const auto& r = p_.recs[i];
        const int32_t tech = p_.pp_tech_level(r.type);
        if (p_.pp_tech_veto(r, tech)) return kAiBigFloat;
        return static_cast<float>(tech) / 5.0f * 2.0f;
    }

private:
    W& p_;
};

// Contrast 0x72b6a0: the score of one (entry, weight) pair for a candidate of effective power P against the entry's
// remaining requirement R: d = max(R - wP, 0), m = max(R, wP), s = (1 - (d / m)^2) x w (0 when m = 0).
inline double pp_contrast_pair_score(double remaining, float weight, double power) {
    const double wp = static_cast<double>(weight) * power;
    const double d = std::max(remaining - wp, 0.0), m = std::max(remaining, wp);
    return m > 0.0 ? (1.0 - (d / m) * (d / m)) * static_cast<double>(weight) : 0.0;
}

// One entry of a Contrast requirement list (0x5f1990 galactic, 0x5f1d00 tactical): `row` = the EnemyContrastTypes row, -1
// for a layer total; `space` = layer byte 0 (space) / false = layer byte 1 (ground); `target` = the required strength.
struct PpContrastEntry {
    int32_t row = -1;
    bool space = false;
    double target = 0.0;
};

// The contrast arithmetic of every mode (AI_GALACTIC_TOKENS §9.8, §9.8a: none of it tests the mode) [H]:
// - the typed-row scale: ((double)MaxContrastScale + (double)(float)(f x PerFailureContrastAdjust)) x (double)multiplier
//   (`failure_adjust` = the float product f x PFCA).
inline double pp_contrast_scale(float max_contrast_scale, float failure_adjust, float multiplier) {
    return (static_cast<double>(max_contrast_scale) + static_cast<double>(failure_adjust)) * static_cast<double>(multiplier);
}
// - 0x72ac70 satisfied test, in float, over one list: total = sum of (float)target over the layer's typed entries (row >= 0)
//   in list order; residual = sum of (rem / target) x (target / total) over those with target > 0 and rem > 0;
//   satisfied <=> residual <= tolerance.
inline bool pp_contrast_layer_satisfied(std::span<const PpContrastEntry> entries, std::span<const double> remaining, bool space,
                                        float tolerance) {
    float total = 0.0f;
    for (const PpContrastEntry& e : entries)
        if (e.row >= 0 && e.space == space) total += static_cast<float>(e.target);
    float residual = 0.0f;
    for (size_t i = 0; i < entries.size() && i < remaining.size(); ++i) {
        const PpContrastEntry& e = entries[i];
        if (e.row < 0 || e.space != space) continue;
        const auto t = static_cast<float>(e.target), r = static_cast<float>(remaining[i]);
        if (!(t > 0.0f && r > 0.0f)) continue;
        residual += (r / t) * (t / total);
    }
    return residual <= tolerance;
}
// - 0x72b940 "still allowed" after v <= 0: an untyped total (row -1) of the list still has remaining > 0 and the candidate
//   covers that entry's layer (layer byte 1 ground -> rec+0x90, byte 0 space -> rec+0x91). Found by layer byte, not position.
inline bool pp_contrast_total_open(std::span<const PpContrastEntry> entries, std::span<const double> remaining, bool covers_space,
                                   bool covers_ground) {
    for (size_t i = 0; i < entries.size() && i < remaining.size(); ++i) {
        const PpContrastEntry& e = entries[i];
        if (e.row < 0 && (e.space ? covers_space : covers_ground) && remaining[i] > 0.0) return true;
    }
    return false;
}

// The minimum checks 0x72b940 runs after v <= 0, once the gate passed (both modes): every task-force slot of the
// candidate below its team min, MinimumTotalForce or MinimumTotalSize keeps it allowed (0), and so does an unmet
// RequiredCategories mask the candidate provides.
template <class W, class Rec>
bool pp_contrast_still_needed(const W& p, const Rec& r, uint64_t candidate_mask) {
    for (const uint64_t m : p.required_masks())
        if ((m & p.selected_mask) == 0 && (m & candidate_mask) != 0) return true;
    for (const int32_t s : r.teams) {
        const auto& slot = p.slots[static_cast<size_t>(s)];
        const TeamDef& td = p.team_def(slot);
        const TaskForceDef& d = p.tf_def(slot.tf);
        if ((!td.percent && static_cast<int32_t>(slot.assigned.size()) < td.min) || slot.tf_force < d.minimum_total_force ||
            slot.tf_count < d.minimum_total_size)
            return true;
    }
    return false;
}

}  // namespace alamo::ai
