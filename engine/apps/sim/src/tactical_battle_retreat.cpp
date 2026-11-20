// TRU1 t06: the space retreat coordinator's jump-out flight (docs/re/TRU1_C_RE.md §2; tactical_battle.hpp "TRU1 t06").
// Between the retreat countdown's end and complete_retreat (coordinator state 3) the listed units are lined up at the
// ATTACKER ENTRY POSITION marker and fly off one by one while the retreat cinematic runs. Serial code, no synced draws.
#include "tactical_battle_internal.hpp"

namespace alamo::sim {

using namespace bo3split;

namespace {

constexpr float kCreepStep = 8.125f;      // per-frame step of every unit not yet released (0x533ac0)
constexpr int32_t kReleaseFrame = 30;     // counter >= 30: units are released
constexpr int32_t kFlightEndFrame = 89;   // counter > 89: the jump-out step is done
constexpr int32_t kBaseKillFrame = 180;   // counter > 180: the player's star bases die (state 4, 0x533e30)
constexpr float kFormationRadius = 2500.0f;
constexpr float kSortDistance = 10000.0f;

}  // namespace

bool TacticalBattle::rt_listed(objects::ObjectId id) const {
    return rt_.state != 0 && std::find(rt_.list.begin(), rt_.list.end(), id) != rt_.list.end();
}

void TacticalBattle::rt_move(TacticalMode& mode, objects::ObjectId id, float step) {
    // Non-forced teleport step along the flight direction (0x3a8f90 flag 0): store, combat and trees follow.
    Unit* u = unit_by_id(id);
    if (!u || !u->alive || u->retreated) return;
    objects::ObjectStore& st = mode.primary();
    if (!st.alive(u->h)) return;
    const Vec3 p0 = st.position(u->h);
    const Vec3 p{p0.x + rt_.dir.x * step, p0.y + rt_.dir.y * step, p0.z + rt_.dir.z * step};
    st.set_position(u->h, p);
    if (combat_->has_unit(u->id)) combat_->set_position(u->id, tv(p));
    mode.trees().update(static_cast<tactical::EntityHandle>(u->id), mode.bounds_of(u->h));
}

bool TacticalBattle::rt_begin_flight(TacticalMode& mode, uint8_t side) {
    // Countdown end, state 1 -> 2 (§2.2): the cinematic runs only in space with In_Game_Cinematics; otherwise state 2
    // ends at its first update and the retreat completes at once (no flight).
    rt_ = RetreatFlight{};
    if (!space_ || !l1_k_.in_game_cinematics || !movement_) return false;
    objects::ObjectStore& st = mode.primary();
    const objects::GameObjectTypes& types = catalog_->types();
    // The retreat list (vf8 §2.1, pruned at the countdown end): the side's live units with a locomotor. [port] Battle units
    // in units_ order (the store order of their creation); a Hyperspace_Away departure and this tick's attrition victims
    // are left out.
    std::vector<objects::ObjectId> list;
    for (const Unit& u : units_) {
        if (u.side != side || !u.alive || u.retreated || !u.movable || !st.alive(u.h)) continue;
        if (l1_jumps_.departing(u.id)) continue;
        if (std::find(bt_attrition_ids_.begin(), bt_attrition_ids_.end(), u.id) != bt_attrition_ids_.end()) continue;
        list.push_back(u.id);
    }
    // Direction (0x49e060): (-1, 0, 0) turned by the marker's pitch and yaw = opposite the marker's facing. [port] No
    // Attacker Entry marker (scenario maps): the attacker side's fallback anchor / facing.
    Vec3 mpos = anchor_[1], mface = facing_[1];
    (void)bt_attacker_entry(mpos, mface);
    {
        const float len = dmath::sqrt(mface.x * mface.x + mface.y * mface.y + mface.z * mface.z);
        rt_.dir = len > 0.0f ? Vec3{-mface.x / len, -mface.y / len, -mface.z / len} : Vec3{-1.0f, 0.0f, 0.0f};
    }
    const Vec3 face = rt_.dir;
    // Formation (0x534110): largest Tactical_Health first (stable), each stopped and teleported to a free spot around the
    // marker (radius 2500), oriented away from the marker; then nearest first to the point 10000 out along that way.
    std::vector<std::pair<int32_t, objects::ObjectId>> by_health;  // Tactical_Health is an int field
    for (const objects::ObjectId id : list) {
        const Unit* u = unit_by_id(id);
        by_health.emplace_back(types.get(u->type, objects::tags::Tactical_Health), id);
    }
    std::stable_sort(by_health.begin(), by_health.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    constexpr uint64_t kLayer20 = 0x20;  // Space_Layer 0x20: never a blocker (space_member_spots)
    for (const auto& e : by_health) {
        Unit* u = unit_by_id(e.second);
        // Blockers: every other live object with a model box (as space_member_spots), at its current place.
        std::vector<FreePlacementBox> blockers;
        for (const objects::ObjectHandle h : st.all_objects()) {
            if (h == u->h || !st.alive(h) || st.dying(h)) continue;
            const objects::TypeIndex t = st.type(h);
            if (t >= types.size() || types.get(t, objects::tags::Is_Decoration)) continue;
            if (st.has_behavior(h, objects::beh::Projectile) || types.get(t, objects::tags::Space_Layer) == kLayer20) continue;
            const tactical::Aabb b = mode.bounds_of(h);
            if (!(b.max.x > b.min.x) && !(b.max.y > b.min.y)) continue;
            blockers.push_back({b.min.x, b.min.y, b.max.x, b.max.y});
        }
        float qx = mpos.x, qy = mpos.y;
        if (const BattleCatalog::ModelInfo* m = catalog_->model_info(u->type, true); m && m->has_bounds) {
            const FreePlacementBox local{m->min[0], m->min[1], m->max[0], m->max[1]};
            if (!find_free_placement(mpos.x, mpos.y, local, blockers, kFormationRadius, qx, qy)) {
                qx = mpos.x;
                qy = mpos.y;
            }
        }
        const Vec3 p{qx, qy, 0.0f};
        // Stop (locomotor vf +0x200): out of path following, the coordinator owns the position until the end.
        movement_->remove_unit(u->id);
        u->movable = false;
        if (abilities_->has_object(u->id)) abilities_->set_locomotor_busy(u->id, true);
        st.set_position(u->h, p);
        st.set_facing(u->h, face);
        if (combat_->has_unit(u->id)) combat_->set_position(u->id, tv(p));
        mode.trees().update(static_cast<tactical::EntityHandle>(u->id), mode.bounds_of(u->h));
    }
    const Vec3 far{mpos.x + face.x * kSortDistance, mpos.y + face.y * kSortDistance, 0.0f};
    std::vector<std::pair<float, objects::ObjectId>> by_dist;
    for (const auto& e : by_health) {
        const Vec3 p = st.position(unit_by_id(e.second)->h);
        const float dx = far.x - p.x, dy = far.y - p.y, dz = far.z - p.z;
        by_dist.emplace_back(dmath::sqrt(dx * dx + dy * dy + dz * dz), e.second);
    }
    std::stable_sort(by_dist.begin(), by_dist.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& e : by_dist) rt_.list.push_back(e.second);
    rt_.state = 2;
    bt_used_ = true;
    return true;
}

bool TacticalBattle::rt_flight_tick(TacticalMode& mode) {
    ++rt_.counter;  // [port] the cinematic camera's counter: one step per frame (counter > 19: the stretch effect, not modelled)
    const int32_t c = rt_.counter;
    const int32_t n = static_cast<int32_t>(rt_.list.size());
    if (rt_.state == 2) {
        if (c > kFlightEndFrame) {
            rt_.state = 4;  // the cinematic is still running
        } else if (c < kReleaseFrame) {
            for (const objects::ObjectId id : rt_.list) rt_move(mode, id, kCreepStep);
            return false;
        } else {
            objects::ObjectStore& st = mode.primary();
            if (rt_.release == rt_.sound) {
                // The released unit's jump sound (AUDIO.md §9.6; presentation log). [port] obj +0x3a0 bits 0x40 / 0x80 are
                // not modelled.
                if (rt_.release >= 0 && rt_.release < n)
                    if (const Unit* u = unit_by_id(rt_.list[static_cast<size_t>(rt_.release)]); u && u->alive && !u->retreated && st.alive(u->h))
                        hyperspace_sfx_.push_back({HyperspaceSfx::Kind::Depart, u->id, u->type, st.owner(u->h), st.position(u->h)});
                ++rt_.sound;
            }
            const int32_t released = std::min(rt_.release, n - 1);
            const float flight = retreat_flight_increment(side_player(static_cast<uint8_t>(retreat_side_)));
            for (int32_t i = 0; i < n; ++i) rt_move(mode, rt_.list[static_cast<size_t>(i)], i <= released ? flight : kCreepStep);
            if (--rt_.wait < 1) {
                rt_.release = std::min(rt_.release + 1, n - 1);
                rt_.wait = retreat_increment_frames(side_player(static_cast<uint8_t>(retreat_side_)));
            }
            return false;
        }
    }
    // State 4 (0x533e30): past counter 180 the player's star bases die (complete_retreat kills every object of the player
    // that did not leave, the station included, through this tick's combat phase D). [port] The camera's own end is not
    // modelled: the wait ends there.
    if (c <= kBaseKillFrame) return false;
    // trl2 t05 (TRU1_C_RE §2.4, MCX4_T15_RE_ANSWERS): each star base of the retreating side first spawns its type's
    // Retreat_Self_Destruct_Explosion at its position (no draw). [port] star base = a starbase participant's live unit.
    objects::ObjectStore& st = mode.primary();
    const objects::GameObjectTypes& types = catalog_->types();
    for (const Unit& u : units_) {
        if (u.side != static_cast<uint8_t>(retreat_side_) || !u.alive || u.retreated || u.participant >= parts_.size() ||
            !parts_[u.participant].starbase || !st.alive(u.h))
            continue;
        // ledger: consumes GameObjectType.Retreat_Self_Destruct_Explosion
        const objects::TypeIndex x = types.get_type_ref(static_cast<objects::TypeIndex>(u.type), "Retreat_Self_Destruct_Explosion");
        if (x != objects::kNone && x < types.size()) spawn_particle(mode, static_cast<int32_t>(x), st.position(u.h), st.owner(u.h));
    }
    return true;
}

}  // namespace alamo::sim
