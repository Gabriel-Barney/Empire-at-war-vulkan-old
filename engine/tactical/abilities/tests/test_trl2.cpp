// trl2 t03: Earthquake_Attack / Force_Telekinesis target gates and the target behaviour's ramp state
// (docs/re/TACTICAL_GAPS.md §2.3.4). Hand-computed frames on the OI sandbox.
#include "test_abilities_common.hpp"

namespace bo3split {

namespace {
struct QuakeWorld {
    OiWorld w;
    uint32_t hero = 0, target = 0, plain = 0;
};

// Two heroes (1, 4) of player 0 with `cls`; player 1 owns 2 (has the target behaviour) and 3 (has not). D = 10.
void quake_world(QuakeWorld& q, SpecialClass cls) {
    OiWorld& w = q.w;
    SpecialAbilityDesc s = oi_special("Quake", cls, ActivationStyle::UserInput);
    s.max_range = 200.0f;
    s.activation_chance = 1.0f;
    s.damage_amount = 10.0f;
    s.damage_percentage = 0.0f;
    s.damage_interval = 0.5f;  // 15 frames
    const uint32_t sd = add_special(w.ad, s);
    AbilityObjectDesc a;
    a.unit_abilities = {oi_gui(kForceTelekinesis, 0.0f, 0.0f, "Quake")};
    a.specials = {sd};
    q.hero = push(w.cd, w.ad, oi_unit("Hero", 100.0f), a);
    UnitDesc t = oi_unit("Walker", 1000.0f);
    t.earthquake_target = cls == SpecialClass::Earthquake;
    t.telekinesis_target = cls == SpecialClass::Telekinesis;
    q.target = push(w.cd, w.ad, t, AbilityObjectDesc{});
    q.plain = push(w.cd, w.ad, oi_unit("Gargantuan", 1000.0f), AbilityObjectDesc{});
    w.make(GameMode::Land);
    w.add(1, q.hero, 0, {0, 0, 0});
    w.add(2, q.target, 1, {150, 0, 0});
    w.add(3, q.plain, 1, {150, 60, 0});
    w.add(4, q.hero, 0, {0, 60, 0});
}
}  // namespace

void test_trl2_quake_gates_and_ramp() {
    for (const SpecialClass cls : {SpecialClass::Earthquake, SpecialClass::Telekinesis}) {
        QuakeWorld q;
        quake_world(q, cls);
        OiWorld& w = q.w;
        const uint32_t quake = name_crc("Quake");
        const ObjectId one[] = {1}, four[] = {4};
        // 0x6f7510 / 0x6fe400: a target without EARTHQUAKE_TARGET / TELEKINESIS_TARGET is refused
        CHECK(w.ab->targeted_ability(one, kForceTelekinesis, 3, -1, w.frame) == 0);
        CHECK(w.ab->targeted_ability(one, kForceTelekinesis, 2, -1, w.frame) == 1);
        w.tick();
        CHECK(w.ab->special_applied(1, quake) && w.ab->special_target(1, quake) == 2);
        const uint32_t applied = w.frame - 1;  // the tick that fired the pending ability
        // 0x6331b0 / 0x63efa0: a second user cannot take a target the behaviour already holds (Earthquake refuses in its
        // accepts, Telekinesis in its apply)
        if (cls == SpecialClass::Earthquake) {
            CHECK(w.ab->targeted_ability(four, kForceTelekinesis, 2, -1, w.frame) == 0);
        } else {
            w.ab->targeted_ability(four, kForceTelekinesis, 2, -1, w.frame);
            w.tick();
        }
        CHECK(!w.ab->special_applied(4, quake));
        // State 1 ramps over the GameConstants transition time (Earthquake 2.0 s = 60 frames, Telekinesis 0.75 s =
        // 22.5 frames): no damage before it; then state 2 (next damage := now) and the first hit on the service after.
        const uint32_t ramp = cls == SpecialClass::Earthquake ? 60u : 23u;
        while (w.frame < applied + ramp) {
            CHECK_EQ(w.sys->health(2), 1000.0f);
            w.tick();
        }
        uint32_t first = 0;
        for (int i = 0; i < 12 && first == 0; ++i) {
            w.tick();
            if (w.sys->health(2) < 1000.0f) first = w.frame - 1;
        }
        // the special's update runs every 3 frames: the ramp ends at the first update at or after `ramp`, the hit lands on
        // the next update
        CHECK(first > applied + ramp && first <= applied + ramp + 6);
        CHECK_EQ(w.sys->health(2), 990.0f);
        // then every Damage_Interval_In_Secs (0.5 s = 15 frames) counted from the ramp's end (one update, 3 frames, before
        // the first hit): the second hit 12 frames after the first (15 when the float sum lands just past an update)
        while (w.frame <= first + 18 && w.sys->health(2) == 990.0f) w.tick();
        CHECK_EQ(w.sys->health(2), 980.0f);
        CHECK(w.frame - 1 == first + 12 || w.frame - 1 == first + 15);
        std::printf("  trl2 quake (%s): applied %u, first hit %u, second %u\n",
                    cls == SpecialClass::Earthquake ? "earthquake" : "telekinesis", applied, first, w.frame - 1);
    }
}

}  // namespace bo3split
