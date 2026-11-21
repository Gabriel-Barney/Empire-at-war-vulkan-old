// Object contents lists (obj +0xF8 -> list) and in-battle tactical production (player +0x3A0 build manager) for the
// skirmish station upgrade, docs/re/STATION_HANGARS_SOUND.md §1.1 / §1.2 steps 5, 7, 8.
//
// ContentsSystem: per container, the contained objects in list order (DUMMY_UPGRADE objects such as the
// *_Starbase_Upgrade objects, Enhanced_Shielding / Improved_Weapons dummies, garrisoned units). The parent of an
// object (+0x2B0) is container_of(). move_all() is Do_Starbase_Upgrade step 5 (0x70eec0): snapshot S's list, then
// each entry in order leaves S and joins S' (no eject effects). [L] the two contents-pack bytes +0x30 / +0x31 the
// original copies are not modelled.
//
// TacticalProduction: the per-player tactical build queues (build manager player +0x3a0, SKIRMISH_SETUP §7). mcx4 t04
// (service 0x498170 [H]): two queues per player, picked by the type's Tactical_Production_Queue (+0xa30) minus 2 -
// Tactical_Units (2) and Tactical_Upgrades (3); the head of each queue counts down one frame per update() whatever its
// builder, and when it pops the next head starts at its full time. [port] One list per player in issue order, each
// order carrying its queue index (the head of a queue = its first order in the list). Build frames =
// int(seconds x FPS) with the seconds already multiplied by the host (Tactical_Build_Time_Seconds, the AI
// Space_Build_Time_Multiplier, build-time modifiers list 10). retarget() is §1.2 steps 7 / 8 (0x497460): orders keep
// their progress on the new builder.
//
// HOST WIRING (engine/apps/sim TacticalBattle), skirmish space:
//   * The player (command bar) or the AI (Produce_Force of *_Starbase_Upgrade) buys an upgrade at its station: the host
//     checks Tactical_Buildable_Objects_Multiplayer of the station type, prerequisites (the current station level),
//     Build_Limit_Current_For_All_Allies (count the player's and allies' existing objects plus
//     production.count_type(...) of allied players), charges Tactical_Build_Cost_Multiplayer, then
//     production.enqueue(player, station, type, seconds, frame).
//   * production.update(frame) once per frame (serial, before the object services); for each completed() order:
//       - builder dead / gone: drop it (refund is the host's policy);
//       - DUMMY_UPGRADE type: create the object for the order's player (no model, no combat unit), contents.add(builder,
//         obj), then abilities.add_container(obj, desc, player, {}, frame, &synced_rng): its Skirmish_Automatic
//         Starbase_Upgrade_Ability is scheduled at Get_Int(now, now + 2). Set AbilityHost::upgrade_container =
//         contents.container_of;
//       - other types: the normal tactical spawn at the builder.
//     then production.clear_completed().
//   * StationUpgrade request: ... complete_station_upgrade(S -> S'), contents.move_all(S, S'), selection / groups,
//     tech, production.retarget(S, S', owner) and for each ally, remove S silently (contents.remove_container(S)
//     returns nothing left), announcement; RemoveSilently: contents.remove(upgrade object), abilities.remove(it).
//   * Any object removal: contents.remove(id); a container's removal: contents.remove_container(id) (returns the
//     contained objects for the host's eject / kill policy) and production.builder_removed(id).
//
// DETERMINISM: lists sorted by container id / in insertion order; players in index order; no RNG. Hashed in
// "<prefix>.contents" / "<prefix>.production".
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "alamo/tactical/combat/combat_system.hpp"

namespace alamo {
class StateHasher;
class CrcRegistry;
class ByteWriter;
class ByteReader;
}  // namespace alamo

namespace alamo::tactical::hangar {

using combat::kNoObject;
using combat::ObjectId;

class ContentsSystem {
public:
    // Appends `object` to `container`'s list (0x379d30). False when it is already contained somewhere, or the ids are
    // equal / none.
    bool add(ObjectId container, ObjectId object);
    // Takes `object` out of its container (0x37a8d0). False when not contained.
    bool remove(ObjectId object);
    // The container is removed: its list is dropped; returns the objects it held (list order).
    std::vector<ObjectId> remove_container(ObjectId container);
    // §1.2 step 5: every object of `from` (list order) moves to the end of `to`'s list. Returns how many moved.
    uint32_t move_all(ObjectId from, ObjectId to);

    ObjectId container_of(ObjectId object) const;              // parent (+0x2B0), kNoObject when none
    std::span<const ObjectId> contents(ObjectId container) const;  // list order; empty when none

    void hash(StateHasher& h) const;
    void register_crc(CrcRegistry& registry, const std::string& prefix = "hangar") const;
    // N1 stage 2 (save_file.hpp contract, hangar_save.cpp): every list (ascending container id, objects in list order).
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);

private:
    struct List {
        ObjectId container = kNoObject;
        std::vector<ObjectId> objects;
    };
    int32_t find(ObjectId container) const;
    std::vector<List> lists_;  // sorted by container id
};

struct BuildOrder {
    uint32_t id = 0;                 // issue order (1, 2, ...)
    int32_t player = -1;
    ObjectId builder = kNoObject;    // +0x2C
    int32_t type = -1;               // host TypeIndex
    uint32_t total_frames = 0;
    uint32_t frames_left = 0;
    uint32_t issued = 0;
    uint8_t queue = 0;               // Tactical_Production_Queue - 2: 0 Tactical_Units, 1 Tactical_Upgrades (save v2)
};

class TacticalProduction {
public:
    explicit TacticalProduction(uint32_t fps = combat::kDefaultFps) : fps_(fps ? fps : combat::kDefaultFps) {}

    // Queues `type` at `builder` for `player` in queue `q` (0 / 1, see BuildOrder::queue); build frames =
    // int(seconds x FPS) (at least 1). Returns the order id.
    uint32_t enqueue(int32_t player, ObjectId builder, int32_t type, float seconds, uint32_t frame, uint8_t q = 0);
    bool cancel(uint32_t order);
    // §1.2 steps 7 / 8 (0x497460): `player`'s orders at `old_builder` now belong to `new_builder` (progress kept).
    // Returns how many changed.
    uint32_t retarget(int32_t player, ObjectId old_builder, ObjectId new_builder);
    // The builder is gone: its orders (every player) are dropped and returned (issue order) [port].
    std::vector<BuildOrder> builder_removed(ObjectId builder);

    // Once per frame: the head (first order) of each of a player's queues counts down; finished ones move to
    // completed() (players in index order, list order).
    void update(uint32_t frame);
    std::span<const BuildOrder> completed() const { return done_; }
    void clear_completed() { done_.clear(); }

    std::span<const BuildOrder> queue(int32_t player) const;  // list order
    // Orders of `type` queued or in progress for `player` (build limits).
    uint32_t count_type(int32_t player, int32_t type) const;
    // Orders queued or in progress in `player`'s queue `q` (the human Max_Build_Queue test).
    uint32_t count_queue(int32_t player, uint8_t q) const;
    // mcx4 t04: builds of `type` that `player` completed in this battle (completion 0x498f70 adds 1 to the player's lifetime
    // count, 0x2821b0, before the builder is looked at). The host adds the galactic count for the lifetime build limits.
    uint32_t built_count(int32_t player, int32_t type) const;

    void hash(StateHasher& h) const;
    void register_crc(CrcRegistry& registry, const std::string& prefix = "hangar") const;
    // N1 stage 2 (save_file.hpp contract, hangar_save.cpp): the order counter, every player's list (player order, list
    // order) and the completed orders not yet cleared. The fps is the constructor's (not saved).
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);

private:
    std::vector<BuildOrder>& list(int32_t player);
    void add_built(int32_t player, int32_t type);
    uint32_t fps_;
    uint32_t next_id_ = 1;
    std::vector<std::vector<BuildOrder>> players_;  // by player index
    std::vector<BuildOrder> done_;
    std::vector<std::vector<std::pair<int32_t, uint32_t>>> built_;  // per player: (type, completed builds), first-build order
};

}  // namespace alamo::tactical::hangar
