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
    return ::alamo::test::report("alamo_tactical_movement_mcx4_tests");
}
