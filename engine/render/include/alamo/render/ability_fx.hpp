// alamo_render_sim - vis1 t06 ability target / status particles (docs/re/ABILITY_PARTICLES.md). Presentation only.
//
// Retail creates these as particle GameObjects attached to the target; the port draws them as render particles
// (ABILITY_VISUALS §6.4 decision, ABILITY_PARTICLES §10): SnapshotEffect entries the particle attachment syncs.
//   * state effects (Earthquake / Telekinesis dust while the special is applied on its target, Infection_Particle while
//     the victim's infection is active): listed every capture under attached_effect_key(target, percent);
//   * one-shots (Target_Particle_Effect of Force_Healing / Force_Lightning / Hack / Repair, Stun_Ability's Stunned_Effect):
//     each occurrence gets its own system (SnapshotEffect::fx_seq), listed until Particle_Lifetime_Frames have passed
//     (a type without a lifetime: while its target exists).
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/render/sim_snapshot.hpp"

namespace alamo::sim {
class TacticalBattle;
}
namespace alamo::objects {
class GameObjectTypes;
}

namespace alamo::render {

// SnapshotEffect::percent of the state effects [port: free keys below kEffectLobChargeup 239].
inline constexpr uint8_t kEffectTargetState = 238;  // Earthquake / Telekinesis Target_Particle_Effect
inline constexpr uint8_t kEffectInfection = 237;    // Infection_Particle

// One capture's ability visuals as plain data (apply_battle_ability_fx fills it from the battle).
struct AbilityFxInput {
    struct State {
        objects::ObjectId target = objects::kNoObjectId;
        uint8_t percent = kEffectTargetState;
        std::string particle;  // particle / GameObjectType name
        std::string bones;     // candidate bones, comma separated (first the model has, else the root)
    };
    struct OneShot {
        uint32_t frame = 0;
        objects::ObjectId target = objects::kNoObjectId;
        std::string particle;
        std::string bones;
        uint32_t lifetime = 0;  // Particle_Lifetime_Frames; 0 = while the target exists (one such per target and asset)
    };
    uint32_t frame = 0;  // battle frame of the capture
    std::vector<State> states;
    std::vector<OneShot> shots;  // new occurrences since the last capture
};

class AbilityFxTracker {
public:
    void reset();
    // Starts `in.shots`, drops one-shots whose lifetime ended or whose target left the snapshot, and appends the state
    // effects and the live one-shots to `snap` (asset: the particle type's model through `table`, else the name).
    void apply(const AbilityFxInput& in, SimSnapshot& snap, const SnapshotTypeTable* table);
    size_t live() const { return live_.size(); }

    // apply_battle_ability_fx: the serial of the last AbilitySystem::visual_events entry taken.
    uint32_t visual_serial = 0;

private:
    struct Live {
        uint32_t seq = 0;
        objects::ObjectId target = objects::kNoObjectId;
        std::string asset, bones;
        uint32_t start = 0, end = 0;  // end = UINT32_MAX: while the target exists
    };
    std::vector<Live> live_;
    uint32_t next_seq_ = 1;
};

#ifdef ALAMO_SIM_HAVE_BATTLES
// Fills the input from the battle (const reads between ticks): applied Earthquake / Telekinesis specials, active
// infections (Infection_Particle of the first special that names one [port]: the victim keeps no infector type),
// the abilities' visual events after tracker.visual_serial (a particle name that is no object type: none, as retail's
// unknown type); then tracker.apply. `status_bones` = GameConstants
// Object_Visual_Status_Particle_Attach_Bone_Names (stun / infection placement); `types` gives Particle_Lifetime_Frames.
void apply_battle_ability_fx(const sim::TacticalBattle& battle, AbilityFxTracker& tracker, std::string_view status_bones,
                             const objects::GameObjectTypes* types, SimSnapshot& snap, const SnapshotTypeTable* table);
#endif

}  // namespace alamo::render
