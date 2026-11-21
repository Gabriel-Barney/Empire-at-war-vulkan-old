// ref5 t07 - BattleFacts: what the tactical AI reads from its battle, in one object the battle host owns and the AI world
// reads (TacticalAiHost::facts()). Header-only; library alamo_ai_tactical.
//
// Each fact family has ONE rule, written by the host from the battle. The object answers a read in one of two modes:
//   * live (the default): the read asks the rule now. The lane-off AI runs here: its own orders change the battle inside
//     an AI frame (a Reinforce deploys a pool entry, an ability locks a unit), so no answer is held across reads.
//   * snapshot (TacticalAiHost sets it while the AI lane's think() runs): the read answers from what the host captured on
//     the critical lane at the AI slot, and never asks the battle. A FactTable family captures the rule's answers over the
//     host's key list (one rule for both lanes; a key the capture did not list answers the family's `missing` value). A
//     FactQuery family takes positions or a pair of ids, so it cannot be listed: it has a second, `snapshot`, function
//     that answers from captured inputs (the snapshot store, the captured tables); some of them are partial by design and
//     the full test runs when the lane's order is applied (the host's header says which).
// version() counts the captures and the writes the host reports (wrote()): a reader that keeps an answer across reads
// compares it. The live mode itself never needs it (every read is current).
//
// Determinism: the tables are sorted by key; nothing here iterates an unordered container or reads anything but the
// rules' answers. Nothing here is saved or hashed (a capture is rebuilt at every AI slot; a load runs live until then).
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "alamo/ai/tactical/target_list.hpp"  // Vec3f

namespace alamo::galactic {
class ContrastTable;
}

namespace alamo::ai::tactical {

class DamageTrackingStore;

// One entry of a player's reinforcement pool (campaign: units in orbit = the battle's reserves; skirmish: the
// tactical reinforcement pool).
struct TacticalReinforcement {
    int32_t key = -1;          // pool key (the Reinforce event's "unit" field)
    int32_t type = -1;         // PerceptionWorld type index
    float population = 0.0f;   // population cost (0x373500)
    uint32_t units = 1;        // objects it spawns (team members)
    // --- A2 tactical: the Reinforcement marshaller's entry facts (AI_GALACTIC_3 §1.3) ---
    int32_t member_type = -1;          // first Company_Units member type (-1: the type has no Company_Units)
    bool member_create_team = false;   // that member type has Create_Team
    int32_t count = 1;                 // count(E): selections the entry serves
    // --- end A2 tactical ---
};

enum class PadCheck : uint8_t { Ok = 0, NotFriendlyOrVisible = 1, Blocked = 2, RebuildCooldown = 3 };

// refixf t06 (AI_TACTICAL §7.4 space): what the space reinforcement ring search needs from the mode.
struct SpaceDropFrame {
    float entry_facing_deg = 0.0f;  // the player's reinforcement entry facing (mode +0x288 record, field +8), degrees
    bool has_box = false;           // the playable box (mode vf 0x208): ring points are clamped into it
    float min_x = 0.0f, min_y = 0.0f, max_x = 0.0f, max_y = 0.0f;
};

// A hard point acquired an opportunity target (STRATEGIC_GAPS §5.3: signal 0x21 OpportunityTargetAcquiredDataClass, sent
// to the hard point's owner, or to the team object for a team member). Object ids; hardpoint_type = HardPointType value.
struct OpportunityTargetEvent {
    int32_t owner = -1;           // the hard point's owner object
    int32_t target = -1;          // the acquired target object
    int32_t hardpoint_type = -1;  // HardPointType of the hard point (5..10 for weapons)
};

// s78f t08 - signal 0x1E (TACTICAL_COMBAT §3.1.1 "s78f t08 answers" 4): a unit's targeting state entered 4 (attacking)
// for the first time. `unit` = the object the signal is raised on (a team member's: its container), `target` = its
// formation attack target, else its current target. Object ids.
struct AttackStateSignal {
    int32_t unit = -1;
    int32_t target = -1;
};

// skai t15 - signal 0x18 (SKAI_T15_UNIT_DAMAGED §1): one applied damage instance, raised by Take_Damage 0x3a9e30 through
// the UNIT_AI behaviour 0x4f59d0. `unit` = the UNIT_AI holder (the victim, else its container: a squad's TEAM container),
// `attacker` = the shooter (a TEAM member replaced by its container), `deliberate` = the attacker's current attack
// target (formation target first) was `unit`. Object ids, in the battle's damage order.
struct UnitDamagedSignal {
    int32_t unit = -1;
    int32_t attacker = -1;
    bool deliberate = false;
};

// a1j t01 - signal 0x19 "reinforcements unloaded" (ReinforcementsUnloadedDataClass, A1_READINGS §32.1 / §36): a land
// landing object finished its unload (0x6438a0) or was removed while unloading (detach vf15 0x6442d0). `key` = the
// Reinforce event's unit field (the pool entry); `units` = the unloaded object ids in spawn order.
struct ReinforcementsUnloaded {
    int32_t player = -1;
    int32_t key = -1;
    std::vector<int32_t> units;
};

// q6 t19 (SKIRMISH_SETUP §3 / §3.1, AI_GALACTIC_3 §1.11): a land tactical build completed (signal 0x22): the type went to
// the player's reinforcement pool (no object appears), so the build task of the first task force (id order) producing
// that type ends its entry.
struct ProductionCompleted {
    int32_t player = -1;
    int32_t type = -1;  // PerceptionWorld type index
};

// --- A2 tactical: a build pad the player can build on (the TacticalBuild marshaller's pool, AI_TACTICAL §5.1
// side table 0x4d29a0): visible, empty, friendly or neutral [M], and the UC_ types its pad type lists for the
// player's faction (Tactical_Buildable_Objects_Multiplayer in skirmish, _Campaign otherwise). ---
struct TacticalPad {
    int32_t id = -1;
    Vec3f position;
    std::vector<int32_t> accepts;  // PerceptionWorld type indices
};
// --- end A2 tactical ---

// The signals of one AI frame: what the battle raised since the previous AI frame, each list in the order it happened.
// TacticalAiHost::frame_events fills them once per AI frame (tactical_ai.hpp says when).
struct FrameEvents {
    // STRATEGIC_GAPS §5.3: opportunity-fire acquisitions. A task force holding the owner (or its team container) raises plan
    // event 17 Hardpoint_Opportunity_Target_Acquired (unit, target) when it subscribed that HardPointType.
    std::vector<OpportunityTargetEvent> opportunity;
    // s78f t08: signal 0x1E. A task force holding the unit (TaskForceClass::Add_Force 0x5f9a90 subscribes every unit it
    // takes; handler 0x5fe5d0 case 0x1E) raises plan event 12 Target_In_Range (unit, target) when the target is live.
    std::vector<AttackStateSignal> attack_states;
    // skai t15: signal 0x18, in damage order. A land / space task force holding the unit (Add_Force 0x6e9240 / 0x6e4e30
    // subscribe) queues plan event 14 Unit_Damaged (unit, attacker, deliberate), at most one waiting per task force
    // (0x5fd250). Free-store units never get it.
    std::vector<UnitDamagedSignal> damaged;
    // S7 t15 A1 - "unit ability finished" (object signal 0x29: every ability end, ABILITIES_HARDPOINTS §2.6, and the
    // jetpack's swap back to the primary locomotor, MOVEMENT §3.1.1 step 6): object ids (an id may repeat). The AI feeds its
    // scripts' object bridge (blocking GameObjectWrapper.Activate_Ability), finishes the task-force AbilityBlockStatus
    // counts and raises plan event 19 <TF>_Unit_Ability_Finished(tf, unit) for task-force members that are not dying.
    std::vector<int32_t> ability_finished;
    // a1j t01: the signal 0x19 records of the tick just run, in the order they were sent.
    std::vector<ReinforcementsUnloaded> unloaded;
    // q6 t19: land tactical builds completed in the tick just run, in completion order (hosts whose builds spawn objects:
    // none; those are claimed when they appear).
    std::vector<ProductionCompleted> produced;
    // The lists by bit (TacticalAiHost::frame_events), in the order the world handles them.
    enum Kind : uint32_t {
        kOpportunity = 1u,
        kAttackStates = 2u,
        kDamaged = 4u,
        kAbilityFinished = 8u,
        kUnloaded = 16u,
        kProduced = 32u,
        kAll = 63u
    };
};

// One fact family keyed by Key (a player, an object id, a (player, type) pair). Live: the rule's answer now (no rule:
// `missing`). Snapshot: the answer capture() stored for the key, `missing` for a key it did not list.
template <class Key, class Value>
class FactTable {
public:
    using Rule = std::function<Value(const Key&)>;
    explicit FactTable(Value missing = Value{}) : missing_(std::move(missing)) {}
    void set_rule(Rule r) { rule_ = std::move(r); }
    Value get(bool snapshot, const Key& k) const {
        if (!snapshot) return rule_ ? rule_(k) : missing_;
        const auto it = std::lower_bound(table_.begin(), table_.end(), k, [](const Entry& e, const Key& x) { return e.first < x; });
        return it != table_.end() && it->first == k ? it->second : missing_;
    }
    // The rule's answer for every key (sorted and made unique here); replaces the previous capture.
    void capture(std::vector<Key> keys) {
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        table_.clear();
        table_.reserve(keys.size());
        for (Key& k : keys) {
            Value v = rule_ ? rule_(k) : missing_;
            table_.emplace_back(std::move(k), std::move(v));
        }
    }
    // The same with the rule calls spread over `pf` (a ParallelFor: pf(n, grain, fn(begin, end))); each call writes its own
    // index, so the table is identical for every worker count. The rule must only read.
    template <class Pf>
    void capture(std::vector<Key> keys, const Pf& pf, uint32_t grain) {
        static_assert(!std::is_same_v<Value, bool>, "vector<bool> packs bits: parallel writes would race");
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        std::vector<Value> values(keys.size(), missing_);
        if (rule_)
            pf(static_cast<uint32_t>(keys.size()), grain, [&](uint32_t begin, uint32_t end) {
                for (uint32_t i = begin; i < end; ++i) values[i] = rule_(keys[i]);
            });
        table_.clear();
        table_.reserve(keys.size());
        for (size_t i = 0; i < keys.size(); ++i) table_.emplace_back(std::move(keys[i]), std::move(values[i]));
    }
    void clear() { table_.clear(); }
    size_t captured() const { return table_.size(); }

private:
    using Entry = std::pair<Key, Value>;
    Rule rule_;
    Value missing_;
    std::vector<Entry> table_;
};

// One fact family whose reads take positions or several ids (no key list to capture): `live` answers from the battle,
// `snapshot` from the host's captured inputs. A mode without its function answers `fallback`.
template <class Sig>
class FactQuery;
template <class R, class... Args>
class FactQuery<R(Args...)> {
public:
    explicit FactQuery(R fallback = R{}) : fallback_(std::move(fallback)) {}
    std::function<R(Args...)> live, snapshot;
    R get(bool snap, Args... args) const {
        const std::function<R(Args...)>& f = snap ? snapshot : live;
        return f ? f(args...) : fallback_;
    }

private:
    R fallback_;
};

class BattleFacts {
public:
    BattleFacts() = default;
    BattleFacts(const BattleFacts&) = delete;
    BattleFacts& operator=(const BattleFacts&) = delete;

    // ---- mode and version (the host) ----
    bool snapshot() const { return snapshot_; }
    void set_snapshot(bool on) { snapshot_ = on; }
    uint64_t version() const { return version_; }
    void captured() { ++version_; }  // after a capture of the tables (the AI slot of the lane)
    void wrote() { ++version_; }     // at each AI order the host is handed (also queued or refused), release or HIJACK call

    // ---- what the AI reads ----
    std::vector<TacticalReinforcement> reinforcement_pool(int32_t player) const { return pool.get(snapshot_, player); }
    bool can_reinforce(int32_t player) const { return reinforce_ok.get(snapshot_, player); }  // 0x2b03e0
    float population_cap(int32_t player) const { return pop_cap.get(snapshot_, player); }           // < 0: no cap
    float population_used(int32_t player) const { return pop_used.get(snapshot_, player); }
    // refixf t06 - space TF.Reinforce ring search (AI_TACTICAL §7.4): the player's entry facing and the playable box (false:
    // facing 0, no box).
    bool space_drop_frame(int32_t player, SpaceDropFrame& out) const {
        const std::optional<SpaceDropFrame> f = drop_frame.get(snapshot_, player);
        if (f) out = *f;
        return f.has_value();
    }
    // S7 t21 - the unit's movement lock (0x39ad40: ability share, script lock, suspended locomotor; A1_READINGS §28.9): a
    // locked member does not join a SpaceAmbushBlockStatus (AI_TACTICAL §7.3.1).
    bool movement_locked(int32_t object_id) const { return locked.get(snapshot_, object_id); }
    // a1j t01 - land TF.Reinforce with a timed landing (ReinforceBlockStatus, A1_READINGS §36): the Reinforce of pool entry
    // `key` just executed for `player` put the company in a landing object that has not finished its unload.
    bool reinforce_in_flight(int32_t player, int32_t key) const { return in_flight.get(snapshot_, {player, key}); }
    // A2 tactical: the free store's builder list (free store +0x120, AI_GALACTIC_3 §1.8 / §1.11 [M]) for `type`: ids of the
    // live objects of `player` whose type lists `type` (PerceptionWorld index) for the owner's faction
    // (Tactical_Buildable_Objects_Multiplayer), in the order Produce_Force tries them [port: ascending object id].
    std::vector<int32_t> tactical_builders(int32_t player, int32_t type) const { return builders.get(snapshot_, {player, type}); }
    // A2 tactical: TacticalBuild for types without TACTICAL_UNDER_CONSTRUCTION - a builder of the player can build `type`
    // now (0x288b80 + 0x497140).
    bool can_build_now(int32_t player, int32_t type) const { return can_build.get(snapshot_, {player, type}); }
    // skm1 t03 (AI_TACTICAL §7.4, 0x6e8620): in a skirmish a space TF.Reinforce needs a live DUMMY_STAR_BASE owned by `player`
    // or an ally; false = refused ("AI players cannot reinforce after their starbase has been destroyed", nil).
    bool space_reinforce_base_alive(int32_t player) const { return base_alive.get(snapshot_, player); }
    // A2 tactical: the pads `player` can build on now (ascending id) with their buildable types (TacticalBuild marshaller).
    std::vector<TacticalPad> build_pads(int32_t player) const { return pads.get(snapshot_, player); }
    // Build (§7.5): the three refusal checks for (pad, type) of `player`; type = PerceptionWorld type index.
    PadCheck build_pad_check(int32_t player, int32_t pad, int32_t type) const { return pad_check.get(snapshot_, player, pad, type); }
    // skai t17 (STATION_HANGARS_SOUND §1.2 steps 7-8, 0x497460): the live object that replaced builder `id` (following
    // repeated station upgrades), -1 when none.
    int32_t replaced_builder(int32_t id) const { return replaced.get(snapshot_, id); }
    // refixf t06 - space_prevention_hit (0x4d8d10): `p` lies inside the Reinforcement_Prevention_Radius (2-D, strict) of a
    // live object whose owner is not allied with `player`. space_drop_ok (SpaceMode vf80 0x4d6fd0, force 0): `p` is inside
    // the playable box, not fogged for `player` (force 1), outside every non-allied prevention radius, and the occupancy
    // sweep of pool entry `key`'s type from p - sweep x (cos, sin)(facing) to p over frames [now, now + 115] on its
    // Space_Layer (default 4) and layer 8 is clear.
    bool space_prevention_hit(int32_t player, const Vec3f& p) const { return prevention.get(snapshot_, player, p); }
    bool space_drop_ok(int32_t player, int32_t key, const Vec3f& p, float facing_deg, float sweep) const {
        return space_drop.get(snapshot_, player, key, p, facing_deg, sweep);
    }
    // a1g t04 follow-up - land TF.Reinforce drop validity (LandMode vf80 0x3b5f60 -> 0x3b6170, TACTICAL_GAPS §4.2.1,
    // A1_READINGS §30.7): can pool entry `key` of `player` land at spot `p` (the company layout's centre)? A refused spot
    // sends the unit to the next ring spot.
    bool land_drop_ok(int32_t player, int32_t key, const Vec3f& p) const { return land_drop.get(snapshot_, player, key, p); }
    // mcx6 t06 (LUA_API.md "Attack position 0x4f5390", terrain check 0x4f48a0 [M]): the land terrain line test 0x5a4c10 for
    // the movement class of `object_id` (a TEAM container: its first member's) from `a` to `b`, 2-D.
    bool attack_position_line_clear(int32_t object_id, const Vec3f& a, const Vec3f& b) const {
        return line_clear.get(snapshot_, object_id, a, b);
    }
    // S7 t27 - the land sight factor 0x3722a0 of land Prepare_Ambush (AI_TACTICAL §7.3.3 item 5): the weather
    // Sight_Range_Modifier for the Weather_Category of `type` (the target's company type), for the object `object_id`; 1.0
    // without strong weather / no entry.
    float sight_weather_factor(int32_t type, int32_t object_id) const { return weather.get(snapshot_, type, object_id); }
    // The DAMAGE_TRACKING store FindDeadlyEnemy reads (the battle's; snapshot: the copy capture_damage_tracking took).
    const DamageTrackingStore* damage_tracking() const { return snapshot_ && damage_copy ? damage_copy : damage_live; }
    const galactic::ContrastTable* contrast() const { return contrast_table; }
    // q6 t19 (A1_READINGS §33.2.2): the object ids the order just executed dropped from its move because their attack order
    // (path A, kTfAttackMoveIssueAttack) was refused, in the order tried. Such a unit stays in the block's all-units list
    // but is neither moved nor pending. The host clears it at every execute.
    const std::vector<int32_t>& attack_order_refused() const { return order_refused; }

    // ---- set by the host: one rule per family (FactTable), or the live / snapshot pair (FactQuery) ----
    // Defaults (no rule): an empty pool, no reinforcing, no cap, nothing used, no drop frame, nothing locked or in flight,
    // no builder, a live star base, no pads, every pad refused, no replaced builder, no prevention, every drop spot and
    // line clear, weather 1.0.
    FactTable<int32_t, std::vector<TacticalReinforcement>> pool;
    FactTable<int32_t, bool> reinforce_ok{false};
    FactTable<int32_t, float> pop_cap{-1.0f};
    FactTable<int32_t, float> pop_used{0.0f};
    FactTable<int32_t, std::optional<SpaceDropFrame>> drop_frame;
    FactTable<int32_t, bool> locked{false};
    FactTable<std::pair<int32_t, int32_t>, bool> in_flight{false};
    FactTable<std::pair<int32_t, int32_t>, std::vector<int32_t>> builders;
    FactTable<std::pair<int32_t, int32_t>, bool> can_build{false};
    FactQuery<bool(int32_t)> base_alive{true};
    FactQuery<std::vector<TacticalPad>(int32_t)> pads;
    FactQuery<PadCheck(int32_t, int32_t, int32_t)> pad_check{PadCheck::NotFriendlyOrVisible};
    FactQuery<int32_t(int32_t)> replaced{-1};
    FactQuery<bool(int32_t, Vec3f)> prevention{false};
    FactQuery<bool(int32_t, int32_t, Vec3f, float, float)> space_drop{true};
    FactQuery<bool(int32_t, int32_t, Vec3f)> land_drop{true};
    FactQuery<bool(int32_t, Vec3f, Vec3f)> line_clear{true};
    FactQuery<float(int32_t, int32_t)> weather{1.0f};
    const DamageTrackingStore* damage_live = nullptr;
    const DamageTrackingStore* damage_copy = nullptr;  // the host's capture (snapshot mode); null: damage_live
    const galactic::ContrastTable* contrast_table = nullptr;
    std::vector<int32_t> order_refused;

private:
    bool snapshot_ = false;
    uint64_t version_ = 0;
};

}  // namespace alamo::ai::tactical
