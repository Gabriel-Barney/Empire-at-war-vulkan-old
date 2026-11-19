// ref2 t04 (REF2 test seam, serves aitac t03 "Lua spawns adopted"): the battle's adoption invariant after each tick of the
// battle fixtures (run_campaign, the AI fixture's tick()). TacticalBattle::adoption_violations is a const query outside the
// CRC: it never changes the battle, so the goldens do not see it. A breach fails the exe; the first few are printed.
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "alamo/sim/tactical_battle.hpp"
#include "alamo/test/support.hpp"

namespace battle_fixture {

inline uint64_t g_adoption_checks = 0;    // ticks checked (all exes' fixtures)
inline uint64_t g_adoption_breaches = 0;  // ticks with a breach
// Objects a test makes outside the battle on purpose (a bare store object standing in for a story marker); cleared when
// the AI fixture's next session starts (ClashSession).
inline std::vector<alamo::objects::ObjectId> g_adoption_exempt;

inline void check_adoption(const alamo::sim::TacticalBattle& b, const alamo::sim::TacticalMode& tm) {
    ++g_adoption_checks;
    std::vector<std::string> v = b.adoption_violations(tm);
    std::erase_if(v, [](const std::string& line) {  // each line starts with the object id
        const auto id = static_cast<alamo::objects::ObjectId>(std::strtol(line.c_str(), nullptr, 10));
        return std::find(g_adoption_exempt.begin(), g_adoption_exempt.end(), id) != g_adoption_exempt.end();
    });
    if (v.empty()) return;
    if (++g_adoption_breaches <= 5)
        std::printf("  adoption invariant broken at battle frame %u (%zu): %s\n", b.frame(), v.size(), v.front().c_str());
    CHECK(v.empty());
}

}  // namespace battle_fixture
