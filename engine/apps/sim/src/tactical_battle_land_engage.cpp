// tactical_battle: cmb1 t01 / t03 - the land formation records (LandFormationRecord), land Auto_Engage and the land scan
// range (docs/re/BKR1_RE_ANSWERS.md Q2 / Q8 [H], FIGHTER_FLYING §4.10 A / §4.11 B / E [H]).
#include "tactical_battle_internal.hpp"

namespace alamo::sim {
using namespace bo3split;

namespace {
// A unit of this battle by id, const (units_ is sorted by id).
template <class Units>
auto unit_in(const Units& units, objects::ObjectId id) -> const typename Units::value_type* {
    const auto it = std::lower_bound(units.begin(), units.end(), id, [](const auto& x, objects::ObjectId v) { return x.id < v; });
    return it == units.end() || it->id != id ? nullptr : &*it;
}
}  // namespace

void TacticalBattle::land_order(std::span<const objects::ObjectId> ids, uint8_t flags, Vec3 goal, objects::ObjectId goal_object) {
    if (space_ || !movement_) return;
    for (const objects::ObjectId id : ids) {
        const Unit* u = unit_by_id(id);
        if (!u || !u->alive || u->retreated || !u->movable || movement_locked(id)) continue;  // a locked unit keeps its formation
        const objects::ObjectId mover = attack_mover(id);
        if (!movement_->has_unit(mover)) continue;
        auto it = std::lower_bound(lformations_.begin(), lformations_.end(), mover,
                                   [](const LandFormationRecord& r, objects::ObjectId v) { return r.mover < v; });
        if (it == lformations_.end() || it->mover != mover) it = lformations_.insert(it, LandFormationRecord{});
        LandFormationRecord nr;
        nr.mover = mover;
        nr.flags = static_cast<uint8_t>(flags & (mv::kMoveFlagAttackMove | mv::kMoveFlagGuardMove));
        nr.goal = goal;
        nr.goal_object = goal_object;
        nr.seen = goal;
        *it = nr;  // a new coordinator: the old stack (and its auto block) is gone (FIGHTER_FLYING §4.11 B 3)
    }
}

void TacticalBattle::land_order_clear(std::span<const objects::ObjectId> ids) {
    for (const objects::ObjectId id : ids) {
        const objects::ObjectId mover = attack_mover(id);
        std::erase_if(lformations_, [mover](const LandFormationRecord& r) { return r.mover == mover; });
    }
}

const LandFormationRecord* TacticalBattle::land_formation(objects::ObjectId mover) const {
    const auto it = std::lower_bound(lformations_.begin(), lformations_.end(), mover,
                                     [](const LandFormationRecord& r, objects::ObjectId v) { return r.mover < v; });
    return it != lformations_.end() && it->mover == mover ? &*it : nullptr;
}

bool TacticalBattle::land_auto_pushed(objects::ObjectId unit) const {
    if (space_ || lformations_.empty()) return false;
    const LandFormationRecord* r = land_formation(attack_mover(unit));
    return r && r->pushed != objects::kNoObjectId;
}

std::vector<objects::ObjectId> TacticalBattle::land_mover_units(objects::ObjectId mover) const {
    std::vector<objects::ObjectId> out;
    if (movement_ && movement_->is_land_team_container(mover)) {
        for (const auto& [mid, slot] : movement_->land_team_slots(mover)) {
            (void)slot;
            const Unit* m = unit_in(units_, mid);
            if (m && m->alive && !m->retreated) out.push_back(mid);
        }
    } else {
        out.push_back(mover);
    }
    return out;
}

objects::ObjectId TacticalBattle::land_mover_target_unit(objects::ObjectId mover) {
    // Service_Land runs on the TARGETING owner: a land team container's own TeamTargeting, else the unit (BKR1 Q8: on land
    // the owner is not replaced by its container)
    if (combat_->has_unit(mover)) return mover;
    return attack_lead(mover);
}

const TacticalBattle::Unit* TacticalBattle::land_goal_unit(const objects::ObjectStore& st, objects::ObjectId id) const {
    const auto live = [&](objects::ObjectId x) -> const Unit* {
        const Unit* u = x != objects::kNoObjectId ? unit_in(units_, x) : nullptr;
        return u && u->alive && !u->retreated && st.alive(u->h) ? u : nullptr;
    };
    if (const Unit* u = live(id)) return u;
    if (id == objects::kNoObjectId || !movement_->is_land_team_container(id)) return nullptr;
    for (const auto& [mid, slot] : movement_->land_team_slots(id)) {
        (void)slot;
        if (const Unit* m = live(mid)) return m;
    }
    return nullptr;
}

bool TacticalBattle::land_goal_member(objects::ObjectId goal, objects::ObjectId id) const {
    if (goal == objects::kNoObjectId || id == objects::kNoObjectId) return false;
    if (id == goal) return true;
    if (!movement_->is_land_team_container(goal)) return false;
    for (const auto& [mid, slot] : movement_->land_team_slots(goal)) {
        (void)slot;
        if (mid == id) return true;
    }
    return false;
}

uint32_t TacticalBattle::land_type_of(objects::ObjectId mover) const {
    if (const Unit* u = unit_in(units_, mover)) return u->type;
    for (const Container& c : containers_)  // u +0x337 = -1: the company type 0x3973b0 (the container's own type without one)
        if (c.id == mover && !c.removed) return c.squad_type != 0xFFFFFFFFu ? c.squad_type : c.type;
    return 0xFFFFFFFFu;
}

// 0x4160a0(F, u, c, &Rr) on land: FIGHTER_FLYING §4.10 A without the Space_Layer gate (BKR1 Q8 [H]); the rules are
// fighter_chase_test's. [port] F state 3 ("arrived") is the mover's own order done (UnitView::has_goal false); F's current
// path point of the attack-move branch is not modelled ([M] the unit answers 2 while its attack-move is under way); the slot
// goal is the mover's movement goal (an object block: the object's position).
uint8_t TacticalBattle::land_chase(const objects::ObjectStore& store, const LandFormationRecord& r, objects::ObjectId unit,
                                   objects::ObjectId cand, float range, float& rr) const {
    rr = 0.0f;
    if (r.locked || !movement_->has_unit(r.mover)) return 0;  // u order-locked (0x39ad40)
    const mv::UnitView v = movement_->view(r.mover);
    if (v.deploy_state != mv::LocoState::None) return 0;  // deployed with type +0x23b0 = 0 [port: any deploy state]
    const auto live = [&](objects::ObjectId id) -> const Unit* {
        const Unit* x = id != objects::kNoObjectId ? unit_in(units_, id) : nullptr;
        return x && x->alive && !x->retreated && store.alive(x->h) ? x : nullptr;
    };
    FighterChaseInputs ci;
    ci.formation = true;
    ci.flags = r.flags;  // Rr and the attack-move branch read blocks[0]
    ci.moving = v.has_goal;
    ci.last_guard = (r.flags & mv::kMoveFlagGuardMove) != 0 && r.pushed == objects::kNoObjectId;
    ci.single_position = !ci.moving && r.pushed == objects::kNoObjectId && r.goal_object == objects::kNoObjectId;
    // a guarded TEAM container: its own mover, its first live member's position
    const Unit* guarded = (r.flags & mv::kMoveFlagGuardMove) != 0 ? land_goal_unit(store, r.goal_object) : nullptr;
    const objects::ObjectId gm = guarded && movement_->has_unit(r.goal_object) ? r.goal_object : guarded ? guarded->id : objects::kNoObjectId;
    if (gm != objects::kNoObjectId && movement_->has_unit(gm)) {
        const mv::UnitView gv = movement_->view(gm);
        ci.guarded_moving = gv.has_goal && gv.speed > 0.0f;
    }
    ci.unit = Vec3{v.pos.x, v.pos.y, 0.0f};
    ci.goal = Vec3{v.goal.x, v.goal.y, 0.0f};
    if (const Unit* go = land_goal_unit(store, r.goal_object)) ci.goal = store.position(go->h);
    ci.attack_range = range;
    const uint32_t ty = land_type_of(r.mover);
    if (ty < fchase_have_.size() && fchase_have_[ty]) {
        ci.guard_range = fchase_cache_[ty][0];
        ci.idle_range = fchase_cache_[ty][1];
        ci.attack_move_range = fchase_cache_[ty][2];
        ci.extension = fchase_cache_[ty][3];
    } else if (ty < catalog_->types().size()) {
        const objects::GameObjectTypes& types = catalog_->types();
        const auto ti = static_cast<objects::TypeIndex>(ty);
        ci.guard_range = types.get_float(ti, "Guard_Chase_Range");
        ci.idle_range = types.get_float(ti, "Idle_Chase_Range");
        ci.attack_move_range = types.get_float(ti, "Attack_Move_Response_Range");
        ci.extension = types.get_float(ti, "Autonomous_Move_Extension_Vs_Attacker");
    }
    if (cand == objects::kNoObjectId) {
        ci.candidate = ci.goal;
    } else {
        const Unit* c = live(cand);
        if (!c) return 0;
        ci.candidate_is_goal = land_goal_member(r.goal_object, cand) || cand == r.pushed;  // c is the object of the first or the last block
        // c attacks the guarded object or u itself (0x39b1c0: its attacker list; [port] its current combat target)
        const objects::ObjectId at = combat_->has_unit(c->id) ? combat_->current_target(c->id) : objects::kNoObjectId;
        if (at != objects::kNoObjectId) {
            if (guarded && land_goal_member(r.goal_object, at)) ci.candidate_attacker = true;
            if (at == unit || at == r.mover) ci.candidate_attacker = true;
            if (!ci.candidate_attacker && movement_->is_land_team_container(r.mover))
                for (const auto& [mid, slot] : movement_->land_team_slots(r.mover)) {
                    (void)slot;
                    if (at == mid) ci.candidate_attacker = true;
                }
        }
        ci.candidate = store.position(c->h);
    }
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

uint8_t TacticalBattle::land_scan_range(const objects::ObjectStore& store, objects::ObjectId unit, objects::ObjectId cand, float range,
                                        float& rr) const {
    rr = 0.0f;
    const LandFormationRecord* r = land_formation(attack_mover(unit));
    return r ? land_chase(store, *r, unit, cand, range, rr) : 0;
}

void TacticalBattle::land_engage_service(TacticalMode& mode, SimRng& rng) {
    if (space_ || !movement_ || !combat_) return;
    const objects::ObjectStore& st = mode.primary();
    const auto live = [&](objects::ObjectId id) -> const Unit* {
        const Unit* x = id != objects::kNoObjectId ? unit_by_id(id) : nullptr;
        return x && x->alive && !x->retreated && st.alive(x->h) && combat_->has_unit(x->id) ? x : nullptr;
    };
    // the records of movers that are gone (death, retreat, a squad emptied)
    std::erase_if(lformations_, [&](const LandFormationRecord& r) {
        if (!movement_->has_unit(r.mover)) return true;
        if (movement_->is_land_team_container(r.mover)) return land_mover_units(r.mover).empty();
        const Unit* u = unit_by_id(r.mover);
        return !u || !u->alive || u->retreated;
    });
    // derived per tick (serial): the order lock of the members (0x39ad40; Push 0x41fd30 refuses any locked member) and the
    // chase ranges of each record's type (read by the parallel scan)
    const objects::GameObjectTypes& types = catalog_->types();
    if (fchase_have_.size() != types.size()) {
        fchase_have_.assign(types.size(), 0);
        fchase_cache_.assign(types.size(), {});
    }
    for (LandFormationRecord& r : lformations_) {
        r.locked = false;
        for (const objects::ObjectId id : land_mover_units(r.mover)) r.locked = r.locked || movement_locked(id);
        const uint32_t ty = land_type_of(r.mover);
        if (ty < types.size() && !fchase_have_[ty]) {
            const auto ti = static_cast<objects::TypeIndex>(ty);
            fchase_cache_[ty] = {types.get_float(ti, "Guard_Chase_Range"), types.get_float(ti, "Idle_Chase_Range"),
                                 types.get_float(ti, "Attack_Move_Response_Range"),
                                 types.get_float(ti, "Autonomous_Move_Extension_Vs_Attacker")};
            fchase_have_[ty] = 1;
        }
    }
    // cmb1 t03 (Get_Scan_Range 0x566f50 on land, BKR1 Q8 [H]): the TARGETING owner o (a land team container, else the unit)
    // asks the formation (kUnitScanQuery, land_scan_range) when it has one and is not deployed; else R (steps 1 / 2). [port]
    // a steering squad's TeamTargeting runs on its head member (from the container's position): its members carry the flag.
    for (const Unit& u : units_) {
        if (!combat_->has_unit(u.id)) continue;
        bool q = false;
        const objects::ObjectId mover = attack_mover(u.id);
        if (u.alive && !u.retreated && movement_->has_unit(mover))
            q = land_formation(mover) != nullptr && movement_->view(mover).deploy_state == mv::LocoState::None;
        combat_->set_flag(u.id, tc::kUnitScanQuery, q);
    }
    for (const Container& c : containers_) {
        if (c.removed || !combat_->has_unit(c.id)) continue;
        combat_->set_flag(c.id, tc::kUnitScanQuery, movement_->has_unit(c.id) && land_formation(c.id) != nullptr);
    }
    // Per record, ascending mover (land_attack_order / land_pop do not touch lformations_).
    for (size_t i = 0; i < lformations_.size(); ++i) {
        LandFormationRecord& r = lformations_[i];
        const objects::ObjectId tu = land_mover_target_unit(r.mover);
        const Unit* lead = live(attack_lead(r.mover));
        if (tu == objects::kNoObjectId || !combat_->has_unit(tu) || !lead) continue;
        const mv::UnitView v = movement_->view(r.mover);
        const int32_t me = st.owner(lead->h);
        // cmb1 t04 / t06: blocks[0]'s last-observed point (0x584750 at every formation step, the lead as viewer): seen ->
        // the object's position; hidden -> frozen; stealthed -> the formation lead's position, which is the member nearest
        // the movers' centroid and is taken where the movers are known (BattleAi::guard_follow_step)
        if (const Unit* go = land_goal_unit(st, r.goal_object)) {
            bool hidden = false, stealthed = false;
            attack_target_unseen(st, *lead, go->id, hidden, stealthed);
            if (!hidden && !stealthed) r.seen = st.position(go->h);
        }
        // 1. The auto block on top (n = 2). FIGHTER_FLYING §4.11 B 3 / E [H]: while it is the top, 0x4192b0 (moving, state 1)
        //    re-paths it only when 0x4165f0 holds (the leash), else step 5 pops it (no 0x04); 0x410a60 (arrived, state 3)
        //    pops it when the leash fails and (c is false or blocks[0] names an object). Its object's death ends it too
        //    ([port] at once; retail converts it to a position at its path end and pops it there on arrival).
        if (r.pushed != objects::kNoObjectId) {
            const Unit* pu = live(r.pushed);
            bool pop = !pu || !combat_->is_enemy(me, st.owner(pu->h));
            if (!pop) {  // the attack block that carries it is gone (a C 3 drop, a newer block)
                const auto it = std::lower_bound(attack_blocks_.begin(), attack_blocks_.end(), r.mover,
                                                 [](const AttackBlock& b, objects::ObjectId m) { return b.mover < m; });
                pop = it == attack_blocks_.end() || it->mover != r.mover || it->target != r.pushed;
            }
            if (!pop && frame_ >= r.next) {
                r.next = frame_ + 30u;
                bool hidden = false, stealthed = false;
                attack_target_unseen(st, *lead, r.pushed, hidden, stealthed);
                // 0x4165f0 for n >= 2 (§4.11 E): blocks[0] attack-move -> Attack_Move_Response_Range around F's path point
                // ([port] the formation's own position: the lead stays inside); else Guard / Idle_Chase_Range around the
                // goal of blocks[n-2] (the order's goal point). The attacker extension; a moving guarded object answers its
                // attackers only; the object farther than Rr from the anchor -> the 0x5672b0 point test ([port] within
                // Rr + the attack range, as fighter_chase_test). A hidden object -> false.
                const uint32_t ty = land_type_of(r.mover);
                const bool am = (r.flags & mv::kMoveFlagAttackMove) != 0;
                float rr = 0.0f, ext = 0.0f;
                if (ty < fchase_have_.size() && fchase_have_[ty]) {
                    rr = am ? fchase_cache_[ty][2] : (r.flags & mv::kMoveFlagGuardMove) != 0 ? fchase_cache_[ty][0] : fchase_cache_[ty][1];
                    ext = fchase_cache_[ty][3];
                }
                if (rr < 0.0f) rr = 0.0f;
                const Unit* go = land_goal_unit(st, r.goal_object);
                Vec3 anchor = am ? Vec3{v.pos.x, v.pos.y, 0.0f} : go ? st.position(go->h) : r.goal;
                const objects::ObjectId at = combat_->current_target(pu->id);
                const std::vector<objects::ObjectId> us = land_mover_units(r.mover);
                const bool attacker = at != objects::kNoObjectId && ((go && (r.flags & mv::kMoveFlagGuardMove) && land_goal_member(r.goal_object, at)) || at == r.mover ||
                                                                    std::find(us.begin(), us.end(), at) != us.end());
                if (attacker) rr += ext;
                bool a = !hidden;
                const objects::ObjectId gm = go && movement_->has_unit(r.goal_object) ? r.goal_object : go ? go->id : objects::kNoObjectId;
                if (a && go && (r.flags & mv::kMoveFlagGuardMove) && !attacker && movement_->has_unit(gm)) {
                    const mv::UnitView gv = movement_->view(gm);
                    if (gv.has_goal && gv.speed > 0.0f) a = false;
                }
                const auto d2 = [](Vec3 p, Vec3 q) { return (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y); };
                const Vec3 L{v.pos.x, v.pos.y, 0.0f}, cp = st.position(pu->h);
                if (a && !(d2(L, anchor) < rr * rr)) a = false;
                if (a && d2(cp, anchor) > rr * rr) {
                    const float far = rr + combat_->attack_range(tu);
                    a = d2(cp, anchor) <= far * far;
                }
                if (v.has_goal) {
                    pop = !a;
                } else {  // 0x40edd0: the mover tested through its lead (a squad container: its first live member, D 5)
                    bool c = !hidden && !stealthed;
                    if (c) {
                        const tc::CanHit h = attack_can_hit(lead->id, r.pushed);
                        c = h == tc::CanHit::Yes || h == tc::CanHit::NoTarget;
                    }
                    pop = !a && (!c || r.goal_object != objects::kNoObjectId);
                }
            }
            if (pop) {
                land_pop(mode, r, rng);
                continue;
            }
        }
        // 2. Auto_Engage (Service_Land 0x634810 -> locomotor vf +0x260 for every service with a target, locked or not):
        //    LocomotorCommon 0x42d0f0 for Walk / Bike / LandTeamContainer / LandTeamInfantry ([port] JetPack's own 0x6418f0
        //    and the air locomotors are not read: they do not push). F's goal 0x41a2e0(F, -1) = the newest block with a
        //    hostile object, never a guard; t != goal and rr = 0x4160a0(F, u, t) != 0 -> the auto block {kind 3, t, flags 0,
        //    +0x5d = 1} ([port] rr = 1's split into a formation of its own: the record is per mover already).
        if (r.locked) continue;
        if (v.locomotor != mv::LocomotorKind::Default && v.locomotor != mv::LocomotorKind::Bike) continue;
        const objects::ObjectId t = combat_->current_target(tu);
        const Unit* tt = live(t);
        if (!tt || !combat_->is_enemy(me, st.owner(tt->h))) continue;
        const objects::ObjectId goal =
            r.pushed != objects::kNoObjectId ? r.pushed : (r.flags & mv::kMoveFlagGuardMove) != 0 ? objects::kNoObjectId : r.goal_object;
        if (t == goal || (r.pushed == objects::kNoObjectId && land_goal_member(goal, t))) continue;
        float rr = 0.0f;
        if (land_chase(st, r, tu, t, combat_->attack_range(tu), rr) == 0) continue;
        // Push 0x41fd30: the block goes on top (the pre-pop drops an earlier auto block, never the order's own); the
        // formation goal becomes t (path B locks it in weapon range: the units' attack goal); the members plan to it
        // (land_attack_order; the AttackBlock with kAttackBlockNoLock carries its 30-frame checks: no unit's order names it)
        const std::vector<objects::ObjectId> us = land_mover_units(r.mover);
        if (us.empty()) continue;
        const Vec3 tp = st.position(tt->h);
        r.pushed = t;
        r.next = frame_ + 30u;
        // cmb2 t04 (FIGHTER_FLYING §4.11 B, BKT1_ATTACK_RE §1.3 [H]): Push gives the block a hard point when the object has
        // hard points (0x394610 Choose_Target_Hardpoint for the TARGETING unit; -1 none)
        const int32_t hp = combat_->hardpoint_count(t) > 0 ? combat_->choose_target_hardpoint(tu, t) : -1;
        for (const objects::ObjectId id : us)
            if (combat_->has_unit(id)) combat_->set_attack_goal(id, t, hp);
        attack_block_set(us.front(), t, kAttackBlockNoLock, tp, hp);
        (void)land_attack_order(mode, us, t, tp, 0, false, rng);
        ++stats_.land_auto_pushes;
    }
}

// 0x4105a0 of the auto block (FIGHTER_FLYING §4.11 B 1): the order's own block is the top again and the formation plans to
// it (state 5 / 0 [M]): an object block (attack / guard) re-plans on its object, a position block moves to its point. The
// formation goal (path B) returns to the order's object (a guard: none). [port] the members' combat targets are left alone.
void TacticalBattle::land_pop(TacticalMode& mode, LandFormationRecord& r, SimRng& rng) {
    const objects::ObjectId was = r.pushed;
    r.pushed = objects::kNoObjectId;
    ++stats_.land_auto_pops;
    const auto it = std::lower_bound(attack_blocks_.begin(), attack_blocks_.end(), r.mover,
                                     [](const AttackBlock& b, objects::ObjectId m) { return b.mover < m; });
    if (it != attack_blocks_.end() && it->mover == r.mover && it->target == was) attack_blocks_.erase(it);
    const std::vector<objects::ObjectId> us = land_mover_units(r.mover);
    if (us.empty()) return;
    const objects::ObjectStore& st = mode.primary();
    const objects::ObjectId goal = (r.flags & mv::kMoveFlagGuardMove) != 0 ? objects::kNoObjectId : r.goal_object;
    for (const objects::ObjectId id : us)
        if (combat_->has_unit(id)) combat_->set_attack_goal(id, goal);
    // the object (a TEAM container through its first live member's position; the order names the container)
    const Unit* go = land_goal_unit(st, r.goal_object);
    if (go && combat_->has_unit(go->id)) {
        const Vec3 tp = st.position(go->h);
        if ((r.flags & mv::kMoveFlagGuardMove) != 0) {
            (void)land_attack_order(mode, us, r.goal_object, tp, mv::kMoveFlagGuardMove, false, rng, true);
            return;
        }
        const Unit* lu = unit_by_id(us.front());
        const bool hostile = lu && combat_->is_enemy(st.owner(lu->h), st.owner(go->h));
        attack_block_set(us.front(), r.goal_object, static_cast<uint8_t>(r.flags | (hostile ? 0 : kAttackBlockNoLock)), tp);
        (void)land_attack_order(mode, us, r.goal_object, tp, r.flags, false, rng);
        return;
    }
    // a position block (or an object block whose object is gone: its position then)
    mv::MoveOrder o;
    for (const objects::ObjectId id : us)
        if (!movement_locked(id)) o.units.push_back(id);
    if (o.units.empty()) return;
    const tactical::Aabb& bb = mode.bounds();
    o.destination = {std::clamp(r.goal.x, bb.min.x, bb.max.x), std::clamp(r.goal.y, bb.min.y, bb.max.y)};
    o.flags = r.flags;
    movement_->move(o, rng, frame_);
}
}  // namespace alamo::sim
