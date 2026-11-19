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
#include "alamo/core/sim_rng.hpp"
#include "alamo/tactical/movement/formation.hpp"
#include "alamo/tactical/movement/land_path.hpp"
#include "alamo/tactical/movement/locomotor.hpp"
#include "alamo/tactical/movement/movement_system.hpp"
#include "alamo/tactical/movement/space_path.hpp"
#include "alamo/tactical/movement/space_reevaluate.hpp"
#include "alamo/tactical/movement/special_locomotors.hpp"
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

UnitDesc mk_unit(ObjectId id, V2 pos, float speed, LocomotorKind kind, const char* type = "MCX4_UNIT") {
    UnitDesc d;
    d.id = id;
    d.pos = pos;
    d.max_speed = speed;
    d.turn_rate = 6.0f;
    d.alt_max_speed = speed;
    d.alt_turn_rate = 6.0f;
    d.movement_class = 2;
    d.collision_class = 5;
    d.radius = 6.0f;
    d.type_name = type;
    d.locomotor = kind;
    return d;
}

void open_land(MovementSystem& sys, uint32_t cells) {
    PassabilityGrid g;
    g.init(cells, cells);
    sys.set_grid(std::move(g));
    sys.set_height_fn([](float, float) { return 0.0f; });
    sys.set_anim_length_fn([](ObjectId, int32_t) { return 10u; });
    const float hi = static_cast<float>(cells - 1) * kCellSize;
    sys.air().set_box({0.0f, 0.0f}, {hi, hi});
}

// ledger: verifies GameObjectType.MaxFacingLookAheadFrames
// ledger: verifies GameConstants.SpaceLocomotorFacingLookaheadAcc
void test_bomber_facing_lookahead() {
    // MCX4_T11_T12_RE_ANSWERS §B2 / §B4 (LANDBOMBER step 0x645370, facing getter 0x645a70 [H]): the bomber faces the
    // position reserved int(lookahead) frames ahead (past the pass end: its own position, the path heading stays); after
    // the step the lookahead grows by SpaceLocomotorFacingLookaheadAcc (GC +0x144c, FoC 1.0) up to the type's
    // MaxFacingLookAheadFrames (+0x3b8, type ctor 30; FoC bombing runs 30, Snowspeeder / birds 5).
    const MovementConstants d;
    CHECK(near(d.space_locomotor_facing_lookahead_acc, 1.0f));
    CHECK(near(load("<SpaceLocomotorFacingLookaheadAcc>2.5</SpaceLocomotorFacingLookaheadAcc>").space_locomotor_facing_lookahead_acc, 2.5f));
    CHECK(UnitDesc{}.max_facing_lookahead == 30.0f);
    CHECK(bomber_lookahead_step(0.0f, 1.0f, 30.0f) == 1.0f && bomber_lookahead_step(29.5f, 1.0f, 30.0f) == 30.0f &&
          bomber_lookahead_step(30.0f, 1.0f, 30.0f) == 30.0f && bomber_lookahead_step(3.0f, 2.5f, 5.0f) == 5.0f);
    CHECK(bomber_facing({0.0f, 0.0f}, 12.0f, {10.0f, 10.0f}, true) == wrap_deg(heading_deg({10.0f, 10.0f})));
    CHECK(bomber_facing({0.0f, 0.0f}, 12.0f, {10.0f, 10.0f}, false) == 12.0f);  // past the pass end
    CHECK(bomber_facing({4.0f, 4.0f}, 12.0f, {4.0f, 4.0f}, true) == 12.0f);     // lookahead 0: the same position
    // through the system: a pass that starts facing away from its target curves; looking 30 frames ahead the bomber's yaw
    // leads the path heading, with a cap of 0 it is the path heading
    const auto run = [](float cap, float acc) {
        MovementConfig cfg;
        cfg.constants.space_locomotor_facing_lookahead_acc = acc;
        MovementSystem sys(cfg);
        open_land(sys, 100);
        UnitDesc b = mk_unit(9, cell_center(20, 50), 4.0f, LocomotorKind::LandBomber);
        b.yaw_deg = 90.0f;
        b.prepare_strafe_height = 100.0f;
        b.max_facing_lookahead = cap;
        CHECK(sys.add_unit(b));
        SimRng rng(1);
        MoveOrder o;
        o.units = {9};
        o.destination = cell_center(70, 50);
        sys.move(o, rng, 0);
        std::vector<float> yaws;
        for (uint32_t f = 0; f < 600 && sys.has_unit(9); ++f) {
            sys.tick(f, rng);
            if (sys.has_unit(9) && sys.view(9).loco_state == LocoState::WalkMoving) yaws.push_back(sys.view(9).yaw);
        }
        return yaws;
    };
    const std::vector<float> ahead = run(30.0f, 1.0f), path = run(0.0f, 1.0f), fast = run(30.0f, 30.0f);
    CHECK(!ahead.empty() && ahead.size() == path.size() && fast.size() == path.size());
    CHECK(!ahead.empty() && ahead[0] == path[0]);  // the first step looks 0 frames ahead
    float diff = 0.0f, diff_fast = 0.0f;
    for (size_t i = 0; i < ahead.size() && i < path.size() && i < fast.size(); ++i) {
        diff = std::max(diff, std::fabs(wrap_deg(ahead[i] - path[i])));
        if (i < 10) diff_fast = std::max(diff_fast, std::fabs(wrap_deg(fast[i] - ahead[i])));
    }
    std::printf("MaxFacingLookAheadFrames: %zu pass frames, max yaw lead %.3f deg (cap 30 vs 0), %.3f deg (acc 30 vs 1)\n",
                ahead.size(), static_cast<double>(diff), static_cast<double>(diff_fast));
    CHECK(diff > 1.0f && diff_fast > 0.0f);
}

// ledger: verifies GameObjectType.LateralAcceleration
void test_bike_lateral() {
    // MCX4_T11_T12_RE_ANSWERS §B2 (BIKE move step 0x63abe0 [H]): the sideways speed L (pack +0x1fc) steps toward the left
    // component of the steering vector by at most LateralAcceleration (+0x3bc, ctor 0; FoC Scout_Trooper 0.15), clamped
    // to +-v / vmax; the bike travels along normalise(facing + L x left).
    CHECK(UnitDesc{}.lateral_accel == 0.0f);
    CHECK(near(bike_lateral_step(0.0f, 1.0f, 0.15f, 2.0f, 2.0f), 0.15f));
    CHECK(near(bike_lateral_step(0.15f, -1.0f, 0.15f, 2.0f, 2.0f), 0.0f));
    CHECK(near(bike_lateral_step(0.9f, 1.0f, 0.15f, 1.0f, 2.0f), 0.5f));   // clamp +-v / vmax
    CHECK(bike_lateral_step(0.0f, 1.0f, 0.0f, 2.0f, 2.0f) == 0.0f);        // the default 0: never changes
    const V2 t = bike_travel_dir(0.0f, 0.5f);
    CHECK(near(length(t), 1.0f) && t.y > 0.0f && near(t.y / t.x, 0.5f));
    // through the walk step: a steering vector pointing left makes the bike drift left with the FoC value, not with 0
    LandMotion m;
    m.pos = {0.0f, 0.0f};
    m.yaw = 0.0f;
    m.speed = 2.0f;
    m.state = LocoState::WalkMoving;
    m.path = {{0.0f, 0.0f}, {400.0f, 0.0f}};
    m.next = 1;
    LocomotorParams p;
    p.max_speed = 2.0f;
    p.accel = 2.0f;
    p.decel = 2.0f;
    p.turn_rate = 0.0f;  // no turning: only the lateral term moves the bike off its facing
    const auto drift = [&](float accel) {
        LandMotion k = m;
        float lat = 0.0f;
        WalkExtras x;
        x.avoid = [](const void*, V2) { return V2{0.0f, 0.8f}; };
        x.lateral = &lat;
        x.lateral_accel = accel;
        for (int i = 0; i < 20; ++i) walk_step(k, p, 3.0e38f, nullptr, &x);
        return k.pos.y;
    };
    const float fo = drift(0.15f), none = drift(0.0f);
    std::printf("LateralAcceleration: sideways drift %.3f (0.15) vs %.3f (0)\n", static_cast<double>(fo), static_cast<double>(none));
    CHECK(none == 0.0f && fo > 1.0f);
}

// ledger: verifies GameObjectType.Snap_Movement_Orders_To_Center
void test_snap_orders_to_center() {
    // MCX4_T11_T12_RE_ANSWERS §B2 (the group move 0x2a7b80 [H]): of the objects whose type has
    // Snap_Movement_Orders_To_Center (FoC Prop_Good_Ground_Area), the nearest whose 3-D distance to the order point is <= its radius takes the point to its
    // centre; a later one replaces the pick only when strictly nearer; none: the point stays.
    const SnapCenter objs[] = {{{100.0f, 0.0f}, 0.0f, 50.0f}, {{0.0f, 100.0f}, 0.0f, 50.0f}, {{60.0f, 60.0f}, 0.0f, 10.0f}};
    CHECK(snap_order_center({90.0f, 10.0f}, 0.0f, objs) == 0);
    CHECK(snap_order_center({10.0f, 90.0f}, 0.0f, objs) == 1);
    CHECK(snap_order_center({0.0f, 0.0f}, 0.0f, objs) == -1);           // outside every radius
    CHECK(snap_order_center({60.0f, 60.0f}, 0.0f, objs) == 2);          // inside two: the nearer
    CHECK(snap_order_center({100.0f, 0.0f}, 60.0f, objs) == -1);        // 3-D: 60 above is outside the radius 50
    const SnapCenter tie[] = {{{10.0f, 0.0f}, 0.0f, 50.0f}, {{-10.0f, 0.0f}, 0.0f, 50.0f}};
    CHECK(snap_order_center({0.0f, 0.0f}, 0.0f, tie) == 0);             // a tie keeps the first in list order
}

// ledger: verifies GameConstants.FinalFormationFacingMinimumAngle
// ledger: verifies GameConstants.FinalFormationFacingDeltaCoefficient
void test_final_formation_facing() {
    // MCX4_T11_T12_RE_ANSWERS §B4 (0x411980, 0x421cc0, 0x559350 [H]): psi = wrap(facing - last segment yaw); below
    // FinalFormationFacingMinimumAngle (FoC 10) no rotation; R = sqrt(2) x half_width x 10; budget =
    // DeltaCoefficient (FoC 0.5) x L / R; start = sign(psi)(|psi| - budget) when the budget is short, rate = (psi - start) /
    // L x 1.2; the slots turn by r(s) = start + rate s, never past psi.
    const MovementConstants k;
    CHECK(near(k.final_formation_facing_minimum_angle, 10.0f) && near(k.final_formation_facing_delta_coefficient, 0.5f));
    const MovementConstants m =
        load("<FinalFormationFacingMinimumAngle>45</FinalFormationFacingMinimumAngle><FinalFormationFacingDeltaCoefficient>2"
             "</FinalFormationFacingDeltaCoefficient>");
    CHECK(near(m.final_formation_facing_minimum_angle, 45.0f) && near(m.final_formation_facing_delta_coefficient, 2.0f));
    const std::vector<V2> path = {{0.0f, 0.0f}, {200.0f, 0.0f}};
    // facing 90: psi = 90 deg; hw 2 -> R = sqrt(2) x 20; budget = 0.5 x 200 / 28.28 = 3.54 rad > pi/2: start 0
    FinalFacing f = final_formation_facing(path, 90.0f, 2.0f, 1.0f, k);
    const float full = 90.0f * 0.017453292f;
    CHECK(near(f.full, full) && f.start == 0.0f && near(f.rate, full / 200.0f * 1.2f));
    CHECK(near(f.rotation_at(100.0f), full * 0.6f) && f.rotation_at(200.0f) == f.full);  // reached before the end
    CHECK(!final_formation_facing(path, 5.0f, 2.0f, 1.0f, k).on());      // below 10 degrees
    CHECK(!final_formation_facing(path, 30.0f, 2.0f, 1.0f, m).on());     // below a mod's 45
    CHECK(!final_formation_facing(path, kNoFacing, 2.0f, 1.0f, k).on());
    // a short path: budget 0.5 x 20 / R(hw 4: 56.57) = 0.177 rad < |psi| -> start = -(|psi| - budget) for psi = -90
    const std::vector<V2> shortp = {{0.0f, 0.0f}, {20.0f, 0.0f}};
    f = final_formation_facing(shortp, -90.0f, 4.0f, 1.0f, k);
    const float r = std::sqrt(40.0f * 2.0f * 40.0f), budget = ((0.5f * 1.0f) / r) * (20.0f / 1.0f);
    CHECK(near(f.full, -full) && near(f.start, budget - full) && near(f.rate, ((f.full - f.start) / 20.0f) * 1.2f));
    CHECK(f.rotation_at(1000.0f) == f.full);
    // a mod's larger coefficient turns more while walking (less at once)
    const FinalFacing g = final_formation_facing(shortp, -90.0f, 4.0f, 1.0f, m);
    CHECK(g.start > f.start);
    // the member's stop point: G + d1 (cos psi D - sin psi L) + d1_perp (sin psi D + cos psi L), D = -back
    LaneShape sh;
    sh.left = 10.0f;
    const V2 plain = lane_end(path, sh, 20.0f);
    CHECK(near(plain.x, 180.0f) && near(plain.y, 10.0f));
    sh.depth = -20.0f;
    sh.rot_full = full;
    sh.rot_rate = full / 200.0f * 1.2f;
    const V2 turned = lane_end(path, sh, 20.0f);
    std::printf("FinalFormationFacing: stop (%.2f, %.2f) without, (%.2f, %.2f) with a 90 deg facing\n", static_cast<double>(plain.x),
                static_cast<double>(plain.y), static_cast<double>(turned.x), static_cast<double>(turned.y));
    CHECK(near(turned.x, 200.0f - 10.0f) && near(turned.y, -20.0f));  // the slot (-20, 10) turned by 90: (-10, -20)
}

// ledger: verifies GameConstants.Rotate_Formation_Facing_Moves
void test_rotate_formation_facing_moves() {
    // MCX4_T11_T12_RE_ANSWERS §B4 (0x439360 [H]): with Rotate_Formation_Facing_Moves (FoC true) and an ordered facing, the
    // destination check measures phi from the path's first segment to the facing (else to its last segment), so the
    // formations' places around the goal turn with the facing.
    CHECK(MovementConstants{}.rotate_formation_facing_moves);
    CHECK(!load("<Rotate_Formation_Facing_Moves>false</Rotate_Formation_Facing_Moves>").rotate_formation_facing_moves);
    const auto run = [](bool rotate) {
        MovementConfig cfg;
        cfg.constants.rotate_formation_facing_moves = rotate;
        MovementSystem sys(cfg);
        open_land(sys, 100);
        // two formations (two types far apart)
        const V2 a = cell_center(20, 40), b = cell_center(20, 60);
        for (ObjectId i = 1; i <= 2; ++i) CHECK(sys.add_unit(mk_unit(i, a + V2{0.0f, 15.0f * static_cast<float>(i)}, 1.0f, LocomotorKind::Default, "MCX4_A")));
        for (ObjectId i = 3; i <= 4; ++i) CHECK(sys.add_unit(mk_unit(i, b + V2{0.0f, 15.0f * static_cast<float>(i)}, 1.0f, LocomotorKind::Default, "MCX4_B")));
        SimRng rng(5);
        sys.tick(0, rng);
        MoveOrder o;
        o.units = {1, 2, 3, 4};
        o.destination = cell_center(70, 50);
        o.facing_deg = 90.0f;
        sys.move(o, rng, 1);
        std::vector<V2> goals;
        for (ObjectId i = 1; i <= 4; ++i) goals.push_back(sys.view(i).goal);
        return goals;
    };
    const std::vector<V2> on = run(true), off = run(false);
    float moved = 0.0f;
    for (size_t i = 0; i < on.size(); ++i) moved = std::max(moved, distance(on[i], off[i]));
    std::printf("Rotate_Formation_Facing_Moves: goals move by up to %.2f\n", static_cast<double>(moved));
    CHECK(moved > 1.0f);
}

// ledger: verifies GameConstants.ShouldSkipLandFormup
void test_skip_land_formup() {
    // MCX4_T11_T12_RE_ANSWERS §B4 Round 4 "ShouldSkipLandFormup = false" (0x41c1a0 [H]): with the constant clear, a member of
    // a land formation of 2+ walks a lead-in path from where it stands to its form-up point (its slot in the layout box at
    // the formation path's start) and then its formation path; each member goes on once its own lead-in is done (no wait,
    // no frame limit). FoC sets it: the member joins its lane from where it stands.
    CHECK(MovementConstants{}.should_skip_land_formup);
    CHECK(!load("<ShouldSkipLandFormup>No</ShouldSkipLandFormup>").should_skip_land_formup);
    struct Out {
        uint32_t leadins = 0, lanes = 0;
        std::vector<std::vector<V2>> paths;
        std::vector<V2> starts;
        bool arrived = true;
        uint32_t frames = 0;
    };
    const auto run = [](bool skip) {
        MovementConfig cfg;
        cfg.constants.should_skip_land_formup = skip;
        MovementSystem sys(cfg);
        open_land(sys, 100);
        // one behind the other along the move: the layout puts them side by side, so neither stands on its form-up point
        CHECK(sys.add_unit(mk_unit(1, cell_center(21, 50), 1.0f, LocomotorKind::Default, "MCX4_F")));
        CHECK(sys.add_unit(mk_unit(2, cell_center(19, 50), 1.0f, LocomotorKind::Default, "MCX4_F")));
        SimRng rng(3);
        sys.tick(0, rng);
        MoveOrder o;
        o.units = {1, 2};
        o.destination = cell_center(70, 50);
        sys.move(o, rng, 1);
        Out out;
        for (ObjectId i = 1; i <= 2; ++i) out.starts.push_back(sys.view(i).pos);
        uint32_t f = 1;
        for (; f < 4000; ++f) {
            sys.tick(f, rng);
            if (out.paths.empty() && sys.stats().lanes_taken >= 2)
                for (ObjectId i = 1; i <= 2; ++i) {
                    const std::span<const V2> p = sys.land_path(i);
                    out.paths.emplace_back(p.begin(), p.end());
                }
            if (!sys.view(1).has_goal && !sys.view(2).has_goal) break;
        }
        out.frames = f;
        for (ObjectId i = 1; i <= 2; ++i) out.arrived = out.arrived && sys.view(i).arrived;
        out.leadins = sys.stats().formup_leadins;
        out.lanes = sys.stats().lanes_taken;
        return out;
    };
    const Out foc = run(true), mod = run(false);
    std::printf("ShouldSkipLandFormup: lead-ins %u (true) / %u (false), lanes %u / %u, arrived in %u / %u frames\n", foc.leadins,
                mod.leadins, foc.lanes, mod.lanes, foc.frames, mod.frames);
    CHECK(foc.leadins == 0 && mod.leadins == 2);
    CHECK(foc.lanes == 2 && mod.lanes == 2);
    CHECK(foc.arrived && mod.arrived);
    CHECK(foc.paths.size() == 2 && mod.paths.size() == 2);
    for (size_t i = 0; i < foc.paths.size() && i < mod.paths.size(); ++i) {
        // the lead-in starts where the member stands; FoC's lane path starts at its foot on the lane
        CHECK(!mod.paths[i].empty() && distance(mod.paths[i].front(), mod.starts[i]) < 1.0e-3f);
        CHECK(foc.paths[i] != mod.paths[i]);
        CHECK(!foc.paths[i].empty() && !mod.paths[i].empty() && foc.paths[i].back() == mod.paths[i].back());  // the same slot
    }
}

MovementConfig space_cfg() {
    MovementConfig cfg;
    cfg.mode = MoveMode::Space;
    return cfg;
}

UnitDesc frigate(ObjectId id, V2 pos) {
    UnitDesc d;
    d.id = id;
    d.pos = pos;
    d.max_speed = 3.0f;
    d.turn_rate = 2.0f;
    d.radius = 40.0f;
    d.space_layer = kLayerFrigate;
    d.space_layer_type = 2;
    d.type_name = "MCX4_FRIGATE";
    return d;
}

// ledger: verifies GameConstants.ShouldUseSpaceIdleMovement
// ledger: verifies GameConstants.IdleMovementFrames
void test_space_idle_drift() {
    // MCX4_T11_T12_RE_ANSWERS §B4 (0x423070 / 0x420a40 / 0x413100 [H read, M meaning]): in space with
    // ShouldUseSpaceIdleMovement (FoC false), an arrived ship waits int(F + synced float(0, F / 2) + 0.5) frames
    // (F = IdleMovementFrames, FoC 30), then drifts int(F) ahead along its facing at SpaceIdleMovementSpeed, inside the map
    // less 300; then it waits again. FoC: nothing (no draw, no move).
    const MovementConstants d;
    CHECK(!d.should_use_space_idle_movement && near(d.idle_movement_frames, 30.0f) && d.space_idle_movement_speed == 0.0f);
    const MovementConstants k = load("<ShouldUseSpaceIdleMovement>Yes</ShouldUseSpaceIdleMovement><IdleMovementFrames>40"
                                     "</IdleMovementFrames><SpaceIdleMovementSpeed>1.5</SpaceIdleMovementSpeed>");
    CHECK(k.should_use_space_idle_movement && near(k.idle_movement_frames, 40.0f) && near(k.space_idle_movement_speed, 1.5f));
    struct Out {
        uint32_t arrival = 0, first_drift = 0, drifts = 0;
        V2 at_arrival, drift_goal, drift_from, end;
        float drift_speed = 0.0f;
        bool stopped_still = true;
    };
    const auto run = [](const MovementConstants& c, V2 dest, float box) {
        MovementConfig cfg = space_cfg();
        cfg.constants = c;
        MovementSystem sys(cfg);
        sys.space().set_box({-box, -box}, {box, box});
        CHECK(sys.add_unit(frigate(5, {0.0f, 0.0f})));
        SimRng rng(11);
        MoveOrder o;
        o.units = {5};
        o.destination = dest;
        sys.move(o, rng, 0);
        Out out;
        for (uint32_t f = 0; f < 3000; ++f) {
            sys.tick(f, rng);
            const UnitView v = sys.view(5);
            if (out.arrival == 0 && v.arrived) {
                out.arrival = f;
                out.at_arrival = v.pos;
            }
            if (out.arrival != 0 && out.first_drift == 0 && v.has_goal) {
                out.first_drift = f;
                out.drift_goal = v.goal;
                out.drift_from = out.at_arrival;  // the ship stood there until the drift (planned and started this tick)
            }
            if (out.first_drift != 0 && v.has_goal) out.drift_speed = std::max(out.drift_speed, v.speed);
            if (f == 1500) {  // a stop: no drift after it
                const ObjectId ids[] = {5};
                sys.stop(ids);
                out.drifts = sys.stats().space_idle_drifts;
                out.end = sys.view(5).pos;
            }
            if (f > 1500) out.stopped_still = out.stopped_still && sys.view(5).pos == out.end && !sys.view(5).has_goal;
        }
        CHECK(sys.stats().space_idle_drifts == out.drifts);
        return out;
    };
    const Out foc = run(d, {600.0f, 0.0f}, 5000.0f);
    CHECK(foc.arrival != 0 && foc.first_drift == 0 && foc.drifts == 0);
    const Out mod = run(k, {600.0f, 0.0f}, 5000.0f);
    std::printf("IdleMovementFrames: arrival %u, first drift %u (wait %u), %u drifts, goal %.2f from the arrival, top speed %.3f\n",
                mod.arrival, mod.first_drift, mod.first_drift - mod.arrival, mod.drifts,
                static_cast<double>(distance(mod.drift_goal, mod.drift_from)), static_cast<double>(mod.drift_speed));
    CHECK(mod.first_drift >= mod.arrival + 41 && mod.first_drift <= mod.arrival + 61);  // int(40 + [0, 20] + 0.5), armed next frame
    CHECK(std::fabs(distance(mod.drift_goal, mod.drift_from) - 40.0f) < 0.01f);       // int(F) along the facing
    CHECK(mod.drifts >= 5 && mod.drift_speed > 0.0f && mod.drift_speed <= 1.5f * 1.05f);
    CHECK(mod.stopped_still);
    // near the map edge: the drift point stays 300 inside the box (x <= 1000 - 300)
    const Out edge = run(k, {950.0f, 0.0f}, 1000.0f);
    CHECK(edge.first_drift != 0 && near(edge.drift_goal.x, 700.0f));
    // the speed cap 0 (FoC's SpaceIdleMovementSpeed) with the flag: the waits run, no drift moves
    MovementConstants z = k;
    z.space_idle_movement_speed = 0.0f;
    const Out none = run(z, {600.0f, 0.0f}, 5000.0f);
    CHECK(none.first_drift == 0 && none.drifts == 0);
}

// ledger: verifies GameConstants.MovementReevaluationFrameCount
void test_movement_reevaluation() {
    // MCX4_T11_T12_RE_ANSWERS §B4 Round 4 (0x5d51e0 gate, 0x5d55f0 tests [H]): the coordinator re-evaluates when its id %
    // N = frame % N (N = MovementReevaluationFrameCount, FoC 10) and frame >= its not-before frame; it re-plans when the
    // lead is in max range neither from its slot point nor from its position, or is in range and the target is inside
    // its min range. The battle host runs the pass (TacticalBattle::space_attack_reevaluate).
    CHECK(MovementConstants{}.movement_reevaluation_frame_count == 10u);
    const MovementConstants k = load("<MovementReevaluationFrameCount>4</MovementReevaluationFrameCount>");
    CHECK(k.movement_reevaluation_frame_count == 4u);
    uint32_t foc = 0, mod = 0;
    for (uint32_t f = 0; f < 100; ++f) {
        foc += space_reevaluation_due(13, f, MovementConstants{}.movement_reevaluation_frame_count) ? 1u : 0u;
        mod += space_reevaluation_due(13, f, k.movement_reevaluation_frame_count) ? 1u : 0u;
    }
    CHECK(foc == 10u && mod == 25u);
    CHECK(space_reevaluation_due(13, 3, 10) && !space_reevaluation_due(13, 4, 10) && space_reevaluation_due(13, 13, 10));
    CHECK(!space_reevaluation_due(13, 13, 10, 20) && space_reevaluation_due(13, 23, 10, 20));  // the not-before frame
    CHECK(!space_reevaluation_due(0, 0, 0));                                                  // [port] N = 0: never
    CHECK(space_attack_replan(false, false, false) && space_attack_replan(false, false, true));
    CHECK(!space_attack_replan(true, false, false) && !space_attack_replan(false, true, false) && !space_attack_replan(true, true, false));
    CHECK(space_attack_replan(true, false, true) && space_attack_replan(false, true, true));
}

// ledger: verifies GameObjectType.Max_Distance_From_Spawner
void test_indigenous_wander() {
    // MCX4_T11_T12_RE_ANSWERS §B2 Max_Distance_From_Spawner and its Round 4 paragraph (0x621d80 [H]), TRU1_C_RE §1.3 / §1.4:
    // the spawner writes home := its position and leash D := its type's Max_Distance_From_Spawner (FoC 150 - 220; 0: no
    // leash). An idle unit with Can_Indigenous_Unit_Stop stays when float(0, 1) > 0.1; else dx = float(-D, D), dy =
    // float(-sqrt(D^2 - dx^2), +...), point = home + (dx, dy); without a leash a point of the map rectangle; a step outside
    // [50, 400] ([60, 150] burning) becomes float(min, max) long along the same direction.
    IndigenousWanderInput in;
    in.pos = {1000.0f, 1000.0f, 0.0f};
    in.home.home = {1000.0f, 1000.0f, 0.0f};
    in.home.leash = 150.0f;  // Max_Distance_From_Spawner of most FoC spawn houses
    in.can_stop = false;
    in.cx = 2000.0f;
    in.cy = 2000.0f;
    in.hx = 2000.0f;
    in.hy = 2000.0f;
    // the draws, one by one, against a reference stream
    for (uint32_t seed = 1; seed <= 300; ++seed) {
        SimRng a(seed), b(seed);
        WanderPoint p;
        CHECK(indigenous_wander_point(in, a, p));
        const float dx = b.range_float(-150.0f, 150.0f);
        const float s = std::sqrt(150.0f * 150.0f - dx * dx);
        const float dy = b.range_float(-s, s);
        float ex = 1000.0f + dx, ey = 1000.0f + dy;
        const float vx = ex - 1000.0f, vy = ey - 1000.0f;
        const float dd = std::sqrt(vx * vx + vy * vy);
        if (dd > 400.0f || dd < 50.0f) {
            const float t = b.range_float(50.0f, 400.0f);
            ex = 1000.0f + vx * (t / dd);
            ey = 1000.0f + vy * (t / dd);
            CHECK(std::sqrt((p.x - 1000.0f) * (p.x - 1000.0f) + (p.y - 1000.0f) * (p.y - 1000.0f)) >= 50.0f - 1e-3f);
        } else {
            CHECK(dd <= 150.0f + 1e-3f);  // inside the leash circle
        }
        CHECK(p.x == ex && p.y == ey && p.z == 0.0f);
        CHECK(a.range_int(0, 1000000) == b.range_int(0, 1000000));  // the same number of draws
    }
    // Can_Indigenous_Unit_Stop: stays about 90 % of the services (one draw each)
    {
        IndigenousWanderInput c = in;
        c.can_stop = true;
        SimRng rng(5);
        uint32_t moved = 0;
        WanderPoint p;
        for (int i = 0; i < 2000; ++i) moved += indigenous_wander_point(c, rng, p) ? 1u : 0u;
        std::printf("Max_Distance_From_Spawner: Can_Indigenous_Unit_Stop moves %u of 2000 services\n", moved);
        CHECK(moved > 140 && moved < 260);
    }
    // no map rectangle: stays (after the stop draw)
    {
        IndigenousWanderInput c = in;
        c.cx = c.cy = c.hx = c.hy = 0.0f;
        SimRng rng(5);
        WanderPoint p;
        CHECK(!indigenous_wander_point(c, rng, p));
    }
    // no leash (D = 0, Desert_Sketto_Spawn / Bird_Spawn): the map rectangle, x then y; and the burning bounds
    {
        IndigenousWanderInput c = in;
        c.home.leash = 0.0f;
        c.burning = true;
        c.pos = {100.0f, 100.0f, 0.0f};
        SimRng a(77), b(77);
        WanderPoint p;
        CHECK(indigenous_wander_point(c, a, p));
        const float x = b.range_float(0.0f, 4000.0f), y = b.range_float(0.0f, 4000.0f);
        const float vx = x - 100.0f, vy = y - 100.0f, dd = std::sqrt(vx * vx + vy * vy);
        if (dd > 150.0f || dd < 60.0f) {
            const float t = b.range_float(60.0f, 150.0f);
            CHECK(p.x == 100.0f + vx * (t / dd) && p.y == 100.0f + vy * (t / dd));
        } else {
            CHECK(p.x == x && p.y == y);
        }
    }
    // the per-unit home in the movement system (set by the host's spawner)
    MovementSystem sys;
    open_land(sys, 50);
    CHECK(sys.add_unit(mk_unit(3, cell_center(10, 10), 1.0f, LocomotorKind::Default)));
    CHECK(!sys.indigenous_home(3).has_home() && !sys.indigenous_home(99).has_home());
    IndigenousHome h;
    h.home = {200.0f, 220.0f, 5.0f};
    h.leash = 180.0f;
    CHECK(sys.set_indigenous_home(3, h) && !sys.set_indigenous_home(99, h));
    CHECK(sys.indigenous_home(3).has_home() && sys.indigenous_home(3).leash == 180.0f && sys.indigenous_home(3).home.y == 220.0f);
    h.leash = -5.0f;  // a negative leash is the no-leash branch: kept as 0
    CHECK(sys.set_indigenous_home(3, h) && sys.indigenous_home(3).leash == 0.0f);
}

// ledger: verifies GameConstants.LandObjectTrackingInterval
// ledger: verifies GameConstants.LandObjectTrackingTreeCount
void test_land_tracking_window() {
    // FIGHTER_FLYING §6.3.1 item 3 / MCX4_T11_T12_RE_ANSWERS §B4 (0x5d8140 [H]): in land mode the tracking rings hold
    // LandObjectTrackingTreeCount (FoC 25) slices of LandObjectTrackingInterval (FoC 20) frames. [port M] the land air
    // units' timed-path occupancy sees tracks that far ahead (500 frames in FoC; space keeps 45 x 90).
    const MovementConstants d;
    CHECK(d.land_object_tracking_tree_count == 25u && d.land_object_tracking_interval == 20u);
    const MovementConstants k =
        load("<LandObjectTrackingInterval>30</LandObjectTrackingInterval><LandObjectTrackingTreeCount>40</LandObjectTrackingTreeCount>");
    CHECK(k.land_object_tracking_interval == 30u && k.land_object_tracking_tree_count == 40u);
    const auto seen = [](const MovementConstants& c, float t) {
        MovementConfig cfg;
        cfg.constants = c;
        MovementSystem sys(cfg);
        SpaceWaypoint a, b;
        a.pos = b.pos = {0.0f, 0.0f};
        b.time = 5000.0f;  // a flyer hovering over (0, 0) for 5000 frames
        sys.air().set_track(7, kLayerCorvette, 10.0f, 0, {a, b});
        return sys.air().query_point({0.0f, 0.0f}, t, 10.0f, kLayerCorvette, 1, 0) != 0u;
    };
    CHECK(seen(d, 400.0f) && seen(k, 400.0f));
    CHECK(!seen(d, 600.0f) && seen(k, 600.0f));        // 25 x 20 = 500 vs 40 x 30 = 1200
    CHECK(!seen(k, 1300.0f));
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_formation_side_error();
    test_approximation_forward_distance();
    test_infantry_teams_split();
    test_space_static_bonus();
    test_bomber_facing_lookahead();
    test_bike_lateral();
    test_snap_orders_to_center();
    test_final_formation_facing();
    test_rotate_formation_facing_moves();
    test_skip_land_formup();
    test_space_idle_drift();
    test_movement_reevaluation();
    test_indigenous_wander();
    test_land_tracking_window();
    return ::alamo::test::report("alamo_tactical_movement_mcx4_tests");
}
