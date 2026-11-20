#pragma once
// ref3 t06: the test-only seam of TacticalBattle. The battle's public interface hands out read-only views of its inner
// systems; tests that drive them (activate an ability, reveal the fog, crush a unit) or call the test hooks go through
// this friend instead. Tests only: engine code never includes this header.
#include "alamo/sim/tactical_battle.hpp"

namespace alamo::sim {

struct TacticalBattleTestAccess {
    static tactical::abilities::AbilitySystem& abilities(TacticalBattle& b) { return *b.abilities_; }
    // The battle's fog, mutable (null when fog is off or the battle is not set up).
    static tactical::fog::FogOfWar* fog(TacticalBattle& b) { return b.fog_rw(); }
#ifdef ALAMO_SIM_HAVE_AI_TACTICAL
    static ai::tactical::DamageTrackingStore& damage_tracking(TacticalBattle& b) { return b.damage_tracking_; }
#endif
    // s78k t05: CombatHooks::attack_idle on `id` and a clip play on the battle's animation world (TacticalBattle::test_*).
    static bool attack_idle(TacticalBattle& b, objects::ObjectId id, bool arg, SimRng& rng) { return b.test_attack_idle(id, arg, rng); }
    static bool play_clip(TacticalBattle& b, objects::ObjectId id, int32_t clip_type) { return b.test_play_clip(id, clip_type); }
    // r3g t05: `crusher` crushes `victim` now, as a movement crush event does.
    static bool crush(TacticalBattle& b, objects::ObjectId crusher, objects::ObjectId victim) { return b.test_crush(crusher, victim); }
    // s78n t09: the joint box half-extents of movement-backed land squad container `id`.
    static bool squad_joint_box(const TacticalBattle& b, const objects::ObjectStore& st, objects::ObjectId id, float& hx, float& hy) {
        return b.squad_joint_box(st, id, hx, hy);
    }
    // tst2 t03 (bkt1 coverage): the land attack staging 0x40e260 (attack_stage_goal), a land attack plan
    // (land_attack_order; returns the AttackPlan as an int) and the map extents the staging's search keeps inside.
    static Vec3 stage_goal(TacticalBattle& b, const TacticalMode& mode, std::span<const objects::ObjectId> ids, objects::ObjectId target,
                           Vec3 tp, size_t movers) {
        return b.attack_stage_goal(mode, ids, target, tp, movers);
    }
    static int land_attack_order(TacticalBattle& b, TacticalMode& mode, std::span<const objects::ObjectId> ids, objects::ObjectId target,
                                 Vec3 tp, uint8_t flags, SimRng& rng, bool guard) {
        return static_cast<int>(b.land_attack_order(mode, ids, target, tp, flags, false, rng, guard));
    }
    static tactical::Aabb map_extents(const TacticalBattle& b, const TacticalMode& mode) { return b.map_extents(mode); }    // s78n t10: the 8 (x, y) points of object `h`'s fog multisample footprint.
    static void fog_footprint(const TacticalBattle& b, const objects::ObjectStore& st, objects::ObjectHandle h, float* xy16) {
        b.fog_footprint_samples(st, h, xy16);
    }
};

}  // namespace alamo::sim
