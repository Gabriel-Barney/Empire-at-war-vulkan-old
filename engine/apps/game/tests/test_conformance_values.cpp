// alamo_game_conformance_values_tests (mcx1 t07): the conformance mod `values` (testdata/conformance/values, README there)
// loaded through the real MODPATH= route with no game data. Every expectation is retail's rule, cited from
// docs/re/MCX1_RE_ANSWERS.md (Q1 per-child dispatch, Q2 bools, Q3 overflow, Q4 several roots, Q5 UTF-16, Q7 element tags)
// or docs/re/OBJECT_TYPES.md (§4.4 repeated tags, §4.5 converters). Read only: nothing is written.
//   alamo_game_conformance_values_tests <testdata/conformance/values>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "alamo/core/ascii.hpp"
#include "alamo/data/xml.hpp"
#include "alamo/galactic/campaign.hpp"
#include "alamo/galactic/galactic_rules.hpp"
#include "alamo/game/game_data.hpp"
#include "alamo/tactical/abilities/ability_data.hpp"
#include "alamo/test/support.hpp"

using ::alamo::test::g_fail;

namespace {

namespace galactic = alamo::galactic;

bool any_diag(const alamo::game::GameData& d, const std::string& needle) {
    for (const std::string& m : d.diagnostics)
        if (m.find(needle) != std::string::npos) return true;
    return false;
}

const galactic::FactionRules* faction(const alamo::game::GameData& d, const char* name) {
    const uint32_t f = d.rules.find_faction(name);
    return f == galactic::kNoIndex ? nullptr : &d.rules.factions[f];
}

galactic::UnitKind kind(const alamo::game::GameData& d, const char* name) {
    const uint32_t t = d.rules.find_type(name);
    return t == galactic::kNoIndex ? static_cast<galactic::UnitKind>(0xFF) : d.rules.types[t].kind;
}

// GameConstants (Q1: every element, nested ones descended into; Q2 bools; §4.5 atoi / atof; Q3 clamp).
void test_constants(const alamo::game::GameData& d) {
    const alamo::data::GameConstants& c = d.db.constants();
    CHECK(!c.get_bool("AIUsesFogOfWarGalactic", true));               // "On": not "1" and no first Y / T -> false
    CHECK(c.get_bool("AI_Uses_Galactic_Pop_Cap", false));              // "Yes please": first character Y -> true
    CHECK(c.get_int("MP_Default_Credits", 0) == 2500);                 // repeated: the last, atoi "2500abc"
    CHECK(c.get_float("Production_Speed_Factor", 0.0f) == 2.5f);       // nested under <Production_Settings>, atof "2.5f"
    CHECK(!c.get("Planet_Reveal_Delay_Time"));                         // TBD: skipped, the field keeps its default
    CHECK(c.get_int("Max_Ground_Forces_On_Planet", 0) == std::numeric_limits<int32_t>::max());  // Q3: UCRT atoi clamps
    CHECK(c.get_float("Health_Low_Percent_Threshold", 9.0f) == 0.0f);  // atof of no number: 0
    CHECK(c.max_fleet_move_distance == 4.5f);                          // atof "4.5 parsecs"; the empty repeat changes nothing
}

// Factions (Q1: last wins, TBD skipped, lists append, nested fields; any top-level element is a faction).
void test_factions(const alamo::game::GameData& d) {
    const galactic::FactionRules* r = faction(d, "Values_Rebels");
    CHECK(r != nullptr);
    if (r) {
        CHECK(r->is_playable);                       // "No" then "True": the last
        CHECK(!r->is_neutral);                       // "On" is false (Q2)
        CHECK(r->hyperspace_speed_factor == 2.0f);   // the later TBD changes nothing
        CHECK(r->allies.size() == 2);                // code 0x2a: both occurrences appended
        CHECK(r->maintenance_cost == 0.5f);          // nested under <Economy>
        CHECK(r->displayed_tech_level_adjustment == 1);  // code 0x6: atoi "+1x"
        CHECK(r->basic_ai == "Second_AI" && r->xml_text("Basic_AI") == "Second_AI");
    }
    const galactic::FactionRules* p = faction(d, "Values_Pirates");  // written as <Pirate_Faction>
    CHECK(p != nullptr && p->is_playable);
}

// Campaigns (Q1: any entry tag; lists append; atoi; Q2 bool).
void test_campaigns(const alamo::game::GameData& d) {
    const alamo::data::CampaignDef* c = d.db.find_campaign("Values_GC");  // written as <Values_Conquest>
    CHECK(c != nullptr);
    if (!c) return;
    CHECK(c->locations.size() == 2);
    CHECK(c->sort_order == 7);  // "5", then "7x": the last, atoi
    std::vector<std::string> diag;
    const galactic::GalacticSetup s = galactic::build_campaign_setup(d.db, d.rules, "Values_GC", {}, &diag);
    CHECK(!s.planet_auto_reveal);  // "On" is false
    CHECK(s.forces.size() == 2);
}

// Object types (Q7: element tags carry no behaviour; §4.4 last wins; abilities: Q1 kAbility rule).
void test_objects(const alamo::game::GameData& d) {
    CHECK(kind(d, "Values_Gamma") == galactic::UnitKind::Planet);           // <Moon> with PLANET in GalacticBehavior
    CHECK(kind(d, "Values_Not_A_Planet") == galactic::UnitKind::Other);     // <Planet> without PLANET
    CHECK(kind(d, "Values_Cruiser") == galactic::UnitKind::SpaceUnit);      // <Custom_Ship> with DUMMY_STARSHIP
    CHECK(kind(d, "Values_Plain_Ship") == galactic::UnitKind::Other);       // <SpaceUnit> with no DUMMY_* id: no role
    const alamo::data::PlanetTypes& pl = d.db.planets();
    const uint32_t g = pl.find("Values_Gamma");
    CHECK(g != alamo::kInvalidPlanet);
    if (g != alamo::kInvalidPlanet) {
        CHECK((pl.flags[g] & alamo::kPlanetHasPlanetBehavior) != 0);
        CHECK((pl.flags[g] & alamo::kPlanetInBackground) == 0);  // "On" is false
        CHECK(pl.credit_value[g] == 25);                         // repeated: the last
    }
    const uint32_t np = pl.find("Values_Not_A_Planet");
    CHECK(np == alamo::kInvalidPlanet || (pl.flags[np] & alamo::kPlanetHasPlanetBehavior) == 0);
    // The ability sub-object reads direct children only and converts TBD like any value (a TBD bool is true).
    const alamo::data::GameObjectRecord* cruiser = d.db.find_game_object("Values_Cruiser");
    CHECK(cruiser != nullptr);
    if (cruiser) {
        const alamo::data::XmlNode ua = cruiser->element.child("Unit_Abilities_Data").child("Unit_Ability");
        alamo::tactical::abilities::UnitAbilityRecord rec;
        CHECK(alamo::tactical::abilities::parse_unit_ability(ua, rec, {}));
        CHECK(rec.active_by_default);           // "No", then "TBD": the last, 'T' -> true
        CHECK(rec.recharge_seconds == 9.0f);    // repeated: the last
        CHECK(rec.expiration_seconds == 0.0f);  // nested under <Nested>: not read by an ability
    }
}

// Files (Q4: a second root is reported, the first read; Q5: UTF-16 refused).
void test_files(const alamo::game::GameData& d) {
    CHECK(d.rules.find_type("Values_First_Root_Ship") != galactic::kNoIndex);
    CHECK(d.rules.find_type("Values_Second_Root_Ship") == galactic::kNoIndex);
    CHECK(d.rules.find_type("Values_Utf16_Ship") == galactic::kNoIndex);
    CHECK(any_diag(d, "xml_extra_root"));
    CHECK(any_diag(d, "UTF-16"));
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::fprintf(stderr, "usage: alamo_game_conformance_values_tests <testdata/conformance/values>\n");
        return 2;
    }
    const std::string foc = std::string(argv[1]) + "/corruption";
    alamo::game::GameData d;
    std::string err;
    CHECK(alamo::game::load_game_data(foc, {"MODPATH=Mods/Values"}, d, &err));
    if (!err.empty()) std::printf("load: %s\n", err.c_str());
    test_constants(d);
    test_factions(d);
    test_campaigns(d);
    test_objects(d);
    test_files(d);
    if (g_fail)
        for (const std::string& m : d.diagnostics) std::printf("  diag: %s\n", m.c_str());
    std::printf(g_fail ? "conformance values: %d check(s) FAILED\n" : "conformance values: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
