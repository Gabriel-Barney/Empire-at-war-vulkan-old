// TRU1 t05 / t07: SPAWN_INDIGENOUS_UNITS (behaviour 0x1b) and DEBRIS (behaviour 0x28), docs/re/TRU1_C_RE.md §1 / §3;
// tactical_battle.hpp "TRU1 t05 / t07". Serial code; synced draws in the order the original makes them, objects in
// ascending id order.
#include "tactical_battle_internal.hpp"
#include "alamo/core/frames.hpp"

namespace alamo::sim {

using namespace bo3split;

namespace {

constexpr objects::BehaviorTypeId kSpawnerBehavior = 0x1B;     // SPAWN_INDIGENOUS_UNITS
constexpr objects::BehaviorTypeId kDebrisBehavior = 0x28;      // DEBRIS
constexpr objects::BehaviorTypeId kIndigenousBehavior = 0x20;  // INDIGENOUS_UNIT (tru2 t01)
// tru2 t01 (MCX4_T11_T12_RE_ANSWERS "Max_Distance_From_Spawner" step 5): the wander step bounds (max, min), .data
// 0x140a442c8 / cc, and while burning 0x140a442d0 / d4.
constexpr float kWanderStepMax = 400.0f, kWanderStepMin = 50.0f;
constexpr float kBurningStepMax = 150.0f, kBurningStepMin = 60.0f;
constexpr float kWanderStayChance = 0.1f;  // Can_Indigenous_Unit_Stop: a draw above it stays put
constexpr int32_t kPlacementTries = 10;                        // 0x140a436a0
constexpr float kPackRadius = 50.0f;                           // pack members around the first member
constexpr int32_t kSnapRings = 16;                             // [port] nearest passable cell search (cells)

objects::TypeIndex first_type_of_list(const objects::GameObjectTypes& types, objects::TypeIndex t, const objects::ListTag& tag) {
    for (const std::string_view s : types.get(t, tag)) {
        const objects::TypeIndex x = types.resolve_name(ascii_trim(s));
        if (x != objects::kNone && x < types.size()) return x;
    }
    return objects::kNone;
}

std::vector<objects::TypeIndex> spawned_types(const objects::GameObjectTypes& types, objects::TypeIndex t) {
    // Spawned_Indigenous_Units_Type in list order; unknown names keep their slot as kNone (the index draw covers the whole list).
    std::vector<objects::TypeIndex> out;
    for (const std::string_view s : types.get(t, objects::tags::Spawned_Indigenous_Units_Type)) out.push_back(types.resolve_name(ascii_trim(s)));
    return out;
}

Vec3 facing_of_angles(float pitch_deg, float yaw_deg) {
    // [port] the store keeps a facing vector: yaw about z from +x, pitch towards +z; roll is not visible in it.
    float sy = 0.0f, cy = 1.0f, sp = 0.0f, cp = 1.0f;
    dmath::sincos(yaw_deg * (dmath::kPiF / 180.0f), sy, cy);
    dmath::sincos(pitch_deg * (dmath::kPiF / 180.0f), sp, cp);
    return {cp * cy, cp * sy, sp};
}

}  // namespace

void TacticalBattle::sp_add_types() {
    // The units a map spawner can make (and a pack's team types) join the battle tables before combat exists.
    const objects::GameObjectTypes& types = catalog_->types();
    const TypeCrcIndex index(types);
    const size_t before = types_used_.size();
    auto use = [this](uint32_t t) {
        if (std::find(types_used_.begin(), types_used_.end(), t) == types_used_.end()) types_used_.push_back(t);
    };
    for (const terrain::ObjectPlacement& pl : map_.objects) {
        const objects::TypeIndex t = index.find(pl.type_crc);
        if (t == objects::kNone || t >= types.size() || !types.has_behavior(t, objects::BehaviorSet::Union, kSpawnerBehavior)) continue;
        const std::vector<objects::TypeIndex> list = spawned_types(types, t);
        for (const objects::TypeIndex x : list)
            if (x != objects::kNone && x < types.size()) use(static_cast<uint32_t>(x));
        const objects::TypeIndex pack = types.resolve_name(ascii_trim(types.get(t, objects::tags::Spawned_Indigenous_Pack_Type)));
        const objects::TypeIndex first = first_type_of_list(types, t, objects::tags::Spawned_Indigenous_Units_Type);
        uint32_t ot = 0xFFFFFFFFu, bt = 0xFFFFFFFFu, squad = 0;
        if (pack != objects::kNone && pack < types.size() && first != objects::kNone &&
            bt_team(static_cast<uint32_t>(pack), static_cast<uint32_t>(first), ot, bt, squad)) {
            use(ot);
            use(bt);
        }
    }
    if (types_used_.size() != before) tables_extend();
}

void TacticalBattle::sp_tick(TacticalMode& mode, SimRng& rng) {
    objects::ObjectStore& st = mode.primary();
    const objects::GameObjectTypes& types = catalog_->types();
    // 1. New objects (ascending id): a spawner joins the global list (vf7) with its spawned list empty and its next spawn
    //    frame = now (vf9); a debris piece draws its lifetime and gets its attached particle (vf9 0x62aca0).
    std::vector<objects::ObjectHandle> fresh;
    objects::ObjectId top = sp_scanned_;
    for (const objects::ObjectHandle h : st.all_objects()) {
        if (!st.alive(h)) continue;
        const objects::ObjectId id = st.id(h);
        if (id <= sp_scanned_) continue;
        top = std::max(top, id);
        if (st.has_behavior(h, kSpawnerBehavior) || st.has_behavior(h, kDebrisBehavior) || st.has_behavior(h, kIndigenousBehavior))
            fresh.push_back(h);
    }
    const bool first_scan = sp_scanned_ == 0;
    sp_scanned_ = top;
    std::sort(fresh.begin(), fresh.end(), [&st](objects::ObjectHandle a, objects::ObjectHandle b) { return st.id(a) < st.id(b); });
    const uint32_t mf = st.mode_frame();
    for (const objects::ObjectHandle h : fresh) {
        if (st.has_behavior(h, kIndigenousBehavior)) sp_indigenous_.push_back(st.id(h));  // tru2 t01 (ascending id)
        if (!st.has_behavior(h, kSpawnerBehavior) && !st.has_behavior(h, kDebrisBehavior)) continue;
        sp_used_ = true;
        if (st.has_behavior(h, kSpawnerBehavior)) {
            IndigenousSpawner s;
            s.id = st.id(h);
            s.next = mf;
            sp_spawners_.push_back(std::move(s));
        }
        if (st.has_behavior(h, kDebrisBehavior)) {
            const objects::TypeIndex t = st.type(h);
            DebrisPiece d;
            d.id = st.id(h);
            const int32_t life = rng.range_int(types.get(t, objects::tags::Debris_Min_Lifetime_Seconds), types.get(t, objects::tags::Debris_Max_Lifetime_Seconds));
            d.expiry = life < 1 ? 0u : mf + static_cast<uint32_t>(life) * 30u;
            const objects::TypeIndex pt = types.resolve_name(ascii_trim(types.get(t, objects::tags::Debris_Attached_Particle)));
            if (pt != objects::kNone && pt < types.size()) {
                objects::SpawnParams sp;
                sp.type = pt;
                sp.owner = st.owner(h);
                sp.position = st.position(h);
                sp.facing = st.facing(h);
                const objects::ObjectHandle ph = st.create(sp);  // attached: follows the piece, removed with it
                if (st.alive(ph)) d.particle = st.id(ph);
            }
            const Vec3 f = st.facing(h);
            d.yaw = f.x == 0.0f && f.y == 0.0f ? 0.0f : dmath::atan2(f.y, f.x) * (180.0f / dmath::kPiF);
            d.pitch = dmath::atan2(f.z, dmath::sqrt(f.x * f.x + f.y * f.y)) * (180.0f / dmath::kPiF);
            sp_debris_.push_back(d);
        }
    }
    // 2. Removals: a gone spawner leaves the list (vf15); a gone spawned object leaves its spawner's list (vf16).
    std::erase_if(sp_spawners_, [&st](const IndigenousSpawner& s) { return !st.alive(st.find(s.id)); });
    for (IndigenousSpawner& s : sp_spawners_)
        std::erase_if(s.spawned, [&st](objects::ObjectId id) { return !st.alive(st.find(id)); });
    std::erase_if(sp_indigenous_, [&st](objects::ObjectId id) { return !st.alive(st.find(id)); });  // tru2 t01
    std::erase_if(sp_homes_, [&st](const IndigenousHome& e) { return !st.alive(st.find(e.id)); });
    // mcx6 SET_PLANET_SPAWN (0x4b71c0 at the battle start, after the map's objects): the battle planet's spawn map, once.
    if (first_scan && !sp_spawners_.empty()) sp_planet_spawn(mode, rng);
    // 3. DEBRIS services (every frame, 0x62ad30): move, turn, expire.
    for (size_t i = 0; i < sp_debris_.size();) {
        DebrisPiece& d = sp_debris_[i];
        const objects::ObjectHandle h = st.find(d.id);
        if (!st.alive(h)) {
            if (d.particle != objects::kNoObjectId) {
                const objects::ObjectHandle ph = st.find(d.particle);
                if (st.alive(ph)) st.destroy(ph);
            }
            sp_debris_.erase(sp_debris_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        const uint32_t bi = st.behavior_index(h, kDebrisBehavior);
        if (bi == objects::kNone || !st.behavior_enabled(h, bi) || st.behavior_next_service(h, bi) > mf) {
            ++i;
            continue;
        }
        st.set_behavior_next_service(h, bi, mf + std::max<uint32_t>(1u, st.behavior_interval(h, bi)));
        const objects::TypeIndex t = st.type(h);
        const std::array<float, 4> mv = types.get(t, objects::tags::Debris_Movement_Vector);
        const std::array<float, 4> rv = types.get(t, objects::tags::Debris_Facing_Rotate_Vector);
        const Vec3 p0 = st.position(h);
        const Vec3 p{p0.x + mv[0], p0.y + mv[1], p0.z + mv[2]};
        st.set_position(h, p);
        d.roll += rv[0];
        d.pitch += rv[1];
        d.yaw += rv[2];
        st.set_facing(h, facing_of_angles(d.pitch, d.yaw));
        mode.trees().update(static_cast<tactical::EntityHandle>(d.id), mode.bounds_of(h));
        if (d.particle != objects::kNoObjectId) {
            const objects::ObjectHandle ph = st.find(d.particle);
            if (st.alive(ph)) st.set_position(ph, p);
        }
        if (types.get(t, objects::tags::Debris_Max_Lifetime_Seconds) > 0 && d.expiry != 0 && d.expiry <= mf) {
            const DebrisPiece gone = d;
            sp_debris_.erase(sp_debris_.begin() + static_cast<std::ptrdiff_t>(i));
            sp_debris_expire(mode, rng, gone);
            continue;
        }
        ++i;
    }
    // 4. SPAWN_INDIGENOUS_UNITS services on the store's interval-30 schedule, global list order.
    for (size_t i = 0; i < sp_spawners_.size(); ++i) {
        const objects::ObjectHandle h = st.find(sp_spawners_[i].id);
        if (!st.alive(h)) continue;
        const uint32_t bi = st.behavior_index(h, kSpawnerBehavior);
        if (bi == objects::kNone || !st.behavior_enabled(h, bi) || st.behavior_next_service(h, bi) > mf) continue;
        st.set_behavior_next_service(h, bi, mf + std::max<uint32_t>(1u, st.behavior_interval(h, bi)));
        sp_service(mode, rng, i);
    }
    // 5. tru2 t01: INDIGENOUS_UNIT services (ascending id).
    sp_wander(mode, rng);
}

void TacticalBattle::sp_wander(TacticalMode& mode, SimRng& rng) {
    // The base service (0x4c2f70) sets the next service from the interval cached at the previous one, then 0x4c3710
    // re-caches it: 30, or 1 while burning (vf +0x28 0x621840).
    objects::ObjectStore& st = mode.primary();
    const uint32_t mf = st.mode_frame();
    for (size_t i = 0; i < sp_indigenous_.size(); ++i) {  // by index: a burning unit's damage may end it
        const objects::ObjectHandle h = st.find(sp_indigenous_[i]);
        if (!st.alive(h)) continue;
        const uint32_t bi = st.behavior_index(h, kIndigenousBehavior);
        if (bi == objects::kNone || !st.behavior_enabled(h, bi) || st.behavior_next_service(h, bi) > mf) continue;
        st.set_behavior_next_service(h, bi, mf + std::max<uint32_t>(1u, st.behavior_interval(h, bi)));
        const objects::ObjectId id = st.id(h);
        const bool burning = combat_ && combat_->has_unit(id) && combat_->burning(id);
        st.set_behavior_interval(h, bi, burning ? 1u : 30u);
        sp_wander_one(mode, rng, h);
    }
}

bool TacticalBattle::sp_idle_clip(objects::ObjectId id) const {
    // 0x3956a0 -> 0x27b5e0: the current animation type is one of 0, 1, 0x47, 0x4d - 0x52. [port] Without the animation
    // service (headless, no clip for the object) the unit counts as idle.
#ifdef ALAMO_SIM_HAVE_ANIM
    const anim::AnimWorld* w = hooks_ ? hooks_->world() : nullptr;
    if (!w || !w->contains(id)) return true;
    const int32_t t = w->current_type(id);
    return t == 0 || t == 1 || t == 0x47 || (t >= 0x4d && t <= 0x52);
#else
    (void)id;
    return true;
#endif
}

void TacticalBattle::sp_wander_one(TacticalMode& mode, SimRng& rng, objects::ObjectHandle h) {
    // 0x621d80 (MCX4_T11_T12_RE_ANSWERS "Max_Distance_From_Spawner", TRU1_C_RE §1.4).
    objects::ObjectStore& st = mode.primary();
    const objects::GameObjectTypes& types = catalog_->types();
    const objects::ObjectId id = st.id(h);
    const Unit* u = unit_by_id(id);  // fetched again after the burn damage below
    // [port] only a movable battle unit can take the move; the busy test 0x39b000 (the unit's coordinator) is not modelled.
    if (!u || !u->alive || u->retreated || !u->movable || !movement_) return;
    const auto moving = [&] { return movement_->view(id).speed > 0.0f; };
    const bool has_combat = combat_ && combat_->has_unit(id);
    bool burning = has_combat && combat_->burning(id);
    if (burning) {  // Take_Damage(kind 7, Burning_Damage_Per_Second / FPS)
        tc::DamageRequest r;
        r.victim = id;
        r.amount = types.get(st.type(h), objects::tags::Burning_Damage_Per_Second) / static_cast<float>(combat_->config().fps);
        r.kind = tc::kKindFire;
        combat_->take_damage(r, frame_);
        u = unit_by_id(id);
        if (!st.alive(h) || !u || !u->alive || !combat_->has_unit(id) || combat_->health(id) <= 0.0f) return;  // the burn killed it
        burning = combat_->burning(id);
    }
    if (!burning) {
        if (moving()) return;
        if (has_combat && combat_->current_target(id) != tc::kNoObject) return;
        if (!sp_idle_clip(id)) return;
    } else if (moving()) {
        return;
    }
    // Can_Indigenous_Unit_Stop: one draw; past the first interval (frame > the cached interval) a draw above 0.1 stays.
    if (types.get(st.type(h), objects::tags::Can_Indigenous_Unit_Stop)) {
        const float r = rng.range_float(0.0f, 1.0f);
        const uint32_t bi = st.behavior_index(h, kIndigenousBehavior);
        if (st.mode_frame() > st.behavior_interval(h, bi) && kWanderStayChance < r) return;
    }
    float mn[2], mx[2];
    if (!map_.playable_box(mn, mx)) return;  // no map rectangle
    const float cx = (mn[0] + mx[0]) * 0.5f, cy = (mn[1] + mx[1]) * 0.5f;
    const float hx = (mx[0] - mn[0]) * 0.5f, hy = (mx[1] - mn[1]) * 0.5f;
    if (cx == 0.0f && cy == 0.0f && hx == 0.0f && hy == 0.0f) return;
    Vec3 to{};
    const auto home = std::lower_bound(sp_homes_.begin(), sp_homes_.end(), id,
                                       [](const IndigenousHome& e, objects::ObjectId k) { return e.id < k; });
    const bool leashed = home != sp_homes_.end() && home->id == id && home->leash > 0.0f &&
                         (home->home.x != -1.0f || home->home.y != -1.0f || home->home.z != -1.0f);
    if (leashed) {  // a point of the disc of radius D around home
        const float d = home->leash;
        const float dx = rng.range_float(-d, d);
        const float s = dmath::sqrt(std::max(0.0f, d * d - dx * dx));  // [port] a rounding below 0 reads as 0
        const float dy = rng.range_float(-s, s);
        to = Vec3{home->home.x + dx, home->home.y + dy, home->home.z + 0.0f};
    } else {  // a point of the map rectangle
        to.x = rng.range_float(cx - hx, hx + cx);
        to.y = rng.range_float(cy - hy, hy + cy);
        to.z = 0.0f;
    }
    // The step length, clamped by one more draw between the bounds when it falls outside them.
    const Vec3 p = st.position(h);
    const float vx = to.x - p.x, vy = to.y - p.y, vz = to.z - p.z;
    const float len = dmath::sqrt(vy * vy + vx * vx + vz * vz);
    const float smax = burning ? kBurningStepMax : kWanderStepMax, smin = burning ? kBurningStepMin : kWanderStepMin;
    if (smax < len || len < smin) {
        const float t = rng.range_float(smin, smax);
        if (!(len > 0.0f)) return;  // [port] no direction (retail divides by 0)
        const float inv = 1.0f / len;
        to = Vec3{p.x + t * vx * inv, p.y + t * inv * vy, p.z + t * inv * vz};
    }
    // 0x6218f0: the move, on the unit's team container's members when it has one (a pack moves together).
    std::vector<objects::ObjectId> ids;
    if (u->container != 0xFFFFFFFFu && u->container < containers_.size() && containers_[u->container].team) {
        for (const Unit& m : units_)
            if (m.container == u->container && m.alive && !m.retreated && m.movable) ids.push_back(m.id);
    } else {
        ids.push_back(id);
    }
    exec_move(mode, ids, to.x, to.y, rng, mv::kNoFacing, false);
}

void TacticalBattle::sp_debris_expire(TacticalMode& mode, SimRng& rng, const DebrisPiece& d) {
    // The attached particle is destroyed (0x391310 flag 1), then the piece takes 1e6 damage: its death (Die) makes the death
    // clone (its own type, +0x3a0 |= 0xc0) and the first Death_Explosions entry [port: pick rule untraced, as process_death].
    objects::ObjectStore& st = mode.primary();
    if (d.particle != objects::kNoObjectId) {
        const objects::ObjectHandle ph = st.find(d.particle);
        if (st.alive(ph)) st.destroy(ph);
    }
    const objects::ObjectHandle h = st.find(d.id);
    if (!st.alive(h)) return;  // the 1e6 damage always kills: the piece dies
    const objects::TypeIndex t = st.type(h);
    const Vec3 at = st.position(h), face = st.facing(h);
    const int32_t owner = st.owner(h);
    mode.destroy(h);
    if (const objects::ObjectHandle ch = spawn_death_clone(mode, static_cast<int32_t>(t), at, face, owner)) {
        ++stats_.death_clones;
        schedule_death_clone(mode, ch, static_cast<int32_t>(t), 1, rng);
    }
    const objects::TypeIndex ex = first_type_of_list(catalog_->types(), t, objects::tags::Death_Explosions);
    if (ex != objects::kNone) {
        const int32_t neutral = galaxy_ ? galaxy_->neutral_player() : -1;  // storyb t17: the clone's owner
        spawn_death_explosion(mode, static_cast<int32_t>(ex), at, neutral >= 0 ? neutral : owner);
    }
}

void TacticalBattle::sp_activate(const TacticalMode& mode, SimRng& rng, size_t index) {
    const objects::ObjectStore& st = mode.primary();
    sp_rebalance(mode, rng, st.type(st.find(sp_spawners_[index].id)), -1.0f, false);
}

uint32_t TacticalBattle::sp_rebalance(const TacticalMode& mode, SimRng& rng, objects::TypeIndex type, float chance, bool include_off) {
    // 0x5310b0: decide every spawner of this type at once. Undecided ones become off (pool U), active ones form pool A;
    // T = int(N x chance) (chance > 1 = percent); draws pick the ones that change. SET_SPAWN (include_off) also puts the
    // spawners already off into U (§1.13: N = on + off), so a higher chance switches them on again; the first-service
    // activation keeps TRU1's pools (a spawner switched off stays out).
    const objects::ObjectStore& st = mode.primary();
    const objects::GameObjectTypes& types = catalog_->types();
    std::vector<size_t> undecided, active;
    for (size_t i = 0; i < sp_spawners_.size(); ++i) {
        const objects::ObjectHandle h = st.find(sp_spawners_[i].id);
        if (!st.alive(h) || st.type(h) != type) continue;
        if (sp_spawners_[i].state == -1) {
            sp_spawners_[i].state = 0;
            undecided.push_back(i);
        } else if (sp_spawners_[i].state == 0 && include_off) {
            undecided.push_back(i);
        } else if (sp_spawners_[i].state == 1) {
            active.push_back(i);
        }
    }
    const int32_t n = static_cast<int32_t>(undecided.size() + active.size());
    if (n == 0) return 0;
    float c = std::max(0.0f, chance < 0.0f ? types.get(type, objects::tags::Spawn_Indigenous_Units_Chance) : chance);
    if (c > 1.0f) c = std::clamp(c * 0.01f, 0.0f, 1.0f);
    const auto target = static_cast<int32_t>(static_cast<float>(n) * c);
    const auto have = static_cast<int32_t>(active.size());
    std::vector<size_t>& pool = target > have ? undecided : active;
    const int8_t to = target > have ? int8_t{1} : int8_t{0};
    for (int32_t k = std::abs(target - have); k > 0 && !pool.empty(); --k) {
        const int32_t pick = rng.range_int(0, static_cast<int32_t>(pool.size()) - 1);
        sp_spawners_[pool[static_cast<size_t>(pick)]].state = to;
        pool.erase(pool.begin() + pick);
    }
    return static_cast<uint32_t>(n);
}

uint32_t TacticalBattle::reward_set_spawn(const TacticalMode& mode, SimRng& rng, std::string_view key, float chance) {
    // 0x531070: the key is the CRC of Reward_Param1 as written; a spawner type matches with the CRC of its upper-cased
    // name, so only the upper-case spelling matches [port: compared as text, the same test without the CRC].
    if (key.empty()) return 0;
    const objects::GameObjectTypes& types = catalog_->types();
    const objects::TypeIndex t = types.resolve_name(key);
    if (t == objects::kNone || t >= types.size() || ascii_upper(types.name(t)) != key) return 0;
    return sp_rebalance(mode, rng, t, chance, true);
}

void TacticalBattle::sp_planet_spawn(const TacticalMode& mode, SimRng& rng) {
    if (!galaxy_) return;
    const PlanetIndex planet = req_.info.planet;
    for (const galactic::StoryGates::PlanetSpawn& e : galaxy_->story_gates().planet_spawn)
        if (e.planet == planet) reward_set_spawn(mode, rng, e.key, e.chance);  // vector order = ascending CRC per planet
}

void TacticalBattle::sp_service(TacticalMode& mode, SimRng& rng, size_t index) {
    // 0x531760 (TRU1_C_RE §1.2).
    objects::ObjectStore& st = mode.primary();
    const objects::GameObjectTypes& types = catalog_->types();
    const objects::ObjectHandle sh = st.find(sp_spawners_[index].id);
    const objects::TypeIndex t = st.type(sh);
    if (sp_spawners_[index].state == -1) sp_activate(mode, rng, index);
    const uint32_t mf = st.mode_frame();
    if (sp_spawners_[index].state != 1 || mf < sp_spawners_[index].next) return;
    const int32_t quantity = types.get(t, objects::tags::Spawned_Indigenous_Units_Quantity);
    const int32_t spawner_owner = st.owner(sh);
    if (!types.get(t, objects::tags::Spawn_Indigenous_Units_In_Packs)) {
        const auto have = static_cast<int32_t>(sp_spawners_[index].spawned.size());
        if (quantity <= have) return;  // full: the next spawn frame stays
        for (int32_t k = quantity - have; k > 0; --k) {
            const objects::ObjectHandle u = sp_spawn_one(mode, rng, sh, nullptr);
            if (!st.alive(u)) continue;
            const int32_t owner = sp_corrupt_owner(st.type(u), spawner_owner);  // mcx4 t05
            if (owner != spawner_owner) st.set_owner(u, owner);
            sp_spawners_[index].spawned.push_back(st.id(u));
            // a map unit of a non-Is_Neutral owner is a battle unit (bt_map_load)
            if (!bt_owner_neutral(owner)) (void)bt_adopt_built(mode, u, owner, 1.0f, &rng, true);
        }
    } else {
        if (!sp_spawners_[index].spawned.empty()) return;  // a pack respawns only when it is all gone
        std::vector<objects::ObjectHandle> members;
        const objects::ObjectHandle first = sp_spawn_one(mode, rng, sh, nullptr);
        if (st.alive(first)) {
            // mcx4 t05 [port]: a pack follows its first member (one owner for the pack and its team container)
            const int32_t owner = sp_corrupt_owner(st.type(first), spawner_owner);
            const bool adopt = !bt_owner_neutral(owner);
            members.push_back(first);
            const Vec3 at = st.position(first);
            for (int32_t k = 1; k < quantity; ++k) {
                const objects::ObjectHandle u = sp_spawn_one(mode, rng, sh, &at);
                if (st.alive(u)) members.push_back(u);
            }
            // The pack's team container (0x2a5a60): Spawned_Indigenous_Pack_Type's team object over the members. [port] No
            // container without a pack type that resolves to a team (the original then uses the first member's type), or
            // for an Is_Neutral owner (its members are bare objects, as map units).
            uint32_t ci = 0xFFFFFFFFu;
            const objects::TypeIndex pack = types.resolve_name(ascii_trim(types.get(t, objects::tags::Spawned_Indigenous_Pack_Type)));
            uint32_t ot = 0xFFFFFFFFu, bt = 0xFFFFFFFFu, squad = 0;
            if (adopt && pack != objects::kNone && pack < types.size() &&
                bt_team(static_cast<uint32_t>(pack), static_cast<uint32_t>(st.type(first)), ot, bt, squad) &&
                std::find(types_used_.begin(), types_used_.end(), ot) != types_used_.end()) {
                objects::SpawnParams sp;
                sp.type = static_cast<objects::TypeIndex>(ot);
                sp.owner = owner;
                sp.position = at;
                sp.facing = st.facing(first);
                const objects::ObjectHandle ch = st.create(sp);
                if (st.alive(ch)) {
                    Container ct;
                    ct.id = st.id(ch);
                    ct.h = ch;
                    ct.team = true;
                    ct.type = bt;
                    ct.owner = owner;
                    ct.squad_type = static_cast<uint32_t>(pack);
                    ci = static_cast<uint32_t>(containers_.size());
                    containers_.push_back(ct);
                    ++stats_.containers;
                }
            }
            for (const objects::ObjectHandle u : members) {
                if (owner != spawner_owner) st.set_owner(u, owner);
                sp_spawners_[index].spawned.push_back(st.id(u));
                if (adopt) (void)bt_adopt_built(mode, u, owner, 1.0f, &rng, true, ci);
            }
        }
    }
    sp_spawners_[index].next = mf + static_cast<uint32_t>(frames_trunc(30.0f * std::max(0.0f, types.get(t, objects::tags::Spawned_Indigenous_Units_Delay_Seconds))));
}

int32_t TacticalBattle::sp_corrupt_owner(objects::TypeIndex unit_type, int32_t owner) const {
    // mcx4 t05 (MCX4_RE_ANSWERS_TACTICAL §6, SpawnIndigenousUnitsBehaviorClass 0x5314f0 [H]; [M] that it runs for each unit
    // the spawner makes): a unit whose type has Is_Indigenous_Unit_Corruptible (default true; FoC sets False on the
    // animals, bird / sketto spawners and the Imperial guards), spawned while the battle planet's corruption is
    // Corruption_Corrupt_Militia (4), changes owner to the first player (list order) whose faction has
    // Benefits_From_Corruption.
    const objects::GameObjectTypes& types = catalog_->types();
    if (unit_type == objects::kNone || unit_type >= types.size() || !types.get(unit_type, objects::tags::Is_Indigenous_Unit_Corruptible)) return owner;
    const auto planet = static_cast<size_t>(req_.info.planet);
    if (planet >= galaxy_->planets().size() || galaxy_->planets()[planet].corruption != 4) return owner;
    const auto& factions = galaxy_->rules().factions;
    for (size_t p = 0; p < galaxy_->players().size(); ++p) {
        const uint32_t f = galaxy_->players()[p].faction;
        if (f < factions.size() && factions[f].benefits_from_corruption) return static_cast<int32_t>(p);
    }
    return owner;
}

objects::ObjectHandle TacticalBattle::sp_spawn_one(TacticalMode& mode, SimRng& rng, objects::ObjectHandle spawner, const Vec3* centre) {
    // 0x531b60 (TRU1_C_RE §1.3): facing draws, type draw, creation, up to 10 placement tries.
    objects::ObjectStore& st = mode.primary();
    const objects::GameObjectTypes& types = catalog_->types();
    const objects::TypeIndex t = st.type(spawner);
    const float a = rng.range_float(0.0f, 1.0f);
    const float b = rng.range_float(0.0f, 1.0f);
    const std::vector<objects::TypeIndex> list = spawned_types(types, t);
    if (list.empty()) return {};
    const auto pick = static_cast<int32_t>(rng.range_float(0.0f, static_cast<float>(list.size()) - 1.0f));
    const objects::TypeIndex ut = pick >= 0 && static_cast<size_t>(pick) < list.size() ? list[static_cast<size_t>(pick)] : objects::kNone;
    if (ut == objects::kNone || ut >= types.size()) return {};
    objects::SpawnParams sp;
    sp.type = ut;
    sp.owner = st.owner(spawner);
    sp.position = Vec3{0.0f, 0.0f, 0.0f};
    sp.facing = Vec3{b, a, 0.0f};
    const objects::ObjectHandle h = mode.spawn(sp, 1.0f);
    if (!st.alive(h)) return {};
    const float radius = centre ? kPackRadius : types.get(t, objects::tags::Spawn_Indigenous_Units_Radius);
    const Vec3 c = centre ? *centre : st.position(spawner);
    const float hover = types.get(ut, objects::tags::Hover_Offset);
    for (int32_t k = 0; k < kPlacementTries; ++k) {
        const float dy = rng.range_float(-1.0f, 1.0f);
        const float dx = rng.range_float(-1.0f, 1.0f);
        Vec3 p{c.x + dx * radius, c.y + dy * radius, c.z};
        if (hover > 0.0f) p.z = p.z + hover;
        const int32_t r = sp_place(mode, h, p);
        if (r == 0) {
            // tru2 t01 (§1.3): the unit's INDIGENOUS_UNIT pack gets home := the spawner's position and leash := the spawner
            // type's Max_Distance_From_Spawner.
            if (st.has_behavior(h, kIndigenousBehavior)) {
                IndigenousHome e;
                e.id = st.id(h);
                e.home = st.position(spawner);
                e.leash = static_cast<float>(types.get(t, objects::tags::Max_Distance_From_Spawner));
                const auto at = std::lower_bound(sp_homes_.begin(), sp_homes_.end(), e.id,
                                                 [](const IndigenousHome& x, objects::ObjectId k) { return x.id < k; });
                sp_homes_.insert(at, e);
            }
            return h;
        }
        if (r == 2) break;
    }
    mode.destroy(h);  // no place (0x391310)
    return {};
}

int32_t TacticalBattle::sp_place(TacticalMode& mode, objects::ObjectHandle h, Vec3 p) {
    // 0x532140 [port]: clamp into the playable box; land: the nearest cell passable for the unit's movement class (16-cell
    // ring search) with z = terrain height; then the unit's footprint box against every other live object's box that
    // projectiles collide with: free -> teleport there (0), overlap -> 1, no passable cell -> 2.
    objects::ObjectStore& st = mode.primary();
    const objects::GameObjectTypes& types = catalog_->types();
    float mn[2], mx[2];
    if (map_.playable_box(mn, mx)) {
        p.x = std::clamp(p.x, mn[0], mx[0]);
        p.y = std::clamp(p.y, mn[1], mx[1]);
    }
    const objects::TypeIndex t = st.type(h);
    if (!space_ && movement_ && movement_->grid().width() > 0) {
        const mv::PassabilityGrid& grid = movement_->grid();
        const uint32_t mask = movement_->classes().mask(mv::unit_desc_from_type(types, t).movement_class);
        const int32_t cx = mv::world_to_cell(p.x), cy = mv::world_to_cell(p.y);
        bool found = grid.passable_cell(cx, cy, mask);
        for (int32_t r = 1; !found && r <= kSnapRings; ++r)
            for (int32_t oy = -r; oy <= r && !found; ++oy)
                for (int32_t ox = -r; ox <= r && !found; ++ox) {
                    if (std::abs(ox) != r && std::abs(oy) != r) continue;  // the ring only
                    if (grid.passable_cell(cx + ox, cy + oy, mask)) {
                        found = true;
                        const mv::V2 cc = mv::cell_center(cx + ox, cy + oy);
                        p.x = cc.x;
                        p.y = cc.y;
                    }
                }
        if (!found) return 2;
        if (map_.terrain.present) p.z = map_.terrain.sample_height(p.x, p.y) + std::max(0.0f, types.get(t, objects::tags::Hover_Offset));
    }
    float hx = 0.0f, hy = 0.0f;
    drop_half_extents(static_cast<uint32_t>(t), hx, hy);
    for (const objects::ObjectHandle o : st.all_objects()) {
        if (o == h || !st.alive(o)) continue;
        const objects::TypeIndex ot = st.type(o);
        if (ot >= types.size() || !types.get(ot, st.dying(o) ? objects::tags::Collidable_By_Projectile_Dead : objects::tags::Collidable_By_Projectile_Living))
            continue;
        const tactical::Aabb b = mode.bounds_of(o);
        if (p.x + hx > b.min.x && p.x - hx < b.max.x && p.y + hy > b.min.y && p.y - hy < b.max.y) return 1;
    }
    st.set_position(h, p);
    mode.trees().update(static_cast<tactical::EntityHandle>(st.id(h)), mode.bounds_of(h));
    return 0;
}

}  // namespace alamo::sim
