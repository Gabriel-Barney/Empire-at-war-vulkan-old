// alamo_sim_r3g_tests: r3g "Animation-timed gameplay" on synthetic land battles (battle_test_fixture.hpp, no game data).
//   * t01 (A1_READINGS §35.2): a timed land Reinforce whose landing object has Land / Takeoff clips (151 frames at 30 fps,
//     played at 1.15) unloads only once Land has passed 98 % (129 frames after the clearance), and is removed only once
//     Takeoff has passed 98 % after the finish. Without the clips (the other landing tests) both pass at once.
//   * t05 (MOVEMENT §5.5, S9_DEATH_CLONE_SHIELDS §1.4): a crush with no Squash_Damage_Type kills with kind 14
//     (Damage_Crush, the type default 0x367250 writes) through the victim's Damage_Crush clone, whose death clip is Crushed;
//     a crusher whose Squash_Damage_Type names another tDamageType kills with that kind.
//   * t06 (TACTICAL_GAPS §6.2): a trooper under SPREAD_OUT idles on Idle Crouch (71); off again, on Idle.
#include "battle_test_fixture.hpp"
#include "alamo/tactical/abilities/ability_types.hpp"
#include "alamo/tactical/combat/combat_system.hpp"
#include "alamo/tactical/movement/movement_system.hpp"

namespace {

#ifdef ALAMO_SIM_HAVE_ANIM
// One-bone skeleton, clips without tracks: only the frame counts matter.
std::unique_ptr<anim::AnimModel> synth_model(const char* base, std::initializer_list<std::pair<int32_t, uint32_t>> clips) {
    auto m = std::make_unique<anim::AnimModel>();
    m->model = std::string(base) + ".ALO";
    m->base = base;
    m->skeleton.names = {"Root"};
    m->skeleton.parents = {-1};
    m->skeleton.billboard_mode = {0u};
    m->skeleton.bind_local = {anim::Mat34{}};
    m->has_skeleton = true;
    for (const auto& [type, frames] : clips) {
        auto c = std::make_shared<anim::Clip>();
        c->name = anim::clip_file_name(m->base, type, 0);
        c->frame_count = frames;
        c->fps = 30.0f;
        anim::ClipRef r;
        r.clip = c;
        m->clips[static_cast<size_t>(type)].push_back(r);
    }
    anim::bind_clips(*m);
    return m;
}
#endif

// A land battle on S10_B (invaded by player 1's S10_Squad company from planet 2). `per_tick(b, tm)` runs after every host
// tick once the battle exists; it returns false to stop. `animation` (may be null) is attached.
template <class F>
void land_battle(Data& d, SimAnimation* animation, uint32_t max_frames, F per_tick) {
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
    if (animation) {
        animation->attach(host, false);
        opt.animation = animation;
    }
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
    if (animation) animation->detach(host);
}

// ---- t01 ----
void test_landing_clips() {
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = "alamo_sim_r3g_tests";
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, true);
    const objects::TypeIndex landing_t = d->types.resolve_name("S10_Lander_Landing");
    CHECK(landing_t != objects::kNone);
    const auto model = synth_model("R3G_LANDER", {{anim::kAnimIdle, 30u}, {0x2b, 151u}, {0x2c, 151u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(landing_t, objects::ModeKind::Land, model.get());
    namespace mv = alamo::tactical::movement;
    bool dropped = false;
    objects::ObjectId landing = objects::kNoObjectId;
    uint32_t drop = 0, spawned_before = 0;
    int team0 = -1, team1 = -1, finish = -1, removed = -1;
    land_battle(*d, &animation, 1500, [&](TacticalBattle& b, TacticalMode& tm, SimHost& host) {
        objects::ObjectStore& st = tm.primary();
        if (!dropped) {
            if (b.frame() != 1) return true;
            const auto reserves = b.reserves(1);
            CHECK(reserves.size() == 1);
            if (reserves.empty()) return false;
            const objects::ObjectId key = reserves[0].galactic_id;
            spawned_before = b.stats().spawned;
            if (b.fog()) b.fog()->reveal_all(1);
            for (int32_t cy = 6; cy < 58 && !dropped; cy += 2)
                for (int32_t cx = 6; cx < 58 && !dropped; cx += 2) {
                    const mv::V2 c = mv::cell_center(cx, cy);
                    if (!b.land_drop_valid(tm, 1, key, c.x, c.y, false, false)) continue;
                    if (b.order_reinforce(tm, 1, key, c.x, c.y, host.random().synced()) != commands::RejectReason::None) continue;
                    dropped = true;
                    drop = b.frame();
                    for (const objects::ObjectHandle h : st.all_objects())
                        if (st.alive(h) && st.type(h) == landing_t) landing = st.id(h);
                }
            CHECK(dropped && landing != objects::kNoObjectId);
            return dropped;
        }
        const int since = static_cast<int>(b.frame() - drop);
        const uint32_t spawned = b.stats().spawned - spawned_before;
        if (team0 < 0 && spawned >= 2) team0 = since;
        if (team1 < 0 && spawned >= 4) team1 = since;
        if (finish < 0 && b.landings_in_flight(1) == 0) finish = since;
        if (removed < 0 && !st.alive(st.find(landing))) removed = since;
        return removed < 0 && since < 900;
    });
    // Clearance at +1 starts Land: 98 % of 151 frames at 1.15 is passed 129 frames later (+130, the step timer starts);
    // teams at +190 / +250 (2 s steps), the finish at +310 starts Takeoff; removed 129 frames later (+439). Without the
    // clips: +61 / +121 / +181 (test_landing_drop).
    std::printf("  r3g landing clips: team 0 +%d, team 1 +%d, finish +%d, removed +%d\n", team0, team1, finish, removed);
    CHECK(team0 == 190 && team1 == 250 && finish == 310 && removed == 439);
#else
    std::printf("  r3g landing clips: skipped (no engine/anim)\n");
#endif
}

// ---- t05 ----
std::string crush_patch(std::string t) {
    // The trooper gets a Damage_Crush clone (Crushed, as the 21 FoC Damage_Crush clones) and a Damage_Normal clone.
    const std::string at = "<GroundInfantry Name=\"S10_Trooper\">";
    if (const size_t p = t.find(at); p != std::string::npos)
        t.insert(p + at.size(), "<Death_Clone>Damage_Crush, S10_Trooper_Crush_Clone</Death_Clone>"
                                "<Death_Clone>Damage_Normal, S10_Trooper_Normal_Clone</Death_Clone>");
    const size_t end = t.find("</S10>");
    if (end != std::string::npos)
        t.insert(end, " <GroundInfantry Name=\"S10_Trooper_Crush_Clone\"><Specific_Death_Anim_Type>Crushed</Specific_Death_Anim_Type>"
                      "<Death_Persistence_Duration>5</Death_Persistence_Duration><LandBehavior>HIDE_WHEN_FOGGED</LandBehavior></GroundInfantry>\n"
                      " <GroundInfantry Name=\"S10_Trooper_Normal_Clone\"><Death_Persistence_Duration>5</Death_Persistence_Duration>"
                      "<LandBehavior>HIDE_WHEN_FOGGED</LandBehavior></GroundInfantry>\n");
    return t;
}

void test_crush_kind() {
    g_temp_prefix = "alamo_sim_r3g_tests_crush";
    g_types_patch = crush_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper"), crush_clone = d->types.resolve_name("S10_Trooper_Crush_Clone");
    CHECK(trooper != objects::kNone && crush_clone != objects::kNone);
    CHECK(d->types.get_string(crush_clone, "Specific_Death_Anim_Type").find("Crushed") != std::string_view::npos);
    int phase = 0;
    objects::ObjectId victim = objects::kNoObjectId, crusher = objects::kNoObjectId;
    uint8_t kind = 0;
    int32_t clone = -2;
    land_battle(*d, nullptr, 600, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        if (phase == 0) {
            if (b.frame() < 2) return true;
            for (const objects::ObjectHandle h : st.all_objects()) {
                if (!st.alive(h) || !b.combat().has_unit(st.id(h)) || !b.combat().alive(st.id(h))) continue;
                if (victim == objects::kNoObjectId && st.type(h) == trooper) victim = st.id(h);
                else if (crusher == objects::kNoObjectId) crusher = st.id(h);
            }
            CHECK(victim != objects::kNoObjectId && crusher != objects::kNoObjectId);
            if (victim == objects::kNoObjectId || crusher == objects::kNoObjectId) return false;
            CHECK(b.test_crush(crusher, victim));
            phase = 1;
            return true;
        }
        for (const tactical::combat::DeathEvent& e : b.combat().deaths())
            if (e.id == victim) {
                kind = e.kind;
                clone = e.death_clone;
            }
        return clone == -2 && b.frame() < 60;
    });
    std::printf("  r3g crush: kind %u, clone %d (Damage_Crush clone %d)\n", static_cast<unsigned>(kind), clone, static_cast<int>(crush_clone));
    CHECK(kind == 14 && clone == static_cast<int32_t>(crush_clone));
}

// ---- t06 ----
std::string spread_patch(std::string t) {
    // IDLE (the idle service runs only for it) and a SPREAD_OUT unit ability on the trooper.
    const std::string beh = "<Behavior>SELECTABLE, WEAPON</Behavior>";
    if (const size_t p = t.find(beh); p != std::string::npos) t.replace(p, beh.size(), "<Behavior>SELECTABLE, WEAPON, IDLE</Behavior>");
    const std::string at = "<GroundInfantry Name=\"S10_Trooper\">";
    if (const size_t p = t.find(at); p != std::string::npos)
        t.insert(p + at.size(), "<Unit_Abilities_Data SubObjectList=\"Yes\"><Unit_Ability><Type>SPREAD_OUT</Type>"
                                "<Recharge_Seconds>1</Recharge_Seconds></Unit_Ability></Unit_Abilities_Data>");
    return t;
}

void test_crouch() {
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = "alamo_sim_r3g_tests_crouch";
    g_types_patch = spread_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper");
    const auto model = synth_model("R3G_TROOPER", {{anim::kAnimIdle, 30u}, {anim::kAnimIdleCrouch, 30u}, {anim::kAnimMove, 20u},
                                                   {anim::kAnimCrouchMove, 20u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(trooper, objects::ModeKind::Land, model.get());
    int phase = 0;
    objects::ObjectId unit = objects::kNoObjectId;
    uint32_t on_frame = 0, off_frame = 0;
    int32_t crouched = -1, standing = -1;
    land_battle(*d, &animation, 900, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        const anim::AnimWorld* w = animation.find_world(tm);
        if (phase == 0) {
            if (b.frame() < 30) return true;
            for (const objects::ObjectHandle h : st.all_objects())
                if (unit == objects::kNoObjectId && st.alive(h) && st.type(h) == trooper && w && w->contains(st.id(h)))
                    unit = st.id(h);
            CHECK(unit != objects::kNoObjectId);
            if (unit == objects::kNoObjectId) return false;
            const objects::ObjectId ids[] = {unit};
            CHECK(b.abilities().set_unit_ability_mode(ids, tactical::abilities::kSpreadOut, true, b.frame()) == 1);
            CHECK(b.abilities().spread_out(unit));
            on_frame = b.frame();
            phase = 1;
            return true;
        }
        if (!w || !w->contains(unit)) return false;
        const int32_t type = w->current_type(unit);
        if (phase == 1 && type == anim::kAnimIdleCrouch) {
            crouched = static_cast<int32_t>(b.frame() - on_frame);
            const objects::ObjectId ids[] = {unit};
            CHECK(b.abilities().set_unit_ability_mode(ids, tactical::abilities::kSpreadOut, false, b.frame()) == 1);
            off_frame = b.frame();
            phase = 2;
        } else if (phase == 2 && type == anim::kAnimIdle) {
            standing = static_cast<int32_t>(b.frame() - off_frame);
            return false;
        }
        return b.frame() < on_frame + 300;
    });
    std::printf("  r3g crouch: Idle Crouch %d frames after SPREAD_OUT on, Idle %d frames after off\n", crouched, standing);
    CHECK(crouched >= 0 && crouched <= 2 && standing >= 0 && standing <= 2);
#else
    std::printf("  r3g crouch: skipped (no engine/anim)\n");
#endif
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("r3g t01: landing Land / Takeoff clip timing\n");
    test_landing_clips();
    std::printf("r3g t05: crush kind and Damage_Crush clones\n");
    test_crush_kind();
    std::printf("r3g t06: crouch under SPREAD_OUT\n");
    test_crouch();
    if (g_fail) {
        std::printf("alamo_sim_r3g_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_sim_r3g_tests: OK\n");
    return 0;
}
