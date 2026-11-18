// mcx4 t05: CASH_POINT service (cash_point.hpp; MCX4_RE_ANSWERS_TACTICAL.md §5).
#include "alamo/tactical/garrison/cash_point.hpp"

#include <algorithm>

#include "alamo/core/bytes.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/objects/behaviors.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/tactical/save_io.hpp"

namespace alamo::tactical::garrison {

namespace {
constexpr uint32_t kCashSaveVersion = 1;
}  // namespace

bool cash_point_desc_from_type(const objects::GameObjectTypes& types, uint32_t type, CashPointDesc& out) {
    static const objects::BehaviorTypeId kCash = objects::behavior_type_id("CASH_POINT");
    const auto t = static_cast<objects::TypeIndex>(type);
    if (kCash == objects::kNoBehavior || t == objects::kNone || t >= types.size() ||
        !types.has_behavior(t, objects::BehaviorSet::Union, kCash))
        return false;
    CashPointDesc d;
    if (!types.get_string(t, "Cash_Point_Credits_Per_Second").empty()) d.credits_per_second = types.get_float(t, "Cash_Point_Credits_Per_Second");
    if (!types.get_string(t, "Cash_Point_Radius").empty()) d.radius = types.get_float(t, "Cash_Point_Radius");
    if (!types.get_string(t, "Cash_Point_Transition_Time_Seconds").empty())
        d.transition_seconds = types.get_float(t, "Cash_Point_Transition_Time_Seconds");
    out = d;
    return true;
}

int32_t CashPointSystem::index(ObjectId id) const {
    const auto it = std::lower_bound(points_.begin(), points_.end(), id, [](const Point& p, ObjectId x) { return p.id < x; });
    return (it == points_.end() || it->id != id) ? -1 : static_cast<int32_t>(it - points_.begin());
}

bool CashPointSystem::add_point(ObjectId id, int32_t owner, Vec3 position, const CashPointDesc& d) {
    const auto it = std::lower_bound(points_.begin(), points_.end(), id, [](const Point& p, ObjectId x) { return p.id < x; });
    if (id == kNoObject || (it != points_.end() && it->id == id)) return false;
    Point p;
    p.id = id;
    p.owner = owner;
    p.pos = position;
    p.d = d;
    points_.insert(it, p);
    return true;
}

void CashPointSystem::remove(ObjectId id) {
    if (const int32_t i = index(id); i >= 0) points_.erase(points_.begin() + i);
}

void CashPointSystem::set_owner(ObjectId id, int32_t owner) {
    if (const int32_t i = index(id); i >= 0) points_[static_cast<size_t>(i)].owner = owner;
}

void CashPointSystem::set_position(ObjectId id, Vec3 p) {
    if (const int32_t i = index(id); i >= 0) points_[static_cast<size_t>(i)].pos = p;
}

int32_t CashPointSystem::owner(ObjectId id) const {
    const int32_t i = index(id);
    return i < 0 ? -1 : points_[static_cast<size_t>(i)].owner;
}

int32_t CashPointSystem::capturer(ObjectId id) const {
    const int32_t i = index(id);
    return i < 0 ? -1 : points_[static_cast<size_t>(i)].capturer;
}

float CashPointSystem::progress(ObjectId id) const {
    const int32_t i = index(id);
    return i < 0 ? 0.0f : points_[static_cast<size_t>(i)].progress;
}

void CashPointSystem::service(std::span<const int32_t> players, std::span<const Influencer> influencers, uint32_t frames) {
    payouts_.clear();
    changes_.clear();
    const float fps = static_cast<float>(fps_);
    const float n = static_cast<float>(frames);
    for (Point& p : points_) {
        // 1. the controller scan (one counting player that is not the owner becomes the capturer; two: contested)
        const float r2 = p.d.radius * p.d.radius;
        int32_t found = -1;
        bool contested = false;
        for (const int32_t pl : players) {
            bool counts = false;
            for (const Influencer& f : influencers) {
                if (f.owner != pl || f.id == p.id) continue;
                const float dx = f.position.x - p.pos.x, dy = f.position.y - p.pos.y;
                if (dx * dx + dy * dy <= r2) {
                    counts = true;
                    break;
                }
            }
            if (!counts) continue;
            if (found != -1) {
                contested = true;
                break;
            }
            found = pl;
        }
        if (!contested && found != -1 && found != p.owner) p.capturer = found;
        // 2. progress toward the capturer
        if (p.capturer != -1 && p.d.transition_seconds != 0.0f) {
            const float step = 1.0f / p.d.transition_seconds / fps * n;
            if (p.capturer == p.owner) {
                const float v = step + p.progress;
                p.progress = v <= 1.0f ? v : 1.0f;
            } else {
                p.progress = p.progress - step;
                if (p.progress <= 0.0f) {
                    changes_.push_back({p.id, p.owner, p.capturer});
                    p.owner = p.capturer;
                    p.progress = 0.0f;
                }
            }
        }
        // 3. payout to a holding, non-neutral owner
        if (p.owner >= 0 && p.capturer == p.owner && p.progress >= 1.0f && !(neutral_faction_ && neutral_faction_(p.owner)))
            payouts_.push_back({p.id, p.owner, p.d.credits_per_second / fps * n});
    }
}

void CashPointSystem::hash(StateHasher& h) const {
    h.u32(static_cast<uint32_t>(points_.size()));
    for (const Point& p : points_) {
        h.i32(p.id);
        h.i32(p.owner);
        h.i32(p.capturer);
        h.f32(p.progress);
    }
}

void CashPointSystem::write(ByteWriter& w) const {
    w.u32(kCashSaveVersion);
    w.u32(static_cast<uint32_t>(points_.size()));
    for (const Point& p : points_) {
        w.i32(p.id).i32(p.owner);
        save_io::put_vec3(w, p.pos);
        w.f32(p.d.credits_per_second).f32(p.d.radius).f32(p.d.transition_seconds);
        w.i32(p.capturer).f32(p.progress);
    }
}

bool CashPointSystem::read(ByteReader& r) {
    if (r.u32() != kCashSaveVersion || !r.ok()) return false;
    uint32_t n = 0;
    if (!r.count(n, 40)) return false;
    std::vector<Point> points(n);
    for (size_t i = 0; i < points.size(); ++i) {
        Point& p = points[i];
        p.id = r.i32();
        p.owner = r.i32();
        p.pos = save_io::get_vec3<Vec3>(r);
        p.d.credits_per_second = r.f32();
        p.d.radius = r.f32();
        p.d.transition_seconds = r.f32();
        p.capturer = r.i32();
        p.progress = r.f32();
        if (!r.ok() || (i > 0 && p.id <= points[i - 1].id)) return false;
    }
    if (!r.ok() || !r.done()) return false;
    points_ = std::move(points);
    payouts_.clear();
    changes_.clear();
    return true;
}

}  // namespace alamo::tactical::garrison
