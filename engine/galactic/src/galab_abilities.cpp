// galab (owner: galactic agent): the galactic abilities of roadmap group GALAB - Remove_Corruption_Ability,
// Planet_Destruction_Ability (Death Star), Siphon_Credits_Ability, the planet owner bonuses Galaxy_Wide_Upgrade_Ability and
// Reduce_Technology_Price_Ability, Political_Transition_Bonus_Ability, Neutralize_Hero_Ability and Hero_Protection_Ability. Behaviour: docs/re/GALAB_ABILITIES_RE.md (= GA).
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

bool GalacticSim::political_control_protected(PlanetIndex planet) const {
    // Political_Control_Protection_Ability [H]: accept vf5 0x709450 = the own-planet test (a hero of the planet's owner
    // there); while held, vf30 0x709140 keeps an entry {true, never expiring, keyed by the holder} in the planet list
    // D +0x250 (vf31 0x709470 removes it). The reader 0x4b8510 folds the list with a logical AND: true with any entry,
    // false when empty. Holders: as political_transition_factor [M].
    if (!valid_planet(planet)) return false;
    const PlanetState& p = planets_[planet];
    if (!valid_player(p.owner) || special().galab.empty()) return false;
    const auto holds = [&](ObjectHandle h) {
        if (!store_->alive(h) || store_->owner(h) != p.owner) return false;
        for (uint32_t gi : special_type(h).galab) {
            const SpecialAbilityDef& a = special().galab[gi];
            if (a.cls == SpecialAbilityClass::PoliticalProtection && a.initially_enabled) return true;
        }
        return false;
    };
    for (ObjectHandle u : p.surface)
        if (holds(u)) return true;
    for (uint32_t f : p.orbit)
        if (fleets_[f].alive && !fleets_[f].moving)
            for (ObjectHandle m : fleets_[f].contents)
                if (holds(m)) return true;
    return false;
}

}  // namespace alamo::galactic

namespace alamo::galactic {

// ---- GA §7 Neutralize_Hero_Ability / Hero_Protection_Ability ------------------------------------------------------------

float NeutralizeCandidate::price() const {
    // 0x4b40b0 [H]: the lines summed in list order, then the record's clamps: the candidate ctor sets the minimum 1.0
    // (+0x28, flag +0x30); no maximum.
    float sum = 0.0f;
    for (const auto& t : terms) sum += t.second;
    return sum < 1.0f ? 1.0f : sum;
}

bool GalacticSim::neutralize_accepts(PlanetIndex planet) const {
    // vf5 0x6166d0 [H]: the target is a planet and 0x61b610 (the hack super-weapon search, P4 3.2) finds nothing in its
    // orbit: no neutralizing while a Death Star is there.
    return valid_planet(planet) && planets_[planet].is_planet && !store_->alive(hack_find_super_weapon(planet));
}

bool GalacticSim::neutralize_target_visible(ObjectHandle target, int32_t player, PlanetIndex planet, bool space) const {
    // 0x50bba0 for a hero [M composite reading, REMAINING_GAPS 6.3]: the minor (generic) or major (named) stealth-hero
    // bit decides for a stealthed hero; an unstealthed one is seen with that bit, or with the planet's fleet-contents
    // (orbit) / ground-company-contents (surface) information.
    const TypeSpecial& t = special_type(target);
    const PlanetInfo bit = t.generic_hero ? PlanetInfo::MinorStealthHeroes : PlanetInfo::MajorStealthHeroes;
    const bool stealth_seen = can_see_planet_info(player, planet, bit);
    if (galactic_stealth(target)) return stealth_seen;
    return stealth_seen ||
           can_see_planet_info(player, planet, space ? PlanetInfo::FleetContents : PlanetInfo::GroundCompanyContents);
}

float GalacticSim::hero_protection(PlanetIndex planet, ObjectHandle target) const {
    // Hero_Protection_Ability [H]: while its holder is at the planet and the accept 0x703470 holds, an entry
    // {Neutralization_Cost_Multiplier, never expiring, keyed by the holder} sits in the planet map D +0x2A8 under its
    // Stacking_Category (vf30 0x703140; vf31 0x703600 removes it). Accept: the planet holding it itself; else a
    // multiplier above 1 needs the holder allied with the planet's owner, below 1 not allied, exactly 1 never.
    // The cost function 0x6171e0 copies the map without the target's own entries, then multiplies over the categories
    // (ascending) the largest value of each. The map's clamps (+0x2B8 / +0x2BC) have no known writer [M]: not modelled.
    // Holders: the planet, the surface units and the orbiting fleets' contents, any owner [M].
    if (!valid_planet(planet)) return 1.0f;
    const PlanetState& p = planets_[planet];
    std::vector<std::pair<int32_t, float>> best;  // category -> largest
    const auto take = [&](const SpecialAbilityDef& a, int32_t holder, bool the_planet) {
        if (a.cls != SpecialAbilityClass::HeroProtection || !a.initially_enabled) return;
        const float m = a.protection_multiplier;
        if (!the_planet) {
            if (m == 1.0f) return;
            const bool friendly = allied(holder, p.owner);
            if (m > 1.0f ? !friendly : friendly) return;
        }
        for (auto& e : best)
            if (e.first == a.stacking_category) {
                if (m > e.second) e.second = m;
                return;
            }
        best.emplace_back(a.stacking_category, m);
    };
    for (uint32_t gi : special().type(p.type).galab) take(special().galab[gi], p.owner, true);
    const auto add = [&](ObjectHandle h) {
        if (!store_->alive(h) || h == target) return;
        for (uint32_t gi : special_type(h).galab) take(special().galab[gi], store_->owner(h), false);
    };
    for (ObjectHandle u : p.surface) add(u);
    for (uint32_t f : p.orbit)
        if (fleets_[f].alive)
            for (ObjectHandle m : fleets_[f].contents) add(m);
    if (best.empty()) return 1.0f;
    std::sort(best.begin(), best.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    float m = best.front().second;
    for (size_t i = 1; i < best.size(); ++i) m *= best[i].second;
    return m;
}

namespace {
// neutralized_ is (player, type)-ascending.
constexpr auto neutralized_before = [](const auto& e, const std::pair<int32_t, uint32_t>& k) {
    return e.player != k.first ? e.player < k.first : e.type < k.second;
};
}  // namespace

int32_t GalacticSim::neutralized_count(int32_t player, uint32_t type) const {
    const auto it = std::lower_bound(neutralized_.begin(), neutralized_.end(), std::make_pair(player, type), neutralized_before);
    return it != neutralized_.end() && it->player == player && it->type == type ? it->count : 0;
}

void GalacticSim::count_neutralized(int32_t player, uint32_t type) {
    // GameScoring.lua Galactic_Neutralized_Event(hero_type, killer): GalacticNeutralizedTable[killer owner][type] += 1.
    auto it = std::lower_bound(neutralized_.begin(), neutralized_.end(), std::make_pair(player, type), neutralized_before);
    if (it != neutralized_.end() && it->player == player && it->type == type) ++it->count;
    else neutralized_.insert(it, NeutralizedEntry{player, type, 1});
}

std::vector<NeutralizeCandidate> GalacticSim::neutralize_candidates(const SpecialAbilityDef& a, ObjectHandle neutralizer,
                                                                    int32_t player, PlanetIndex planet) {
    // 0x615fd0 + 0x615d20 [H]: the orbit list (space flag 1), then the surface list (0), heroes in list order (a fleet's
    // members in order); each skipped when allied with `player`, without a Neutralization_Cost > 0, or not visible to
    // `player`. Then the planet's siphon (D +0x200 set by a player not allied with `player`): the siphoning hero's type,
    // no cost or visibility test. Eligible (+0x61): a generic target needs Can_Neutralize_Minor_Heroes, a named one
    // Can_Neutralize_Major_Heroes; a named neutralizer needs the target's Can_Be_Neutralized_By_Major_Heroes, any other
    // its Can_Be_Neutralized_By_Minor_Heroes.
    std::vector<NeutralizeCandidate> out;
    if (!valid_planet(planet) || !valid_player(player)) return out;
    const PlanetState& p = planets_[planet];
    const bool named_neutralizer = special_type(neutralizer).named_hero;
    const auto eligible = [&](const TypeSpecial& t) {
        bool e = true;
        if (t.generic_hero) e = a.can_neutralize_minor;
        if (t.named_hero && !a.can_neutralize_major) e = false;
        return e && (named_neutralizer ? t.neutralizable_by_major : t.neutralizable_by_minor);
    };
    const auto add = [&](ObjectHandle h, bool space) {
        if (!store_->alive(h)) return;
        const uint32_t ty = static_cast<uint32_t>(store_->type(h));
        const TypeSpecial& t = special().type(ty);
        if (!t.generic_hero && !t.named_hero) return;
        if (allied(player, store_->owner(h)) || t.neutralization_cost <= 0.0f) return;
        if (!neutralize_target_visible(h, player, planet, space)) return;
        NeutralizeCandidate c;
        c.type = ty;
        c.object = static_cast<int32_t>(store_->id(h));
        c.space = space;
        c.eligible = eligible(t);
        out.push_back(std::move(c));
    };
    for (uint32_t f : p.orbit)
        if (fleets_[f].alive)
            for (ObjectHandle m : fleets_[f].contents) add(m, true);
    for (ObjectHandle u : p.surface) add(u, false);
    if (valid_player(p.siphon_player) && !allied(player, p.siphon_player) && p.siphon_type != kNoIndex) {
        NeutralizeCandidate c;
        c.type = p.siphon_type;
        c.object = p.siphon_source;
        c.siphon = true;
        c.eligible = eligible(special().type(p.siphon_type));
        out.push_back(std::move(c));
    }
    // Cost 0x6171e0 [H], per candidate in list order: the two respawn draws (Get_Float 0x1ffbb0, owner then target;
    // either bound <= 0 -> Default_Hero_Respawn_Time, no draw), then the lines (base always, the others when non-zero):
    //   base = Neutralization_Cost x General_Major_Hero_Cost_Mod (named) / General_Minor_Hero_Cost_Mod (generic);
    //   base levels = Cost_Mod_By_Base_Level(level) x base, level = the starbase (orbit) or ground base (surface) level
    //     when the target's owner is allied with the planet's owner (a hero defended by the planet's bases), else 0;
    //   hero protection = (the Hero_Protection multiplier - 1) x base;
    //   prior neutralizations (a named hero target only) = Cost_Mod_By_Previous_Neutralizations(n) x base, n = how many
    //     of this type the neutralizer's owner has neutralized (GameScoring Get_Neutralized_Count_For_Type);
    //   planetary corruption = -Cost_Mod_By_Planetary_Corruption_Level(1 if corrupted else 0) x base.
    const float corrupt_x = p.corruption != kCorruptionNone ? 1.0f : 0.0f;
    for (NeutralizeCandidate& c : out) {
        ObjectHandle target;
        int32_t target_owner = p.siphon_player;
        if (!c.siphon) {
            target = store_->find(static_cast<objects::ObjectId>(c.object));
            target_owner = store_->owner(target);
        }
        c.owner_respawn = a.owner_respawn_min <= 0.0f || a.owner_respawn_max <= 0.0f
                              ? special().default_hero_respawn_time
                              : rng_->range_float(a.owner_respawn_min, a.owner_respawn_max);
        c.target_respawn = a.target_respawn_min <= 0.0f || a.target_respawn_max <= 0.0f
                               ? special().default_hero_respawn_time
                               : rng_->range_float(a.target_respawn_min, a.target_respawn_max);
        const TypeSpecial& t = special().type(c.type);
        float base = t.neutralization_cost;
        if (t.named_hero) base *= a.major_cost_mod;
        else if (t.generic_hero) base *= a.minor_cost_mod;
        float level = 0.0f;
        if (allied(target_owner, p.owner))
            level = static_cast<float>(c.space ? p.starbase_level : ground_base_level(planet));
        const float levels = a.cost_by_base_level.eval(level) * base;
        const float shield = (hero_protection(planet, target) - 1.0f) * base;
        float prior = 0.0f;
        if (!c.siphon && t.named_hero)
            prior = a.cost_by_neutralizations.eval(static_cast<float>(neutralized_count(player, c.type))) * base;
        const float corruption = -a.cost_by_corruption.eval(corrupt_x) * base;
        c.terms.emplace_back(NeutralizeTerm::Base, base);
        if (levels != 0.0f) c.terms.emplace_back(NeutralizeTerm::BaseLevels, levels);
        if (shield != 0.0f) c.terms.emplace_back(NeutralizeTerm::HeroProtection, shield);
        if (prior != 0.0f) c.terms.emplace_back(NeutralizeTerm::PriorNeutralizations, prior);
        if (corruption != 0.0f) c.terms.emplace_back(NeutralizeTerm::PlanetaryCorruption, corruption);
    }
    return out;
}

bool GalacticSim::apply_neutralize(const SpecialAbilityDef& a, ObjectHandle neutralizer, int32_t player, PlanetIndex planet) {
    // vf30 0x615fd0 [H]: the accept again; the candidates; none -> the local player's error SFX. A local human gets the
    // panel (0x3c8d90; the pick comes back as SpecialAbility kind 3) and nothing is applied now. An AI weighs every
    // eligible candidate it can pay for (price <= credits) by 1, makes one synced draw Get_Float(0, n) (0x532480) and
    // takes the first candidate whose cumulative weight >= the draw; then the executor. A remote human: nothing.
    if (!neutralize_accepts(planet)) return false;
    std::vector<NeutralizeCandidate> cands = neutralize_candidates(a, neutralizer, player, planet);
    if (cands.empty()) {
        emit_special(GalacticEventKind::SpecialError, player, planet, neutralizer, kNoIndex, 0, -1);
        return false;
    }
    const PlayerState& ps = players_[static_cast<size_t>(player)];
    if (ps.human) {
        const int32_t n = static_cast<int32_t>(cands.size());
        neutralize_offer_ = std::move(cands);
        emit_special(GalacticEventKind::NeutralizeDialog, player, planet, neutralizer, kNoIndex, n, -1);
        return false;
    }
    if (!ps.ai) return false;
    std::vector<size_t> pool;
    for (size_t i = 0; i < cands.size(); ++i)
        if (cands[i].eligible && cands[i].price() <= ps.account.credits) pool.push_back(i);
    if (pool.empty()) return false;  // cumulative weight 0: no draw
    const float r = rng_->inline_range_float(0.0f, static_cast<float>(pool.size()));
    size_t pick = pool.size() - 1;
    for (size_t k = 0; k < pool.size(); ++k)
        if (static_cast<float>(k + 1) >= r) {
            pick = k;
            break;
        }
    return neutralize_do(player, planet, neutralizer, cands[pool[pick]]);
}

RejectReason GalacticSim::neutralize_execute(int32_t player, PlanetIndex planet, ObjectHandle neutralizer,
                                             const NeutralizeCandidate& c) {
    // SpecialAbility kind 3 (0x4b4d70 case 3): runs for a neutralizer at a planet. The candidate comes from the payload as
    // in the original (its price is the q8 code; the executor re-checks eligibility, credits, a live target or the
    // siphon). [port] Owner test as the other kinds, and the type must be a type (it is counted and saved).
    if (!valid_player(player)) return RejectReason::InvalidPlayer;
    if (!valid_planet(planet)) return RejectReason::InvalidTarget;
    if (!store_->alive(neutralizer) || store_->owner(neutralizer) != player) return RejectReason::InvalidUnit;
    if (location_of(neutralizer) != planet) return RejectReason::InvalidTarget;
    if (c.type >= rules_->types.size()) return RejectReason::Malformed;
    return neutralize_do(player, planet, neutralizer, c) ? RejectReason::None : RejectReason::NotAllowed;
}

bool GalacticSim::neutralize_do(int32_t player, PlanetIndex planet, ObjectHandle neutralizer, const NeutralizeCandidate& c) {
    // Executor 0x616740 [H], in order:
    //  1. a siphon entry needs the planet still siphoned; the candidate must be eligible and its price <= credits; a
    //     hero entry needs its target alive (each refusal: the local player's error SFX);
    //  2. Planet_Vigilance catches a generic neutralizer: respawn at the default time, killed, nothing else;
    //  3. a hero target: respawn scheduled with the candidate's target time, the "neutralized" object signal (GameScoring
    //     Galactic_Neutralized_Event: the count) and despawn; the siphon entry: the count, the siphon record cleared;
    //  4. Add_Credits(-price, no AI notify); story LAND_ON / DEPLOY; the story generic "hero_neutralized";
    //  5. the neutralizer leaves: respawn with the candidate's owner time, despawn.
    PlanetState& p = planets_[planet];
    const float price = c.price();
    ObjectHandle target;
    if (!c.siphon) target = store_->find(static_cast<objects::ObjectId>(c.object));
    // [port] A siphon entry also needs the siphon it was made for (a player not allied with `player`, the same hero type):
    // a pick that arrives after the record changed hands is refused rather than clearing another siphon.
    const bool target_ok = c.siphon ? valid_player(p.siphon_player) && !allied(player, p.siphon_player) && p.siphon_type == c.type
                                    : store_->alive(target);
    const bool ok = target_ok && c.eligible && price <= players_[static_cast<size_t>(player)].account.credits;
    if (!ok) {
        emit_special(GalacticEventKind::SpecialError, player, planet, neutralizer, kNoIndex, 0, -1);
        return false;
    }
    if (special().type(p.type).planet_vigilance && special_type(neutralizer).generic_hero) {
        emit_special(GalacticEventKind::NeutralizeVigilance, player, planet, neutralizer, kNoIndex, 0, p.owner);
        schedule_hero_respawn(neutralizer, planet, 0.0f);
        destroy_object(neutralizer);
        return false;
    }
    const int32_t target_owner = c.siphon ? p.siphon_player : store_->owner(target);
    const uint32_t counted = c.siphon ? p.siphon_type : static_cast<uint32_t>(store_->type(target));  // the real type
    if (!c.siphon) {
        hero_depart(target, planet, c.target_respawn);
    } else {
        p.siphon_player = kNoPlayer;  // 0x3ff1f0
        p.siphon_amount = 0.0f;
        p.siphon_type = kNoIndex;
        p.siphon_source = 0;
    }
    count_neutralized(player, counted);
    add_credits(player, -price, false);
    uint32_t ref = kNoIndex;  // the neutralizer's Neutralize_Hero entry, for the LAND_ON event
    for (uint32_t r : special_type(neutralizer).triggers())
        if (special_def(r).cls == SpecialAbilityClass::NeutralizeHero) {
            ref = r;
            break;
        }
    emit_special(GalacticEventKind::SpecialApplied, player, planet, neutralizer,
                 static_cast<uint32_t>(store_->type(neutralizer)), static_cast<int32_t>(ref), -1);
    emit_special(GalacticEventKind::HeroNeutralized, player, planet, target, counted, static_cast<int32_t>(price + 0.5f),
                 target_owner);
    hero_depart(neutralizer, planet, c.owner_respawn);
    return true;
}

// ---- CRC tail "GLB1" -----------------------------------------------------------------------------------------------------
// Only while some planet holds a bonus or a siphon source, or a hero was neutralized, so sessions that never use these
// abilities keep their CRC stream (an FoC campaign has bonus holders from its first planet service). Player home planets
// are fixed at setup and not hashed (like the faction).

bool GalacticSim::galab_state_default() const {
    if (!neutralized_.empty()) return false;
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
    h.u32(static_cast<uint32_t>(neutralized_.size()));  // GA §7: GameScoring's neutralized counts
    for (const NeutralizedEntry& e : neutralized_) {
        h.i32(e.player);
        h.u32(e.type);
        h.i32(e.count);
    }
}

}  // namespace alamo::galactic
