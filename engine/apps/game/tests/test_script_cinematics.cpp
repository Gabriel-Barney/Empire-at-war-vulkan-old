// mcx6 t05: the story script's cinematic object calls, host side (script_cinematics.hpp), data-free. Expected values from
// docs/re/MCX6_T05_CINEMATIC_BINDINGS.md (0x5c9a80 timer 25 - delay, shown at 35, sound at 45; the ModelEmitterType table
// 0x2641c0). Do_End_Cinematic_Cleanup's object set is the sim binding's (engine/lua tests, test_lua_presentation.cpp).
#include <cstdio>
#include <vector>

#include "alamo/game/script_cinematics.hpp"
#include "alamo/test/support.hpp"

using namespace alamo::game;
using alamo::objects::ObjectId;

namespace {

void arrival_test() {
    alamo::test::section("Cinematic_Hyperspace_In: hidden delay + 10 frames, arrival sound at delay + 20");
    ScriptCinematics s;
    s.hyperspace_in(7, 30, 100);  // STORY_EMPIRE_ACTII_M04_SPACE moncal_01.Cinematic_Hyperspace_In(30)
    CHECK(s.hidden(7, 100) && s.hidden(7, 139) && !s.hidden(7, 140) && !s.hidden(8, 100));
    std::vector<ObjectId> due;
    s.take_arrival_sfx(149, due);
    CHECK(due.empty());
    s.take_arrival_sfx(150, due);
    CHECK(due == std::vector<ObjectId>{7});
    s.take_arrival_sfx(151, due);  // once
    CHECK(due.size() == 1 && s.arrivals() == 0);
    // A second call restarts the arrival; a negative delay counts as 0.
    s.hyperspace_in(9, 10, 0);
    s.hyperspace_in(9, -5, 20);
    CHECK(s.arrivals() == 1 && s.hidden(9, 29) && !s.hidden(9, 30));
    due.clear();
    s.take_arrival_sfx(39, due);
    CHECK(due.empty());
    s.take_arrival_sfx(40, due);
    CHECK(due == std::vector<ObjectId>{9});
    s.reset();
    CHECK(s.arrivals() == 0 && !s.hidden(9, 0));
}

void emitter_test() {
    alamo::test::section("Show_Emitter: ModelEmitterType names -> particle prefix groups");
    CHECK(model_emitter_type("Engines") == 0 && model_emitter_type("TURBO_ENGINES") == 1 && model_emitter_type("ion_stun") == 2);
    CHECK(model_emitter_type("Sprint_Ability_Effect") == 4 && model_emitter_type("Power_To_Weapons_Effect") == 5 &&
          model_emitter_type("Missile_Shield") == 7);
    CHECK(model_emitter_type("Gravity_Well") == -1 && model_emitter_type("") == -1);  // groups 3 and 6 have no name
    const std::map<std::string, bool> state = {
        {"12|ENGINES", false}, {"12|ION_STUN", true}, {"13|MISSILE_SHIELD", true}, {"13|NOPE", true}, {"x|ENGINES", false}};
    const auto o = emitter_overrides(state);
    CHECK(o.size() == 2);
    CHECK(o.count(12) && o.at(12) == (EmitterGroups{0x04, 0x01}));
    CHECK(o.count(13) && o.at(13) == (EmitterGroups{0x80, 0x00}));
}

}  // namespace

int main() {
    arrival_test();
    emitter_test();
    return alamo::test::report("script_cinematics");
}
