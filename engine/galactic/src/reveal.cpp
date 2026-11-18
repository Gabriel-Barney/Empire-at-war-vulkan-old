// docs/re/GALACTIC_REVEAL_CORRUPTION.md (GRC) on GalacticSim (declared in campaign.hpp): the planet revealed byte
// D +0x2c9 (campaign-start home reveal, the galactic pre-step 0x4b2600, story rewards, Is_Planet_Revealed 0x3fa420) and
// the corruption transition D +0x2e2..+0x2f8 (begin 0x4b72e0 / 0x4b7260, complete 0x4b7a50 via 0x3fc5b0, cancel
// 0x4b6c50, fraction 0x4b7e20, particle pairs 0x3ff230).
//
// Determinism: serial code; planets walked in row order (= the category-3 list order), orbit lists in arrival order;
// no RNG. The transition fraction is presentation-only: no sim code branches on it (completion is an integer frame
// test).
#include <algorithm>

#include "alamo/galactic/campaign.hpp"
#include "alamo/core/sim_crc.hpp"

namespace alamo::galactic {

namespace {

// 0x2c7c90 / 0x2c7cf0 / 0x2c7cc0: type -1 -> [0], 0..7 -> [1], anything else 1.0; a missing element reads 1.0 [L].
float level_value(const std::vector<float>& list, int8_t type) {
    size_t i = 0;
    if (type == kCorruptionNone) i = 0;
    else if (type >= 0 && type <= 7) i = 1;
    else return 1.0f;
    return i < list.size() ? list[i] : 1.0f;
}

// (int)((float)FPS x secs), truncation (cvttss2si).
uint32_t transition_frames(float seconds) {
    return static_cast<uint32_t>(static_cast<int32_t>(static_cast<float>(econ::kFramesPerSecond) * seconds));
}

}  // namespace

// ---- reveal --------------------------------------------------------------------------------------------------------

bool GalacticSim::is_planet_revealed(PlanetIndex planet) const {
    if (!valid_planet(planet) || !planets_[planet].is_planet || planets_[planet].hidden) return false;
    if (auto_reveal_) return true;
    return planets_[planet].revealed;
}

bool GalacticSim::planet_revealed_raw(PlanetIndex planet) const {
    return valid_planet(planet) && planets_[planet].revealed;
}

void GalacticSim::set_planet_revealed(PlanetIndex planet, bool revealed) {
    if (valid_planet(planet) && planets_[planet].is_planet) planets_[planet].revealed = revealed;
}

void GalacticSim::reveal_planets_of_type(uint32_t planet_type, bool on) {
    for (PlanetState& p : planets_)
        if (p.is_planet && p.type == planet_type) p.revealed = on;
}

void GalacticSim::reveal_all_planets() {
    for (PlanetState& p : planets_)
        if (p.is_planet) p.revealed = true;
}

bool GalacticSim::consume_reveal_sfx() {
    const bool r = reveal_sfx_;
    reveal_sfx_ = false;
    return r;
}

bool GalacticSim::reveal_one(PlanetIndex planet) {
    PlanetState& d = planets_[planet];
    if (!d.is_planet || d.revealed) return false;
    d.revealed = true;  // + highlight / visibility refresh (presentation; the masks are not modelled)
    return true;
}

void GalacticSim::reveal_prestep() {
    // GRC 1.3 [H]. Nothing without a local player.
    if (!valid_player(local_player_)) return;
    const int32_t local = local_player_;
    // threshold = (float)FPS x Planet_Reveal_Delay_Time (float, no FMA).
    const float threshold = static_cast<float>(econ::kFramesPerSecond) * rules_->constants.planet_reveal_delay_seconds;
    const double r = static_cast<double>(map_.max_fleet_move_distance);
    const double r2 = r * r;
    bool newly = false;
    for (PlanetIndex i = 0; i < planets_.size(); ++i) {
        PlanetState& d = planets_[i];
        if (!d.is_planet) continue;
        bool source = false;
        // 1. held by me or an ally (0x4ad9f0): owner != -1, relation 0, control > 0.
        if (valid_player(d.owner) && allied(local, d.owner) && d.control > 0.0f) {
            source = true;
        } else {
            // 2. orbit presence with delay (counter D +0x2cc).
            int32_t total = 0, hid = 0;
            for (uint32_t f : d.orbit) {
                const FleetState& fl = fleets_[f];
                if (!fl.alive || fl.owner != local) continue;
                for (ObjectHandle m : fl.contents) {
                    ++total;
                    hid += hidden(m) ? 1 : 0;
                }
            }
            if (total < 1 || total <= hid || paused() || d.orbit.empty()) d.reveal_delay = 0;
            else if (static_cast<float>(d.reveal_delay) <= threshold) ++d.reveal_delay;
            else source = true;
            // 3. corruption (any type, whoever made it) and the local faction benefits.
            if (d.corruption != kCorruptionNone && benefits(local)) source = true;
        }
        if (!source) continue;
        newly = reveal_one(i) || newly;  // 4.
        if (!auto_reveal_) continue;
        // 6. auto-reveal spread: planets not owned by the local player within MaximumFleetMovementDistance (squared,
        // float products summed in double, strict <), then the trade-route neighbours.
        const Vec3 c = d.position;
        for (PlanetIndex j = 0; j < planets_.size(); ++j) {
            const PlanetState& o = planets_[j];
            if (!o.is_planet || o.owner == local) continue;
            const float dx = o.position.x - c.x, dy = o.position.y - c.y, dz = o.position.z - c.z;
            const float xx = dx * dx, yy = dy * dy, zz = dz * dz;
            const double dist2 = static_cast<double>(xx) + static_cast<double>(yy) + static_cast<double>(zz);
            if (dist2 < r2) newly = reveal_one(j) || newly;
        }
        for (uint32_t rt : planets_[i].links) {
            const PlanetIndex nb = map_.routes.point_a[rt] == i ? map_.routes.point_b[rt] : map_.routes.point_a[rt];
            if (valid_planet(nb)) newly = reveal_one(nb) || newly;
        }
    }
    if (newly && frame_ > 0) reveal_sfx_ = true;  // SFXEvent_Planet_Revealed (frameArg > 0 [L])
}

// ---- corruption transitions ------------------------------------------------------------------------------------------

float GalacticSim::corruption_transition_fraction(PlanetIndex planet, uint32_t now) const {
    if (!valid_planet(planet)) return 1.0f;
    const PlanetState& p = planets_[planet];
    const uint32_t end = p.corruption_end, start = p.corruption_start;
    if (end == 0 || start == end) return 1.0f;
    if (start > end) return 0.0f;
    const uint32_t n = now != 0 ? now : static_cast<uint32_t>(frame_);
    // u32 zero-extended through a 64-bit signed convert: exact unsigned; now < start wraps to a large value -> 1.0.
    const float t = static_cast<float>(static_cast<int64_t>(n - start)) / static_cast<float>(static_cast<int64_t>(end - start));
    return std::min(1.0f, t);
}

int8_t GalacticSim::effective_corruption_type(PlanetIndex planet) const {
    if (!valid_planet(planet)) return kCorruptionNone;
    const PlanetState& p = planets_[planet];
    return (p.corruption_protected || p.corruption_removing) ? p.corruption_pending : p.corruption;
}

bool GalacticSim::corruption_transition_active(PlanetIndex planet) const {
    return valid_planet(planet) && (planets_[planet].corruption_protected || planets_[planet].corruption_removing);
}

GalacticSim::CorruptionParticlePairs GalacticSim::corruption_particle_pairs(PlanetIndex planet) const {
    CorruptionParticlePairs out;
    if (!valid_planet(planet)) return out;
    const PlanetState& p = planets_[planet];
    out.present = p.corruption_particles;
    out.brightness_start = p.corruption_pairs[0];
    out.brightness_end = p.corruption_pairs[1];
    out.scale_start = p.corruption_pairs[2];
    out.scale_end = p.corruption_pairs[3];
    out.energy_start = p.corruption_pairs[4];
    out.energy_end = p.corruption_pairs[5];
    return out;
}

bool GalacticSim::begin_gain_raw(PlanetIndex planet, float seconds, int8_t type, int32_t player) {
    PlanetState& p = planets_[planet];
    if (p.corruption_protected || p.corruption_removing || p.corruption != kCorruptionNone) return false;
    p.corruption_protected = true;
    p.corruption_pending = type;
    p.corruption_pending_player = player;
    p.corruption_start = static_cast<uint32_t>(frame_);
    p.corruption_end = p.corruption_start + transition_frames(seconds);
    return true;
}

bool GalacticSim::begin_removal_raw(PlanetIndex planet, float seconds) {
    PlanetState& p = planets_[planet];
    if (p.corruption_protected || p.corruption_removing || p.corruption == kCorruptionNone) return false;
    p.corruption_removing = true;
    p.corruption_pending = kCorruptionNone;
    p.corruption_pending_player = kNoPlayer;
    p.corruption_start = static_cast<uint32_t>(frame_);
    p.corruption_end = p.corruption_start + transition_frames(seconds == 0.0f ? 0.1f : seconds);
    return true;
}

void GalacticSim::cancel_corruption_transition(PlanetIndex planet) {
    if (!valid_planet(planet)) return;
    PlanetState& p = planets_[planet];
    p.corruption_protected = p.corruption_removing = false;
    p.corruption_start = p.corruption_end = 0;
    p.corruption_pending = kCorruptionNone;
    p.corruption_pending_player = kNoPlayer;
}

bool GalacticSim::begin_corruption_gain(PlanetIndex planet, int8_t type, int32_t corrupting_player, float seconds) {
    if (!valid_planet(planet) || !planets_[planet].is_planet) return false;
    if (!begin_gain_raw(planet, seconds, type, corrupting_player)) return false;
    rebuild_corruption_pairs(planet);
    GalacticEvent ev;  // wrapper 0x3f6810 raises the notification at the start of the transition
    ev.kind = GalacticEventKind::CorruptionStarted;
    ev.planet = planet;
    ev.player = corrupting_player;
    ev.value = 1;
    emit(ev);
    return true;
}

bool GalacticSim::begin_corruption_removal(PlanetIndex planet, float seconds) {
    if (!valid_planet(planet) || !planets_[planet].is_planet) return false;
    if (!begin_removal_raw(planet, seconds)) return false;
    rebuild_corruption_pairs(planet);
    GalacticEvent ev;  // wrapper 0x3f6770
    ev.kind = GalacticEventKind::CorruptionStarted;
    ev.planet = planet;
    ev.value = 0;
    emit(ev);
    return true;
}

void GalacticSim::complete_corruption(PlanetIndex planet) {
    // 0x3fc5b0: only with a flag set and frame >= end (unsigned).
    PlanetState& p = planets_[planet];
    if (!p.corruption_protected && !p.corruption_removing) return;
    if (static_cast<uint32_t>(frame_) < p.corruption_end) return;
    const int8_t old = p.corruption;
    const int32_t old_player = p.corrupting_player;
    // 1. complete 0x4b7a50.
    p.corruption = p.corruption_pending;
    p.corrupting_player = p.corruption == kCorruptionNone ? kNoPlayer : p.corruption_pending_player;
    p.corruption_start = p.corruption_end = 0;
    p.corruption_protected = p.corruption_removing = false;
    p.corruption_pending = kCorruptionNone;
    p.corruption_pending_player = kNoPlayer;
    p.corruption_changed = static_cast<uint32_t>(frame_);
    rebuild_corruption_pairs(planet);  // 2.
    if (old != p.corruption) {          // 3. corruption-changed notification, 5. 0x3f9690 (unlocks)
        corruption_unlocks(planet, old, old_player);
        // mcx4 t09 (MCX4_T09_RE_ANSWERS Q1): 0x3f9690's credit bonus goes to P = the first Benefits_From_Corruption
        // player (0x2948d0), not the planet owner; a removal (new none) pays nothing.
        pay_corruption_bonus(planet, p.corruption, first_corruption_player());
        GalacticEvent ev;
        ev.kind = GalacticEventKind::CorruptionChanged;
        ev.planet = planet;
        ev.player = p.corrupting_player;
        ev.value = p.corruption;
        ev.other_player = old;
        emit(ev);
    }
    // 4. the gained / removed SFX is presentation.
}

void GalacticSim::rebuild_corruption_pairs(PlanetIndex planet) {
    // 0x3ff230: the particle object exists while corrupted or gaining; the pairs are written only while it exists.
    PlanetState& p = planets_[planet];
    p.corruption_particles = p.corruption != kCorruptionNone || p.corruption_protected;
    if (!p.corruption_particles) return;
    const EconomyConstants& c = rules_->constants;
    const int8_t cur = p.corruption, eff = effective_corruption_type(planet);
    p.corruption_pairs = {level_value(c.particle_brightness_per_level, cur), level_value(c.particle_brightness_per_level, eff),
                          level_value(c.particle_scale_per_level, cur),      level_value(c.particle_scale_per_level, eff),
                          level_value(c.particle_energy_per_level, cur),     level_value(c.particle_energy_per_level, eff)};
}

// ---- mcx4 t09: the fleet visibility vote (MCX4_GAL2_RE_ANSWERS Q3, 0x433090) [H] ---------------------------------------
// Presentation only: no rule reads it. Steps 1-10 of Q3 with the local player L, owner O.
bool GalacticSim::fleet_visible_to_local(uint32_t fleet) const {
    if (local_player_ == kNoPlayer) return true;  // 1. the vote only means something for L
    if (fleet >= fleets_.size() || !fleets_[fleet].alive) return false;  // 2. [port] no fleet data / contained fleet
    const FleetState& f = fleets_[fleet];
    // 3. [port] the player +0x368 "sees everything" flag is 0 in a fresh game and is not modelled.
    if (f.owner == local_player_) return true;
    const auto type_of = [this](ObjectHandle h) -> const TypeInfo* {
        if (!store_->exists(h)) return nullptr;
        const auto t = static_cast<uint32_t>(store_->type(h));
        return t < rules_->types.size() ? &rules_->types[t] : nullptr;
    };
    const bool fleet_hidden = hidden(f.object);
    // 4. the current hop's destination (0x4329c0, path[index + 1]) [M] held by L.
    if (!fleet_hidden && !f.path.empty() && valid_planet(f.to) && planets_[f.to].owner == local_player_) return true;
    // 5. a Super content ([port] one level: fleet members, not a transport's cargo).
    for (const ObjectHandle u : f.contents)
        if (const TypeInfo* ti = type_of(u); ti && ti->super) return true;
    if (fleet_hidden) return false;  // 6. (7. the mode is galactic here)
    // The match condition of step 9. An AI owner: the fleet data's destination planet (+0x50; the path's end, the orbit
    // planet without a path) [M] is revealed. Otherwise: the path planet at the hop's start or the one before it.
    const auto revealed_at = [this](PlanetIndex p) { return valid_planet(p) && planets_[p].revealed; };
    bool match = false;
    if (valid_player(f.owner) && players_[static_cast<size_t>(f.owner)].ai) {
        match = revealed_at(f.path.empty() ? f.planet : f.path.back());
    } else if (!f.path.empty() && f.path_index >= 1 && f.path_index - 1 < f.path.size()) {
        const uint32_t i = f.path_index - 1;  // the port's index is the hop target's
        match = revealed_at(f.path[i]) || (i > 0 && revealed_at(f.path[i - 1]));
    }
    if (!match) return false;  // every candidate in range would only "go on"
    // 8 / 9. the candidates: L's revealers (types with Hyperspace_Fleet_Reveal_Range > 0), then, with a
    // Benefits_From_Corruption player, every Bribery planet whoever owns it. A candidate whose corruption type is 0
    // (Intimidation) is skipped (the "!= 0" test); a non-planet has none (-1). Range: Bribery_Fleet_Reveal_Range for a
    // type-5 planet, else the type's own; squared float distance, strict. [port] a unit stands at its fleet's or
    // planet's position.
    constexpr int8_t kIntimidation = 0, kBribery = 5;
    const auto in_range = [&f](const Vec3& c, float r) {
        const float dx = f.position.x - c.x, dy = f.position.y - c.y, dz = f.position.z - c.z;
        return dx * dx + dy * dy + dz * dz < r * r;
    };
    const float bribery_r = rules_->constants.bribery_fleet_reveal_range;
    const auto unit_reveals = [&](ObjectHandle u, const Vec3& at) {
        const TypeInfo* ti = type_of(u);
        return ti && store_->owner(u) == local_player_ && ti->fleet_reveal_range > 0.0f && in_range(at, ti->fleet_reveal_range);
    };
    for (const FleetState& g : fleets_)
        if (g.alive && g.owner == local_player_)
            for (const ObjectHandle u : g.contents)
                if (unit_reveals(u, g.position)) return true;
    for (const PlanetState& p : planets_) {
        if (!p.is_planet) continue;
        if (p.owner == local_player_ && p.type < rules_->types.size() && rules_->types[p.type].fleet_reveal_range > 0.0f &&
            p.corruption != kIntimidation)
            if (in_range(p.position, p.corruption == kBribery ? bribery_r : rules_->types[p.type].fleet_reveal_range)) return true;
        for (const ObjectHandle u : p.surface)
            if (unit_reveals(u, p.position)) return true;
        for (const ObjectHandle u : p.structures)
            if (unit_reveals(u, p.position)) return true;
    }
    if (first_corruption_player() != kNoPlayer)
        for (const PlanetState& p : planets_)
            if (p.is_planet && p.corruption == kBribery && in_range(p.position, bribery_r)) return true;
    return false;
}

// ---- CRC ---------------------------------------------------------------------------------------------------------------

bool GalacticSim::reveal_state_default() const {
    if (local_player_ != kNoPlayer || !auto_reveal_) return false;
    for (const PlanetState& p : planets_)
        if (p.revealed || p.corruption_removing || p.corruption_start != 0 || p.corruption_end != 0 ||
            p.corruption_changed != 0 || p.corruption_pending != kCorruptionNone || p.corruption_pending_player != kNoPlayer)
            return false;
    return true;
}

void GalacticSim::hash_reveal(StateHasher& h) const {
    h.u32(0x52564331u);  // "RVC1"
    h.i32(local_player_);
    h.boolean(auto_reveal_);
    for (const PlanetState& p : planets_) {
        if (!p.is_planet) continue;
        h.boolean(p.revealed);
        h.boolean(p.corruption_removing);
        h.u32(p.corruption_start);
        h.u32(p.corruption_end);
        h.u32(p.corruption_changed);
        h.value(p.corruption_pending);
        h.i32(p.corruption_pending_player);
    }
}

}  // namespace alamo::galactic
