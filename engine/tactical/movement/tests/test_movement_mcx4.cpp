// alamo_tactical_movement_mcx4_tests (no framework; non-zero exit on failure).
// mcx4 t11 (Stage 9, box MCX2): movement, formation and animation tags that base FoC sets and the port stored without
// using. Each test cites the docs/re spec its expected values come from.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/core/file_provider.hpp"
#include "alamo/data/game_database.hpp"
#include "alamo/tactical/movement/collision.hpp"
#include "alamo/tactical/movement/constants.hpp"
#include "alamo/tactical/movement/formation.hpp"
#include "alamo/tactical/movement/locomotor.hpp"
#include "alamo/tactical/movement/space_path.hpp"
#include "alamo/test/support.hpp"

using namespace alamo;
using namespace alamo::tactical::movement;

namespace {

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] + 32) : a[i];
        const char y = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] + 32) : b[i];
        if (x != y) return false;
    }
    return true;
}

struct OneFileProvider final : FileProvider {  // serves Data\XML\GameConstants.xml from memory
    std::string text;
    bool read(std::string_view path, std::vector<uint8_t>& out) const override {
        if (!exists(path)) return false;
        out.assign(text.begin(), text.end());
        return true;
    }
    bool exists(std::string_view path) const override { return iequal(path, "Data\\XML\\GameConstants.xml"); }
    std::string origin(std::string_view path) const override { return exists(path) ? "memory" : ""; }
};

MovementConstants load(const std::string& body) {
    OneFileProvider fp;
    fp.text = "<GameConstants>" + body + "</GameConstants>";
    data::GameDatabase db;
    std::vector<std::string> ignored;
    db.load(fp, {}, &ignored);  // only GameConstants exists: the other loaders report missing files
    return MovementConstants::from_game_constants(db.constants());
}

bool near(float a, float b) { return std::fabs(a - b) <= 1e-5f * (1.0f + std::fabs(b)); }

// ledger: verifies GameConstants.FormationMinimumSideError
// ledger: verifies GameConstants.FormationMaximumSideError
void test_formation_side_error() {
    // MOVEMENT §4.4 (0x41dbb0 [H]): a formation member's side error e pulls it back along the path normal by
    // (e -+ FormationMinimumSideError) / FormationMaximumSideError, 0 when |e| <= the minimum. FoC 0.1 / 30.
    const MovementConstants d;
    CHECK(near(d.formation_minimum_side_error, 0.1f) && near(d.formation_maximum_side_error, 30.0f));
    const MovementConstants k =
        load("<FormationMinimumSideError>0.5</FormationMinimumSideError><FormationMaximumSideError>10</FormationMaximumSideError>");
    CHECK(near(k.formation_minimum_side_error, 0.5f) && near(k.formation_maximum_side_error, 10.0f));
    CHECK(near(formation_side_correction(2.0f, k.formation_minimum_side_error, k.formation_maximum_side_error), 0.15f));
    CHECK(near(formation_side_correction(-2.0f, k.formation_minimum_side_error, k.formation_maximum_side_error), -0.15f));
    CHECK(formation_side_correction(0.4f, k.formation_minimum_side_error, k.formation_maximum_side_error) == 0.0f);
    // through the walk step: a unit 2 units left of its lane steers back harder with the modded constants
    const std::vector<V2> path = {{0.0f, 0.0f}, {100.0f, 0.0f}};
    WalkExtras x;
    x.lane = true;
    V2 vanilla, modded;
    CHECK(walk_wanted_dir(path, 1, V2{10.0f, 2.0f}, &x, vanilla));
    x.side_error_min = k.formation_minimum_side_error;
    x.side_error_max = k.formation_maximum_side_error;
    CHECK(walk_wanted_dir(path, 1, V2{10.0f, 2.0f}, &x, modded));
    CHECK(vanilla.y < 0.0f && modded.y < vanilla.y);  // toward the lane (y = 0), more with max 10 than with 30
}

// ledger: verifies GameConstants.ApproximationForwardDistance
void test_approximation_forward_distance() {
    // BKR1_RE_ANSWERS Q5 [H] (MOVEMENT §1.6, path approximation 0x505fe0): the approximation's straight steps are at most
    // ApproximationForwardDistance (FoC 2.0) long and at least 0.1; the moving-vs-stopped sweep (foot_path_contact,
    // 0x441b60) runs from approximation point to approximation point, so the spacing sets where the contact is found.
    const MovementConstants d;
    CHECK(near(d.approximation_forward_distance, 2.0f));
    const MovementConstants k = load("<ApproximationForwardDistance>25</ApproximationForwardDistance>");
    CHECK(near(k.approximation_forward_distance, 25.0f));
    const std::vector<V2> path = {{0.0f, 0.0f}, {200.0f, 0.0f}};
    const V2 ha{6.0f, 3.0f};
    const FootRect b = foot_rect({100.0f, 9.0f}, 45.0f, 8.0f, 4.0f);  // beside the line: the contact radius varies
    const float vanilla = foot_path_contact(path[0], path, 1, ha, b, 1000.0f);
    CHECK(vanilla >= 0.0f);
    CHECK(foot_path_contact(path[0], path, 1, ha, b, 1000.0f, kNoFacing, d.approximation_forward_distance) == vanilla);
    const float modded = foot_path_contact(path[0], path, 1, ha, b, 1000.0f, kNoFacing, k.approximation_forward_distance);
    std::printf("ApproximationForwardDistance: contact at %.4f (2.0) vs %.4f (25)\n", static_cast<double>(vanilla),
                static_cast<double>(modded));
    CHECK(modded != vanilla);
    // below the 0.1 floor: a 0 spacing samples as 0.1
    CHECK(foot_path_contact(path[0], path, 1, ha, b, 1000.0f, kNoFacing, 0.0f) ==
          foot_path_contact(path[0], path, 1, ha, b, 1000.0f, kNoFacing, 0.1f));
}

// ledger: verifies GameConstants.ShouldInfantryTeamsSplitAcrossFormations
void test_infantry_teams_split() {
    // BKR2_RE_ANSWERS Q8 [H] (MOVEMENT §4.2 step 2 item 7 test 3, 0x43c1b0): a candidate joins seed S only when (S has no
    // parent team, or ShouldInfantryTeamsSplitAcrossFormations (GC +0x149d, FoC true) is false) and the short-range rule
    // passes. So with the FoC value a squad member seeding a formation recruits nobody.
    const MovementConstants d;
    CHECK(d.should_infantry_teams_split_across_formations);
    const MovementConstants k = load("<ShouldInfantryTeamsSplitAcrossFormations>No</ShouldInfantryTeamsSplitAcrossFormations>");
    CHECK(!k.should_infantry_teams_split_across_formations);
    std::vector<FormationMember> m(2);
    for (uint32_t i = 0; i < 2; ++i) {
        m[i].id = static_cast<ObjectId>(i + 1);
        m[i].pos = {static_cast<float>(i) * 10.0f, 0.0f};
        m[i].max_speed = 1.0f;
        m[i].type_name = "Rebel_Infantry";
        m[i].collision_class = 5;
        m[i].team_member = true;
    }
    const uint32_t bucket[] = {0, 1};
    CHECK(split_into_formations(m, bucket, d).size() == 2);  // FoC: split
    CHECK(split_into_formations(m, bucket, k).size() == 1);  // modded off: one formation
    for (FormationMember& f : m) f.team_member = false;     // no parent team: the clause passes either way
    CHECK(split_into_formations(m, bucket, d).size() == 1);
}

// ledger: verifies GameConstants.SpaceStaticObstacleAvoidanceBonusDistance
void test_space_static_bonus() {
    // MOVEMENT §2.2 obstacle test item 1 (0x59a630 -> 0x5d7c00): each motion is swept against the layer map, plus the
    // static map offset by SpaceStaticObstacleAvoidanceBonusDistance (GC +0x1414, int, FoC 0). [port] the offset is a
    // margin on the static test's clearance.
    const MovementConstants d;
    CHECK(d.space_static_obstacle_avoidance_bonus_distance == 0.0f);
    const MovementConstants k = load("<SpaceStaticObstacleAvoidanceBonusDistance>250</SpaceStaticObstacleAvoidanceBonusDistance>");
    CHECK(near(k.space_static_obstacle_avoidance_bonus_distance, 250.0f));
    SpaceOccupancy occ;
    occ.add_static(-1, {0.0f, 0.0f}, 400.0f, kSpaceAsteroid);
    for (int pass = 0; pass < 2; ++pass) {  // full scan, then the grid index
        if (pass == 1) occ.build_index();
        CHECK(occ.query_segment({-1000.0f, 500.0f}, {1000.0f, 500.0f}, 0.0f, 10.0f, 60.0f, kLayerFrigate, 1, true, 0) == 0);
        CHECK(occ.query_segment({-1000.0f, 500.0f}, {1000.0f, 500.0f}, 0.0f, 10.0f, 60.0f, kLayerFrigate, 1, true, 0,
                                k.space_static_obstacle_avoidance_bonus_distance) == kSpaceAsteroid);
    }
    // through the planner: the path keeps the larger clearance from the asteroid
    SpaceRequest q;
    q.self = 1;
    q.start = {-1500.0f, 0.0f};
    q.goal = {1500.0f, 0.0f};
    q.speed = 6.0f;
    q.turn_rate = 1.5f;
    q.radius = 60.0f;
    q.layer = kLayerFrigate;
    const auto closest = [&](const MovementConstants& c) {
        SpaceScratch sc;
        SpacePath p;
        if (!plan_space_path(occ, c, q, sc, p) || !p.ok || p.fallback) return -1.0f;
        float best = 3.0e38f;
        for (float t = 0.0f; t <= p.wps.back().time; t += 1.0f) best = std::min(best, length(eval_space_path(p.wps, t).pos));
        return best;
    };
    const float vanilla = closest(d), modded = closest(k);
    std::printf("SpaceStaticObstacleAvoidanceBonusDistance: closest approach %.1f (0) vs %.1f (250)\n",
                static_cast<double>(vanilla), static_cast<double>(modded));
    CHECK(vanilla > 0.0f && modded > vanilla + 100.0f);
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_formation_side_error();
    test_approximation_forward_distance();
    test_infantry_teams_split();
    test_space_static_bonus();
    return ::alamo::test::report("alamo_tactical_movement_mcx4_tests");
}
