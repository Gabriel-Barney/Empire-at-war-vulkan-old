// mcx6 t05: the host side of the story script's cinematic object calls (docs/re/MCX6_T05_CINEMATIC_BINDINGS.md). The Lua
// bindings only record them (lua::PresentationOp); the game app feeds the events here, between ticks, and the drawn
// state below is what the battle view, the world pick and the object sounds read (GameSession::script_hidden).
//
//   Cinematic_Hyperspace_In(delay)  (§1, L1_S4_REMAINING §3.4, 0x574e50 -> 0x5c9a80, state 0x2c): the arrival timer starts
//                                   at 25 - delay and counts one per frame; the model is hidden until 35 and the owner
//                                   faction's SFXEvent_Arrive_From_Hyperspace plays at 45 (AUDIO.md §9.6), so the ship
//                                   shows delay + 10 frames and is heard delay + 20 frames after the call.
//   Do_End_Cinematic_Cleanup()      (§2, 0x541e20 -> 0x38ed10): not here. The sim binding suspends the objects (saved
//                                   ScriptObjectState::end_cinematic_suspended) and GameSession::script_hidden hides them
//                                   from that flag (CampaignBattles::end_cinematic_hidden), so a load keeps the hide.
//   Show_Emitter(name, on)          (§4, 0x581120): the ModelEmitterType name of one of the model's particle prefix groups
//                                   (model_emitter_type); emitter_overrides turns the binding's state into per-object
//                                   shown / hidden group bits for the renderer (render::SimInstanceBuilder emitter source).
//
// Presentation only (rule 3): nothing here is read by the sim, hashed or saved. [port] The game sees a call when it drains
// the presentation log after the tick that made it, so `now` is the battle frame at that drain (at most the ticks of one
// render frame later than the call); a loaded save starts with no arrival state (the emitters come back from the saved
// lua::PresentationState).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/objects/object_store.hpp"

namespace alamo::game {

// 0x5c9a80 state 0x2c: timer := 25 - delay; shown at 35, arrival sound at 45 (frames after the call: delay + these).
inline constexpr uint32_t kCinematicArrivalShowFrames = 10;
inline constexpr uint32_t kCinematicArrivalSfxFrames = 20;

class ScriptCinematics {
public:
    // Cinematic_Hyperspace_In on `object` seen at battle frame `now`. A negative delay counts as 0 [port: retail's timer
    // would start above 25 and show the model at once, which a delay of 0 does after 10 frames].
    void hyperspace_in(objects::ObjectId object, int32_t delay_frames, uint32_t now);
    // Drawn hidden at battle frame `now`: an arrival before its show frame.
    bool hidden(objects::ObjectId id, uint32_t now) const;
    // The arrivals whose sound is due at `now` (sound frame <= now), each once, in call order.
    void take_arrival_sfx(uint32_t now, std::vector<objects::ObjectId>& out);
    void reset();  // a new battle
    size_t arrivals() const { return arrivals_.size(); }

private:
    struct Arrival {
        objects::ObjectId id = objects::kNoObjectId;
        uint32_t show = 0, sfx = 0;
        bool sfx_done = false;
    };
    std::vector<Arrival> arrivals_;  // call order; a second call on the same object replaces its entry
};

// Show_Emitter's ModelEmitterType names (the enum table 0x2641c0): Engines 0, Turbo_Engines 1, Ion_Stun 2,
// Sprint_Ability_Effect 4, Power_To_Weapons_Effect 5, Missile_Shield 7 (the particle prefix group index of
// BKX1_RENDER_RE §1.1). -1 = not a name of the table. [L] matched without regard to case.
int32_t model_emitter_type(std::string_view name);

// Per object: the particle prefix groups (bit = group index) a script showed / hid.
struct EmitterGroups {
    uint8_t show = 0, hide = 0;
    friend bool operator==(const EmitterGroups&, const EmitterGroups&) = default;
};
// lua::PresentationState::emitters ("<object id>|<NAME>" -> shown) as per-object group bits; unknown names are left out.
std::map<objects::ObjectId, EmitterGroups> emitter_overrides(const std::map<std::string, bool>& emitters);

}  // namespace alamo::game
