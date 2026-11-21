// The tactical AI world's state records (tactical_ai.cpp) and their field lists (REF1 t03): one visit() per record
// drives the save writer, the save reader and the CRC hasher (tactical_save_io.hpp, "one field list per record"). Not
// installed; the world tests include it for the record round-trip and hash checks.
//
// A record kept in a map by id (plans, task forces, statuses, PotentialPlans, attack-positioning records) has its id
// written by the caller as the map key, then visit(); the CRC and the save put the key first.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "alamo/ai/ai_types.hpp"
#include "alamo/ai/tactical/tactical_ai.hpp"
#include "alamo/ai/tactical/tactical_pp.hpp"
#include "alamo/ai/tactical/target_list.hpp"
#include "alamo/lua/ai_bridge.hpp"
#include "tactical_save_io.hpp"

namespace alamo::lua {
class LuaVm;
}

namespace alamo::ai::tactical {

struct Slot {
    TacticalTarget t;
    int32_t handle = -1;  // TacticalPerception handle
    bool alive = true;
};

struct TfRec {
    int32_t id = -1;
    int32_t plan = -1;
    int32_t player = -1;
    uint32_t def = 0;
    std::string name;
    lua::AiTaskForceKind kind = lua::AiTaskForceKind::Land;
    std::vector<objects::ObjectHandle> units;
    std::vector<TacticalReinforcement> pending;              // claimed pool entries (Reinforce draws them)
    std::vector<std::pair<int32_t, int32_t>> reservations;   // (pad object id, type) for Build / Build_All
    std::vector<std::pair<int32_t, int32_t>> producing;      // (type, builder object id): tactical builds in progress
    std::string priorities;                                  // Set_Targeting_Priorities
};

// Ambush: S7 t21 SpaceAmbushBlockStatus (AI_TACTICAL §7.3.1); Ability: S7 t15 A1 AbilityBlockStatus of the task-force
// Activate_Ability (MOVEMENT §3.1.1 step 6). Reinforce: a1j t01 land ReinforceBlockStatus with landings in flight.
// Explore: aitac t07 ExploreAreaBlockStatus (L1_S4_REMAINING §2.3.1): `members` = the unit list (+0x40), `target` = the AI
// target slot (+0x48), `plan_frame` = the start frame (+0x54), `lists` = pick again (+0x50), `direction` = 1 once a unit was
// seen moving after the last pick's order [port: the arrival stand-in]; `unit` the one-unit form.
enum class StatusKind : uint8_t { Done, Failed, ProduceForce, Move, AttackTarget, Guard, SpaceReinforce, Ambush, Ability, Reinforce, Explore };

struct StatusRec {
    int32_t id = -1;
    int32_t plan = -1;
    int32_t tf = -1;
    StatusKind kind = StatusKind::Done;
    bool finished = false;
    lua::AiStatusResult result = lua::AiStatusResult::Nil;
    int64_t started = 0, last_order = 0;
    objects::ObjectHandle unit{};  // L1 final: a one-unit Move status (GameObjectWrapper.Explore_Area), tf = -1
    int32_t target = -1;           // object id (attack / guard)
    Vec3f dest;                    // SpaceReinforce: the ring centre T (+0x68, z = 0)
    // refixf t06 - SpaceReinforceBlockStatus (AI_TACTICAL §7.4): timeout frame (+0x74, -1 none), ring radius R (+0x7c),
    // T inside a non-allied prevention radius at creation (R resets to 1000, else 0).
    int64_t timeout = -1;
    float ring = 0.0f;
    bool prevent = false;
    // S7 t21 - SpaceAmbushBlockStatus (§7.3.1): direction (+0x50: 0 front, 1 left, 2 right, 3 back), minDist (+0x54),
    // threat tolerance (+0x58), mode frame of the last plan (+0x40), lists created (first plan); `members` (+0x68) and
    // `moving` (+0x60) as object ids in list order; `target` = the target object, -1 once it died; last_order = the AI
    // frame of the last group move. S7 t15 A1 - AbilityBlockStatus: `members` = the activated units still pending.
    int8_t direction = -1;
    float min_dist = 0.0f, threat = 1.0e18f;
    uint32_t plan_frame = 0;
    bool lists = false;
    std::vector<int32_t> members, moving;
    // a1j t01 - land ReinforceBlockStatus (A1_READINGS §36): the landings in flight as (pool key, company type), order
    // issued (in-flight count +0x40 = its size); the plan's removable flag saved at the order (+0x44), restored when the
    // last landing reports; Result = `result` set at the order (+0x45: every pending unit placed).
    std::vector<std::pair<int32_t, int32_t>> landings;
    bool saved_removable = true;
    // q6 land end chain - Attack_Move on an object (A1_READINGS §33.2, AI_TACTICAL §7.7.3 item 1): the target's owner
    // last seen (signal 0x1a stand-in: an owner change is detected by polling).
    // q6 t19 (§33.2.2): `members` = the block's all-units list (+0x58, the TF units at the order), `pending` = its
    // pending list (+0x60, the units moved by the order, ascending issue order); a unit leaves `pending` once, on its
    // arrival, and is never re-added (re-paths do not refill it, only a new order does).
    int32_t target_owner = -1;
    std::vector<int32_t> pending;
};

struct PlanInstance {
    int32_t id = -1;
    int32_t player = -1;
    uint32_t plan = kAiNone;
    uint32_t goal_type = kAiNone;
    int32_t slot = -1;          // target slot (-1 global)
    int32_t target_owner = -1;  // unit targets: owner for Original_Target_Owner_Changed
    std::unique_ptr<lua::LuaVm> vm;
    std::vector<int32_t> tfs;
    bool result = false, non_removable = false, ended = false;
    uint32_t live_threads = 0;
    int32_t pp = -1;  // A2 tactical: the goal's PotentialPlan handle (Produce_Force reads its selection)
};

// --- A2 tactical: a solved tactical PotentialPlan (tactical_pp.hpp). ---
struct TacPpRec {
    int32_t player = -1;
    uint32_t plan = kAiNone;
    std::vector<TacticalPpPick> picks;  // FreeStore picks = PP+0x108 (0x6c27d0 removes a unit that left the plan)
    bool reserved = false;              // PP+0x100: the first 0x6c2d60 ran (pool / pad slots reserved then)
    // skai t15 (SKAI_T15_PLAN_RETEST §1): the solve's contrast layer flags (PP +0x1a0 / +0x1a1) and the sticky validity
    // byte PP +0x4c as the running-plan re-test leaves it (false once the contrast re-test failed).
    bool layer_space = false, layer_ground = false;
    bool contrast_failed = false;
    // mcx6 t06 (LUA_API.md "Release_Reinforcements() (0x6e8a70)"): pool keys a Lua Release_Reinforcements freed from this
    // reserved PP (ascending); its next Commit (reserve_pp, 0x6c2b20) takes back the ones still free, then the list empties.
    std::vector<int32_t> lua_released;
};
// --- end A2 tactical ---

struct PendingBonus {
    uint32_t goal_type = kAiNone;
    int32_t target = -1;
    float bonus = 0.0f;
    int64_t deadline = 0;
};

// mcx6 t06 (LUA_API.md "Task force: Enable_Attack_Positioning and Release_Reinforcements"): the UnitAI data pack's
// attack-positioning fields of an object a land task force's Enable_Attack_Positioning reached.
struct AttackPositioning {
    bool flag = false;     // +0x38
    int32_t next = 0;      // +0x34: the frame of the next re-evaluation (wrapping, signed difference)
    bool moving = false;   // +0x24: moving to the attack position
    Vec3f pos;             // +0x28 / +0x2c / +0x30: the attack position
};

// The 23 admission facts of FreeStoreObjectInfo in declaration order (the 17 land / common facts, then the six
// space facts of A1_READINGS §6.1, save version 2).
template <class F>
auto free_store_flags(F& f) {
    return std::array<decltype(&f.owned_by_player), 23>{
        &f.owned_by_player,         &f.is_transport,         &f.type_create_team,     &f.type_is_dummy,
        &f.type_stay_in_transport,  &f.has_indigenous_or_explosion, &f.has_lobbing_superweapon, &f.has_locomotor,
        &f.is_starship_combatant,   &f.is_team,              &f.type_category_bit_5f, &f.team_has_bit_20,
        &f.type_bit_5b,             &f.subobject_f8_nonempty, &f.in_mode_manager_set, &f.dead_or_removing,
        &f.in_world,                &f.type_squadron_member, &f.type_company_member,  &f.has_special_weapon,
        &f.type_dummy_orbital_structure, &f.type_dummy_star_base, &f.has_tactical_super_weapon};
}

namespace save_io {

// Lower bounds of one list element (reader count checks only).
inline constexpr size_t kSlotBytes = kTargetBytes + 5;
inline constexpr size_t kReinforcementBytes = 4 + 4 + 4 + 4 + 4 + 1 + 4;
inline constexpr size_t kBonusBytes = 4 + 4 + 4 + 8;
inline constexpr size_t kPpResultBytes = 1 + 4 + 4 + 4;
inline constexpr size_t kHandleBytes = 8 + 4;
inline constexpr size_t kPairBytes = 4 + 4;

template <class Ar, Of<Slot> R>
void visit(Ar& ar, R& s) {
    visit(ar, s.t);
    ar.field(s.handle);
    ar.field(s.alive);
}

// Without its id (the map key) and its VM: the save follows these fields with has_vm and the VM image, which the
// reader decodes with the plan runtime.
template <class Ar, Of<PlanInstance> R>
void visit(Ar& ar, R& p) {
    ar.field(p.player);
    ar.field(p.plan);
    ar.field(p.goal_type);
    ar.field(p.slot);
    ar.field(p.target_owner, In::Save);
    ar.field(p.pp, In::Save);
    ar.vec(p.tfs, In::Save);
    ar.field(p.result);
    ar.field(p.non_removable);
    ar.field(p.ended, In::Save);
    ar.field(p.live_threads);  // cached on the sim thread (no VM access from hashers)
}

// The CRC of a claimed pool entry is its key.
template <class Ar, Of<TacticalReinforcement> R>
void visit(Ar& ar, R& e) {
    ar.field(e.key);
    ar.field(e.type, In::Save);
    ar.field(e.population, In::Save);
    ar.field(e.units, In::Save);
    ar.field(e.member_type, In::Save);
    ar.field(e.member_create_team, In::Save);
    ar.field(e.count, In::Save);
}

// Without its id (the map key). The CRC reads the units (object ids), the pending pool keys, the reservation count and
// the builds in progress (only when there are any, so runs without tactical production keep their CRC).
template <class Ar, Of<TfRec> R>
void visit(Ar& ar, R& t) {
    ar.field(t.plan, In::Save);
    ar.field(t.player, In::Save);
    ar.field(t.def, In::Save);
    ar.str(t.name, In::Save);
    ar.enumeration(t.kind, lua::AiTaskForceKind::Land, In::Save);
    ar.list(t.units, In::Both, kHandleBytes, [](auto& a, auto& h) { a.handle(h); });
    ar.list(t.pending, In::Both, kReinforcementBytes, visit_each);
    ar.list(t.reservations, In::Both, kPairBytes, pair_fields(In::Save));
    ar.list(t.producing, t.producing.empty() ? In::Save : In::Both, kPairBytes, pair_fields(In::Both));
    ar.str(t.priorities, In::Save);
}

// Without its id (the map key). The CRC reads whether the block finished and its result.
template <class Ar, Of<StatusRec> R>
void visit(Ar& ar, R& s) {
    ar.field(s.plan, In::Save);
    ar.field(s.tf, In::Save);
    ar.enumeration(s.kind, StatusKind::Explore, In::Save);  // Explore: aitac t07
    ar.field(s.finished);
    ar.enumeration(s.result, lua::AiStatusResult::False);
    ar.field(s.started, In::Save);
    ar.field(s.last_order, In::Save);
    ar.handle(s.unit, In::Save);
    ar.field(s.target, In::Save);
    ar.vec3(s.dest, In::Save);
    ar.field(s.timeout, In::Save);  // refixf t06
    ar.field(s.ring, In::Save);
    ar.field(s.prevent, In::Save);
    ar.field(s.direction, In::Save);  // S7 t21
    ar.field(s.min_dist, In::Save);
    ar.field(s.threat, In::Save);
    ar.field(s.plan_frame, In::Save);
    ar.field(s.lists, In::Save);
    ar.vec(s.members, In::Save);
    ar.vec(s.moving, In::Save);
    ar.vec(s.pending, In::Save);
    ar.list(s.landings, In::Save, kPairBytes, pair_fields(In::Save));  // a1j t01
    ar.field(s.saved_removable, In::Save);
    ar.field(s.target_owner, In::Save);  // q6 land end chain
}

// Without its id (the map key). The save keeps the Lua-released pool keys in a block of their own (the last block of
// TacticalAiWorld::write, version 18 / 19); the CRC reads them here, after a marker, only when there are any, and the
// re-test byte as a marker only when set (skai t15, so runs whose re-tests all pass keep their CRC stream).
template <class Ar, Of<TacPpRec> R>
void visit(Ar& ar, R& pp) {
    ar.field(pp.player);
    ar.field(pp.plan);
    ar.field(pp.reserved);
    if (pp.contrast_failed) ar.hash_tag(0xC7);
    if (!pp.lua_released.empty()) {  // mcx6 t06: only after a Lua Release_Reinforcements (none in shipped scripts)
        ar.hash_tag(0xC8);
        ar.vec(pp.lua_released, In::Hash);
    }
    ar.list(pp.picks, In::Both, kPickBytes, visit_each);
    if (ar.since(Feature::Retest)) {
        ar.field(pp.layer_space, In::Save);
        ar.field(pp.layer_ground, In::Save);
        ar.field(pp.contrast_failed, In::Save);
    }
}

// Without its object id (the map key).
template <class Ar, Of<AttackPositioning> R>
void visit(Ar& ar, R& a) {
    ar.field(a.flag);
    ar.field(a.next);
    ar.field(a.moving);
    ar.vec3(a.pos);
}

// The CRC counts the pending bonuses only.
template <class Ar, Of<PendingBonus> R>
void visit(Ar& ar, R& b) {
    ar.field(b.goal_type, In::Save);
    ar.field(b.target, In::Save);
    ar.field(b.bonus, In::Save);
    ar.field(b.deadline, In::Save);
}

// A pending slot-10 solve's result (OPT2 t10); saved only.
template <class Ar, Of<TacticalPpResult> R>
void visit(Ar& ar, R& o) {
    ar.field(o.ok, In::Save);
    ar.template field_as<uint32_t>(o.candidates, In::Save);
    ar.str(o.last_veto, In::Save);
    ar.list(o.picks, In::Save, kPickBytes, visit_each);
    if (ar.since(Feature::Retest)) {
        ar.field(o.layer_space, In::Save);
        ar.field(o.layer_ground, In::Save);
    }
}

// The last update's per-object facts; saved only (the CRC reads the snapshot through the perception).
inline constexpr size_t kFactBytes = 12 + 5 + 13 + 17 + 4 + 6;

template <class Ar, Of<TacticalAiObject> R>
void visit(Ar& ar, R& f) {
    ar.handle(f.handle, In::Save);
    ar.field(f.target.id, In::Save);
    ar.field(f.target.dead_or_removing, In::Save);
    ar.field(f.target.rep_has_land_evaluator, In::Save);
    ar.field(f.target.rep_has_space_evaluator, In::Save);
    ar.field(f.target.own_has_space_evaluator, In::Save);
    auto& a = f.applies;
    ar.field(a.object_id, In::Save);
    ar.field(a.has_owner, In::Save);
    ar.field(a.owner_allied, In::Save);
    ar.field(a.has_locomotor, In::Save);
    ar.field(a.has_build_objects, In::Save);
    ar.field(a.has_cash_point, In::Save);
    ar.field(a.reinforcement_enabling_radius, In::Save);
    ar.field(a.last_state_visible_under_fow, In::Save);
    ar.field(a.initial_state_visible_under_fow, In::Save);
    for (auto* b : free_store_flags(f.free_store)) ar.field(*b, In::Save);
    ar.vec(f.team_members, In::Save);
    ar.field(f.moving, In::Save);
    ar.field(f.attack_target, In::Save);
    ar.field(f.damage_tracking, In::Save);
    ar.field(f.reprojection_stamp, In::Save);
    ar.field(f.valid_target, In::Save);  // q6 t19
    ar.field(f.hero_clash, In::Save);
    if (ar.since(Feature::PadFact))  // b0416 (absent before version 14 / 15: false)
        ar.field(f.pad_occupied, In::Save);
    else
        ar.absent(f.pad_occupied, false);
}

}  // namespace save_io

}  // namespace alamo::ai::tactical
