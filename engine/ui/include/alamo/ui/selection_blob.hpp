// alamo_ui_draw - selection circles as 3D scene primitives (docs/re/UI.md §1.3): the only world-space UI
// element that is not a projected 2D quad.
//   * Space (alBlob, flat): a unit quad ModelAsset in the XY plane with the ShadowBlobMaterials.xml material's
//     effect (Shader_Name, BlobStencilMasked.fx: render_phase Transparent) and textures Texture0 = Texture_Name
//     (i_selection_cutout_space.tga) / Texture1 = Texture_Name_2 (i_selection_<faction>.tga). Uploaded once per
//     material through Renderer::upload_model; each selected unit is one render::Instance (transform = scale
//     radius, translation = blob centre) appended to the frame's main instance list, so the renderer sorts it
//     into the Transparent bucket like any other transparent mesh.
//   * Land: the same blob instance, laid on the terrain under the unit (selection_blob_ground_instance): [port] the quad
//     follows the ground's local slope (a plane through the centre height with the slopes sampled at +-radius) and keeps
//     the circle's top-down footprint, instead of draping over every terrain vertex as the original's alTerrainShadowBlob
//     does. The land selection materials use BlobStencilMasked.fx, a two-pass stencil mask (cutout, then the colour where
//     the cutout passed) that the terrain decoration path (first pass only) cannot draw, so the instance path stays
//     (vis1 t03; cu13 t03 removed the earlier decoration-path attempt).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "alamo/assets/model.hpp"
#include "alamo/render/renderer.hpp"
#include "alamo/terrain/decorations.hpp"
#include "alamo/ui/hud_world.hpp"

namespace alamo::ui {

inline constexpr float kSpaceBlobLift = 0.5f;  // [port] z offset above the unit's plane (z-fighting guard)

// Unit quad (+-1 in X / Y, z = 0, normal +Z, uv (0,0) top-left) with `material`'s effect and textures; a material
// without Shader_Name gets the effect-less unlit stand-in with Texture_Name as BaseTexture.
std::shared_ptr<assets::ModelAsset> selection_blob_model(const terrain::ShadowBlobMaterial& material);
// Instance of an uploaded blob model for one blob (space).
render::Instance selection_blob_instance(const render::GpuModel& model, const SelectionBlob& blob);
// vis1 t03: land - the instance on the ground `height(x, y)`: centre z = the height at the centre + kSpaceBlobLift, the
// quad's X / Y axes sheared by the slopes (h(x + r) - h(x - r)) / 2r and (h(y + r) - h(y - r)) / 2r, so the circle keeps
// its radius seen from above and lies on the ground's local plane.
render::Instance selection_blob_ground_instance(const render::GpuModel& model, const SelectionBlob& blob,
                                                const std::function<float(float, float)>& height);

}  // namespace alamo::ui
