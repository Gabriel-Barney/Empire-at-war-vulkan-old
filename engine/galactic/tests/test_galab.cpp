// alamo_galactic_galab_tests: the galactic abilities of roadmap group GALAB (docs/re/GALAB_ABILITIES_RE.md = GA), on a
// synthetic galaxy with hand-computed expectations:
//   1. Remove_Corruption_Ability: accept (own corrupted planet, no transition), cost = base x multiplier, credit refusal,
//      the removal transition with Corruption_Removal_Time_In_Secs, its completion;
//   2. Planet_Destruction_Ability (Death Star): accept (not destroyed, not own / allied), the contested-space refusal,
//      the leader test and victory claim 7 (countdown 30), the planet destroyed;
//   3. Siphon_Credits_Ability: accept, faction pairing, vigilance kill, one synced draw, the siphon record (player,
//      fraction, cycles, source type / object), the income moved to the siphoner, the end after the cycles;
//   4. Galaxy_Wide_Upgrade_Ability: the owner holds the planet's Space_ / Land_Upgrade_Type (player_upgrades), the
//      Intimidation gate of the first apply, the move on an owner change;
//   5. Reduce_Technology_Price_Ability: the owner's tech-cost multiplier 1 - reduction, back to 1 on an owner change;
//   6. Political_Transition_Bonus_Ability: the political ramp factor while the holder is present; Political_Control_
//      Protection_Ability: protected while the owner's holder is there;
//   7. Neutralize_Hero_Ability / Hero_Protection_Ability: candidates, the respawn draws, the cost lines (base, hero
//      protection, prior neutralizations, planetary corruption), the AI pick and the executor (charge, both heroes
//      leave with their drawn respawn times, the count), the siphon entry, eligibility, vigilance, the accept, the
//      kind 3 payload;
//   8. FoC data (ALAMO_FOC_DATA; skipped when unset): the tables of the vanilla entries.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "alamo/core/bytes.hpp"
#include "alamo/core/file_provider.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/data/game_database.hpp"
#include "alamo/galactic/campaign.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/objects/object_types.hpp"
#include "foc_data.hpp"
#include "alamo/test/support.hpp"

using namespace alamo;
using namespace alamo::galactic;

using ::alamo::test::g_fail;
using ::alamo::test::g_checks;

namespace {

enum TypeId : uint32_t {
    tPA, tPB, tPC, tPD, tPE,  // planets
    tVader, tDeathStar, tFrigate, tHan, tSmuggler, tTarkin, tPalpatine, tMothma, tSpaceUp, tLandUp,
    tBoba, tHunter, tJabba, tStarbase, tAce,
    tTypeCount
};
enum Row : PlanetIndex { rA = 0, rB = 1, rC = 2, rD = 3, rE = 4 };
enum PlayerId : int32_t { pEmp = 0, pReb = 1, pUnd = 2, pNeu = 3 };
enum GalabId : uint32_t {
    gRemove, gDestroy, gHanSiphon, gSmugSiphon, gUpgrade, gTechPrice, gTarkin, gBobaNeut, gHunterNeut, gJabbaProt, gTarkinProt,
    gCount
};

SpecialAbilityDef def(SpecialAbilityClass cls, const char* name, ActivationStyle style) {
    SpecialAbilityDef a;
    a.cls = cls;
    a.name = name;
    a.name_crc = special_name_crc(name);
    a.style = style;
    return a;
}

GalacticRules make_rules() {
    GalacticRules r;
    auto faction = [](const char* n, bool neutral, bool benefits) {
        FactionRules f;
        f.name = n;
        f.is_neutral = neutral;
        f.can_control_planets = f.is_playable = !neutral;
        f.benefits_from_corruption = benefits;
        return f;
    };
    r.factions = {faction("Empire", false, false), faction("Rebel", false, false), faction("Underworld", false, true),
                  faction("Neutral", true, false)};
    r.factions[1].leader = tMothma;  // victory 7: the Rebel leader
    r.types.resize(tTypeCount);
    const char* pn[] = {"PA", "PB", "PC", "PD", "PE"};
    for (uint32_t i = 0; i < 5; ++i) {
        r.types[i].name = pn[i];
        r.types[i].kind = UnitKind::Planet;
        r.types[i].credit_value = 100;
        r.types[i].destroyed_credit_value = 2;
    }
    auto unit = [&r](uint32_t id, const char* name, UnitKind k) -> TypeInfo& {
        r.types[id].name = name;
        r.types[id].kind = k;
        return r.types[id];
    };
    unit(tVader, "Vader", UnitKind::GroundUnit);
    unit(tDeathStar, "Death_Star", UnitKind::SpaceUnit).planet_destruction = true;
    unit(tFrigate, "Frigate", UnitKind::SpaceUnit).ai_combat_power = 100.0f;
    unit(tHan, "Han", UnitKind::GroundUnit);
    unit(tSmuggler, "Smuggler", UnitKind::GroundUnit);
    unit(tTarkin, "Tarkin", UnitKind::GroundUnit);
    unit(tPalpatine, "Palpatine", UnitKind::GroundUnit);
    unit(tMothma, "Mon_Mothma", UnitKind::GroundUnit);
    unit(tSpaceUp, "Krayt_Damage_Upgrade", UnitKind::GroundStructure);
    unit(tLandUp, "ATAT_Damage_Upgrade", UnitKind::GroundStructure);
    unit(tBoba, "Boba_Fett", UnitKind::GroundUnit);
    unit(tHunter, "Bounty_Hunter", UnitKind::GroundUnit);
    unit(tJabba, "Jabba", UnitKind::GroundUnit);
    unit(tStarbase, "Star_Base_3", UnitKind::StarBase).base_level = 3;
    unit(tAce, "Space_Ace", UnitKind::SpaceUnit);

    SpecialRules& s = r.special;
    s.galab.resize(gCount);
    s.galab[gRemove] = def(SpecialAbilityClass::RemoveCorruption, "Vader_Remove_Corruption", ActivationStyle::GroundActivated);
    s.galab[gRemove].removal_cost_multiplier = 1.5f;
    s.galab[gRemove].removal_seconds = 3.0f;
    s.galab[gDestroy] = def(SpecialAbilityClass::PlanetDestruction, "Death_Star_Planet_Destruction", ActivationStyle::GroundActivated);
    s.galab[gHanSiphon] = def(SpecialAbilityClass::SiphonCredits, "Han_Solo_Siphon_Credits", ActivationStyle::GroundActivated);
    s.galab[gHanSiphon].siphon_min = 0.45f;
    s.galab[gHanSiphon].siphon_max = 0.65f;
    s.galab[gHanSiphon].siphon_cycles = 2;
    s.galab[gHanSiphon].causes_despawn = true;
    s.galab[gHanSiphon].min_respawn = s.galab[gHanSiphon].max_respawn = 300.0f;
    s.galab[gSmugSiphon] = def(SpecialAbilityClass::SiphonCredits, "Smuggler_Siphon_Credits", ActivationStyle::GroundActivated);
    s.galab[gSmugSiphon].siphon_min = s.galab[gSmugSiphon].siphon_max = 0.5f;
    s.galab[gSmugSiphon].siphon_cycles = 5;
    s.galab[gUpgrade] = def(SpecialAbilityClass::GalaxyWideUpgrade, "PC_Upgrade_Bonus", ActivationStyle::GalacticAutomatic);
    s.galab[gUpgrade].space_upgrade = tSpaceUp;
    s.galab[gUpgrade].land_upgrade = tLandUp;
    s.galab[gTechPrice] = def(SpecialAbilityClass::ReduceTechPrice, "PD_Tech_Price", ActivationStyle::GalacticAutomatic);
    s.galab[gTechPrice].production_reduction = 0.15f;
    s.galab[gTarkin] = def(SpecialAbilityClass::PoliticalTransition, "Tarkin_Political_Transition_Bonus", ActivationStyle::GalacticAutomatic);
    s.galab[gTarkin].production_reduction = 0.25f;
    // Neutralize_Hero: the FoC Boba Fett / Bounty_Hunter values.
    const auto curve = [](float x1, float y1) { return PiecewiseCurve{{0.0f, x1}, {0.0f, y1}}; };
    s.galab[gBobaNeut] = def(SpecialAbilityClass::NeutralizeHero, "Boba_Fett_Neutralize_Hero", ActivationStyle::GroundActivated);
    s.galab[gBobaNeut].cost_by_base_level = curve(5.0f, 1.0f);
    s.galab[gBobaNeut].cost_by_neutralizations = curve(1000.0f, 750.0f);
    s.galab[gBobaNeut].cost_by_corruption = curve(1.0f, 0.25f);
    s.galab[gBobaNeut].owner_respawn_min = 120.0f;
    s.galab[gBobaNeut].owner_respawn_max = 150.0f;
    s.galab[gBobaNeut].target_respawn_min = 240.0f;
    s.galab[gBobaNeut].target_respawn_max = 300.0f;
    s.galab[gHunterNeut] = s.galab[gBobaNeut];
    s.galab[gHunterNeut].name = "Bounty_Hunter_Neutralize_Hero";
    s.galab[gHunterNeut].name_crc = special_name_crc(s.galab[gHunterNeut].name);
    s.galab[gHunterNeut].can_neutralize_major = false;
    s.galab[gHunterNeut].owner_respawn_min = 240.0f;
    s.galab[gHunterNeut].owner_respawn_max = 300.0f;
    s.galab[gJabbaProt] = def(SpecialAbilityClass::HeroProtection, "Jabba_Hero_Protection", ActivationStyle::GalacticAutomatic);
    s.galab[gJabbaProt].protection_multiplier = 2.0f;
    s.galab[gTarkinProt] = def(SpecialAbilityClass::PoliticalProtection, "Tarkin_Political_Protection", ActivationStyle::GalacticAutomatic);
    s.hack_required_type = tDeathStar;  // Neutralize_Hero accept: no hack-target super weapon in orbit

    s.types.resize(tTypeCount);
    auto give = [&s](uint32_t t, std::vector<uint32_t> g) {
        s.types[t].galab = g;
        for (uint32_t i : g) s.types[t].trigger_order.push_back(kGalabRef | i);
    };
    give(tVader, {gRemove});
    s.types[tVader].named_hero = true;
    give(tDeathStar, {gDestroy});
    give(tHan, {gHanSiphon});
    s.types[tHan].named_hero = true;
    give(tSmuggler, {gSmugSiphon});
    s.types[tSmuggler].generic_hero = true;
    give(tTarkin, {gTarkin, gTarkinProt});
    give(tPC, {gUpgrade});
    give(tPD, {gTechPrice});
    give(tBoba, {gBobaNeut});
    s.types[tBoba].named_hero = true;
    give(tHunter, {gHunterNeut});
    s.types[tHunter].generic_hero = true;
    give(tJabba, {gJabbaProt});
    s.types[tJabba].named_hero = true;
    s.types[tHan].neutralization_cost = 2500.0f;  // FoC Han: major only
    s.types[tHan].neutralizable_by_major = true;
    s.types[tAce].named_hero = true;  // a named space hero (orbit candidate)
    s.types[tAce].neutralization_cost = 1000.0f;
    s.types[tAce].neutralizable_by_major = true;
    s.types[tSmuggler].neutralization_cost = 100.0f;  // FoC smugglers: both
    s.types[tSmuggler].neutralizable_by_major = s.types[tSmuggler].neutralizable_by_minor = true;
    s.types[tPA].corruption_removal_cost = 800.0f;
    s.types[tPE].planet_vigilance = true;
    return r;
}

GalacticSetup make_setup() {
    GalacticSetup s;
    GalaxyMap& m = s.map;
    m.campaign_name = "GalabSynthetic";
    const char* names[] = {"A", "B", "C", "D", "E"};
    const Vec3 pos[] = {{0, 0, 0}, {60, 0, 0}, {120, 0, 0}, {60, 80, 0}, {300, 0, 0}};
    for (uint32_t i = 0; i < 5; ++i) {
        m.planets.add(names[i]);
        m.planets.position[i] = pos[i];
        m.planets.flags[i] = static_cast<uint32_t>(kPlanetHasPlanetBehavior);
    }
    auto route = [&m](const char* n, PlanetIndex a, PlanetIndex b) {
        const uint32_t r = m.routes.add(n);
        m.routes.point_a[r] = a;
        m.routes.point_b[r] = b;
        m.routes.hs_speed_factor[r] = 5.0f;
        m.routes.credit_gain_factor[r] = 0.0f;
    };
    route("A_B", rA, rB);
    route("B_C", rB, rC);
    route("B_D", rB, rD);
    route("C_E", rC, rE);
    s.planet_type = {tPA, tPB, tPC, tPD, tPE};
    auto player = [](uint32_t f, bool human, float credits) {
        PlayerSetup p;
        p.faction = f;
        p.human = human;
        p.ai = !human;
        p.credits = credits;
        return p;
    };
    s.players = {player(0, false, 5000.0f), player(1, false, 5000.0f), player(2, false, 5000.0f), player(3, false, 0.0f)};
    s.players[3].ai = false;
    s.players[0].home = rA;  // galab t06: Home_Location
    // Owners: A Empire, B Rebel, C Underworld, D Empire, E Rebel (a frigate holds each).
    s.forces = {{pEmp, rA, tFrigate}, {pReb, rB, tFrigate}, {pUnd, rC, tFrigate}, {pEmp, rD, tFrigate}, {pReb, rE, tFrigate}};
    return s;
}

struct World {
    GalacticRules rules = make_rules();
    objects::GameObjectTypes types;
    objects::IdCounter ids;
    objects::ObjectStore store{objects::ModeKind::Galactic, objects::StoreKind::Primary, types, &ids};
    SimRng rng{0x6A1Au};
    GalacticSim sim{rules, store, rng};
    World() {
        std::vector<std::string> diag;
        CHECK(sim.start(make_setup(), &diag));
        sim.set_emit_events(true);
    }
    void run(int n) {
        for (int i = 0; i < n; ++i) sim.update();
    }
    const PlanetState& planet(PlanetIndex p) const { return sim.planets()[p]; }
    size_t count(GalacticEventKind k) const {
        size_t n = 0;
        for (const GalacticEvent& e : sim.events()) n += e.kind == k ? 1u : 0u;
        return n;
    }
};

// Destroys every unit of `player`'s fleets at `planet` (the empty fleets go with them).
void clear_orbit(GalacticSim& s, PlanetIndex planet, int32_t player) {
    for (uint32_t f : s.fleets_at(planet, player)) {
        const std::vector<ObjectHandle> units = s.fleets()[f].contents;
        for (ObjectHandle u : units) s.destroy_object(u);
    }
}

// ---- 1. Remove_Corruption_Ability --------------------------------------------------------------------------------------

void test_remove_corruption() {
    World w;
    GalacticSim& s = w.sim;
    const ObjectHandle vader = s.spawn_unit(pEmp, rA, tVader);
    CHECK(!s.remove_corruption_accepts(pEmp, rA));  // not corrupted
    CHECK(!s.ground_activated_trigger(vader, rA));
    s.set_corruption(rA, 1, pUnd);  // Piracy
    s.set_corruption(rB, 1, pUnd);
    CHECK(s.remove_corruption_accepts(pEmp, rA) && !s.remove_corruption_accepts(pEmp, rB));  // B is not Empire's
    // cost = Planet_Corruption_Removal_Base_Cost 800 x multiplier 1.5 = 1200
    CHECK(s.corruption_removal_cost(w.rules.special.galab[gRemove], rA) == 1200.0f);
    s.add_credits(pEmp, -4000.0f);  // 1000 left: refused, nothing charged, error SFX
    CHECK(!s.ground_activated_trigger(vader, rA) && s.credits(pEmp) == 1000.0f && !w.planet(rA).corruption_removing &&
          w.count(GalacticEventKind::SpecialError) == 1);
    s.add_credits(pEmp, 200.0f);  // exactly 1200: enough
    bool accepted = false;
    const int32_t f0 = s.frame();
    CHECK(s.ground_activated_trigger(vader, rA, &accepted) && accepted && s.credits(pEmp) == 0.0f);
    CHECK(w.store.alive(vader) && w.count(GalacticEventKind::SpecialApplied) == 1);  // Causes_Despawn No
    const PlanetState& a = w.planet(rA);
    CHECK(a.corruption_removing && a.corruption == 1 && a.corruption_end - a.corruption_start == 90u);  // 3 s
    CHECK(static_cast<int32_t>(a.corruption_start) == f0);
    CHECK(!s.remove_corruption_accepts(pEmp, rA));  // a transition runs
    w.run(95);
    CHECK(w.planet(rA).corruption == kCorruptionNone && !w.planet(rA).corruption_removing);
    // Multiplier 0 (kept by the post-load rule): free.
    w.rules.special.galab[gRemove].removal_cost_multiplier = 0.0f;
    s.set_corruption(rA, 3, pUnd);
    CHECK(s.ground_activated_trigger(vader, rA) && s.credits(pEmp) == 0.0f);
}

// ---- 2. Planet_Destruction_Ability ---------------------------------------------------------------------------------------

void test_death_star() {
    {
        World w;
        GalacticSim& s = w.sim;
        const ObjectHandle ds = s.spawn_unit(pEmp, rD, tDeathStar);
        CHECK(!s.planet_destruction_accepts(pEmp, rA) && !s.planet_destruction_accepts(pEmp, rD));  // own planets
        CHECK(s.planet_destruction_accepts(pEmp, rB) && s.planet_destruction_accepts(pEmp, rC));
        // Contested: the Rebel frigate orbits B -> refused, nothing destroyed, error SFX.
        CHECK(!s.ground_activated_trigger(ds, rB) && !w.planet(rB).destroyed && w.count(GalacticEventKind::SpecialError) == 1);
        // C after its Underworld fleet is gone; the Rebel leader is not there: destroyed, no claim.
        clear_orbit(s, rC, pUnd);
        CHECK(s.ground_activated_trigger(ds, rC) && w.planet(rC).destroyed && s.victory_claims().empty());
        CHECK(w.count(GalacticEventKind::SpecialApplied) == 0 && w.count(GalacticEventKind::PlanetDestroyed) == 1);
        CHECK(!s.planet_destruction_accepts(pEmp, rC));  // already destroyed
        CHECK(w.store.alive(ds));
    }
    {
        World w;  // leader test: Mon Mothma on B (Rebel) -> claim 7, countdown 30
        GalacticSim& s = w.sim;
        const ObjectHandle ds = s.spawn_unit(pEmp, rD, tDeathStar);
        s.spawn_unit(pReb, rB, tMothma);
        clear_orbit(s, rB, pReb);
        CHECK(s.ground_activated_trigger(ds, rB) && w.planet(rB).destroyed);
        CHECK(s.victory_claims().size() == 1 && s.victory_claims()[0].player == pEmp && s.victory_claims()[0].type == 7 &&
              s.victory_claims()[0].countdown == 30);
    }
}

// ---- 3. Siphon_Credits_Ability -------------------------------------------------------------------------------------------

void test_siphon() {
    {
        World w;
        GalacticSim& s = w.sim;
        const ObjectHandle han = s.spawn_unit(pReb, rA, tHan);  // Rebel hero on an Empire planet
        CHECK(s.siphon_accepts(pReb, rA) && !s.siphon_accepts(pReb, rB) && !s.siphon_accepts(pEmp, rA));
        SimRng expect = w.rng;
        const float fraction = expect.range_float(0.45f, 0.65f);  // one synced draw
        (void)expect.range_float(300.0f, 300.0f);                 // the Causes_Despawn draw
        const int32_t f0 = s.frame();
        const int32_t period = econ::fiscal_period_frames(w.rules.constants.fiscal_cycle_seconds);
        const float before = s.planet_income(rA, false);
        CHECK(s.ground_activated_trigger(han, rA));
        CHECK(w.rng == expect && !w.store.alive(han));  // departed: respawn in 300 s
        const PlanetState& a = w.planet(rA);
        CHECK(a.siphon_player == pReb && a.siphon_fraction == fraction && a.siphon_until == f0 + 2 * period);
        CHECK(a.siphon_type == tHan && a.siphon_source != 0 && w.count(GalacticEventKind::SiphonStarted) == 1);
        CHECK(s.respawns_pending(rA).size() == 1);
        // Income moved: the owner keeps (1 - f) of the net, the siphoner gets f of it.
        const float owner_part = s.planet_income(rA, false);
        const float taken = w.planet(rA).siphon_amount;
        CHECK(taken > 0.0f && owner_part < before && std::abs(owner_part + taken - before) < 0.01f);
        const float reb_with = s.player_income(pReb, false);
        CHECK(reb_with >= taken);
        // A second siphon of the same planet is refused (error SFX).
        const ObjectHandle han2 = s.spawn_unit(pReb, rA, tHan);
        CHECK(!s.ground_activated_trigger(han2, rA) && w.count(GalacticEventKind::SpecialError) == 1);
        // The record ends after the cycles (the next income evaluation after siphon_until).
        w.run(2 * period + 10);
        (void)s.planet_income(rA, false);
        CHECK(w.planet(rA).siphon_player == kNoPlayer && w.planet(rA).siphon_type == kNoIndex);
    }
    {
        World w;  // faction pairing: an Underworld smuggler on a Rebel planet passes; vigilance kills a generic hero
        GalacticSim& s = w.sim;
        const ObjectHandle sm = s.spawn_unit(pUnd, rE, tSmuggler);  // E: Planet_Vigilance
        CHECK(!s.ground_activated_trigger(sm, rE) && !w.store.alive(sm) && w.count(GalacticEventKind::SiphonVigilance) == 1);
        CHECK(w.planet(rE).siphon_player == kNoPlayer && s.respawns_pending(rE).size() == 1);
        const ObjectHandle sm2 = s.spawn_unit(pUnd, rB, tSmuggler);
        CHECK(s.ground_activated_trigger(sm2, rB) && w.planet(rB).siphon_player == pUnd && w.planet(rB).siphon_fraction == 0.5f);
    }
}

// ---- 4. / 5. planet owner bonuses ----------------------------------------------------------------------------------------

void test_planet_bonuses() {
    World w;
    GalacticSim& s = w.sim;
    w.run(2);  // planet services apply the bonuses to the owners
    CHECK(w.planet(rC).bonus_holder == pUnd && w.planet(rD).bonus_holder == pEmp);
    CHECK(s.player_upgrades(pUnd, true) == std::vector<uint32_t>{tSpaceUp} && s.player_upgrades(pUnd, false) == std::vector<uint32_t>{tLandUp});
    CHECK(s.player_upgrades(pEmp, true).empty());
    CHECK(s.players()[pEmp].tech_cost_mult == 1.0f - 0.15f && s.players()[pUnd].tech_cost_mult == 1.0f);
    // Owner change: C to the Empire moves the upgrades; D to the Rebels moves the tech price.
    CHECK(s.change_owner(w.planet(rC).object, pEmp) && s.change_owner(w.planet(rD).object, pReb));
    CHECK(w.planet(rC).bonus_holder == pEmp && s.player_upgrades(pEmp, true) == std::vector<uint32_t>{tSpaceUp} &&
          s.player_upgrades(pUnd, true).empty());
    CHECK(s.players()[pEmp].tech_cost_mult == 1.0f && s.players()[pReb].tech_cost_mult == 1.0f - 0.15f);
    // A destroyed planet gives no bonus: D's holder loses the tech price.
    CHECK(s.super_weapon_destroy_planet(rD, pEmp, false) && w.planet(rD).bonus_holder == kNoPlayer &&
          s.players()[pReb].tech_cost_mult == 1.0f);
    {
        World w2;  // the first apply needs corruption != Intimidation (type 0)
        w2.sim.set_corruption(rC, 0, pUnd);
        w2.run(2);
        CHECK(w2.planet(rC).bonus_holder == kNoPlayer && w2.sim.player_upgrades(pUnd, true).empty());
        w2.sim.set_corruption(rC, 3, pUnd);
        w2.run(1);
        CHECK(w2.planet(rC).bonus_holder == pUnd);
    }
}

// ---- 6. Political_Transition_Bonus_Ability -------------------------------------------------------------------------------

void test_political_transition() {
    World w;
    GalacticSim& s = w.sim;
    CHECK(s.political_transition_factor(rA) == 1.0f);
    const ObjectHandle tarkin = s.spawn_unit(pEmp, rA, tTarkin);
    CHECK(s.political_transition_factor(rA) == 0.25f && s.political_transition_factor(rB) == 1.0f);
    const ObjectHandle t2 = s.spawn_unit(pReb, rA, tTarkin);  // another player's holder does not count
    CHECK(s.political_transition_factor(rA) == 0.25f);
    // Political_Control_Protection: only the owner's holder protects.
    CHECK(s.political_control_protected(rA) && !s.political_control_protected(rB));
    s.destroy_object(tarkin);
    CHECK(!s.political_control_protected(rA));
    s.destroy_object(t2);
    CHECK(s.political_transition_factor(rA) == 1.0f);
}

// ---- 7. Neutralize_Hero_Ability / Hero_Protection_Ability -----------------------------------------------------------------

const NeutralizeCandidate* find_candidate(const std::vector<NeutralizeCandidate>& c, uint32_t type, bool siphon = false) {
    for (const NeutralizeCandidate& x : c)
        if (x.type == type && x.siphon == siphon) return &x;
    return nullptr;
}

float term(const NeutralizeCandidate& c, NeutralizeTerm t) {
    for (const auto& e : c.terms)
        if (e.first == t) return e.second;
    return 0.0f;
}

void test_neutralize() {
    {
        World w;  // AI Boba Fett (Empire) at the Rebel planet B: Han on the surface is the one candidate
        GalacticSim& s = w.sim;
        const ObjectHandle han = s.spawn_unit(pReb, rB, tHan);
        const ObjectHandle boba = s.spawn_unit(pEmp, rB, tBoba);
        CHECK(s.neutralize_accepts(rB));
        SimRng expect = w.rng;
        const float owner_t = expect.range_float(120.0f, 150.0f);   // per candidate: owner, then target
        const float target_t = expect.range_float(240.0f, 300.0f);
        (void)expect.range_float(0.0f, 1.0f);                       // the AI pick over one weight
        const int32_t f0 = s.frame();
        const float credits0 = s.credits(pEmp);
        CHECK(s.ground_activated_trigger(boba, rB));
        CHECK(w.rng == expect);
        CHECK(!w.store.alive(han) && !w.store.alive(boba));
        CHECK(s.credits(pEmp) == credits0 - 2500.0f);  // base 2500 only: no bases, protection, prior or corruption
        CHECK(s.neutralized_count(pEmp, tHan) == 1 && s.neutralized_count(pReb, tHan) == 0);
        CHECK(w.count(GalacticEventKind::HeroNeutralized) == 1 && w.count(GalacticEventKind::SpecialApplied) == 1);
        const auto pending = s.respawns_pending(rB);
        CHECK(pending.size() == 2);
        bool han_due = false, boba_due = false;
        for (const RespawnEntry& e : pending) {
            han_due = han_due || (e.type == tHan && e.due == f0 + static_cast<int32_t>(30.0f * target_t + 0.5f));
            boba_due = boba_due || (e.type == tBoba && e.due == f0 + static_cast<int32_t>(30.0f * owner_t + 0.5f));
        }
        CHECK(han_due && boba_due);

        // Second visit: B corrupted, Jabba (2.0 protection, allied with the planet's owner) on the surface, one prior
        // neutralization of Han by the Empire. Lines: base 2500, protection (2 - 1) x 2500, prior 0.75 x 2500 (curve
        // 0..1000 -> 0..750 at n = 1), corruption -0.25 x 2500; price 6250.
        s.set_corruption(rB, 1, pUnd);
        s.spawn_unit(pReb, rB, tJabba);
        s.spawn_unit(pReb, rB, tHan);
        const ObjectHandle boba2 = s.spawn_unit(pEmp, rB, tBoba);
        const std::vector<NeutralizeCandidate> c = s.neutralize_candidates(w.rules.special.galab[gBobaNeut], boba2, pEmp, rB);
        const NeutralizeCandidate* h = find_candidate(c, tHan);
        CHECK(c.size() == 1 && h && h->eligible && !h->space);  // Jabba has no Neutralization_Cost: no candidate
        const auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };  // the curve step is not exact
        CHECK(h && h->terms.size() == 4 && term(*h, NeutralizeTerm::Base) == 2500.0f &&
              term(*h, NeutralizeTerm::HeroProtection) == 2500.0f && near(term(*h, NeutralizeTerm::PriorNeutralizations), 1875.0f) &&
              term(*h, NeutralizeTerm::PlanetaryCorruption) == -625.0f && near(h->price(), 6250.0f));
        CHECK(h && h->terms[0].first == NeutralizeTerm::Base && h->terms[1].first == NeutralizeTerm::HeroProtection &&
              h->terms[2].first == NeutralizeTerm::PriorNeutralizations &&
              h->terms[3].first == NeutralizeTerm::PlanetaryCorruption);  // panel order
        CHECK(h && h->owner_respawn >= 120.0f && h->owner_respawn <= 150.0f && h->target_respawn >= 240.0f &&
              h->target_respawn <= 300.0f);
        // The AI cannot pay 6250: nothing happens (no pick draw).
        const float credits1 = s.credits(pEmp);
        SimRng expect2 = w.rng;
        (void)expect2.range_float(120.0f, 150.0f);
        (void)expect2.range_float(240.0f, 300.0f);
        CHECK(!s.ground_activated_trigger(boba2, rB) && w.store.alive(boba2) && s.credits(pEmp) == credits1 && w.rng == expect2);
        // Base levels: a Rebel space hero in B's orbit under a Rebel starbase of level 3 -> curve (0,0)-(5,1) at 3 = 0.6.
        CHECK(s.replace_starbase(rB, tStarbase) || static_cast<bool>(s.spawn_unit(pReb, rB, tStarbase)));
        CHECK(w.planet(rB).starbase_level == 3);
        s.spawn_unit(pReb, rB, tAce);
        s.spawn_unit(pEmp, rB, tFrigate);  // an Empire fleet in orbit sees the fleet contents (a surface unit does not)
        CHECK(s.can_see_planet_info(pEmp, rB, GalacticSim::PlanetInfo::FleetContents));
        const std::vector<NeutralizeCandidate> c2 = s.neutralize_candidates(w.rules.special.galab[gBobaNeut], boba2, pEmp, rB);
        const NeutralizeCandidate* ace = find_candidate(c2, tAce);
        CHECK(ace && ace->space && std::fabs(term(*ace, NeutralizeTerm::BaseLevels) - 600.0f) < 0.01f);
        // Protection does not cover Jabba's own entry, and a reducing holder (< 1) needs the holder NOT allied.
        CHECK(s.hero_protection(rB, {}) == 2.0f && s.hero_protection(rA, {}) == 1.0f);
    }
    {
        World w;  // the siphon entry: an Underworld smuggler siphons B; the Empire's Boba Fett removes it
        GalacticSim& s = w.sim;
        const ObjectHandle sm = s.spawn_unit(pUnd, rB, tSmuggler);
        CHECK(s.ground_activated_trigger(sm, rB) && w.planet(rB).siphon_player == pUnd);
        s.destroy_object(sm);  // the smuggler itself would be a candidate too: only the siphon entry is left
        const ObjectHandle boba = s.spawn_unit(pEmp, rB, tBoba);
        const float credits0 = s.credits(pEmp);
        CHECK(s.ground_activated_trigger(boba, rB));
        CHECK(w.planet(rB).siphon_player == kNoPlayer && w.planet(rB).siphon_type == kNoIndex);
        CHECK(s.credits(pEmp) == credits0 - 100.0f && s.neutralized_count(pEmp, tSmuggler) == 1 && !w.store.alive(boba));
    }
    {
        World w;  // eligibility: a generic bounty hunter cannot take Han (major); vigilance kills a generic neutralizer
        GalacticSim& s = w.sim;
        s.spawn_unit(pReb, rB, tHan);
        const ObjectHandle bh = s.spawn_unit(pEmp, rB, tHunter);
        const std::vector<NeutralizeCandidate> c = s.neutralize_candidates(w.rules.special.galab[gHunterNeut], bh, pEmp, rB);
        CHECK(c.size() == 1 && !c[0].eligible);
        CHECK(!s.ground_activated_trigger(bh, rB) && w.store.alive(bh) && s.neutralized_count(pEmp, tHan) == 0);
        const ObjectHandle mark = s.spawn_unit(pReb, rE, tSmuggler);  // E: Planet_Vigilance
        const ObjectHandle bh2 = s.spawn_unit(pEmp, rE, tHunter);
        CHECK(!s.ground_activated_trigger(bh2, rE) && !w.store.alive(bh2) && w.store.alive(mark));
        CHECK(w.count(GalacticEventKind::NeutralizeVigilance) == 1 && s.neutralized_count(pEmp, tSmuggler) == 0);
        // No candidate at all (A is the Empire's own): the error SFX.
        const ObjectHandle boba = s.spawn_unit(pEmp, rA, tBoba);
        const size_t errors = w.count(GalacticEventKind::SpecialError);
        CHECK(!s.ground_activated_trigger(boba, rA) && w.count(GalacticEventKind::SpecialError) == errors + 1);
        // A Death Star in B's orbit: the accept fails.
        s.spawn_unit(pEmp, rB, tDeathStar);
        CHECK(!s.neutralize_accepts(rB) && s.neutralize_accepts(rE));
    }
    {
        World w;  // kind 3: the payload round trip and the executor through neutralize_execute (a human's pick)
        GalacticSim& s = w.sim;
        const ObjectHandle han = s.spawn_unit(pReb, rB, tHan);
        const ObjectHandle boba = s.spawn_unit(pEmp, rB, tBoba);
        NeutralizeCandidate c;
        c.type = tHan;
        c.object = static_cast<int32_t>(w.store.id(han));
        c.eligible = true;
        c.owner_respawn = 130.0f;
        c.target_respawn = 250.0f;
        c.terms.emplace_back(NeutralizeTerm::Base, 2500.0f);
        SpecialPayload pl;
        CHECK(read_special_payload(neutralize_hero_event(7, 9, c), pl) && pl.kind == kSpecialNeutralize && pl.planet == 7 &&
              pl.unit == 9 && pl.structure == static_cast<uint32_t>(c.object) && pl.type == tHan &&
              pl.amount == commands::requantize(kQuantAmount, 2500.0f) &&
              pl.seconds == commands::requantize(kQuantDuration, 130.0f) &&
              pl.target_seconds == commands::requantize(kQuantDuration, 250.0f) && pl.eligible && !pl.space && !pl.siphon);
        CHECK(s.neutralize_execute(pEmp, rA, boba, c) == RejectReason::InvalidTarget);  // not there
        NeutralizeCandidate bad = c;
        bad.type = 0xFFFFu;  // not a type: refused before anything is counted or charged
        CHECK(s.neutralize_execute(pEmp, rB, boba, bad) == RejectReason::Malformed && w.store.alive(han));
        NeutralizeCandidate wrong = c;
        wrong.type = tSmuggler;  // a valid type that is not the target's: the target's real type is counted
        CHECK(s.neutralize_execute(pEmp, rB, boba, wrong) == RejectReason::None && !w.store.alive(han) && !w.store.alive(boba));
        CHECK(s.neutralized_count(pEmp, tHan) == 1 && s.neutralized_count(pEmp, tSmuggler) == 0);
        // A siphon pick needs the siphon it was made for: a Rebel siphon (own player) or another type is refused.
        NeutralizeCandidate sip;
        sip.type = tSmuggler;
        sip.siphon = true;
        sip.eligible = true;
        sip.terms.emplace_back(NeutralizeTerm::Base, 100.0f);
        const ObjectHandle hunter = s.spawn_unit(pReb, rA, tHunter);
        s.set_siphon(rA, pReb, 0.5f, s.frame() + 1000);
        CHECK(s.neutralize_execute(pReb, rA, hunter, sip) == RejectReason::NotAllowed && w.planet(rA).siphon_player == pReb);
        // Save v8 round trip with galab state: the neutralized counts, the planet bonus holders, a siphon record.
        w.run(2);
        const ObjectHandle sm = s.spawn_unit(pUnd, rB, tSmuggler);
        CHECK(s.ground_activated_trigger(sm, rB) && w.planet(rB).siphon_type == tSmuggler && w.planet(rC).bonus_holder == pUnd);
        ByteWriter out;
        s.write(out);
        World w2;
        w2.store = w.store;
        w2.ids.set_next(w.ids.next());
        ByteReader in(out.bytes());
        CHECK(w2.sim.read(in));
        CHECK(w2.sim.neutralized_count(pEmp, tHan) == 1 && w2.planet(rB).siphon_type == tSmuggler &&
              w2.planet(rB).siphon_source == w.planet(rB).siphon_source && w2.planet(rC).bonus_holder == pUnd &&
              w2.sim.home_planet(pEmp) == rA);
        ByteWriter again;
        w2.sim.write(again);
        CHECK(again.bytes() == out.bytes());
    }
}

// ---- 8. FoC data ---------------------------------------------------------------------------------------------------------

std::string env_or_empty(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

void test_foc() {
    const std::string root = env_or_empty("ALAMO_FOC_DATA");
    std::string how;
    std::unique_ptr<FileProvider> provider = testing::open_foc_data(root, how);
    if (!provider) {
        std::printf("ALAMO_FOC_DATA unset or without Data\\XML: FoC GALAB data checks skipped\n");
        return;
    }
    std::vector<std::string> diag;
    data::GameDatabase db;
    objects::ObjectTypeDatabase raw;
    objects::GameObjectTypes types;
    GalacticRules rules;
    CHECK(db.load(*provider, {}, &diag));
    CHECK(raw.load(*provider, &diag));
    CHECK(types.build(raw, provider.get(), &diag));
    CHECK(load_galactic_rules(*provider, db, types, rules, &diag));
    const SpecialRules& s = rules.special;
    const auto first = [&](const char* type, SpecialAbilityClass cls) -> const SpecialAbilityDef* {
        const uint32_t t = rules.find_type(type);
        if (t == kNoIndex) return nullptr;
        for (uint32_t g : s.type(t).galab)
            if (s.galab[g].cls == cls) return &s.galab[g];
        return nullptr;
    };
    const SpecialAbilityDef* emperor = first("Emperor_Palpatine", SpecialAbilityClass::RemoveCorruption);
    CHECK(emperor && emperor->removal_cost_multiplier == 1.0f && emperor->removal_seconds == 3.0f &&
          emperor->style == ActivationStyle::GroundActivated && !emperor->causes_despawn);
    const SpecialAbilityDef* ds = first("Death_Star", SpecialAbilityClass::PlanetDestruction);
    CHECK(ds && ds->style == ActivationStyle::GroundActivated);
    const SpecialAbilityDef* bespin = first("Bespin", SpecialAbilityClass::GalaxyWideUpgrade);
    CHECK(bespin && bespin->space_upgrade == rules.find_type("Bespin_Krayt_Damage_Upgrade") && bespin->land_upgrade == kNoIndex &&
          bespin->style == ActivationStyle::GalacticAutomatic);
    const SpecialAbilityDef* maw = first("The_Maw", SpecialAbilityClass::ReduceTechPrice);
    CHECK(maw && maw->production_reduction == 0.15f);
    size_t upgrades = 0;
    for (const SpecialAbilityDef& a : s.galab) upgrades += a.cls == SpecialAbilityClass::GalaxyWideUpgrade ? 1u : 0u;
    CHECK(upgrades >= 15);
    const uint32_t bespin_t = rules.find_type("Bespin");
    CHECK(bespin_t != kNoIndex && s.type(bespin_t).corruption_removal_cost > 0.0f);
    const SpecialAbilityDef* boba = first("Boba_Fett", SpecialAbilityClass::NeutralizeHero);
    CHECK(boba && boba->style == ActivationStyle::GroundActivated && boba->owner_respawn_min == 120.0f &&
          boba->owner_respawn_max == 150.0f && boba->target_respawn_min == 240.0f && boba->target_respawn_max == 300.0f &&
          boba->can_neutralize_minor && boba->can_neutralize_major &&
          std::fabs(boba->cost_by_neutralizations.eval(1.0f) - 0.75f) < 1e-5f &&
          boba->cost_by_corruption.eval(1.0f) == 0.25f && boba->cost_by_base_level.eval(5.0f) == 1.0f);
    const SpecialAbilityDef* jabba = first("Jabba_The_Hutt", SpecialAbilityClass::HeroProtection);
    CHECK(jabba && jabba->protection_multiplier == 2.0f && jabba->stacking_category == 0);
    size_t targets = 0;
    for (const TypeSpecial& t : s.types) targets += t.neutralization_cost > 0.0f ? 1u : 0u;
    CHECK(targets >= 20);
    std::printf("  FoC GALAB: %zu entries, %zu Galaxy_Wide_Upgrade, %zu neutralization targets\n", s.galab.size(), upgrades,
                targets);
}

}  // namespace

int main() {
    test_remove_corruption();
    test_death_star();
    test_siphon();
    test_planet_bonuses();
    test_political_transition();
    test_neutralize();
    test_foc();
    if (g_fail) {
        std::printf("%d of %d check(s) failed\n", g_fail, g_checks);
        return 1;
    }
    std::printf("alamo_galactic_galab_tests: all %d checks passed\n", g_checks);
    return 0;
}
