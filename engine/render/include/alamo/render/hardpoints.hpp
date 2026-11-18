// alamo_render - hardpoint visuals (docs/re/SPACE_AND_OPEN.md B.9 / B.10): the <HardPoint> records of
// HardPointDataFiles.xml, the bone map of a model, the per (type, model) hardpoint layout (sub-objects, setup
// mask, destroy effects, attachments) and the per-instance state (HardpointVisibility).
//
// Sub-objects (B.10 answer 3): the owner model's meshes [0, M), its particle proxies [M, M + P) and one slot
// per hardpoint for its attached Model_To_Attach [M + P, M + P + H). One hidden bit each (1 = hidden).
// Instance::mesh_hidden takes the whole mask (the renderer reads only the mesh prefix). Proxies are not drawn
// (no particle system yet); their bits are kept so the rule is complete.
//
// Rule (B.10): type setup hides each hardpoint's Damage_Decal (first sub-object with that name) and the
// STEALTH / buildup / charged meshes over the ALO hidden flag; instance creation hides each hardpoint's
// Damage_Particles bone subtree (HardPoints order). Destruction, in order: Damage_Decal shown, Damage_Particles
// subtree shown, Engine_Particles subtree hidden when Engine_Death_Hide_Engine_Particles (default true).
// Subtree = the sub-object's bone is the target bone or a descendant, walking parents up to but not including
// bone 0 (bone 0 never matches). Writes overwrite the bit (an ALO-hidden mesh can be shown); last write wins.
// Not reversible in the game; DUMMY_ART and Is_Destroyable = No hardpoints never change visuals.
// Model_To_Attach removal (B.10 answer 4, step (f) 0x3a5700, before the visibility changes; r1b t03): a destroyed
// hardpoint with a Model_To_Attach erases the FIRST remaining sub-object named like that file's stem (upper-cased,
// cut at ".ALO"; case-insensitive) in sub-object order - meshes, proxies, then the attachment slots in HardPoints
// order. Usually its own slot; with several hardpoints attaching the same model, the first copy still present.
// An erased sub-object stays hidden whatever later effects show.
//
// Threading: HardpointVisibility is written on the frame thread only, between Renderer builds. Its mask()
// storage never moves after init(), so an Instance::mesh_hidden span taken from it stays valid; the
// parallel records step only reads it.
#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "alamo/assets/model.hpp"

namespace alamo::data {
class XmlNode;
}

namespace alamo::render {

// One <HardPoint Name="..."> record. The fields the renderer uses are copied out; every child element is kept
// as (tag, trimmed text) in file order (repeated tags kept) so other tags can be read without re-plumbing.
struct HardpointDef {
    std::string name;              // Name attribute (as written)
    bool known = false;            // found in the hardpoint XML (false: only the type's <HardPoints> name)
    std::string type;              // <Type> (HARD_POINT_*)
    std::string attachment_bone;   // <Attachment_Bone> (owner bone; Model_To_Attach goes here)
    std::string damage_decal;      // <Damage_Decal> (sub-object / mesh name; "" for <Damage_Decal />)
    std::string damage_particles;  // <Damage_Particles> (owner bone: subtree hidden at creation, shown on death)
    std::string engine_particles;  // <Engine_Particles> (owner bone: subtree hidden on death when the flag is set)
    std::string model_to_attach;   // <Model_To_Attach> (.alo attached at Attachment_Bone)
    bool hide_engine_particles = true;  // <Engine_Death_Hide_Engine_Particles> (default true; no vanilla XML sets it)
    bool destroyable = true;            // <Is_Destroyable> (default true)
    std::vector<std::pair<std::string, std::string>> tags;
    // First tag with this name (case-insensitive), trimmed text; "" when absent.
    std::string_view tag(std::string_view tag_name) const;
    bool has_tag(std::string_view tag_name) const;
    bool dummy_art() const;  // Type HARD_POINT_DUMMY_ART: destruction returns at once (B.10)
};
HardpointDef parse_hardpoint(const data::XmlNode& hardpoint);

// Per mesh its bone (MeshInfo::attach_bone, -1 when out of the skeleton), per bone its parent (-1 = root).
class MeshBoneMap {
public:
    MeshBoneMap() = default;
    explicit MeshBoneMap(const assets::ModelAsset& model);
    size_t mesh_count() const { return mesh_bone_.size(); }
    size_t bone_count() const { return parent_.size(); }
    int32_t bone_of(uint32_t mesh) const { return mesh < mesh_bone_.size() ? mesh_bone_[mesh] : -1; }
    int32_t parent_of(int32_t bone) const {
        return bone >= 0 && static_cast<size_t>(bone) < parent_.size() ? parent_[static_cast<size_t>(bone)] : -1;
    }
    // B.10 subtree test: bone == root or root is an ancestor, walking up to but not including bone 0; false
    // for root <= 0, bone <= 0 or out-of-range indices.
    bool in_subtree(int32_t bone, int32_t root) const;
    // Meshes whose own bone is `bone` / that are in_subtree of `root` (ascending mesh index).
    std::vector<uint32_t> meshes_on_bone(int32_t bone) const;
    std::vector<uint32_t> meshes_in_subtree(int32_t root) const;

private:
    std::vector<int32_t> mesh_bone_;
    std::vector<int32_t> parent_;
};

// What destroying one hardpoint does (sub-object indices, ascending, no duplicates): show first, then hide.
struct HardpointEffect {
    std::vector<uint32_t> show, hide;
};

// Per (type, model): sub-object layout, setup mask, destroy effects, attachment bones.
struct HardpointLayout {
    uint32_t meshes = 0, proxies = 0, hardpoints = 0;
    uint32_t sub_objects() const { return meshes + proxies + hardpoints; }
    uint32_t proxy_index(uint32_t p) const { return meshes + p; }
    uint32_t attachment_index(uint32_t h) const { return meshes + proxies + h; }
    std::vector<uint8_t> base;             // setup mask (1 = hidden) per sub-object
    std::vector<HardpointEffect> effects;  // per hardpoint
    std::vector<int32_t> attach_bone;      // per hardpoint: owner bone of Attachment_Bone (-1: none / not found)
    std::vector<std::string> attach_model; // per hardpoint: Model_To_Attach ("" none)
    // Per hardpoint: the sub-objects its destruction may erase, in lookup order (the first not yet erased goes);
    // empty when it has no Model_To_Attach or never changes visuals.
    std::vector<std::vector<uint32_t>> removes;
};
using HardpointLayoutPtr = std::shared_ptr<const HardpointLayout>;

// Case-insensitive mesh lookup: every mesh named `name` (ascending).
std::vector<uint32_t> meshes_named(const assets::ModelAsset& model, std::string_view name);

// The B.10 layout of a type's hardpoints on one owner model. show_damage: setup hides only the ALO flag
// (alamo_render --show-damage); effects are unchanged.
HardpointLayout build_hardpoint_layout(const assets::ModelAsset& model, std::span<const HardpointDef> hardpoints,
                                       bool show_damage = false);
// Without hardpoint XML: base = `mesh_mask` (meshes) + the ALO proxy flags, no effects, no attachments.
HardpointLayout fallback_hardpoint_layout(const assets::ModelAsset& model, std::span<const uint8_t> mesh_mask, size_t hardpoints);

// Mesh part of the setup mask (ALO flag + Damage_Decal first matches + state meshes + Damage_Particles subtrees).
std::vector<uint8_t> base_hidden_mask(const assets::ModelAsset& model, std::span<const HardpointDef> hardpoints,
                                      bool show_damage = false);

// World transform of a Model_To_Attach: owner_transform x the owner's bone (bone_world = the owner's current
// pose, per skeleton bone; empty = bind pose). bone < 0 / out of range: the owner's transform.
assets::Mat3x4 attachment_transform(const assets::ModelAsset& owner, const assets::Mat3x4& owner_transform,
                                    std::span<const assets::Mat3x4> bone_world, int32_t bone);

enum class MeshOverride : uint8_t { None = 0, Show = 1, Hide = 2 };

// Per-instance hardpoint state. Effective mask = layout base, then the effects of the destroyed hardpoints in
// destruction order (show, then hide), then explicit per-sub-object overrides. Recomputed on change only.
class HardpointVisibility {
public:
    // Destroyed state for `hardpoint_count` hardpoints (kept across init(); usable before the model is ready).
    void reset(size_t hardpoint_count);
    // Binds the owner model's layout. Keeps the destroyed state. Null: no layout (mask stays empty).
    void init(HardpointLayoutPtr layout);
    bool ready() const { return ready_; }
    const HardpointLayout* layout() const { return layout_.get(); }
    size_t hardpoint_count() const { return hardpoint_count_; }
    // destroyed = true: the hardpoint is destroyed (idempotent; appended to the destruction order).
    // destroyed = false: DEBUG RESET only - the game never repairs visuals (B.10 answer 5); this drops the
    // hardpoint from the destruction order and recomputes as if it had never died. False for an out-of-range index.
    bool set_destroyed(size_t hardpoint, bool destroyed);
    bool destroyed(size_t hardpoint) const;
    size_t destroyed_count() const { return order_.size(); }
    // Explicit per-sub-object override (false for an out-of-range index or before init()).
    bool set_override(uint32_t sub_object, MeshOverride o);
    // Effective mask over every sub-object (empty before init()); storage stable after init().
    std::span<const uint8_t> mask() const { return mask_; }
    bool hidden(uint32_t sub_object) const { return sub_object < mask_.size() && mask_[sub_object] != 0; }
    // Attached Model_To_Attach of hardpoint h drawn (has a model and its sub-object bit is clear).
    bool attachment_visible(uint32_t h) const;

private:
    void recompute();
    size_t hardpoint_count_ = 0;
    std::vector<uint32_t> order_;  // destroyed hardpoints in destruction order
    std::vector<uint8_t> overrides_, mask_, erased_;
    HardpointLayoutPtr layout_;
    bool ready_ = false;
};

// bkx1 t05 (b0019 / b0020, docs/re/BKX1_RENDER_RE.md §1 [H]): the model's particle groups switched by the engine state. A
// group is the particle proxies whose name starts with a fixed prefix (case-insensitive, prefix length only: "pe" also
// matches "pem*", not "pte*"): pe = the engine glow, pte = the turbo engines, pi = the ion-stun effect. 0 = none.
inline constexpr uint8_t kGlowPe = 1, kGlowPte = 2, kGlowPi = 4;
// SnapshotObject::engine_flags: a SimpleSpace-locomotor unit / its engines off / ion-stunned (apply_battle_state)
inline constexpr uint8_t kEngineGlowUnit = 1, kEngineGlowOff = 2, kEngineGlowIonStun = 4;
uint8_t particle_glow_group(std::string_view proxy_name);
// mcx6 t05 (BKX1_RENDER_RE §1.1, Lua Show_Emitter 0x581120 -> 0x265ae0): all 8 particle prefix groups of the model wrapper,
// bit i = the proxy name starts with prefix i (pe, pte, pi, pgw, pas, pptw, pem, prs; case-insensitive, prefix length only,
// so "pem*" is in groups 0 and 6).
uint8_t particle_emitter_groups(std::string_view proxy_name);
// The SimpleSpace locomotor's per-service glow rule (0x626420, §1.2 / §1.5) and the death / ion-stun switches (§1.3) for
// one unit. Inputs: SnapshotObject engine flags (kEngineGlow*), the timed disable's frames left (0 = none), v / V and
// TURBO active. G = pte while TURBO is active, else pe; the other group is hidden.
//   engines off, no timer (destroyed engine hard point, indefinite disable) -> G hidden;
//   engines off, timed (energy-pool ion drain) -> G shown, tint = (n x 0.5 + 0.5) x 0.2 snapped, n in [-1, 1] a fixed
//     per-frame value of x = 2 x frames left ([port] a hash lattice: retail's noise table 0x140a45f20 is not used), and
//     1.0 over 0.1 s on the last frame (frames left = 1);
//   engines on -> G shown, tint = clamp(0.8 v / V + 0.2, 0, 1) snapped;
//   ion stun (behaviour 0x45) -> pi shown, else pi hidden ([port]: retail hides it at the stun's end; before the first
//     stun it keeps the model's flag); dying -> pe and pte hidden.
// show bits mean "not hidden by this rule" ([port] only pi is un-hidden: pe / pte stay under the hard-point mask, so
// the B.10 Engine_Particles subtree rule keeps them hidden after the engine hard point dies).
struct EngineGlow {
    bool applies = false;  // a unit with the SimpleSpace glow rule
    uint8_t hide = 0, show = 0;
    uint8_t tint_group = 0;  // the group the tint applies to (G)
    float tint = -1.0f;      // colour RGBA brightness (SystemScalar::Color), < 0 = none
    float tint_seconds = 0.0f;
};
EngineGlow engine_glow(uint8_t engine_flags, uint32_t timer_frames, float speed_ratio, bool turbo, bool dying);
float engine_glow_flicker(uint32_t timer_frames);  // the timed-disable tint, in [0, 0.2]

}  // namespace alamo::render
