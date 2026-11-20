// tactical_battle: ref6 t02 - the battle's one EngagementQueries (TacticalBattle::Engagement), the answers movement and
// combat ask about each other. Each answer is the one the battle gave through its per-rule hook before (same values,
// same call order): space has no fire test, no Targeting_Min_Attack_Distance query and no shield-sides test.
#include "tactical_battle_internal.hpp"

namespace alamo::sim {
using namespace bo3split;

// OI tactical: crush hostility = the combat relation (player +0x370 table, MOVEMENT.md 5.5).
bool TacticalBattle::Engagement::hostile(int32_t a, int32_t b) const { return b_.combat_ && b_.combat_->is_enemy(a, b); }

// s78n t26 review round 2 (MOVEMENT §4.3 correction [H]): the formation layout's rank key, Get attack range 0x3749d0 (the
// value land_attack_order reads); a squad container through its first live member. bkm1 t05 (b0037): space ranks by it too.
float TacticalBattle::Engagement::attack_range(int32_t id) const {
    if (!b_.combat_) return 0.0f;
    const objects::ObjectId unit = b_.attack_lead(id);
    return unit != objects::kNoObjectId && b_.combat_->has_unit(unit) ? b_.combat_->attack_range(unit) : 0.0f;
}

// bkm1 t04 (MOVEMENT §4.2 step 2 item 7 test 9): Targeting_Min_Attack_Distance for the recruitment's engagement test (land).
float TacticalBattle::Engagement::min_attack_distance(int32_t id) const {
    if (b_.space_ || !b_.combat_) return 0.0f;
    const objects::ObjectId unit = b_.attack_lead(id);
    return unit != objects::kNoObjectId && b_.combat_->has_unit(unit) ? b_.combat_->min_attack_distance(unit) : 0.0f;
}

bool TacticalBattle::Engagement::has_fire_test() const { return !b_.space_; }

// s78n t25.3 (MOVEMENT §4.3.2 items 6 / 7, 0x40edd0 / 0x410e90; TACTICAL_COMBAT §2.8 B 4): can the mover hit the target
// from p: slots 43 / 46 and the line of fire from p at the ground height (CombatSystem::can_hit_from; a squad container
// through its first live member). Yes or NoTarget (nothing tested) = it can fire. s78n t25.4: los_only (a guard block's
// trim, item 7) answers the line of fire alone (CombatSystem::line_of_fire_from).
bool TacticalBattle::Engagement::can_fire_from(int32_t mover, float px, float py, int32_t target, bool los_only, float gx, float gy,
                                               float gz) const {
    const objects::ObjectId unit = b_.attack_lead(mover);
    if (unit == objects::kNoObjectId || !b_.combat_->has_unit(unit)) return true;
    tactical::Vec3 from = b_.combat_->can_hit_origin(unit);
    from.x = px;
    from.y = py;
    // bkt1 t06 (b0079, MOVEMENT §4.3.2 item 7 / 0x40f7c0): from p at the terrain height, or the water height for a
    // Water-capable class (the mover's movement mask has the Water bit) where the water lies above; to G at its height (the
    // movement's goal point); CombatSystem raises both by their Ranged_Target_Z_Adjust
    const terrain::MapData& map = b_.map_;
    from.z = map.terrain.sample_height(px, py);
    if ((b_.movement_->mask_of(mover) & mv::kPassWater) != 0u && map.terrain.header.water_type != terrain::WaterType::None)
        from.z = std::max(from.z, map.terrain.header.water_height);
    const tactical::Vec3 to{gx, gy, gz};
    // cmb2 t06 (BKT1_ATTACK_RE §3.2 / §3.3 item 4 [H]): an object that is not a combat unit (a story marker) is tested by
    // geometry against G
    const tc::CanHit c = !b_.combat_->has_unit(target) ? b_.combat_->can_hit_point(unit, from, to, 0.0f)
                         : los_only                    ? b_.combat_->line_of_fire_from(unit, from, target, &to)
                                                       : b_.combat_->can_hit_from(unit, from, target, &to);
    return c == tc::CanHit::Yes || c == tc::CanHit::NoTarget;
}

// S8 t24 (MOVEMENT §3.2 "Turret recentre around deploy"): a Deploys unit's TURRET aims at its live current combat target
// (0x632050 [inferred: whenever the unit has one]); the raw position, no z adjust [inferred].
bool TacticalBattle::Engagement::turret_aim(int32_t id, tactical::Vec3& at) const {
    if (!b_.combat_ || !b_.combat_->has_unit(id)) return false;
    const tc::ObjectId t = b_.combat_->current_target(id);
    if (t == tc::kNoObject || !b_.combat_->has_unit(t) || !b_.combat_->alive(t)) return false;
    at = b_.combat_->position(t);
    return true;
}

// cu8l t03 (CU8K_T03_T08_T09_RE §1.10 rule 1): the movement lock of the start fleet's hyperspace arrival (arrivals_).
bool TacticalBattle::Engagement::movement_locked(int32_t key) const { return b_.arrival_locked(key); }

// S8 t17 (FIGHTER_FLYING §4.10 A): the scan cube R + Rr of fighters with a formation (kUnitScanQuery); cmb1 t03 (BKR1 Q8
// [H]): on land the units and squad containers with a land formation record (land_scan_range). Read-only.
uint8_t TacticalBattle::Engagement::scan_range(int32_t unit, int32_t candidate, float range, float& rr) const {
    rr = 0.0f;
    if (!mode) return 0;
    return b_.space_ ? b_.fighter_scan_range(mode->primary(), unit, candidate, range, rr)
                     : b_.land_scan_range(mode->primary(), unit, candidate, range, rr);
}

bool TacticalBattle::Engagement::has_shield_sides() const { return !b_.space_; }

// s78n t20 (FIGHTER_FLYING §4.11 E, 0x40edd0): can_hit's land line-of-fire term 0x3b5ad0 (L1_S4_REMAINING §1.2).
bool TacticalBattle::Engagement::opposite_shield_sides(tactical::Vec3 from, tactical::Vec3 to, bool shield_damage) const {
    return b_.opposite_sides_of_shield(sv(from), sv(to), -1, true, shield_damage);
}
}  // namespace alamo::sim
