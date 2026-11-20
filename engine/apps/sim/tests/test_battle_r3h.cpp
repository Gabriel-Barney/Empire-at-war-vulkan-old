// alamo_sim_r3h_tests: r3h "Ability, hero and structure clip playback" on a synthetic land battle (battle_test_fixture.hpp,
// no game data). The battle hands the ability system's clips to its animation world (TacticalBattle::oi_ability_anims,
// ABILITIES_HARDPOINTS §4.15): the AnimWorld row type of a trooper with FORCE_SIGHT is traced per frame - Force_Revel_Begin
// (105) one-shot, Force_Revel_Loop (106) from the frame Begin finishes, Force_Revel_End (107) when the 2 s run out, then
// the IDLE behaviour's idle once End has finished.
#include "battle_test_fixture.hpp"
#include "tactical_battle_test_access.hpp"  // ref3 t06
#include "alamo/tactical/abilities/ability_types.hpp"

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

// A land battle on S10_B (invaded by player 1's S10_Squad company from planet 2). `per_tick(b, tm, host)` runs after every
// host tick once the battle exists; it returns false to stop. `animation` is attached (its CRC system too).
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
    if (animation) {
        animation->attach(host, true);
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

std::string sight_patch(std::string t) {
    // The trooper gets FORCE_SIGHT with a 2 s Force_Sight_Ability (as the fog data's troopers).
    const std::string at = "<GroundInfantry Name=\"S10_Trooper\">";
    if (const size_t p = t.find(at); p != std::string::npos)
        t.insert(p + at.size(),
                 "<Unit_Abilities_Data SubObjectList=\"Yes\"><Unit_Ability><Type>FORCE_SIGHT</Type><Recharge_Seconds>50</Recharge_Seconds>"
                 "<GUI_Activated_Ability_Name>S10_Force_Sight</GUI_Activated_Ability_Name></Unit_Ability></Unit_Abilities_Data>"
                 "<Abilities SubObjectList=\"Yes\"><Force_Sight_Ability Name=\"S10_Force_Sight\"><Activation_Style>User_Input</Activation_Style>"
                 "<Force_Sight_Duration_In_Secs>2.0</Force_Sight_Duration_In_Secs></Force_Sight_Ability></Abilities>");
    return t;
}

// ---- t01: Force_Sight's three clips on the AnimWorld row ----
void test_force_sight_rows() {
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = "alamo_sim_r3h_tests_sight";
    g_types_patch = sight_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper");
    CHECK(trooper != objects::kNone);
    const auto model = synth_model("R3H_TROOPER", {{anim::kAnimIdle, 30u}, {anim::kAnimMove, 20u}, {105, 20u}, {106, 40u}, {107, 12u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(trooper, objects::ModeKind::Land, model.get());
    objects::ObjectId unit = objects::kNoObjectId;
    uint32_t on = 0;
    int32_t begin = -1, loop = -1, end = -1, idle = -1, last = -2;
    std::string trace;
    land_battle(*d, &animation, 900, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        const anim::AnimWorld* w = animation.find_world(tm);
        if (on == 0) {
            if (b.frame() < 30) return true;
            for (const objects::ObjectHandle h : st.all_objects())
                if (unit == objects::kNoObjectId && st.alive(h) && st.type(h) == trooper && w && w->contains(st.id(h))) unit = st.id(h);
            CHECK(unit != objects::kNoObjectId);
            if (unit == objects::kNoObjectId) return false;
            const objects::ObjectId ids[] = {unit};
            CHECK(alamo::sim::TacticalBattleTestAccess::abilities(b).set_unit_ability_mode(ids, tactical::abilities::kForceSight, true, b.frame()) == 1);
            on = b.frame();
            return true;
        }
        if (!w || !w->contains(unit) || !b.combat().alive(unit)) return false;
        // The row after this frame's tick (the clips the battle played at frame f advance from f + 1).
        const int32_t type = w->current_type(unit);
        const int32_t rel = static_cast<int32_t>(b.frame() - on);
        if (type != last) {
            trace += " +" + std::to_string(rel) + ":" + std::to_string(type);
            last = type;
            if (type == 105 && begin < 0) begin = rel;
            if (type == 106 && loop < 0) loop = rel;
            if (type == 107 && end < 0) end = rel;
            if (end >= 0 && type != 107 && idle < 0) idle = rel;
        }
        return rel < 200 && idle < 0;
    });
    // Begin is played by the activation (between ticks, frame +0; first sampled after the next tick); Begin (20 frames)
    // finishes 18 frames later -> the loop (§4.14, played in the frame the record reports it finished); the 2 s run out
    // int(30 x 2) = 60 updates after the apply -> End (12 frames, blend 0.5), finished 10 frames later; then the unit's
    // own clips again (the locomotor: it still walks).
    std::printf("  r3h force sight rows (frame:type):%s\n", trace.c_str());
    std::printf("  r3h force sight: begin +%d, loop +%d, end +%d, next clip +%d\n", begin, loop, end, idle);
    CHECK(begin == 1 && loop == 18 && end == 60 && idle == 71);
#else
    std::printf("  r3h force sight rows: skipped (no engine/anim)\n");
#endif
}

// r3h review S2-2: a trooper killed by a lethal script Take_Damage while its Force_Sight loop runs - the dying removes
// the special (Force_Revel_End queued) and the death clip starts; no ability clip is played over the death clip / corpse.
// Without a Die clip the row stays as an unlocked corpse (no death clip locks it): the queued End must not play on it.
void test_force_sight_death(bool die_clip) {
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = die_clip ? "alamo_sim_r3h_tests_death" : "alamo_sim_r3h_tests_corpse";
    g_types_patch = sight_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper");
    const auto model = die_clip ? synth_model("R3H_TROOPER", {{anim::kAnimIdle, 30u}, {anim::kAnimMove, 20u}, {anim::kAnimDie, 40u},
                                                              {105, 20u}, {106, 40u}, {107, 12u}})
                                : synth_model("R3H_TROOPER", {{anim::kAnimIdle, 30u}, {anim::kAnimMove, 20u}, {105, 20u}, {106, 40u}, {107, 12u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(trooper, objects::ModeKind::Land, model.get());
    objects::ObjectId unit = objects::kNoObjectId;
    uint32_t on = 0;
    int32_t last = -2, killed = -1;
    bool death_seen = false, ability_after_death = false;
    std::string trace;
    land_battle(*d, &animation, 900, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        const anim::AnimWorld* w = animation.find_world(tm);
        if (on == 0) {
            if (b.frame() < 30) return true;
            for (const objects::ObjectHandle h : st.all_objects())
                if (unit == objects::kNoObjectId && st.alive(h) && st.type(h) == trooper && w && w->contains(st.id(h))) unit = st.id(h);
            if (unit == objects::kNoObjectId) return false;
            const objects::ObjectId ids[] = {unit};
            CHECK(alamo::sim::TacticalBattleTestAccess::abilities(b).set_unit_ability_mode(ids, tactical::abilities::kForceSight, true, b.frame()) == 1);
            on = b.frame();
            return true;
        }
        const int32_t rel = static_cast<int32_t>(b.frame() - on);
        if (!w || !w->contains(unit)) return false;
        const int32_t type = w->current_type(unit);
        if (type != last) {
            trace += " +" + std::to_string(rel) + ":" + std::to_string(type);
            last = type;
        }
        if (killed >= 0 && (type == anim::kAnimDie || !die_clip)) death_seen = true;
        if (death_seen && type >= 105 && type <= 107 && type != 106) ability_after_death = true;
        if (killed < 0 && rel == 30) {  // in the loop: a lethal script Take_Damage between ticks
            CHECK(type == 106);
            CHECK(b.script_damage(tm, unit, 1.0e6f, false));
            killed = rel;
        }
        return rel < 120;
    });
    std::printf("  r3h force sight death (%s) rows (frame:type):%s\n", die_clip ? "Die clip" : "no Die clip", trace.c_str());
    CHECK(killed == 30 && death_seen && !ability_after_death);
#else
    (void)die_clip;
    std::printf("  r3h force sight death: skipped (no engine/anim)\n");
#endif
}

// r3h review (stuck gate): a model with Force_Revel_Begin and _Loop but no _End - when the 2 s run out the closing play
// fails; the record must end the hold so the unit's own clips (the locomotor) take over again instead of the loop
// running for good behind a gate that skips the locomotor / IDLE service.
void test_force_sight_no_end() {
#ifdef ALAMO_SIM_HAVE_ANIM
    g_temp_prefix = "alamo_sim_r3h_tests_noend";
    g_types_patch = sight_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper");
    const auto model = synth_model("R3H_TROOPER", {{anim::kAnimIdle, 30u}, {anim::kAnimMove, 20u}, {105, 20u}, {106, 40u}});
    SimAnimation animation(d->types, *d->files);
    animation.set_model(trooper, objects::ModeKind::Land, model.get());
    objects::ObjectId unit = objects::kNoObjectId;
    uint32_t on = 0;
    int32_t last = -2, after = -1;
    std::string trace;
    land_battle(*d, &animation, 900, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        const anim::AnimWorld* w = animation.find_world(tm);
        if (on == 0) {
            if (b.frame() < 30) return true;
            for (const objects::ObjectHandle h : st.all_objects())
                if (unit == objects::kNoObjectId && st.alive(h) && st.type(h) == trooper && w && w->contains(st.id(h))) unit = st.id(h);
            if (unit == objects::kNoObjectId) return false;
            const objects::ObjectId ids[] = {unit};
            CHECK(alamo::sim::TacticalBattleTestAccess::abilities(b).set_unit_ability_mode(ids, tactical::abilities::kForceSight, true, b.frame()) == 1);
            on = b.frame();
            return true;
        }
        if (!w || !w->contains(unit) || !b.combat().alive(unit)) return false;
        const int32_t type = w->current_type(unit);
        const int32_t rel = static_cast<int32_t>(b.frame() - on);
        if (type != last) {
            trace += " +" + std::to_string(rel) + ":" + std::to_string(type);
            last = type;
            if (rel > 18 && type != 106 && after < 0) after = rel;
        }
        return rel < 200 && after < 0;
    });
    std::printf("  r3h force sight without End rows (frame:type):%s\n", trace.c_str());
    CHECK(after >= 60 && after <= 62);
#else
    std::printf("  r3h force sight without End: skipped (no engine/anim)\n");
#endif
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("r3h t01: Force_Sight clips on the battle's animation world\n");
    test_force_sight_rows();
    test_force_sight_death(true);
    test_force_sight_death(false);
    test_force_sight_no_end();
    if (g_fail) {
        std::printf("alamo_sim_r3h_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_sim_r3h_tests: OK\n");
    return 0;
}
