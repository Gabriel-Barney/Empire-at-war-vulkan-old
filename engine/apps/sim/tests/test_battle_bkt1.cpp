// tst2 t03 (backlog b0168): bkt1 test coverage, the cases the 2026-11-12 review found untested. Expected values come
// from the specs, not from the code: BKT1_ATTACK_RE §1.1 / §1.2 (engagement range 0x4f6e40, staging 0x40e260, the
// stand-off search 0x440f90), FIGHTER_FLYING §4.11 C 3 / G 1 / G 3 (land guard follow, attack formation lead) and the
// recorded [port] choices of docs/PHASE4.md "BKT1" (t03 lead skips locked members, all locked waits; t07 third parties
// idle). Synthetic S10 data (no game data needed):
//   * staging (t01): the ring slide at the map edge (the on-line spot is off the map: the stand-off point turns
//     counter-clockwise around M, >= d from T, inside the map), the open-ground control (on the T -> M line, overshooting
//     d by less than one ring step), a search failure (d beyond 20 rings: G stays at T's point), the crush case (one
//     member with SQUASH: Is_Squashable, or Is_Supercrusher + Is_Squashable_By_Supercrusher -> G = T; two members, no
//     SQUASH, or a T squashable only by a supercrusher -> staged) and Activation_Min_Range picking M (a pending targeted
//     ability's min range 150 beats a weapon min range 100: M is the ability user and d = 150 + 0.2 x (250 - 150));
//   * the Activation_Max_Range engage clause (t01): a pending ability caps the engagement range at min(range, 200) with
//     no target extent and no Land_Guard_Range cap, seen as the arc slot radius of the land attack / guard plan;
//   * the attack formation lead (t03): a movement-locked would-be lead is skipped (the formation is still checked and
//     re-planned, the locked member keeps its order); every member locked: no check until one is unlocked;
//   * the land guard follow (t02): R = min(half the lead's range, Land_Guard_Range 150) (lead range 400: a 170 move
//     re-paths, 130 does not); a guarded object hidden after arrival: d >= 40 plans toward its last-observed point,
//     then d < 40 turns the guard into a position guard (the follow ends);
//   * a third-party unit on land idles (t07): the stand-in orders drive the attacker and the defender only.
#include "test_battles_ai_common.hpp"

#include <cstring>
#include <functional>

namespace {

using TA = alamo::sim::TacticalBattleTestAccess;

float dist3(Vec3 a, Vec3 b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
float dist2(Vec3 a, Vec3 b) {
    const float dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// A standalone (no TEAM) armed land unit: weapon range `maxr`, min range `minr`, hard half extents hx x hy (default 3 x 3),
// plus `extra` tags.
std::string land_unit(const char* name, float minr, float maxr, const char* land_behavior, const char* extra, int hx = 3, int hy = 3) {
    const std::string min_tag =
        minr > 0.0f ? "<Targeting_Min_Attack_Distance>" + std::to_string(static_cast<int>(minr)) + "</Targeting_Min_Attack_Distance>" : "";
    char buf[2048];
    std::snprintf(buf, sizeof buf,
                  " <GroundVehicle Name=\"%s\"><Tactical_Health>100000</Tactical_Health><Max_Speed>1.0</Max_Speed>"
                  "<AI_Combat_Power>100</AI_Combat_Power><CategoryMask>Vehicle</CategoryMask><MovementClass>Tracked</MovementClass>"
                  "<Targeting_Max_Attack_Distance>%.0f</Targeting_Max_Attack_Distance>%s"
                  "<Projectile_Types>S10_Blaster_Proj</Projectile_Types><Projectile_Fire_Recharge_Seconds>1.0</Projectile_Fire_Recharge_Seconds>"
                  "<Projectile_Fire_Pulse_Count>1</Projectile_Fire_Pulse_Count><Behavior>SELECTABLE, WEAPON</Behavior>"
                  "<Collidable_By_Projectile_Living>Yes</Collidable_By_Projectile_Living><LandBehavior>%s</LandBehavior>"
                  "<Custom_Soft_Footprint_Radius>3</Custom_Soft_Footprint_Radius><Custom_Hard_XExtent>%d</Custom_Hard_XExtent>"
                  "<Custom_Hard_YExtent>%d</Custom_Hard_YExtent><Tactical_Bribe_Cost>40</Tactical_Bribe_Cost>%s</GroundVehicle>\n"
                  " <GroundCompany Name=\"%s_Co\"><Company_Units>%s</Company_Units><Population_Value>1</Population_Value></GroundCompany>\n",
                  name, static_cast<double>(maxr), min_tag.c_str(), land_behavior, hx, hy, extra, name, name);
    return buf;
}

std::string bkt1_types(std::string t) {
    const size_t end = t.find("</S10>");
    if (end == std::string::npos) return t;
    std::string add;
    add += land_unit("S10_Ranger", 100.0f, 250.0f, "WALK_LOCOMOTOR", "<Is_Squashable>Yes</Is_Squashable>");
    add += land_unit("S10_Far", 100.0f, 2000.0f, "WALK_LOCOMOTOR", "");
    // cmb3 t02: a target the fog's object test can hide (HIDE_WHEN_FOGGED; a plain vehicle is never hidden)
    add += land_unit("S10_Lurker", 0.0f, 250.0f, "WALK_LOCOMOTOR, HIDE_WHEN_FOGGED", "");
    add += land_unit("S10_Crusher", 100.0f, 250.0f, "WALK_LOCOMOTOR, SQUASH", "<Is_Squashable_By_Supercrusher>Yes</Is_Squashable_By_Supercrusher>");
    // hard half extents 5 x 2 (soft radius 3): the engage-clause target, so min(hx, hy) = 2 is told apart from the others
    add += land_unit("S10_Super", 100.0f, 250.0f, "WALK_LOCOMOTOR, SQUASH", "<Is_Supercrusher>Yes</Is_Supercrusher>", 5, 2);
    add += land_unit("S10_Briber", 0.0f, 250.0f, "WALK_LOCOMOTOR",
                     "<Unit_Abilities_Data SubObjectList=\"Yes\"><Unit_Ability><Type>TACTICAL_BRIBE</Type><Recharge_Seconds>1</Recharge_Seconds>"
                     "<GUI_Activated_Ability_Name>S10_Bkt1_Bribe</GUI_Activated_Ability_Name></Unit_Ability></Unit_Abilities_Data>"
                     "<Abilities SubObjectList=\"Yes\"><Tactical_Bribe_Ability Name=\"S10_Bkt1_Bribe\"><Activation_Style>User_Input</Activation_Style>"
                     "<Activation_Min_Range>150</Activation_Min_Range><Activation_Max_Range>200</Activation_Max_Range>"
                     "</Tactical_Bribe_Ability></Abilities>");
    t.insert(end, add);
    return t;
}

std::string trooper_range_400(std::string t) {  // the squad trooper's Targeting_Max_Attack_Distance 250 -> 400
    const std::string at = "<GroundInfantry Name=\"S10_Trooper\">", r = "<Targeting_Max_Attack_Distance>250</Targeting_Max_Attack_Distance>";
    if (const size_t p = t.find(at); p != std::string::npos)
        if (const size_t q = t.find(r, p); q != std::string::npos) t.replace(q, r.size(), "<Targeting_Max_Attack_Distance>400</Targeting_Max_Attack_Distance>");
    return t;
}

// The live combat unit of `type_name` and owner `owner`, the `nth` one in combat order.
objects::ObjectId unit_of(const TacticalBattle& b, const objects::ObjectStore& st, const Data& d, const char* type_name, int32_t owner,
                          int nth = 0) {
    const tc::CombatSystem& cs = b.combat();
    const objects::TypeIndex t = d.types.resolve_name(type_name);
    for (size_t i = 0; i < cs.unit_count(); ++i) {
        const auto id = cs.unit_id_at(i);
        const objects::ObjectHandle h = st.find(id);
        if (!st.alive(h) || st.type(h) != t || st.owner(h) != owner) continue;
        if (nth-- == 0) return id;
    }
    return objects::kNoObjectId;
}

void invulnerable_all(TacticalBattle& b) {
    auto& cs = const_cast<tc::CombatSystem&>(b.combat());
    for (size_t i = 0; i < cs.unit_count(); ++i) cs.make_invulnerable(cs.unit_id_at(i), 1000000, b.frame());
}

// BKT1_ATTACK_RE §1.2.1, written from the spec (not from the port's code): rings centred on M from R0 = max(0, min -
// |M - T|) (3-D), step 1.0 growing by 0.8 up to min(1.5 r, 10) (r = M's hard footprint radius, here sqrt(3^2 + 3^2)),
// 20 rings, n = trunc(2 pi R / r) + 1 samples (at least 4 after ring 0) counter-clockwise from the T -> M direction; the
// first sample >= d from T (3-D, the sample at M's z) inside the map extents and in M's zone for M's own class (the
// movement grid's zone query stands in for 0x50eeb0) wins. None: T's point. Doubles: the port's float result agrees to
// well under 1e-3.
Vec3 spec_stand_off(const TacticalBattle& b, objects::ObjectId m, Vec3 mp, Vec3 tp, float min_s, float d, const tactical::Aabb& box) {
    constexpr double kPi = 3.14159265358979323846;
    const mv::MovementSystem& ms = b.movement();
    const uint32_t mask = ms.mask_of(m);
    const double r = std::sqrt(18.0), max_step = std::min(1.5 * r, 10.0);
    const double yaw = std::atan2(static_cast<double>(mp.y - tp.y), static_cast<double>(mp.x - tp.x));
    double R = std::max(0.0, static_cast<double>(min_s) - static_cast<double>(dist3(mp, tp))), step = 1.0;
    for (int ring = 0; ring < 20; ++ring) {
        int n = static_cast<int>(R * 2.0 * kPi / r) + 1;
        if (ring > 0) n = std::max(4, n);
        for (int k = 0; k < n; ++k) {
            const double th = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(n) + yaw;
            const Vec3 q{static_cast<float>(mp.x + R * std::cos(th)), static_cast<float>(mp.y + R * std::sin(th)), mp.z};
            if (dist3(q, tp) < d) continue;
            if (q.x < box.min.x || q.x > box.max.x || q.y < box.min.y || q.y > box.max.y) continue;
            if (!mv::zone_connected(ms.regions(), mask, {mp.x, mp.y}, {q.x, q.y})) continue;
            return q;
        }
        R += step;
        step = std::min(step + 0.8, max_step);
    }
    return tp;
}

// ---- staging and the engage clause -------------------------------------------------------------------------------

struct StagingCase {
    const char* name;
    std::function<void(TacticalBattle&, TacticalMode&)> place;  // teleports (one tick before check)
    std::function<void(TacticalBattle&, TacticalMode&)> check;
};

void test_staging(const Data& d) {
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 4000;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;  // no stand-in orders
    opt.battle.fog = false;
    ClashSession s(d, 0, opt,
                   {"S10_Ranger_Co", "S10_Ranger_Co", "S10_Ranger_Co", "S10_Far_Co", "S10_Crusher_Co", "S10_Super_Co", "S10_Briber_Co"},
                   {"S10_Ranger_Co", "S10_Crusher_Co", "S10_Super_Co"}, true);
    if (!s.gs) return;
    objects::ObjectId r[3] = {}, far = 0, cru = 0, sup = 0, bri = 0, e = 0, ec = 0, es = 0;
    tactical::Aabb box{};
    Vec3 mid{}, S{};  // S: the engage-clause target spot (set with the cases)
    std::vector<StagingCase> cases;
    size_t ci = 0;
    bool placed = false, ready = false;
    const uint8_t bribe = tactical::abilities::ability_type_from_name("TACTICAL_BRIBE");
    const auto put = [](TacticalBattle& b, TacticalMode& tm, objects::ObjectId id, Vec3 p) {
        objects::ObjectStore& st = tm.primary();
        CHECK(b.teleport(tm, id, p, st.facing(st.find(id))));
    };
    const auto pos = [](TacticalMode& tm, objects::ObjectId id) { return tm.primary().position(tm.primary().find(id)); };
    const auto stage = [&](TacticalBattle& b, TacticalMode& tm, std::span<const objects::ObjectId> ids, objects::ObjectId t, size_t movers) {
        return TA::stage_goal(b, tm, ids, t, pos(tm, t), movers);
    };
    // the spec's d for min m and base range a: m + 0.2 (a - m); the largest ring step: min(1.5 r, 10), r = sqrt(3^2 + 3^2)
    const float step_max = std::min(1.5f * std::sqrt(18.0f), 10.0f);
    // engage clause: the arc slot radius of a plain and a pending-ability plan (attack and guard), read off the plan
    float rho_plain = -1.0f, rho_gplain = -1.0f, rho_pend = -1.0f, rho_gpend = -1.0f;
    for (uint64_t tk = 0; tk < 4000 && !(ready && ci >= cases.size()); ++tk) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) {
            if (ready) break;
            continue;
        }
        objects::ObjectStore& st = tm->primary();
        if (!ready) {
            for (int i = 0; i < 3; ++i) r[i] = unit_of(*b, st, d, "S10_Ranger", 0, i);
            far = unit_of(*b, st, d, "S10_Far", 0);
            cru = unit_of(*b, st, d, "S10_Crusher", 0);
            sup = unit_of(*b, st, d, "S10_Super", 0);
            bri = unit_of(*b, st, d, "S10_Briber", 0);
            e = unit_of(*b, st, d, "S10_Ranger", 1);
            ec = unit_of(*b, st, d, "S10_Crusher", 1);
            es = unit_of(*b, st, d, "S10_Super", 1);
            const objects::ObjectId need[] = {r[0], r[1], r[2], far, cru, sup, bri, e, ec, es};
            bool all = true;
            for (const objects::ObjectId id : need) all = all && id != objects::kNoObjectId;
            if (!all && b->frame() < 600) continue;  // the invaders land over the first frames
            std::printf("  staging scene: units %s (own rangers %d %d %d far %d crusher %d super %d briber %d; enemy ranger %d crusher %d "
                        "super %d)\n",
                        all ? "found" : "MISSING", static_cast<int>(r[0]), static_cast<int>(r[1]), static_cast<int>(r[2]),
                        static_cast<int>(far), static_cast<int>(cru), static_cast<int>(sup), static_cast<int>(bri), static_cast<int>(e),
                        static_cast<int>(ec), static_cast<int>(es));
            CHECK(all);
            if (!all) break;
            invulnerable_all(*b);
            box = TA::map_extents(*b, *tm);
            const tactical::Aabb& bb = tm->bounds();
            mid = Vec3{0.5f * (bb.min.x + bb.max.x), 0.5f * (bb.min.y + bb.max.y), 0.0f};
            std::printf("  map extents (%.0f, %.0f)-(%.0f, %.0f), bounds (%.0f, %.0f)-(%.0f, %.0f)\n", static_cast<double>(box.min.x),
                        static_cast<double>(box.min.y), static_cast<double>(box.max.x), static_cast<double>(box.max.y),
                        static_cast<double>(bb.min.x), static_cast<double>(bb.min.y), static_cast<double>(bb.max.x),
                        static_cast<double>(bb.max.y));
            // park everything not in a case far from the case spots (the staging ignores occupancy; this keeps targets of
            // opportunity out of weapon range)
            const objects::ObjectId parked[] = {r[1], r[2], far, cru, sup, ec, es};
            for (size_t k = 0; k < std::size(parked); ++k)
                put(*b, *tm, parked[k], Vec3{bb.min.x + 60.0f + 60.0f * static_cast<float>(k), bb.min.y + 60.0f, 0.0f});
            ready = true;

            // (A) the ring slide at the map edge: T 20 inside M on the +x axis, M 40 inside the playable box's east edge
            // (on passable ground; the map extents end 20 beyond it). The first ring is R0 = 100 - 20 = 80 and its theta 0
            // spot (on the T -> M ray) is off the map; spots >= d = 130 from T inside the map exist only well around M:
            // the search slides counter-clockwise (+y here). The edge column beyond the playable box (x 1240-1260) is
            // impassable on this map, so here the reachability test (§1.2.1 test 3) rejects the first spots that are far
            // enough, and the map-box test (test 2) only the theta 0 one; the oracle spec_stand_off takes both answers
            // (the extents, the zone query) from the port, so it checks the ring order, radii, sample count and distance
            // test, not the map box or the zone rule themselves.
            cases.push_back({"ring slide at the map edge",
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 const float ex = std::min(box.max.x, m.bounds().max.x) - 40.0f;
                                 put(bt, m, r[0], Vec3{ex, mid.y, 0.0f});
                                 put(bt, m, e, Vec3{ex - 20.0f, mid.y, 0.0f});
                             },
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 const objects::ObjectId ids[] = {r[0]};
                                 const Vec3 mp = pos(m, r[0]), tp = pos(m, e), g = stage(bt, m, ids, e, 1);
                                 const float R0 = std::max(0.0f, 100.0f - dist3(mp, tp));
                                 const float ax = mp.x - tp.x, ay = mp.y - tp.y, bx = g.x - mp.x, by = g.y - mp.y;
                                 const float cross = ax * by - ay * bx;
                                 const bool inside = g.x >= box.min.x && g.x <= box.max.x && g.y >= box.min.y && g.y <= box.max.y;
                                 std::printf("    edge: M (%.1f, %.1f) T (%.1f, %.1f), on-line spot x %.1f vs edge %.1f; G (%.2f, %.2f) %.2f from "
                                             "T, cross %.1f, inside %d\n",
                                             static_cast<double>(mp.x), static_cast<double>(mp.y), static_cast<double>(tp.x),
                                             static_cast<double>(tp.y), static_cast<double>(mp.x + R0), static_cast<double>(box.max.x),
                                             static_cast<double>(g.x), static_cast<double>(g.y), static_cast<double>(dist3(g, tp)),
                                             static_cast<double>(cross), inside ? 1 : 0);
                                 const Vec3 want = spec_stand_off(bt, r[0], mp, tp, 100.0f, 130.0f, box);
                                 std::printf("    edge: spec spot (%.2f, %.2f)\n", static_cast<double>(want.x), static_cast<double>(want.y));
                                 CHECK(std::fabs(g.x - want.x) <= 1e-3f && std::fabs(g.y - want.y) <= 1e-3f);
                                 CHECK(mp.x + R0 > box.max.x);  // the precondition: the theta 0 spot of ring 0 is off the map
                                 CHECK(!(g.x == tp.x && g.y == tp.y) && inside && dist3(g, tp) >= 130.0f - 1e-3f);
                                 CHECK(cross > 0.0f && by > 0.0f);  // turned counter-clockwise around M, not on the line
                             }});
            // (A0) the same pair in the open, the min-range gate passes: G on the T -> M ray beyond M, |G - T| in [130,
            // 130 + one ring step]
            cases.push_back({"stand-off in the open",
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 put(bt, m, r[0], Vec3{mid.x, mid.y, 0.0f});
                                 put(bt, m, e, Vec3{mid.x - 60.0f, mid.y, 0.0f});
                             },
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 const objects::ObjectId ids[] = {r[0]};
                                 const Vec3 mp = pos(m, r[0]), tp = pos(m, e), g = stage(bt, m, ids, e, 1);
                                 const float ax = mp.x - tp.x, ay = mp.y - tp.y, bx = g.x - mp.x, by = g.y - mp.y;
                                 const float cross = ax * by - ay * bx, dot = ax * bx + ay * by;
                                 const float over = dist3(g, tp) - 130.0f;
                                 std::printf("    open: G %.3f from T (overshoot %.3f, max ring step %.3f), cross %.4f\n",
                                             static_cast<double>(dist3(g, tp)), static_cast<double>(over), static_cast<double>(step_max),
                                             static_cast<double>(cross));
                                 CHECK(over >= -1e-3f && over <= step_max + 1e-3f);
                                 const Vec3 want = spec_stand_off(bt, r[0], mp, tp, 100.0f, 130.0f, box);
                                 CHECK(std::fabs(g.x - want.x) <= 1e-3f && std::fabs(g.y - want.y) <= 1e-3f);
                                 CHECK(dot > 0.0f && std::fabs(cross) <= 1e-3f * std::sqrt(ax * ax + ay * ay) * std::sqrt(bx * bx + by * by) + 1e-2f);
                             }});
            // (B) search failure: base range 2000 -> d = 100 + 0.2 x 1900 = 480; 20 rings around M reach about
            // 60 + 40 + 100 < 480 from T: no spot, G stays at T's point
            cases.push_back({"search failure keeps T",
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 put(bt, m, far, Vec3{mid.x, mid.y + 200.0f, 0.0f});
                                 put(bt, m, e, Vec3{mid.x - 60.0f, mid.y + 200.0f, 0.0f});
                             },
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 const objects::ObjectId ids[] = {far};
                                 const Vec3 tp = pos(m, e), g = stage(bt, m, ids, e, 1);
                                 // the gate passes (so G = T comes from the failed search, not from step 3) and the spec's rings
                                 // find nothing either
                                 const bool gate = bt.combat().inside_min_range_of(far, e, 100.0f);
                                 const Vec3 want = spec_stand_off(bt, far, pos(m, far), tp, 100.0f, 480.0f, box);
                                 std::printf("    search failure: gate %d, G - T (%.3f, %.3f), spec finds no spot %d\n", gate ? 1 : 0,
                                             static_cast<double>(g.x - tp.x), static_cast<double>(g.y - tp.y),
                                             want.x == tp.x && want.y == tp.y ? 1 : 0);
                                 CHECK(gate && want.x == tp.x && want.y == tp.y);
                                 CHECK(g.x == tp.x && g.y == tp.y && g.z == tp.z);
                             }});
            // (C) the crush case: the gate passes (60 < 100 + E); one member with SQUASH and T Is_Squashable -> G = T
            cases.push_back({"crush case",
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 put(bt, m, cru, Vec3{mid.x, mid.y - 200.0f, 0.0f});
                                 put(bt, m, sup, Vec3{mid.x + 300.0f, mid.y - 200.0f, 0.0f});
                                 put(bt, m, r[1], Vec3{mid.x - 120.0f, mid.y - 200.0f, 0.0f});  // 60 west of e
                                 put(bt, m, e, Vec3{mid.x - 60.0f, mid.y - 200.0f, 0.0f});
                                 put(bt, m, ec, Vec3{mid.x + 240.0f, mid.y - 200.0f, 0.0f});
                             },
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 const Vec3 tp = pos(m, e), tcp = pos(m, ec);
                                 const objects::ObjectId c1[] = {cru}, s1[] = {sup}, r1[] = {r[1]};
                                 const Vec3 g1 = stage(bt, m, c1, e, 1);      // crusher vs Is_Squashable: T
                                 const Vec3 g2 = stage(bt, m, c1, e, 2);      // two members: staged
                                 const Vec3 g3 = stage(bt, m, s1, ec, 1);     // supercrusher vs Is_Squashable_By_Supercrusher: T
                                 const Vec3 g5 = stage(bt, m, r1, e, 1);      // no SQUASH: staged
                                 put(bt, m, cru, Vec3{tcp.x - 60.0f, tcp.y, 0.0f});  // case C': the crusher inside its range of ec
                                 const auto at = [](Vec3 g, Vec3 t) { return g.x == t.x && g.y == t.y; };
                                 std::printf("    crush: squashable %d, two members %d, supercrusher %d; staged: two members %d, no SQUASH %d "
                                             "(%.1f from T)\n",
                                             at(g1, tp) ? 1 : 0, at(g2, tp) ? 1 : 0, at(g3, tcp) ? 1 : 0, !at(g2, tp) ? 1 : 0,
                                             !at(g5, tp) ? 1 : 0, static_cast<double>(dist3(g5, tp)));
                                 CHECK(at(g1, tp) && !at(g2, tp) && dist3(g2, tp) >= 130.0f - 1e-3f);
                                 CHECK(at(g3, tcp));
                                 CHECK(!at(g5, tp) && dist3(g5, tp) >= 130.0f - 1e-3f);
                             }});
            // (C') a crusher that is not a supercrusher against a T that is only Is_Squashable_By_Supercrusher: staged
            cases.push_back({"crush case, supercrusher-only target",
                             [&](TacticalBattle&, TacticalMode&) {},  // placed by the crush case's check
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 const objects::ObjectId c1[] = {cru};
                                 const Vec3 tcp = pos(m, ec), g4 = stage(bt, m, c1, ec, 1);
                                 std::printf("    crusher vs supercrusher-only target: G %.1f from T\n", static_cast<double>(dist3(g4, tcp)));
                                 CHECK(!(g4.x == tcp.x && g4.y == tcp.y) && dist3(g4, tcp) >= 130.0f - 1e-3f);
                             }});
            // (E) the Activation_Max_Range engage clause, on the enemy S10_Super (hard half extents 5 x 2) at S with the
            // briber 600 east of it (out of its range 250: slotted). Plain (nothing pending yet): engage = 250 + min(5, 2) =
            // 252 (attack), min(252, Land_Guard_Range 150) = 150 (guard). With the bribe pending (max 200; case D orders it
            // on ANOTHER target, e: §1.1 step 3 has no check that the pending target is B's object): engage = min(250, 200) =
            // 200 for both - no extent, no guard cap. The arc slot radius is R minus the formation's half depth, so the two
            // plans of each kind differ by exactly the engagement ranges' difference: 52 (attack), -50 (guard).
            S = Vec3{mid.x - 300.0f, mid.y - 450.0f, 0.0f};
            cases.push_back({"engage clause, plain plans",
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 put(bt, m, es, S);
                                 put(bt, m, bri, Vec3{S.x + 600.0f, S.y, 0.0f});
                             },
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 float mn = 0.0f, mx = 0.0f;
                                 const bool pend = TA::abilities(bt).pending_ranges(bri, mn, mx);
                                 SimRng& rng = s.host->random().synced();
                                 const objects::ObjectId ids[] = {bri};
                                 const Vec3 tp = pos(m, es);
                                 (void)TA::land_attack_order(bt, m, ids, es, tp, 0, rng, false);
                                 rho_plain = bt.movement().view(bri).atk_rho;
                                 (void)TA::land_attack_order(bt, m, ids, es, tp, mv::kMoveFlagGuardMove, rng, true);
                                 rho_gplain = bt.movement().view(bri).atk_rho;
                                 std::printf("    engage clause: nothing pending %d; plain attack rho %.4f, guard rho %.4f\n", !pend ? 1 : 0,
                                             static_cast<double>(rho_plain), static_cast<double>(rho_gplain));
                                 CHECK(!pend);
                             }});
            // (D) Activation_Min_Range picks M: a ranger (min 100) and the briber (no weapon min range), both 120 from T.
            // Without a pending ability M = the ranger, outside its min range (120 > 100 + E): G = T. With the briber's
            // pending Tactical_Bribe on T (min 150 > 100): M = the briber, inside 150 + E: staged with
            // d = 150 + 0.2 x (250 - 150) = 170, on the T -> briber line.
            cases.push_back({"Activation_Min_Range picks M",
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 put(bt, m, e, Vec3{mid.x, mid.y + 400.0f, 0.0f});
                                 put(bt, m, r[1], Vec3{mid.x, mid.y + 520.0f, 0.0f});
                                 put(bt, m, bri, Vec3{mid.x + 120.0f, mid.y + 400.0f, 0.0f});
                             },
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 const objects::ObjectId ids[] = {r[1], bri};
                                 const Vec3 tp = pos(m, e), bp = pos(m, bri);
                                 const Vec3 g0 = stage(bt, m, ids, e, 2);
                                 const objects::ObjectId users[] = {bri};
                                 const bool ordered = TA::abilities(bt).targeted_ability(users, bribe, e, -1, bt.frame()) == 1u;
                                 float mn = 0.0f, mx = 0.0f;
                                 const bool pend = TA::abilities(bt).pending_ranges(bri, mn, mx);
                                 const Vec3 g1 = stage(bt, m, ids, e, 2);
                                 const float ax = bp.x - tp.x, ay = bp.y - tp.y, bx = g1.x - bp.x, by = g1.y - bp.y;
                                 std::printf("    Activation_Min_Range: no pending -> G at T %d; bribe ordered %d pending %d (%.0f, %.0f) -> G "
                                             "%.3f from T, cross %.4f\n",
                                             g0.x == tp.x && g0.y == tp.y ? 1 : 0, ordered ? 1 : 0, pend ? 1 : 0, static_cast<double>(mn),
                                             static_cast<double>(mx), static_cast<double>(dist3(g1, tp)),
                                             static_cast<double>(ax * by - ay * bx));
                                 CHECK(g0.x == tp.x && g0.y == tp.y);
                                 CHECK(ordered && pend && mn == 150.0f && mx == 200.0f);
                                 const float over = dist3(g1, tp) - 170.0f;
                                 CHECK(over >= -1e-3f && over <= step_max + 1e-3f && ax * bx + ay * by > 0.0f);
                                 const Vec3 want = spec_stand_off(bt, bri, bp, tp, 150.0f, 170.0f, box);  // rings around the briber
                                 CHECK(std::fabs(g1.x - want.x) <= 1e-3f && std::fabs(g1.y - want.y) <= 1e-3f);
                                 // the briber goes back to its engage-clause spot at once, out of the bribe's range of e (the
                                 // ability's attack order would walk it in)
                                 put(bt, m, bri, Vec3{S.x + 600.0f, S.y, 0.0f});
                                 put(bt, m, es, S);
                             }});
            cases.push_back({"engage clause, pending plans",
                             [&](TacticalBattle&, TacticalMode&) {},  // placed by case D's check
                             [&](TacticalBattle& bt, TacticalMode& m) {
                                 float mn = 0.0f, mx = 0.0f;
                                 const bool pend = TA::abilities(bt).pending_ranges(bri, mn, mx);
                                 const bool on_e = TA::abilities(bt).pending_target(bri) == e;
                                 SimRng& rng = s.host->random().synced();
                                 const objects::ObjectId ids[] = {bri};
                                 const Vec3 tp = pos(m, es);
                                 (void)TA::land_attack_order(bt, m, ids, es, tp, 0, rng, false);
                                 rho_pend = bt.movement().view(bri).atk_rho;
                                 (void)TA::land_attack_order(bt, m, ids, es, tp, mv::kMoveFlagGuardMove, rng, true);
                                 rho_gpend = bt.movement().view(bri).atk_rho;
                                 CHECK(on_e);  // pending on e, the plans on es
                                 const float want = 252.0f - 200.0f, gwant = 150.0f - 200.0f;
                                 std::printf("    engage clause: pending %d (max %.0f): attack rho %.4f, guard rho %.4f; plain - pending: "
                                             "attack %.4f (spec %.4f), guard %.4f (spec %.4f)\n",
                                             pend ? 1 : 0, static_cast<double>(mx), static_cast<double>(rho_pend),
                                             static_cast<double>(rho_gpend), static_cast<double>(rho_plain - rho_pend),
                                             static_cast<double>(want), static_cast<double>(rho_gplain - rho_gpend),
                                             static_cast<double>(gwant));
                                 CHECK(pend && mx == 200.0f);
                                 CHECK(rho_plain > 0.0f && rho_pend > 0.0f && rho_gplain > 0.0f && rho_gpend > 0.0f);
                                 CHECK(std::fabs((rho_plain - rho_pend) - want) <= 1e-2f);
                                 CHECK(std::fabs((rho_gplain - rho_gpend) - gwant) <= 1e-2f);
                             }});
            continue;
        }
        if (ci >= cases.size()) break;
        if (!placed) {
            cases[ci].place(*b, *tm);
            placed = true;
            continue;
        }
        std::printf("  bkt1 staging: %s\n", cases[ci].name);
        cases[ci].check(*b, *tm);
        placed = false;
        ++ci;
    }
    std::printf("  staging cases run: %zu of %zu\n", ci, cases.size());
    CHECK(ready && ci == cases.size());
}

// ---- cmb3 t02: the fire trim's gate is the object hidden test ------------------------------------------------------

// cmb3 t02 (b0196, BKR2 Q6 [H]): 0x410e90's gate is FOW_Object_Hidden(members[0]'s owner, T, force 1) and the stealth test.
// An attack plan of one S10_Ranger (range 250) on an enemy S10_Lurker (HIDE_WHEN_FOGGED) 900 away, fog on: while
// the target object is hidden for player 0 the plan is not trimmed (the formation walks to the target's point); revealed,
// the same plan is trimmed.
void test_trim_gate_hidden(const Data& d) {
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 2000;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;  // no stand-in orders
    ClashSession s(d, 0, opt, {"S10_Ranger_Co"}, {"S10_Lurker_Co"}, true);
    if (!s.gs) return;
    objects::ObjectId r = objects::kNoObjectId, e = objects::kNoObjectId;
    int stage = 0;
    uint32_t stage_frame = 0, trims0 = 0;
    bool hidden_seen = false, untrimmed = false, revealed = false, trimmed = false;
    const auto pos = [](TacticalMode& tm, objects::ObjectId id) { return tm.primary().position(tm.primary().find(id)); };
    for (uint64_t tk = 0; tk < 2000 && stage < 9; ++tk) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) {
            if (stage > 0) break;
            continue;
        }
        objects::ObjectStore& st = tm->primary();
        if (stage == 0) {
            r = unit_of(*b, st, d, "S10_Ranger", 0);
            e = unit_of(*b, st, d, "S10_Lurker", 1);
            if ((r == objects::kNoObjectId || e == objects::kNoObjectId) && b->frame() < 600) continue;
            CHECK(r != objects::kNoObjectId && e != objects::kNoObjectId && b->fog() != nullptr);
            if (r == objects::kNoObjectId || e == objects::kNoObjectId || !b->fog()) break;
            invulnerable_all(*b);
            // the target goes to a point player 0 has fogged (this map's start reveals part of it), the attacker 700 from it
            // along an axis, inside the playable box
            const tactical::Aabb& bb = tm->bounds();
            const tactical::fog::FogOfWar* fw = TA::fog(*b);
            Vec3 ep{}, rp{};
            bool spot = false;
            for (float y = bb.min.y + 40.0f; y <= bb.max.y - 40.0f && !spot; y += 40.0f)
                for (float x = bb.min.x + 40.0f; x <= bb.max.x - 40.0f && !spot; x += 40.0f) {
                    if (!fw->point_fogged(0, x, y, true)) continue;
                    for (const std::pair<float, float> dv : {std::pair{700.0f, 0.0f}, {-700.0f, 0.0f}, {0.0f, 700.0f}, {0.0f, -700.0f}}) {
                        const float qx = x + dv.first, qy = y + dv.second;
                        if (qx < bb.min.x + 40.0f || qx > bb.max.x - 40.0f || qy < bb.min.y + 40.0f || qy > bb.max.y - 40.0f) continue;
                        ep = Vec3{x, y, 0.0f};
                        rp = Vec3{qx, qy, 0.0f};
                        spot = true;
                        break;
                    }
                }
            CHECK(spot);
            if (!spot) break;
            CHECK(b->teleport(*tm, e, ep, st.facing(st.find(e))));
            CHECK(b->teleport(*tm, r, rp, st.facing(st.find(r))));
            stage = 1;
            stage_frame = b->frame();
            continue;
        }
        const objects::ObjectId ids[] = {r};
        SimRng& rng = s.host->random().synced();
        if (stage == 1 && b->frame() >= stage_frame + 2) {
            bool h = false, sl = false;
            TA::target_unseen(*b, st, r, e, h, sl);
            hidden_seen = h && !sl;
            {
                const tactical::fog::FogOfWar* fw = TA::fog(*b);
                const Vec3 ep = pos(*tm, e), rp = pos(*tm, r);
                const tactical::fog::FogGrid* g0 = fw ? fw->grid(0) : nullptr;
                std::printf("  trim gate scene: attacker (%.0f, %.0f) target (%.0f, %.0f); fog started %d active %d reveal-all %d "
                            "target point fogged %d / %d (force); hidden %d stealthed %d\n",
                            static_cast<double>(rp.x), static_cast<double>(rp.y), static_cast<double>(ep.x), static_cast<double>(ep.y),
                            fw && fw->started() ? 1 : 0, fw && fw->fow_active() ? 1 : 0, g0 && g0->reveal_all_active() ? 1 : 0,
                            fw && fw->point_fogged(0, ep.x, ep.y, false) ? 1 : 0, fw && fw->point_fogged(0, ep.x, ep.y, true) ? 1 : 0,
                            h ? 1 : 0, sl ? 1 : 0);
            }
            trims0 = b->movement().stats().attack_trims;
            (void)TA::land_attack_order(*b, *tm, ids, e, pos(*tm, e), 0, rng, false);
            untrimmed = b->movement().stats().attack_trims == trims0 && b->movement().view(r).has_goal;
            TA::fog(*b)->reveal_all(0);
            stage = 2;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 2 && b->frame() >= stage_frame + 2) {
            bool h = true, sl = true;
            TA::target_unseen(*b, st, r, e, h, sl);
            revealed = !h && !sl;
            trims0 = b->movement().stats().attack_trims;
            (void)TA::land_attack_order(*b, *tm, ids, e, pos(*tm, e), 0, rng, false);
            trimmed = b->movement().stats().attack_trims == trims0 + 1;
            stage = 9;
        }
    }
    std::printf("  fire trim gate: hidden %d -> untrimmed %d; revealed %d -> trimmed %d\n", hidden_seen ? 1 : 0, untrimmed ? 1 : 0,
                revealed ? 1 : 0, trimmed ? 1 : 0);
    CHECK(hidden_seen && untrimmed && revealed && trimmed);
}

// ---- t03: the attack formation lead skips locked members ----------------------------------------------------------

void test_locked_lead(const Data& d) {
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 6000;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;
    opt.battle.fog = false;
    ClashSession s(d, 0, opt, {"S10_Ranger_Co", "S10_Ranger_Co", "S10_Ranger_Co"}, {"S10_Ranger_Co"}, true);
    if (!s.gs) return;
    objects::ObjectId r[3] = {}, e = 0;
    std::vector<objects::ObjectId> locked;
    int stage = 0;
    uint32_t stage_frame = 0, replans0 = 0;
    objects::ObjectId lead = 0;
    mv::V2 lead_goal{};
    bool one_replanned = false, kept = false, waited = false, unlocked_replanned = false;
    uint32_t one_frames = 0, unlock_frames = 0;
    Vec3 perp{};
    for (uint64_t tk = 0; tk < 6000 && stage < 9; ++tk) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) {
            if (stage > 0) break;
            continue;
        }
        objects::ObjectStore& st = tm->primary();
        const mv::MovementSystem& ms = b->movement();
        if (stage == 0) {
            for (int i = 0; i < 3; ++i) r[i] = unit_of(*b, st, d, "S10_Ranger", 0, i);
            e = unit_of(*b, st, d, "S10_Ranger", 1);
            CHECK(r[0] && r[1] && r[2] && e);
            if (!(r[0] && r[1] && r[2] && e)) break;
            invulnerable_all(*b);
            b->set_script_movement_lock(&locked, [&locked](objects::ObjectId id) {
                return std::find(locked.begin(), locked.end(), id) != locked.end();
            });
            // the three rangers in a row 25 apart across the approach, 800 from the target (out of range: a long walk)
            const tactical::Aabb& bb = tm->bounds();
            const float y0 = 0.5f * (bb.min.y + bb.max.y);
            for (int i = 0; i < 3; ++i)
                CHECK(b->teleport(*tm, r[i], Vec3{bb.min.x + 150.0f, y0 + 25.0f * static_cast<float>(i - 1), 0.0f}, st.facing(st.find(r[i]))));
            CHECK(b->teleport(*tm, e, Vec3{bb.min.x + 950.0f, y0, 0.0f}, st.facing(st.find(e))));
            perp = Vec3{0.0f, 1.0f, 0.0f};
            stage = 1;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 1 && b->frame() >= stage_frame + 2) {
            const objects::ObjectId ids[] = {r[0], r[1], r[2]};
            CHECK(b->order_attack(*tm, 0, ids, e, s.host->random().synced()) == commands::RejectReason::None);
            const uint32_t f0 = ms.view(r[0]).atk_form;
            CHECK(f0 != 0 && ms.view(r[1]).atk_form == f0 && ms.view(r[2]).atk_form == f0);  // one attack formation
            stage = 2;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 2 && b->frame() >= stage_frame + 60) {
            // lock the member nearest the movers' centroid (the would-be lead, G 3), then move the target 300 across: the
            // formation must still be checked (through an unlocked lead) and re-planned within one 30-frame check
            mv::V2 c{0.0f, 0.0f};
            for (const auto id : r) c = c + ms.view(id).pos;
            c = c * (1.0f / 3.0f);
            float best = 3.0e38f;
            for (const auto id : r) {
                const mv::V2 p = ms.view(id).pos;
                const float dd = (p.x - c.x) * (p.x - c.x) + (p.y - c.y) * (p.y - c.y);
                if (dd < best) {
                    best = dd;
                    lead = id;
                }
            }
            CHECK(ms.view(lead).has_goal);
            lead_goal = ms.view(lead).goal;
            locked = {lead};
            const Vec3 tp = st.position(st.find(e));
            CHECK(b->teleport(*tm, e, Vec3{tp.x + perp.x * 300.0f, tp.y + perp.y * 300.0f, tp.z}, st.facing(st.find(e))));
            replans0 = ms.stats().attack_replans;
            stage = 3;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 3) {
            if (ms.stats().attack_replans > replans0) {
                one_replanned = true;
                one_frames = b->frame() - stage_frame;
                const mv::UnitView v = ms.view(lead);
                kept = v.goal == lead_goal;  // the locked member keeps its order
                // every member locked: move the target back across; no check, no re-plan while all stay locked
                locked = {r[0], r[1], r[2]};
                const Vec3 tp = st.position(st.find(e));
                CHECK(b->teleport(*tm, e, Vec3{tp.x - perp.x * 300.0f, tp.y - perp.y * 300.0f, tp.z}, st.facing(st.find(e))));
                replans0 = ms.stats().attack_replans;
                stage = 4;
                stage_frame = b->frame();
            } else if (b->frame() > stage_frame + 40) {
                stage = 9;
            }
            continue;
        }
        if (stage == 4) {
            if (ms.stats().attack_replans != replans0) {
                stage = 9;  // a re-plan with every member locked
            } else if (b->frame() >= stage_frame + 90) {
                waited = true;
                locked.clear();
                stage = 5;
                stage_frame = b->frame();
            }
            continue;
        }
        if (stage == 5) {
            if (ms.stats().attack_replans > replans0) {
                unlocked_replanned = true;
                unlock_frames = b->frame() - stage_frame;
                stage = 9;
            } else if (b->frame() > stage_frame + 40) {
                stage = 9;
            }
        }
    }
    std::printf("  locked lead: the would-be lead %d locked -> re-planned %d after %u frame(s), its goal kept %d; all locked -> no "
                "re-plan for 90 frames %d; unlocked -> re-planned %d after %u frame(s)\n",
                static_cast<int>(lead), one_replanned ? 1 : 0, one_frames, kept ? 1 : 0, waited ? 1 : 0, unlocked_replanned ? 1 : 0,
                unlock_frames);
    CHECK(one_replanned && one_frames <= 31 && kept);
    CHECK(waited && unlocked_replanned && unlock_frames <= 31);
}

#ifdef ALAMO_SIM_HAVE_TACTICAL_AI
// ---- t02: the land guard follow -----------------------------------------------------------------------------------

std::unique_ptr<BattleAi> guard_ai(TacticalBattle& b, TacticalMode& tm, ClashSession& s, const Data& d, ai::AiDefinitions& defs,
                                   ai::EquationSet& eqs) {
    TacticalAiSetup ts;
    ts.defs = &defs;
    ts.equations = &eqs;
    ts.files = d.files.get();
    ts.ai_type = [](int32_t, const galactic::GalacticSim&) { return std::string(); };  // no AI player: orders only
    return std::make_unique<BattleAi>(b, tm, *s.host, ts);
}

bool lua_guard(BattleAi& bai, objects::ObjectId unit, objects::ObjectId target, Vec3 tp) {
    std::vector<uint8_t> pay;
    const auto put = [&pay](uint32_t v) {
        for (int i = 0; i < 4; ++i) pay.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    put(static_cast<uint32_t>(unit));
    put(static_cast<uint32_t>(target));
    put(std::bit_cast<uint32_t>(tp.x));
    put(std::bit_cast<uint32_t>(tp.y));
    put(std::bit_cast<uint32_t>(tp.z));
    put(0u);
    const auto sub = static_cast<uint16_t>(lua::kLuaOrderSubtype + static_cast<uint16_t>(lua::LuaOrder::Guard));
    return bai.execute(0, commands::ev::port_extension(sub, pay)) == commands::RejectReason::None;
}

// Two player-0 squads (guard sq[0], guarded sq[1]) and the first player-1 squad; at stage 0 the squads are found.
bool find_squads(TacticalBattle& b, objects::ObjectStore& st, objects::ObjectId sq[2], objects::ObjectId& enemy_sq) {
    const tc::CombatSystem& cs = b.combat();
    const mv::MovementSystem& ms = b.movement();
    std::map<uint32_t, int> teams;
    for (size_t i = 0; i < cs.unit_count(); ++i) {
        const auto id = cs.unit_id_at(i);
        if (cs.team(id) != 0 && cs.alive(id)) ++teams[cs.team(id)];
    }
    for (const auto& [tid, n] : teams) {
        const objects::ObjectHandle th = st.find(static_cast<objects::ObjectId>(tid));
        if (n < 2 || !st.alive(th) || !ms.has_unit(static_cast<objects::ObjectId>(tid))) continue;
        if (st.owner(th) == 0) {
            if (sq[0] == objects::kNoObjectId) sq[0] = static_cast<objects::ObjectId>(tid);
            else if (sq[1] == objects::kNoObjectId) sq[1] = static_cast<objects::ObjectId>(tid);
        } else if (st.owner(th) == 1 && enemy_sq == objects::kNoObjectId) {
            enemy_sq = static_cast<objects::ObjectId>(tid);
        }
    }
    return sq[0] != objects::kNoObjectId && sq[1] != objects::kNoObjectId && enemy_sq != objects::kNoObjectId;
}

// (R cap) the trooper's range 400: R = min(200, Land_Guard_Range 150) = 150. The guard walks ~800 to the guarded squad;
// while it moves the guarded squad is moved 130 off its recorded point (<= 150: no re-path at the next 30-frame check),
// then 170 off it (> 150, < 200 = the uncapped R): re-path within one check.
void test_guard_r_cap() {
    auto d = squad_data_patched("_bkt1_range400", trooper_range_400);
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 6000;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;
    opt.battle.fog = false;
    ClashSession s(*d, 0, opt, {"S10_Squad", "S10_Squad"}, {}, true);
    if (!s.gs) return;
    ai::AiDefinitions defs;
    ai::EquationSet eqs;
    std::unique_ptr<BattleAi> bai;
    objects::ObjectId sq[2] = {objects::kNoObjectId, objects::kNoObjectId}, enemy = objects::kNoObjectId;
    int stage = 0;
    uint32_t stage_frame = 0, rp0 = 0;
    Vec3 gp0{};
    bool ordered = false, quiet = false, repathed = false, moving = false;
    uint32_t repath_frames = 0;
    for (uint64_t tk = 0; tk < 6000 && stage < 9; ++tk) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) {
            if (stage > 0) break;
            continue;
        }
        objects::ObjectStore& st = tm->primary();
        const mv::MovementSystem& ms = b->movement();
        if (bai) bai->follow_service();
        if (stage == 0) {
            CHECK(find_squads(*b, st, sq, enemy));
            if (sq[1] == objects::kNoObjectId) break;
            invulnerable_all(*b);
            const tactical::Aabb& bb = tm->bounds();
            const float y0 = bb.min.y + 120.0f;  // a lane along the south edge, away from the enemy squads
            CHECK(b->teleport(*tm, sq[0], Vec3{bb.min.x + 120.0f, y0, 0.0f}, st.facing(st.find(sq[0]))));
            CHECK(b->teleport(*tm, sq[1], Vec3{bb.min.x + 920.0f, y0 + 200.0f, 0.0f}, st.facing(st.find(sq[1]))));
            bai = guard_ai(*b, *tm, s, *d, defs, eqs);
            stage = 1;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 1 && b->frame() >= stage_frame + 2) {
            gp0 = st.position(st.find(sq[1]));
            ordered = lua_guard(*bai, sq[0], sq[1], gp0);
            CHECK(ordered && ms.view(sq[0]).has_goal);
            rp0 = b->stats().guard_repaths;
            stage = 2;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 2 && b->frame() >= stage_frame + 5) {  // moving: 130 off the recorded point (along +y)
            CHECK(b->teleport(*tm, sq[1], Vec3{gp0.x, gp0.y + 130.0f, gp0.z}, st.facing(st.find(sq[1]))));
            stage = 3;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 3 && b->frame() >= stage_frame + 40) {  // one 30-frame check passed
            quiet = b->stats().guard_repaths == rp0 && ms.view(sq[0]).has_goal;
            CHECK(b->teleport(*tm, sq[1], Vec3{gp0.x, gp0.y + 170.0f, gp0.z}, st.facing(st.find(sq[1]))));
            stage = 4;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 4) {
            if (b->stats().guard_repaths > rp0) {
                repathed = true;
                repath_frames = b->frame() - stage_frame;
                moving = ms.view(sq[0]).has_goal;
                stage = 9;
            } else if (b->frame() > stage_frame + 40) {
                stage = 9;
            }
        }
    }
    std::printf("  guard R cap (range 400 -> R 150): ordered %d; 130 off: no re-path %d; 170 off: re-path %d after %u frame(s), "
                "still moving %d\n",
                ordered ? 1 : 0, quiet ? 1 : 0, repathed ? 1 : 0, repath_frames, moving ? 1 : 0);
    CHECK(ordered && quiet && repathed && repath_frames <= 31 && moving);
}

// (C 3) the guard of an enemy squad, the map revealed for player 0: it walks in and stands. The reveal is then undone:
// arrived with the object hidden -> d = |L - P| >= 40 (it stands ~150 off, the trim's guard range) -> the plan toward the
// last-observed point P (guard_unseen_plans + 1, no trim for an unseen target: it walks onto P), then, arrived within 40
// of P -> a position guard at L (guard_unseen_drops + 1; the follow and the attack block end).
void test_guard_hidden_drop() {
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
                       false, false, true);
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 8000;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;
    ClashSession s(*d, 0, opt, {"S10_Squad", "S10_Squad"}, {}, true);
    if (!s.gs) return;
    ai::AiDefinitions defs;
    ai::EquationSet eqs;
    std::unique_ptr<BattleAi> bai;
    objects::ObjectId sq[2] = {objects::kNoObjectId, objects::kNoObjectId}, enemy = objects::kNoObjectId;
    int stage = 0;
    uint32_t stage_frame = 0;
    Vec3 P{};
    bool ordered = false, arrived = false, hidden_plan = false, dropped = false, no_block = false;
    float stand_d = 0.0f, drop_d = 0.0f;
    uint32_t plans0 = 0, drops0 = 0;
    for (uint64_t tk = 0; tk < 8000 && stage < 9; ++tk) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) {
            if (stage > 0) break;
            continue;
        }
        objects::ObjectStore& st = tm->primary();
        const mv::MovementSystem& ms = b->movement();
        if (bai) bai->follow_service();
        const auto centroid = [&] {  // L = the movers' centroid: one mover here, the guard squad's container
            mv::V2 c = ms.view(sq[0]).pos;
            return Vec3{c.x, c.y, 0.0f};
        };
        if (stage == 0) {
            CHECK(find_squads(*b, st, sq, enemy));
            if (enemy == objects::kNoObjectId) break;
            invulnerable_all(*b);
            CHECK(b->fog() != nullptr);
            if (!b->fog()) break;
            TA::fog(*b)->reveal_all(0);
            const Vec3 ep = st.position(st.find(enemy));
            const tactical::Aabb& bb = tm->bounds();
            const float gx = ep.x + 500.0f <= bb.max.x - 60.0f ? ep.x + 500.0f : ep.x - 500.0f;
            CHECK(b->teleport(*tm, sq[0], Vec3{gx, ep.y, 0.0f}, st.facing(st.find(sq[0]))));
            CHECK(b->teleport(*tm, sq[1], Vec3{bb.min.x + 60.0f, bb.min.y + 60.0f, 0.0f}, st.facing(st.find(sq[1]))));
            bai = guard_ai(*b, *tm, s, *d, defs, eqs);
            stage = 1;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 1 && b->frame() >= stage_frame + 2) {
            ordered = lua_guard(*bai, sq[0], enemy, st.position(st.find(enemy)));
            CHECK(ordered);
            stage = 2;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 2) {
            const mv::UnitView v = ms.view(sq[0]);
            if (!v.has_goal && v.speed == 0.0f && b->frame() > stage_frame + 30) {
                arrived = true;
                P = st.position(st.find(enemy));
                stand_d = dist2(centroid(), P);
                plans0 = b->stats().guard_unseen_plans;
                drops0 = b->stats().guard_unseen_drops;
                TA::fog(*b)->undo_reveal_all(0);  // the guarded squad is hidden for player 0 from now on
                stage = 3;
                stage_frame = b->frame();
            } else if (b->frame() > stage_frame + 2000) {
                stage = 9;
            }
            continue;
        }
        if (stage == 3) {
            if (b->stats().guard_unseen_plans > plans0) hidden_plan = true;
            if (b->stats().guard_unseen_drops > drops0) {
                dropped = true;
                drop_d = dist2(centroid(), P);
                no_block = b->attack_blocks() == 0;
                stage = 9;
            } else if (b->frame() > stage_frame + 2000) {
                stage = 9;
            }
        }
    }
    std::printf("  guard of a hidden object: ordered %d, arrived %d at %.1f from it; hidden -> plan toward P %d, position guard %d at "
                "%.1f from P, attack block gone %d\n",
                ordered ? 1 : 0, arrived ? 1 : 0, static_cast<double>(stand_d), hidden_plan ? 1 : 0, dropped ? 1 : 0,
                static_cast<double>(drop_d), no_block ? 1 : 0);
    CHECK(ordered && arrived && stand_d >= 40.0f);
    CHECK(hidden_plan && dropped && drop_d < 40.0f && no_block);
}
#endif  // ALAMO_SIM_HAVE_TACTICAL_AI

// ---- t07: a third-party unit idles on land ------------------------------------------------------------------------

// The land battle of ClashSession on the s4 data, plus one S10_Ranger placed on the map for the Hostile faction (raw
// owner 3: the Hostile player 3, neither attacker nor defender; a map unit of an Is_Neutral owner would stay inert, so
// it would prove nothing). Stand-in orders on (order_interval 60, human units too): the two sides' units get orders;
// the third party never gets a movement goal and stays where it was put.
void test_third_party_idle(const Data& d) {
    jobs::JobSystem js(jobs::JobSystemConfig{0});
    SimConfig cfg;
    cfg.seed = 2026;
    cfg.players = 4;
    SimHost host(d.types, js, cfg);
    galactic::GalacticSetup setup = make_setup(d, true);
    galactic::PlayerSetup hostile;  // the s4 data's Hostile faction (3): player 3, neither attacker nor defender
    hostile.faction = d.rules.find_faction("Hostile");
    CHECK(hostile.faction == 3u);
    setup.players.push_back(hostile);
    galactic::GalacticSim* gs = host.start_galactic(d.rules, setup);
    CHECK(gs != nullptr);
    if (!gs) return;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 3000;
    opt.battle.fog = false;
    CampaignBattles battles(host, d.crules, *d.catalog,
                            [](const std::string& name, terrain::MapData& m) {
                                if (name != "S10_Land_B") return false;
                                m = land_map();
                                terrain::ObjectPlacement p;
                                p.type_crc = name_crc("S10_Ranger");
                                p.owner = 3;  // the Hostile faction: player 3 (a Neutral-owned map unit stays inert)
                                p.position[0] = 1200.0f;
                                p.position[1] = 1200.0f;
                                m.objects.push_back(p);
                                return true;
                            },
                            opt);
    CHECK(battles.attach(nullptr));
    std::vector<objects::ObjectHandle> up;
    for (objects::ObjectHandle h : gs->planets()[2].surface)
        if (gs->store().owner(h) == 1) up.push_back(h);
    CHECK(gs->launch_units(1, 2, up) == commands::RejectReason::None);
    const std::vector<uint32_t> uf = gs->fleets_at(2, 1);
    if (!uf.empty()) CHECK(gs->move_fleet(1, uf[0], 1) == commands::RejectReason::None);
    bool invaded = false, started = false;
    objects::ObjectId third = objects::kNoObjectId;
    Vec3 at{};
    uint32_t start_frame = 0, frames = 0, side_goals = 0;
    bool third_goal = false;
    float third_moved = 0.0f;
    for (uint64_t t = 0; t < 4000; ++t) {
        host.tick_once();
        if (!invaded && host.modes().depth() == 1) {
            const std::vector<uint32_t> f = gs->fleets_at(1, 1);
            if (!f.empty() && !gs->fleets()[f[0]].moving) invaded = gs->invade(1, 1, {}) == commands::RejectReason::None;
        }
        TacticalBattle* b = battles.battle();
        TacticalMode* tm = host.tactical();
        if (!b || !tm || b->frame() < 2) {
            if (started) break;
            continue;
        }
        const objects::ObjectStore& st = tm->primary();
        const mv::MovementSystem& ms = b->movement();
        if (!started) {
            third = unit_of(*b, st, d, "S10_Ranger", 3);
            if (third == objects::kNoObjectId && b->frame() < 60) continue;
            if (third == objects::kNoObjectId) {  // diagnostics: every S10_Ranger in the store and its owner
                const objects::TypeIndex rt = d.types.resolve_name("S10_Ranger");
                for (objects::ObjectHandle h : st.all_objects())
                    if (st.alive(h) && st.type(h) == rt)
                        std::printf("    store S10_Ranger %d owner %d combat %d\n", static_cast<int>(st.id(h)), static_cast<int>(st.owner(h)),
                                    b->combat().has_unit(st.id(h)) ? 1 : 0);
            }
            std::printf("  third party: map-placed ranger %d (owner 3), stand-in sides 0 %d 1 %d 3 %d\n", static_cast<int>(third),
                        b->standin_owner(0) ? 1 : 0, b->standin_owner(1) ? 1 : 0, b->standin_owner(3) ? 1 : 0);
            CHECK(third != objects::kNoObjectId && !b->standin_owner(3));
            if (third == objects::kNoObjectId) break;
            invulnerable_all(*b);
            at = st.position(st.find(third));
            started = true;
            start_frame = b->frame();
            continue;
        }
        frames = b->frame() - start_frame;
        third_goal = third_goal || ms.view(third).has_goal;
        third_moved = std::max(third_moved, dist2(st.position(st.find(third)), at));
        const tc::CombatSystem& cs = b->combat();
        for (size_t i = 0; i < cs.unit_count(); ++i) {
            const auto id = cs.unit_id_at(i);
            const objects::ObjectHandle h = st.find(id);
            if (!st.alive(h) || (st.owner(h) != 0 && st.owner(h) != 1)) continue;
            const objects::ObjectHandle ch = st.container(h);  // a squad member steers through its container
            const objects::ObjectId mover = st.exists(ch) && ms.has_unit(st.id(ch)) ? st.id(ch) : id;
            if (ms.has_unit(mover) && ms.view(mover).has_goal) ++side_goals;
        }
        if (frames >= 600) break;
    }
    std::printf("  third party over %u frames: side unit-frames with a goal %u; third party goal %d, moved %.2f\n", frames, side_goals,
                third_goal ? 1 : 0, static_cast<double>(third_moved));
    CHECK(started && frames >= 600 && side_goals > 0 && !third_goal && third_moved < 1.0f);
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_temp_prefix = "alamo_sim_bkt1_tests";
    auto d = squad_data_patched("_bkt1", bkt1_types);
    std::printf("bkt1 t01: attack staging 0x40e260 / 0x440f90 and the engagement range's pending-ability clause\n");
    test_staging(*d);
    std::printf("cmb3 t02: the fire trim's gate is the object hidden test\n");
    test_trim_gate_hidden(*d);
    std::printf("bkt1 t03: the attack formation lead skips movement-locked members; all locked waits\n");
    test_locked_lead(*d);
#ifdef ALAMO_SIM_HAVE_TACTICAL_AI
    std::printf("bkt1 t02: land guard follow - Land_Guard_Range caps R; a hidden guarded object (C 3)\n");
    test_guard_r_cap();
    test_guard_hidden_drop();
#endif
    std::printf("bkt1 t07: a third-party unit idles on land\n");
    {
        g_types_patch = bkt1_types;  // the s4 data (Hostile faction) with the bkt1 units
        const char* const old = g_temp_prefix;
        g_temp_prefix = "alamo_sim_bkt1_tests_s4";
        auto d4 = make_data(false, false, true);
        g_types_patch = nullptr;
        g_temp_prefix = old;
        test_third_party_idle(*d4);
    }
    return ::alamo::test::report("alamo_sim_bkt1_tests");
}
