// N1 save / load helpers of the tactical AI (tactical_save.cpp, tactical_ai.cpp). Not installed. Byte rules of
// alamo/core/save_file.hpp: little-endian fields, counts before data, floats as bit patterns, no pointers / size_t.
#pragma once
#include <bit>
#include <concepts>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "ai_save_io.hpp"  // engine/ai/src, through alamo_ai_save_io
#include "alamo/ai/tactical/hint_zones.hpp"
#include "alamo/ai/tactical/tactical_pp.hpp"
#include "alamo/ai/tactical/target_list.hpp"
#include "alamo/core/bytes.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/objects/object_store.hpp"

namespace alamo::ai::tactical::save_io {

// Rejects the data (r.fail()) and returns false: semantic checks of the readers.
inline bool bad(ByteReader& r) {
    r.fail();
    return false;
}

// The u32 version that starts every record; false (r.fail()) on another value or truncation.
inline bool version(ByteReader& r, uint32_t want) {
    const uint32_t v = r.u32();
    return (r.ok() && v == want) || bad(r);
}

// The shared AI save codec (count, put_count, put / get, min_size, put_vec / get_vec): engine/ai/src/ai_save_io.hpp,
// exported to this library by the alamo_ai_save_io INTERFACE target. Same byte layout.
using ai::save::count;
using ai::save::get;
using ai::save::get_vec;
using ai::save::min_size;
using ai::save::put;
using ai::save::put_count;
using ai::save::put_vec;

inline void put_rect(ByteWriter& w, const Rect2& x) {
    w.f32(x.min_x);
    w.f32(x.min_y);
    w.f32(x.max_x);
    w.f32(x.max_y);
}

inline Rect2 get_rect(ByteReader& r) {
    Rect2 x;
    x.min_x = r.f32();
    x.min_y = r.f32();
    x.max_x = r.f32();
    x.max_y = r.f32();
    return x;
}

inline void put_vec3(ByteWriter& w, const Vec3f& v) {
    w.f32(v.x);
    w.f32(v.y);
    w.f32(v.z);
}

inline Vec3f get_vec3(ByteReader& r) {
    Vec3f v;
    v.x = r.f32();
    v.y = r.f32();
    v.z = r.f32();
    return v;
}

// An object handle: its raw value and the object's id (kNoObjectId when the handle was stale or null).
inline void put_handle(ByteWriter& w, const objects::ObjectStore& st, objects::ObjectHandle h) {
    w.u64(h.value);
    w.i32(st.exists(h) ? st.id(h) : objects::kNoObjectId);
}

// The handle of the saved id in the restored store; a handle that was stale (or null) when saved stays stale.
inline objects::ObjectHandle get_handle(ByteReader& r, const objects::ObjectStore& st) {
    const objects::ObjectHandle raw{r.u64()};
    const objects::ObjectId id = r.i32();
    if (!r.ok()) return {};
    if (id != objects::kNoObjectId) {
        const objects::ObjectHandle h = st.find(id);
        if (!h) r.fail();  // the restored store does not know the object
        return h;
    }
    return st.exists(raw) ? objects::ObjectHandle{} : raw;
}

// ---------------------------------------------------------------- one field list per record (REF1 t03)
// A record lists its fields once, in a visit(Ar&, R&) function (R = the record, const for the writer and the hasher).
// Three archives walk that list: SaveWriter (the N1 save bytes), SaveReader (decodes them) and RecordHasher (the
// per-tick CRC). Each field names the streams it belongs to: the CRC covers the fields the battle's outcome reads, so
// a field such as a display name or a cached position is saved but not hashed. Hash-only fields (In::Hash) are markers
// and blocks the save keeps elsewhere. The CRC byte order of every record is the one the goldens were written with.
enum class In : uint8_t { Both, Save, Hash };

// Save-format features an older image lacks (SaveReader); the writer and the hasher always have them.
struct Features {
    bool pad_fact = true;  // b0416: TacticalAiObject::pad_occupied (tactical AI save version 14 / 15)
    bool retest = true;    // skai t15: pick contrast entries, PP layer flags and re-test byte (version 16 / 17)
};
enum class Feature : uint8_t { PadFact, Retest };

template <class R, class T>
concept Of = std::same_as<std::remove_const_t<R>, T>;

class SaveWriter {
public:
    explicit SaveWriter(ByteWriter& w, const objects::ObjectStore* st = nullptr) : w_(w), st_(st) {}
    static bool since(Feature) { return true; }
    template <class T>
    void field(const T& v, In in = In::Both) {
        if (in != In::Hash) put<T>(w_, v);
    }
    template <class U, class T>
    void field_as(const T& v, In in = In::Both) {  // a wider in-memory value saved as U
        if (in != In::Hash) put<U>(w_, static_cast<U>(v));
    }
    template <class E>
    void enumeration(const E& e, E /*max*/, In in = In::Both) {
        if (in != In::Hash) w_.u8(static_cast<uint8_t>(e));
    }
    void str(const std::string& s, In in = In::Both) {
        if (in != In::Hash) w_.str(s);
    }
    void vec3(const Vec3f& v, In in = In::Both) {
        if (in != In::Hash) put_vec3(w_, v);
    }
    void rect(const Rect2& x, In in = In::Both) {
        if (in != In::Hash) put_rect(w_, x);
    }
    template <class T>
    void vec(const std::vector<T>& v, In in = In::Both) {
        if (in != In::Hash) put_vec(w_, v);
    }
    void handle(objects::ObjectHandle h, In in = In::Both) {
        if (in != In::Hash) put_handle(w_, *st_, h);
    }
    // A count, then each element through fn(archive, element); min_bytes is the reader's lower bound per element.
    template <class T, class F>
    void list(const std::vector<T>& v, In in, size_t /*min_bytes*/, F&& fn) {
        if (in == In::Hash) return;
        put_count(w_, v.size());
        for (const T& x : v) fn(*this, x);
    }
    void hash_tag(uint8_t) {}
    template <class T, class U>
    void absent(const T&, const U&) {}

private:
    ByteWriter& w_;
    const objects::ObjectStore* st_;
};

class SaveReader {
public:
    explicit SaveReader(ByteReader& r, const objects::ObjectStore* st = nullptr, Features f = {}) : r_(r), st_(st), f_(f) {}
    bool since(Feature x) const { return x == Feature::PadFact ? f_.pad_fact : f_.retest; }
    bool ok() const { return r_.ok(); }
    template <class T>
    void field(T& v, In in = In::Both) {
        if (in != In::Hash) v = get<T>(r_);
    }
    template <class U, class T>
    void field_as(T& v, In in = In::Both) {
        if (in != In::Hash) v = static_cast<T>(get<U>(r_));
    }
    template <class E>
    void enumeration(E& e, E max, In in = In::Both) {  // a value past `max` is invalid data
        if (in == In::Hash) return;
        const uint8_t b = r_.u8();
        if (b > static_cast<uint8_t>(max)) r_.fail();
        e = static_cast<E>(b);
    }
    void str(std::string& s, In in = In::Both) {
        if (in != In::Hash) s = r_.str();
    }
    void vec3(Vec3f& v, In in = In::Both) {
        if (in != In::Hash) v = get_vec3(r_);
    }
    void rect(Rect2& x, In in = In::Both) {
        if (in != In::Hash) x = get_rect(r_);
    }
    template <class T>
    void vec(std::vector<T>& v, In in = In::Both) {
        if (in != In::Hash) get_vec(r_, v);
    }
    void handle(objects::ObjectHandle& h, In in = In::Both) {
        if (in != In::Hash) h = get_handle(r_, *st_);
    }
    template <class T, class F>
    void list(std::vector<T>& v, In in, size_t min_bytes, F&& fn) {
        if (in == In::Hash) return;
        v.clear();
        v.resize(count(r_, min_bytes));
        for (T& x : v) {
            if (!r_.ok()) break;
            fn(*this, x);
        }
    }
    void hash_tag(uint8_t) {}
    template <class T, class U>
    void absent(T& x, const U& value) {  // a field this image's version does not have
        x = static_cast<T>(value);
    }

private:
    ByteReader& r_;
    const objects::ObjectStore* st_;
    Features f_;
};

class RecordHasher {
public:
    explicit RecordHasher(StateHasher& h, const objects::ObjectStore* st = nullptr) : h_(h), st_(st) {}
    static bool since(Feature) { return true; }
    template <class T>
    void field(const T& v, In in = In::Both) {
        if (in != In::Save) h_.value(v);
    }
    template <class U, class T>
    void field_as(const T& v, In in = In::Both) {
        if (in != In::Save) h_.value(static_cast<U>(v));
    }
    template <class E>
    void enumeration(const E& e, E /*max*/, In in = In::Both) {
        if (in != In::Save) h_.u8(static_cast<uint8_t>(e));
    }
    void str(const std::string& s, In in = In::Both) {
        if (in != In::Save) h_.str(s);
    }
    void vec3(const Vec3f& v, In in = In::Both) {
        if (in == In::Save) return;
        h_.f32(v.x);
        h_.f32(v.y);
        h_.f32(v.z);
    }
    void rect(const Rect2& x, In in = In::Both) {
        if (in == In::Save) return;
        h_.f32(x.min_x);
        h_.f32(x.min_y);
        h_.f32(x.max_x);
        h_.f32(x.max_y);
    }
    template <class T>
    void vec(const std::vector<T>& v, In in = In::Both) {
        if (in == In::Save) return;
        h_.u32(static_cast<uint32_t>(v.size()));
        for (const T& x : v) h_.value(x);
    }
    void handle(objects::ObjectHandle h, In in = In::Both) {  // the object's id
        if (in != In::Save) h_.i32(st_->id(h));
    }
    template <class T, class F>
    void list(const std::vector<T>& v, In in, size_t /*min_bytes*/, F&& fn) {
        if (in == In::Save) return;
        h_.u32(static_cast<uint32_t>(v.size()));
        for (const T& x : v) fn(*this, x);
    }
    void hash_tag(uint8_t t) { h_.u8(t); }
    template <class T, class U>
    void absent(const T&, const U&) {}

private:
    StateHasher& h_;
    const objects::ObjectStore* st_;
};

// The element visitor of list(): the element's own visit().
inline constexpr auto visit_each = [](auto& ar, auto& x) { visit(ar, x); };
// Pair elements (i32, i32), both fields in `in`.
constexpr auto pair_fields(In in) {
    return [in](auto& ar, auto& p) {
        ar.field(p.first, in);
        ar.field(p.second, in);
    };
}

// TacticalTarget (target lists, AI target slots): u8 kind, i32 object id, name, position, evaluator rect, zone, cell x / y.
// The CRC of an AI target slot reads kind, object id, zone and cell.
inline constexpr size_t kTargetBytes = 1 + 4 + 4 + 12 + 16 + 4 + 4 + 4;

template <class Ar, Of<TacticalTarget> R>
void visit(Ar& ar, R& t) {
    ar.enumeration(t.kind, TargetKind::Region);
    ar.field(t.object_id);
    ar.str(t.name, In::Save);
    ar.vec3(t.position, In::Save);
    ar.rect(t.eval_rect, In::Save);
    ar.field(t.zone);
    ar.field(t.cell_x);
    ar.field(t.cell_y);
}

inline void put_target(ByteWriter& w, const TacticalTarget& t) {
    SaveWriter a(w);
    visit(a, t);
}

inline bool get_target(ByteReader& r, TacticalTarget& t) {
    SaveReader a(r);
    visit(a, t);
    return r.ok();
}

// TacticalPpPick (PotentialPlan picks): i32 type, u8 source, i32 ref, i32 task force, i32 team, i32 contrast entry (skai
// t15, tactical AI save version 16 / 17; -1 in older images). The CRC reads all but the contrast entry.
inline constexpr size_t kPickBytes = 4 + 1 + 4 + 4 + 4;  // lower bound (skai t15 adds the contrast entry, 4)

template <class Ar, Of<TacticalPpPick> R>
void visit(Ar& ar, R& k) {
    ar.field(k.type);
    ar.enumeration(k.source, PpSource::Produce);
    ar.field(k.ref);
    ar.field(k.tf);
    ar.field(k.team);
    if (ar.since(Feature::Retest))
        ar.field(k.contrast_entry, In::Save);
    else
        ar.absent(k.contrast_entry, -1);
}

inline void put_pick(ByteWriter& w, const TacticalPpPick& k) {
    SaveWriter a(w);
    visit(a, k);
}

inline bool get_pick(ByteReader& r, TacticalPpPick& k, bool with_entry) {
    Features f;
    f.retest = with_entry;
    SaveReader a(r, nullptr, f);
    visit(a, k);
    return r.ok();
}

// Bit-exact float equality (geometry checks: a reader built from the same data has the same bits).
inline bool same_bits(float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }

}  // namespace alamo::ai::tactical::save_io
