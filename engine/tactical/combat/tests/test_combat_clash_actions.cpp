// mcx4 (Stage 9, lane mcx4-tac2): the action-driven hero clash's action runner (docs/re/HERO_CLASH.md §4;
// docs/re/MCX4_T10_RE_ANSWERS.md §11 Attack_Action; docs/re/MCX4_T11_T12_RE_ANSWERS.md §B3 Move_Action and
// Special_Ability_Action). Shipped data never starts such a clash (Clash_Range 0.0), so these tests drive the manager with
// synthetic HEROCLASH.XML entries and a stub host that records every effect.
#include "test_combat_common.hpp"

namespace bo3split {

namespace {

constexpr int32_t kHeroA = 10, kHeroB = 11;

// The host: two heroes, a clip clock (a played one-shot clip runs `clip_frames` frames), moves that walk until the test
// stops them.
struct ActionStub final : HeroClashHost {
    struct U {
        ObjectId id = kNoObject;
        int32_t type = -1, owner = -1;
        Vec3 pos;
        float yaw = 0.0f, health_frac = 1.0f;
        bool alive = true, jet = false, walking = false, flag = false, held = false, loco_susp = false, jetpack = false,
             locked = false;
        std::string anim_override;
        int32_t override_sub = 0;
        float override_speedup = 0.0f, speed_override = 0.0f;
        std::string clip;
        int32_t clip_start = 0;
        bool clip_loop = false;
        Vec3 goal;
        uint32_t orders = 0;
    };
    struct Shot {
        ObjectId shooter, target;
        std::string projectile, bone;
        int32_t frame;
    };
    struct Ability {
        ObjectId id, target;
        std::string name;
        int32_t frame;
    };
    struct Clip {
        ObjectId id;
        std::string anim;
        int32_t sub;
        bool loop;
        float blend, speed;
    };
    std::vector<U> units;
    int32_t now = 0;
    int32_t clip_frames = 10;
    bool support = true, ability_ok = true;
    std::vector<std::string> missing;  // clips the model lacks
    std::vector<Shot> shots;
    std::vector<Ability> abilities;
    std::vector<Clip> plays;
    uint32_t default_clips = 0;

    U* get(ObjectId id) {
        for (U& u : units)
            if (u.id == id) return &u;
        return nullptr;
    }
    U& add(ObjectId id, int32_t type, int32_t owner, Vec3 pos) {
        U u;
        u.id = id;
        u.type = type;
        u.owner = owner;
        u.pos = pos;
        units.push_back(u);
        return units.back();
    }
    void candidates(int32_t type, std::vector<ObjectId>& out) override {
        for (const U& u : units)
            if (u.type == type && u.alive) out.push_back(u.id);
    }
    bool alive(ObjectId id) override { return get(id) && get(id)->alive; }
    int32_t type_of(ObjectId id) override { return get(id) ? get(id)->type : -1; }
    int32_t owner(ObjectId id) override { return get(id) ? get(id)->owner : -1; }
    bool hero_type(int32_t) override { return true; }
    bool enemies(int32_t a, int32_t b) override { return a != b; }
    Vec3 position(ObjectId id) override { return get(id)->pos; }
    float yaw_deg(ObjectId id) override { return get(id)->yaw; }
    float health(ObjectId) override { return 500.0f; }
    bool jetpacking(ObjectId id) override { return get(id)->jet; }
    ObjectId attack_target(ObjectId) override { return kNoObject; }
    bool walking(ObjectId id) override { return get(id)->walking; }
    bool in_clash(ObjectId id) override { return get(id)->flag; }
    void set_in_clash(ObjectId id, bool on) override { get(id)->flag = on; }
    void move_to(ObjectId, Vec3) override {}
    void place(ObjectId, Vec3, float) override {}
    void face(ObjectId id, float yaw) override { get(id)->yaw = yaw; }
    void hold(ObjectId, bool) override {}
    bool clip(ObjectId, std::string_view, uint32_t, uint32_t&, uint32_t&, float&) override { return false; }
    bool speech_idle() override { return true; }
    void damage(ObjectId, float, ObjectId) override {}

    bool action_clashes() override { return support; }
    int32_t type_by_name(std::string_view n) override { return n == "Hero_A" ? kHeroA : n == "Hero_B" ? kHeroB : -1; }
    float health_fraction(ObjectId id) override { return get(id)->health_frac; }
    void action_hold(ObjectId id, bool on) override {
        U* u = get(id);
        u->held = on;
        u->flag = on;
        u->loco_susp = on;  // 0x63f060 suspends the locomotor
    }
    bool play_clip(ObjectId id, std::string_view anim, int32_t sub, bool loop, float blend, float speed) override {
        if (std::find(missing.begin(), missing.end(), std::string(anim)) != missing.end()) return false;
        U* u = get(id);
        u->clip = std::string(anim);
        u->clip_start = now;
        u->clip_loop = loop;
        plays.push_back({id, std::string(anim), sub, loop, blend, speed});
        return true;
    }
    void play_default_clip(ObjectId, float) override { ++default_clips; }
    int32_t clip_state(ObjectId id, std::string_view anim) override {
        const U* u = get(id);
        if (u->clip != anim) return 0;
        return u->clip_loop || now < u->clip_start + clip_frames ? 1 : 2;
    }
    void fire_projectile(ObjectId s, std::string_view bone, ObjectId t, std::string_view p) override {
        shots.push_back({s, t, std::string(p), std::string(bone), now});
    }
    bool locomotor_suspended(ObjectId id) override { return get(id)->loco_susp; }
    void suspend_locomotor(ObjectId id, bool on) override { get(id)->loco_susp = on; }
    void jetpack_locomotor(ObjectId id, bool on) override { get(id)->jetpack = on; }
    bool move_goal(ObjectId, Vec3& goal, bool) override {
        goal.z = 5.0f;  // the ground height under the goal
        return true;
    }
    void move_anim_override(ObjectId id, std::string_view anim, int32_t sub, float speedup) override {
        U* u = get(id);
        u->anim_override = std::string(anim);
        u->override_sub = sub;
        u->override_speedup = speedup;
    }
    void clear_move_anim_override(ObjectId id) override { get(id)->anim_override.clear(); }
    void speed_override(ObjectId id, float speed) override { get(id)->speed_override = speed; }
    void clear_speed_override(ObjectId id) override { get(id)->speed_override = 0.0f; }
    void order_move(ObjectId id, Vec3 goal) override {
        U* u = get(id);
        u->goal = goal;
        u->walking = true;
        ++u->orders;
    }
    void lock_orders(ObjectId id, bool on) override { get(id)->locked = on; }
    bool activate_ability(ObjectId id, std::string_view name, ObjectId target) override {
        abilities.push_back({id, target, std::string(name), now});
        return ability_ok;
    }
};

// One Action_Driven entry Hero_A / Hero_B (a mod's: Clash_Range 300) with the given Clash_Actions.
std::string action_xml(std::string_view actions) {
    std::string x = "<Hero_Clashes><Hero_Clash Name=\"Mod_Action\"><Clash_Type>Action_Driven</Clash_Type>"
                    "<Clash_Range>300.0</Clash_Range><First_Hero_Type>Hero_A</First_Hero_Type>"
                    "<Second_Hero_Type>Hero_B</Second_Hero_Type><Clash_Actions SubObjectList=\"Yes\">";
    x += actions;
    x += "</Clash_Actions></Hero_Clash></Hero_Clashes>";
    return x;
}

// An Attack_Action of `object` on `target` with a constant relevance.
std::string attack(std::string_view object, std::string_view target, std::string_view constant, std::string_view extra = {}) {
    std::string x = "<Attack_Action Name=\"Atk\"><Action_Object>";
    x += object;
    x += "</Action_Object><Action_Target>";
    x += target;
    x += "</Action_Target><Action_Relevance_Type>Constant</Action_Relevance_Type><Action_Relevance_Constant>";
    x += constant;
    x += "</Action_Relevance_Constant><Attack_Projectile_Type>Proj_Test</Attack_Projectile_Type>";
    x += extra;
    x += "</Attack_Action>";
    return x;
}

struct ActionRig {
    HeroClashData data;
    HeroClashManager m;
    ActionStub host;
    SimRng rng;
    uint32_t frame = 15;
    explicit ActionRig(const std::string& xml, uint32_t seed = 7u, float b_x = 100.0f) : rng(seed) {
        CHECK(parse_hero_clash_xml(clash_bytes(xml), data));
        m.set_data(&data, {{kHeroA, kHeroB}});
        host.add(1, kHeroA, 0, Vec3{0.0f, 0.0f, 0.0f});
        host.add(2, kHeroB, 1, Vec3{b_x, 0.0f, 0.0f});
    }
    void step() {
        host.now = static_cast<int32_t>(frame);
        m.update(frame++, host, rng);
    }
    void run_to(uint32_t f) {
        while (frame <= f) step();
    }
    const HeroClashManager::RunningAction* running(ObjectId actor) const {
        if (m.action_clashes().empty()) return nullptr;
        for (const auto& r : m.action_clashes()[0].running)
            if (r.actor == actor) return &r;
        return nullptr;
    }
};

}  // namespace

void test_clash_action_parse() {
    // The FoC ObiWan_BobaFett actions (HEROCLASH.XML), every field the three classes read; defaults from the class
    // constructors (0x71e840: shots 1, bone ROOT, speedup 1; 0x5e9b50: Move_Speed -1, speedup 1).
    // ledger: verifies Attack_Action.Action_Object
    // ledger: verifies Attack_Action.Action_Target
    // ledger: verifies Attack_Action.Action_Relevance_Type
    // ledger: verifies Attack_Action.Action_Distance_From_Target_Curve
    // ledger: verifies Attack_Action.Action_Health_Percentage_Curve
    // ledger: verifies Move_Action.Ability_Cooldown_Time_In_Secs
    const std::string xml = action_xml(R"(
<Attack_Action Name="ObiWan_Lightsaber_Attack">
  <Action_Object>Hero_A</Action_Object>
  <Action_Target>Hero_B</Action_Target>
  <Action_Relevance_Type>Distance_From_Target_Curve</Action_Relevance_Type>
  <Action_Distance_From_Target_Curve>16.001,0.0, 0.0,1.0, 16.0,1.0, 100000.0,0.0</Action_Distance_From_Target_Curve>
  <Attack_Projectile_Type>Proj_Saber</Attack_Projectile_Type>
  <Attack_Animation_Type>Attack</Attack_Animation_Type>
  <Attack_Animation_Subindex>-1</Attack_Animation_Subindex>
  <Attack_Animation_Speedup>12.0</Attack_Animation_Speedup>
  <Attack_Firing_Bone_Name>MUZZLEA_00</Attack_Firing_Bone_Name>
  <Attack_Number_Of_Shots>4</Attack_Number_Of_Shots>
  <Attack_SFXEvent>Unit_Storm_Trooper_Fire</Attack_SFXEvent>
</Attack_Action>
<Move_Action Name="ObiWan_Force_Run">
  <Action_Object>Hero_A</Action_Object>
  <Action_Target>Hero_B</Action_Target>
  <Action_Relevance_Type>Distance_From_Target_Curve_Times_Health_Percentage_Curve</Action_Relevance_Type>
  <Action_Health_Percentage_Curve>0.0,1.0, 0.4,1.0, 0.80,0.0, 1.0,0.0</Action_Health_Percentage_Curve>
  <Move_Type>Move_To_Target</Move_Type>
  <Wait_For_Finish_If_Target_Using_Jetpack>Yes</Wait_For_Finish_If_Target_Using_Jetpack>
  <Move_Speed>2.5</Move_Speed>
  <Move_Animation_Override>Yes</Move_Animation_Override>
  <Move_Animation_Type>Force_Run</Move_Animation_Type>
  <Move_Animation_Subindex>-1</Move_Animation_Subindex>
  <Move_Animation_Speedup>1.5</Move_Animation_Speedup>
  <Move_Min_Distance>10.0</Move_Min_Distance>
  <Move_Max_Distance>16.0</Move_Max_Distance>
</Move_Action>
<Move_Action Name="BobaFett_Jetpack_Jump">
  <Action_Object>Hero_B</Action_Object>
  <Action_Target>Hero_A</Action_Target>
  <Action_Relevance_Type>Health_Percentage_Curve</Action_Relevance_Type>
  <Move_Type>Move_Past_Target</Move_Type>
  <Move_Use_Jetpack>Yes</Move_Use_Jetpack>
</Move_Action>
<Special_Ability_Action Name="BobaFett_Flamethrower_Attack">
  <Action_Object>Hero_B</Action_Object>
  <Action_Target>Hero_A</Action_Target>
  <Ability_Name>Boba_Fett_Flame_Thrower</Ability_Name>
  <Ability_Cooldown_Time_In_Secs>1.5</Ability_Cooldown_Time_In_Secs>
</Special_Ability_Action>
<Attack_Action Name="Defaults"/>
<Not_An_Action/>)");
    HeroClashData d;
    std::vector<std::string> diag;
    CHECK(parse_hero_clash_xml(clash_bytes(xml), d, &diag));
    CHECK(d.entries.size() == 1 && diag.size() == 1);  // Not_An_Action: counted, not kept
    if (d.entries.size() != 1) return;
    const HeroClashEntry& e = d.entries[0];
    CHECK(e.action_count == 6 && e.actions.size() == 5);
    if (e.actions.size() != 5) return;
    const ClashAction& a = e.actions[0];
    CHECK(a.kind == kActionAttack && a.name == "ObiWan_Lightsaber_Attack" && a.object_type == "Hero_A" && a.target_type == "Hero_B");
    CHECK(a.relevance_type == kRelevanceDistance && a.projectile_type == "Proj_Saber" && a.attack_anim == "Attack");
    CHECK(a.attack_subindex == kAnyVariant && a.shots == 4 && a.firing_bone == "MUZZLEA_00" && a.sfx == "Unit_Storm_Trooper_Fire");
    CHECK_F(a.attack_speedup, 12.0f);
    // The curve's points are sorted by x (0x2c8660 sorts on first use): (0,1) (16,1) (16.001,0) (100000,0).
    CHECK(a.distance_curve.points.size() == 4 && a.distance_curve.points[2].first > 16.0f);
    CHECK_F(a.distance_curve.value(8.0f), 1.0f);
    CHECK_F(a.distance_curve.value(16.0f), 1.0f);
    CHECK_F(a.distance_curve.value(17.0f), 0.0f);
    CHECK_F(a.distance_curve.value(-1.0f), 0.0f);      // below the first point
    CHECK_F(a.distance_curve.value(200000.0f), 0.0f);  // above the last point
    const ClashAction& m = e.actions[1];
    CHECK(m.kind == kActionMove && m.relevance_type == kRelevanceDistanceTimesHealth && m.move_type == kMoveToTarget);
    CHECK(m.wait_for_jetpack && m.anim_override && !m.use_jetpack && m.move_anim == "Force_Run" && m.move_subindex == kAnyVariant);
    CHECK_F(m.move_speed, 2.5f);
    CHECK_F(m.move_speedup, 1.5f);
    CHECK_F(m.min_distance, 10.0f);
    CHECK_F(m.max_distance, 16.0f);
    CHECK_NEAR(m.health_curve.value(0.2f), 1.0, 1e-5);
    CHECK_NEAR(m.health_curve.value(0.6f), 0.5, 1e-5);  // linear between (0.4, 1) and (0.8, 0)
    CHECK(m.distance_curve.points.empty() && m.distance_curve.value(5.0f) == 0.0f);  // fewer than two points: 0
    const ClashAction& j = e.actions[2];
    CHECK(j.move_type == kMovePastTarget && j.use_jetpack && !j.wait_for_jetpack && j.relevance_type == kRelevanceHealth);
    CHECK_F(j.move_speed, -1.0f);
    CHECK_F(j.move_speedup, 1.0f);
    const ClashAction& s = e.actions[3];
    CHECK(s.kind == kActionSpecialAbility && s.ability_name == "Boba_Fett_Flame_Thrower");
    CHECK_F(s.cooldown_secs, 1.5f);
    CHECK(s.cooldown_frames == 45);  // post-parse 0x71f5f0: round(30 x 1.5)
    const ClashAction& def = e.actions[4];
    CHECK(def.relevance_type == -1 && def.shots == 1 && def.firing_bone == "ROOT" && def.attack_anim.empty());
    CHECK_F(def.attack_speedup, 1.0f);
}

void test_clash_action_pick() {
    // §11 pick (0x6aae80 / 0x5e9300 / 0x5e91a0): only an actor of type Action_Object, candidates of type Action_Target, one
    // synced draw weighted by relevance.
    // ledger: verifies Attack_Action.Action_Object
    // ledger: verifies Attack_Action.Action_Target
    // ledger: verifies Attack_Action.Action_Relevance_Type
    {  // without host support (default HeroClashHost) the entry never starts (s78n t06 rule kept)
        ActionRig r(action_xml(attack("Hero_A", "Hero_B", "1.0")));
        r.host.support = false;
        r.run_to(45);
        CHECK(r.m.empty() && r.rng.draws() == 0);
    }
    {  // the start: detection at frame 15, both held and flagged, A picks the attack, B (no action of its type) nothing
        ActionRig r(action_xml(attack("Hero_A", "Hero_B", "1.0")));
        r.step();
        CHECK(r.m.action_clashes().size() == 1 && r.m.started_total() == 1);
        CHECK(r.host.get(1)->held && r.host.get(1)->flag && r.host.get(2)->held && r.host.get(2)->flag);
        CHECK(r.running(1) != nullptr && r.running(2) == nullptr);
        CHECK(r.running(1) && r.running(1)->target == 2 && r.running(1)->interval == kAttackInterval);
        const auto pairs = r.m.pairs();
        CHECK(pairs.size() == 1 && pairs[0].first == 1 && pairs[0].second == 2);
    }
    {  // Action_Target missing, Action_Object of another type, or an Attack on a non-enemy (A on A): never chosen
        ActionRig r(action_xml("<Attack_Action><Action_Object>Hero_A</Action_Object><Action_Relevance_Type>Constant"
                               "</Action_Relevance_Type><Action_Relevance_Constant>1</Action_Relevance_Constant></Attack_Action>" +
                               attack("Hero_B", "Hero_B", "1.0") + attack("Hero_A", "Hero_A", "1.0")));
        r.run_to(20);
        CHECK(r.m.action_clashes().size() == 1 && r.running(1) == nullptr && r.running(2) == nullptr);
    }
    {  // relevance-weighted: constants 1 and 3 -> the second in about 3 of 4 picks (one draw per pick)
        uint32_t second = 0;
        const int n = 600;
        for (int seed = 1; seed <= n; ++seed) {
            ActionRig r(action_xml(attack("Hero_A", "Hero_B", "1.0") + attack("Hero_A", "Hero_B", "3.0")), static_cast<uint32_t>(seed));
            r.step();
            if (r.running(1) && r.running(1)->action == 1) ++second;
            CHECK(r.rng.draws() == 1);
        }
        CHECK_NEAR(second / static_cast<double>(n), 0.75, 0.05);
    }
    // ledger: verifies Attack_Action.Action_Distance_From_Target_Curve
    // ledger: verifies Attack_Action.Action_Health_Percentage_Curve
    const std::string curves = action_xml(
        "<Attack_Action><Action_Object>Hero_A</Action_Object><Action_Target>Hero_B</Action_Target>"
        "<Action_Relevance_Type>Distance_From_Target_Curve</Action_Relevance_Type>"
        "<Action_Distance_From_Target_Curve>0,1, 50,1, 50.001,0, 100000,0</Action_Distance_From_Target_Curve></Attack_Action>"
        "<Attack_Action><Action_Object>Hero_A</Action_Object><Action_Target>Hero_B</Action_Target>"
        "<Action_Relevance_Type>Distance_From_Target_Curve_Times_Health_Percentage_Curve</Action_Relevance_Type>"
        "<Action_Distance_From_Target_Curve>0,0, 50,0, 50.001,1, 100000,1</Action_Distance_From_Target_Curve>"
        "<Action_Health_Percentage_Curve>0,0, 0.3,0, 1,1</Action_Health_Percentage_Curve></Attack_Action>");
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        ActionRig close_rig(curves, seed, 30.0f), far_rig(curves, seed, 100.0f), hurt(curves, seed, 100.0f);
        hurt.host.get(1)->health_frac = 0.2f;  // health curve 0 below 0.3: the far action's product is 0
        close_rig.step();
        far_rig.step();
        hurt.step();
        CHECK(close_rig.running(1) && close_rig.running(1)->action == 0);
        CHECK(far_rig.running(1) && far_rig.running(1)->action == 1);
        CHECK(hurt.running(1) == nullptr);
    }
}

void test_clash_attack_action() {
    // §11 Attack_Action runner: serviced every 2 frames, fires when the attack clip is not playing, finishes once the clip
    // ended after Attack_Number_Of_Shots shots; the end plays clip type 6 looped.
    // ledger: verifies Attack_Action.Attack_Number_Of_Shots
    // ledger: verifies Attack_Action.Attack_Projectile_Type
    // ledger: verifies Attack_Action.Attack_Animation_Type
    {
        ActionRig r(action_xml(attack("Hero_A", "Hero_B", "1.0",
                                      "<Attack_Animation_Type>FW_Attack</Attack_Animation_Type><Attack_Number_Of_Shots>2"
                                      "</Attack_Number_Of_Shots><Attack_Firing_Bone_Name>MUZZLE</Attack_Firing_Bone_Name>")));
        r.host.get(2)->pos = Vec3{0.0f, 100.0f, 0.0f};
        r.run_to(34);
        CHECK(r.host.shots.size() == 2);
        if (r.host.shots.size() == 2) {
            CHECK(r.host.shots[0].frame == 15 && r.host.shots[1].frame == 25);  // the 10-frame clip ended at 25
            CHECK(r.host.shots[0].projectile == "Proj_Test" && r.host.shots[0].bone == "MUZZLE" && r.host.shots[0].target == 2);
        }
        CHECK(!r.host.plays.empty() && r.host.plays[0].anim == "FW_Attack" && !r.host.plays[0].loop && r.host.plays[0].sub == kAnyVariant);
        CHECK_NEAR(r.host.yaw_deg(1), 90.0, 1e-3);  // faces the target
        CHECK(r.running(1) && r.running(1)->shots_fired == 2);
        r.step();  // frame 35: the second clip ended, 2 shots: finished and ended
        CHECK(r.running(1) == nullptr);
        CHECK(!r.host.plays.empty() && r.host.plays.back().anim == "Attack Idle" && r.host.plays.back().loop);
        if (!r.host.plays.empty()) CHECK(r.host.plays.back().blend >= 0.1f && r.host.plays.back().blend <= 0.5f);
        r.step();  // frame 36: a new pick, serviced at once (last service 0)
        CHECK(r.host.shots.size() == 3 && r.host.shots.back().frame == 36);
    }
    {  // no such clip: the fallback clip type 5 ("Attack", any variant) is played and remembered
        ActionRig r(action_xml(attack("Hero_A", "Hero_B", "1.0", "<Attack_Animation_Type>Missing</Attack_Animation_Type>")));
        r.host.missing = {"Missing"};
        r.step();
        CHECK(r.host.shots.size() == 1 && r.running(1) && r.running(1)->fallback_clip);
        CHECK(!r.host.plays.empty() && r.host.plays[0].anim == "Attack");
        r.run_to(24);
        CHECK(r.running(1) != nullptr);
        r.step();  // frame 25: the fallback clip ended, 1 shot (the default count): finished
        CHECK(r.host.shots.size() == 1 && r.running(1) == nullptr);
    }
    {  // neither clip: finished at once, no shot; without the type 6 clip the end plays the default clip
        ActionRig r(action_xml(attack("Hero_A", "Hero_B", "1.0", "<Attack_Animation_Type>Missing</Attack_Animation_Type>")));
        r.host.missing = {"Missing", "Attack", "Attack Idle"};
        r.step();
        CHECK(r.host.shots.empty() && r.running(1) == nullptr && r.host.default_clips == 1);
    }
}

void test_clash_move_action() {
    // §B3 Move_Action runner (0x5e9ce0 begin, 0x5ea190 / 0x5e9f30 destinations, 0x5ea910 start, 0x5eaab0 update,
    // 0x5ea820 end), serviced every 6 frames.
    // ledger: verifies Move_Action.Move_Type
    // ledger: verifies Move_Action.Move_Speed
    // ledger: verifies Move_Action.Move_Animation_Type
    // ledger: verifies Move_Action.Move_Animation_Subindex
    // ledger: verifies Move_Action.Move_Animation_Speedup
    // ledger: verifies Move_Action.Move_Animation_Override
    // ledger: verifies Move_Action.Move_Min_Distance
    // ledger: verifies Move_Action.Move_Max_Distance
    const std::string to_target = action_xml(
        "<Move_Action><Action_Object>Hero_A</Action_Object><Action_Target>Hero_B</Action_Target>"
        "<Action_Relevance_Type>Constant</Action_Relevance_Type><Action_Relevance_Constant>1</Action_Relevance_Constant>"
        "<Move_Type>Move_To_Target</Move_Type><Move_Speed>2.5</Move_Speed><Move_Animation_Override>Yes</Move_Animation_Override>"
        "<Move_Animation_Type>Force_Run</Move_Animation_Type><Move_Animation_Subindex>2</Move_Animation_Subindex>"
        "<Move_Animation_Speedup>1.5</Move_Animation_Speedup><Move_Min_Distance>10.0</Move_Min_Distance>"
        "<Move_Max_Distance>16.0</Move_Max_Distance></Move_Action>");
    for (uint32_t seed = 1; seed <= 10; ++seed) {  // Move_To_Target: the actor stops U(min, max) short of the target
        ActionRig r(to_target, seed);
        r.step();
        const ActionStub::U& a = *r.host.get(1);
        CHECK(a.orders == 1 && a.locked && a.walking && !a.loco_susp);  // ordered, locked, the locomotor resumed
        CHECK(a.goal.x >= 84.0f - 1e-3f && a.goal.x <= 90.0f + 1e-3f && a.goal.y == 0.0f && a.goal.z == 5.0f);
        CHECK(a.anim_override == "Force_Run" && a.override_sub == 2);
        CHECK_F(a.override_speedup, 1.5f);
        CHECK_F(a.speed_override, 2.5f);
        CHECK(r.running(1) && r.running(1)->interval == kMoveInterval && r.running(1)->loco_suspended);
    }
    {  // the walk ends: finished at the next service; the end clears the overrides, unlocks, suspends the locomotor again
        ActionRig r(to_target);
        r.run_to(26);
        CHECK(r.running(1) && r.host.get(1)->orders == 1);  // serviced at 15 and 21: still walking, distance unchanged
        r.host.get(1)->walking = false;
        r.step();  // frame 27: finished and ended
        const ActionStub::U& a = *r.host.get(1);
        CHECK(r.running(1) == nullptr && a.orders == 1);
        CHECK(a.anim_override.empty() && a.speed_override == 0.0f && !a.locked && a.loco_susp);
        r.step();  // frame 28: the next pick orders again
        CHECK(a.orders == 2 && a.anim_override == "Force_Run" && a.locked);
    }
    {  // the target runs away: up to 3 restarts (no re-pick afterwards: B leaves the relevance curve)
        ActionRig r(action_xml(
            "<Move_Action><Action_Object>Hero_A</Action_Object><Action_Target>Hero_B</Action_Target>"
            "<Action_Relevance_Type>Distance_From_Target_Curve</Action_Relevance_Type>"
            "<Action_Distance_From_Target_Curve>0,1, 150,1, 150.001,0, 100000,0</Action_Distance_From_Target_Curve>"
            "<Move_Type>Move_To_Target</Move_Type><Move_Speed>2.5</Move_Speed><Move_Animation_Override>Yes</Move_Animation_Override>"
            "<Move_Animation_Type>Force_Run</Move_Animation_Type><Move_Min_Distance>10.0</Move_Min_Distance>"
            "<Move_Max_Distance>16.0</Move_Max_Distance></Move_Action>"));
        r.step();
        r.host.get(2)->pos = Vec3{100.0f, 0.5f, 0.0f};  // d2 grows by 0.25: within the +10 tolerance
        r.run_to(21);
        CHECK(r.host.get(1)->orders == 1);
        r.host.get(2)->pos = Vec3{200.0f, 0.0f, 0.0f};  // the target ran away: restart toward it (out of the curve now)
        r.run_to(27);
        const ActionStub::U& a = *r.host.get(1);
        CHECK(a.orders == 2 && r.running(1) && r.running(1)->restarts == 1);
        CHECK(a.goal.x >= 184.0f - 1e-3f && a.goal.x <= 190.0f + 1e-3f);
        r.host.get(2)->pos = Vec3{220.0f, 0.0f, 0.0f};
        r.run_to(33);
        r.host.get(2)->pos = Vec3{240.0f, 0.0f, 0.0f};
        r.run_to(39);
        CHECK(a.orders == 4 && r.running(1) && r.running(1)->restarts == 3);
        r.host.get(2)->pos = Vec3{260.0f, 0.0f, 0.0f};
        r.run_to(45);  // the fourth growth: the restarts are used up, finished and ended
        CHECK(r.running(1) == nullptr && a.orders == 4);
        CHECK(a.anim_override.empty() && a.speed_override == 0.0f && !a.locked && a.loco_susp);
    }
    // ledger: verifies Move_Action.Move_Use_Jetpack
    // ledger: verifies Move_Action.Move_Type
    for (uint32_t seed = 1; seed <= 10; ++seed) {  // Move_Past_Target with the jet pack: d + U(min, max), +-60 degrees
        ActionRig r(action_xml("<Move_Action><Action_Object>Hero_A</Action_Object><Action_Target>Hero_B</Action_Target>"
                               "<Action_Relevance_Type>Constant</Action_Relevance_Type><Action_Relevance_Constant>1"
                               "</Action_Relevance_Constant><Move_Type>Move_Past_Target</Move_Type><Move_Use_Jetpack>Yes"
                               "</Move_Use_Jetpack><Move_Min_Distance>100.0</Move_Min_Distance><Move_Max_Distance>140.0"
                               "</Move_Max_Distance></Move_Action>"),
                    seed);
        r.step();
        const ActionStub::U& a = *r.host.get(1);
        const float len = dmath::sqrt(a.goal.x * a.goal.x + a.goal.y * a.goal.y);
        CHECK(a.orders == 1 && a.jetpack && a.anim_override.empty() && a.speed_override == 0.0f);  // Move_Speed -1: none
        CHECK(len >= 200.0f - 1e-2f && len <= 240.0f + 1e-2f);
        CHECK(a.goal.x >= len * 0.5f - 1e-2f);  // within 60 degrees of the line to the target
        r.host.get(1)->walking = false;
        r.run_to(21);  // the service at 21: the walk ended, finished; the end swaps the jet pack back
        CHECK(r.running(1) == nullptr && !r.host.get(1)->jetpack);
    }
    // ledger: verifies Move_Action.Wait_For_Finish_If_Target_Using_Jetpack
    {  // the target is in a jet-pack jump: wait facing it, start once it landed
        ActionRig r(action_xml("<Move_Action><Action_Object>Hero_A</Action_Object><Action_Target>Hero_B</Action_Target>"
                               "<Action_Relevance_Type>Distance_From_Target_Curve</Action_Relevance_Type>"
                               "<Action_Distance_From_Target_Curve>0,1, 150,1, 150.001,0, 100000,0</Action_Distance_From_Target_Curve>"
                               "<Move_Type>Move_To_Target</Move_Type><Wait_For_Finish_If_Target_Using_Jetpack>Yes"
                               "</Wait_For_Finish_If_Target_Using_Jetpack><Move_Min_Distance>10.0</Move_Min_Distance>"
                               "<Move_Max_Distance>10.0</Move_Max_Distance></Move_Action>"),
                    7u, 200.0f);
        r.step();  // frame 15: started, B out of the curve: nothing to do
        CHECK(r.m.action_clashes().size() == 1 && r.running(1) == nullptr);
        r.host.get(2)->pos = Vec3{0.0f, 100.0f, 0.0f};
        r.host.get(2)->jet = true;
        r.step();  // frame 16: picked, waiting
        CHECK(r.running(1) && r.running(1)->waiting && r.host.get(1)->orders == 0);
        r.run_to(21);
        CHECK(r.host.get(1)->orders == 0);
        CHECK_NEAR(r.host.yaw_deg(1), 90.0, 1e-3);
        r.host.get(2)->jet = false;
        r.run_to(27);  // the next service: the goal 10 short of the target
        CHECK(r.host.get(1)->orders == 1 && r.running(1) && !r.running(1)->waiting);
        CHECK_NEAR(r.host.get(1)->goal.y, 90.0, 1e-3);
    }
}

void test_clash_special_ability_action() {
    // §B3: Special_Ability_Action activates Ability_Name on the target and keeps the actor busy for
    // Ability_Cooldown_Time_In_Secs (30 x 1.5 = 45 frames, 3 per service), not the ability's own recharge.
    // ledger: verifies Move_Action.Ability_Cooldown_Time_In_Secs
    const std::string xml = action_xml(
        "<Special_Ability_Action><Action_Object>Hero_A</Action_Object><Action_Target>Hero_B</Action_Target>"
        "<Action_Relevance_Type>Constant</Action_Relevance_Type><Action_Relevance_Constant>1</Action_Relevance_Constant>"
        "<Ability_Name>Boba_Fett_Flame_Thrower</Ability_Name><Ability_Cooldown_Time_In_Secs>1.5</Ability_Cooldown_Time_In_Secs>"
        "</Special_Ability_Action>");
    {
        ActionRig r(xml);
        r.step();
        CHECK(r.host.abilities.size() == 1 && r.host.abilities[0].name == "Boba_Fett_Flame_Thrower" && r.host.abilities[0].target == 2);
        r.run_to(56);
        CHECK(r.running(1) != nullptr && r.host.abilities.size() == 1);
        r.step();  // frame 57 = 15 + 42: the 15th service brings 45 to 0
        CHECK(r.running(1) == nullptr && r.host.abilities.size() == 1);
        r.step();  // frame 58: picked again
        CHECK(r.host.abilities.size() == 2 && r.host.abilities[1].frame == 58);
    }
    {  // the ability refuses: no action runs
        ActionRig r(xml);
        r.host.ability_ok = false;
        r.step();
        CHECK(r.host.abilities.size() == 1 && r.running(1) == nullptr);
    }
}

void test_clash_action_end_and_save() {
    // §4 object removed (0x6ab4d0): the remaining object is the winner (one synced Get_Int over one object: no draw),
    // every running action ends, every object is released, the clash is removed.
    const std::string xml = action_xml(attack("Hero_A", "Hero_B", "1.0", "<Attack_Number_Of_Shots>3</Attack_Number_Of_Shots>") +
                                       attack("Hero_B", "Hero_A", "1.0"));
    {
        ActionRig r(xml);
        r.run_to(20);
        CHECK(r.running(1) && r.running(2));
        r.host.get(2)->alive = false;
        r.step();
        CHECK(r.m.action_clashes().empty() && r.m.action_clashes_ended() == 1 && r.m.pairs().empty());
        CHECK(!r.host.get(1)->held && !r.host.get(1)->flag && !r.host.get(2)->flag);
        CHECK(!r.host.plays.empty() && r.host.plays.back().id == 1 && r.host.plays.back().anim == "Attack Idle");
    }
    {  // save / load mid-clash: the copy continues bit for bit
        ActionRig a(xml, 31u), b(xml, 31u);
        a.run_to(22);
        ByteWriter w;
        a.m.write(w);
        ByteWriter rw;
        a.rng.write(rw);
        ByteReader rd(w.bytes());
        CHECK(b.m.read(rd));
        ByteReader rr(rw.bytes());
        CHECK(b.rng.read(rr));
        b.host = a.host;
        b.frame = a.frame;
        StateHasher ha, hb;
        a.m.hash(ha);
        b.m.hash(hb);
        CHECK(ha.digest() == hb.digest());
        a.run_to(60);
        b.run_to(60);
        CHECK(a.host.shots.size() == b.host.shots.size() && a.host.shots.size() > 3);
        StateHasher ha2, hb2;
        a.m.hash(ha2);
        b.m.hash(hb2);
        CHECK(ha2.digest() == hb2.digest());
        // A save without an action clash is the version-1 layout (byte-identical to the melee-only saves).
        HeroClashManager empty;
        ByteWriter we;
        empty.write(we);
        CHECK(!we.bytes().empty() && we.bytes()[0] == 1u);
    }
}

}  // namespace bo3split
