// alamo_render (lib alamo_render_sim) - vis1 t02: object blob shadows (docs/re/WEATHER_VISUALS.md §4, DECORATIONS.md §3).
// A land object whose type names Blob_Shadow_Material_Name gets a terrain shadow blob (terrain::ShadowBlob, drawn by
// MapScene::set_shadow_blobs) instead of its stencil shadow when the shadow detail is below the type's
// Blob_Shadow_Below_Detail_Level:
//   blob_shadow_mode(detail, level) = None (detail 0: no shadow at all), Blob (detail < level) or Stencil.
// Retail's shadow detail is 0..3 (Player options ShadowDetail; 3 = the shipped default). [port] The port's Shadow detail
// option has Off / On: Off = 0, On = 3 (kPortShadowDetailOn), so at On only the types with level > 3 (the trees, rocks and
// props at 99) draw a blob and every other type keeps its shadow volume, as retail at ShadowDetail 3.
// Per frame and object: centre = the object position, or the world position of Blob_Shadow_Bone_Name when the model has
// that bone; size = Blob_Shadow_Scale (world units, not scaled by the object); rotation = the object's heading
// [M: the object field the original reads is in degrees and taken as the heading]; intensity = Blob_Shadow_Intensity;
// material = the ShadowBlobMaterials entry of that name (missing: the default shadow, entry 0). Space has no terrain:
// no blobs. Presentation only.
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include "alamo/render/sim_snapshot.hpp"
#include "alamo/terrain/decorations.hpp"

namespace alamo::render {

enum class BlobShadowMode : uint8_t { None = 0, Blob = 1, Stencil = 2 };
inline constexpr int32_t kPortShadowDetailOn = 3;
BlobShadowMode blob_shadow_mode(int32_t shadow_detail, int32_t below_detail_level);

// World position of a named bone of a frame object (false: no such bone / model not loaded).
using BlobBoneFn = std::function<bool(const SnapshotObject& o, std::string_view bone, Vec3& out)>;

class BlobShadowBuilder {
public:
    // The ShadowBlobMaterials table of the scene (MapScene::blob_materials); clears the per-type cache.
    void set_materials(std::span<const terrain::ShadowBlobMaterial> materials);
    // The blobs of `frame` for the types of `table` at `shadow_detail` (ascending object id). Skipped: objects without a
    // drawable model, dying, corpses, contained, Lua-hidden or hidden by the fog of war [port: a fogged object's blob would
    // show where the object is not drawn].
    void build(const InterpolatedFrame& frame, const SnapshotTypeTable& table, int32_t shadow_detail, const BlobBoneFn& bone,
               std::vector<terrain::ShadowBlob>& out);

private:
    uint32_t material_index(const SnapshotTypeTable& table, objects::TypeIndex t);
    std::vector<terrain::ShadowBlobMaterial> materials_;
    std::vector<uint32_t> type_material_;  // per type: material index + 1, 0 = not resolved yet
};

}  // namespace alamo::render
