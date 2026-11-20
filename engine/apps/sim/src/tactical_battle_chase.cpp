// tactical_battle: ref5 t08 - the one chase-input builder of 0x4160a0(F, u, c, &Rr) (FIGHTER_FLYING §4.10 A; on land
// BKR1_RE_ANSWERS Q8 [H]: the same rules without the Space_Layer gate) for land formation records and fighter formations,
// the per-type chase ranges, the fighter adapter and the fighter scan-range query.
#include "tactical_battle_internal.hpp"

namespace alamo::sim {
using namespace bo3split;

namespace {
template <class Units>
auto unit_c(const Units& units, objects::ObjectId id) -> const typename Units::value_type* {
    const auto it = std::lower_bound(units.begin(), units.end(), id, [](const auto& x, objects::ObjectId v) { return x.id < v; });
    return it == units.end() || it->id != id ? nullptr : &*it;
}
}  // namespace

const mv::FighterSquad* TacticalBattle::fighter_squad_c(objects::ObjectId id) const {
    if (!movement_) return nullptr;
    const std::span<const mv::FighterSquad> s = movement_->fighter_squads();
    const auto it = std::lower_bound(s.begin(), s.end(), id, [](const mv::FighterSquad& q, objects::ObjectId v) { return q.id < v; });
    return it == s.end() || it->id != id ? nullptr : &*it;
}

objects::ObjectId TacticalBattle::fighter_goal_key(objects::ObjectId id) const {
    const Unit* x = id != objects::kNoObjectId ? unit_c(units_, id) : nullptr;
    if (x && x->container < containers_.size() && fighter_squad_c(containers_[x->container].id)) return containers_[x->container].id;
    return id;
}

bool TacticalBattle::fighter_goal_live(objects::ObjectId key, const objects::ObjectStore& store) const {
    if (key == objects::kNoObjectId) return false;
    if (const mv::FighterSquad* s = fighter_squad_c(key)) return !s->members.empty();
    const Unit* x = unit_c(units_, key);
    return x && x->alive && !x->retreated && store.alive(x->h) && combat_->has_unit(x->id);
}

std::array<float, 4> TacticalBattle::chase_ranges(uint32_t ty) const {
    if (ty < fchase_have_.size() && fchase_have_[ty]) return fchase_cache_[ty];
    const objects::GameObjectTypes& types = catalog_->types();
    if (ty >= types.size()) return {};
    const auto ti = static_cast<objects::TypeIndex>(ty);
    return {types.get(ti, objects::tags::Guard_Chase_Range), types.get(ti, objects::tags::Idle_Chase_Range),
            types.get(ti, objects::tags::Attack_Move_Response_Range), types.get(ti, objects::tags::Autonomous_Move_Extension_Vs_Attacker)};
}

void TacticalBattle::chase_ranges_fill(uint32_t ty) {
    const objects::GameObjectTypes& types = catalog_->types();
    if (fchase_have_.size() != types.size()) {
        fchase_have_.assign(types.size(), 0);
        fchase_cache_.assign(types.size(), {});
    }
    if (ty >= types.size() || fchase_have_[ty]) return;
    fchase_cache_[ty] = chase_ranges(ty);
    fchase_have_[ty] = 1;
}

FighterChaseInputs TacticalBattle::chase_inputs(const ChaseOrder& o) const {
    FighterChaseInputs ci;
    ci.formation = true;
    ci.flags = o.first_flags;  // Rr and the attack-move branch read blocks[0]
    ci.moving = o.moving;
    ci.last_guard = (o.flags & mv::kMoveFlagGuardMove) != 0 && !o.live_push;
    ci.single_position = !ci.moving && !o.live_push && !o.goal_object && !o.order_goal_live;
    if (o.guarded_mover != objects::kNoObjectId && movement_->has_unit(o.guarded_mover)) {
        const mv::UnitView gv = movement_->view(o.guarded_mover);
        ci.guarded_moving = gv.has_goal && gv.speed > 0.0f;
    }
    ci.unit = o.unit;
    ci.goal = o.goal;
    ci.attack_range = o.attack_range;
    const std::array<float, 4> r = chase_ranges(o.range_type);
    ci.guard_range = r[0];
    ci.idle_range = r[1];
    ci.attack_move_range = r[2];
    ci.extension = r[3];
    return ci;
}

uint8_t TacticalBattle::chase_answer(const FighterChaseInputs& ci, float& rr) {
    rr = 0.0f;
    const uint8_t res = fighter_chase_test(ci);
    if (res != 0) {
        rr = (ci.flags & mv::kMoveFlagGuardMove) != 0    ? ci.guard_range
             : (ci.flags & mv::kMoveFlagAttackMove) != 0 ? ci.attack_move_range
                                                         : ci.idle_range;
        if (rr < 0.0f) rr = 0.0f;
        if (ci.candidate_attacker) rr += ci.extension;
    }
    return res;
}

uint32_t TacticalBattle::fighter_chase_type(const Unit& leader, bool squad) const {
    // Rr: the squadron type's (a fighter without one: its own type's).
    if (squad && leader.container < containers_.size() && containers_[leader.container].squad_type < catalog_->types().size())
        return containers_[leader.container].squad_type;
    return leader.type;
}

TacticalBattle::FighterChaseFacts TacticalBattle::fighter_chase_order(const FighterFormationRecord& r, const Unit& leader,
                                                                     std::span<const objects::ObjectId> members,
                                                                     const objects::ObjectStore& store, float attack_range) const {
    FighterChaseFacts f;
    f.members = members;
    // s78f t02: the movement formation's top, when it is a live auto block
    if (const mv::FighterFormation* ff = movement_->fighter_formation(r.key);
        ff && !ff->blocks.empty() && ff->blocks.back().pushed && fighter_goal_live(ff->blocks.back().object, store))
        f.last_push = ff->blocks.back().object;
    f.order_goal = fighter_goal_key(combat_->attack_goal(leader.id));
    const mv::UnitView lv = movement_->view(leader.id);
    const FighterOrderView ov = fighter_order_view(r);  // s78f review: the movement stack's order block
    f.goal_key = fighter_goal_key(ov.goal_object);
    ChaseOrder& o = f.order;
    o.first_flags = ov.first_flags;
    o.flags = ov.flags;
    o.moving = lv.has_goal && !lv.arrived;
    o.live_push = f.last_push != objects::kNoObjectId;
    o.goal_object = ov.goal_object != objects::kNoObjectId;
    o.order_goal_live = fighter_goal_live(f.order_goal, store);
    // Rule 3: the guarded object (a guard block's object)
    f.guarded = (ov.flags & mv::kMoveFlagGuardMove) != 0 && ov.goal_object != objects::kNoObjectId ? unit_c(units_, ov.goal_object) : nullptr;
    if (f.guarded && (!f.guarded->alive || f.guarded->retreated || !store.alive(f.guarded->h))) f.guarded = nullptr;
    o.guarded_mover = f.guarded ? f.guarded->id : objects::kNoObjectId;
    o.unit = store.position(leader.h);
    o.goal = ov.goal;
    if (const Unit* go = ov.goal_object != objects::kNoObjectId ? unit_c(units_, ov.goal_object) : nullptr;
        go && go->alive && !go->retreated && store.alive(go->h))
        o.goal = store.position(go->h);  // an object block: the slot goal follows the object
    o.range_type = fighter_chase_type(leader, fighter_squad_c(r.key) != nullptr);
    o.attack_range = attack_range;
    return f;
}

void TacticalBattle::fighter_chase_candidate(const FighterChaseFacts& f, objects::ObjectId cand, objects::ObjectId tk, Vec3 at,
                                             FighterChaseInputs& ci) const {
    ci.candidate_is_goal = tk == f.order_goal || tk == f.goal_key || (f.last_push != objects::kNoObjectId && tk == f.last_push);
    // c, or every member of c's squadron: it attacks the guarded object or the squadron
    std::span<const objects::ObjectId> attackers(&cand, 1);
    if (const mv::FighterSquad* cs = fighter_squad_c(tk)) attackers = cs->members;
    for (const objects::ObjectId a : attackers) {
        if (!combat_->has_unit(a)) continue;
        const objects::ObjectId t = combat_->current_target(a);
        if (t == objects::kNoObjectId) continue;
        if ((f.guarded && t == f.guarded->id) || std::find(f.members.begin(), f.members.end(), t) != f.members.end())
            ci.candidate_attacker = true;
    }
    ci.candidate = at;
}

// S8 t17 (FIGHTER_FLYING §4.10 A, TACTICAL_COMBAT §2.2): Get_Scan_Range 0x566f50 / the per-candidate acceptance of the
// combat scan for a fighter (a squadron member: its squadron's formation). The inputs are fighter_push's for the auto
// target c; c = none: no goal / attacker match and c at the slot goal (rule 7: "c = null -> 1"). Rr = the first block's
// range (+ Autonomous_Move_Extension_Vs_Attacker for an attacker). Read-only: combat calls it from its parallel gather
// phase (nothing else runs then); the dead pushed blocks fighter_push erases are skipped instead.
uint8_t TacticalBattle::fighter_scan_range(const objects::ObjectStore& store, objects::ObjectId member, objects::ObjectId cand,
                                           float range, float& rr) const {
    rr = 0.0f;
    const Unit* u = unit_c(units_, member);
    if (!u) return 0;
    const FighterFormationRecord* r = fighter_formation(fighter_key(*u));
    if (!r) return 0;
    const mv::FighterSquad* sq = fighter_squad_c(r->key);
    const objects::ObjectId lid = sq && !sq->members.empty() ? sq->members.front() : u->id;
    const Unit* leader = unit_c(units_, lid);
    if (!leader || !movement_->has_unit(lid)) return 0;
    std::span<const objects::ObjectId> members(&leader->id, 1);
    if (sq) members = sq->members;
    const FighterChaseFacts f = fighter_chase_order(*r, *leader, members, store, range);
    FighterChaseInputs ci = chase_inputs(f.order);
    if (cand == objects::kNoObjectId) {
        ci.candidate = ci.goal;
    } else {
        const Unit* c = unit_c(units_, cand);
        if (!c || !store.alive(c->h)) return 0;
        fighter_chase_candidate(f, cand, fighter_goal_key(cand), store.position(c->h), ci);
    }
    return chase_answer(ci, rr);
}
}  // namespace alamo::sim
