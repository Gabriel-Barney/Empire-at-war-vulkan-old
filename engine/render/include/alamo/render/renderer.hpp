// alamo_render - scene renderer joining assets + materials + rhi.
//
// Lit path (C3b): every drawable submesh resolves its ALO material effect through EffectCache
// (materials::EffectLibrary over the VFS). Each draw pass picks its PsoKey with
// ResolvedPass::select(LIGHT_SCALE.w); PipelineManager turns keys into pipelines on the job system
// (disk-persisted VkPipelineCache + key list). The hot-set / FF SPIR-V of engine/materials draws
// with the real interface of common.hlsli:
//   set 0 (per frame): FrameCB, Draws (DrawData per draw), Materials (deduplicated MaterialTable of
//         MeshMaterial built by materials::build_mesh_material), Bones (per-draw palettes in
//         Submesh::bone_palette order), FfDraws (FfDrawData of fixed-function passes);
//   set 1 (once, update-after-bind): bindless textures (slot 0 = white = materials::kNoTexture)
//         + the 4 engine samplers;
//   push constants: the pass's PushConstants (alpha test, fog) with the draw index.
// Draw order (RENDER.md 1.3/1.4): the 12 phase buckets in enum order; Transparent and Heat
// back-to-front by the submesh's world AABB centre; the others grouped by effect (the original's global
// effect list in first-creation order, SPACE_AND_OPEN.md D.1; names outside it after, by name), pass,
// material, with meshes in submission (instance) order inside a group.
// Submeshes without a usable effect (missing, untranslated without FF technique, vertex-layout
// family mismatch) and passes whose pipeline is still being created draw with the renderer-owned
// unlit shader (diffuse texture x vertex colour) so streaming never stalls a frame. Shadow-volume
// and Heat buckets are not drawn yet (counted).
//
// Unlit path (C2b, DrawMode::Unlit / alamo_render --unlit): every submesh with the unlit shader.
//
// Transforms (RENDER.md, SPEC.md 2.6): static submesh World = instance x bone_world[attach_bone];
// skinned palette[i] = instance x bone_world[b] x inverse_bind_world[b], b = bone_palette[i].
//
// Threading: upload_model() may run on worker jobs; make_resident(), draw(), destroy_model() run on
// the frame thread.
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "alamo/assets/asset_cache.hpp"
#include "alamo/assets/model.hpp"
#include "alamo/materials/effect_loader.hpp"
#include "alamo/materials/fixed_function.hpp"
#include "alamo/materials/params.hpp"
#include "alamo/materials/pso.hpp"
#include "alamo/materials/translated.hpp"
#include "alamo/render/camera.hpp"
#include "alamo/render/beams.hpp"
#include "alamo/render/lasers.hpp"
#include "alamo/render/lighting.hpp"
#include "alamo/render/lod.hpp"
#include "alamo/render/pipelines.hpp"
#include "alamo/render/upscale.hpp"
#include "alamo/rhi/rendertarget.hpp"
#include "alamo/rhi/rhi.hpp"

namespace alamo::jobs {
class JobSystem;
class JobCounter;
}  // namespace alamo::jobs

namespace alamo::render {

enum class BlendMode : uint8_t { Opaque, Alpha, Additive };
// Unlit stand-in for a key's blend state (Additive when the destination factor is One).
BlendMode blend_mode_for_key(const materials::PsoKey& k);
// The material's colour texture: parameter "BaseTexture", else the first Texture parameter whose
// role is Color (assets::texture_role_for_param). "" if none.
std::string diffuse_texture_name(const assets::MaterialRef& m);

// SHADOW_EXTRUSION_DISTANCE: a constant 200 world units in the original (SPACE_AND_OPEN.md B.5 [H]).
inline constexpr float kShadowExtrusionDistance = 200.0f;

// RENDER.md 1.3 bucket order.
enum class PhaseBucket : uint8_t {
    Terrain = 0, TerrainMesh, Fissure, Opaque, Occluded, OpaqueNonOccluding, Shadow, Water, Transparent, Lasers,
    Bloom, Heat, Count
};
PhaseBucket phase_bucket(materials::RenderPhase p);
const char* phase_bucket_name(PhaseBucket b);

using TextureSource =
    std::function<std::shared_ptr<const assets::TextureAsset>(std::string_view name, assets::TextureRole role)>;

struct GpuTexture {
    std::string name;
    assets::TextureRole role = assets::TextureRole::Color;
    rhi::Image image;
    rhi::UploadTicket ticket;
    uint32_t slot = 0;  // bindless slot once resident (0 = white)
    bool resident = false;
    bool fallback = false;
};

struct GpuPass {
    const materials::ResolvedPass* pass = nullptr;  // owned by GpuSubmesh::effect
    bool ff = false;
    bool translated = false;            // ProgramStatus::Translated (track B, translated on demand)
    // The pass (or its phase baseline) sets D3DRS_FOGENABLE = FALSE: no scene fog on its draws. Passes that
    // leave it alone inherit the scene's fog enable (RENDER.md 2.4); expression values count as not set.
    bool fog_off = false;
    struct XSlot {                      // translated: combined sampler s_n <- texture parameter + sampler state
        uint8_t slot = 0;
        int tex = -1;                   // GpuModel::textures, -1 = default (white)
        materials::SamplerKey key;
    };
    std::vector<XSlot> xslots;
    int tex[4] = {-1, -1, -1, -1};      // GpuModel::textures per sampler slot s0..s3 (FF: stage)
    uint32_t sampler[4] = {0, 0, 0, 0};  // EngineSampler per slot
    // Slots s0..s3 (bit n) whose texture is the engine's CLOUD_TEXTURE (semantic, no material texture):
    // bound to the attached world's cloud texture like translated passes do.
    uint8_t cloud_slots = 0;
    // FF passes: per-draw template (state expressions re-evaluated per draw from LIGHT_SCALE, TIME,
    // VIEWINVERSE, FOW_TEX_U/V, team colour) and its base record (load-time values when the template
    // could not be built). Callers change FF inputs per instance with Instance::ff_hook, not here.
    std::shared_ptr<const materials::FfDrawTemplate> ff_tpl;
    materials::FfDrawData ff_data{};
};

enum class LitStatus : uint8_t { Lit, NoEffect, Untranslated, LayoutMismatch, NotDrawn };

struct GpuSubmesh {
    uint32_t submesh = 0;  // index into ModelAsset::submeshes
    uint32_t mesh = 0;     // its ModelAsset::meshes index (hidden masks)
    int lod = -1;          // _LOD index of its mesh (-1: drawn at every LOD)
    int alt = -1;          // R4: _ALT index of its mesh (-1: drawn for every alternate; else only when Instance::alt matches)
    materials::VertexLayout layout = materials::VertexLayout::Unknown;
    bool skinned = false;
    rhi::Buffer vertices, indices;
    VkIndexType index_type = VK_INDEX_TYPE_UINT16;
    uint32_t index_count = 0;
    // Lit.
    std::shared_ptr<const materials::MaterialEffect> effect;
    std::shared_ptr<const materials::ParamStore> params;  // ALO material values (register-file fill)
    std::string effect_name;
    // D.1 opaque grouping: the effect's creation-order rank (EffectCache::load_rank); effects outside the
    // list (UINT32_MAX here or rank == list size) follow by name.
    uint32_t effect_order = UINT32_MAX;
    LitStatus lit = LitStatus::NoEffect;
    PhaseBucket bucket = PhaseBucket::Opaque;
    std::vector<GpuPass> passes;
    materials::MeshMaterial material{};  // tex filled per draw from residency
    uint64_t material_hash = 0;
    // Unlit.
    int base_texture = -1;
    BlendMode unlit_blend = BlendMode::Opaque;
    // Alpha test of the unlit stand-in: the first pass's D3DRS_ALPHAFUNC / ALPHAREF (PushConstants of the
    // material state; Always = off without an effect). Scene fog applies like on the lit passes, unless the
    // first pass disables it (GpuPass::fog_off).
    uint32_t unlit_alpha_func = static_cast<uint32_t>(materials::CompareOp::Always);
    uint32_t unlit_alpha_ref = 0;
    bool unlit_fog_off = false;
};

struct GpuModel {
    // Unique per upload_model() (process-wide counter); 0 = not uploaded. Keys the renderer's per-model memo
    // (pipelines, translated register images) kept across frames.
    uint64_t uid = 0;
    std::shared_ptr<const assets::ModelAsset> asset;
    MeshSelection selection;
    std::vector<GpuSubmesh> submeshes;
    std::vector<GpuTexture> textures;
    std::vector<rhi::UploadTicket> buffer_tickets;
    bool buffers_ready = false;
    uint64_t upload_bytes = 0;
    // Default per-mesh hidden mask (ModelAsset::meshes order, 1 = hidden): RendererDesc::damage_decal_rule at
    // upload (all zero with hide_damage_decals off). The meshes are uploaded anyway, so Instance::mesh_hidden
    // (the type's Damage_Decal rule, destroyed hardpoints) can show them.
    std::vector<uint8_t> mesh_hidden;
    uint32_t damage_meshes_hidden = 0;  // uploaded meshes hidden by the default mask
    int max_lod = -1;    // highest _LOD index uploaded (-1: no LOD meshes)
    int max_alt = -1;    // R1: highest _ALT index of the model's meshes and proxies (-1: none; damage alternates above it are not applied)
    // cull_bounds() at upload (whole-model culling, LOD screen size): covers every mesh an instance can show
    // (Instance::mesh_hidden may reveal damage decals / ALO-hidden meshes the default mask hides).
    assets::Aabb bounds;
    bool resident() const;  // buffers and every texture resident
    // Bind-pose model-space bounds of the submeshes this model draws by default (what the camera frames).
    assets::Aabb drawn_bounds() const;
    assets::Sphere drawn_sphere() const;
    // Bind-pose model-space bounds of every uploaded submesh (all LODs, hidden-by-default meshes, shadow volumes).
    assets::Aabb cull_bounds() const;
    // Union over the uploaded submeshes; default_mask = skip the meshes mesh_hidden hides.
    assets::Aabb submesh_bounds(bool default_mask) const;
};

// Per-instance fixed-function inputs hook (tools, reference tests): called once per FF draw of the instance
// with the submesh, its pass index, the renderer's per-draw expression inputs and the record after the
// renderer's per-draw template update; it may overwrite any field of `data` except the texture slots
// (tex.x/y/z, written afterwards). Runs on build() workers in parallel: must be thread-safe and must not
// touch renderer state. The result must depend only on its arguments (determinism across thread counts).
using FfDrawHook = std::function<void(const GpuSubmesh& g, size_t pass, const materials::ExprInputs& in,
                                      materials::FfDrawData& data)>;

struct Instance {
    const GpuModel* model = nullptr;
    Mat3x4 transform;
    std::span<const Mat3x4> bone_world;          // per skeleton bone for the current pose; empty = bind
    materials::float4 light_scale{1, 1, 1, 1};   // LIGHT_SCALE (.w = fade alpha)
    // Per-instance Colorization (team colour): written as (player RGB, 1) into the instance's copy of every
    // material with a Colorization parameter (SPACE_AND_OPEN.md B.3).
    bool team_color_override = false;
    materials::float4 team_color{1, 1, 1, 1};
    float lod_bias = kDefaultLodBias;            // GameObjectType LOD_Bias (B.4)
    // Per ModelAsset::meshes entry, 1 = hidden (hidden_mesh_mask / set_hardpoint_destroyed). Empty: the
    // model's default GpuModel::mesh_hidden. Must outlive the frame's build.
    std::span<const uint8_t> mesh_hidden;
    // R4 damage state: current model alternate (RENDER.md 5 / TERRAIN_WATER_POST 1.6). Meshes named "_ALTn" draw only
    // when n == alt; meshes without an _ALT suffix always draw. Every alternate is uploaded (GpuSubmesh::alt), so the
    // swap is per instance and per frame. Default 0 = the undamaged model.
    int alt = 0;
    // R5 stealth (ABILITY_VISUALS §1.3): the model's stencil-shadow bit is cleared - the instance's shadow-volume submeshes
    // (Shadow bucket) are not drawn. Everything else draws as before.
    bool no_shadow = false;
    // mcx4 t15 (Exclude_From_Distance_Fade, SnapshotTypeInfo::no_distance_fade): not culled beyond Object_Fade_End.
    bool no_distance_fade = false;
    // FF inputs hook (FfDrawHook); null = the renderer's per-draw records. Must outlive the frame's build.
    const FfDrawHook* ff_hook = nullptr;
    // [port] TAA object motion (taa.hpp): the key this object keeps from frame to frame (motion_key), so the renderer can
    // look up where it stood last frame and give its pixels a motion vector. 0 = no key: drawn as static (camera motion
    // only). Two live instances never share a key.
    uint64_t motion_id = 0;
};

// Key of a moving thing for Instance::motion_id: `kind` (1 = sim object, 2 = projectile, 3 = test / other) keeps id spaces
// apart, `slot` tells an object's attachments apart (0 = the object itself). Never 0.
constexpr uint64_t motion_key(uint32_t kind, uint64_t id, uint32_t slot = 0) {
    return (static_cast<uint64_t>(kind & 0xFFu) << 56) | ((id & 0xFFFFFFFFFFull) << 8) | (slot & 0xFFu);
}

// Multithreaded recording (C5). threads <= 1: one inline pass in the primary. Otherwise every phase
// bucket of the sorted draw list is split into contiguous chunks of at least min_draws_per_chunk
// draws (at most `threads` per bucket), each recorded into a secondary command buffer on a job-system
// worker (Frame::secondary(current_slot) - the FrameRing needs recording_threads >= JobSystem::
// thread_count()), then executed in order from the primary. Output is identical for any setting.
struct RecordOptions {
    uint32_t threads = 1;
    uint32_t min_draws_per_chunk = 128;
    float lod_detail = 1.0f;   // Mesh Detail option: d = min(Instance::lod_bias x coverage, (detail + 1) / 2)
    bool dynamic_lod = true;   // false: always the most detailed LOD
    bool frustum_cull = true;  // whole-model AABB vs frustum
    // Consecutive identical opaque items (same pipeline, submesh, pass, fading; not translated, not Transparent /
    // Heat) drawn as one instanced call (DrawData[draw_index + SV_InstanceID]); false = one call per item (tests:
    // the output is identical).
    bool instancing = true;
    // Frame graph (render() only; RENDER.md 1.4/1.5, TERRAIN_WATER_POST.md 4).
    bool post = true;             // heat + bloom after the Transparent bucket
    bool shadows = true;          // stencil shadow volumes + darken (never in space, needs a stencil format)
    bool soft_shadows = false;    // StencilDarkenToAlpha + back-buffer copy + StencilDarkenFinalBlur
    bool galactic = false;        // galactic mode: no bloom / heat (TERRAIN_WATER_POST.md 4.5)
    float heat_amount = -1.0f;    // SceneHeat DistortionAmount override (< 0: FXO default 0.25)
    float bloom_strength = -1.0f; // BloomStrength override (< 0: the environment's TEE 23)
    // Mirrored camera (water reflection, sky-cube faces): the view has determinant -1, so every
    // pipeline swaps its cull face (PsoKey::cull Front <-> Back) to keep the outward faces.
    bool mirrored = false;
    // Offscreen image for the GUI (planet portraits, cleared to 0, 0, 0, 0) [port]: the alpha channel keeps
    // coverage. Alpha-blended passes (SrcAlpha / InvSrcAlpha) blend alpha One / InvSrcAlpha (premultiplied "over");
    // every other blended pass (additive glows, modulate) leaves alpha alone; opaque passes are unchanged. The
    // colour is then premultiplied relative to the clear (the caller turns additive light into alpha).
    bool coverage_alpha = false;
    // Space frame (SPACE_AND_OPEN.md A.3-A.5): render()'s `sky` list is the background scene - drawn at the
    // objects' own transforms (not camera-relative) with near 10 / far 100000, no distance fade, no fog, the
    // lighting's fill lights zeroed and its Shadow / Bloom / Heat items skipped (bucket mask 0xbbf, no heat there) - then a depth-only clear, then the main
    // scene with the full lighting (fills on) and stencil shadows (when `shadows`).
    bool space = false;
    // render(): GPU timestamp scopes "world" (sky cube + reflection + refraction), "scene" (sky .. Bloom
    // buckets incl. terrain, shadows, water), "post" (SceneTexture copy, heat, bloom). Null: none.
    rhi::GpuTimer* gpu_timer = nullptr;
    // [port] Screen-space ambient occlusion (ssao.hpp; the original has none): after the opaque buckets and the
    // shadow darken, before water / transparent / particles / post. Not in galactic or space frames. Strength 0..1
    // scales the darkening; radius = the occlusion distance in world units. "ao" GPU timer scope.
    bool ambient_occlusion = false;
    float ao_strength = 0.6f;
    float ao_radius = 24.0f;
    // [port] Temporal anti-aliasing (taa.hpp; the original has none): every scene draw uses a sub-pixel jittered
    // projection (Halton 2,3), and after the post chain a resolve blends the frame into a reprojected, neighbourhood-
    // clipped history, then sharpens (taa_sharpen 0..1, 0 = off) back into the frame. Off: no jitter, nothing runs,
    // the history is dropped. "taa" GPU timer scope. Renderer::reset_taa_history() on camera cuts the rule misses.
    bool taa = false;
    float taa_sharpen = 0.25f;  // kTaaDefaultSharpen
    float taa_weight = -1.0f;   // share of the current frame in the resolve (smaller = stronger smoothing); < 0: kTaaCurrentWeight
    // [port] BM2b: this frame's TAA step (jitter index, history, object motion) may be taken back with
    // Renderer::discard_taa_frame() before the next render() - the benchmark's fixed-frames mode draws an unsettled frame
    // again and must not let it into the history. The frame itself is drawn exactly as without the flag.
    bool taa_provisional = false;
    // [port: Gabriel 2026-11-13] FSR upscaling (upscale.hpp): Upscaler::Fsr draws the frame at the quality mode's render
    // size with FSR's jitter and reconstructs the caller's (display) size with AMD FSR 4.1 / 3.1; it replaces TAA for the
    // frame (`taa` is then ignored) and reuses its object-motion bookkeeping. When FSR cannot run (no runtime, no
    // support, a failed dispatch) the frame is drawn at full size with TAA instead; DrawStats::fsr says which happened.
    // fsr_sharpness: FSR's own RCAS sharpening 0..1 (0 = off). frame_ms: time since the last frame (0: 16.6). "fsr" GPU
    // timer scope. [port: Gabriel 2026-11-13] Upscaler::Xess does the same with Intel XeSS Super Resolution (src/xess.hpp:
    // XeSS's preset of the same name, its own render size; no sharpening, no frame time); DrawStats::xess, "xess" scope.
    Upscaler upscaler = Upscaler::None;
    UpscaleQuality upscale_quality = UpscaleQuality::Quality;
    float fsr_sharpness = kFsrDefaultSharpness;
    float frame_ms = 0.0f;
    // [port] SMAA 1x (src/smaa.hpp; the original has none): after the post chain (after the TAA resolve when both are
    // on), luma edge detection, blending weights and neighbourhood blending of the scene colour at one fixed quality
    // (the reference HIGH preset). "smaa" GPU timer scope.
    bool smaa = false;
    // [port] MSAA: samples per pixel of the main scene (1 = off, 2, 4 or 8; clamped to what the device supports for the
    // scene's colour + depth-stencil formats, Renderer::msaa_samples). The scene buckets, the world passes, lasers / beams
    // and the scene hook draw into a multisampled colour + depth-stencil target that is resolved (colour average, depth
    // sample 0) before the post chain; the soft-shadow blur and SSAO resolve it first where they read it. Water reflection /
    // refraction and the sky cube stay single-sample. "msaa" GPU timer scope (the resolves).
    uint32_t msaa = 1;
    // [port] The video options' Shader / Water detail on top of the frame graph (RENDER.md 1.5, 6; both default to the
    // maximum = nothing changes). Shader detail 0..3: 0 is the fixed-function path - no shadow volumes, no soft shadows (the
    // StencilDarken variant, TERRAIN_WATER_POST.md 4.3) - and it caps Water detail: < 3 -> 1, < 1 -> 0. Water detail 0..2:
    // 2 = reflection + refraction targets recorded, 1 = reflection only (the DX8 technique), 0 = neither (the water
    // samples its default textures). The port keeps the effects' best technique at every level; only these passes change.
    int shader_detail = 3;
    int water_detail = 2;
    // [port: Gabriel] Render scale: the share of the output image's PIXELS the scene (world, post chain, TAA) is drawn
    // with, clamped to kRenderScaleMin..1; 1 = native. Each side is scaled by sqrt(render_scale) (render_scale_extent),
    // so 0.75 at 3840 x 2160 draws 3326 x 1871. The scene is then stretched to the output with a linear filter (native:
    // the unchanged point copy). Whatever the caller draws on the output afterwards (HUD, dialogs) stays native.
    float render_scale = 1.0f;
};

inline constexpr float kRenderScaleMin = 0.25f;
// The scene size render() draws for an output of `out` at `scale` (see RecordOptions::render_scale): each side
// round(side x sqrt(clamp(scale, kRenderScaleMin, 1))), at least 1. 1 (or anything above) returns `out` unchanged.
VkExtent2D render_scale_extent(VkExtent2D out, float scale);

struct DrawTiming {
    double build_ms = 0;   // cull + LOD + keys (parallel), sort, records, upload to the frame ring
    double cull_ms = 0, sort_ms = 0, fill_ms = 0;  // parts of build_ms
    double record_ms = 0;  // command recording (secondaries in parallel + execute, or inline)
    double world_ms = 0;   // part of record_ms: world prepass (sky cube, reflection, refraction incl. their builds)
    double post_ms = 0;    // part of record_ms: post chain (SceneTexture copy, heat, bloom)
    uint32_t chunks = 0;   // secondary command buffers (0 = inline)
};

struct RendererDesc {
    VkFormat color_format = VK_FORMAT_B8G8R8A8_UNORM;
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    uint32_t texture_capacity = 4096;           // bindless slots (clamped to the device limit)
    uint32_t bindless_slots = 0;                // slots the texture table hands out (0 = texture_capacity; tests)
    EffectCache* effects = nullptr;             // null: unlit only
    jobs::JobSystem* js = nullptr;              // pipeline creation jobs (null: synchronous)
    std::filesystem::path pipeline_cache_dir;   // empty: no persistence
    // Default hidden meshes of a model without type information (lod.hpp, SPACE_AND_OPEN.md B.9): the
    // fallback name rule (*_BLAST*) + STEALTH/buildup/charged. Instances with the type's hardpoint XML
    // override it (Instance::mesh_hidden). false = draw them (alamo_render --show-damage).
    bool hide_damage_decals = true;
    DamageDecalRule damage_decal_rule = default_hidden_mesh_rule;
    bool team_color_override = false;
    materials::float4 team_color{1, 1, 1, 1};   // Colorization when overridden
    // Track B: runtime DXC (dxcompiler.dll next to the exe / $ALAMO_DXC_DIR) + disk shader cache.
    bool translated = true;                     // false: translated passes draw unlit
    std::filesystem::path shader_cache_dir;     // empty: shaderxlat::ShaderCache::default_directory()
};

enum class DrawMode : uint8_t { Lit, Unlit };

struct DrawStats {
    uint32_t draws = 0, lit_draws = 0, ff_draws = 0, unlit_draws = 0;
    uint32_t pending_pipeline = 0;   // submeshes drawn unlit while their pipelines are created
    uint32_t no_effect = 0, untranslated = 0, layout_mismatch = 0, not_drawn = 0;
    uint32_t skinned_draws = 0, transparent_draws = 0, materials = 0;
    uint32_t instances = 0, culled = 0;
    uint32_t translated_draws = 0;   // track-B passes drawn with their translated program
    uint32_t shadow_volumes = 0, heat_draws = 0, sky_draws = 0;
    bool darkened = false, soft_shadows = false, heat = false, bloom = false;  // post passes run this frame
    uint32_t post_pending = 0;       // post passes skipped this frame (program / pipeline still compiling)
    uint32_t translation_pending = 0, no_translation = 0;  // drawn unlit: program compiling / unavailable
    uint64_t triangles = 0;
    // Mesh-list vkCmdDrawIndexed calls: consecutive identical opaque items are one instanced call (draws counts
    // items = instances). instanced_calls: calls with more than one instance.
    uint32_t draw_calls = 0, instanced_calls = 0;
    // R4: scene-hook runs of the Transparent bucket drawn between mesh items (per-renderable back-to-front interleave).
    uint32_t hook_interleaved = 0;
    // Q3: translated mesh draws recorded as instanced runs (calls with > 1 instance among instanced_calls).
    uint32_t translated_instanced_calls = 0;
    // Laser projectiles (set_lasers): beams / kites drawn this frame, batches skipped (pipeline / texture not ready).
    uint32_t laser_beams = 0, laser_kites = 0, laser_pending = 0;
    // Ability beams (set_beams, beams.hpp): ribbons / dots / lightning bolts drawn this frame, batches skipped (texture or pipeline not ready).
    uint32_t beam_ribbons = 0, beam_dots = 0, beam_bolts = 0, beam_pending = 0;
    // [port] TAA (RecordOptions::taa): the resolve ran; this frame restarted the history (current frame only).
    bool taa = false, taa_reset = false;
    uint32_t taa_movers = 0;  // draws of the TAA velocity pass (keyed instances that moved since the last frame)
    // [port] FSR (RecordOptions::upscaler): the frame was drawn at render_width x render_height and upscaled by FSR.
    bool fsr = false;
    bool xess = false;  // [port] the same with XeSS (RecordOptions::upscaler = Upscaler::Xess)
    uint32_t render_width = 0, render_height = 0;  // the scene's size this frame (the caller's size without FSR)
    bool smaa = false;        // [port] SMAA (RecordOptions::smaa) ran
    uint32_t msaa = 1;        // [port] samples the scene was drawn with (RecordOptions::msaa after the device clamp)
};

// ---- R4 scene hook (engine/render/particles): render-owned effects drawn inside the frame graph's buckets.
// render() calls prepare() once on the primary before any pass (outside dynamic rendering: compute, fills, barriers),
// then draw() inside an open pass on the scene target after the bucket's mesh items: Water (bucket 7) and Transparent
// (bucket 8) in the main scene, Heat (bucket 11) in the heat pass (which then runs even without heat meshes). Buckets
// the hook reports empty (has() false) cost nothing. The hook sets its own viewport / scissor and pipelines.
struct SceneHookPass {
    rhi::Frame* frame = nullptr;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    const ViewMatrices* view = nullptr;
    PhaseBucket bucket = PhaseBucket::Transparent;
    VkFormat color_format = VK_FORMAT_UNDEFINED, depth_format = VK_FORMAT_UNDEFINED, stencil_format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;  // [port] MSAA: the scene target's sample count
    uint32_t width = 0, height = 0;
};
class SceneHook {
public:
    virtual ~SceneHook() = default;
    // Scene fog of the frame (render() calls it before prepare): enabled, colour and FOG_VALS (scene_fog_vals).
    virtual void scene_fog(bool /*enabled*/, const assets::Float3& /*color*/, float /*fog_x*/, float /*fog_y*/) {}
    virtual void prepare(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view) = 0;
    virtual bool has(PhaseBucket bucket) const = 0;
    virtual void draw(const SceneHookPass& pass) = 0;
    // Optional per-renderable interleave (R4, ANIM_PARTICLES B.5: bucket 8 is sorted back-to-front PER RENDERABLE): the
    // hook's renderables of `bucket` in its own back-to-front order with their squared camera distances (the metric of
    // the mesh items' depth keys); render() then merges them with the bucket's mesh items by distance (ties: meshes
    // first [port]) and calls draw_range for each run. draw_count 0 = draw() after the bucket's mesh items.
    virtual uint32_t draw_count(PhaseBucket /*bucket*/) const { return 0; }
    virtual float draw_distance(PhaseBucket /*bucket*/, uint32_t /*index*/) const { return 0.0f; }
    virtual void draw_range(const SceneHookPass& pass, uint32_t /*begin*/, uint32_t /*end*/) { draw(pass); }
};

// ---- Colour + depth pass helpers (dynamic rendering, sync2 barriers).
struct PassTarget {
    VkImage color = VK_NULL_HANDLE;
    VkImageView color_view = VK_NULL_HANDLE;
    VkImage depth = VK_NULL_HANDLE;  // optional
    VkImageView depth_view = VK_NULL_HANDLE;
    VkExtent2D extent{};
    float clear[4] = {0, 0, 0, 1};
    rhi::ImageState color_after = rhi::ImageState::Present;  // Present (swapchain) / TransferSrc (readback)
    bool secondaries = false;  // VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT (no viewport set in the primary)
};
void begin_pass(const rhi::DeviceFns& fn, VkCommandBuffer cmd, const PassTarget& t);
void end_pass(const rhi::DeviceFns& fn, VkCommandBuffer cmd, const PassTarget& t);

class WorldRenderer;  // world.hpp: terrain + water of a land map
class RendererCore;  // src/renderer_core.hpp: the implementation (frame graph, internal passes, track B, bindless slots)

// The scene renderer as the rest of the engine sees it: setup and settings, model residency, frame submit (draw() inline
// into the caller's pass, render() through the whole frame graph), the [port] upscaler / TAA controls, lasers and beams
// of the frame, bindless slot retirement and read-only diagnostics. Everything else (internal passes, draw lists,
// translated register images, frame constants) is RendererCore's and stays inside engine/render.
class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // ---- Setup and settings.
    bool init(rhi::Context& ctx, rhi::Allocator& alloc, rhi::Uploader& up, const RendererDesc& desc);
    void shutdown();  // caller makes sure the GPU is idle; saves the pipeline cache
    bool set_color_format(VkFormat format);  // swapchain recreate (GPU idle)
    VkFormat color_format() const;
    VkFormat depth_format() const;
    void set_lighting(const Lighting& l);
    const Lighting& lighting() const;
    // Scene clock in seconds (TIME: FrameCB, FF expressions, UV scrolling). Default 0 (deterministic).
    void set_time(float seconds);
    // [port] Anisotropic filtering (the video options' setting). 0 / 1 = off (the default: every sampler keeps
    // its material / engine state exactly), else 2..16, clamped to 16 and the device limit. Samplers of meshes,
    // terrain, water and decorations with linear minification use max(their own level, this); point-filtered,
    // full-screen post and 2D prim samplers are untouched. Takes effect from the next draw()/render(): translated
    // samplers are picked per frame / generation, and the hot set's engine samplers (8x already) are rewritten by
    // the first draw()/render() of a later frame after an idle wait. Old samplers stay alive in the sampler cache.
    void set_anisotropy(uint32_t level);
    // [port] Texture detail (the original's "texture reduction", RENDER.md 6): the number of top mip levels the scene's
    // samplers skip (0..3, 0 = off: the default, every sampler keeps its state). It travels with the anisotropy setting:
    // same latch and same sampler set (meshes, terrain, water, decorations with linear minification and mips; never the
    // HUD, post or prim samplers). A texture with fewer levels samples its last one.
    void set_texture_reduction(uint32_t mips);
    // [port] Output gamma (Options > Video > Gamma; the original sets the device gamma ramp): raises every colour channel
    // of the finished frame - `color` after the HUD, in `state` - to 1 / gamma in one full-screen pass (a scratch copy of
    // the frame is read), leaving it in `after`. gamma 1 (or outside 0.25..4) records nothing and returns true. False
    // when a pipeline / descriptor / scratch image is missing (the frame is untouched).
    bool apply_gamma(rhi::Frame& frame, VkCommandBuffer cmd, VkImage color, VkImageView view, VkFormat format, VkExtent2D extent,
                     rhi::ImageState state, rhi::ImageState after, float gamma);

    // ---- Models.
    bool upload_model(std::shared_ptr<const assets::ModelAsset> model, const TextureSource& textures, GpuModel& out,
                      jobs::JobSystem* js = nullptr, std::string* error = nullptr);
    bool make_resident(GpuModel& m);
    // Blocking: waits for `m`'s buffer / texture uploads and records their acquires on the graphics queue
    // (Uploader::finish), so the next make_resident(m) succeeds. False when an upload failed.
    bool finish_uploads(const GpuModel& m);
    // Releases the model's bindless slots with the frame ring's current value and defers its images and vertex / index
    // buffers until the slots retire (collect_released). Without a frame ring the caller guarantees an idle GPU.
    void destroy_model(GpuModel& m);
    // Requests the pipelines `m` needs (so they start compiling before the first draw).
    void prepare_pipelines(const GpuModel& m, float light_scale_w = 1.0f);

    // ---- Frame submit.
    // Single-threaded, inline into `cmd` inside an open pass (begin_pass without secondaries).
    bool draw(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view, std::span<const Instance> instances,
              DrawMode mode = DrawMode::Lit, DrawStats* stats = nullptr);
    // Same with record options (only RecordOptions::coverage_alpha and the build options apply; draw() never
    // draws the frame-graph parts).
    bool draw(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view, std::span<const Instance> instances,
              DrawMode mode, const RecordOptions& opts, DrawStats* stats = nullptr);
    // Whole colour pass: begin_pass (with the secondary-contents flag when chunks are recorded on
    // workers), draws, end_pass. `primary` is frame.cmd() (or another primary of thread 0).
    // The frame renders into renderer-owned targets (back-buffer equivalent with a depth-stencil
    // image, SceneTexture, DistortionTexture, 1/4 bloom pair) and is copied into target.color at the
    // end (target.depth is not used). `sky`: background objects (TEE 19/1A skydomes) drawn first with
    // a camera-relative transform (translation = eye) and the background camera (near 10, far 100000),
    // then depth is cleared (RENDER.md 1.2).
    bool render(rhi::Frame& frame, VkCommandBuffer primary, const PassTarget& target, const ViewMatrices& view,
                std::span<const Instance> instances, DrawMode mode, const RecordOptions& opts, DrawStats* stats = nullptr,
                DrawTiming* timing = nullptr, std::span<const Instance> sky = {});
    // Land map world (terrain bucket 0, water bucket 7 + its reflection / refraction / sky-cube passes),
    // drawn by render() while set. Not owned; null detaches.
    // Detaching (or switching) also drops the world's CLOUD_TEXTURE / FOW_TEXTURE views, slots and matrices.
    void set_world(WorldRenderer* w);
    WorldRenderer* world() const;
    // Object distance fade (TacticalCameras.xml Object_Fade_Begin / End, RENDER.md 3.4): DISTANCE_FADE_VALS
    // in the FOG_VALS form, and instances entirely beyond `end` (view depth) are culled. end <= 0: off.
    void set_object_fade(float begin, float end);
    // R4: effects drawn by render() in buckets 7 / 8 / 11 (SceneHook). Not owned; null detaches.
    void set_scene_hook(SceneHook* h);

    // ---- [port] TAA, MSAA and upscalers (RecordOptions).
    // TAA: the next render() takes the current frame only (no history). render() already restarts on resize, on a frame
    // without TAA and on a camera jump (taa_history_usable); call this on cuts it can miss (teleports to a similar view, a
    // cinematic cut, a different scene drawn through the same Renderer).
    void reset_taa_history();
    // BM2b: undo the TAA step of the last render() drawn with RecordOptions::taa_provisional, so the next frame jitters,
    // reprojects and sees object motion as if it had not been drawn. True when there was one to undo; false after a
    // render() without the flag or without TAA, a reset_taa_history(), keep_taa_frame() or a discard already done.
    // keep_taa_frame(): that frame stands (drops what discard would restore).
    bool discard_taa_frame();
    void keep_taa_frame();
    // MSAA: the sample count render() uses for `requested` (1, 2, 4 or 8): the largest supported count not above it (the
    // device's framebuffer colour / depth / stencil sample counts), 1 without a device.
    uint32_t msaa_samples(uint32_t requested) const;
    // FSR (RecordOptions::upscaler): loads the runtime on first call; true when FSR frames can be drawn here (the context
    // itself is created by the first FSR frame and can still fail - DrawStats::fsr). xess_supported(): the same for
    // Upscaler::Xess. upscaler_status(u): that upscaler's runtime ("FSR 4.1.2" / "XeSS 2.0.2" style, once a frame created
    // the context) or why it is off.
    bool fsr_supported();
    bool xess_supported();
    std::string upscaler_status(Upscaler u = Upscaler::Fsr) const;

    // ---- Lasers and ability beams of the frame.
    // Laser projectiles (lasers.hpp, PROJECTILE_NEBULA_DRAW.md 1): init_lasers loads W_Laser_Pill.tga /
    // p_particle_master.tga through `textures` and Engine\PrimAdditive; set_lasers gives the frame's lasers (copied),
    // drawn by render() in the Transparent bucket after its mesh items (and the hook's transparent draws) with the frame's
    // camera. Main scene only; no effect without init_lasers.
    bool init_lasers(const TextureSource& textures);
    void set_lasers(std::span<const Laser> lasers, const LaserConstants& constants = {});
    // Ability beams (beams.hpp, ABILITY_VISUALS.md 2 / 4): init_beams loads Engine\PrimAdditive and the textures on demand
    // (w_galaxy_dot.tga, lightning_default.tga, each beam's ribbon texture) through `textures`; `lightning` (optional,
    // copied) = the LightningEffectTypes.xml effects (default: the shipped Tractor_Beam / Piet_Energy_Weapon). set_beams
    // gives the frame's beams (world endpoints) and the sim tick the frame shows: the per-beam animation (scroll, dots,
    // lightning bolts) advances once per tick. Drawn by render() in the Transparent bucket with the lasers (main scene only).
    bool init_beams(const TextureSource& textures, const LightningLibrary* lightning = nullptr);
    void set_beams(std::span<const BeamInstance> beams, uint64_t tick);
    void release_beams();  // drops the beam pass, its textures and buffers (GPU idle); set_beams then does nothing until init_beams

    // ---- Bindless texture slots (slot 0 = white). destroy_model() releases a model's slots with the frame ring's
    // current value; collect_released() (called by draw()/render() after begin_frame, or by the caller) retires them.
    void set_frame_ring(const rhi::FrameRing* ring);
    uint32_t collect_released();  // returns slots retired; destroys the images whose slots retired
    uint32_t textures_resident() const;
    uint32_t texture_slot_high_water() const;
    uint32_t texture_slots_pending() const;

    // ---- Pipelines and track B (translated passes): waits and diagnostics.
    void wait_pipelines();  // blocks until every queued pipeline creation finished
    PipelineManager::Stats pipeline_stats() const;
    bool dxc_available() const;
    const std::string& dxc_status() const;
    void wait_translations();  // blocks until every queued translation job finished
    struct TranslationStats {
        uint32_t programs = 0, ready = 0, failed = 0, pending = 0, cache_hits = 0;
        double translate_ms = 0;  // summed job time
    };
    TranslationStats translation_stats() const;
    // Shader issues met so far (docs/MODS.md shader section): desc.effects->issues() plus one TranslateFailed
    // per failed translation job (effect, path, translator / DXC message; the submesh draws unlit). Sorted by
    // (effect, kind); a load report or debug overlay lists them. Thread-safe.
    std::vector<materials::ShaderIssue> shader_issues() const;

private:
    friend RendererCore& renderer_core(Renderer& r);
    friend const RendererCore& renderer_core(const Renderer& r);
    std::unique_ptr<RendererCore> core_;
};

// ---- Model streaming on the job system (AssetCache loads + uploads as background jobs).
class ModelStreamer {
public:
    ModelStreamer(Renderer& r, assets::AssetCache& cache, jobs::JobSystem& js);
    ~ModelStreamer();
    ModelStreamer(const ModelStreamer&) = delete;
    ModelStreamer& operator=(const ModelStreamer&) = delete;

    enum class State : int { Loading = 0, Uploading = 1, Ready = 2, Failed = 3 };
    uint32_t request(std::string model_name);
    void update();  // frame thread, after record_acquires
    // Blocking (frame thread, scripted runs): waits for every load job, finishes the uploads of the models still
    // uploading (Renderer::finish_uploads), then update(): every requested model is Ready or Failed afterwards, so the
    // frame a model first draws no longer depends on load timing.
    void finish_pending();
    State state(uint32_t handle) const;
    GpuModel* model(uint32_t handle);
    const std::string& error(uint32_t handle) const;
    double cpu_ms(uint32_t handle) const;
    void wait_jobs();
    void destroy_all();  // GPU must be idle

private:
    struct Entry {
        std::string name;
        GpuModel model;
        std::atomic<int> state{0};
        bool job_done = false;
        bool pipelines_requested = false;
        std::atomic<bool> finished{false};
        std::string error;
        double cpu_ms = 0;
    };
    Renderer& renderer_;
    assets::AssetCache& cache_;
    jobs::JobSystem& js_;
    std::unique_ptr<jobs::JobCounter> counter_;
    std::deque<std::unique_ptr<Entry>> entries_;
};

bool setup_validation_env(const std::string& source_dir);

// ALO bone billboard (docs/formats/SPEC.md 2.2): the world matrix `world` (instance x bone) of a static submesh on a bone
// with billboard `mode` re-oriented for the camera (eye, right / up / forward axes) and `sun` (towards the light). 1
// parallel, 2 face, 3 z_axis_view, 5 z_axis_light; 6 sun_glow = parent origin + sun x bone_distance (model units, x the
// parent's scale), 7 sun = eye + sun x bone_distance; 0 / 4 (wind) unchanged. Facing modes turn local -Y towards the
// camera, keeping the column lengths [port: axis use not traced]. Applied by the renderer to every static submesh.
Mat3x4 billboard_world(const Mat3x4& world, uint32_t mode, const Mat3x4& parent_world, float bone_distance, const Float3& eye,
                       const Float3& cam_right, const Float3& cam_up, const Float3& cam_forward, const Float3& sun);

}  // namespace alamo::render
