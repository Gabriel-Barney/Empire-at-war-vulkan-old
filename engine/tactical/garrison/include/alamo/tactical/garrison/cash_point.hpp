// mcx4 t05 - cash points: CASH_POINT objects (MARKERS.XML Cash_Point_Small / _Medium / _Large) pay credits to the player
// that holds them in a land battle. Spec: docs/re/MCX4_RE_ANSWERS_TACTICAL.md §5 (CASH_POINT service 0x621370) [H unless
// tagged]. Per point, every service, with n = the frames since the last service and FPS = 30:
//   1. Controller scan: players in list order; a player counts when one of its influencers (host-filtered: a battle
//      object of that player, alive, not garrisoned, type Influences_Cash_Point) lies within the XY Cash_Point_Radius of
//      the point. A second counting player makes the point contested: the scan stops and nothing changes in this step.
//      Exactly one counting player that is not the point's owner becomes the point's capturer. One counting player that
//      is the owner, or none, leaves the capturer as it was.
//   2. With a capturer and T = Cash_Point_Transition_Time_Seconds != 0, s = (1 / T) / FPS x n:
//        capturer == owner: progress := min(progress + s, 1);
//        else progress -= s, and at <= 0 the owner becomes the capturer (Change_Owner) and progress := 0.
//   3. Payout: when the owner is the capturer, its faction is not Is_Neutral (no faction pays) and progress >= 1, the
//      owner gets Cash_Point_Credits_Per_Second / FPS x n credits.
// The capturer starts at -1 [M: the behaviour pack's constructor value]. Determinism: serial, points in ascending id,
// influencers in the host's order, single-precision steps in a fixed order.
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "alamo/core/galaxy.hpp"
#include "alamo/core/sim_crc_fwd.hpp"
#include "alamo/tactical/garrison/capture.hpp"

namespace alamo::objects {
class GameObjectTypes;
}

namespace alamo::tactical::garrison {

struct CashPointDesc {
    float credits_per_second = 0.0f;  // Cash_Point_Credits_Per_Second (type +0x1fb0, float; ctor 0)
    float radius = 200.0f;            // Cash_Point_Radius (+0x1fb4, float; ctor 200), XY
    float transition_seconds = 2.0f;  // Cash_Point_Transition_Time_Seconds (+0x1fb8; ctor 2)
};

// A CASH_POINT behaviour type: its descriptor. False for other types.
bool cash_point_desc_from_type(const objects::GameObjectTypes& types, uint32_t type, CashPointDesc& out);

struct CashPayout {
    ObjectId point = kNoObject;
    int32_t player = -1;
    float credits = 0.0f;
};

struct CashOwnerChange {
    ObjectId point = kNoObject;
    int32_t old_owner = -1, new_owner = -1;
};

class CashPointSystem {
public:
    explicit CashPointSystem(uint32_t fps = 30) : fps_(fps ? fps : 30) {}
    // Whether a player's faction is Is_Neutral (no payout). Unset: no player is.
    void set_neutral_faction(std::function<bool(int32_t player)> fn) { neutral_faction_ = std::move(fn); }

    bool add_point(ObjectId id, int32_t owner, Vec3 position, const CashPointDesc& d);
    void remove(ObjectId id);
    void set_owner(ObjectId id, int32_t owner);  // external owner changes (capturer and progress kept [M])
    void set_position(ObjectId id, Vec3 p);
    bool is_point(ObjectId id) const { return index(id) >= 0; }
    int32_t owner(ObjectId id) const;
    int32_t capturer(ObjectId id) const;
    float progress(ObjectId id) const;
    size_t point_count() const { return points_.size(); }

    // One service of every point; `players` = the player ids in list order. Owner changes and payouts of this service
    // are in changes() / payouts() (point order) until the next service.
    void service(std::span<const int32_t> players, std::span<const Influencer> influencers, uint32_t frames = 1);
    std::span<const CashPayout> payouts() const { return payouts_; }
    std::span<const CashOwnerChange> changes() const { return changes_; }

    void hash(StateHasher& h) const;
    void write(ByteWriter& w) const;  // points (owner, position, descriptor, capturer, progress)
    bool read(ByteReader& r);

private:
    struct Point {
        ObjectId id = kNoObject;
        int32_t owner = -1;
        Vec3 pos{};
        CashPointDesc d;
        int32_t capturer = -1;
        float progress = 0.0f;
    };
    int32_t index(ObjectId id) const;
    uint32_t fps_;
    std::function<bool(int32_t)> neutral_faction_;
    std::vector<Point> points_;  // ascending id
    std::vector<CashPayout> payouts_;
    std::vector<CashOwnerChange> changes_;
};

}  // namespace alamo::tactical::garrison
