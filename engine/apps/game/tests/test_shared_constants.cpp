// alamo_game_shared_constants_tests (ref4 t06): every GameConstants tag that several modules read is read once
// (data::SharedGameConstants, GameConstants::shared) and a mod's value reaches every user. A temp mod over the synthetic mod
// fixture (testdata/mod_fixture) replaces GameConstants.xml with a value for every shared tag that differs from FoC's;
// load_game_data loads it through MODPATH=, and each user is checked: galactic rules, conflict rules, the battle catalog
// (combat config, movement constants, locomotor blends, transition and fog constants), the tactical AI constants and the
// sim's skirmish option defaults. A tag the file does not set leaves each user's own default.
//   alamo_game_shared_constants_tests <testdata/mod_fixture>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "alamo/core/host_fs.hpp"
#include "alamo/game/game_data.hpp"
#include "alamo/sim/l1_loaders.hpp"  // skirmish_option_defaults
#include "alamo/tactical/movement/constants.hpp"
#include "alamo/test/support.hpp"
#ifdef ALAMO_TEST_HAVE_AI_TACTICAL
#include "alamo/ai/tactical/constants.hpp"
#endif

namespace fs = std::filesystem;

namespace {

// Every shared tag, each with a value FoC does not ship.
constexpr const char* kModConstants = R"(<GameConstants>
  <Damage_Types>Damage_Default</Damage_Types>
  <Health_Low_Percent_Threshold>0.45</Health_Low_Percent_Threshold>
  <MP_Default_Credits>7250</MP_Default_Credits>
  <Object_Max_Speed_Multiplier_Galactic>3.5</Object_Max_Speed_Multiplier_Galactic>
  <Object_Max_Speed_Multiplier_Space>1.75</Object_Max_Speed_Multiplier_Space>
  <Object_Max_Speed_Multiplier_Land>2.25</Object_Max_Speed_Multiplier_Land>
  <InfantryTurnBlendTime>0.375</InfantryTurnBlendTime>
  <Raid_Force_Max_Limited_Objects>4</Raid_Force_Max_Limited_Objects>
  <Raid_Force_Max_Heros>3</Raid_Force_Max_Heros>
  <DesiredLandFOWCellSize>20.0</DesiredLandFOWCellSize>
  <DesiredSpaceFOWCellSize>125.0</DesiredSpaceFOWCellSize>
  <AIUsesFogOfWarGalactic>True</AIUsesFogOfWarGalactic>
  <AIUsesFogOfWarSpace>True</AIUsesFogOfWarSpace>
  <AIUsesFogOfWarLand>True</AIUsesFogOfWarLand>
</GameConstants>
)";

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: alamo_game_shared_constants_tests <testdata/mod_fixture>\n");
        return 2;
    }
    const std::string foc = alamo::path_to_utf8(fs::path(alamo::path_from_utf8(argv[1])) / "corruption");
    const fs::path mod = fs::temp_directory_path() / "alamo_game_shared_constants_tests" / "ConstantsMod";
    std::error_code ec;
    fs::remove_all(mod.parent_path(), ec);
    CHECK(alamo::test::write_file(mod / "Data" / "XML" / "GameConstants.xml", kModConstants));

    alamo::test::section("the fixture without the mod: its GameConstants.xml has none of the shared tags");
    {
        alamo::game::GameData base;
        std::string err;
        CHECK(alamo::game::load_game_data(foc, {}, base, &err));
        const alamo::data::SharedGameConstants& s = base.db.constants().shared;
        CHECK(!s.health_low_percent_threshold && !s.mp_default_credits && !s.raid_force_max_heros && !s.ai_uses_fog_of_war_land);
        // the users keep their own defaults
        CHECK(base.crules.raid_max_limited == 0 && base.crules.raid_max_heroes == 0);
        CHECK(alamo::sim::skirmish_option_defaults(base.db.constants()).credits == 6000);
    }

    alamo::test::section("a mod's value for every shared tag reaches every user");
    alamo::game::GameData d;
    std::string err;
    const std::vector<std::string> args{"MODPATH=" + alamo::path_to_utf8(mod)};
    const bool loaded = alamo::game::load_game_data(foc, args, d, &err);
    std::printf("load: %s\n", loaded ? "ok" : err.c_str());
    CHECK(loaded);
    const alamo::data::GameConstants& gc = d.db.constants();
    if (loaded) {
        const alamo::data::SharedGameConstants& s = gc.shared;
        CHECK(s.health_low_percent_threshold == 0.45f && s.mp_default_credits == 7250);
        CHECK(s.object_max_speed_multiplier_galactic == 3.5f && s.object_max_speed_multiplier_space == 1.75f &&
              s.object_max_speed_multiplier_land == 2.25f);
        CHECK(s.infantry_turn_blend_time == 0.375f && s.raid_force_max_limited_objects == 4 && s.raid_force_max_heros == 3);
        CHECK(s.desired_land_fow_cell_size == 20.0f && s.desired_space_fow_cell_size == 125.0f);
        CHECK(s.ai_uses_fog_of_war_galactic == true && s.ai_uses_fog_of_war_space == true && s.ai_uses_fog_of_war_land == true);

        // galactic: rules constants and conflict rules
        CHECK(d.rules.constants.health_low_percent_threshold == 0.45f);
        CHECK(d.rules.constants.fleet_speed_multiplier == 3.5f);
        CHECK(d.crules.raid_max_limited == 4 && d.crules.raid_max_heroes == 3);
        // the battle catalog: combat, movement, locomotor blends, transition and fog constants
        CHECK(d.catalog != nullptr);
        if (d.catalog) {
            const alamo::sim::BattleCatalog& c = *d.catalog;
            CHECK(c.combat_config().low_health_threshold == 0.45f);
            CHECK(c.combat_config().turn_multiplier_space == 1.75f && c.combat_config().turn_multiplier_land == 2.25f);
            CHECK(c.movement_constants().space_speed_multiplier == 1.75f && c.movement_constants().land_speed_multiplier == 2.25f);
            CHECK(c.movement_constants().infantry_turn_blend_time == 0.375f && c.locomotor_blends().infantry_turn == 0.375f);
            CHECK(c.transition_constants().raid_max_limited == 4 && c.transition_constants().raid_max_heroes == 3);
            CHECK(c.fog_constants().desired_land_cell_size == 20.0f && c.fog_constants().desired_space_cell_size == 125.0f);
            CHECK(c.fog_constants().ai_uses_fog_land && c.fog_constants().ai_uses_fog_space);
        }
#ifdef ALAMO_TEST_HAVE_AI_TACTICAL
        alamo::ai::tactical::TacticalAiConstants t;
        t.load(gc);
        CHECK(t.health_low_percent_threshold == 0.45f && t.desired_land_fow_cell_size == 20.0f && t.desired_space_fow_cell_size == 125.0f);
        CHECK(t.ai_uses_fow_space && t.ai_uses_fow_land);
#endif
        // the sim's skirmish options (alamo_game's option dialogs take theirs from it)
        CHECK(alamo::sim::skirmish_option_defaults(gc).credits == 7250);
    }

    alamo::test::section("no GameConstants.xml: no shared values, the users keep their defaults");
    {
        alamo::data::GameDatabase empty;
        CHECK(!empty.constants().shared.infantry_turn_blend_time && !empty.constants().shared.health_low_percent_threshold);
        alamo::tactical::movement::MovementConstants m = alamo::tactical::movement::MovementConstants::from_game_constants(empty.constants());
        CHECK(m.infantry_turn_blend_time == alamo::tactical::movement::MovementConstants{}.infantry_turn_blend_time);
    }
    fs::remove_all(mod.parent_path(), ec);
    return alamo::test::report("alamo_game_shared_constants_tests");
}
