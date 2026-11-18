// mcx6 t05: the story script's cinematic object calls, host side (script_cinematics.hpp), data-free. Expected values from
// docs/re/MCX6_T05_CINEMATIC_BINDINGS.md (0x5c9a80 timer 25 - delay, shown at 35, sound at 45; 0x541e20 / 0x38ed10
// object set; the ModelEmitterType table 0x2641c0).
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

void cleanup_test() {
    alamo::test::section("Do_End_Cinematic_Cleanup: locomotor objects not neutral and not In_End_Cinematic are hidden");
    const std::vector<CleanupCandidate> c = {
        {1, 0, true},   // player 0 unit: hidden
        {2, 0, true},   // marked In_End_Cinematic(true): kept
        {3, 5, true},   // the neutral player's: kept
        {4, 1, false},  // no locomotor (a structure, a marker): kept
        {5, 1, true},   // player 1 unit: hidden
    };
    const auto ids = end_cinematic_cleanup_set(c, 5, [](ObjectId id) { return id == 2; });
    CHECK(ids == std::vector<ObjectId>({1, 5}));
    ScriptCinematics s;
    s.end_cinematic_cleanup(ids);
    CHECK(s.hidden(1, 0) && s.hidden(5, 1000000) && !s.hidden(2, 0) && !s.hidden(3, 0) && s.cleaned() == 2);
    CHECK(end_cinematic_cleanup_set(c, 5, {}).size() == 3);  // no marks: object 2 goes too
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
    cleanup_test();
    emitter_test();
    return alamo::test::report("script_cinematics");
}
