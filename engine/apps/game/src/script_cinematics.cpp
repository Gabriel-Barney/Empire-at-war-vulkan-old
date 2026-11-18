// mcx6 t05: the story script's cinematic object calls, host side (alamo/game/script_cinematics.hpp).
#include "alamo/game/script_cinematics.hpp"

#include <algorithm>
#include <charconv>

#include "alamo/core/ascii.hpp"

namespace alamo::game {

void ScriptCinematics::hyperspace_in(objects::ObjectId object, int32_t delay_frames, uint32_t now) {
    const uint32_t delay = delay_frames > 0 ? static_cast<uint32_t>(delay_frames) : 0u;
    Arrival a;
    a.id = object;
    a.show = now + delay + kCinematicArrivalShowFrames;
    a.sfx = now + delay + kCinematicArrivalSfxFrames;
    // 0x5c9a80 restarts the object's arrival: a second call replaces the first.
    arrivals_.erase(std::remove_if(arrivals_.begin(), arrivals_.end(), [&](const Arrival& x) { return x.id == object; }),
                    arrivals_.end());
    arrivals_.push_back(a);
}

void ScriptCinematics::end_cinematic_cleanup(std::span<const objects::ObjectId> ids) {
    cleaned_.insert(ids.begin(), ids.end());
}

bool ScriptCinematics::hidden(objects::ObjectId id, uint32_t now) const {
    if (cleaned_.count(id) != 0) return true;
    for (const Arrival& a : arrivals_)
        if (a.id == id) return now < a.show;
    return false;
}

void ScriptCinematics::take_arrival_sfx(uint32_t now, std::vector<objects::ObjectId>& out) {
    for (Arrival& a : arrivals_)
        if (!a.sfx_done && a.sfx <= now) {
            a.sfx_done = true;
            out.push_back(a.id);
        }
    // An arrival is over once it was heard and shown; its entry is no longer needed.
    arrivals_.erase(std::remove_if(arrivals_.begin(), arrivals_.end(), [&](const Arrival& x) { return x.sfx_done && now >= x.show; }),
                    arrivals_.end());
}

void ScriptCinematics::reset() {
    arrivals_.clear();
    cleaned_.clear();
}

std::vector<objects::ObjectId> end_cinematic_cleanup_set(std::span<const CleanupCandidate> candidates, int32_t neutral_player,
                                                         const std::function<bool(objects::ObjectId)>& in_end_cinematic) {
    std::vector<objects::ObjectId> out;
    for (const CleanupCandidate& c : candidates) {
        if (!c.locomotor || c.owner == neutral_player) continue;
        if (in_end_cinematic && in_end_cinematic(c.id)) continue;  // obj +0x3c9 set: 0x38ed10 leaves it alone
        out.push_back(c.id);
    }
    return out;
}

int32_t model_emitter_type(std::string_view name) {
    struct Entry {
        std::string_view name;
        int32_t group;
    };
    static constexpr Entry kTable[] = {{"Engines", 0},          {"Turbo_Engines", 1},           {"Ion_Stun", 2},
                                       {"Sprint_Ability_Effect", 4}, {"Power_To_Weapons_Effect", 5}, {"Missile_Shield", 7}};
    for (const Entry& e : kTable)
        if (ascii_iequals(e.name, name)) return e.group;
    return -1;
}

std::map<objects::ObjectId, EmitterGroups> emitter_overrides(const std::map<std::string, bool>& emitters) {
    std::map<objects::ObjectId, EmitterGroups> out;
    for (const auto& [key, shown] : emitters) {
        const size_t bar = key.find('|');
        if (bar == std::string::npos) continue;
        objects::ObjectId id = objects::kNoObjectId;
        const auto r = std::from_chars(key.data(), key.data() + bar, id);
        if (r.ec != std::errc() || r.ptr != key.data() + bar) continue;
        const int32_t g = model_emitter_type(std::string_view(key).substr(bar + 1));
        if (g < 0) continue;
        const uint8_t bit = static_cast<uint8_t>(1u << static_cast<uint32_t>(g));
        EmitterGroups& e = out[id];
        e.show = static_cast<uint8_t>(shown ? (e.show | bit) : (e.show & ~bit));
        e.hide = static_cast<uint8_t>(shown ? (e.hide & ~bit) : (e.hide | bit));
    }
    return out;
}

}  // namespace alamo::game
