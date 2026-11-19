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
void land_battle(Data& d, SimAnimation* animation, uint32_t max_frames, F per_tick, bool skirmish = false) {
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
    opt.battle.skirmish = skirmish;  // mcx4 tac2: the skirmish landing clearance delay
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
    objects::ObjectId victim = objects::kNoObjectId, crusher = objects::kNoObjectId;
    uint8_t kind = 0;
    int32_t clone = -2;
    land_battle(*d, nullptr, 600, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        {
            if (b.frame() < 2) return true;
            for (const objects::ObjectHandle h : st.all_objects()) {
                if (!st.alive(h) || !b.combat().has_unit(st.id(h)) || !b.combat().alive(st.id(h))) continue;
                if (victim == objects::kNoObjectId && st.type(h) == trooper) victim = st.id(h);
                else if (crusher == objects::kNoObjectId) crusher = st.id(h);
            }
            CHECK(victim != objects::kNoObjectId && crusher != objects::kNoObjectId);
            if (victim == objects::kNoObjectId || crusher == objects::kNoObjectId) return false;
            CHECK(b.test_crush(crusher, victim));
            // The kill is reported at once (combat deaths() of this frame; the next tick clears them).
            for (const tactical::combat::DeathEvent& e : b.combat().deaths())
                if (e.id == victim) {
                    kind = e.kind;
                    clone = e.death_clone;
                }
        }
        return false;
    });
    std::printf("  r3g crush: kind %u, clone %d (Damage_Crush clone %d)\n", static_cast<unsigned>(kind), clone, static_cast<int>(crush_clone));
    CHECK(kind == 14 && clone == static_cast<int32_t>(crush_clone));
}

// ---- t06 ----
std::string spread_patch(std::string t) {
    // IDLE (the idle service runs only for it) and a SPREAD_OUT unit ability on the trooper. Attack range 1: with a
    // target held the IDLE service changes nothing (0x62e570 step 4), so the troopers must not engage.
    const std::string beh = "<Behavior>SELECTABLE, WEAPON</Behavior>";
    if (const size_t p = t.find(beh); p != std::string::npos) t.replace(p, beh.size(), "<Behavior>SELECTABLE, WEAPON, IDLE</Behavior>");
    const std::string range = "<Targeting_Max_Attack_Distance>250</Targeting_Max_Attack_Distance>";
    if (const size_t p = t.find(range); p != std::string::npos)
        t.replace(p, range.size(), "<Targeting_Max_Attack_Distance>1</Targeting_Max_Attack_Distance>");
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
    int32_t crouched = -1, standing = -1, standing_type = -1, last_type = -2;
    bool other = false;
    std::string trace;
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
        if (!w || !w->contains(unit) || !b.combat().alive(unit)) return false;
        const int32_t type = w->current_type(unit);
        if (type != last_type) {
            trace += " " + std::to_string(b.frame()) + ":" + std::to_string(type);
            last_type = type;
        }
        const bool crouch_clip = type == anim::kAnimIdleCrouch || type == anim::kAnimCrouchMove;
        if (phase == 1) {
            if (crouched < 0 && crouch_clip) crouched = static_cast<int32_t>(b.frame() - on_frame);
            if (type != anim::kAnimIdle && type != anim::kAnimMove && !crouch_clip) other = true;  // only the four clips
            if (type == anim::kAnimIdleCrouch) {  // stopped and crouching: SPREAD_OUT off
                const objects::ObjectId ids[] = {unit};
                CHECK(b.abilities().set_unit_ability_mode(ids, tactical::abilities::kSpreadOut, false, b.frame()) == 1);
                CHECK(!b.abilities().spread_out(unit));
                off_frame = b.frame();
                phase = 2;
            }
        } else if (phase == 2 && (type == anim::kAnimIdle || type == anim::kAnimMove)) {
            standing = static_cast<int32_t>(b.frame() - off_frame);
            standing_type = type;
            return false;
        }
        return b.frame() < on_frame + 600;
    });
    // On: the next locomotor service plays a crouch clip (CROUCHMOVE while moving, Idle Crouch once stopped); off: the
    // crouch change forces the plain idle at the next IDLE service.
    std::printf("  r3g crouch: crouch clip %d frames after SPREAD_OUT on (stopped crouching at +%d), standing clip %d frames "
                "after off; clip types (frame:type)%s\n",
                crouched, static_cast<int>(off_frame - on_frame), standing, trace.c_str());
    // mov1 (2026-11-17: the retail land tick order and the kept StoppedVsStopped record): the trooper can be set walking by a
    // collision resolution right when SPREAD_OUT goes off - then no IDLE service runs (it counts as moving) and the plain
    // clip is MOVE once the walk starts (measured +12); standing still, the plain idle comes at the next IDLE service
    CHECK(!other && crouched >= 0 && crouched <= 2 && standing >= 0 && (standing <= 2 || standing_type == anim::kAnimMove));
#else
    std::printf("  r3g crouch: skipped (no engine/anim)\n");
#endif
}

// ---- mcx4 tac2: reinforcement arrival rules ----
// Drops player 1's S10_Squad reserve at the first valid cell; true when the order went through.
bool drop_reserve(TacticalBattle& b, TacticalMode& tm, SimHost& host) {
    namespace mv = alamo::tactical::movement;
    const auto reserves = b.reserves(1);
    if (reserves.empty()) return false;
    const objects::ObjectId key = reserves[0].galactic_id;
    if (b.fog()) b.fog()->reveal_all(1);
    for (int32_t cy = 6; cy < 58; cy += 2)
        for (int32_t cx = 6; cx < 58; cx += 2) {
            const mv::V2 c = mv::cell_center(cx, cy);
            if (!b.land_drop_valid(tm, 1, key, c.x, c.y, false, false)) continue;
            if (b.order_reinforce(tm, 1, key, c.x, c.y, host.random().synced()) == commands::RejectReason::None) return true;
        }
    return false;
}

struct UnloadRun {
    bool dropped = false;
    int team0 = -1;                     // frames from the order to the first team step
    uint32_t step_frame = 0;            // battle frame of that step
    size_t members = 0, with_entry = 0;  // first step's combat members / those with the Elevated_Vulnerability entry
    float value = 0.0f;                 // the entry's value
    uint32_t expiry = 0;                // its expiry frame
    float defense_after = 1.0f;         // a member's Defense list 100 frames after the step
};

// FoC's values of the four Elevated_Vulnerability constants (the fixture's GameConstants set none otherwise).
constexpr const char* kFocVulnerability =
    " <Elevated_Vulnerability_Duration>3.0</Elevated_Vulnerability_Duration>\n"
    " <Elevated_Vulnerability_Factor>-2.0</Elevated_Vulnerability_Factor>\n"
    " <Space_Elevated_Vulnerability_Duration>5.0</Space_Elevated_Vulnerability_Duration>\n"
    " <Space_Elevated_Vulnerability_Factor>-3.0</Space_Elevated_Vulnerability_Factor>\n"
    " <Override_Death_Persistence_Duration>1.0</Override_Death_Persistence_Duration>\n";

UnloadRun run_unload(Data& d, bool skirmish) {
    UnloadRun out;
    uint32_t drop = 0;
    objects::ObjectId max_before = 0;
    std::vector<objects::ObjectId> members;
    static const uint32_t kElevated = name_crc("Elevated_Vulnerability");
    land_battle(
        d, nullptr, 1500,
        [&](TacticalBattle& b, TacticalMode& tm, SimHost& host) {
            objects::ObjectStore& st = tm.primary();
            auto& cs = const_cast<tactical::combat::CombatSystem&>(b.combat());
            if (!out.dropped) {
                if (b.frame() != 1) return true;
                for (const objects::ObjectHandle h : st.all_objects()) max_before = std::max(max_before, st.id(h));
                out.dropped = drop_reserve(b, tm, host);
                drop = b.frame();
                return out.dropped;
            }
            const int since = static_cast<int>(b.frame() - drop);
            if (out.team0 < 0) {
                for (const objects::ObjectHandle h : st.owned_by(1))
                    if (st.alive(h) && st.id(h) > max_before && cs.has_unit(st.id(h))) members.push_back(st.id(h));
                if (members.empty()) return since < 600;
                out.team0 = since;
                out.step_frame = b.frame();
                out.members = members.size();
                for (const objects::ObjectId id : members)
                    if (const tactical::combat::ModifierSet* m = cs.unit_modifiers(id))
                        for (const tactical::combat::ModifierEntry& e : m->entries())
                            if (e.source_crc == kElevated && e.list == static_cast<uint8_t>(tactical::combat::ModList::Defense)) {
                                ++out.with_entry;
                                out.value = e.value;
                                out.expiry = e.expiry;
                            }
                return true;
            }
            if (b.frame() < out.step_frame + 100) return true;
            if (cs.has_unit(members.front())) out.defense_after = cs.modifier(members.front(), tactical::combat::ModList::Defense);
            return false;
        },
        skirmish);
    return out;
}

// A Battlefield_Modifier on the trooper (the S10_Squad member): Disable_Reinforcement_Vulnerability for its owner's allies.
std::string disable_vulnerability_patch(std::string t) {
    const std::string at = "<GroundInfantry Name=\"S10_Trooper\">";
    if (const size_t p = t.find(at); p != std::string::npos)
        t.insert(p + at.size(), "<Abilities SubObjectList=\"Yes\"><Battlefield_Modifier_Ability Name=\"T_Enhanced_Deployment\">"
                                "<Activation_Style>Ground_Automatic</Activation_Style>"
                                "<Disable_Reinforcement_Vulnerability>True</Disable_Reinforcement_Vulnerability>"
                                "<Apply_To_All_Allies>Yes</Apply_To_All_Allies></Battlefield_Modifier_Ability></Abilities>");
    return t;
}

// MCX4_T10_RE_ANSWERS §16 / §16a / §15 [H] (Apply_Elevated_Vulnerability 0x3a8550, transport unload step 0x644330):
// each member of a landing's team step gets one Defense entry "Elevated_Vulnerability" with the factor for the duration:
// with FoC's Elevated_Vulnerability_Duration 3 s / Elevated_Vulnerability_Factor -2.0 (3x damage taken) the entry expires
// int(30 x 3 + 0.5) = 90 frames after the step and 100 frames later the Defense list is back to 0. With a
// Battlefield_Modifier Disable_Reinforcement_Vulnerability applied for the landing owner (here the team members' own
// Ground_Automatic ability, Apply_To_All_Allies), the team step adds no entry (the gate 0x527f00).
// ledger: verifies GameConstants.Elevated_Vulnerability_Duration
// ledger: verifies GameConstants.Elevated_Vulnerability_Factor
// ledger: verifies Battlefield_Modifier_Ability.Disable_Reinforcement_Vulnerability
void test_elevated_vulnerability() {
    g_temp_prefix = "alamo_sim_r3g_tests_mcx4_vuln";
    g_constants_extra = kFocVulnerability;
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, true);
    const UnloadRun a = run_unload(*d, false);
    std::printf("  mcx4 elevated vulnerability: dropped %d, team step +%d, %zu member(s), %zu with the entry (value %.2f, "
                "expiry %u = step %u + %u), Defense 100 frames later %.2f\n",
                a.dropped, a.team0, a.members, a.with_entry, static_cast<double>(a.value), a.expiry, a.step_frame,
                a.expiry - a.step_frame, static_cast<double>(a.defense_after));
    CHECK(a.dropped && a.members > 0 && a.with_entry == a.members);
    CHECK(a.value == -2.0f && a.expiry >= a.step_frame + 89 && a.expiry <= a.step_frame + 90);
    CHECK(a.defense_after == 0.0f);
    g_temp_prefix = "alamo_sim_r3g_tests_mcx4_novuln";
    g_types_patch = disable_vulnerability_patch;
    auto g = make_data(false, false, false, false, false, false, false, false, false, false, false, true);
    g_types_patch = nullptr;
    g_constants_extra = nullptr;
    const UnloadRun n = run_unload(*g, false);
    std::printf("  mcx4 Disable_Reinforcement_Vulnerability: team step +%d, %zu member(s), %zu with the entry\n", n.team0,
                n.members, n.with_entry);
    CHECK(n.dropped && n.members > 0 && n.with_entry == 0);
}

// MCX4_T10_RE_ANSWERS §19, A1_READINGS §35.2 state 4 (clearance 0x642ef0) [H]: in a skirmish a timed landing waits at least
// Skirmish_Reinforcement_Delay_Frames (ctor and FoC 90; the fixture sets none) after its attach before the footprint test,
// so its first team step comes 89 frames later than in a campaign battle (clearance at +90 instead of +1).
// ledger: verifies GameConstants.Skirmish_Reinforcement_Delay_Frames
void test_skirmish_landing_delay() {
    g_temp_prefix = "alamo_sim_r3g_tests_mcx4_delay";
    auto d = make_data(false, false, false, false, false, false, false, false, false, false, false, true);
    const UnloadRun c = run_unload(*d, false);
    const UnloadRun s = run_unload(*d, true);
    std::printf("  mcx4 skirmish landing delay: campaign team step +%d, skirmish +%d\n", c.team0, s.team0);
    CHECK(c.dropped && s.dropped && c.team0 > 0 && s.team0 == c.team0 + 89);
}

// MCX4_T10_RE_ANSWERS §16 / §18 (0x29f270) [H]: the death clone of a dying Infantry type persists for
// Override_Death_Persistence_Duration, not its own Death_Persistence_Duration. The crushed trooper's S10_Trooper_Crush_Clone
// (5 s of its own, primary clone object) goes after the override (1 s here) plus the default Death_Fade_Time 0.25 s:
// int(30 x 1) + int(30 x 0.25) = 37 frames, not 157.
// ledger: verifies GameConstants.Override_Death_Persistence_Duration
std::string persist_patch(std::string t) {
    // crush_patch with the clones' HIDE_WHEN_FOGGED in the generic <Behavior> set, so they keep the primary path (a clone
    // object the battle removes at its planned frame; death_clone_corpse_path reads the generic set only).
    t = crush_patch(std::move(t));
    for (size_t p = t.find("<LandBehavior>HIDE_WHEN_FOGGED</LandBehavior>"); p != std::string::npos;
         p = t.find("<LandBehavior>HIDE_WHEN_FOGGED</LandBehavior>", p))
        t.replace(p, 45, "<Behavior>HIDE_WHEN_FOGGED</Behavior>");
    return t;
}

void test_death_persistence_override() {
    g_temp_prefix = "alamo_sim_r3g_tests_mcx4_persist";
    g_types_patch = persist_patch;
    g_constants_extra = kFocVulnerability;
    auto d = make_data();
    g_types_patch = nullptr;
    g_constants_extra = nullptr;
    const objects::TypeIndex trooper = d->types.resolve_name("S10_Trooper");
    objects::ObjectId victim = objects::kNoObjectId, crusher = objects::kNoObjectId;
    uint32_t crushed = 0;
    int gone = -1;
    size_t pending = 0;
    land_battle(*d, nullptr, 600, [&](TacticalBattle& b, TacticalMode& tm, SimHost&) {
        objects::ObjectStore& st = tm.primary();
        if (b.frame() < 2) return true;
        if (crushed == 0) {
            for (const objects::ObjectHandle h : st.all_objects()) {
                if (!st.alive(h) || !b.combat().has_unit(st.id(h)) || !b.combat().alive(st.id(h))) continue;
                if (victim == objects::kNoObjectId && st.type(h) == trooper) victim = st.id(h);
                else if (crusher == objects::kNoObjectId) crusher = st.id(h);
            }
            const bool ok = victim != objects::kNoObjectId && crusher != objects::kNoObjectId && b.test_crush(crusher, victim);
            if (!ok) std::printf("  mcx4 death persistence override: victim %d crusher %d, crush refused\n", victim, crusher);
            if (!ok) return false;
            crushed = b.frame();
            return true;
        }
        if (pending == 0) {  // the clone is made when the death is processed (this or a later tick)
            pending = b.death_clones_pending();
            if (pending > 0) crushed = b.frame();
        } else if (b.death_clones_pending() < pending) {
            gone = static_cast<int>(b.frame() - crushed);
            return false;
        }
        return b.frame() < crushed + 300;
    });
    std::printf("  mcx4 death persistence override: clone pending %zu after the crush, removed +%d\n", pending, gone);
    CHECK(pending > 0 && gone >= 36 && gone <= 39);
}

// MCX4_T10_RE_ANSWERS §16a addendum [H]: the space reinforcement drop 0x2bf2d0 ends with Apply_Elevated_Vulnerability on
// every arriving unit (no gate), with the space pair: FoC Space_Elevated_Vulnerability_Duration 5 s /
// Space_Elevated_Vulnerability_Factor -3.0 (4x damage taken), so the entry expires 150 frames after the drop. Player 1's
// extra corvettes wait in the pool (unit cap); one of its fleet units is removed first to make room.
// ledger: verifies GameConstants.Space_Elevated_Vulnerability_Duration
// ledger: verifies GameConstants.Space_Elevated_Vulnerability_Factor
void test_space_reinforcement_vulnerability() {
    g_temp_prefix = "alamo_sim_r3g_tests_mcx4_space";
    g_constants_extra = kFocVulnerability;
    auto d = make_data();
    g_constants_extra = nullptr;
    jobs::JobSystem js(jobs::JobSystemConfig{0});
    SimConfig cfg;
    cfg.seed = 2026;
    cfg.players = 4;
    SimHost host(d->types, js, cfg);
    galactic::GalacticSetup setup = make_setup(*d, false);
    for (int i = 0; i < 3; ++i) setup.forces.push_back({1, 2, type_of(*d, "S10_Corvette")});
    galactic::GalacticSim* gs = host.start_galactic(d->rules, std::move(setup));
    CHECK(gs != nullptr);
    if (!gs) return;
    CampaignBattleOptions opt;
    opt.conflict.human_choice = galactic::BattleChoice::Fight;
    opt.battle.max_frames = 600;
    opt.battle.order_interval = 0;  // no stand-in orders
    CampaignBattles battles(host, d->crules, *d->catalog,
                            [](const std::string& name, terrain::MapData& m) {
                                if (name != "S10_Space_B") return false;
                                m = space_map();
                                return true;
                            },
                            opt);
    CHECK(battles.attach(nullptr));
    const std::vector<uint32_t> uf = gs->fleets_at(2, 1);
    if (!uf.empty()) CHECK(gs->move_fleet(1, uf[0], 1) == commands::RejectReason::None);
    static const uint32_t kElevated = name_crc("Elevated_Vulnerability");
    bool killed = false, dropped = false;
    uint32_t drop_frame = 0;
    objects::ObjectId max_before = 0;
    size_t units = 0, with_entry = 0;
    float value = 0.0f;
    uint32_t expiry = 0;
    for (uint64_t t = 0; t < 3000 && !dropped; ++t) {
        host.tick_once();
        TacticalBattle* b = battles.battle();
        TacticalMode* tm = host.tactical();
        if (!b || !tm || b->frame() < 1 || b->finished()) continue;
        objects::ObjectStore& st = tm->primary();
        auto& cs = const_cast<tactical::combat::CombatSystem&>(b->combat());
        if (!killed) {
            if (b->fog()) b->fog()->reveal_all(1);  // the space drop validity refuses a fogged point
            for (const objects::ObjectHandle h : st.owned_by(1))
                if (st.alive(h) && cs.has_unit(st.id(h)) && cs.alive(st.id(h))) {
                    killed = b->script_damage(*tm, st.id(h), 1.0e9f, false);
                    break;
                }
            continue;
        }
        const auto r = b->reserves(1);
        if (r.empty()) {
            std::printf("  mcx4 space reinforcement: no reserve of player 1 at frame %u\n", b->frame());
            break;
        }
        max_before = 0;
        for (const objects::ObjectHandle h : st.all_objects()) max_before = std::max(max_before, st.id(h));
        if (const commands::RejectReason rr = b->order_reinforce(*tm, 1, r[0].galactic_id, 0.0f, 0.0f, host.random().synced());
            rr != commands::RejectReason::None) {
            if (b->frame() % 100 == 0) std::printf("  mcx4 space reinforcement: frame %u refused %d\n", b->frame(), static_cast<int>(rr));
            continue;
        }
        dropped = true;
        drop_frame = b->frame();
        for (const objects::ObjectHandle h : st.owned_by(1)) {
            if (!st.alive(h) || st.id(h) <= max_before || !cs.has_unit(st.id(h))) continue;
            ++units;
            if (const tactical::combat::ModifierSet* m = cs.unit_modifiers(st.id(h)))
                for (const tactical::combat::ModifierEntry& e : m->entries())
                    if (e.source_crc == kElevated && e.list == static_cast<uint8_t>(tactical::combat::ModList::Defense)) {
                        ++with_entry;
                        value = e.value;
                        expiry = e.expiry;
                    }
        }
    }
    std::printf("  mcx4 space reinforcement: killed %d, dropped %d at %u, %zu unit(s), %zu with the entry (value %.2f, expiry +%u)\n",
                killed, dropped, drop_frame, units, with_entry, static_cast<double>(value), expiry - drop_frame);
    CHECK(killed && dropped && units > 0 && with_entry == units);
    CHECK(value == -3.0f && expiry >= drop_frame + 149 && expiry <= drop_frame + 150);
}

// MCX4_T09_RE_ANSWERS Q10 [H]: Income_Stream_Ability / Income_Stream_Mod_Ability as parse_income_stream reads them (the
// validator 0x703d00 / 0x704880 and the create 0x703990), the fold 0x33fb70, the per-frame pay 0x6951a0 with the effective
// interval 0x694b30, the share modes 0x694ba0 / 0x694e90 and the mod filter of apply 0x703fe0.
// ledger: verifies Income_Stream_Ability.Split_Favors_Owner
// ledger: verifies Income_Stream_Ability.Full_Amount_To_Everyone
// ledger: verifies Income_Stream_Ability.Owner_Income_Percentage
// ledger: verifies Income_Stream_Mod_Ability.Interval_Multiplier
// ledger: verifies Income_Stream_Mod_Ability.Affects_All_Allied_Sources
std::string income_patch(std::string t) {
    const size_t end = t.find("</S10>");
    if (end != std::string::npos)
        t.insert(end,
                 // favours the owner with a percentage >= 1 (clamped to 0.99); a mod on itself: m 2, im <= 0 (-> 1.0)
                 " <SpaceUnit Name=\"S10_Inc_Favor\"><Abilities SubObjectList=\"Yes\"><Income_Stream_Ability Name=\"I1\">"
                 "<Base_Income_Value>50</Base_Income_Value><Base_Interval_In_Secs>10</Base_Interval_In_Secs>"
                 "<Split_Income_With_Allies>Yes</Split_Income_With_Allies><Split_Favors_Owner>Yes</Split_Favors_Owner>"
                 "<Owner_Income_Percentage>1.5</Owner_Income_Percentage></Income_Stream_Ability>"
                 "<Income_Stream_Mod_Ability Name=\"M1\"><Income_Multiplier>2.0</Income_Multiplier>"
                 "<Interval_Multiplier>0</Interval_Multiplier><Stacking_Category>3</Stacking_Category>"
                 "<Target_Stream_Source>S10_Inc_Favor</Target_Stream_Source>"
                 "<Affects_All_Allied_Sources>Yes</Affects_All_Allied_Sources></Income_Stream_Mod_Ability></Abilities></SpaceUnit>\n"
                 // both share flags: the validator clears both (mode 0); an interval mod 0.5 on another source, own only
                 " <SpaceUnit Name=\"S10_Inc_Both\"><Abilities SubObjectList=\"Yes\"><Income_Stream_Ability Name=\"I2\">"
                 "<Base_Income_Value>30</Base_Income_Value><Base_Interval_In_Secs>5</Base_Interval_In_Secs>"
                 "<Split_Income_With_Allies>Yes</Split_Income_With_Allies><Split_Favors_Owner>Yes</Split_Favors_Owner>"
                 "<Full_Amount_To_Everyone>Yes</Full_Amount_To_Everyone></Income_Stream_Ability>"
                 "<Income_Stream_Mod_Ability Name=\"M2\"><Interval_Multiplier>0.5</Interval_Multiplier>"
                 "<Target_Stream_Source>S10_Inc_Favor</Target_Stream_Source>"
                 "<Affects_All_Allied_Sources>No</Affects_All_Allied_Sources></Income_Stream_Mod_Ability></Abilities></SpaceUnit>\n"
                 // full amount to everyone; favours the owner without split (cleared, percentage 0)
                 " <SpaceUnit Name=\"S10_Inc_Full\"><Abilities SubObjectList=\"Yes\"><Income_Stream_Ability Name=\"I3\">"
                 "<Base_Income_Value>10</Base_Income_Value><Base_Interval_In_Secs>1</Base_Interval_In_Secs>"
                 "<Split_Income_With_Allies>Yes</Split_Income_With_Allies><Full_Amount_To_Everyone>Yes</Full_Amount_To_Everyone>"
                 "</Income_Stream_Ability></Abilities></SpaceUnit>\n"
                 " <SpaceUnit Name=\"S10_Inc_Solo\"><Abilities SubObjectList=\"Yes\"><Income_Stream_Ability Name=\"I4\">"
                 "<Base_Income_Value>10</Base_Income_Value><Base_Interval_In_Secs>1</Base_Interval_In_Secs>"
                 "<Split_Favors_Owner>Yes</Split_Favors_Owner><Owner_Income_Percentage>0.5</Owner_Income_Percentage>"
                 "</Income_Stream_Ability></Abilities></SpaceUnit>\n");
    return t;
}

void test_income_streams() {
    g_temp_prefix = "alamo_sim_r3g_tests_mcx4_income";
    g_types_patch = income_patch;
    auto d = make_data();
    g_types_patch = nullptr;
    const auto ty = [&](const char* n) { return static_cast<uint32_t>(d->types.resolve_name(n)); };
    const uint32_t favor_t = ty("S10_Inc_Favor");
    const IncomeStreamDef favor = parse_income_stream(d->types, favor_t), both = parse_income_stream(d->types, ty("S10_Inc_Both"));
    const IncomeStreamDef full = parse_income_stream(d->types, ty("S10_Inc_Full")), solo = parse_income_stream(d->types, ty("S10_Inc_Solo"));
    std::printf("  mcx4 income parse: favor split %d mode %u share %.2f mods %zu (m %.1f im %.1f cat %d all %d); both mode %u; "
                "full mode %u; solo mode %u share %.2f\n",
                favor.split, favor.mode, static_cast<double>(favor.owner_share), favor.mods.size(),
                favor.mods.empty() ? 0.0 : static_cast<double>(favor.mods[0].multiplier),
                favor.mods.empty() ? 0.0 : static_cast<double>(favor.mods[0].interval), favor.mods.empty() ? -1 : favor.mods[0].category,
                favor.mods.empty() ? 0 : favor.mods[0].all_allied, both.mode, full.mode, solo.mode, static_cast<double>(solo.owner_share));
    CHECK(favor.value == 50.0f && favor.interval == 10.0f && favor.split && favor.mode == 1 && favor.owner_share == 0.99f);
    CHECK(favor.mods.size() == 1 && favor.mods[0].target_type == favor_t && favor.mods[0].multiplier == 2.0f &&
          favor.mods[0].interval == 1.0f && favor.mods[0].category == 3 && favor.mods[0].all_allied);
    CHECK(both.mode == 0 && both.split && both.mods.size() == 1 && both.mods[0].interval == 0.5f && !both.mods[0].all_allied);
    CHECK(full.mode == 2 && full.split);
    CHECK(solo.mode == 0 && !solo.split && solo.owner_share == 0.0f);
    // Fold: per category the largest value, the maxima summed (0.2 / 0.4 in category 1 -> 0.4, plus 1.0 in category 0).
    const IncomeModEntry e1[] = {{1, 0.2f, 0.0f, 0.0f}, {1, 0.4f, 0.0f, 0.0f}, {0, 1.0f, 20.0f, 0.0f}};
    CHECK(income_fold(e1, 0) == 1.0f + 0.4f && income_fold(e1, 1) == 20.0f && income_fold(e1, 2) == 0.0f);
    // Pay: ((M + 1) x base + A) / I / FPS.
    CHECK(income_stream_pay(50.0f, 10.0f, {}) == 50.0f / 10.0f / 30.0f);
    CHECK(income_stream_pay(50.0f, 10.0f, e1) == ((1.4f + 1.0f) * 50.0f + 20.0f) / 10.0f / 30.0f);
    // Interval_Multiplier: im - 1 in the interval map; 0.5 halves the interval, 2.0 doubles it, the floor is 1 s; two cuts
    // in one category: the larger im - 1 (the smaller cut) wins.
    const IncomeModEntry half[] = {{0, 0.0f, 0.0f, -0.5f}}, twice[] = {{0, 0.0f, 0.0f, 1.0f}}, deep[] = {{0, 0.0f, 0.0f, -0.95f}};
    const IncomeModEntry two_cuts[] = {{2, 0.0f, 0.0f, -0.5f}, {2, 0.0f, 0.0f, -0.25f}};
    CHECK(income_stream_pay(50.0f, 10.0f, half) == 50.0f / 5.0f / 30.0f);
    CHECK(income_stream_pay(50.0f, 10.0f, twice) == 50.0f / 20.0f / 30.0f);
    CHECK(income_stream_pay(50.0f, 10.0f, deep) == 50.0f / 1.0f / 30.0f);
    CHECK(income_stream_pay(50.0f, 10.0f, two_cuts) == 50.0f / 7.5f / 30.0f);
    // Shares of a 0.3 pay among recipients {0, 1, 2} of owner 0.
    const float pay = 0.3f;
    CHECK(income_share(2, 0.0f, 0, 1, 3, pay) == pay);                                  // Full_Amount_To_Everyone
    CHECK(income_share(1, 0.5f, 0, 0, 3, pay) == pay * 0.5f);                           // Split_Favors_Owner: owner s
    CHECK(income_share(1, 0.5f, 0, 2, 3, pay) == (pay - pay * 0.5f) / 3.0f);            // others (pay - s) / N, N with the owner
    CHECK(income_share(1, 0.99f, 0, 0, 3, pay) == pay * 0.99f);                         // Owner_Income_Percentage
    CHECK(income_share(1, 1.0f, 0, 1, 3, pay) == 0.0f);                                 // a share <= 0 is not paid
    CHECK(income_share(0, 0.0f, 0, 1, 3, pay) == pay / 3.0f);                           // equal split
    // Affects_All_Allied_Sources: without it only the stream owner's own mods; with it an ally's mod too, but only on a
    // stream that lists the mod owner as a recipient.
    const int32_t split_r[] = {0, 1}, solo_r[] = {0};
    IncomeModDef m;
    m.target_type = favor_t;
    CHECK(income_mod_applies(m, favor_t, 0, 0, solo_r) && !income_mod_applies(m, favor_t, 0, 1, split_r));
    CHECK(!income_mod_applies(m, favor_t + 1u, 0, 0, solo_r));
    m.all_allied = true;
    CHECK(income_mod_applies(m, favor_t, 0, 1, split_r) && !income_mod_applies(m, favor_t, 0, 1, solo_r));
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
    std::printf("mcx4 tac2: Elevated_Vulnerability on the landing unload\n");
    test_elevated_vulnerability();
    std::printf("mcx4 tac2: Elevated_Vulnerability on the space reinforcement drop\n");
    test_space_reinforcement_vulnerability();
    std::printf("mcx4 tac2: skirmish landing clearance delay\n");
    test_skirmish_landing_delay();
    std::printf("mcx4 tac2: Override_Death_Persistence_Duration\n");
    test_death_persistence_override();
    std::printf("mcx4 tac2: income streams\n");
    test_income_streams();
    if (g_fail) {
        std::printf("alamo_sim_r3g_tests: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("alamo_sim_r3g_tests: OK\n");
    return 0;
}
