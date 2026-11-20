// alamo_sim_trl2_tests: trl2 / tru2 battle-flow rules on synthetic battles (battle_test_fixture.hpp, no game data).
//   * trl2 t02 (BKG1_RE_ANSWERS §7.1, 0x2aba70 / 0x29bcd0 [H]): the land population "used" is the float sum of per-object
//     shares rounded up: a company member holds Population_Value / Company_Units count, a timed landing in flight the whole
//     company (its unloaded members join at the unload's finish), and skirmish start forces never count.
//   * tru2 t02 (TRU1_C_RE §2.1): at a space retreat request every listed ship turns in place (at its turn rate) to one
//     common heading (the reverse of the attacker entry yaw); the star base does not turn.
//   * trl2 t04 (MCX4_PRES_RE_ANSWERS_2 Q4.2): a player cancel during the countdown clears the retreat (a new request is
//     accepted), stops the turning ships and posts a by_player cancel record; the retreat check turning non-zero during the
//     countdown (DISABLE_RETREAT) cancels on its own (by_player false).
//   * trl2 t05 (BT §3.4 vf11): at a land retreat's end a structure of the retreating player (the attacker's map-placed
//     bunker) dies, its transport and its units retreat.
#include "battle_test_fixture.hpp"
#include "tactical_battle_test_access.hpp"
#include "alamo/tactical/combat/combat_system.hpp"
#include "alamo/tactical/movement/movement_system.hpp"

namespace {

// A land battle on S10_B (invaded by player 1's S10_Squad company from planet 2). `per_tick(b, tm, host)` runs after every
// host tick once the battle exists; it returns false to stop.
template <class F>
void land_battle(Data& d, uint32_t max_frames, F per_tick, bool skirmish = false, const terrain::MapData* map = nullptr,
                 uint32_t end_hold = 0) {
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
    opt.battle.end_hold_frames = end_hold;
    CampaignBattles battles(host, d.crules, *d.catalog,
                            [map](const std::string& name, terrain::MapData& m) {
                                if (name != "S10_Land_B") return false;
                                m = map ? *map : land_map();
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

// A space battle on S10_B (player 1's fleet attacks player 0's frigate, corvette and star base). `per_tick(b, tm, host)`
// runs after every host tick once the battle exists; it returns false to stop. Stand-in orders off.
template <class F>
void space_battle(Data& d, uint32_t max_frames, F per_tick, float retreat_countdown) {
    jobs::JobSystem js(jobs::JobSystemConfig{0});
    SimConfig cfg;
    cfg.seed = 2026;
    cfg.players = 4;
    SimHost host(d.types, js, cfg);
    galactic::GalacticSim* gs = host.start_galactic(d.rules, make_setup(d, false));
    CHECK(gs != nullptr);
    if (!gs) return;
    galactic::ConflictRules crules = d.crules;
    for (galactic::ConflictFactionInfo& fi : crules.factions) fi.space_retreat_countdown = retreat_countdown;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = max_frames;
    opt.battle.order_interval = 0;
    opt.battle.fog = false;
    CampaignBattles battles(host, crules, *d.catalog,
                            [](const std::string& name, terrain::MapData& m) {
                                if (name != "S10_Space_B") return false;
                                m = space_map();
                                // yaw 0: facing (0, -1, 0) (map placement convention) -> the retreat heading (0, 1, 0), 90 degrees
                                terrain::ObjectPlacement mk;
                                mk.type_crc = name_crc("Attacker Entry Position");
                                mk.owner = 1;
                                mk.position[0] = 1000.0f;
                                mk.position[1] = -2000.0f;
                                m.objects.push_back(mk);
                                return true;
                            },
                            opt);
    CHECK(battles.attach(nullptr));
    const std::vector<uint32_t> uf = gs->fleets_at(2, 1);
    if (!uf.empty()) CHECK(gs->move_fleet(1, uf[0], 1) == commands::RejectReason::None);
    bool seen = false;
    for (uint64_t t = 0; t < 8000; ++t) {
        host.tick_once();
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

// Player 0's battle units with a movement unit (the listed ones) and the others, ascending id.
void own_units(TacticalBattle& b, TacticalMode& tm, std::vector<objects::ObjectId>& movers, std::vector<objects::ObjectId>& still) {
    objects::ObjectStore& st = tm.primary();
    movers.clear();
    still.clear();
    for (const objects::ObjectHandle h : st.owned_by(0)) {
        if (!st.alive(h) || !b.combat().has_unit(st.id(h))) continue;
        (b.movement().has_unit(st.id(h)) ? movers : still).push_back(st.id(h));
    }
    std::sort(movers.begin(), movers.end());
    std::sort(still.begin(), still.end());
}

float wrap180(float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a <= -180.0f) a += 360.0f;
    return a;
}

// ---- tru2 t02 ----
void test_retreat_heading() {
    g_temp_prefix = "alamo_sim_trl2_tests_heading";
    auto d = make_data();
    std::vector<objects::ObjectId> movers, still;
    std::vector<float> yaw0, still_face0;
    uint32_t req = 0;
    bool requested = false, turned = false, done = false, same = false, still_kept = true;
    float spread = 0.0f;
    space_battle(
        *d, 3000,
        [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
            objects::ObjectStore& st = tm.primary();
            if (!requested) {
                if (b.retreat_block(0) != 0) return true;
                own_units(b, tm, movers, still);
                for (const objects::ObjectId id : movers) yaw0.push_back(b.movement().view(id).yaw);
                for (const objects::ObjectId id : still) still_face0.push_back(st.facing(st.find(id)).x);
                requested = b.request_retreat(0);
                req = b.frame();
                return requested;
            }
            if (b.retreating_player() != 0) return false;
            bool all_done = true;
            for (size_t i = 0; i < movers.size(); ++i) {
                const tactical::movement::UnitView v = b.movement().view(movers[i]);
                turned = turned || std::fabs(wrap180(v.yaw - yaw0[i])) > 1.0f;
                all_done = all_done && !v.has_goal;
            }
            for (size_t i = 0; i < still.size(); ++i)
                if (st.alive(st.find(still[i])) && st.facing(st.find(still[i])).x != still_face0[i]) still_kept = false;
            if (all_done && b.frame() > req + 2) {
                done = true;
                float lo = 1e9f, hi = -1e9f;
                const float ref = b.movement().view(movers[0]).yaw;
                for (const objects::ObjectId id : movers) {
                    const float dlt = wrap180(b.movement().view(id).yaw - ref);
                    lo = std::min(lo, dlt);
                    hi = std::max(hi, dlt);
                }
                spread = hi - lo;
                same = spread < 0.05f && std::fabs(wrap180(ref - 90.0f)) < 0.05f;
                return false;
            }
            return true;
        },
        60.0f);
    std::printf("  tru2 t02: requested %d at %u, %zu ships, turned %d, all done %d, yaw spread %.3f, star base kept %d (%zu)\n",
                requested, req, movers.size(), turned, done, static_cast<double>(spread), still_kept, still.size());
    CHECK(requested && movers.size() >= 2 && turned && done && same && still_kept);
}

// ---- trl2 t04 ----
void test_retreat_cancel() {
    g_temp_prefix = "alamo_sim_trl2_tests_cancel";
    auto d = make_data();
    std::vector<objects::ObjectId> movers, still;
    std::vector<float> yaw_at_cancel;
    int phase = 0;
    uint32_t mark = 0;
    bool cancel_ok = false, record_ok = false, frozen = true, rerequest = false, auto_ok = false;
    size_t record_units = 0;
    space_battle(
        *d, 3000,
        [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
            if (phase == 0) {
                if (b.retreat_block(0) != 0) return true;
                own_units(b, tm, movers, still);
                if (!b.request_retreat(0)) return false;
                phase = 1;
                mark = b.frame();
                return true;
            }
            if (phase == 1) {
                if (b.frame() < mark + 10) return true;
                cancel_ok = b.retreat_cancellable(0) && !b.retreat_cancellable(1) && b.cancel_retreat(0, true) &&
                            b.retreating_player() < 0 && b.retreat_block(0) == 0;
                const auto recs = b.retreat_cancels();
                record_ok = recs.size() == 1 && recs[0].player == 0 && recs[0].by_player;
                record_units = recs.empty() ? 0 : recs[0].units.size();
                for (const objects::ObjectId id : movers) yaw_at_cancel.push_back(b.movement().view(id).yaw);
                phase = 2;
                mark = b.frame();
                return true;
            }
            if (phase == 2) {
                for (size_t i = 0; i < movers.size(); ++i) {
                    const tactical::movement::UnitView v = b.movement().view(movers[i]);
                    if (v.yaw != yaw_at_cancel[i] || v.has_goal) frozen = false;
                }
                if (b.frame() < mark + 30) return true;
                rerequest = b.request_retreat(0);
                b.set_retreat_off(0, true);  // the story's DISABLE_RETREAT: the retreat check turns non-zero
                phase = 3;
                return rerequest;
            }
            const auto recs = b.retreat_cancels();
            auto_ok = b.retreating_player() < 0 && recs.size() == 1 && recs[0].player == 0 && !recs[0].by_player;
            return false;
        },
        60.0f);
    std::printf("  trl2 t04: cancel %d, record %d (%zu units), ships stopped %d, new request %d, automatic cancel %d\n", cancel_ok,
                record_ok, record_units, frozen, rerequest, auto_ok);
    CHECK(cancel_ok && record_ok && record_units == movers.size() && frozen && rerequest && auto_ok);
}

// ---- trl2 t05: space ----
// S10_Star_Base_1 names T_SD_Boom (a PARTICLE) as its Retreat_Self_Destruct_Explosion.
std::string self_destruct_patch(std::string t) {
    const std::string at = "<StarBase Name=\"S10_Star_Base_1\">";
    if (const size_t p = t.find(at); p != std::string::npos)
        t.insert(p + at.size(), "<Retreat_Self_Destruct_Explosion>T_SD_Boom</Retreat_Self_Destruct_Explosion>");
    const size_t end = t.find("</S10>");
    if (end != std::string::npos) t.insert(end, " <SpaceProp Name=\"T_SD_Boom\"><Behavior>PARTICLE</Behavior></SpaceProp>\n");
    return t;
}

// TRU1_C_RE §2.4: past cinematic counter 180 the retreating player's star base spawns its Retreat_Self_Destruct_Explosion
// (owned by that player) and dies.
// ledger: verifies GameObjectType.Retreat_Self_Destruct_Explosion
void test_starbase_self_destruct() {
    g_temp_prefix = "alamo_sim_trl2_tests_self_destruct";
    g_types_patch = self_destruct_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const uint32_t boom_t = type_of(*d, "T_SD_Boom"), base_t = type_of(*d, "S10_Star_Base_1");
    bool requested = false, flight = false, boom = false, boom_owner_ok = false, base_died = false;
    objects::ObjectId base = objects::kNoObjectId;
    space_battle(
        *d, 3000,
        [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
            objects::ObjectStore& st = tm.primary();
            if (!requested) {
                if (b.retreat_block(0) != 0) return true;
                for (const objects::ObjectHandle h : st.owned_by(0))
                    if (st.alive(h) && static_cast<uint32_t>(st.type(h)) == base_t) base = st.id(h);
                requested = b.request_retreat(0);
                return requested;
            }
            flight = flight || b.retreat_flight_state() != 0;
            for (const objects::ObjectHandle h : st.all_objects())
                if (st.exists(h) && static_cast<uint32_t>(st.type(h)) == boom_t) {
                    boom = true;
                    boom_owner_ok = st.owner(h) == 0;
                }
            const objects::ObjectHandle bh = st.find(base);
            base_died = base_died || !st.alive(bh) || st.dying(bh);
            return !(boom && base_died);
        },
        1.0f);
    std::printf("  trl2 t05 space: requested %d, flight %d, explosion %d (owner 0: %d), star base died %d\n", requested, flight, boom,
                boom_owner_ok, base_died);
    CHECK(requested && flight && boom && boom_owner_ok && base_died);
}

// ---- trl2 t05 ----
// The landing data plus T_Bunker, a structure (no locomotor).
std::string bunker_patch(std::string t) {
    const size_t end = t.find("</S10>");
    if (end != std::string::npos)
        t.insert(end, " <GroundStructure Name=\"T_Bunker\"><Tactical_Health>500</Tactical_Health><CategoryMask>Structure</CategoryMask>"
                      "<Victory_Relevant>No</Victory_Relevant></GroundStructure>\n");
    return t;
}

void test_land_retreat_end() {
    g_temp_prefix = "alamo_sim_trl2_tests_retreat_end";
    g_types_patch = bunker_patch;
    g_constants_extra = " <Land_Retreat_Allowed_Countdown_Seconds>1.0</Land_Retreat_Allowed_Countdown_Seconds>\n";
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, true);
    g_types_patch = nullptr;
    g_constants_extra = nullptr;
    const uint32_t bunker_t = type_of(*d, "T_Bunker"), lander_t = type_of(*d, "S10_Lander"), trooper_t = type_of(*d, "S10_Trooper");
    bool adopted = false, requested = false, completed = false;
    int lander_left = 0, troopers_left = 0;
    uint32_t last_frame = 0;
    int32_t last_block = -1;
    std::vector<TacticalBattle::RemovalRecord> removals;
    land_battle(
        *d, 4000,
        [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
            if (b.frame() == 1) {  // the attacker's (player 1) bunker and transport, as script spawns (real battle units)
                namespace mv = alamo::tactical::movement;
                objects::ObjectStore& st = tm.primary();
                const auto spawn = [&](uint32_t type, int32_t cx) {
                    objects::SpawnParams sp;
                    sp.type = static_cast<objects::TypeIndex>(type);
                    sp.owner = 1;
                    const mv::V2 c = mv::cell_center(cx, 50);
                    sp.position = Vec3{c.x, c.y, 0.0f};
                    sp.facing = Vec3{1.0f, 0.0f, 0.0f};
                    const objects::ObjectHandle h = b.script_spawn(tm, sp);
                    return st.alive(h) && b.combat().has_unit(st.id(h));
                };
                adopted = spawn(bunker_t, 50) && spawn(lander_t, 54);
            }
            if (!requested && adopted && b.retreat_block(1) == 0) requested = b.request_retreat(1);
            if (requested && !completed && b.retreating_player() < 0) {  // the retreat completed this tick
                completed = true;
                objects::ObjectStore& st = tm.primary();
                for (const objects::ObjectHandle h : st.owned_by(1)) {
                    if (!st.alive(h) || st.dying(h)) continue;
                    if (static_cast<uint32_t>(st.type(h)) == lander_t) ++lander_left;
                    if (static_cast<uint32_t>(st.type(h)) == trooper_t) ++troopers_left;
                }
            }
            last_frame = b.frame();
            last_block = b.retreat_block(1);
            removals.assign(b.removals().begin(), b.removals().end());
            return true;
        },
        false, nullptr);
    int bunker_dead = 0, lander_dead = 0, troopers_dead = 0;
    for (const TacticalBattle::RemovalRecord& r : removals) {
        if (r.owner != 1 || r.survived) continue;
        if (r.object_type == bunker_t) ++bunker_dead;
        if (r.object_type == lander_t) ++lander_dead;
        if (r.object_type == trooper_t) ++troopers_dead;
    }
    std::printf("  trl2 t05: last frame %u, retreat block %d\n", last_frame, last_block);
    std::printf("  trl2 t05: adopted %d, requested %d; bunker died %d, lander left %d / died %d, troopers left %d / died %d\n", adopted,
                requested, bunker_dead, lander_left, lander_dead, troopers_left, troopers_dead);
    CHECK(adopted && requested && completed && bunker_dead == 1 && lander_left == 1 && lander_dead == 0 && troopers_left > 0);
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
    std::printf("tru2 t02: space retreat heading\n");
    test_retreat_heading();
    std::printf("trl2 t04: retreat cancel\n");
    test_retreat_cancel();
    std::printf("trl2 t05: land retreat end\n");
    test_land_retreat_end();
    test_starbase_self_destruct();
    if (g_fail) {
        std::printf("alamo_sim_trl2_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_sim_trl2_tests: OK\n");
    return 0;
}
