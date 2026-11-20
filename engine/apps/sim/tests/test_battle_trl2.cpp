// alamo_sim_trl2_tests: trl2 / tru2 battle-flow rules on synthetic battles (battle_test_fixture.hpp, no game data).
//   * trl2 t02 (BKG1_RE_ANSWERS §7.1, 0x2aba70 / 0x29bcd0 [H]): the land population "used" is the float sum of per-object
//     shares rounded up: a company member holds Population_Value / Company_Units count, a timed landing in flight the whole
//     company (its unloaded members join at the unload's finish), and skirmish start forces never count.
#include "battle_test_fixture.hpp"
#include "tactical_battle_test_access.hpp"
#include "alamo/tactical/combat/combat_system.hpp"
#include "alamo/tactical/movement/movement_system.hpp"

namespace {

// A land battle on S10_B (invaded by player 1's S10_Squad company from planet 2). `per_tick(b, tm, host)` runs after every
// host tick once the battle exists; it returns false to stop.
template <class F>
void land_battle(Data& d, uint32_t max_frames, F per_tick, bool skirmish = false) {
    jobs::JobSystem js(jobs::JobSystemConfig{0});
    SimConfig cfg;
    cfg.seed = 2026;
    cfg.players = 4;
    SimHost host(d.types, js, cfg);
    galactic::GalacticSim* gs = host.start_galactic(d.rules, make_setup(d, true));
    CHECK(gs != nullptr);
    if (!gs) return;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = max_frames;
    opt.battle.landing_unload_timed = true;
    opt.battle.skirmish = skirmish;
    CampaignBattles battles(host, d.crules, *d.catalog,
                            [](const std::string& name, terrain::MapData& m) {
                                if (name != "S10_Land_B") return false;
                                m = land_map();
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
    bool invaded = false, seen = false;
    for (uint64_t t = 0; t < 6000; ++t) {
        host.tick_once();
        if (!invaded && host.modes().depth() == 1) {
            const std::vector<uint32_t> f = gs->fleets_at(1, 1);
            if (!f.empty() && !gs->fleets()[f[0]].moving) invaded = gs->invade(1, 1, {}) == commands::RejectReason::None;
        }
        TacticalBattle* b = battles.battle();
        TacticalMode* tm = host.tactical();
        if (!b || !tm) {
            if (seen) break;
            continue;
        }
        seen = true;
        if (!per_tick(*b, *tm, host)) break;
    }
    CHECK(seen);
}

// Drops player 1's S10_Squad reserve at the first valid cell; true when the order went through.
bool drop_reserve(TacticalBattle& b, TacticalMode& tm, SimHost& host) {
    namespace mv = alamo::tactical::movement;
    const auto reserves = b.reserves(1);
    if (reserves.empty()) return false;
    const objects::ObjectId key = reserves[0].galactic_id;
    if (b.fog()) alamo::sim::TacticalBattleTestAccess::fog(b)->reveal_all(1);
    for (int32_t cy = 6; cy < 58; cy += 2)
        for (int32_t cx = 6; cx < 58; cx += 2) {
            const mv::V2 c = mv::cell_center(cx, cy);
            if (!b.land_drop_valid(tm, 1, key, c.x, c.y, false, false)) continue;
            if (b.order_reinforce(tm, 1, key, c.x, c.y, host.random().synced()) == commands::RejectReason::None) return true;
        }
    return false;
}

// ---- trl2 t02 ----
// S10_Squad (4 S10_Trooper) at Population_Value 2: each trooper's share is 0.5.
std::string pop2_patch(std::string t) {
    const std::string at = "<GroundCompany Name=\"S10_Squad\">";
    if (const size_t p = t.find(at); p != std::string::npos) {
        const std::string pv = "<Population_Value>1</Population_Value>";
        if (const size_t q = t.find(pv, p); q != std::string::npos) t.replace(q, pv.size(), "<Population_Value>2</Population_Value>");
    }
    return t;
}

struct PopRun {
    bool dropped = false;
    float before = -1.0f;      // player 1's used at the drop order
    float in_flight = -1.0f;   // while the landing is in flight, after its first member is out
    float landed = -1.0f;      // after the unload finished, all four troopers alive
    float two_dead = -1.0f;    // two troopers killed
    float all_dead = -1.0f;    // every trooper killed
    float defender = -1.0f;    // player 0's used at frame 1 (its start forces)
};

PopRun run_population(Data& d, bool skirmish) {
    PopRun out;
    std::vector<objects::ObjectId> troopers;
    objects::ObjectId max_before = 0;
    int phase = 0;
    uint32_t mark = 0;
    land_battle(
        d, 2000,
        [&](TacticalBattle& b, TacticalMode& tm, SimHost& host) {
            objects::ObjectStore& st = tm.primary();
            auto& cs = const_cast<tactical::combat::CombatSystem&>(b.combat());
            if (!out.dropped) {
                if (b.frame() != 1) return true;
                out.defender = b.population_used(0);
                for (const objects::ObjectHandle h : st.all_objects()) max_before = std::max(max_before, st.id(h));
                out.before = b.population_used(1);
                out.dropped = drop_reserve(b, tm, host);
                return out.dropped;
            }
            if (phase == 0) {  // in flight: once a member is out
                bool out_one = false;
                for (const objects::ObjectHandle h : st.owned_by(1))
                    out_one = out_one || (st.alive(h) && st.id(h) > max_before && cs.has_unit(st.id(h)));
                if (b.landings_in_flight(1) > 0 && out_one && out.in_flight < 0.0f) out.in_flight = b.population_used(1);
                if (b.landings_in_flight(1) == 0) {
                    out.landed = b.population_used(1);
                    for (const objects::ObjectHandle h : st.owned_by(1))
                        if (st.alive(h) && st.id(h) > max_before && cs.has_unit(st.id(h))) troopers.push_back(st.id(h));
                    if (troopers.size() >= 4) {
                        cs.kill_unit(troopers[0], objects::kNoObjectId, 0, b.frame());
                        cs.kill_unit(troopers[1], objects::kNoObjectId, 0, b.frame());
                    }
                    phase = 1;
                    mark = b.frame();
                }
                return b.frame() < 1500;
            }
            if (phase == 1) {
                if (b.frame() < mark + 3) return true;
                out.two_dead = b.population_used(1);
                for (size_t i = 2; i < troopers.size(); ++i) cs.kill_unit(troopers[i], objects::kNoObjectId, 0, b.frame());
                phase = 2;
                mark = b.frame();
                return true;
            }
            if (b.frame() < mark + 3) return true;
            out.all_dead = b.population_used(1);
            return false;
        },
        skirmish);
    return out;
}

// ledger: verifies GameObjectType.Population_Value
void test_land_population() {
    g_temp_prefix = "alamo_sim_trl2_tests_pop";
    g_types_patch = pop2_patch;
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, true);
    g_types_patch = nullptr;
    const PopRun c = run_population(*d, false);
    std::printf("  trl2 t02 campaign: defender %.2f, attacker before %.2f, in flight %.2f, landed %.2f, two dead %.2f, all dead "
                "%.2f\n",
                static_cast<double>(c.defender), static_cast<double>(c.before), static_cast<double>(c.in_flight),
                static_cast<double>(c.landed), static_cast<double>(c.two_dead), static_cast<double>(c.all_dead));
    CHECK(c.dropped);
    // The landing holds the whole company (2) while it unloads; the troopers out so far add nothing yet.
    CHECK(c.in_flight == c.before + 2.0f);
    CHECK(c.landed == c.before + 2.0f);
    // Two of four troopers at 0.5 each: 1.0 (the old whole-company rule kept 2 while one member lived).
    CHECK(c.two_dead == c.before + 1.0f);
    CHECK(c.all_dead == c.before);
    const PopRun s = run_population(*d, true);
    std::printf("  trl2 t02 skirmish: defender %.2f, attacker before %.2f, landed %.2f, two dead %.2f\n",
                static_cast<double>(s.defender), static_cast<double>(s.before), static_cast<double>(s.landed),
                static_cast<double>(s.two_dead));
    // Skirmish start forces never count: nothing of player 0 (all start force) and nothing before the drop.
    CHECK(s.dropped && s.defender == 0.0f && s.before == 0.0f);
    CHECK(s.landed == 2.0f && s.two_dead == 1.0f);
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("trl2 t02: land population shares\n");
    test_land_population();
    if (g_fail) {
        std::printf("alamo_sim_trl2_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_sim_trl2_tests: OK\n");
    return 0;
}
