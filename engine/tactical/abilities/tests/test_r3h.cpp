// r3h (docs/re/ABILITIES_HARDPOINTS.md §4.14 / §4.15): the clips the ability system hands its host (AbilityAnimPlay) per
// frame, and the Berserker damage passes of the §4.13 probe case under the retail restart rule (update 0x712fd0).
// Hand-computed frames on the OI sandbox (test_abilities_common.hpp).
#include "test_abilities_common.hpp"

namespace bo3split {

namespace {

struct PlayRec {
    uint32_t frame;
    ObjectId id;
    int32_t type;
    uint32_t variant;
    bool loop;
    float blend, speed;
};

void drain(OiWorld& w, std::vector<PlayRec>& log) {
    std::vector<AbilityAnimPlay> p;
    w.ab->take_anim_plays(p);
    for (const AbilityAnimPlay& x : p) log.push_back({x.frame, x.id, x.type, x.variant, x.loop != 0, x.blend, x.speed});
}

std::string trace(const std::vector<PlayRec>& log) {
    std::string s;
    for (const PlayRec& r : log)
        s += " " + std::to_string(r.frame) + ":" + std::to_string(r.id) + "/" + std::to_string(r.type) + "v" + std::to_string(r.variant) +
             (r.loop ? "L" : "");
    return s;
}

int32_t anim_id(std::string_view name) {
    if (name == "POUND_ATTACK") return 63;
    if (name == "EAT_ATTACK") return 64;
    if (name == "EATEN_DIE") return 65;
    return -1;
}

}  // namespace

// Force_Lightning (§4.14): FL_Attack one-shot (0xFFFF, one variant: no draw; blend 0.1) at the apply; the default idle
// at the frame the clip finishes (32 frames at 30 fps: end 30).
void r3h_lightning() {
    OiWorld w;
    oi_gate_world(w, true, nullptr);
    std::vector<PlayRec> log;
    const ObjectId one[] = {1};
    CHECK(w.ab->targeted_ability(one, kForceLightning, 2, -1, w.frame) == 1);
    for (int i = 0; i < 40; ++i) {
        w.tick();
        drain(w, log);
    }
    std::printf("  r3h lightning:%s\n", trace(log).c_str());
    CHECK(log.size() == 2);
    if (log.size() != 2) return;
    CHECK(log[0].id == 1 && log[0].type == 0x28 && log[0].variant == 0 && !log[0].loop && log[0].blend == 0.1f && log[0].speed == 1.0f);
    CHECK(log[1].id == 1 && log[1].type == kAbilityPlayDefaultIdle && log[1].frame == log[0].frame + 30);
}

// Generic_Attack (§4.14): Attack_Animation one-shot at Attack_Animation_Speedup, blend 0.2; the default idle at its end;
// the hit at Damage_Frame_Number. r3h t04: a clip from outside (observe_clip: the world shows a flinch) aborts it before
// the hit - default idle, release, no damage.
void r3h_generic(bool interrupt) {
    OiWorld w;
    SpecialAbilityDesc g = oi_special("Pound", SpecialClass::GenericAttack, ActivationStyle::UserInput);
    g.max_range = 100.0f;
    g.damage_amount = 30.0f;
    g.damage_frame = 10;
    g.anim_speedup = 1.0f;
    g.attack_anim = "POUND_ATTACK";
    const uint32_t gs = add_special(w.ad, g);
    AbilityObjectDesc a;
    a.unit_abilities = {oi_gui(kTargetedHack, 0.0f, 0.0f, "Pound")};
    a.specials = {gs};
    const uint32_t ud = push(w.cd, w.ad, oi_unit("Rancor", 1000.0f), a);
    const uint32_t td = push(w.cd, w.ad, oi_unit("Victim", 100.0f), AbilityObjectDesc{});
    w.make(GameMode::Land);
    AbilityHost h;
    h.anim_type = [](std::string_view n) { return anim_id(n); };
    h.anim_clip = [](ObjectId id, int32_t type, std::string_view name, uint32_t v, uint32_t& n, float& f) {
        if (id != 1 || v != 0 || (type != 63 && name != "POUND_ATTACK")) return false;
        n = 20;
        f = 30.0f;
        return true;
    };
    w.ab->set_host(std::move(h));
    w.add(1, ud, 0, {0, 0, 0});
    w.add(2, td, 1, {50, 0, 0});
    std::vector<PlayRec> log;
    const ObjectId one[] = {1};
    CHECK(w.ab->targeted_ability(one, kTargetedHack, 2, -1, w.frame) == 1);
    drain(w, log);
    int removed = -1;
    for (int i = 1; i <= 40 && removed < 0; ++i) {
        if (interrupt && i == 3) w.ab->observe_clip(1, 79, w.frame);  // the world shows Flinch_Left (a flinch replaced it)
        w.tick();
        drain(w, log);
        if (!w.ab->special_applied(1, name_crc("Pound"))) removed = i;
    }
    std::printf("  r3h generic%s: removed at tick %d, health %.0f, plays%s\n", interrupt ? " (interrupted)" : "", removed,
                static_cast<double>(w.sys->health(2)), trace(log).c_str());
    CHECK(log.size() == 2);
    if (log.size() != 2) return;
    CHECK(log[0].type == 63 && !log[0].loop && log[0].blend == 0.2f && log[0].speed == 1.0f);
    CHECK(log[1].type == kAbilityPlayDefaultIdle);
    if (interrupt) {
        CHECK(w.sys->health(2) == 100.0f && removed == 3 && log[1].frame == log[0].frame + 2);  // the update after the look
    } else {
        CHECK(w.sys->health(2) == 70.0f && log[1].frame == log[0].frame + 18);
    }
}

// r3h review: Generic_Attack with two Attack_Animation variants (20 and 40 frames): the apply draws one (0xFFFF) and the
// update times the hit and the end on the drawn clip - the 40-frame variant ends 20 frames later than the 20-frame one.
// Returns the drawn variant, the tick the special was removed and the tick of the default idle.
struct GenericRun {
    uint32_t variant = 0xFFFFu;
    int removed = -1, idle = -1;
    float health = 0.0f;
};
GenericRun r3h_generic_variant(uint32_t seed) {
    OiWorld w;
    SpecialAbilityDesc g = oi_special("Pound", SpecialClass::GenericAttack, ActivationStyle::UserInput);
    g.max_range = 100.0f;
    g.damage_amount = 30.0f;
    g.damage_frame = 10;
    g.anim_speedup = 1.0f;
    g.attack_anim = "POUND_ATTACK";
    const uint32_t gs = add_special(w.ad, g);
    AbilityObjectDesc a;
    a.unit_abilities = {oi_gui(kTargetedHack, 0.0f, 0.0f, "Pound")};
    a.specials = {gs};
    const uint32_t ud = push(w.cd, w.ad, oi_unit("Rancor", 1000.0f), a);
    const uint32_t td = push(w.cd, w.ad, oi_unit("Victim", 100.0f), AbilityObjectDesc{});
    w.make(GameMode::Land);
    w.rng = SimRng{seed};
    AbilityHost h;
    h.anim_type = [](std::string_view n) { return anim_id(n); };
    h.anim_variants = [](ObjectId id, int32_t type) { return (id == 1 && type == 63) ? 2u : 0u; };
    h.anim_clip = [](ObjectId id, int32_t type, std::string_view name, uint32_t v, uint32_t& n, float& f) {
        if (id != 1 || v > 1 || (type != 63 && name != "POUND_ATTACK")) return false;
        n = v == 0 ? 20u : 40u;
        f = 30.0f;
        return true;
    };
    w.ab->set_host(std::move(h));
    w.add(1, ud, 0, {0, 0, 0});
    w.add(2, td, 1, {50, 0, 0});
    std::vector<PlayRec> log;
    const ObjectId one[] = {1};
    GenericRun r;
    if (w.ab->targeted_ability(one, kTargetedHack, 2, -1, w.frame) != 1) return r;
    drain(w, log);
    for (int i = 1; i <= 60 && r.removed < 0; ++i) {
        w.tick();
        drain(w, log);
        if (!w.ab->special_applied(1, name_crc("Pound"))) r.removed = i;
    }
    if (!log.empty()) r.variant = log[0].variant;
    for (const PlayRec& p : log)
        if (p.type == kAbilityPlayDefaultIdle && !log.empty()) r.idle = static_cast<int>(p.frame - log[0].frame);
    r.health = w.sys->health(2);
    return r;
}

void r3h_generic_variants() {
    GenericRun v0, v1;
    for (uint32_t seed = 1; seed <= 64 && (v0.removed < 0 || v1.removed < 0); ++seed) {
        const GenericRun r = r3h_generic_variant(seed);
        if (r.variant == 0 && v0.removed < 0) v0 = r;
        if (r.variant == 1 && v1.removed < 0) v1 = r;
    }
    std::printf("  r3h generic variants: v0 removed %d idle +%d health %.0f; v1 removed %d idle +%d health %.0f\n", v0.removed,
                v0.idle, static_cast<double>(v0.health), v1.removed, v1.idle, static_cast<double>(v1.health));
    CHECK(v0.removed > 0 && v1.removed == v0.removed + 20);
    CHECK(v0.idle == 18 && v1.idle == 38);
    CHECK(v0.health == 70.0f && v1.health == 70.0f);
}

// Eat_Attack (§4.14, r3h t04): the owner plays Attack_Animation (blend 0.2); at Attachment_Frame the victim loops its
// Target_Grabbed_Animation (EATEN_DIE 65, blend 0); it is destroyed at Target_Destruction_Frame.
void r3h_eat() {
    OiWorld w;
    SpecialAbilityDesc e = oi_special("Eat", SpecialClass::EatAttack, ActivationStyle::UserInput);
    e.max_range = 100.0f;
    e.attack_anim = "EAT_ATTACK";
    e.grabbed_anim = "EATEN_DIE";
    e.attachment_frame = 14;
    e.destruction_frame = 110;
    const uint32_t es = add_special(w.ad, e);
    AbilityObjectDesc a;
    a.unit_abilities = {oi_gui(kTargetedHack, 0.0f, 0.0f, "Eat")};
    a.specials = {es};
    const uint32_t ud = push(w.cd, w.ad, oi_unit("Rancor", 1000.0f), a);
    const uint32_t td = push(w.cd, w.ad, oi_unit("Victim", 100.0f), AbilityObjectDesc{});
    w.make(GameMode::Land);
    AbilityHost h;
    h.anim_type = [](std::string_view n) { return anim_id(n); };
    h.anim_clip = [](ObjectId id, int32_t type, std::string_view name, uint32_t v, uint32_t& n, float& f) {
        if (v != 0) return false;
        if (id == 1 && (type == 64 || name == "EAT_ATTACK")) n = 112;
        else if (id == 2 && type == 65) n = 30;
        else return false;
        f = 30.0f;
        return true;
    };
    w.ab->set_host(std::move(h));
    w.add(1, ud, 0, {0, 0, 0});
    w.add(2, td, 1, {50, 0, 0});
    std::vector<PlayRec> log;
    const ObjectId one[] = {1};
    CHECK(w.ab->targeted_ability(one, kTargetedHack, 2, -1, w.frame) == 1);
    drain(w, log);
    uint32_t dead = 0;
    for (int i = 1; i <= 130 && dead == 0; ++i) {
        w.tick();
        drain(w, log);
        if (!w.sys->alive(2)) dead = w.frame - 1;
    }
    std::printf("  r3h eat: victim destroyed at %u, plays%s\n", dead, trace(log).c_str());
    CHECK(log.size() >= 2);
    if (log.size() < 2) return;
    CHECK(log[0].id == 1 && log[0].type == 64 && log[0].blend == 0.2f && !log[0].loop);
    CHECK(log[1].id == 2 && log[1].type == 65 && log[1].loop && log[1].blend == 0.0f && log[1].frame == log[0].frame + 14);
    CHECK(dead == log[0].frame + 110);
}

// Berserker (§4.15, the §4.13 probe case): Yoda's SpinMove has three variants (36, 38, 36 frames at 30 fps), 6 s of
// berserk = 180 updates. Apply: SpinMove loop, 0xFFFF. Update: near the end (frame >= count - 3) the clip restarts with a
// new draw; damage passes when counter % 5 == 0 (every frame near the end) and 0.2 count < frame < 0.8 count.
// Returns the passes; `variants` gets the SpinMove variants played (apply first).
int r3h_berserker_passes(uint32_t seed, std::vector<uint32_t>* variants, const std::vector<uint32_t>& counts) {
    OiWorld w;
    w.rng = SimRng{seed};
    SpecialAbilityDesc b = oi_special("Berserk", SpecialClass::Berserker, ActivationStyle::UserInput);
    b.max_range = 500.0f;
    b.berserker_speed = 5.0f;
    b.berserker_damage = 1.0f;
    b.berserker_damage_range = 1000.0f;
    b.berserker_duration = 6.0f;  // 180 frames
    const uint32_t bs = add_special(w.ad, b);
    AbilityObjectDesc a;
    a.unit_abilities = {oi_gui(kBerserker, 0.0f, 60.0f, "Berserk")};
    a.specials = {bs};
    const uint32_t bd = push(w.cd, w.ad, oi_unit("Yoda", 1000.0f), a);
    UnitDesc e = oi_unit("Enemy", 100000.0f);
    e.has_locomotor = true;
    const uint32_t td = push(w.cd, w.ad, e, AbilityObjectDesc{});
    w.make(GameMode::Land);
    AbilityHost h;
    h.anim_clip = [counts](ObjectId id, int32_t type, std::string_view, uint32_t v, uint32_t& n, float& f) {
        if (id != 1 || type != 0x68 || v >= counts.size()) return false;
        n = counts[v];
        f = 30.0f;
        return true;
    };
    h.anim_variants = [counts](ObjectId id, int32_t type) { return id == 1 && type == 0x68 ? static_cast<uint32_t>(counts.size()) : 0u; };
    w.ab->set_host(std::move(h));
    w.add(1, bd, 0, {0, 0, 0});
    w.add(2, td, 1, {100, 0, 0});
    std::vector<PlayRec> log;
    const ObjectId one[] = {1};
    CHECK(w.ab->targeted_ability(one, kBerserker, 2, -1, w.frame) == 1);
    for (int i = 0; i < 200; ++i) {
        w.tick();
        drain(w, log);
    }
    drain(w, log);
    CHECK(!w.ab->special_applied(1, name_crc("Berserk")));
    if (variants) {
        variants->clear();
        for (const PlayRec& r : log)
            if (r.id == 1 && r.type == 0x68) variants->push_back(r.variant);
    }
    return static_cast<int>(100000.0f - w.sys->health(2));
}

// r3h review S2-1: a play queued after the host drained the queue (here an activation between ticks) is saved: the run
// loaded from that point hands its host the same plays and ends in the same state as the unbroken run.
void r3h_save_queue() {
    OiWorld a;
    oi_gate_world(a, true, nullptr);
    const ObjectId one[] = {1};
    CHECK(a.ab->targeted_ability(one, kForceLightning, 2, -1, a.frame) == 1);
    const SavedBattle saved = save_battle(*a.sys, *a.ab, a.rng);
    OiWorld b;
    oi_gate_world(b, true, nullptr);
    CHECK(load_battle(*b.sys, *b.ab, b.rng, saved));
    b.frame = a.frame;
    std::vector<PlayRec> la, lb;
    for (int i = 0; i < 40; ++i) {
        a.tick();
        drain(a, la);
        b.tick();
        drain(b, lb);
    }
    std::printf("  r3h save queue: unbroken%s / loaded%s\n", trace(la).c_str(), trace(lb).c_str());
    CHECK(la.size() == 2 && trace(la) == trace(lb));
    CHECK(battle_digest(*a.sys, *a.ab, a.rng) == battle_digest(*b.sys, *b.ab, b.rng));
}

void test_r3h_anim_plays() {
    r3h_lightning();
    r3h_save_queue();
    r3h_generic(false);
    r3h_generic(true);
    r3h_generic_variants();  // r3h review
    r3h_eat();
    // Berserker: one variant (no draws) - 38 frames: restarts every 35 frames, 25 passes; 36 frames: every 33, 22 passes.
    const int p38 = r3h_berserker_passes(777u, nullptr, {38u});
    const int p36 = r3h_berserker_passes(777u, nullptr, {36u});
    // The probe case: the shipped three variants; retail's run 1 played variant 1 first and restarted after 36, 35, 34, 34,
    // 34 frames (+-1 reading; sum 173 = four 38-frame loops of 35 and one 36-frame loop of 33) and dealt 23 passes. Find a
    // seed whose draws give that sequence (1, 1, 1, 0 or 2, 1).
    int probe = -1;
    uint32_t probe_seed = 0;
    std::vector<uint32_t> v;
    for (uint32_t seed = 1; seed <= 3000 && probe < 0; ++seed) {
        const int p = r3h_berserker_passes(seed, &v, {36u, 38u, 36u});
        if (v.size() >= 5 && v[0] == 1 && v[1] == 1 && v[2] == 1 && v[3] != 1 && v[4] == 1) {
            probe = p;
            probe_seed = seed;
        }
    }
    std::printf("  r3h berserker: 38-frame clip %d passes, 36-frame clip %d, probe sequence (seed %u) %d passes\n", p38, p36, probe_seed,
                probe);
    CHECK(p38 == 25 && p36 == 22);
    CHECK(probe == 23);
}

}  // namespace bo3split
