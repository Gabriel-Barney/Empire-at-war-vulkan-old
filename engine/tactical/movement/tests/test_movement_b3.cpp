// alamo_tactical_movement_b3_tests (no framework; non-zero exit on failure).
// s78n t23 (t20 batch 3): the t21 review follow-ups (step 0) - halt's brake stub, crush and locomotor-less objects, a
// squad's final facing after an early arrival; the deployed half width (MOVEMENT §5.3 "When the deployed pair is used");
// MovingVsMoving for every pair (§5.4.2, §5.4.6).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "alamo/test/engagement_fns.hpp"
#include "alamo/core/bytes.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/core/sim_rng.hpp"
#include "alamo/tactical/movement/movement_system.hpp"
#include "alamo/test/support.hpp"
#include "alamo/test/thread_parallel_for.hpp"

using namespace alamo;
using namespace alamo::tactical::movement;

using ::alamo::test::g_fail;

namespace {

[[maybe_unused]] ParallelFor make_thread_pf(unsigned threads) { return ::alamo::test::make_thread_parallel_for(threads); }

[[maybe_unused]] bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

PassabilityGrid open_grid(uint32_t w, uint32_t h) {
    PassabilityGrid g;
    g.init(w, h);
    return g;
}

UnitDesc unit(ObjectId id, V2 pos, int32_t cls, float speed, float radius) {
    UnitDesc d;
    d.id = id;
    d.pos = pos;
    d.max_speed = speed;
    d.turn_rate = 6.0f;
    d.alt_max_speed = speed;
    d.alt_turn_rate = 6.0f;
    d.movement_class = cls;
    d.collision_class = kUccInfantry;
    d.radius = radius;
    d.type_name = "B3_UNIT";
    return d;
}

void land_system(MovementSystem& sys, uint32_t cells) {
    PassabilityGrid g = open_grid(cells, cells);
    const float hi = static_cast<float>(g.width() - 1) * kCellSize;
    sys.set_grid(std::move(g));
    sys.set_height_fn([](float, float) { return 0.0f; });
    sys.air().set_box({0.0f, 0.0f}, {hi, hi});
}

UnitDesc vehicle(ObjectId id, V2 pos, int32_t owner, float hx = 10.0f, float hy = 6.0f, float speed = 1.0f) {
    UnitDesc d = unit(id, pos, 2, speed, 12.0f);
    d.collision_class = kUccVehicle;
    d.hard_x = hx;
    d.hard_y = hy;
    d.owner = owner;
    return d;
}

// A 9-member squad on a 3 x 3 grid of pitch 12 (member half extents 3): container `cid`, members cid + 1 .. cid + 9.
void squad(MovementSystem& sys, ObjectId cid, V2 at, int32_t owner, float cmax = 0.8f) {
    UnitDesc c = unit(cid, at, 2, cmax, 12.0f);
    c.team_container = true;
    c.container_arrangement = 0;
    c.owner = owner;
    c.type_hx = c.type_hy = 0.0f;
    CHECK(sys.add_unit(c));
    std::vector<ObjectId> ids;
    std::vector<V2> slots;
    ObjectId id = cid + 1;
    for (int32_t ix = 1; ix >= -1; --ix)
        for (int32_t iy = 1; iy >= -1; --iy) {
            const V2 rel{12.0f * static_cast<float>(ix), 12.0f * static_cast<float>(iy)};
            UnitDesc d = unit(id, at + rel, 2, 1.0f, 3.0f);
            d.accel = d.decel = 0.05f;
            d.owner = owner;
            d.formation_spacing = 0.3f;
            CHECK(sys.add_unit(d));
            CHECK(sys.set_team_member_shape(d.id, 3.0f, 3.0f, 3.0f, 3.0f));
            ids.push_back(id++);
            slots.push_back(rel);
        }
    CHECK(sys.set_land_team(cid, ids, slots));
    CHECK(sys.set_team(cid, cid, 1.0f, cmax));
    for (ObjectId m : ids) CHECK(sys.set_team(m, cid, 1.0f, cmax));
}

// ---------------------------------------------------------------- step 0: t21 review follow-ups
// halt() keeps the speed: no order and no path record left (s78n t26.4: the t20 End Move brake stub is gone) - the walk
// goes to End Move at T = a and coasts along its last frame's displacement (MOVEMENT 3.5.2 C.1 / C.4), v falling by a per
// frame to a, then 0x15: v - a + v - 2a + ... + a = v^2 / (2a) - v / 2 along the facing.
void test_step0_halt_brake() {
    ::alamo::test::section("step0_halt_brake");
    MovementSystem sys{MovementConfig{}};
    land_system(sys, 80);
    UnitDesc d = vehicle(1, cell_center(10, 40), 0);
    d.accel = d.decel = 0.05f;
    CHECK(sys.add_unit(d));
    SimRng rng(301);
    MoveOrder o;
    o.units = {1};
    o.destination = cell_center(70, 40);
    sys.move(o, rng, 1);
    uint32_t f = 1;
    for (; f < 200 && sys.view(1).speed < sys.view(1).max_speed; ++f) sys.tick(f, rng, serial_for);
    const UnitView v0 = sys.view(1);
    const float v = v0.speed, mult = v0.max_speed / d.max_speed;  // the land multiplier scales the speed and the rates
    const float stub = v * v / (2.0f * d.decel * mult);
    CHECK(v == v0.max_speed && v0.has_goal);
    const ObjectId one[] = {1};
    sys.halt(one, f);
    const UnitView h = sys.view(1);
    CHECK(!h.has_goal && h.speed == v && !h.pushed);
    float travelled = 0.0f;
    V2 p = h.pos;
    for (uint32_t n = 0; n < 200 && sys.view(1).speed > 0.0f; ++n) {
        sys.tick(++f, rng, serial_for);
        travelled += distance(p, sys.view(1).pos);
        p = sys.view(1).pos;
    }
    // the coast: v^2 / (2a) - v / 2 (within one step)
    CHECK(sys.view(1).speed == 0.0f && !sys.view(1).pushed && travelled > stub - v && travelled <= stub + 1e-3f);
    CHECK(std::fabs(p.y - v0.pos.y) < 1e-3f);
    std::printf("step0 halt: coasted %.3f along the facing (v^2 / 2a = %.3f)\n", static_cast<double>(travelled),
                static_cast<double>(stub));
}

// A crusher never crushes an object without a locomotor (a death-clone obstacle), even a hostile squashable one.
void test_step0_crush_no_locomotor() {
    ::alamo::test::section("step0_crush_no_locomotor");
    for (int clone = 0; clone < 2; ++clone) {
        MovementSystem sys{MovementConfig{}};
        land_system(sys, 80);
        UnitDesc c = vehicle(1, cell_center(10, 40), 0, 20.0f, 12.0f);
        c.crusher = true;
        CHECK(sys.add_unit(c));
        UnitDesc v = unit(2, cell_center(13, 40), 2, 1.0f, 4.0f);
        v.owner = 1;
        v.max_speed = 0.0f;  // it cannot flee (the crush-flee push fails): the crusher drives on through it
        v.squashable = true;
        v.no_locomotor = clone != 0;
        v.death_clone_obstacle = clone != 0;
        CHECK(sys.add_unit(v));
        SimRng rng(302);
        MoveOrder o;
        o.units = {1};
        o.destination = cell_center(30, 40);
        sys.move(o, rng, 1);
        bool crushed = false;
        for (uint32_t f = 1; f < 120; ++f) {
            sys.tick(f, rng, serial_for);
            for (const CrushEvent& e : sys.crush_events()) crushed = crushed || e.victim == 2;
        }
        CHECK(crushed == (clone == 0));
        std::printf("step0 crush: %s victim -> %s\n", clone ? "locomotor-less" : "walking", crushed ? "crushed" : "not crushed");
    }
}

// A squad ordered onto a standing unit arrives early and turns in place to its ordered facing (the final-facing test
// also runs when the arrival is not the container's own 0x15).
void test_step0_squad_final_facing() {
    ::alamo::test::section("step0_squad_final_facing");
    MovementSystem sys{MovementConfig{}};
    land_system(sys, 100);
    const V2 at = cell_center(10, 20);
    squad(sys, 100, at, 0);
    const V2 goal = cell_center(25, 20);
    UnitDesc x = unit(70, goal, 2, 1.0f, 6.0f);
    x.owner = 1;
    SimRng rng(303);
    sys.tick(1, rng, serial_for);
    MoveOrder o;
    o.units = {100};
    o.destination = goal;
    o.final_facing_deg = 90.0f;
    sys.move(o, rng, 2);
    bool arrived = false, turned = false, placed = false;
    for (uint32_t f = 2; f < 900 && !arrived; ++f) {
        // mov1 t04 (BKR1 Q5 [H]): the destination counts as blocked only once the squad's approximation byte +0x28c says
        // its path ends within L = 60 (rebuilt every 8 frames): the unit steps onto the goal when the squad is 45 out
        if (!placed && sys.view(100).pos.x > goal.x - 45.0f) placed = sys.add_unit(x);
        sys.tick(f, rng, serial_for);
        turned = turned || sys.view(100).team_final_turn;
        arrived = sys.view(100).arrived && !sys.view(100).team_final_turn;
    }
    const UnitView k = sys.view(100);
    CHECK(arrived && turned && sys.stats().early_arrivals >= 1 && std::fabs(wrap_deg(k.yaw - 90.0f)) < 1e-3f);
    std::printf("step0 squad: early arrival %.2f before the unit, final yaw %.2f\n", static_cast<double>(goal.x - k.pos.x),
                static_cast<double>(k.yaw));
}

// ---------------------------------------------------------------- 3.1 deployed half width (MOVEMENT §5.3)
// MPTL-like (Y 15, deployed Y 24, scale 1.1): the half width 16.5 -> 26.4 while deploying and deployed, 16.5 while
// undeploying; the rectangle never changes (no deployed X). MAL-like (scale 0.7): 10.5 -> 16.8. A synthetic deployed X
// 40 with no deployed Y: the rectangle (40, 0) while deployed, the half width keeps the normal Y.
void test_t20_deployed_width() {
    ::alamo::test::section("t20_deployed_width");
    using DR = MovementSystem::DeployResult;
    struct Case {
        float hx, hy, xd, yd;
    };
    const Case cases[3] = {{30.0f * 1.1f, 15.0f * 1.1f, 0.0f, 24.0f * 1.1f},
                           {20.0f * 0.7f, 15.0f * 0.7f, 0.0f, 24.0f * 0.7f},
                           {30.0f, 16.5f, 40.0f, 0.0f}};
    for (int k = 0; k < 3; ++k) {
        const Case& q = cases[k];
        MovementSystem sys{MovementConfig{}};
        land_system(sys, 60);
        UnitDesc d = vehicle(1, cell_center(20, 20), 0, q.hx, q.hy);
        d.hard_x_deployed = q.xd;
        d.hard_y_deployed = q.yd;
        CHECK(sys.add_unit(d));
        UnitDesc n = vehicle(2, cell_center(20, 20) + V2{q.hx + 20.0f, 0.0f}, 0, 20.0f, 10.0f);
        CHECK(sys.add_unit(n));
        SimRng rng(311);
        const bool rect_switch = q.xd > 0.0f;
        const V2 normal{q.hx, q.hy}, deployed = rect_switch ? V2{q.xd, 0.0f} : normal;
        const float hw_dep = q.yd > 0.0f ? q.yd : q.hy;
        CHECK(sys.collision_half(1) == normal && sys.collision_half_width(1) == q.hy);
        CHECK(sys.deploy(1, true, 1) == DR::Deploying && sys.deploy_state(1) == LocoState::WalkDeploying);
        CHECK(sys.collision_half(1) == deployed && sys.collision_half_width(1) == hw_dep);
        sys.tick(1, rng, serial_for);  // no clip data: the Deploy clip ends on the frame it starts
        CHECK(sys.deploy_state(1) == LocoState::WalkDeployed);
        CHECK(sys.collision_half(1) == deployed && sys.collision_half_width(1) == hw_dep);
        CHECK(sys.deploy(1, false, 2) == DR::Undeploying && sys.deploy_state(1) == LocoState::WalkUndeploying);
        CHECK(sys.collision_half(1) == normal && sys.collision_half_width(1) == q.hy);
        sys.tick(2, rng, serial_for);
        CHECK(sys.deploy_state(1) == LocoState::None && sys.collision_half(1) == normal && sys.collision_half_width(1) == q.hy);
        std::printf("t20 deployed width: case %d - half width %.2f -> %.2f, rectangle (%.1f, %.1f) -> (%.1f, %.1f)\n", k,
                    static_cast<double>(q.hy), static_cast<double>(hw_dep), static_cast<double>(normal.x),
                    static_cast<double>(normal.y), static_cast<double>(deployed.x), static_cast<double>(deployed.y));
    }
}

// ---------------------------------------------------------------- 3.2 MovingVsMoving for every pair (§5.4.2, §5.4.6)
const CollisionObject* mvm_record(const MovementSystem& sys, ObjectId x, ObjectId y) {
    for (const CollisionObject& c : sys.collisions().list())
        if (c.kind == CollisionKind::MovingVsMoving && ((c.a == x && c.b == y) || (c.a == y && c.b == x))) return &c;
    return nullptr;
}
ObjectId master_of(const CollisionObject& c) { return c.master == 0 ? c.a : c.b; }

uint64_t units_crc(const MovementSystem& sys) {
    StateHasher h;
    sys.hash_units(h);
    sys.collisions().hash(h);
    return h.digest();
}

// Two vehicles (soft radius 20, half extents 10 x 6) on one line, both ordered +x: the rear one at 1.0 x the land
// multiplier (1.56), the front one at half that (0.78), 60 apart; the rear one's path is `path` long.
void overtake_scene(MovementSystem& sys, SimRng& rng, float path) {
    land_system(sys, 160);
    const V2 at = cell_center(20, 80);
    UnitDesc r = vehicle(1, at, 0, 10.0f, 6.0f, 1.0f);
    r.radius = 20.0f;
    UnitDesc f = vehicle(2, at + V2{60.0f, 0.0f}, 0, 10.0f, 6.0f, 0.5f);
    f.radius = 20.0f;
    CHECK(sys.add_unit(r) && sys.add_unit(f));
    MoveOrder fo;
    fo.units = {2};
    fo.destination = at + V2{60.0f + 2000.0f, 0.0f};
    sys.move(fo, rng, 1);
    MoveOrder ro;
    ro.units = {1};
    ro.destination = at + V2{path, 0.0f};
    sys.move(ro, rng, 1);
}

// The overtake test 0x683260 (a = rear, b = front): (r_a + r_b + dist) / (Va - min(Vb, Va / 2)) <= D / Va. With a long
// path the rear one passes (state 4, master = the rear one, which gets ahead); with a short one it follows (state 6,
// master = the front one). M's own detour does not stamp the Pass record (MOVEMENT 5.4.1 "The stamp in full" item 3:
// the stamp needs the path to land on the held unit, S in state 4); state 6 pushes nothing.
void test_t20_mvm_overtake() {
    ::alamo::test::section("t20_mvm_overtake");
    for (const float path : {500.0f, 150.0f}) {
        MovementSystem sys{MovementConfig{}};
        SimRng rng(321);
        overtake_scene(sys, rng, path);
        CollisionState decided = CollisionState::Active;
        ObjectId master = -1;
        bool passed = false;
        float lhs = 0.0f, rhs = 0.0f;
        uint32_t decided_at = 0, stamp = 0;
        for (uint32_t f = 1; f < 700; ++f) {
            const UnitView r0 = sys.view(1), f0 = sys.view(2);
            sys.tick(f, rng, serial_for);
            const CollisionObject* c = mvm_record(sys, 1, 2);
            if (decided == CollisionState::Active && c && c->state != CollisionState::Active) {
                decided = c->state;
                master = master_of(*c);
                decided_at = f;
                stamp = c->redecide;  // M's own detour push: no stamp (MOVEMENT 5.4.1 item 3)
                // the hand computation from the positions the decision read (the start of this tick)
                const float va = r0.max_speed, vb = f0.max_speed, vs = std::min(vb, 0.5f * va);
                const float dist = distance(r0.pos, f0.pos);
                lhs = (20.0f + 20.0f + dist) / (va - vs);
                rhs = std::max(0.0f, sys.view(1).goal.x - r0.pos.x) / va;
            }
            passed = passed || sys.view(1).pos.x > sys.view(2).pos.x + 1.0f;
        }
        if (path > 200.0f) CHECK(decided == CollisionState::Pass && master == 1 && passed && lhs <= rhs && stamp == 0);
        else CHECK(decided == CollisionState::MovingWithSpeed && master == 2 && !passed && stamp == 0);
        std::printf("t20 overtake: path %.0f -> state %d, master %d, (r + r + d) / (Va - vs) %.1f vs D / Va %.1f, rear got "
                    "ahead %d, re-decide stamp +%d\n",
                    static_cast<double>(path), static_cast<int>(decided), master, static_cast<double>(lhs),
                    static_cast<double>(rhs), passed ? 1 : 0, stamp != 0 ? static_cast<int>(stamp - decided_at) : 0);
    }
}

// Head-on, equal class, one-member formations: Path Around (state 1), the master walks straight on (its lateral offset
// stays exactly 0) and the slave detours around it; both arrive. Serial and 4 workers give the same CRC every tick.
void test_t20_mvm_head_on() {
    ::alamo::test::section("t20_mvm_head_on");
    struct Out {
        std::vector<uint64_t> crc;
        CollisionState decided = CollisionState::Active;
        ObjectId master = -1;
        float lateral = 0.0f;
        bool detoured = false, arrived = false, held = false;
    };
    const auto run = [](const ParallelFor& pf) {
        Out o;
        MovementSystem sys{MovementConfig{}};
        land_system(sys, 120);
        const V2 west = cell_center(20, 60), east = cell_center(60, 60);
        UnitDesc w = vehicle(1, west, 0);
        UnitDesc e = vehicle(2, east, 1);
        e.yaw_deg = 180.0f;
        CHECK(sys.add_unit(w) && sys.add_unit(e));
        SimRng rng(322);
        MoveOrder wo;
        wo.units = {1};
        wo.destination = east + V2{200.0f, 0.0f};
        sys.move(wo, rng, 1);
        MoveOrder eo;
        eo.units = {2};
        eo.destination = west - V2{200.0f, 0.0f};
        sys.move(eo, rng, 1);
        float prev[3] = {0.0f, 0.0f, 0.0f};
        for (uint32_t f = 1; f < 1200; ++f) {
            sys.tick(f, rng, pf);
            o.crc.push_back(units_crc(sys));
            const CollisionObject* c = mvm_record(sys, 1, 2);
            if (o.decided == CollisionState::Active && c && c->state != CollisionState::Active) {
                o.decided = c->state;
                o.master = master_of(*c);
            }
            if (o.master > 0) {
                const ObjectId s = o.master == 1 ? 2 : 1;
                o.lateral = std::max(o.lateral, std::fabs(sys.view(o.master).pos.y - west.y));
                o.detoured = o.detoured || !sys.top_path(s).empty();
                const float v = sys.view(o.master).speed;
                o.held = o.held || (prev[o.master] > 0.5f && v == 0.0f);  // stopped at once at its hold line
                prev[1] = sys.view(1).speed;
                prev[2] = sys.view(2).speed;
            }
        }
        o.arrived = sys.view(1).arrived && sys.view(2).arrived;
        return o;
    };
    const Out s = run(serial_for), p = run(make_thread_pf(4));
    CHECK(s.decided == CollisionState::PathAround && s.master > 0 && s.lateral == 0.0f && s.detoured && s.arrived);
    CHECK(s.crc == p.crc);
    std::printf("t20 head-on: state %d, master %d (lateral offset %.4f, held at once %d), slave detoured %d, both arrived %d, "
                "serial = 4 workers %d\n",
                static_cast<int>(s.decided), s.master, static_cast<double>(s.lateral), s.held ? 1 : 0, s.detoured ? 1 : 0,
                s.arrived ? 1 : 0, s.crc == p.crc ? 1 : 0);
}

// Two vehicles head-on with no order path (halted: s78n t26.4, each coasts in End Move along its last frame's displacement,
// MOVEMENT 3.5.2 C.4, no path record at all), so no Path Around can start (MOVEMENT 1.5.1: no base path): the four attempts fail in the decision
// frame -> Try Again Later with one synced draw, retry - frame in [30, 45]; at the retry frame state 0 with a new draw,
// and the next decision fails again with another. Face to face, both brake to 0 meanwhile. [port] The plan's one-cell
// corridor does not stall in the port: Path Around's pass 2 takes the x 15 crossing edge (1.5.1 item 4) through the
// master, so that detour succeeds.
void test_t20_mvm_try_again() {
    ::alamo::test::section("t20_mvm_try_again");
    MovementSystem sys{MovementConfig{}};
    land_system(sys, 100);
    // s78n t26.4: halted, each coasts v^2 / (2a) - v / 2 = 1.56^2 / (2 x 0.00615 x 1.56) - 0.78 = 126 (MOVEMENT 3.5.2
    // C.4, B.7) from about 259 apart, so they meet still moving and come to rest face to face in contact (0.004 coasted
    // them through each other)
    UnitDesc w = vehicle(1, cell_center(20, 50), 0);
    w.accel = 0.05f;
    w.decel = 0.00615f;
    UnitDesc e = vehicle(2, cell_center(60, 50), 0);
    e.accel = 0.05f;
    e.decel = 0.00615f;
    e.yaw_deg = 180.0f;
    CHECK(sys.add_unit(w) && sys.add_unit(e));
    SimRng rng(323);
    MoveOrder wo;
    wo.units = {1};
    wo.destination = cell_center(90, 50);
    sys.move(wo, rng, 1);
    MoveOrder eo;
    eo.units = {2};
    eo.destination = cell_center(5, 50);
    sys.move(eo, rng, 1);
    uint32_t f = 1;
    for (; f < 600 && distance(sys.view(1).pos, sys.view(2).pos) > 260.0f; ++f) sys.tick(f, rng, serial_for);
    const ObjectId both[] = {1, 2};
    sys.halt(both, f);
    CHECK(!sys.view(1).has_goal && !sys.view(2).has_goal && !sys.view(1).pushed && !sys.view(2).pushed);
    uint32_t first = 0, first_retry = 0, draws_first = 0, failures = 0;
    std::vector<uint32_t> draw_frames;
    bool range_ok = true, ref_ok = true, stood = true;
    for (const uint32_t f0 = f; f < f0 + 400; ++f) {
        SimRng ref = rng;
        const uint64_t d0 = rng.draws();
        sys.tick(f, rng, serial_for);
        const uint64_t dn = rng.draws() - d0;
        if (dn > 0) draw_frames.push_back(f);
        const CollisionObject* c = mvm_record(sys, 1, 2);
        if (!c) continue;
        if (first == 0 && c->state == CollisionState::TryAgainLater) {
            first = f;
            first_retry = c->retry;
            draws_first = static_cast<uint32_t>(dn);
            ref_ok = c->retry == f + 30u + static_cast<uint32_t>(ref.range_int(0, 15));
        }
        if (c->state == CollisionState::TryAgainLater) {
            range_ok = range_ok && c->retry >= f && c->retry <= f + 45u;
            // face to face (neither behind the other yet): both answered 0 from the first service on (the failed
            // decision itself services nothing)
            if (f > first && sys.view(1).pos.x + 1.0f < sys.view(2).pos.x) stood = stood && c->speed_a == 0.0f && c->speed_b == 0.0f;
        }
        failures = c->failures;
    }
    CHECK(first > 0 && draws_first == 1 && ref_ok && first_retry - first >= 30u && first_retry - first <= 45u);
    CHECK(draw_frames.size() >= 3 && draw_frames[0] == first && draw_frames[1] == first_retry && range_ok && stood);
    CHECK(failures >= 2);
    std::printf("t20 try again: state 10 at frame %u (1 draw, retry +%u), draw frames %u %u %u..., failures %u, both answered 0 %d\n",
                first, first_retry - first, draw_frames.size() > 0 ? draw_frames[0] : 0u,
                draw_frames.size() > 1 ? draw_frames[1] : 0u, draw_frames.size() > 2 ? draw_frames[2] : 0u, failures,
                stood ? 1 : 0);
}

// A participant of a MovingVsMoving record that stands on an impassable cell is stopped (its whole path stack cleared)
// and the record ends (validity 0x680650).
void test_t20_mvm_impassable() {
    ::alamo::test::section("t20_mvm_impassable");
    MovementSystem sys{MovementConfig{}};
    SimRng rng(324);
    overtake_scene(sys, rng, 150.0f);
    uint32_t f = 1;
    for (; f < 300 && !mvm_record(sys, 1, 2); ++f) sys.tick(f, rng, serial_for);
    CHECK(mvm_record(sys, 1, 2) != nullptr);
    const V2 p = sys.view(1).pos;
    sys.grid().set_base(world_to_cell(p.x), world_to_cell(p.y), kPassImpassable);
    sys.tick(f, rng, serial_for);
    const UnitView v = sys.view(1);
    const CollisionObject* c = mvm_record(sys, 1, 2);
    CHECK(!v.has_goal && v.speed == 0.0f && !v.pushed && sys.land_path(1).empty() && (!c || c->state == CollisionState::Resolved));
    CHECK(sys.view(2).has_goal);
    std::printf("t20 impassable: rear stopped at frame %u (speed %.2f, goal %d), record %s\n", f, static_cast<double>(v.speed),
                v.has_goal ? 1 : 0, c ? "resolved" : "gone");
}

// Save / load inside state 4 (Pass): the resumed run equals the uninterrupted one tick for tick.
void test_t20_mvm_save_pass() {
    ::alamo::test::section("t20_mvm_save_pass");
    MovementSystem sys{MovementConfig{}};
    SimRng rng(325);
    overtake_scene(sys, rng, 500.0f);
    uint32_t f = 1;
    for (; f < 700; ++f) {
        sys.tick(f, rng, serial_for);
        const CollisionObject* c = mvm_record(sys, 1, 2);
        if (c && c->state == CollisionState::Pass) break;
    }
    const CollisionObject* c = mvm_record(sys, 1, 2);
    CHECK(c && c->state == CollisionState::Pass);
    ByteWriter w;
    sys.write(w);
    const std::vector<uint8_t> bytes = w.bytes();
    MovementSystem copy{MovementConfig{}};
    land_system(copy, 160);
    ByteReader r(bytes);
    CHECK(copy.read(r));
    SimRng rng2 = rng;
    bool same = units_crc(copy) == units_crc(sys);
    uint32_t n = 0;
    for (uint32_t k = f + 1; k < f + 300; ++k, ++n) {
        sys.tick(k, rng, serial_for);
        copy.tick(k, rng2, serial_for);
        same = same && units_crc(copy) == units_crc(sys);
    }
    CHECK(same);
    std::printf("t20 save in Pass: saved at %u (%zu bytes), %u ticks identical %d\n", f, bytes.size(), n, same ? 1 : 0);
}

// A new order (MOVEMENT 5.4.1 "The stamp in full" item 7): to the held unit of a Path Around record (M, owned by no
// record) it goes through the push and stamps the record NOW + 2; to the detourer (S, owned by the record) it destroys
// the detour with no stamp, and the record ends at its next service.
void test_t20_mvm_order_stamp() {
    ::alamo::test::section("t20_mvm_order_stamp");
    for (int to_held = 1; to_held >= 0; --to_held) {
        MovementSystem sys{MovementConfig{}};
        land_system(sys, 120);
        const V2 west = cell_center(20, 60), east = cell_center(60, 60);
        UnitDesc w = vehicle(1, west, 0);
        UnitDesc e = vehicle(2, east, 1);
        e.yaw_deg = 180.0f;
        CHECK(sys.add_unit(w) && sys.add_unit(e));
        SimRng rng(326);
        MoveOrder wo;
        wo.units = {1};
        wo.destination = east + V2{200.0f, 0.0f};
        sys.move(wo, rng, 1);
        MoveOrder eo;
        eo.units = {2};
        eo.destination = west - V2{200.0f, 0.0f};
        sys.move(eo, rng, 1);
        uint32_t f = 1;
        const CollisionObject* c = nullptr;
        for (; f < 600; ++f) {
            sys.tick(f, rng, serial_for);
            c = mvm_record(sys, 1, 2);
            if (c && c->state == CollisionState::PathAround) break;
        }
        CHECK(c && c->state == CollisionState::PathAround && c->redecide == 0);
        if (!c) return;
        const ObjectId m = master_of(*c), s = m == 1 ? 2 : 1;
        CHECK(sys.view(s).pushed && !sys.view(m).pushed);
        const uint32_t seq = c->seq;
        MoveOrder no;
        no.units = {to_held ? m : s};
        no.destination = cell_center(60, 100);
        sys.move(no, rng, ++f);
        c = mvm_record(sys, 1, 2);
        const uint32_t stamp = c ? c->redecide : 0;
        if (to_held) CHECK(c && c->seq == seq && stamp == f + 2);
        else CHECK(c && stamp == 0 && !sys.view(s).pushed);
        sys.tick(f, rng, serial_for);
        c = mvm_record(sys, 1, 2);
        const bool ended = !c || c->seq != seq || c->state == CollisionState::Resolved;
        if (!to_held) CHECK(ended);
        std::printf("t20 order stamp: new order to the %s -> stamp %s, record %s after the next service\n",
                    to_held ? "held M" : "detourer S", stamp ? "NOW + 2" : "none", ended ? "ended" : "kept");
    }
}

// ---------------------------------------------------------------- s78n t23 review item 7: more MovingVsMoving cases
// (The 4432153f regression - a record read one waypoint past a cut path, here pending_tag's push_next == size guard -
// has no direct scene; the save checks of alamo_headless_bench_land_battle_w4 / _w16 caught it and keep covering it.)
// The first decided state and master of the pair's record over a run; `on_tick` sees each tick.
struct Decided {
    CollisionState state = CollisionState::Active;
    ObjectId master = -1;
    uint32_t frame = 0;
};
template <class F>
Decided run_pair(MovementSystem& sys, SimRng& rng, ObjectId x, ObjectId y, uint32_t from, uint32_t to, F&& on_tick) {
    Decided d;
    for (uint32_t f = from; f < to; ++f) {
        sys.tick(f, rng, serial_for);
        const CollisionObject* c = mvm_record(sys, x, y);
        if (d.state == CollisionState::Active && c && c->state != CollisionState::Active) {
            d.state = c->state;
            d.master = master_of(*c);
            d.frame = f;
        }
        on_tick(f, c);
    }
    return d;
}
void order(MovementSystem& sys, SimRng& rng, std::vector<ObjectId> ids, V2 to, uint32_t frame) {
    MoveOrder o;
    o.units = std::move(ids);
    o.destination = to;
    sys.move(o, rng, frame);
}

// Chooser rows (MOVEMENT 5.4.6 "Chooser"): two vehicles crossing at 90 degrees -> Path Around (equal class, no crush);
// a Giant against a Vehicle it cannot crush, head-on -> Path Around with the Giant as M; two halted vehicles head-on
// (no formation: Stop clears it) with max speeds 1.0 / 0.8 -> M = the slower one, and as no Path Around can start
// (no base path) the four attempts fail and leave the roles swapped (state 10, M = the faster one).
void test_t23_chooser_rows() {
    ::alamo::test::section("t23_chooser_rows");
    {
        MovementSystem sys{MovementConfig{}};
        land_system(sys, 120);
        UnitDesc a = vehicle(1, cell_center(20, 60), 0);
        UnitDesc b = vehicle(2, cell_center(50, 30), 1);
        b.yaw_deg = 90.0f;
        CHECK(sys.add_unit(a) && sys.add_unit(b));
        SimRng rng(331);
        order(sys, rng, {1}, cell_center(100, 60), 1);
        order(sys, rng, {2}, cell_center(50, 100), 1);
        const Decided d = run_pair(sys, rng, 1, 2, 1, 600, [](uint32_t, const CollisionObject*) {});
        CHECK(d.state == CollisionState::PathAround);
        std::printf("t23 chooser: crossing at 90 degrees -> port state %d (Path Around), master %d (frame %u)\n",
                    static_cast<int>(d.state), d.master, d.frame);
    }
    {
        MovementSystem sys{MovementConfig{}};
        land_system(sys, 120);
        UnitDesc g = vehicle(1, cell_center(20, 60), 0, 30.0f, 18.0f);
        g.collision_class = kUccGiantVehicle;
        g.radius = 36.0f;
        UnitDesc v = vehicle(2, cell_center(70, 60), 1);
        v.yaw_deg = 180.0f;
        CHECK(sys.add_unit(g) && sys.add_unit(v));
        SimRng rng(332);
        order(sys, rng, {1}, cell_center(110, 60), 1);
        order(sys, rng, {2}, cell_center(5, 60), 1);
        const Decided d = run_pair(sys, rng, 1, 2, 1, 600, [](uint32_t, const CollisionObject*) {});
        CHECK(d.state == CollisionState::PathAround && d.master == 1);
        std::printf("t23 chooser: Giant against a Vehicle -> port state %d (Path Around), master %d (the Giant)\n",
                    static_cast<int>(d.state), d.master);
    }
    {
        MovementSystem sys{MovementConfig{}};
        land_system(sys, 100);
        UnitDesc w = vehicle(1, cell_center(20, 50), 0, 10.0f, 6.0f, 1.0f);
        w.accel = 0.05f;
        w.decel = 0.004f;
        UnitDesc e = vehicle(2, cell_center(60, 50), 0, 10.0f, 6.0f, 0.8f);
        e.accel = 0.05f;
        e.decel = 0.004f;
        e.yaw_deg = 180.0f;
        CHECK(sys.add_unit(w) && sys.add_unit(e));
        SimRng rng(333);
        order(sys, rng, {1}, cell_center(90, 50), 1);
        order(sys, rng, {2}, cell_center(5, 50), 1);
        uint32_t f = 1;
        for (; f < 600 && distance(sys.view(1).pos, sys.view(2).pos) > 260.0f; ++f) sys.tick(f, rng, serial_for);
        const ObjectId both[] = {1, 2};
        sys.halt(both, f);
        // s78o (b0066): halt() keeps each unit's formation id - two separate orders, two formations
        CHECK(sys.view(1).form_id != 0 && sys.view(2).form_id != 0 && sys.view(1).form_id != sys.view(2).form_id);
        const Decided d = run_pair(sys, rng, 1, 2, f, f + 400, [](uint32_t, const CollisionObject*) {});
        CHECK(d.state == CollisionState::TryAgainLater && d.master == 1);
        std::printf("t23 chooser: halted head-on 1.0 / 0.8 -> port state %d (Try Again Later), master %d after the four failed "
                    "attempts (chosen: 2, the slower)\n",
                    static_cast<int>(d.state), d.master);
    }
}

// Same formation (MOVEMENT 5.4.6 chooser step 3): one order for a column of two heading east, the rear one slower and with
// the shorter attack range, so it ranks into the front row (MOVEMENT §4.3 correction [H]: the shortest range leads; s78n t26
// review round 2, the rank key was the max speed before) and its goal lies past the faster one's. Vehicles of one type ->
// state 13: the two swap goals and the record ends in its decision frame (so state 13 is never seen between ticks);
// Infantry -> state 11 with the front-slot unit as M.
void test_t23_same_formation() {
    ::alamo::test::section("t23_same_formation");
    for (int inf = 0; inf < 2; ++inf) {
        MovementSystem sys{MovementConfig{}};
        land_system(sys, 120);
        const V2 base = cell_center(30, 60);
        alamo::test::engagement_of(sys).attack_range_fn = ([](ObjectId id) { return id == 2 ? 80.0f : 100.0f; });  // the rear one ranks in front
        UnitDesc a = inf ? unit(1, base + V2{40.0f, 0.0f}, 2, 1.0f, 6.0f) : vehicle(1, base + V2{40.0f, 0.0f}, 0);
        UnitDesc b = inf ? unit(2, base, 2, 0.8f, 6.0f) : vehicle(2, base, 0, 10.0f, 6.0f, 0.8f);
        CHECK(sys.add_unit(a) && sys.add_unit(b));
        SimRng rng(334);
        order(sys, rng, {1, 2}, cell_center(80, 60), 1);
        const V2 g1 = sys.view(1).goal, g2 = sys.view(2).goal;
        CHECK(sys.view(1).form_id != 0 && sys.view(1).form_id == sys.view(2).form_id && g2.x > g1.x);
        bool swapped = false;
        const Decided d = run_pair(sys, rng, 1, 2, 1, 1200, [&](uint32_t, const CollisionObject*) {
            swapped = swapped || (sys.view(1).goal == g2 && sys.view(2).goal == g1);
        });
        if (inf) CHECK(d.state == CollisionState::FormationFollow && d.master == 2);
        else CHECK(swapped && mvm_record(sys, 1, 2) == nullptr);
        std::printf("t23 same formation: %s, goals %.0f / %.0f -> port state %d seen, master %d (frame %u), goals swapped %d\n",
                    inf ? "infantry" : "vehicles", static_cast<double>(g1.x), static_cast<double>(g2.x), static_cast<int>(d.state),
                    d.master, d.frame, swapped ? 1 : 0);
    }
}

// Save / load inside state 10 (Try Again Later): the resumed run equals the uninterrupted one tick for tick, retry draws
// included. At a passed retry frame the record is in state 0 after its service, flags 0 (MOVEMENT 5.4.6 "Retry frame of
// states 10 and 12": the fold reads state 0's max speed).
void test_t23_save_try_again() {
    ::alamo::test::section("t23_save_try_again");
    MovementSystem sys{MovementConfig{}};
    land_system(sys, 100);
    UnitDesc w = vehicle(1, cell_center(20, 50), 0);
    w.accel = 0.05f;
    w.decel = 0.004f;
    UnitDesc e = vehicle(2, cell_center(60, 50), 0);
    e.accel = 0.05f;
    e.decel = 0.004f;
    e.yaw_deg = 180.0f;
    CHECK(sys.add_unit(w) && sys.add_unit(e));
    SimRng rng(323);
    order(sys, rng, {1}, cell_center(90, 50), 1);
    order(sys, rng, {2}, cell_center(5, 50), 1);
    uint32_t f = 1;
    for (; f < 600 && distance(sys.view(1).pos, sys.view(2).pos) > 260.0f; ++f) sys.tick(f, rng, serial_for);
    const ObjectId both[] = {1, 2};
    sys.halt(both, f);
    for (; f < 1200; ++f) {
        sys.tick(f, rng, serial_for);
        const CollisionObject* c = mvm_record(sys, 1, 2);
        if (c && c->state == CollisionState::TryAgainLater) break;
    }
    const CollisionObject* c = mvm_record(sys, 1, 2);
    CHECK(c && c->state == CollisionState::TryAgainLater);
    if (!c) return;
    const uint32_t retry = c->retry;
    ByteWriter wr;
    sys.write(wr);
    const std::vector<uint8_t> bytes = wr.bytes();
    MovementSystem copy{MovementConfig{}};
    land_system(copy, 100);
    ByteReader r(bytes);
    CHECK(copy.read(r));
    SimRng rng2 = rng;
    bool same = units_crc(copy) == units_crc(sys), retry_state0 = false;
    for (uint32_t k = f + 1; k < f + 200; ++k) {
        sys.tick(k, rng, serial_for);
        copy.tick(k, rng2, serial_for);
        same = same && units_crc(copy) == units_crc(sys) && rng.draws() == rng2.draws();
        if (k == retry) {
            const CollisionObject* q = mvm_record(sys, 1, 2);
            retry_state0 = q && q->state == CollisionState::Active && !q->now_a && !q->now_b;
        }
    }
    CHECK(same && retry_state0);
    std::printf("t23 save in Try Again Later: saved at %u (retry %u), 199 ticks identical %d, state 0 with flags 0 at the "
                "retry frame %d\n",
                f, retry, same ? 1 : 0, retry_state0 ? 1 : 0);
}

// Starvation (MOVEMENT 5.4.6 0x681d30, states 15 / 16): the slower vehicle of one order leads (its front row slot is
// ahead), so it and the rear one are Speed Slave Only with M = the front one; a crawler of another order (max speed
// 0.02, below the 0.05 acceleration default) ahead of it caps it below its acceleration (Moving With Speed Only: it
// does not overtake). The rear one, answered below its own acceleration, starves: 30 frames later it becomes M of state
// 15. Case 1: one rear unit, which also has a record with the crawler - two records; mov1 t01 (b0176, BKR1_RE_ANSWERS Q1
// [H]): 0x505630 counts only the state-15 records whose master is M, so the crawler record does not count and M detours
// (state 16) in the same frame (before mov1 every record of M counted and it stood). Case 2: a Giant leads two rear
// units side by side, each with that one record - state 16 in the same frame, a detour around the Giant.
void test_t23_starvation() {
    ::alamo::test::section("t23_starvation");
    for (int with_detour = 0; with_detour < 2; ++with_detour) {
        MovementSystem sys{MovementConfig{}};
        land_system(sys, 120);
        const V2 at = cell_center(30, 60);
        // s78n t26 review round 2 (MOVEMENT §4.3 "Correction, s78n t19" [H]): rows rank by attack range (shortest in front), no longer by max speed: the slow front one
        // keeps the front row with the shorter range
        alamo::test::engagement_of(sys).attack_range_fn = ([](ObjectId id) { return id == 1 ? 80.0f : 100.0f; });
        UnitDesc front = vehicle(1, at, 0, 10.0f, with_detour ? 24.0f : 6.0f, 0.8f);
        if (with_detour) front.collision_class = kUccGiantVehicle;  // a Giant never overtakes (0x683260)
        CHECK(sys.add_unit(front));
        CHECK(sys.add_unit(vehicle(2, at - V2{40.0f, with_detour ? 16.0f : 0.0f}, 0)));
        if (with_detour) CHECK(sys.add_unit(vehicle(4, at - V2{40.0f, -16.0f}, 0)));
        UnitDesc crawler = vehicle(3, at + V2{80.0f, 0.0f}, 1, 30.0f, with_detour ? 4.0f : 20.0f, 0.02f);
        crawler.radius = 40.0f;  // wide: the front one does not overtake it on the short way to its goal
        CHECK(sys.add_unit(crawler));
        SimRng rng(335);
        order(sys, rng, {3}, cell_center(110, 60), 1);
        if (with_detour) order(sys, rng, {1, 2, 4}, at + V2{400.0f, 0.0f}, 1);
        else order(sys, rng, {1, 2}, at + V2{110.0f, 0.0f}, 1);
        CHECK(sys.view(1).form_id != 0 && sys.view(1).form_id == sys.view(2).form_id);
        uint32_t slave = 0, starved = 0, detour = 0, records = 0;
        ObjectId starved_m = -1;
        bool stood = true;
        for (uint32_t f = 1; f < 1000; ++f) {
            sys.tick(f, rng, serial_for);
            const CollisionObject* c = mvm_record(sys, 1, 2);
            if (!c) continue;
            if (slave == 0 && c->state == CollisionState::SpeedSlave) slave = f;
            const bool st = c->state == CollisionState::Starved || c->state == CollisionState::StarvedDetour;
            if (starved == 0 && st) {
                starved = f;
                starved_m = master_of(*c);
                for (const CollisionObject& k : sys.collisions().list())
                    if (k.a == 2 || k.b == 2) ++records;
            }
            if (detour == 0 && c->state == CollisionState::StarvedDetour) detour = f;
            if (starved > 0 && starved + 5 < f && c->state == CollisionState::Starved) stood = stood && sys.view(2).speed == 0.0f;
        }
        CHECK(slave > 0 && starved > slave + 29 && starved_m == 2);
        if (with_detour) CHECK(records == 1 && detour == starved);
        else CHECK(records == 2 && detour == starved);
        std::printf("t23 starvation: Speed Slave Only from %u, starved at %u (M %d, the rear one, %u record(s)), state 16 at %u%s\n",
                    slave, starved, starved_m, records, detour, with_detour ? "" : stood ? ", stands" : ", moves");
    }
}

// Early pop (MOVEMENT 5.4.6 0x6807e0): head-on vehicles, Path Around. s78n t23 follow-up (RE 2): the gate is rel(M, S)
// after the detour, with the record already in state 1 (MOVEMENT 5.4.1: 0x682d00 writes +0x78 before the set-up), so
// M's cap folds this record's own stored answer - 0 on a fresh record (constructor) - and rel is 2: no gate, the detour
// is walked to its last point. (Before the follow-up the stored answer was "max", the gate was set and the detour was
// dropped early.)
void test_t23_early_pop() {
    ::alamo::test::section("t23_early_pop");
    MovementSystem sys{MovementConfig{}};
    land_system(sys, 120);
    const V2 west = cell_center(20, 60), east = cell_center(60, 60);
    UnitDesc w = vehicle(1, west, 0);
    UnitDesc e = vehicle(2, east, 1);
    e.yaw_deg = 180.0f;
    CHECK(sys.add_unit(w) && sys.add_unit(e));
    SimRng rng(336);
    order(sys, rng, {1}, east + V2{200.0f, 0.0f}, 1);
    order(sys, rng, {2}, west - V2{200.0f, 0.0f}, 1);
    bool gate = false, popped = false;
    float left = 0.0f;
    ObjectId s = -1;
    std::vector<V2> prev;
    for (uint32_t f = 1; f < 900; ++f) {
        sys.tick(f, rng, serial_for);
        const CollisionObject* c = mvm_record(sys, 1, 2);
        if (c && c->state == CollisionState::PathAround) {
            gate = gate || c->early_pop;
            s = master_of(*c) == 1 ? 2 : 1;
        }
        if (s < 0) continue;
        const std::span<const V2> tp = sys.top_path(s);
        if (!popped && !prev.empty() && tp.empty()) {
            left = distance(sys.view(s).pos, prev.back());
            popped = left > 1.0f;
        }
        prev.assign(tp.begin(), tp.end());
    }
    CHECK(s > 0 && !gate && !popped);
    std::printf("t23 early pop: gate %d, detour dropped %.1f before its last point\n", gate ? 1 : 0, static_cast<double>(left));
}

// ---------------------------------------------------------------- s78n t23 follow-ups (MOVEMENT 5.4.6 "State 2 service")
// (a) Two infantry units in different formations (two orders), both heading east, the rear one faster: the chooser's
// relation path gives Speed Slave Only, but the state-2 service re-runs the chooser before its reset and reads the
// record's own stored answer for p0 - 0 from the constructor - so it bails to state 0 writing nothing, every frame: the
// record is never in state 2 between ticks and never slows the rear unit (answered max speed).
// (b) Case 6 resets before its re-run (0x681939): the re-run reads both answers as max speeds, not its own last answer,
// and a bail leaves (max, max, flags 0) - in the short-path overtake scene, every bail from state 6 to state 0.
void test_t23_state2_self_read() {
    ::alamo::test::section("t23_state2_self_read");
    {
        MovementSystem sys{MovementConfig{}};
        land_system(sys, 120);
        const V2 at = cell_center(20, 60);
        CHECK(sys.add_unit(unit(1, at, 2, 1.0f, 6.0f)) && sys.add_unit(unit(2, at + V2{30.0f, 0.0f}, 2, 0.5f, 6.0f)));
        SimRng rng(341);
        order(sys, rng, {2}, cell_center(110, 60), 1);
        order(sys, rng, {1}, cell_center(100, 60), 1);
        CHECK(sys.view(1).form_id != sys.view(2).form_id);
        uint32_t with_record = 0, slave = 0, full_speed = 0;
        for (uint32_t f = 1; f < 400; ++f) {
            sys.tick(f, rng, serial_for);
            const CollisionObject* c = mvm_record(sys, 1, 2);
            if (!c) continue;
            ++with_record;
            if (c->state == CollisionState::SpeedSlave) ++slave;
            if (c->state == CollisionState::Active && sys.view(1).speed >= sys.view(1).max_speed - 1e-4f) ++full_speed;
        }
        CHECK(with_record >= 10 && slave == 0 && full_speed > 0);
        std::printf("t23 state 2 self-read: different-formation infantry pair - %u ticks with a record, %u in state 2, %u at full "
                    "speed while undecided\n",
                    with_record, slave, full_speed);
    }
    {
        MovementSystem sys{MovementConfig{}};
        SimRng rng(321);
        overtake_scene(sys, rng, 150.0f);
        uint32_t six = 0, bails = 0, clean = 0;
        bool was_six = false;
        for (uint32_t f = 1; f < 700; ++f) {
            sys.tick(f, rng, serial_for);
            const CollisionObject* c = mvm_record(sys, 1, 2);
            if (c && c->state == CollisionState::MovingWithSpeed) ++six;
            if (c && was_six && c->state == CollisionState::Active) {
                ++bails;
                if (c->speed_a > 1e38f && c->speed_b > 1e38f && !c->now_a && !c->now_b) ++clean;
            }
            was_six = c && c->state == CollisionState::MovingWithSpeed;
        }
        CHECK(six >= 10 && bails >= 1 && clean == bails);
        std::printf("t23 case 6 reset first: %u ticks in Moving With Speed Only, %u bail(s) to state 0, %u with both answers max "
                    "and flags 0\n",
                    six, bails, clean);
    }
}


// mov2 t03 (MOVEMENT §4.2 step 2 item 7 [H], 0x43c1b0): the recruitment distance d_min is 3-D; a candidate on a team that
// already joined the formation joins at once (test 1); a candidate with a team needs a seed with one (test 7); test 12's
// line / path runs candidate -> seed with the 1.4 x d_min budget.
void test_mov2_recruitment() {
    MovementConstants k;
    k.should_infantry_teams_split_across_formations = false;
    std::vector<FormationMember> m(2);
    for (uint32_t i = 0; i < 2; ++i) {
        m[i].id = static_cast<ObjectId>(i + 1);
        m[i].max_speed = 1.0f;
        m[i].type_name = "Rebel_Infantry";
        m[i].collision_class = 5;
    }
    m[1].pos = {50.0f, 0.0f};  // 2-D 50 <= 60
    const uint32_t bucket[] = {0, 1};
    CHECK(split_into_formations(m, bucket, k).size() == 1);
    m[1].z = 40.0f;  // 3-D sqrt(50^2 + 40^2) = 64 > 60: the candidate seeds its own formation
    CHECK(split_into_formations(m, bucket, k).size() == 2);
    // test 1: the same team joins at once - past the distance, and past the FoC split clause of test 3
    m[0].team = 7;
    m[1].team = 7;
    m[1].pos = {500.0f, 0.0f};
    const MovementConstants foc;
    CHECK(foc.should_infantry_teams_split_across_formations);
    CHECK(split_into_formations(m, bucket, foc).size() == 1);
    // test 7: a candidate with a team and a seed without one
    m[0].team = -1;
    m[1].team = 8;
    m[1].pos = {10.0f, 0.0f};
    m[1].z = 0.0f;
    CHECK(split_into_formations(m, bucket, k).size() == 2);
    m[1].team = -1;
    // test 12: the reach callback gets (seed, candidate, d_min)
    std::vector<FormationMember> r(3, m[0]);
    for (uint32_t i = 0; i < 3; ++i) r[i].id = static_cast<ObjectId>(i + 1);
    r[0].pos = {0.0f, 0.0f};
    r[1].pos = {30.0f, 0.0f};
    r[2].pos = {50.0f, 0.0f};
    const uint32_t b3[] = {0, 1, 2};
    std::vector<float> dmins;
    std::vector<uint32_t> seeds;
    const auto reach = [&](uint32_t sd, uint32_t, float d) {
        seeds.push_back(sd);
        dmins.push_back(d);
        return true;
    };
    CHECK(split_into_formations(r, b3, k, {}, 0.0f, reach).size() == 1);
    // the seed nearest the centroid (26.7, 0) is r[1]; r[0] (d_min 30) then r[2] (nearest member r[1]: 20)
    CHECK(seeds.size() == 2 && seeds[0] == 1 && seeds[1] == 1);
    CHECK(dmins.size() == 2 && dmins[0] == 30.0f && dmins[1] == 20.0f);
}


// mov2 t01 (MOVEMENT §4.3.3 items 1 - 4 [H]): the members of a land formation walk copies of one formation path (shared
// waypoints, not lanes); the lane term steers each onto its slot lateral, and each stops on the last waypoint line moved by
// its end shift: at its slot point (the goal the order gave it), with no search of its own.
void test_mov2_formation_path() {
    ::alamo::test::section("mov2_formation_path");
    MovementSystem sys{MovementConfig{}};
    land_system(sys, 100);
    const ObjectId ids[] = {1, 2, 3};
    for (ObjectId i : ids) CHECK(sys.add_unit(unit(i, cell_center(15, 48 + 2 * i), 2, 1.0f, 6.0f)));
    SimRng rng(77);
    MoveOrder o;
    o.units = {1, 2, 3};
    o.destination = cell_center(80, 50);
    sys.move(o, rng, 1);
    uint32_t f = 1;
    bool planned = false;
    for (; f < 200 && !planned; ++f) {
        sys.tick(f, rng, serial_for);
        planned = !sys.land_path(1).empty() && !sys.land_path(2).empty() && !sys.land_path(3).empty();
    }
    CHECK(planned);
    CHECK(sys.view(1).form_id != 0 && sys.view(2).form_id == sys.view(1).form_id && sys.view(3).form_id == sys.view(1).form_id);
    // one formation path: every member's path has the same points (a copy of W), not its own lane
    const std::vector<V2> w(sys.land_path(1).begin(), sys.land_path(1).end());
    bool shared = true;
    for (ObjectId i : ids) {
        const std::span<const V2> p = sys.land_path(i);
        shared = shared && p.size() == w.size() && std::equal(p.begin(), p.end(), w.begin());
    }
    CHECK(shared && w.size() >= 2);
    // the goals differ (each member's slot point) and none is W's end
    const V2 g1 = sys.view(1).goal, g2 = sys.view(2).goal, g3 = sys.view(3).goal;
    CHECK(!(g1 == g2) && !(g2 == g3) && !(g1 == g3));
    // while walking the middle of the path, each member holds its slot lateral (its goal's side offset from W's line) by
    // the lane term alone: the waypoints are W's
    const V2 wd = normalize_retail(w.back() - w.front());
    const auto side = [&](V2 p) { return cross(wd, p - w.front()); };
    float lat_err = 0.0f;
    uint32_t mid = 0;
    for (; f < 1500; ++f) {
        sys.tick(f, rng, serial_for);
        for (ObjectId i : ids) {
            const UnitView v = sys.view(i);
            const float along = dot(v.pos - w.front(), wd), len = distance(w.front(), w.back());
            if (along > 0.4f * len && along < 0.7f * len) {
                lat_err = std::max(lat_err, std::fabs(side(v.pos) - side(v.goal)));
                ++mid;
            }
        }
        if (sys.view(1).arrived && sys.view(2).arrived && sys.view(3).arrived) break;
    }
    std::printf("mov2 t01 formation path: mid-path lateral error %.3f over %u samples\n", static_cast<double>(lat_err), mid);
    CHECK(mid > 30 && lat_err < 1.0f);
    float worst = 0.0f;
    for (ObjectId i : ids) {
        const UnitView v = sys.view(i);
        CHECK(v.arrived && !v.has_goal);
        worst = std::max(worst, distance(v.pos, v.goal));
    }
    std::printf("mov2 t01 formation path: %zu shared waypoints, all arrived by frame %u, worst %.2f from the slot point\n", w.size(), f,
                static_cast<double>(worst));
    // stopped within the End Move coast of each slot point (the lane term's dead band 0.1 sideways, the coast along)
    CHECK(worst < 2.0f);
}


// mov2 t10 (MOVEMENT "Retail measurement of MovingVsMoving head-on pairs", run 20261116-022245): two units of one player
// ordered at once toward each other's start, 600 apart on open ground - T4B / T4B on one line, T4B / T4B 12 to the side,
// AT-ST / T4B. Retail: neither stands at speed 0 before its goal or leaves 0x13; one swerves at full speed, the other
// slows to 0.11 - 0.22 for 27 - 66 frames and runs on. Type values from GROUNDVEHICLES.XML (T4B_Tank: Max_Speed 0.7,
// OverrideAcceleration / Deceleration 0.05, turn 6, hard extents 20 x 22 x Scale_Factor 0.85, Vehicle; AT_ST_Walker:
// 1.2, turn 7.5, 8 x 6 x 1.6, Large Vehicle).
struct HeadOnOut {
    uint32_t zero[2] = {0, 0};   // frames at speed 0 with a goal, after the unit first moved
    float vlow[2] = {9.0f, 9.0f};  // lowest speed on the way (after its first full-speed frame, 150+ from its goal)
    uint32_t below_half[2] = {0, 0};  // frames below 0.5 on the way
    uint32_t slowed[2] = {0, 0};      // frames below its max speed on the way
    float side[2] = {0.0f, 0.0f};  // largest distance off its start line
    bool arrived = false;
    CollisionState decided = CollisionState::Active;
};
HeadOnOut run_head_on(bool atst, float offset) {
    HeadOnOut o;
    MovementSystem sys{MovementConfig{}};
    land_system(sys, 120);
    const V2 west = cell_center(30, 60), east = west + V2{600.0f, offset};
    const auto t4b = [](ObjectId id, V2 at) {
        UnitDesc d = vehicle(id, at, 0, 20.0f * 0.85f, 22.0f * 0.85f, 0.7f);
        d.accel = d.decel = 0.05f;
        d.turn_rate = d.alt_turn_rate = 6.0f;
        d.type_name = "T4B_Tank";
        return d;
    };
    UnitDesc a = t4b(1, west);
    UnitDesc b = t4b(2, east);
    if (atst) {
        b = vehicle(2, east, 0, 8.0f * 1.6f, 6.0f * 1.6f, 1.2f);
        b.collision_class = kUccLargeVehicle;
        b.turn_rate = b.alt_turn_rate = 7.5f;
        b.type_name = "AT_ST_Walker";
    }
    b.yaw_deg = 180.0f;
    CHECK(sys.add_unit(a) && sys.add_unit(b));
    SimRng rng(1016);
    MoveOrder ao;
    ao.units = {1};
    ao.destination = east;
    sys.move(ao, rng, 1);
    MoveOrder bo;
    bo.units = {2};
    bo.destination = west;
    sys.move(bo, rng, 1);
    bool full[2] = {false, false};
    const V2 start[2] = {west, east};
    for (uint32_t f = 1; f < 2000; ++f) {
        sys.tick(f, rng, serial_for);
        const CollisionObject* c = mvm_record(sys, 1, 2);
        if (o.decided == CollisionState::Active && c && c->state != CollisionState::Active) o.decided = c->state;
        for (int k = 0; k < 2; ++k) {
            const UnitView v = sys.view(static_cast<ObjectId>(k + 1));
            o.side[k] = std::max(o.side[k], std::fabs(v.pos.y - start[k].y));
            if (!v.has_goal) continue;
            if (v.speed >= v.max_speed) full[k] = true;
            if (!full[k]) continue;
            if (v.speed == 0.0f) ++o.zero[k];
            if (distance(v.pos, start[1 - k]) < 150.0f) continue;  // the goal's brake is not part of the meeting
            o.vlow[k] = std::min(o.vlow[k], v.speed);
            if (v.speed < 0.5f) ++o.below_half[k];
            if (v.speed < v.max_speed) ++o.slowed[k];
        }
        if (sys.view(1).arrived && sys.view(2).arrived) {
            o.arrived = true;
            break;
        }
    }
    return o;
}

void test_mov2_head_on_pairs() {
    ::alamo::test::section("mov2_head_on_pairs");
    const struct {
        const char* name;
        bool atst;
        float offset;
    } cases[] = {{"T4B / T4B", false, 0.0f}, {"T4B / T4B offset 12", false, 12.0f}, {"AT-ST / T4B", true, 0.0f}};
    for (const auto& c : cases) {
        const HeadOnOut o = run_head_on(c.atst, c.offset);
        std::printf("mov2 t10 head-on %s: decided %d, zero-speed frames %u / %u, lowest speed %.3f / %.3f, slowed %u / %u "
                    "frames (below 0.5: %u / %u), off line %.1f / %.1f, both arrived %d\n",
                    c.name, static_cast<int>(o.decided), o.zero[0], o.zero[1], static_cast<double>(o.vlow[0]),
                    static_cast<double>(o.vlow[1]), o.slowed[0], o.slowed[1], o.below_half[0], o.below_half[1],
                    static_cast<double>(o.side[0]), static_cast<double>(o.side[1]), o.arrived ? 1 : 0);
        // the retail pattern (four trials): no stand at speed 0, one unit swerves at full speed (up to ~40 off its line),
        // the other slows to 0.11 - 0.22 for 27 - 66 frames; port 2026-11-20: 0.107 - 0.153 for 42 - 66 frames, swerve
        // 31 - 47
        const int sw = o.vlow[0] >= o.vlow[1] ? 0 : 1;
        // Q2 [H]: the pair is decided Path Around (state 1); S detours at full speed, M slows at the slave speed
        CHECK(o.decided == CollisionState::PathAround);
        CHECK(o.arrived && o.zero[0] == 0 && o.zero[1] == 0);
        CHECK(o.slowed[sw] == 0 && o.side[sw] > 20.0f && o.side[sw] < 50.0f);
        CHECK(o.vlow[1 - sw] > 0.1f && o.vlow[1 - sw] < 0.25f && o.slowed[1 - sw] >= 20 && o.slowed[1 - sw] <= 70);
    }
}


// mov2 t09 (MOVEMENT §3.5.2 G correction, MOV2_RE_QUESTIONS Q1 [H]): 0x410170 aligns a new path when |delta| <= R or
// |delta| <= CE (type Close_Enough_Angle_For_Move_Start, else GameConstants CloseEnoughAngleForMoveStart = 180), with
// waypoint 0's fraction > 0 - so a T4B (no type CE) re-ordered at cruise 90 / 180 degrees off keeps 0x13 and turns on
// the move, as retail did in 5 of 6 re-orders; a type with CE 30 (the AT-AT) coasts to a stop and turns in place.
void test_mov2_reorder_at_speed() {
    ::alamo::test::section("mov2_reorder_at_speed");
    const auto run = [](float turn_deg, float ce, bool& kept, bool& coasted) {
        MovementSystem sys{MovementConfig{}};
        land_system(sys, 120);
        const V2 at = cell_center(30, 60);
        UnitDesc d = vehicle(1, at, 0, 20.0f * 0.85f, 22.0f * 0.85f, 0.7f);
        d.accel = d.decel = 0.05f;
        d.turn_rate = d.alt_turn_rate = 6.0f;
        d.close_enough_angle = ce;
        CHECK(sys.add_unit(d));
        SimRng rng(909);
        MoveOrder o;
        o.units = {1};
        o.destination = at + V2{1200.0f, 0.0f};
        sys.move(o, rng, 1);
        uint32_t f = 1;
        for (; f < 400 && !(sys.view(1).speed >= sys.view(1).max_speed && sys.view(1).pos.x > at.x + 200.0f); ++f)
            sys.tick(f, rng, serial_for);
        const V2 p = sys.view(1).pos;
        const float r = turn_deg * 0.017453292f;
        MoveOrder b;
        b.units = {1};
        b.destination = p + V2{400.0f * std::cos(r), 400.0f * std::sin(r)};
        sys.move(b, rng, f);
        kept = true;
        coasted = false;
        for (uint32_t n = 0; n < 60; ++n, ++f) {
            sys.tick(f, rng, serial_for);
            const UnitView v = sys.view(1);
            kept = kept && v.walk_state == LocoState::WalkMoving && v.speed > 0.0f;
            coasted = coasted || v.walk_state == LocoState::WalkEndMove || v.speed == 0.0f;
        }
    };
    for (const float a : {90.0f, 180.0f}) {
        bool kept = false, coasted = false;
        run(a, 0.0f, kept, coasted);
        std::printf("mov2 t09 T4B re-order %.0f deg at cruise: kept 0x13 %d, braked %d\n", static_cast<double>(a), kept ? 1 : 0,
                    coasted ? 1 : 0);
        CHECK(kept && !coasted);
        run(a, 30.0f, kept, coasted);  // a type CE of 30 (AT_AT_Walker): not aligned, the coast and the turn in place
        CHECK(!kept && coasted);
    }
}

}  // namespace

// ---------------------------------------------------------------- s78n t24 (RE f8afef8d): the role slots
// A MovingVsMoving record's stored answers (+0x84 M / +0x88 S) and flags (+0xbd / +0xbe) belong to the role slots: a role
// change (the decision's chooser result, the Pass failure swap, the Path Around retries) hands the old M slot's values
// to the new M. The port keeps them per participant, so set_master / swap_roles swap them; a StoppedVsStopped
// "nobody" (master 2) and an unchanged master leave them.
void test_t24_role_slots() {
    ::alamo::test::section("t24_role_slots");
    CollisionObject c;
    c.kind = CollisionKind::MovingVsMoving;
    c.master = 0;
    c.speed_a = 0.25f;  // M slot (a is M)
    c.speed_b = 3.0e38f;
    c.now_a = true;
    c.now_b = false;
    c.set_master(0);
    CHECK(c.master == 0 && c.speed_a == 0.25f && c.speed_b == 3.0e38f && c.now_a && !c.now_b);
    c.set_master(1);  // b becomes M: it now reads the M slot's 0.25 and flag
    CHECK(c.master == 1 && c.speed_b == 0.25f && c.speed_a == 3.0e38f && c.now_b && !c.now_a);
    c.swap_roles();
    CHECK(c.master == 0 && c.speed_a == 0.25f && c.speed_b == 3.0e38f && c.now_a && !c.now_b);
    c.swap_roles();
    c.swap_roles();
    c.swap_roles();  // three swaps (the Path Around retries 2 - 4): the roles and the slots end swapped
    CHECK(c.master == 1 && c.speed_b == 0.25f && c.speed_a == 3.0e38f);
    CollisionObject s;
    s.kind = CollisionKind::StoppedVsStopped;
    s.master = 2;
    s.speed_a = 1.0f;
    s.speed_b = 2.0f;
    s.set_master(0);
    CHECK(s.master == 0 && s.speed_a == 1.0f && s.speed_b == 2.0f);
}

// mov1 t02 (b0179, BKR1_RE_ANSWERS Q3 [H]): the head-on tie-break of two equal-class formations compares 0x41a5c0. Two
// small vehicles (half extents 10 x 6) in one formation against one long vehicle (40 x 6) alone: by member count the pair
// would be M; by 0x41a5c0 the pair is max(2 x (0 + 20 / 20) + 0.25, sqrt(1.8 x 2 x 1 x 0.6)) = 2.25 and the long one
// max(1 x 4 + 0.25, sqrt(1.8 x 4 x 0.6)) = 4.25 - the long one is M (it walks on, the small one detours).
void test_mov1_head_on_extent() {
    ::alamo::test::section("mov1_head_on_extent");
    MovementSystem sys{MovementConfig{}};
    land_system(sys, 120);
    const V2 w = cell_center(20, 60), e = cell_center(55, 60);
    CHECK(sys.add_unit(vehicle(1, w, 0)) && sys.add_unit(vehicle(2, w + V2{0.0f, 30.0f}, 0)));
    UnitDesc big = vehicle(3, e, 1, 40.0f, 6.0f);
    big.yaw_deg = 180.0f;
    CHECK(sys.add_unit(big));
    SimRng rng(902);
    order(sys, rng, {1, 2}, cell_center(95, 60), 1);
    order(sys, rng, {3}, cell_center(5, 60), 1);
    CHECK(sys.view(1).form_id != 0 && sys.view(1).form_id == sys.view(2).form_id && sys.view(3).form_id != sys.view(1).form_id);
    CollisionState decided = CollisionState::Active;
    ObjectId master = -1;
    for (uint32_t f = 1; f < 600 && decided == CollisionState::Active; ++f) {
        sys.tick(f, rng, serial_for);
        for (const ObjectId small : {1, 2}) {
            const CollisionObject* c = mvm_record(sys, small, 3);
            if (c && c->state != CollisionState::Active && decided == CollisionState::Active) {
                decided = c->state;
                master = master_of(*c);
            }
        }
    }
    std::printf("mov1 head-on extent: state %d, master %d (the long vehicle is 3)\n", static_cast<int>(decided), master);
    CHECK(decided == CollisionState::PathAround && master == 3);
}

// mov1 t05 (b0186, BKR1_RE_ANSWERS Q3 [H]): within one coordinator the overtake 0x683260 needs rank_b > rank_a. One order
// with a fast rear vehicle and a slow front one, each its own formation (their footprint area makes two k-means buckets),
// a long way: the time test passes (before mov1: Pass), but the front formation's goal lies ahead (rank 0) of the rear's
// (rank 1), so the rear one is slaved (Moving With Speed Only).
void test_mov1_overtake_rank() {
    ::alamo::test::section("mov1_overtake_rank");
    MovementSystem sys{MovementConfig{}};
    land_system(sys, 160);
    const V2 at = cell_center(20, 80);
    UnitDesc r = vehicle(1, at, 0, 10.0f, 6.0f, 1.0f);
    r.radius = 30.0f;
    UnitDesc f = vehicle(2, at + V2{100.0f, 0.0f}, 0, 10.0f, 6.0f, 0.5f);
    f.radius = 30.0f;
    CHECK(sys.add_unit(r) && sys.add_unit(f));
    SimRng rng(903);
    order(sys, rng, {1, 2}, at + V2{2000.0f, 0.0f}, 1);
    const UnitView v1 = sys.view(1), v2 = sys.view(2);
    CHECK(v1.form_id != 0 && v2.form_id != 0 && v1.form_id != v2.form_id && v1.coord_id != 0 && v1.coord_id == v2.coord_id);
    CollisionState decided = CollisionState::Active;
    ObjectId master = -1;
    for (uint32_t fr = 1; fr < 900 && decided == CollisionState::Active; ++fr) {
        sys.tick(fr, rng, serial_for);
        const CollisionObject* c = mvm_record(sys, 1, 2);
        if (c && c->state != CollisionState::Active) {
            decided = c->state;
            master = master_of(*c);
        }
    }
    std::printf("mov1 overtake rank: ranks rear %d / front %d, state %d, master %d\n", v1.form_rank, v2.form_rank,
                static_cast<int>(decided), master);
    CHECK(v1.form_rank == 1 && v2.form_rank == 0);
    CHECK(decided == CollisionState::MovingWithSpeed && master == 2);
    // a queued order adds a block to the existing formations [M]: each unit keeps its coordinator, rank and member 0
    MoveOrder q;
    q.units = {1, 2};
    q.destination = at + V2{2000.0f, 400.0f};
    q.flags = kMoveFlagQueue;
    sys.move(q, rng, 900);
    const UnitView q1 = sys.view(1), q2 = sys.view(2);
    CHECK(q1.coord_id == v1.coord_id && q2.coord_id == v2.coord_id && q1.form_rank == 1 && q2.form_rank == 0);
}

// mov1 t06 (b0189, BKR2_RE_ANSWERS Q3 [H]): MovingVsStopped step 10's 0x685490. A corridor five cells wide is closed by
// two standing vehicles, b in its upper half (on the mover's line) and c in its lower half. Retail's Path Around crosses
// an obstacle in its second pass, so the scene's own decision detours (state 1); the probe is run through the test seam on
// the live record: its search (flags 2) keeps b (it shares the record) and ignores c (no record with the mover), so its
// line runs through c's half and meets c's footprint - a hostile c gives the mover's formation an attack block on c; a
// friendly c gives nothing (state 7 would follow).
void test_mov1_step10_attack() {
    ::alamo::test::section("mov1_step10_attack");
    for (const int32_t c_owner : {1, 0}) {
        MovementSystem sys{MovementConfig{}};
        land_system(sys, 60);
        for (int32_t y = 0; y < 60; ++y)
            for (int32_t x = 0; x < 60; ++x)
                if (y < 18 || y > 22 || x < 2 || x > 57) sys.grid().set_base(x, y, kPassImpassable);
        const float yc = cell_center(0, 20).y;
        CHECK(sys.add_unit(vehicle(1, {cell_center(5, 20).x, yc - 25.0f}, 0)));
        CHECK(sys.add_unit(vehicle(2, {cell_center(30, 20).x, yc - 25.0f}, 1, 10.0f, 25.0f)));        // b
        CHECK(sys.add_unit(vehicle(3, {cell_center(30, 20).x, yc + 25.0f}, c_owner, 10.0f, 25.0f)));  // c
        SimRng rng(904);
        order(sys, rng, {1}, {cell_center(55, 20).x, yc - 25.0f}, 1);
        bool probed = false, attacked = false;
        std::vector<MovementSystem::FormationAttack> got;
        for (uint32_t f = 1; f < 400 && !probed; ++f) {
            sys.tick(f, rng, serial_for);
            for (const CollisionObject& c : sys.collisions().list())
                if (c.kind == CollisionKind::MovingVsStopped && c.a == 1 && c.b == 2 && c.state != CollisionState::Active) {
                    probed = true;
                    attacked = sys.test_step10_probe(1, 2, f);
                    got.assign(sys.formation_attacks().begin(), sys.formation_attacks().end());
                    break;
                }
        }
        std::printf("mov1 step 10: c owner %d - probe run %d, attack block %d (%zu)\n", c_owner, probed ? 1 : 0, attacked ? 1 : 0, got.size());
        CHECK(probed);
        if (c_owner == 1) CHECK(attacked && got.size() == 1 && got[0].mover == 1 && got[0].target == 3);
        else CHECK(!attacked && got.empty());
    }
}

// mov2 t04 (b0187, BKR1_RE_ANSWERS Q6 [H]): retail's plans read the tracker flags of the update before this frame's walks,
// so a unit that arrives in a walk still counts as Moving for the next plans' corridor test. The port's snapshot is taken
// after the plans, before the walk: in the tick whose walk ends the unit's move the flag still says Moving (taken before
// that walk); the next tick's snapshot says Static.
void test_mov2_tracker_snapshot() {
    ::alamo::test::section("mov2_tracker_snapshot");
    MovementSystem sys{MovementConfig{}};
    land_system(sys, 60);
    CHECK(sys.add_unit(vehicle(1, cell_center(10, 30), 0)));
    SimRng rng(905);
    CHECK(!sys.view(1).trk_static);  // not in the tracker before its first tick
    sys.tick(1, rng, serial_for);
    CHECK(sys.view(1).trk_static);   // standing
    order(sys, rng, {1}, cell_center(20, 30), 2);
    uint32_t arrived = 0;
    bool moving_at_arrival = false, static_after = false;
    for (uint32_t f = 2; f < 600 && !arrived; ++f) {
        const bool was_moving = sys.view(1).has_goal;
        sys.tick(f, rng, serial_for);
        const UnitView v = sys.view(1);
        if (was_moving && !v.has_goal && !(v.speed > 0.0f)) {
            arrived = f;
            moving_at_arrival = !v.trk_static;
            sys.tick(f + 1, rng, serial_for);
            static_after = sys.view(1).trk_static;
        }
    }
    std::printf("mov2 tracker snapshot: arrived at %u, Moving in that tick's snapshot %d, Static in the next %d\n", arrived,
                moving_at_arrival ? 1 : 0, static_after ? 1 : 0);
    CHECK(arrived != 0 && moving_at_arrival && static_after);
}

int main() {
    test_step0_halt_brake();
    test_step0_crush_no_locomotor();
    test_step0_squad_final_facing();
    test_t20_deployed_width();
    test_t20_mvm_overtake();
    test_t20_mvm_head_on();
    test_t20_mvm_try_again();
    test_t20_mvm_impassable();
    test_t20_mvm_save_pass();
    test_t20_mvm_order_stamp();
    test_t23_chooser_rows();
    test_t23_same_formation();
    test_t23_save_try_again();
    test_t23_starvation();
    test_t23_early_pop();
    test_t23_state2_self_read();
    test_t24_role_slots();
    test_mov1_head_on_extent();
    test_mov1_overtake_rank();
    test_mov1_step10_attack();
    test_mov2_tracker_snapshot();
    test_mov2_recruitment();
    test_mov2_formation_path();
    test_mov2_head_on_pairs();
    test_mov2_reorder_at_speed();
    return ::alamo::test::report("alamo_tactical_movement_b3_tests");
}
