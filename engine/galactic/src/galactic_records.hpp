// REF4 t02: one field list per galactic record (PlayerState, PlanetState, FleetState, ConflictInfo, ConflictLogEntry)
// for the N1 save writer (galactic_save.cpp), its reader and the per-tick CRC (GalacticSim::hash_state, campaign.cpp;
// ConflictManager::hash, conflicts.cpp), as the tactical AI records do (ref1 t03, tactical_save_io.hpp). Before, the
// save and the CRC listed the fields by hand in two places and drifted (3584b6ff, 32dccdff).
//
// A visit(Ar&, R&) walks the record's fields in the save's byte order; R is const for the writer and the hasher. Each
// field names the streams it is in (In): the CRC covers the fields the outcome reads, so a field such as a reveal delay
// or a display flag is saved and not hashed, and a few CRC-only markers (In::Hash) keep the CRC's historical layout.
// The save byte layout and the CRC byte order are those the saves and goldens were written with (galactic save test
// pins both, alamo_galactic_save_tests).
//
// Encodings: the writer and the reader use the save layout of galactic_save.cpp (object handles as the object's id, or
// kNoObjectId + the raw handle for a handle that names no object); the hasher hashes an object handle as its id
// (kNoObjectId when none) and a fleet reference as the fleet object's id. The reader checks nothing beyond the byte
// counts: the semantic checks stay in GalacticSim::read / ConflictManager::read after the visit.
// Included by galactic_save.cpp, campaign.cpp and conflicts.cpp only (not installed).
#pragma once
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "alamo/core/bytes.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/galactic/campaign.hpp"
#include "alamo/galactic/conflicts.hpp"
#include "alamo/objects/object_store.hpp"

namespace alamo::galactic::records {

enum class In : uint8_t { Both, Save, Hash };

// ---- object handles in the save (galactic_save.cpp layout) ----
inline void put_handle(ByteWriter& w, const objects::ObjectStore& s, ObjectHandle h) {
    if (s.exists(h)) {
        w.i32(s.id(h));
        return;
    }
    w.i32(objects::kNoObjectId);
    w.u64(h.value);
}

inline bool get_handle(ByteReader& r, const objects::ObjectStore& s, ObjectHandle& out) {
    const int32_t id = r.i32();
    if (!r.ok()) return false;
    if (id != objects::kNoObjectId) {
        out = s.find(id);
        return static_cast<bool>(out);  // the save names an object the restored store does not have
    }
    const ObjectHandle raw{r.u64()};
    if (!r.ok()) return false;
    out = (raw.slot() < s.capacity() && !s.exists(raw)) ? raw : ObjectHandle{};
    return true;
}

template <class R, class T>
concept Of = std::same_as<std::remove_const_t<R>, T>;

class Writer {
public:
    static constexpr bool kHashing = false;
    Writer(ByteWriter& w, const objects::ObjectStore& st) : w_(w), st_(st) {}
    void b(bool v, In in = In::Both) {
        if (in != In::Hash) w_.boolean(v);
    }
    void u8(uint8_t v, In in = In::Both) {
        if (in != In::Hash) w_.u8(v);
    }
    void i8(int8_t v, In in = In::Both) {
        if (in != In::Hash) w_.u8(static_cast<uint8_t>(v));
    }
    void u32(uint32_t v, In in = In::Both) {
        if (in != In::Hash) w_.u32(v);
    }
    void i32(int32_t v, In in = In::Both) {
        if (in != In::Hash) w_.i32(v);
    }
    void u64(uint64_t v, In in = In::Both) {
        if (in != In::Hash) w_.u64(v);
    }
    void f32(float v, In in = In::Both) {
        if (in != In::Hash) w_.f32(v);
    }
    void str(const std::string& v, In in = In::Both) {
        if (in != In::Hash) w_.str(v);
    }
    void vec3(const Vec3& v, In in = In::Both) {
        if (in != In::Hash) w_.f32(v.x).f32(v.y).f32(v.z);
    }
    void handle(ObjectHandle h, In in = In::Both) {
        if (in != In::Hash) put_handle(w_, st_, h);
    }
    template <class E>
        requires std::is_enum_v<E>
    void enumeration(E e, In in = In::Both) {  // one byte
        u8(static_cast<uint8_t>(e), in);
    }
    // A value the reader checks against the rebuilt session instead of restoring (is_planet, type).
    void check_b(bool v, In in = In::Both) { b(v, in); }
    void check_u32(uint32_t v, In in = In::Both) { u32(v, in); }
    // A count, then each element through fn(archive, element); min_bytes is the reader's lower bound per element.
    template <class T, class F>
    void list(const std::vector<T>& v, In in, size_t /*min_bytes*/, F&& fn) {
        if (in == In::Hash) return;
        w_.u32(static_cast<uint32_t>(v.size()));
        for (const T& x : v) fn(*this, x);
    }
    void handles(const std::vector<ObjectHandle>& v, In in = In::Both) {
        list(v, in, 4, [](Writer& a, ObjectHandle h) { a.handle(h); });
    }
    void u32s(const std::vector<uint32_t>& v, In in = In::Both) {
        list(v, in, 4, [](Writer& a, uint32_t x) { a.u32(x); });
    }
    // A fleet's members and their carrier flags (one u8 per member, 0 past the end of `carried`).
    void contents(const std::vector<ObjectHandle>& c, const std::vector<uint8_t>& carried) {
        w_.u32(static_cast<uint32_t>(c.size()));
        for (size_t i = 0; i < c.size(); ++i) {
            put_handle(w_, st_, c[i]);
            w_.u8(i < carried.size() ? carried[i] : uint8_t{0});
        }
    }
    // Fleet indices (a planet's orbit list): saved as indices, hashed as the fleets' object ids.
    void fleet_refs(const std::vector<uint32_t>& v) { u32s(v); }
    void hash_f32(float) {}
    void hash_tag(uint8_t) {}

private:
    ByteWriter& w_;
    const objects::ObjectStore& st_;
};

class Reader {
public:
    static constexpr bool kHashing = false;
    Reader(ByteReader& r, const objects::ObjectStore& st) : r_(r), st_(st) {}
    bool ok() const { return r_.ok(); }
    void b(bool& v, In in = In::Both) {
        if (in != In::Hash) v = r_.boolean();
    }
    void u8(uint8_t& v, In in = In::Both) {
        if (in != In::Hash) v = r_.u8();
    }
    void i8(int8_t& v, In in = In::Both) {
        if (in != In::Hash) v = static_cast<int8_t>(r_.u8());
    }
    void u32(uint32_t& v, In in = In::Both) {
        if (in != In::Hash) v = r_.u32();
    }
    void i32(int32_t& v, In in = In::Both) {
        if (in != In::Hash) v = r_.i32();
    }
    void u64(uint64_t& v, In in = In::Both) {
        if (in != In::Hash) v = r_.u64();
    }
    void f32(float& v, In in = In::Both) {
        if (in != In::Hash) v = r_.f32();
    }
    void str(std::string& v, In in = In::Both) {
        if (in != In::Hash) v = r_.str();
    }
    void vec3(Vec3& v, In in = In::Both) {
        if (in == In::Hash) return;
        v.x = r_.f32();
        v.y = r_.f32();
        v.z = r_.f32();
    }
    void handle(ObjectHandle& h, In in = In::Both) {
        if (in != In::Hash && r_.ok() && !get_handle(r_, st_, h)) r_.fail();
    }
    template <class E>
        requires std::is_enum_v<E>
    void enumeration(E& e, In in = In::Both) {  // the caller checks the value's range
        if (in != In::Hash) e = static_cast<E>(r_.u8());
    }
    void check_b(const bool& v, In in = In::Both) {
        if (in == In::Hash) return;
        if (r_.boolean() != v) r_.fail();  // not the session's galaxy
    }
    void check_u32(const uint32_t& v, In in = In::Both) {
        if (in == In::Hash) return;
        if (r_.u32() != v) r_.fail();
    }
    template <class T, class F>
    void list(std::vector<T>& v, In in, size_t min_bytes, F&& fn) {
        if (in == In::Hash) return;
        uint32_t n = 0;
        if (!r_.count(n, min_bytes)) {
            r_.fail();
            return;
        }
        v.assign(n, T{});
        for (T& x : v) {
            if (!r_.ok()) break;
            fn(*this, x);
        }
    }
    void handles(std::vector<ObjectHandle>& v, In in = In::Both) {
        list(v, in, 4, [](Reader& a, ObjectHandle& h) { a.handle(h); });
    }
    void u32s(std::vector<uint32_t>& v, In in = In::Both) {
        list(v, in, 4, [](Reader& a, uint32_t& x) { a.u32(x); });
    }
    void contents(std::vector<ObjectHandle>& c, std::vector<uint8_t>& carried) {
        uint32_t n = 0;
        if (!r_.count(n, 5)) {
            r_.fail();
            return;
        }
        c.assign(n, ObjectHandle{});
        carried.assign(n, 0);
        for (uint32_t i = 0; i < n && r_.ok(); ++i) {
            handle(c[i]);
            carried[i] = r_.u8();
        }
    }
    void fleet_refs(std::vector<uint32_t>& v) { u32s(v); }  // checked once the fleets are read
    void hash_f32(float) {}
    void hash_tag(uint8_t) {}

private:
    ByteReader& r_;
    const objects::ObjectStore& st_;
};

class Hasher {
public:
    static constexpr bool kHashing = true;
    // `fleets`: the fleets a planet's orbit list indexes (planet records only).
    Hasher(StateHasher& h, const objects::ObjectStore& st, const std::vector<FleetState>* fleets = nullptr)
        : h_(h), st_(st), fleets_(fleets) {}
    void b(bool v, In in = In::Both) {
        if (in != In::Save) h_.boolean(v);
    }
    void u8(uint8_t v, In in = In::Both) {
        if (in != In::Save) h_.u8(v);
    }
    void i8(int8_t v, In in = In::Both) {
        if (in != In::Save) h_.value(v);
    }
    void u32(uint32_t v, In in = In::Both) {
        if (in != In::Save) h_.u32(v);
    }
    void i32(int32_t v, In in = In::Both) {
        if (in != In::Save) h_.i32(v);
    }
    void u64(uint64_t v, In in = In::Both) {
        if (in != In::Save) h_.u64(v);
    }
    void f32(float v, In in = In::Both) {
        if (in != In::Save) h_.f32(v);
    }
    void str(const std::string& v, In in = In::Both) {
        if (in != In::Save) h_.str(v);
    }
    void vec3(const Vec3& v, In in = In::Both) {
        if (in == In::Save) return;
        h_.f32(v.x);
        h_.f32(v.y);
        h_.f32(v.z);
    }
    void handle(ObjectHandle h, In in = In::Both) {
        if (in != In::Save) h_.i32(id_of(h));
    }
    template <class E>
        requires std::is_enum_v<E>
    void enumeration(E e, In in = In::Both) {
        u8(static_cast<uint8_t>(e), in);
    }
    void check_b(bool v, In in = In::Both) { b(v, in); }
    void check_u32(uint32_t v, In in = In::Both) { u32(v, in); }
    template <class T, class F>
    void list(const std::vector<T>& v, In in, size_t /*min_bytes*/, F&& fn) {
        if (in == In::Save) return;
        h_.u32(static_cast<uint32_t>(v.size()));
        for (const T& x : v) fn(*this, x);
    }
    void handles(const std::vector<ObjectHandle>& v, In in = In::Both) {
        list(v, in, 4, [](Hasher& a, ObjectHandle h) { a.handle(h); });
    }
    void u32s(const std::vector<uint32_t>& v, In in = In::Both) {
        list(v, in, 4, [](Hasher& a, uint32_t x) { a.u32(x); });
    }
    void contents(const std::vector<ObjectHandle>& c, const std::vector<uint8_t>& carried) {
        h_.u32(static_cast<uint32_t>(c.size()));
        for (size_t i = 0; i < c.size(); ++i) {
            h_.i32(id_of(c[i]));
            h_.u8(i < carried.size() ? carried[i] : uint8_t{0});
        }
    }
    void fleet_refs(const std::vector<uint32_t>& v) {
        h_.u32(static_cast<uint32_t>(v.size()));
        for (const uint32_t f : v) h_.i32(id_of((*fleets_)[f].object));
    }
    void hash_f32(float v) { h_.f32(v); }
    void hash_tag(uint8_t t) { h_.u8(t); }

private:
    int32_t id_of(ObjectHandle o) const { return st_.exists(o) ? st_.id(o) : objects::kNoObjectId; }
    StateHasher& h_;
    const objects::ObjectStore& st_;
    const std::vector<FleetState>* fleets_;
};

// ---- PlayerState: the CRC reads the identity, the account and the tech fields; the purchase / lock lists, the
// tutorial flags, the corruption counts, the max tech level and the build counts are saved only (hash_oi /
// hash_diplomacy hash some of them in their own sections, once touched). GalacticSim::write adds last_income after it.
template <class Ar, Of<PlayerState> R>
void visit(Ar& ar, R& p) {
    ar.u32(p.faction);
    ar.b(p.human);
    ar.b(p.ai);
    ar.u32(p.difficulty);
    ar.f32(p.account.credits);
    ar.f32(p.account.cap);
    ar.i32(p.tech_level);
    ar.f32(p.tech_cost_mult);
    ar.b(p.cheat);
    ar.u32s(p.bm_purchased, In::Save);
    ar.b(p.bm_tutorial, In::Save);
    ar.b(p.sabotage_tutorial, In::Save);
    for (auto& c : p.corruptions_completed) ar.i32(c, In::Save);
    ar.u32s(p.locked_types, In::Save);
    ar.u32s(p.unlocked_types, In::Save);
    ar.u32s(p.production_disabled, In::Save);
    ar.i32(p.max_tech_level, In::Save);
    ar.list(p.built, In::Save, 8, [](auto& a, auto& e) {
        a.u32(e.first);
        a.i32(e.second);
    });
}

// ---- PlanetState (row order). The CRC stops after is_planet on a scenery row; it skips the corruption transition
// details, the reveal state, the visibility lists (hash_visibility hashes them in their own section), the ability-less
// income entries' ability fields, the influence ids, the sabotage count, the respawn list, the persisted built objects
// (CampaignBattles::hash_state hashes those) and the queue phases. The definition-derived position and route links are
// not saved (start() rebuilds them); the galab / placement fields are saved later in GalacticSim::write (versions 8, 10).
template <class Ar, Of<PlanetState> R>
void visit(Ar& ar, R& p) {
    ar.check_b(p.is_planet);
    if constexpr (Ar::kHashing) {
        if (!p.is_planet) return;
    }
    ar.check_u32(p.type, In::Save);
    ar.handle(p.object);
    ar.b(p.initialised);
    ar.i32(p.owner);
    ar.f32(p.control);
    ar.f32(p.target_control);
    ar.i32(p.leaning);
    ar.i32(p.base_value);
    ar.f32(p.black_market);
    ar.b(p.black_market_influences);
    ar.b(p.destroyed);
    ar.i8(p.corruption);
    ar.i32(p.corrupting_player);
    ar.b(p.corruption_protected);
    ar.b(p.corruption_removing, In::Save);
    ar.u32(p.corruption_start, In::Save);
    ar.u32(p.corruption_end, In::Save);
    ar.u32(p.corruption_changed, In::Save);
    ar.i8(p.corruption_pending, In::Save);
    ar.i32(p.corruption_pending_player, In::Save);
    ar.b(p.corruption_particles, In::Save);
    for (auto& v : p.corruption_pairs) ar.f32(v, In::Save);
    ar.b(p.revealed, In::Save);
    ar.i32(p.reveal_delay, In::Save);
    ar.list(p.visibility, In::Save, 4, [](auto& a, auto& l) {
        a.list(l, In::Save, 28, [](auto& b, auto& m) {
            b.u32(m.value);
            b.u32(m.expiry);
            b.u32(m.name_hash);
            b.i32(m.key.object);
            b.u32(m.key.serial);
            b.u32(m.key.sub);
            b.u32(m.key.extra);
        });
    });
    ar.f32(p.piracy_pool);
    ar.i32(p.siphon_player);
    ar.f32(p.siphon_fraction);
    ar.i32(p.siphon_until);
    ar.f32(p.siphon_amount);
    ar.i32(p.halt_until);
    ar.b(p.min_income_set);
    ar.b(p.max_income_set);
    ar.f32(p.min_income);
    ar.f32(p.max_income);
    // The two former build bonus floats (always 0, now derived: GalacticSim::production_bonus): in the CRC as 0, so its
    // stream keeps its layout; no longer saved (version 4).
    ar.hash_f32(0.0f);
    ar.hash_f32(0.0f);
    ar.b(p.hidden);
    ar.b(p.in_battle);
    ar.list(p.income, In::Both, 29, [](auto& a, auto& e) {
        a.u8(e.kind);
        a.f32(e.value);
        a.i32(e.expiry);
        const In ability = e.ability != 0 ? In::Both : In::Save;  // P4 sim: the CRC reads ability entries' fields only
        a.u32(e.ability, ability);
        a.i32(e.source, ability);
        a.u32(e.tag, ability);
        a.i32(e.next_eval, ability);
        a.u32(e.def, In::Save);  // derived from `ability`
    });
    for (auto& q : p.queue)
        ar.list(q, In::Both, 28, [](auto& a, auto& e) {
            a.u32(e.type);
            a.f32(e.build_time);
            a.i32(e.start);
            a.i32(e.duration);
            a.f32(e.remaining);
            a.i32(e.player);
            a.f32(e.cost);
        });
    ar.i32(p.starbase_level);
    ar.i32(p.starbase_owner);
    ar.handle(p.starbase);
    ar.handles(p.structures);
    ar.handles(p.surface);
    ar.list(p.influence, In::Save, 4, [](auto& a, auto& id) { a.i32(id); });
    ar.fleet_refs(p.orbit);
    ar.i32(p.sabotage_count, In::Save);
    ar.list(p.respawns, In::Save, 13, [](auto& a, auto& e) {
        a.i32(e.due);
        a.u32(e.type);
        a.i32(e.owner);
        a.u8(e.flag);
    });
    ar.list(p.built_objects, In::Save, 24, [](auto& a, auto& e) {
        a.u32(e.type);
        a.f32(e.x);
        a.f32(e.y);
        a.f32(e.z);
        a.i32(e.base_level);
        a.i32(e.owner);
    });
    for (auto& phase : p.queue_phase) ar.i32(phase, In::Save);
}

// ---- FleetState (every fleet in the save, dead ones too: indices are stable references; only live fleets in the CRC,
// which reads no `alive` byte). The slot choice is saved only (hash_slots hashes the slots in their own section); the
// sticky story auto-resolve lock is saved in its own list (version 11) and adds a CRC marker once set (mcx4 t20).
template <class Ar, Of<FleetState> R>
void visit(Ar& ar, R& f) {
    ar.b(f.alive, In::Save);
    ar.handle(f.object);
    ar.i32(f.owner);
    ar.contents(f.contents, f.carried);
    ar.u32(f.planet);
    ar.u32(f.from);
    ar.u32(f.to);
    ar.u32s(f.path);
    ar.u32(f.path_index);
    ar.f32(f.base_speed);
    ar.f32(f.hop_speed);
    ar.vec3(f.position);
    ar.vec3(f.facing);
    ar.b(f.moving);
    ar.i8(f.slot, In::Save);
    ar.u32(f.want_planet, In::Save);
    ar.i8(f.want_slot, In::Save);
    if (f.story_autoresolve_lock) ar.hash_tag(0x95);
}

// ---- ConflictInfo (both queues, the active request's info): every field in the save and the CRC.
inline constexpr size_t kConflictInfoBytes = 1 + 4 + 4 + 4 + 4 + 4;  // lower bound: no units, empty map
template <class Ar, Of<ConflictInfo> R>
void visit(Ar& ar, R& c) {
    ar.enumeration(c.kind);
    ar.u32(c.planet);
    ar.i32(c.attacker);
    ar.i32(c.defender);
    ar.handles(c.units);
    ar.str(c.map);
}

// ---- ConflictLogEntry (log order): the CRC reads the tick, planet, outcome and losses; the galactic frame, kind, sides,
// random draw count and map are saved only.
inline constexpr size_t kConflictLogBytes = 54;  // lower bound: empty map
template <class Ar, Of<ConflictLogEntry> R>
void visit(Ar& ar, R& e) {
    ar.u64(e.tick);
    ar.i32(e.galactic_frame, In::Save);
    ar.enumeration(e.kind, In::Save);
    ar.u32(e.planet);
    ar.i32(e.attacker, In::Save);
    ar.i32(e.defender, In::Save);
    ar.u8(e.how);
    ar.i32(e.winner);
    ar.i32(e.loser);
    ar.i32(e.retreating);
    ar.u32(e.attacker_losses);
    ar.u32(e.defender_losses);
    ar.u32(e.rng_draws, In::Save);
    ar.str(e.map, In::Save);
}

}  // namespace alamo::galactic::records
