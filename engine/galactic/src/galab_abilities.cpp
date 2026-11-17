// galab (owner: galactic agent): the galactic abilities of roadmap group GALAB - Remove_Corruption_Ability,
// Planet_Destruction_Ability (Death Star), Siphon_Credits_Ability, the planet owner bonuses Galaxy_Wide_Upgrade_Ability and
// Reduce_Technology_Price_Ability, and Political_Transition_Bonus_Ability. Behaviour: docs/re/GALAB_ABILITIES_RE.md (= GA).
// Tables: special_data.hpp (SpecialRules::galab); the hero trigger walk is GalacticSim::ground_activated_trigger.
#include <algorithm>

#include "alamo/core/sim_crc.hpp"
#include "alamo/galactic/campaign.hpp"

namespace alamo::galactic {

// ---- GA §1 Remove_Corruption_Ability ------------------------------------------------------------------------------------

bool GalacticSim::remove_corruption_accepts(int32_t player, PlanetIndex planet) const {
    // vf5 0x5b1770 [H]: the base own-planet test 0x48e0c0 (the planet's owner is the hero's player), then the planet data:
    // no transition running and a current corruption type.
    if (!valid_planet(planet) || !valid_player(player)) return false;
    const PlanetState& p = planets_[planet];
    if (!p.is_planet || p.owner != player) return false;
    return !p.corruption_protected && !p.corruption_removing && p.corruption != kCorruptionNone;
}

float GalacticSim::corruption_removal_cost(const SpecialAbilityDef& a, PlanetIndex planet) const {
    // 0x5b1730 [H]: planet type Planet_Corruption_Removal_Base_Cost (+0x804) x Corruption_Removal_Cost_Multiplier, never
    // negative.
    if (!valid_planet(planet)) return 0.0f;
    const float c = special().type(planets_[planet].type).corruption_removal_cost * a.removal_cost_multiplier;
    return c >= 0.0f ? c : 0.0f;
}

bool GalacticSim::apply_remove_corruption(const SpecialAbilityDef& a, ObjectHandle hero, int32_t player, PlanetIndex planet) {
    // vf30 0x5b13c0 [H], in order: the accept again; cost; refused when cost > 0 and the player's credits are below it,
    // or when the removal wrapper 0x3f6770 refuses (the local player's error SFX); then the charge (Add_Credits(-cost),
    // notify off) and story LAND_ON / DEPLOY (special_apply's SpecialApplied). The hero stays (Causes_Despawn No in FoC).
    if (!remove_corruption_accepts(player, planet)) return false;
    const float cost = corruption_removal_cost(a, planet);
    const bool afford = !(cost > 0.0f && players_[static_cast<size_t>(player)].account.credits < cost);
    if (!afford || !begin_corruption_removal(planet, a.removal_seconds)) {
        emit_special(GalacticEventKind::SpecialError, player, planet, hero, kNoIndex, 0, -1);
        return false;
    }
    if (cost > 0.0f) add_credits(player, -cost, false);  // A2K_T10_CREDIT_NOTIFY_RE: 0x5b13c0 notify off
    return true;
}

// ---- GA §2 Planet_Destruction_Ability (Death Star) ----------------------------------------------------------------------

bool GalacticSim::planet_destruction_accepts(int32_t player, PlanetIndex planet) const {
    // vf5 0x7079c0 [H]: a planet that is not destroyed, and whose owner (if any) is neither the shooter's player nor an
    // ally of it (relation 0).
    if (!valid_planet(planet) || !valid_player(player)) return false;
    const PlanetState& p = planets_[planet];
    if (!p.is_planet || p.destroyed) return false;
    return !valid_player(p.owner) || !allied(player, p.owner);
}

bool GalacticSim::apply_planet_destruction(ObjectHandle shooter, int32_t player, PlanetIndex planet) {
    // vf30 0x707670 [H]: the accept again; the contested-space refusal (shooter +0x3a2 & 0x40, set by default; the Lua
    // Set_Check_Contested_Space flag lives in the script layer and is not bridged here [port]: always checked); the
    // leader test and claim 7; the destroy (0x3f76c0 value 1). No story LAND_ON for this class (special_apply).
    if (!planet_destruction_accepts(player, planet)) return false;
    if (contested_space(planet, player)) {
        emit_special(GalacticEventKind::SpecialError, player, planet, shooter, kNoIndex, 0, -1);
        return false;
    }
    return super_weapon_destroy_planet(planet, player, false);
}

// ---- GA §3 Siphon_Credits_Ability ---------------------------------------------------------------------------------------

bool GalacticSim::siphon_accepts(int32_t player, PlanetIndex planet) const {
    // vf5 0x70e8c0 [H]: 0x48e070 (a planet whose owner is not the hero's player or an ally) and a playable owner faction.
    if (!valid_planet(planet) || !valid_player(player)) return false;
    const PlanetState& p = planets_[planet];
    if (!p.is_planet || !valid_player(p.owner) || allied(player, p.owner)) return false;
    return faction_of(p.owner).is_playable;
}

bool GalacticSim::siphon_blocked_by_presence(PlanetIndex planet) const {
    // 0x4b8410 [H for the field, M for the lists]: an object at the planet whose type has Prevents_Minor_Credit_Siphons
    // (T +0x63). The planet's two object lists are taken as the surface units and the orbiting fleets' contents [M].
    const PlanetState& p = planets_[planet];
    const auto blocks = [&](ObjectHandle h) { return store_->alive(h) && special_type(h).prevents_minor_siphons; };
    for (ObjectHandle u : p.surface)
        if (blocks(u)) return true;
    for (uint32_t f : p.orbit)
        if (fleets_[f].alive)
            for (ObjectHandle m : fleets_[f].contents)
                if (blocks(m)) return true;
    return false;
}

bool GalacticSim::apply_siphon(const SpecialAbilityDef& a, ObjectHandle hero, int32_t player, PlanetIndex planet) {
    // vf30 0x70e440 [H], in order:
    //  1. the accept again;
    //  2. the faction pairing 0x48d900 (Rebel / Empire / Underworld against another of the three);
    //  3. a generic hero is refused where a Prevents_Minor_Credit_Siphons object is present;
    //  4. a planet that is already siphoned (D +0x200 != -1) is refused;
    //  (2-4 refused: the local player's error SFX)
    //  5. Ground_Activated: a Planet_Vigilance planet catches a generic hero - respawn at the default time (0x48eb10 with
    //     0 s) and killed (0x391310); nothing else, no error SFX;
    //  6. fraction = one synced draw Get_Float(Min, Max) (0x532480, bounds swapped when needed);
    //  7. siphon record (0x3f6520): player, fraction, end = now + cycle frames x Siphon_Duration_In_Cycles, the hero's type
    //     and object (read by Neutralize_Hero, GA §7). [port] The original also needs the hero inside a container
    //     (hero +0x2b0 with a behaviour-5 slot); the port's heroes at a planet always qualify.
    if (!siphon_accepts(player, planet)) return false;
    PlanetState& p = planets_[planet];
    const bool generic = special_type(hero).generic_hero;
    if (!faction_pairing(player, p.owner) || (generic && siphon_blocked_by_presence(planet)) || p.siphon_player != kNoPlayer) {
        emit_special(GalacticEventKind::SpecialError, player, planet, hero, kNoIndex, 0, -1);
        return false;
    }
    if (a.style == ActivationStyle::GroundActivated && special().type(p.type).planet_vigilance && generic) {
        emit_special(GalacticEventKind::SiphonVigilance, player, planet, hero, kNoIndex, 0, p.owner);
        schedule_hero_respawn(hero, planet, 0.0f);  // 0x48eb10(hero, planet, 0): the default respawn time
        destroy_object(hero);                       // 0x391310(hero, 1)
        return false;
    }
    const float fraction = rng_->inline_range_float(a.siphon_min, a.siphon_max);
    const int32_t period = econ::fiscal_period_frames(rules_->constants.fiscal_cycle_seconds);
    set_siphon(planet, player, fraction, frame_ + period * a.siphon_cycles);
    p.siphon_type = static_cast<uint32_t>(store_->type(hero));
    p.siphon_source = static_cast<int32_t>(store_->id(hero));
    emit_special(GalacticEventKind::SiphonStarted, player, planet, hero, p.siphon_type, a.siphon_cycles, p.owner);
    return true;
}

// ---- GA §4-§5 planet owner bonuses: Galaxy_Wide_Upgrade_Ability, Reduce_Technology_Price_Ability --------------------------

bool GalacticSim::has_planet_bonus(PlanetIndex planet) const {
    if (!valid_planet(planet)) return false;
    for (uint32_t gi : special().type(planets_[planet].type).galab) {
        const SpecialAbilityDef& a = special().galab[gi];
        if ((a.cls == SpecialAbilityClass::GalaxyWideUpgrade || a.cls == SpecialAbilityClass::ReduceTechPrice) && a.initially_enabled)
            return true;
    }
    return false;
}

void GalacticSim::service_planet_bonuses(PlanetIndex planet) {
    // Activation (vf30 0x6ff530 / 0x70bdb0) [H]: the planet holds the ability itself; it is applied to the planet's owner
    // when the planet is not destroyed and its corruption type is not 0 (Intimidation; -1 = none passes). [M] A
    // Galactic_Automatic ability is (re)tried until it applies; the port tries in every planet service while nobody holds
    // the planet's bonuses. Once held, only an owner change moves them (on_owner_change -> move_planet_bonuses).
    if (!valid_planet(planet)) return;
    PlanetState& p = planets_[planet];
    if (p.bonus_holder != kNoPlayer || !valid_player(p.owner) || p.destroyed || p.corruption == 0) return;
    if (!has_planet_bonus(planet)) return;
    p.bonus_holder = p.owner;
    refresh_tech_cost(p.owner);
}

void GalacticSim::move_planet_bonuses(PlanetIndex planet) {
    // vf21 0x6ff850 / 0x70c080 on an owner change [H]: removed from the old player, then applied to the new one (no
    // corruption test; a destroyed planet applies nothing).
    PlanetState& p = planets_[planet];
    if (!has_planet_bonus(planet)) return;
    const int32_t old = p.bonus_holder;
    p.bonus_holder = valid_player(p.owner) && !p.destroyed ? p.owner : kNoPlayer;
    if (old != p.bonus_holder) {
        if (valid_player(old)) refresh_tech_cost(old);
        if (valid_player(p.bonus_holder)) refresh_tech_cost(p.bonus_holder);
    }
}

void GalacticSim::refresh_tech_cost(int32_t player) {
    // 0x281960 [H]: m = 1 + the sum of the player's +0x270 entries (each held Reduce_Technology_Price_Ability adds
    // -Price_Reduction_Percentage, keyed by its source, never expiring); m <= 0 -> 0.0001. The +0x280 / +0x284 clamps
    // have no known writer [M] and are not modelled. Sum in planet row order [port].
    if (!valid_player(player)) return;
    float sum = 0.0f;
    bool any = false;
    for (const PlanetState& q : planets_) {
        if (q.bonus_holder != player) continue;
        for (uint32_t gi : special().type(q.type).galab) {
            const SpecialAbilityDef& a = special().galab[gi];
            if (a.cls != SpecialAbilityClass::ReduceTechPrice || !a.initially_enabled) continue;
            sum += -a.production_reduction;
            any = true;
        }
    }
    float m = any ? sum + 1.0f : 1.0f;
    if (m <= 0.0f) m = 0.0001f;
    players_[static_cast<size_t>(player)].tech_cost_mult = m;
}

std::vector<uint32_t> GalacticSim::player_upgrades(int32_t player, bool space) const {
    // Player +0x300 (space) / +0x318 (land) lists (0x27f4f0 / 0x27f480 append, 0x285f30 / 0x285ee0 remove the first match):
    // one Space_ / Land_Upgrade_Type per held Galaxy_Wide_Upgrade_Ability. [port] Planet row order, then list order.
    std::vector<uint32_t> out;
    if (!valid_player(player)) return out;
    for (const PlanetState& q : planets_) {
        if (q.bonus_holder != player) continue;
        for (uint32_t gi : special().type(q.type).galab) {
            const SpecialAbilityDef& a = special().galab[gi];
            if (a.cls != SpecialAbilityClass::GalaxyWideUpgrade || !a.initially_enabled) continue;
            const uint32_t t = space ? a.space_upgrade : a.land_upgrade;
            if (t != kNoIndex) out.push_back(t);
        }
    }
    return out;
}

// ---- GA §6 Political_Transition_Bonus_Ability (Tarkin) ------------------------------------------------------------------

float GalacticSim::political_transition_factor(PlanetIndex planet) const {
    // 0x709640 / 0x7099e0 [H]: while a holder (a hero of the planet's owner at the planet, accept 0x709980 = the own-planet
    // test 0x48e0c0) is there, an entry {Time_Reduction_Percentage, never expiring} sits in the planet's transition map
    // D +0x78 under its Stacking_Category; it leaves with the hero (vf31). The ramp 0x4adab0 multiplies, over the
    // categories, the smallest entry of each; the result divides the ramp time (so 0.25 makes the transition 4x faster).
    // Returns 1.0 without an entry. Sources: the owner's live objects on the surface and in stationary orbit fleets [M].
    if (!valid_planet(planet)) return 1.0f;
    const PlanetState& p = planets_[planet];
    if (!valid_player(p.owner) || special().galab.empty()) return 1.0f;
    std::vector<std::pair<int32_t, float>> best;  // category -> smallest value, first-seen order
    const auto add = [&](ObjectHandle h) {
        if (!store_->alive(h) || store_->owner(h) != p.owner) return;
        for (uint32_t gi : special_type(h).galab) {
            const SpecialAbilityDef& a = special().galab[gi];
            if (a.cls != SpecialAbilityClass::PoliticalTransition || !a.initially_enabled) continue;
            auto it = std::find_if(best.begin(), best.end(), [&](const auto& e) { return e.first == a.stacking_category; });
            if (it == best.end()) best.push_back({a.stacking_category, a.production_reduction});
            else it->second = std::min(it->second, a.production_reduction);
        }
    };
    for (ObjectHandle u : p.surface) add(u);
    for (uint32_t f : p.orbit)
        if (fleets_[f].alive && !fleets_[f].moving)
            for (ObjectHandle m : fleets_[f].contents) add(m);
    if (best.empty()) return 1.0f;
    std::sort(best.begin(), best.end(), [](const auto& x, const auto& y) { return x.first < y.first; });  // map key order
    float m = 1.0f;
    for (const auto& e : best) m *= e.second;
    return m;
}

}  // namespace alamo::galactic

namespace alamo::galactic {

// ---- CRC tail "GLB1" -----------------------------------------------------------------------------------------------------
// Only while some planet holds a bonus or a siphon source, so sessions that never use these abilities keep their CRC
// stream. Player home planets are fixed at setup and not hashed (like the faction).

bool GalacticSim::galab_state_default() const {
    for (const PlanetState& p : planets_)
        if (p.bonus_holder != kNoPlayer || p.siphon_type != kNoIndex || p.siphon_source != 0) return false;
    return true;
}

void GalacticSim::hash_galab(StateHasher& h) const {
    h.u32(0x474C4231u);  // "GLB1"
    for (const PlanetState& p : planets_) {
        h.i32(p.bonus_holder);
        h.u32(p.siphon_type);
        h.i32(p.siphon_source);
    }
    for (const PlayerState& ps : players_) h.f32(ps.tech_cost_mult);
}

}  // namespace alamo::galactic
