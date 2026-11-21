#include "alamo/render/blob_shadows.hpp"

#include <cmath>

namespace alamo::render {

BlobShadowMode blob_shadow_mode(int32_t shadow_detail, int32_t below_detail_level) {
    if (shadow_detail <= 0) return BlobShadowMode::None;
    return shadow_detail < below_detail_level ? BlobShadowMode::Blob : BlobShadowMode::Stencil;
}

void BlobShadowBuilder::set_materials(std::span<const terrain::ShadowBlobMaterial> materials) {
    materials_.assign(materials.begin(), materials.end());
    type_material_.clear();
}

uint32_t BlobShadowBuilder::material_index(const SnapshotTypeTable& table, objects::TypeIndex t) {
    if (type_material_.size() < table.size()) type_material_.resize(table.size(), 0u);
    uint32_t& slot = type_material_[t];
    if (slot == 0u) slot = terrain::find_blob_material(materials_, table[t].blob_material) + 1u;
    return slot - 1u;
}

void BlobShadowBuilder::build(const InterpolatedFrame& frame, const SnapshotTypeTable& table, int32_t shadow_detail,
                              const BlobBoneFn& bone, std::vector<terrain::ShadowBlob>& out) {
    out.clear();
    if (shadow_detail <= 0) return;
    constexpr uint8_t kSkip = kSnapDying | kSnapFogHidden | kSnapCorpse | kSnapContained;
    for (const SnapshotObject& o : frame.objects) {
        if (!table.valid(o.type) || (o.flags & kSnapDrawable) == 0 || (o.flags & kSkip) != 0 || o.script_hidden) continue;
        const SnapshotTypeInfo& ti = table[o.type];
        if (ti.blob_material.empty() || blob_shadow_mode(shadow_detail, ti.blob_below_detail) != BlobShadowMode::Blob) continue;
        Vec3 p = o.position;
        if (!ti.blob_bone.empty() && bone) {
            Vec3 b;
            if (bone(o, ti.blob_bone, b)) p = b;
        }
        terrain::ShadowBlob blob;
        blob.position[0] = p.x;
        blob.position[1] = p.y;
        blob.rotation = std::atan2(o.facing.y, o.facing.x);
        blob.width = ti.blob_size[0];
        blob.height = ti.blob_size[1];
        blob.intensity = ti.blob_intensity;
        blob.material = material_index(table, o.type);
        out.push_back(blob);
    }
}

}  // namespace alamo::render
