// alamo_ai_tactical_records_tests: REF1 t03 - the tactical AI world's state records and their one field list each
// (src/tactical_ai_records.hpp), walked by the save writer, the save reader and the CRC hasher:
//   * round trip: write -> read -> write gives the same bytes and the same CRC for every record;
//   * CRC pin: each hashed record's CRC bytes are the ones TacticalAiWorld::hash fed before REF1 (the goldens' order);
//   * save pin: each record's save bytes are the ones TacticalAiWorld::write put before REF1 (existing saves load);
//   * field coverage: changing any one field changes the save bytes (saved fields) and the CRC (hashed fields), a
//     hash-only field leaves the record's save bytes alone, and the changed record still round-trips.
#include <cstdio>
#include <span>
#include <type_traits>
#include <string>
#include <vector>

#include "alamo/core/bytes.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/lua/lua_vm.hpp"  // PlanInstance owns its VM
#include "alamo/objects/game_object_types.hpp"
#include "alamo/objects/object_store.hpp"
#include "alamo/test/support.hpp"
#include "tactical_ai_records.hpp"  // engine/ai/tactical/src (not installed)

using namespace alamo;
using namespace alamo::ai;
using namespace alamo::ai::tactical;
namespace io = alamo::ai::tactical::save_io;

using ::alamo::test::g_fail;

namespace {

// Two live objects: record handles point at the first, the coverage walk swaps in the second.
struct Store {
    objects::GameObjectTypes types;
    objects::IdCounter ids;
    objects::ObjectStore store{objects::ModeKind::Space, objects::StoreKind::Primary, types, &ids};
    objects::ObjectHandle a, b;
    Store() {
        objects::SpawnParams sp;
        sp.owner = 1;
        a = store.create(sp);
        b = store.create(sp);
    }
};

// ---------------------------------------------------------------- samples: a non-default value in every field

TacticalTarget sample_target() {
    TacticalTarget t;
    t.kind = TargetKind::Region;
    t.object_id = 41;
    t.name = "CELL_3_4";
    t.position = {10.0f, -20.0f, 3.0f};
    t.eval_rect = {-5.0f, -6.0f, 7.0f, 8.0f};
    t.zone = 5;
    t.cell_x = 3;
    t.cell_y = 4;
    return t;
}

TacticalPpPick sample_pick(int32_t i) {
    TacticalPpPick k;
    k.type = 3 + i;
    k.source = PpSource::Build;
    k.ref = 900 + i;
    k.tf = 1;
    k.team = 2;
    k.contrast_entry = 6;
    return k;
}

Slot sample_slot(const Store&) {
    Slot s;
    s.t = sample_target();
    s.handle = 12;
    s.alive = false;
    return s;
}

PlanInstance sample_plan(const Store&) {
    PlanInstance p;
    p.id = 7;
    p.player = 1;
    p.plan = 4;
    p.goal_type = 9;
    p.slot = 2;
    p.target_owner = 3;
    p.tfs = {11, 12};
    p.result = true;
    p.non_removable = true;
    p.ended = true;
    p.live_threads = 2;
    p.pp = 5;
    return p;
}

TfRec sample_tf(const Store& s) {
    TfRec t;
    t.id = 11;
    t.plan = 7;
    t.player = 1;
    t.def = 2;
    t.name = "MainForce";
    t.kind = lua::AiTaskForceKind::Space;
    t.units = {s.a, s.a};
    TacticalReinforcement e;
    e.key = 1001;
    e.type = 3;
    e.population = 2.5f;
    e.units = 3;
    e.member_type = 4;
    e.member_create_team = true;
    e.count = 2;
    t.pending = {e};
    t.reservations = {{50, 3}};
    t.producing = {{3, 60}};
    t.priorities = "Infantry";
    return t;
}

StatusRec sample_status(const Store& s) {
    StatusRec r;
    r.id = 4;
    r.plan = 7;
    r.tf = 11;
    r.kind = StatusKind::Ambush;
    r.finished = true;
    r.result = lua::AiStatusResult::True;
    r.started = 100;
    r.last_order = 130;
    r.unit = s.a;
    r.target = 41;
    r.dest = {1.0f, 2.0f, 0.0f};
    r.timeout = 400;
    r.ring = 1000.0f;
    r.prevent = true;
    r.direction = 2;
    r.min_dist = 300.0f;
    r.threat = 5.0f;
    r.plan_frame = 77;
    r.lists = true;
    r.members = {41, 42};
    r.moving = {42};
    r.landings = {{1001, 3}};
    r.saved_removable = false;
    r.target_owner = 0;
    r.pending = {41};
    return r;
}

TacPpRec sample_pp(const Store&) {
    TacPpRec p;
    p.player = 1;
    p.plan = 4;
    p.picks = {sample_pick(0), sample_pick(1)};
    p.reserved = true;
    p.layer_space = true;
    p.layer_ground = true;
    p.contrast_failed = true;
    p.lua_released = {1001, 1003};
    return p;
}

AttackPositioning sample_attack_pos(const Store&) {
    AttackPositioning a;
    a.flag = true;
    a.next = 345;
    a.moving = true;
    a.pos = {4.0f, 5.0f, 6.0f};
    return a;
}

PendingBonus sample_bonus(const Store&) {
    PendingBonus b;
    b.goal_type = 3;
    b.target = 2;
    b.bonus = 0.5f;
    b.deadline = 900;
    return b;
}

TacticalPpResult sample_pp_result(const Store&) {
    TacticalPpResult o;
    o.ok = true;
    o.picks = {sample_pick(2)};
    o.candidates = 6;
    o.last_veto = "Contrast";
    o.layer_space = true;
    o.layer_ground = true;
    return o;
}

TacticalAiObject sample_fact(const Store& s) {
    TacticalAiObject f;
    f.handle = s.a;
    f.target.id = 41;
    f.target.dead_or_removing = true;
    f.target.rep_has_land_evaluator = true;
    f.target.rep_has_space_evaluator = true;
    f.target.own_has_space_evaluator = true;
    f.applies.object_id = 41;
    f.applies.has_owner = true;
    f.applies.owner_allied = true;
    f.applies.has_locomotor = true;
    f.applies.has_build_objects = true;
    f.applies.has_cash_point = true;
    f.applies.reinforcement_enabling_radius = 250.0f;
    f.applies.last_state_visible_under_fow = true;
    f.applies.initial_state_visible_under_fow = true;
    for (bool* b : free_store_flags(f.free_store)) *b = true;
    f.team_members = {42, 43};
    f.moving = true;
    f.attack_target = 42;
    f.damage_tracking = true;
    f.reprojection_stamp = 19;
    f.valid_target = false;
    f.hero_clash = true;
    f.pad_occupied = true;
    return f;
}

// ---------------------------------------------------------------- the three archives

template <class R>
std::vector<uint8_t> save_bytes(const R& x, const Store& s) {
    ByteWriter w;
    io::SaveWriter a(w, &s.store);
    visit(a, x);
    return w.bytes();
}

template <class R>
uint64_t crc(const R& x, const Store& s) {
    StateHasher h;
    io::RecordHasher a(h, &s.store);
    visit(a, x);
    return h.digest();
}

template <class R>
bool load(std::span<const uint8_t> bytes, const Store& s, R& out) {
    ByteReader r(bytes);
    io::SaveReader a(r, &s.store);
    visit(a, out);
    return r.ok() && r.done();
}

// Fields the world saves in a block of their own, not with the record (TacticalAiWorld::write reads them back there).
template <class R>
void restore_elsewhere(R&, const R&) {}
void restore_elsewhere(TacPpRec& y, const TacPpRec& x) { y.lua_released = x.lua_released; }

// write -> read -> write: same bytes, same CRC.
template <class R, class Make>
bool round_trip(const char* name, Make make, const Store& s) {
    const R x = make(s);
    const std::vector<uint8_t> bytes = save_bytes(x, s);
    R y{};
    const bool loaded = load(bytes, s, y);
    restore_elsewhere(y, x);
    const bool ok = loaded && save_bytes(y, s) == bytes && crc(y, s) == crc(x, s);
    if (!ok) std::printf("  round trip FAILED: %s\n", name);
    return ok;
}

// ---------------------------------------------------------------- field coverage walk

}  // namespace

namespace alamo::ai::tactical::save_io {

// Changes the k-th field the visit lists (an archive like the three above) and reports which streams it is in: its
// own In, narrowed by the list holding it (a field of a saved-only list is saved only). Declared in save_io, like the
// other archives, so visit() calls on it find the records' field lists.
class ChangeField {
public:
    ChangeField(size_t k, objects::ObjectHandle other) : k_(k), other_(other) {}
    static bool since(io::Feature) { return true; }
    bool hit() const { return hit_; }
    io::In role() const { return role_; }

    template <class T>
    void field(T& v, io::In in = io::In::Both) {
        if (!take(in)) return;
        if constexpr (std::is_same_v<T, bool>)
            v = !v;
        else if constexpr (std::is_floating_point_v<T>)
            v += T(1);
        else
            v = static_cast<T>(v + 1);
    }
    template <class U, class T>
    void field_as(T& v, io::In in = io::In::Both) {
        if (take(in)) v = static_cast<T>(v + 1);
    }
    template <class E>
    void enumeration(E& e, E max, io::In in = io::In::Both) {
        if (!take(in)) return;
        const auto b = static_cast<uint8_t>(e);
        e = static_cast<E>(b < static_cast<uint8_t>(max) ? b + 1 : b - 1);
    }
    void str(std::string& s, io::In in = io::In::Both) {
        if (take(in)) s += "x";
    }
    void vec3(Vec3f& v, io::In in = io::In::Both) {
        if (take(in)) v.y += 1.0f;
    }
    void rect(Rect2& x, io::In in = io::In::Both) {
        if (take(in)) x.max_y += 1.0f;
    }
    template <class T>
    void vec(std::vector<T>& v, io::In in = io::In::Both) {
        if (take(in)) v.push_back(T(7));
    }
    void handle(objects::ObjectHandle& h, io::In in = io::In::Both) {
        if (take(in)) h = other_;
    }
    template <class T, class F>
    void list(std::vector<T>& v, io::In in, size_t, F&& fn) {
        if (take(in)) {  // the list itself: one more element
            v.push_back(v.empty() ? T{} : v.front());
            return;
        }
        outer_.push_back(in);
        for (T& x : v) fn(*this, x);
        outer_.pop_back();
    }
    void hash_tag(uint8_t) {}
    template <class T, class U>
    void absent(T&, const U&) {}

private:
    bool take(io::In in) {
        if (n_++ != k_) return false;
        role_ = in;
        for (const io::In o : outer_)
            if (o != io::In::Both) role_ = o;
        hit_ = true;
        return true;
    }
    size_t k_, n_ = 0;
    objects::ObjectHandle other_;
    std::vector<io::In> outer_;
    io::In role_ = io::In::Both;
    bool hit_ = false;
};

}  // namespace alamo::ai::tactical::save_io

namespace {

using io::ChangeField;

const char* role_name(io::In in) { return in == io::In::Both ? "save+crc" : in == io::In::Save ? "save" : "crc"; }

template <class R, class Make>
bool coverage(const char* name, Make make, const Store& s, size_t& fields) {
    const R base = make(s);
    const std::vector<uint8_t> b0 = save_bytes(base, s);
    const uint64_t h0 = crc(base, s);
    bool ok = true;
    for (size_t k = 0;; ++k) {
        R x = make(s);
        ChangeField c(k, s.b);
        visit(c, x);
        if (!c.hit()) break;
        ++fields;
        const std::vector<uint8_t> b1 = save_bytes(x, s);
        const uint64_t h1 = crc(x, s);
        const bool saved = c.role() != io::In::Hash, hashed = c.role() != io::In::Save;
        bool good = saved ? b1 != b0 : b1 == b0;
        if (hashed) good = good && h1 != h0;
        R y{};
        const bool loaded = load(b1, s, y);
        restore_elsewhere(y, x);
        good = good && loaded && save_bytes(y, s) == b1 && crc(y, s) == h1;
        if (!good) std::printf("  coverage FAILED: %s field %zu (%s)\n", name, k, role_name(c.role()));
        ok = ok && good;
    }
    return ok;
}

// ---------------------------------------------------------------- CRC pin (TacticalAiWorld::hash before REF1)

void pin_target_slot(StateHasher& h, const Slot& s) {
    h.u8(static_cast<uint8_t>(s.t.kind));
    h.i32(s.t.object_id);
    h.i32(s.t.zone);
    h.i32(s.t.cell_x);
    h.i32(s.t.cell_y);
    h.i32(s.handle);
    h.boolean(s.alive);
}

void pin_plan(StateHasher& h, const PlanInstance& p) {
    h.i32(p.player);
    h.u32(p.plan);
    h.u32(p.goal_type);
    h.i32(p.slot);
    h.boolean(p.result);
    h.boolean(p.non_removable);
    h.u32(p.live_threads);
}

void pin_tf(StateHasher& h, const TfRec& t, const objects::ObjectStore& st) {
    h.u32(static_cast<uint32_t>(t.units.size()));
    for (const objects::ObjectHandle u : t.units) h.i32(st.id(u));
    h.u32(static_cast<uint32_t>(t.pending.size()));
    for (const TacticalReinforcement& r : t.pending) h.i32(r.key);
    h.u32(static_cast<uint32_t>(t.reservations.size()));
    if (!t.producing.empty()) {
        h.u32(static_cast<uint32_t>(t.producing.size()));
        for (const auto& [ty, b] : t.producing) {
            h.i32(ty);
            h.i32(b);
        }
    }
}

void pin_pp(StateHasher& h, const TacPpRec& pp) {
    h.i32(pp.player);
    h.u32(pp.plan);
    h.boolean(pp.reserved);
    if (pp.contrast_failed) h.u8(0xC7);
    if (!pp.lua_released.empty()) {
        h.u8(0xC8);
        h.u32(static_cast<uint32_t>(pp.lua_released.size()));
        for (const int32_t k : pp.lua_released) h.i32(k);
    }
    h.u32(static_cast<uint32_t>(pp.picks.size()));
    for (const TacticalPpPick& k : pp.picks) {
        h.i32(k.type);
        h.u8(static_cast<uint8_t>(k.source));
        h.i32(k.ref);
        h.i32(k.tf);
        h.i32(k.team);
    }
}

void pin_status(StateHasher& h, const StatusRec& s) {
    h.boolean(s.finished);
    h.u8(static_cast<uint8_t>(s.result));
}

void pin_attack_pos(StateHasher& h, const AttackPositioning& a) {
    h.boolean(a.flag);
    h.i32(a.next);
    h.boolean(a.moving);
    h.f32(a.pos.x);
    h.f32(a.pos.y);
    h.f32(a.pos.z);
}

template <class R, class Make, class Pin>
bool pinned(const char* name, Make make, Pin pin, const Store& s) {
    const R x = make(s);
    StateHasher h;
    pin(h, x);
    const bool ok = crc(x, s) == h.digest();
    if (!ok) std::printf("  CRC pin FAILED: %s\n", name);
    return ok;
}

void test_round_trip(const Store& s) {
    bool ok = round_trip<Slot>("Slot", sample_slot, s) && round_trip<PlanInstance>("PlanInstance", sample_plan, s) &&
              round_trip<TfRec>("TfRec", sample_tf, s) && round_trip<StatusRec>("StatusRec", sample_status, s) &&
              round_trip<TacPpRec>("TacPpRec", sample_pp, s) &&
              round_trip<AttackPositioning>("AttackPositioning", sample_attack_pos, s) &&
              round_trip<PendingBonus>("PendingBonus", sample_bonus, s) &&
              round_trip<TacticalPpResult>("TacticalPpResult", sample_pp_result, s) &&
              round_trip<TacticalAiObject>("TacticalAiObject", sample_fact, s);
    // An image without the skai t15 / b0416 fields (save versions 12 / 13): absent fields take their old defaults.
    {
        const TacPpRec x = sample_pp(s);
        ByteWriter w;
        w.i32(x.player).u32(x.plan).boolean(x.reserved).u32(1);
        w.i32(3).u8(static_cast<uint8_t>(PpSource::Build)).i32(900).i32(1).i32(2);  // a pick without its contrast entry
        ByteReader r(w.bytes());
        io::Features old;
        old.retest = false;
        old.pad_fact = false;
        io::SaveReader in(r, &s.store, old);
        TacPpRec y;
        visit(in, y);
        ok = ok && r.ok() && r.done() && y.picks.size() == 1 && y.picks[0].contrast_entry == -1 && y.picks[0].ref == 900;
        // A fact without the pad byte (the last one).
        std::vector<uint8_t> fact = save_bytes(sample_fact(s), s);
        fact.pop_back();
        ByteReader rf(fact);
        io::SaveReader inf(rf, &s.store, old);
        TacticalAiObject f;
        f.pad_occupied = true;
        visit(inf, f);
        ok = ok && rf.ok() && rf.done() && !f.pad_occupied && f.hero_clash;
    }
    CHECK(ok);
    std::printf("records: write -> read -> write round trip of 9 record kinds %s\n", ok ? "ok" : "FAILED");
}

void test_crc_pin(const Store& s) {
    const bool ok = pinned<Slot>("Slot", sample_slot, pin_target_slot, s) && pinned<PlanInstance>("PlanInstance", sample_plan, pin_plan, s) &&
                    pinned<TfRec>("TfRec", sample_tf, [&s](StateHasher& h, const TfRec& t) { pin_tf(h, t, s.store); }, s) &&
                    pinned<TfRec>("TfRec (no builds)", [](const Store& st) { TfRec t = sample_tf(st); t.producing.clear(); return t; },
                                  [&s](StateHasher& h, const TfRec& t) { pin_tf(h, t, s.store); }, s) &&
                    pinned<TacPpRec>("TacPpRec", sample_pp, pin_pp, s) &&
                    pinned<TacPpRec>("TacPpRec (no re-test / release)",
                                     [](const Store& st) {
                                         TacPpRec p = sample_pp(st);
                                         p.contrast_failed = false;
                                         p.lua_released.clear();
                                         return p;
                                     },
                                     pin_pp, s) &&
                    pinned<StatusRec>("StatusRec", sample_status, pin_status, s) &&
                    pinned<AttackPositioning>("AttackPositioning", sample_attack_pos, pin_attack_pos, s) &&
                    pinned<PendingBonus>("PendingBonus", sample_bonus, [](StateHasher&, const PendingBonus&) {}, s) &&
                    pinned<TacticalAiObject>("TacticalAiObject", sample_fact, [](StateHasher&, const TacticalAiObject&) {}, s);
    CHECK(ok);
    std::printf("records: CRC bytes of 10 record samples equal the pre-REF1 hash order %s\n", ok ? "ok" : "FAILED");
}

// ---------------------------------------------------------------- save pin (TacticalAiWorld::write before REF1)
// The save bytes of each record as the hand-written writer put them: a swap of two same-typed fields in a visit() list
// passes the round trip but loads existing saves wrongly, so the layout is pinned here (save version 18 / 19).

void put_target_old(ByteWriter& w, const TacticalTarget& t) {
    w.u8(static_cast<uint8_t>(t.kind)).i32(t.object_id).str(t.name);
    w.f32(t.position.x).f32(t.position.y).f32(t.position.z);
    w.f32(t.eval_rect.min_x).f32(t.eval_rect.min_y).f32(t.eval_rect.max_x).f32(t.eval_rect.max_y);
    w.i32(t.zone).i32(t.cell_x).i32(t.cell_y);
}

void put_pick_old(ByteWriter& w, const TacticalPpPick& k) {
    w.i32(k.type).u8(static_cast<uint8_t>(k.source)).i32(k.ref).i32(k.tf).i32(k.team).i32(k.contrast_entry);
}

void put_handle_old(ByteWriter& w, const objects::ObjectStore& st, objects::ObjectHandle h) {
    w.u64(h.value).i32(st.exists(h) ? st.id(h) : objects::kNoObjectId);
}

void put_ints_old(ByteWriter& w, const std::vector<int32_t>& v) {
    w.u32(static_cast<uint32_t>(v.size()));
    for (const int32_t x : v) w.i32(x);
}

void put_pairs_old(ByteWriter& w, const std::vector<std::pair<int32_t, int32_t>>& v) {
    w.u32(static_cast<uint32_t>(v.size()));
    for (const auto& [a, b] : v) w.i32(a).i32(b);
}

void save_slot_old(ByteWriter& w, const Slot& s, const Store&) {
    put_target_old(w, s.t);
    w.i32(s.handle).boolean(s.alive);
}

void save_plan_old(ByteWriter& w, const PlanInstance& p, const Store&) {
    w.i32(p.player).u32(p.plan).u32(p.goal_type).i32(p.slot).i32(p.target_owner).i32(p.pp);
    put_ints_old(w, p.tfs);
    w.boolean(p.result).boolean(p.non_removable).boolean(p.ended).u32(p.live_threads);
}

void save_tf_old(ByteWriter& w, const TfRec& t, const Store& s) {
    w.i32(t.plan).i32(t.player).u32(t.def).str(t.name).u8(static_cast<uint8_t>(t.kind));
    w.u32(static_cast<uint32_t>(t.units.size()));
    for (const objects::ObjectHandle h : t.units) put_handle_old(w, s.store, h);
    w.u32(static_cast<uint32_t>(t.pending.size()));
    for (const TacticalReinforcement& e : t.pending)
        w.i32(e.key).i32(e.type).f32(e.population).u32(e.units).i32(e.member_type).boolean(e.member_create_team).i32(e.count);
    put_pairs_old(w, t.reservations);
    put_pairs_old(w, t.producing);
    w.str(t.priorities);
}

void save_status_old(ByteWriter& w, const StatusRec& r, const Store& s) {
    w.i32(r.plan).i32(r.tf).u8(static_cast<uint8_t>(r.kind)).boolean(r.finished).u8(static_cast<uint8_t>(r.result));
    w.i64(r.started).i64(r.last_order);
    put_handle_old(w, s.store, r.unit);
    w.i32(r.target).f32(r.dest.x).f32(r.dest.y).f32(r.dest.z);
    w.i64(r.timeout).f32(r.ring).boolean(r.prevent);
    w.u8(static_cast<uint8_t>(r.direction)).f32(r.min_dist).f32(r.threat).u32(r.plan_frame).boolean(r.lists);
    put_ints_old(w, r.members);
    put_ints_old(w, r.moving);
    put_ints_old(w, r.pending);
    put_pairs_old(w, r.landings);
    w.boolean(r.saved_removable).i32(r.target_owner);
}

void save_pp_old(ByteWriter& w, const TacPpRec& p, const Store&) {
    w.i32(p.player).u32(p.plan).boolean(p.reserved).u32(static_cast<uint32_t>(p.picks.size()));
    for (const TacticalPpPick& k : p.picks) put_pick_old(w, k);
    w.boolean(p.layer_space).boolean(p.layer_ground).boolean(p.contrast_failed);
}

void save_attack_pos_old(ByteWriter& w, const AttackPositioning& a, const Store&) {
    w.boolean(a.flag).i32(a.next).boolean(a.moving).f32(a.pos.x).f32(a.pos.y).f32(a.pos.z);
}

void save_bonus_old(ByteWriter& w, const PendingBonus& b, const Store&) { w.u32(b.goal_type).i32(b.target).f32(b.bonus).i64(b.deadline); }

void save_pp_result_old(ByteWriter& w, const TacticalPpResult& o, const Store&) {
    w.boolean(o.ok).u32(static_cast<uint32_t>(o.candidates)).str(o.last_veto).u32(static_cast<uint32_t>(o.picks.size()));
    for (const TacticalPpPick& k : o.picks) put_pick_old(w, k);
    w.boolean(o.layer_space).boolean(o.layer_ground);
}

void save_fact_old(ByteWriter& w, const TacticalAiObject& f, const Store& s) {
    put_handle_old(w, s.store, f.handle);
    w.i32(f.target.id).boolean(f.target.dead_or_removing).boolean(f.target.rep_has_land_evaluator);
    w.boolean(f.target.rep_has_space_evaluator).boolean(f.target.own_has_space_evaluator);
    const UnitAppliesInfo& a = f.applies;
    w.i32(a.object_id).boolean(a.has_owner).boolean(a.owner_allied).boolean(a.has_locomotor).boolean(a.has_build_objects);
    w.boolean(a.has_cash_point).f32(a.reinforcement_enabling_radius).boolean(a.last_state_visible_under_fow);
    w.boolean(a.initial_state_visible_under_fow);
    for (const bool* b : free_store_flags(f.free_store)) w.boolean(*b);
    put_ints_old(w, f.team_members);
    w.boolean(f.moving).i32(f.attack_target).boolean(f.damage_tracking).i32(f.reprojection_stamp);
    w.boolean(f.valid_target).boolean(f.hero_clash).boolean(f.pad_occupied);
}

template <class R, class Make, class Old>
bool save_pinned(const char* name, Make make, Old old, const Store& s) {
    const R x = make(s);
    ByteWriter w;
    old(w, x, s);
    const bool ok = save_bytes(x, s) == w.bytes();
    if (!ok) std::printf("  save pin FAILED: %s\n", name);
    return ok;
}

void test_save_pin(const Store& s) {
    bool ok = true;
    ok = save_pinned<Slot>("Slot", sample_slot, save_slot_old, s) && ok;
    ok = save_pinned<PlanInstance>("PlanInstance", sample_plan, save_plan_old, s) && ok;
    ok = save_pinned<TfRec>("TfRec", sample_tf, save_tf_old, s) && ok;
    ok = save_pinned<StatusRec>("StatusRec", sample_status, save_status_old, s) && ok;
    ok = save_pinned<TacPpRec>("TacPpRec", sample_pp, save_pp_old, s) && ok;
    ok = save_pinned<AttackPositioning>("AttackPositioning", sample_attack_pos, save_attack_pos_old, s) && ok;
    ok = save_pinned<PendingBonus>("PendingBonus", sample_bonus, save_bonus_old, s) && ok;
    ok = save_pinned<TacticalPpResult>("TacticalPpResult", sample_pp_result, save_pp_result_old, s) && ok;
    ok = save_pinned<TacticalAiObject>("TacticalAiObject", sample_fact, save_fact_old, s) && ok;
    CHECK(ok);
    std::printf("records: save bytes of 9 record samples equal the pre-REF1 writer's layout %s\n", ok ? "ok" : "FAILED");
}

void test_coverage(const Store& s) {
    size_t fields = 0;
    bool ok = true;  // every record walked, so each failure is printed
    ok = coverage<Slot>("Slot", sample_slot, s, fields) && ok;
    ok = coverage<PlanInstance>("PlanInstance", sample_plan, s, fields) && ok;
    ok = coverage<TfRec>("TfRec", sample_tf, s, fields) && ok;
    ok = coverage<StatusRec>("StatusRec", sample_status, s, fields) && ok;
    ok = coverage<TacPpRec>("TacPpRec", sample_pp, s, fields) && ok;
    ok = coverage<AttackPositioning>("AttackPositioning", sample_attack_pos, s, fields) && ok;
    ok = coverage<PendingBonus>("PendingBonus", sample_bonus, s, fields) && ok;
    ok = coverage<TacticalPpResult>("TacticalPpResult", sample_pp_result, s, fields) && ok;
    ok = coverage<TacticalAiObject>("TacticalAiObject", sample_fact, s, fields) && ok;
    CHECK(ok && fields >= 100);
    std::printf("records: %zu fields each change the save (saved) / the CRC (hashed) and round-trip %s\n", fields, ok ? "ok" : "FAILED");
}

}  // namespace

int main() {
    const Store s;
    test_round_trip(s);
    test_crc_pin(s);
    test_save_pin(s);
    test_coverage(s);
    return ::alamo::test::report("alamo_ai_tactical_records_tests");
}
