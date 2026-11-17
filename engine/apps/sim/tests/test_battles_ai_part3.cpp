// test_battles_ai: part 3 - cmb1 (land Auto_Engage, land scan range, land Order_Attack issue semantics; BKR1_RE_ANSWERS Q2 /
// Q8 [H], FIGHTER_FLYING §4.10 A / §4.11 B / E [H]).
#include "test_battles_ai_common.hpp"

#include <cstring>

namespace bo3split {
namespace {

// The chase ranges on the land types (the test data has none): Idle_Chase_Range 300, Attack_Move_Response_Range 200,
// Guard_Chase_Range 400 on the company (a squad container's ranges, 0x3973b0) and the trooper (a unit's own).
std::string chase_ranges(std::string t) {
    for (const char* at : {"<GroundCompany Name=\"S10_Squad\">", "<GroundInfantry Name=\"S10_Trooper\">"})
        if (const size_t p = t.find(at); p != std::string::npos)
            t.insert(p + std::strlen(at), "<Idle_Chase_Range>300</Idle_Chase_Range><Attack_Move_Response_Range>200</Attack_Move_Response_Range>"
                                          "<Guard_Chase_Range>400</Guard_Chase_Range>");
    return t;
}

std::unique_ptr<Data> chase_data(bool squad) {
    static std::string prefix;
    const char* const old = g_temp_prefix;
    prefix = std::string(old) + (squad ? "_chase_squad" : "_chase_unit");
    g_temp_prefix = prefix.c_str();
    g_types_patch = chase_ranges;
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
                       false, false, squad);
    g_types_patch = nullptr;
    g_temp_prefix = old;
    return d;
}

const LandFormationRecord* record_of(const TacticalBattle& b, objects::ObjectId mover) {
    for (const LandFormationRecord& r : b.land_formations())
        if (r.mover == mover) return &r;
    return nullptr;
}

float dist2d(mv::V2 a, Vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

}  // namespace

// cmb1 t01 / t03 (BKR1 Q2 / Q8 [H]): a land squad (TEAM container, TeamTargeting on the container) and an enemy squad 450
// away (beyond the weapon range 250, inside 250 + Idle_Chase_Range 300).
//   (i) never ordered: no formation record -> scan R, no Auto_Engage: no target, no push for 120 frames;
//   (ii) a short move order: the record (position block) once arrived answers 0x4160a0 = 1 (an arrived single position
//        block) -> the scan cube R + Rr finds the enemy and LocomotorCommon 0x42d0f0 pushes the auto attack block: the
//        record's `pushed` names an enemy member, the squad moves toward it;
//   (iii) the enemy squad is moved 2500 away: the auto block's 30-frame leash (0x4165f0: the object farther than Rr + R from
//        the slot goal) fails -> 0x4105a0 pops it and the position block resumes (the squad's goal is its order point again).
struct LandAutoEngageRun {
    bool found = false, idle_none = false, ordered = false, arrived = false, pushed = false, moved_toward = false;
    bool popped = false, resumed = false;
    uint32_t push_frames = 0, pop_frames = 0;
};

LandAutoEngageRun run_land_auto_engage(const Data& d) {
    LandAutoEngageRun out;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 6000;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;  // no stand-in orders
    opt.battle.fog = false;
    ClashSession s(d, 0, opt, {"S10_Squad"}, {}, true);
    if (!s.gs) return out;
    int stage = 0;
    uint32_t stage_frame = 0;
    objects::ObjectId sq = objects::kNoObjectId, ec = objects::kNoObjectId;
    std::vector<objects::ObjectId> mem, emem;
    Vec3 dir{}, dest{};
    for (uint64_t t = 0; t < 6000 && stage < 9; ++t) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) {
            if (stage > 0) break;
            continue;
        }
        objects::ObjectStore& st = tm->primary();
        auto& cs = const_cast<tc::CombatSystem&>(b->combat());
        const mv::MovementSystem& ms = b->movement();
        if (stage == 0) {
            std::map<uint32_t, std::vector<objects::ObjectId>> teams;
            for (size_t i = 0; i < cs.unit_count(); ++i) {
                const auto id = cs.unit_id_at(i);
                cs.make_invulnerable(id, 1000000, b->frame());
                if (cs.team(id) != 0 && cs.alive(id)) teams[cs.team(id)].push_back(id);
            }
            std::vector<objects::ObjectId> enemies;
            for (const auto& [tid, m] : teams) {
                const auto cid = static_cast<objects::ObjectId>(tid);
                const objects::ObjectHandle th = st.find(cid);
                if (m.size() < 2 || !st.alive(th) || !ms.has_unit(cid)) continue;
                if (st.owner(th) == 0 && sq == objects::kNoObjectId) {
                    sq = cid;
                    mem = m;
                } else if (st.owner(th) == 1) {
                    enemies.push_back(cid);
                }
            }
            out.found = sq != objects::kNoObjectId && !enemies.empty();
            CHECK(out.found);
            if (!out.found) break;
            ec = enemies.front();
            emem = teams[static_cast<uint32_t>(ec)];
            const Vec3 tp = st.position(st.find(ec)), ap = st.position(st.find(sq));
            const float dx = ap.x - tp.x, dy = ap.y - tp.y, dl = std::sqrt(dx * dx + dy * dy);
            dir = dl > 1.0f ? Vec3{dx / dl, dy / dl, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
            // the other enemy squads far away behind the first one (only `ec` is near)
            for (size_t k = 1; k < enemies.size(); ++k) {
                const objects::ObjectHandle h = st.find(enemies[k]);
                (void)b->teleport(*tm, enemies[k], Vec3{tp.x - dir.x * (2500.0f + 300.0f * static_cast<float>(k)), tp.y - dir.y * 2500.0f, tp.z},
                                  st.facing(h));
            }
            CHECK(b->teleport(*tm, sq, Vec3{tp.x + dir.x * 450.0f, tp.y + dir.y * 450.0f, ap.z}, st.facing(st.find(sq))));
            stage = 1;
            stage_frame = b->frame();
            continue;
        }
        const mv::UnitView v = ms.view(sq);
        if (stage == 1) {  // (i) never ordered
            if (b->frame() < stage_frame + 120) continue;
            // (a squad's TeamTargeting target: read through a member, CombatSystem::current_target)
            out.idle_none = record_of(*b, sq) == nullptr && cs.current_target(mem.front()) == tc::kNoObject && b->stats().land_auto_pushes == 0;
            dest = Vec3{v.pos.x - dir.y * 30.0f, v.pos.y + dir.x * 30.0f, 0.0f};
            out.ordered = b->order_move(*tm, 0, mem, dest.x, dest.y, s.host->random().synced()) == commands::RejectReason::None;
            CHECK(out.ordered && record_of(*b, sq) != nullptr);
            stage = 2;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 2) {  // (ii) arrival, then the push
            const LandFormationRecord* r = record_of(*b, sq);
            if (!out.arrived) {
                out.arrived = !v.has_goal && b->frame() > stage_frame + 5;
                if (out.arrived) stage_frame = b->frame();
                else if (b->frame() > stage_frame + 1500) stage = 9;
                continue;
            }
            if (r && r->pushed != objects::kNoObjectId) {
                out.pushed = std::find(emem.begin(), emem.end(), r->pushed) != emem.end();
                out.push_frames = b->frame() - stage_frame;
                stage = 3;
                stage_frame = b->frame();
            } else if (b->frame() > stage_frame + 300) {
                stage = 9;
            }
            continue;
        }
        if (stage == 3) {  // the squad walks toward the enemy; then the enemy leaves
            if (b->frame() < stage_frame + 20) continue;
            const Vec3 ep = st.position(st.find(ec));
            out.moved_toward = v.has_goal && dist2d(v.goal, ep) < dist2d(mv::V2{dest.x, dest.y}, ep);
            const objects::ObjectHandle eh = st.find(ec);
            CHECK(b->teleport(*tm, ec, Vec3{ep.x - dir.x * 2500.0f, ep.y - dir.y * 2500.0f, ep.z}, st.facing(eh)));
            stage = 4;
            stage_frame = b->frame();
            continue;
        }
        if (stage == 4) {  // (iii) the leash pops the auto block, the order's position block resumes
            const LandFormationRecord* r = record_of(*b, sq);
            if (r && r->pushed == objects::kNoObjectId && b->stats().land_auto_pops > 0) {
                out.popped = true;
                out.pop_frames = b->frame() - stage_frame;
                out.resumed = v.has_goal && dist2d(v.goal, dest) < 40.0f;
                stage = 9;
            } else if (b->frame() > stage_frame + 200) {
                stage = 9;
            }
        }
    }
    return out;
}

// cmb1 t03 (BKR1 Q8 table [H]): the scan cube of a non-turret land unit moving on a plain Move_To is 0 - a squad that starts
// its move 200 from an enemy squad (inside the weapon range 250) acquires nothing while it walks away; the same start with
// an Attack_Move (R + Attack_Move_Response_Range while under way) acquires the enemy.
struct LandScanMovingRun {
    bool found = false, ordered = false;
    bool acquired = false;
    uint32_t scans = 0;
};

LandScanMovingRun run_land_scan_moving(const Data& d, bool attack_move) {
    LandScanMovingRun out;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 3000;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;
    opt.battle.fog = false;
    ClashSession s(d, 0, opt, {"S10_Squad"}, {}, true);
    if (!s.gs) return out;
    int stage = 0;
    uint32_t stage_frame = 0;
    objects::ObjectId sq = objects::kNoObjectId, ec = objects::kNoObjectId;
    std::vector<objects::ObjectId> mem;
    for (uint64_t t = 0; t < 3000 && stage < 9; ++t) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) {
            if (stage > 0) break;
            continue;
        }
        objects::ObjectStore& st = tm->primary();
        auto& cs = const_cast<tc::CombatSystem&>(b->combat());
        const mv::MovementSystem& ms = b->movement();
        if (stage == 0) {
            std::map<uint32_t, std::vector<objects::ObjectId>> teams;
            for (size_t i = 0; i < cs.unit_count(); ++i) {
                const auto id = cs.unit_id_at(i);
                cs.make_invulnerable(id, 1000000, b->frame());
                if (cs.team(id) != 0 && cs.alive(id)) teams[cs.team(id)].push_back(id);
            }
            for (const auto& [tid, m] : teams) {
                const auto cid = static_cast<objects::ObjectId>(tid);
                const objects::ObjectHandle th = st.find(cid);
                if (m.size() < 2 || !st.alive(th) || !ms.has_unit(cid)) continue;
                if (st.owner(th) == 0 && sq == objects::kNoObjectId) {
                    sq = cid;
                    mem = m;
                } else if (st.owner(th) == 1 && ec == objects::kNoObjectId) {
                    ec = cid;
                }
            }
            out.found = sq != objects::kNoObjectId && ec != objects::kNoObjectId && cs.current_target(mem.front()) == tc::kNoObject;
            CHECK(out.found);
            if (!out.found) break;
            const Vec3 tp = st.position(st.find(ec)), ap = st.position(st.find(sq));
            const float dx = ap.x - tp.x, dy = ap.y - tp.y, dl = std::sqrt(dx * dx + dy * dy);
            const Vec3 dir = dl > 1.0f ? Vec3{dx / dl, dy / dl, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
            CHECK(b->teleport(*tm, sq, Vec3{tp.x + dir.x * 200.0f, tp.y + dir.y * 200.0f, ap.z}, st.facing(st.find(sq))));
            // away from the enemy, slowly enough (speed 1) that it stays inside 250 for the first 45 frames
            out.ordered = b->order_move(*tm, 0, mem, tp.x + dir.x * 1400.0f, tp.y + dir.y * 1400.0f, s.host->random().synced(),
                                        mv::kNoFacing, attack_move) == commands::RejectReason::None;
            CHECK(out.ordered);
            stage = 1;
            stage_frame = b->frame();
            continue;
        }
        if (b->frame() > stage_frame + 46) {
            stage = 9;
            continue;
        }
        out.acquired = out.acquired || cs.current_target(mem.front()) != tc::kNoObject;
    }
    return out;
}

// cmb1 t02 (BKR1 Q2 [H], TACTICAL_COMBAT §2.8 C): a land Order_Attack(ordered 1) on a unit that is not a team container /
// member / LUCKY_SHOT (the default test data: troopers without Create_Team) takes the weapon path: no ordered (unlocked)
// target slot, the weapon hard points / weapon aimed at the target, the target the formation's attack goal; path B locks it
// (ordered target, lock set) once it is inside weapon range.
struct LandOrderIssueRun {
    bool found = false, ordered = false, no_slot = false, goal_set = false, aimed = false, locked = false;
    uint32_t lock_frames = 0;
};

LandOrderIssueRun run_land_order_issue(const Data& d) {
    LandOrderIssueRun out;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 3000;
    opt.battle.skip_human_units = true;
    opt.battle.order_interval = 0;
    opt.battle.fog = false;
    ClashSession s(d, 0, opt, {"S10_Squad"}, {}, true);
    if (!s.gs) return out;
    int stage = 0;
    uint32_t stage_frame = 0;
    objects::ObjectId me = objects::kNoObjectId, target = objects::kNoObjectId;
    for (uint64_t t = 0; t < 3000 && stage < 9; ++t) {
        TacticalBattle* b = s.tick();
        TacticalMode* tm = s.host->tactical();
        if (!b || !tm || b->frame() < 3) {
            if (stage > 0) break;
            continue;
        }
        objects::ObjectStore& st = tm->primary();
        auto& cs = const_cast<tc::CombatSystem&>(b->combat());
        if (stage == 0) {
            for (size_t i = 0; i < cs.unit_count(); ++i) {
                const auto id = cs.unit_id_at(i);
                cs.make_invulnerable(id, 1000000, b->frame());
                const objects::ObjectHandle h = st.find(id);
                if (!st.alive(h) || cs.team(id) != 0 || !b->movement().has_unit(id)) continue;
                if (st.owner(h) == 0 && me == objects::kNoObjectId) me = id;
                if (st.owner(h) == 1 && target == objects::kNoObjectId) target = id;
            }
            out.found = me != objects::kNoObjectId && target != objects::kNoObjectId;
            CHECK(out.found);
            if (!out.found) break;
            const Vec3 tp = st.position(st.find(target)), ap = st.position(st.find(me));
            const float dx = ap.x - tp.x, dy = ap.y - tp.y, dl = std::sqrt(dx * dx + dy * dy);
            const Vec3 dir = dl > 1.0f ? Vec3{dx / dl, dy / dl, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
            CHECK(b->teleport(*tm, me, Vec3{tp.x + dir.x * 700.0f, tp.y + dir.y * 700.0f, ap.z}, st.facing(st.find(me))));
            const objects::ObjectId one[] = {me};
            out.ordered = b->order_attack(*tm, 0, one, target, s.host->random().synced()) == commands::RejectReason::None;
            out.no_slot = cs.ordered_target(me) == tc::kNoObject;
            out.goal_set = cs.attack_goal(me) == target;
            out.aimed = cs.weapon_aim(me) == target;
            stage = 1;
            stage_frame = b->frame();
            continue;
        }
        if (cs.ordered_target(me) == target && cs.order_locked(me)) {
            out.locked = true;
            out.lock_frames = b->frame() - stage_frame;
            stage = 9;
        } else if (b->frame() > stage_frame + 2000) {
            stage = 9;
        }
    }
    return out;
}

void test_land_auto_engage() {
    {
        auto d = chase_data(true);
        const LandAutoEngageRun r = run_land_auto_engage(*d);
        std::printf("  land Auto_Engage: never ordered -> no target, no push %d; ordered %d, arrived %d, pushed onto the enemy %d after "
                    "%u frame(s), walking toward it %d; enemy gone -> popped %d after %u frame(s), order resumed %d\n",
                    r.idle_none ? 1 : 0, r.ordered ? 1 : 0, r.arrived ? 1 : 0, r.pushed ? 1 : 0, r.push_frames,
                    r.moved_toward ? 1 : 0, r.popped ? 1 : 0, r.pop_frames, r.resumed ? 1 : 0);
        CHECK(r.found && r.idle_none && r.ordered && r.arrived && r.pushed && r.push_frames <= 120 && r.moved_toward);
        CHECK(r.popped && r.pop_frames <= 61 && r.resumed);
    }
    {
        auto d = chase_data(true);
        const LandScanMovingRun plain = run_land_scan_moving(*d, false);
        const LandScanMovingRun am = run_land_scan_moving(*d, true);
        std::printf("  land scan range while moving: plain Move_To acquired %d (cube 0), Attack_Move acquired %d (R + Rr)\n",
                    plain.acquired ? 1 : 0, am.acquired ? 1 : 0);
        CHECK(plain.found && plain.ordered && !plain.acquired);
        CHECK(am.found && am.ordered && am.acquired);
    }
    {
        auto d = chase_data(false);
        const LandOrderIssueRun r = run_land_order_issue(*d);
        std::printf("  land Order_Attack (weapon path): ordered %d, no ordered slot %d, attack goal %d, weapon aim %d; path B locked "
                    "it %d after %u frame(s)\n",
                    r.ordered ? 1 : 0, r.no_slot ? 1 : 0, r.goal_set ? 1 : 0, r.aimed ? 1 : 0, r.locked ? 1 : 0, r.lock_frames);
        CHECK(r.found && r.ordered && r.no_slot && r.goal_set && r.aimed && r.locked);
    }
}

}  // namespace bo3split
