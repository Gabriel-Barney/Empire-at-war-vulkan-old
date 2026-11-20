// alamo_render - the renderer's implementation behind the public Renderer (alamo/render/renderer.hpp). Not part of the
// public API (REF3 t07): engine/render's own sources, its tests and the alamo_render tool app include it (target
// alamo_render_internal); every other module draws through Renderer. Renderer forwards each public call to the
// RendererCore it owns (src/renderer_api.cpp); renderer_core() below reaches the core of a Renderer.
//
// What lives here and not in the public header: the frame graph state and its internal passes (SSAO, SMAA, TAA,
// gamma, FSR, XeSS), track B translation and its register images, the bindless slot bookkeeping, the frame-constant
// (SH lighting) contract, the per-frame draw lists and the inspection points the tests use (draw keys, translated
// register images, last draw list, laser / beam geometry).
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "alamo/render/renderer.hpp"

namespace alamo::render {

struct EngineFrame;     // render_internal.hpp
struct XConstCache;     // render_internal.hpp: register images of one translated non-mesh draw
struct WorldBindStats;  // world.hpp
class WorldRenderer;    // world.hpp: terrain + water of a land map
class SsaoPass;         // ssao.hpp: [port] screen-space ambient occlusion
class SmaaPass;         // smaa.hpp: [port] SMAA 1x
class TaaPass;          // taa.hpp: [port] temporal anti-aliasing
class GammaPass;        // gamma.hpp: [port] output gamma
class FsrPass;          // fsr.hpp: [port] FSR upscaling
class XessPass;         // xess.hpp: [port] XeSS upscaling

// One recorded draw, for comparing draw lists across thread counts (RendererCore::last_draw_list).
struct DrawListEntry {
    uint32_t instance = 0, submesh = 0;
    int pass = -1;           // -1 = unlit
    uint64_t key_hash = 0;   // PsoKey hash (lit) or unlit pipeline variant
    uint32_t material_index = 0;
    bool operator==(const DrawListEntry& o) const {
        return instance == o.instance && submesh == o.submesh && pass == o.pass && key_hash == o.key_hash &&
               material_index == o.material_index;
    }
};

class RendererCore {
public:
    RendererCore();
    ~RendererCore();
    RendererCore(const RendererCore&) = delete;
    RendererCore& operator=(const RendererCore&) = delete;

    bool init(rhi::Context& ctx, rhi::Allocator& alloc, rhi::Uploader& up, const RendererDesc& desc);
    void shutdown();  // caller makes sure the GPU is idle; saves the pipeline cache
    bool set_color_format(VkFormat format);  // swapchain recreate (GPU idle)
    VkFormat color_format() const { return desc_.color_format; }
    VkFormat depth_format() const { return desc_.depth_format; }
    void set_lighting(const Lighting& l) { lighting_ = l; }
    // Scene clock in seconds (TIME: FrameCB, FF expressions, UV scrolling). Default 0 (deterministic).
    void set_time(float seconds) { time_ = seconds; }
    float time() const { return time_; }
    const Lighting& lighting() const { return lighting_; }
    PipelineManager& pipelines() { return psos_; }
    const PipelineManager& pipelines() const { return psos_; }
    // [port] Anisotropic filtering (the video options' setting). 0 / 1 = off (the default: every sampler keeps
    // its material / engine state exactly), else 2..16, clamped to 16 and the device limit. Samplers of meshes,
    // terrain, water and decorations with linear minification use max(their own level, this); point-filtered,
    // full-screen post and 2D prim samplers are untouched. Takes effect from the next draw()/render(): translated
    // samplers are picked per frame / generation, and the hot set's engine samplers (8x already) are rewritten by
    // the first draw()/render() of a later frame after an idle wait. Old samplers stay alive in the sampler cache.
    void set_anisotropy(uint32_t level) { aniso_request_ = level; }
    uint32_t anisotropy() const;  // the effective level (0 = off)
    // [port] Texture detail (the original's "texture reduction", RENDER.md 6): the number of top mip levels the scene's
    // samplers skip (0..3, 0 = off: the default, every sampler keeps its state). It travels with the anisotropy setting:
    // same latch and same sampler set (meshes, terrain, water, decorations with linear minification and mips; never the
    // HUD, post or prim samplers). A texture with fewer levels samples its last one.
    void set_texture_reduction(uint32_t mips) { mip_skip_request_ = mips < 3u ? mips : 3u; }
    uint32_t texture_reduction() const { return mip_skip_request_; }
    // [port] Output gamma (Options > Video > Gamma; the original sets the device gamma ramp): raises every colour channel
    // of the finished frame - `color` after the HUD, in `state` - to 1 / gamma in one full-screen pass (a scratch copy of
    // the frame is read), leaving it in `after`. gamma 1 (or outside 0.25..4) records nothing and returns true. False
    // when a pipeline / descriptor / scratch image is missing (the frame is untouched).
    bool apply_gamma(rhi::Frame& frame, VkCommandBuffer cmd, VkImage color, VkImageView view, VkFormat format, VkExtent2D extent,
                     rhi::ImageState state, rhi::ImageState after, float gamma);

    bool upload_model(std::shared_ptr<const assets::ModelAsset> model, const TextureSource& textures, GpuModel& out,
                      jobs::JobSystem* js = nullptr, std::string* error = nullptr);
    bool make_resident(GpuModel& m);
    // Blocking: waits for `m`'s buffer / texture uploads and records their acquires on the graphics queue
    // (Uploader::finish), so the next make_resident(m) succeeds. False when an upload failed.
    bool finish_uploads(const GpuModel& m);
    void destroy_model(GpuModel& m);
    // Requests the pipelines `m` needs (so they start compiling before the first draw).
    void prepare_pipelines(const GpuModel& m, float light_scale_w = 1.0f);

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
    void set_world(WorldRenderer* w) {
        world_ = w;
        reset_world_constants();
    }
    // Object distance fade (TacticalCameras.xml Object_Fade_Begin / End, RENDER.md 3.4): DISTANCE_FADE_VALS
    // in the FOG_VALS form, and instances entirely beyond `end` (view depth) are culled. end <= 0: off.
    void set_object_fade(float begin, float end) {
        fade_begin_ = begin;
        fade_end_ = end;
    }
    WorldRenderer* world() const { return world_; }
    // R4: effects drawn by render() in buckets 7 / 8 / 11 (SceneHook). Not owned; null detaches.
    void set_scene_hook(SceneHook* h) { scene_hook_ = h; }
    // [port] TAA (RecordOptions::taa): the next render() takes the current frame only (no history). render() already
    // restarts on resize, on a frame without TAA and on a camera jump (taa_history_usable); call this on cuts it can
    // miss (teleports to a similar view, a cinematic cut, a different scene drawn through the same Renderer).
    void reset_taa_history();
    // [port] MSAA: the sample count render() uses for `requested` (1, 2, 4 or 8): the largest supported count not above
    // it (the device's framebuffer colour / depth / stencil sample counts), 1 without a device.
    uint32_t msaa_samples(uint32_t requested) const;
    // [port] BM2b: undo the TAA step of the last render() drawn with RecordOptions::taa_provisional, so the next frame
    // jitters, reprojects and sees object motion as if it had not been drawn. True when there was one to undo; false after
    // a render() without the flag or without TAA, a reset_taa_history(), keep_taa_frame() or a discard already done.
    // keep_taa_frame(): that frame stands (drops what discard would restore).
    bool discard_taa_frame();
    void keep_taa_frame();
    // [port] FSR (RecordOptions::upscaler): loads the runtime on first call; true when FSR frames can be drawn here
    // (the context itself is created by the first FSR frame and can still fail - DrawStats::fsr). upscaler_status():
    // the provider in use ("FSR 4.1.2" style, once a frame created the context) or why FSR is off.
    bool fsr_supported();
    // [port] XeSS: the same for Upscaler::Xess. upscaler_status(u): that upscaler's runtime ("XeSS 2.0.2" style) or why
    // it is off.
    bool xess_supported();
    std::string upscaler_status(Upscaler u = Upscaler::Fsr) const;
    // Laser projectiles (lasers.hpp, PROJECTILE_NEBULA_DRAW.md 1): init_lasers loads W_Laser_Pill.tga /
    // p_particle_master.tga through `textures` and Engine\PrimAdditive; set_lasers gives the frame's lasers (copied),
    // drawn by render() in the Transparent bucket after its mesh items (and the hook's transparent draws) with the frame's
    // camera. Main scene only; no effect without init_lasers.
    bool init_lasers(const TextureSource& textures);
    void set_lasers(std::span<const Laser> lasers, const LaserConstants& constants = {}) {
        lasers_.assign(lasers.begin(), lasers.end());
        laser_k_ = constants;
    }
    bool lasers_ready() const;  // effect, pipeline inputs and both textures resident
    const LaserGeometry& last_laser_geometry() const { return laser_geo_; }
    // Ability beams (beams.hpp, ABILITY_VISUALS.md 2 / 4): init_beams loads Engine\PrimAdditive and the textures on demand
    // (w_galaxy_dot.tga, lightning_default.tga, each beam's ribbon texture) through `textures`; `lightning` (optional,
    // copied) = the LightningEffectTypes.xml effects (default: the shipped Tractor_Beam / Piet_Energy_Weapon). set_beams
    // gives the frame's beams (world endpoints) and the sim tick the frame shows: the per-beam animation (scroll, dots,
    // lightning bolts) advances once per tick. Drawn by render() in the Transparent bucket with the lasers (main scene only).
    bool init_beams(const TextureSource& textures, const LightningLibrary* lightning = nullptr);
    void set_beams(std::span<const BeamInstance> beams, uint64_t tick);
    bool beams_ready() const;  // effect and pipeline inputs ready (textures load on demand)
    void release_beams();      // drops the beam pass, its textures and buffers (GPU idle); set_beams then does nothing until init_beams
    const BeamGeometry& last_beam_geometry() const;
    // The draw list of the last draw()/render() (instance, submesh, pass, key, material), in order.
    const std::vector<DrawListEntry>& last_draw_list() const { return last_list_; }

    struct Built;      // per-frame draw list (render_internal.hpp)
    struct XTemplate;  // translated register images per (material, pass) (render_internal.hpp, internal)
    // ---- Bindless texture slots (rhi::BindlessTextureTable, slot 0 = white). destroy_model() releases a
    // model's slots with the frame ring's current value and defers its images (and its vertex / index
    // buffers) until the slots retire;
    // collect_released() (called by draw()/render() after begin_frame, or by the caller) retires them.
    // Without a frame ring (set_frame_ring(nullptr)) the caller guarantees an idle GPU at destroy time.
    void set_frame_ring(const rhi::FrameRing* ring) { ring_ = ring; }
    uint32_t collect_released();  // returns slots retired; destroys the images whose slots retired
    size_t buffers_pending();     // destroy_model() buffers waiting for their frames to retire
    uint32_t textures_resident() const { return table_.slots().live(); }
    uint32_t texture_slot_high_water() const { return table_.slots().high_water(); }
    uint32_t texture_slots_pending() const { return table_.slots().pending(); }
    // Allocates a slot for `view` (world textures); 0 (white) when the table is full.
    uint32_t allocate_texture_slot(VkImageView view, const char* what);
    // Releases `slot` (0 ignored) and, when given, hands `image` over for destruction after it retired.
    void release_texture(uint32_t slot, rhi::Image* image);

    // ---- Track B.
    bool dxc_available() const { return dxc_ok_; }
    const std::string& dxc_status() const { return dxc_status_; }
    // The translated program of a pass: nullptr until its translation job finished (the first call
    // queues it; one job per (vs_id, ps_id)). Thread-safe.
    const materials::TranslatedProgram* translated_program(const std::shared_ptr<const materials::MaterialEffect>& effect,
                                                           const materials::ResolvedPass& pass);
    // Q3: the instanced form of a translated MESH pass (TranslateOptions::instanced: storage-buffer register images,
    // one draw per run of identical items) - what the mesh path draws with; same rules. Post / world draws keep the
    // cbuffer form (translated_program).
    const materials::TranslatedProgram* translated_mesh_program(const std::shared_ptr<const materials::MaterialEffect>& effect,
                                                                const materials::ResolvedPass& pass);
    void wait_translations();
    using TranslationStats = Renderer::TranslationStats;
    TranslationStats translation_stats() const;
    // Shader issues met so far (docs/MODS.md shader section): desc.effects->issues() plus one TranslateFailed
    // per failed translation job (effect, path, translator / DXC message; the submesh draws unlit). Sorted by
    // (effect, kind); a load report or debug overlay lists them. Thread-safe.
    std::vector<materials::ShaderIssue> shader_issues() const;
    // Register images of one translated draw (what draw() uploads; tests feed them to the interpreter).
    bool translated_constants(const Instance& inst, const GpuSubmesh& g, const GpuPass& p, const ViewMatrices& view,
                              materials::VSConstants& vs, materials::PSConstants& ps, materials::IntBoolConstants* vs_ib = nullptr,
                              materials::IntBoolConstants* ps_ib = nullptr) const;
    // The same images through the path draws use: the pass's per-frame template (every binding filled once from
    // the frame inputs, per-draw values neutral), then only the per-draw parameters rewritten for `inst`. Must
    // equal translated_constants() bit for bit (tests).
    bool translated_constants_cached(const Instance& inst, const GpuSubmesh& g, const GpuPass& p, const ViewMatrices& view,
                                     materials::VSConstants& vs, materials::PSConstants& ps,
                                     materials::IntBoolConstants* vs_ib = nullptr, materials::IntBoolConstants* ps_ib = nullptr);
    // Pixel offset pushed to translated programs (D3D9 half-pixel convention under the Y-flipped viewport).
    static void translated_pixel_offset(uint32_t width, uint32_t height, float (&out)[4]);
    materials::PsoKey translated_key(const GpuSubmesh& g, const GpuPass& p, const materials::TranslatedProgram& tp, int variant) const;
    // The PsoKey a lit pass draws with: variant 0 (opaque) / 1 (fading) of the pass, render pass and
    // mesh vertex layout filled in; FF passes take the FF program of the MESH layout
    // (ff_program(ff_inputs_for_layout(g.layout))), so COLOR0 / rigid skinning follow the vertices.
    materials::PsoKey draw_key_variant(const GpuSubmesh& g, const GpuPass& p, int variant) const;
    // Same, variant chosen like ResolvedPass::select(light_scale_w).
    materials::PsoKey draw_key(const GpuSubmesh& g, const GpuPass& p, float light_scale_w) const;
    // Variant (0/1) the pass draws with for these per-draw inputs (ResolvedPass::select_exact).
    static int pass_variant(const GpuPass& p, const materials::ExprInputs& in);
    // Hot-set / FF pass: sampler slots s0..s3 (bit n) whose texture is the engine's CLOUD_TEXTURE (sampler
    // semantic, no material texture in tex[n]); draws bind the attached world's cloud texture there
    // (GpuPass::cloud_slots, the translated passes' rule).
    static uint8_t cloud_texture_slots(const materials::ResolvedPass& p, const int (&tex)[4]);

    // ---- Frame constants (SH lighting contract). SH lives in FrameCB (sph_all / sph_fill), not DrawData, so
    // every FrameCB bound to hot-set / FF draws (main list, shadow bucket, decorations / prim geometry, stencil
    // darken) is built by frame_constants() and uploaded through one checked path: camera (fill_frame_cb),
    // lights 0..2 + ambient, SH of the current lighting always; the FrameCbExtra bits add scene fog, distance
    // fade + shadow extrusion, and the world's cloud / FOW matrices.
    enum FrameCbExtra : uint32_t { kFcbFog = 1u, kFcbFade = 2u, kFcbWorld = 4u, kFcbScene = 7u };
    void frame_constants(const ViewMatrices& v, uint32_t extras, materials::FrameCB& out);
    // FrameCBs uploaded with zero SH while the lighting had non-zero SH (must stay 0; logged once).
    uint32_t frame_cb_violations() const { return fcb_violations_.load(); }
    // The contract itself: false when `lighting` has light (non-zero SH) but `cb` carries zero SH.
    static bool frame_cb_sh_ok(const materials::FrameCB& cb, const Lighting& lighting);

private:
    // SH of lighting_, cached per light set (sun, fills, ambient); frame thread only.
    const SceneLightConstants& scene_lights();
    struct SlcCache {
        bool valid = false;
        DirLight sun, fill[2], flash;  // vis1 t01: the lightning flash is part of the SH too
        assets::Float3 ambient{0, 0, 0};
        SceneLightConstants slc;
    };
    SlcCache slc_cache_;
    std::atomic<uint32_t> fcb_violations_{0};
    // Uploads `cb` (checked against the SH contract) into the frame ring; false when the ring is full.
    bool upload_frame_cb(rhi::Frame& frame, const materials::FrameCB& cb, rhi::BufferRange& out);
    bool translated_constants_slc(const Instance& inst, const GpuSubmesh& g, const GpuPass& p, const ViewMatrices& view,
                                  const SceneLightConstants& slc, materials::VSConstants& vs, materials::PSConstants& ps,
                                  materials::IntBoolConstants* vs_ib, materials::IntBoolConstants* ps_ib) const;
    // build() scratch reused across frames (frame thread; build() calls never overlap).
    struct SortKey {
        uint64_t k1, k2;
        uint32_t index;
    };
    std::vector<SortKey> sort_keys_, sort_tmp_;
    std::vector<uint64_t> rank_scratch_;
    struct BuildCache;  // per-model memo + build scratch (render_internal.hpp)
    // Per-frame engine parameters of translated draws (camera, lights, SH, fog, fade, cloud / FOW matrices).
    void engine_frame(const ViewMatrices& v, const SceneLightConstants& slc, EngineFrame& out) const;
    // Translated templates of a build: frame image slot, shared ring ranges / sets (false: ring or pool full).
    bool xt_prepare(XTemplate& t, rhi::Frame& frame, uint32_t thread, const EngineFrame& f, const float (&pixel_offset)[4],
                    bool fog);
    std::unique_ptr<BuildCache> bc_;
    bool sky_build_ = false;  // build_sky() in progress: its own memo set (other model set than the scene)
    // Draw lists reused across frames, one per build site (no per-frame allocation in steady state).
    enum BuiltSlot : int { kBuiltMain, kBuiltSky, kBuiltReflect, kBuiltReflectSky, kBuiltRefract, kBuiltCube, kBuiltDraw, kBuiltSlots };
    std::unique_ptr<Built> built_[kBuiltSlots];
    Built& built(BuiltSlot slot);
    std::vector<Instance> sky_storage_[3];  // build_sky instance copies: main, reflection, sky cube
    std::vector<std::pair<size_t, size_t>> chunks_;  // render(): secondary command buffer ranges
    std::vector<VkCommandBuffer> secs_;
    friend class WorldRenderer;
    bool create_unlit_pipelines();
    void destroy_unlit_pipelines();
    bool upload_texture(GpuTexture& t, const assets::TextureAsset& tex);
    void setup_lit(const assets::ModelAsset& model, const assets::Submesh& sm, GpuSubmesh& g,
                   const std::function<int(const std::string&, assets::TextureRole)>& add_texture);

    rhi::Context* ctx_ = nullptr;
    rhi::Allocator* alloc_ = nullptr;
    rhi::Uploader* up_ = nullptr;
    RendererDesc desc_{};
    Lighting lighting_ = Lighting::space_default();
    float time_ = 0.0f;
    materials::float4 fow_u_{}, fow_v_{};  // FOW_TEX_U/V of the attached world's playable box (0 without a world)
    VkImageView fow_view_ = VK_NULL_HANDLE;  // FOW_TEXTURE while the world's fog of war is on (null = white)
    uint32_t fow_slot_ = 0;                  // its bindless slot (FF / hot-set; 0 = white)
    // Lasers (init_lasers / set_lasers / draw_lasers in lasers.cpp).
    std::vector<Laser> lasers_;
    LaserConstants laser_k_;
    LaserGeometry laser_geo_;
    GpuTexture laser_tex_[2];  // beam pill, kite atlas
    std::shared_ptr<const materials::MaterialEffect> laser_fx_;
    const materials::ResolvedPass* laser_pass_ = nullptr;
    std::shared_ptr<const materials::FfDrawTemplate> laser_tpl_;
    rhi::Buffer laser_vb_[4];  // per frame-ring slot: vertices then indices
    VkDeviceSize laser_vb_size_[4] = {};
    bool lasers_init_ = false;
    void draw_lasers(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view, DrawStats& st);
    void shutdown_lasers();
    // Ability beams (beams.hpp; init_beams / set_beams / draw_beams in beams_draw.cpp).
    struct BeamRenderState;
    std::shared_ptr<BeamRenderState> beams_;
    bool beams_active_ = false;  // set_beams listed something this frame (or lines are still animating)
    void draw_beams(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view, DrawStats& st);
    void shutdown_beams();
    rhi::HotSetLayouts hot_;
    rhi::SamplerCache samplers_;
    // [port] Anisotropy (set_anisotropy): the request, the effective override latched by latch_anisotropy() at the
    // start of draw()/render() (what every recording thread reads), the override the bindless engine samplers were
    // last written with, and the frame number of the last draw()/render() (that set may be bound in it).
    uint32_t aniso_request_ = 0, aniso_ = 0, aniso_engine_ = 0;  // aniso_ / aniso_engine_: level | mip skip << 8 (convert.hpp)
    uint32_t mip_skip_request_ = 0;                              // set_texture_reduction
    std::unique_ptr<GammaPass> gamma_;                           // [port] created on first apply_gamma
    uint64_t aniso_frame_ = ~0ull;
    uint32_t aniso_cap() const;                    // device limit (1 without samplerAnisotropy)
    bool write_engine_samplers(uint32_t level);  // the EngineSampler array of bindless_
    void latch_anisotropy(const rhi::Frame& frame);
    rhi::PipelineCache unlit_cache_;
    PipelineManager psos_;
    rhi::DescriptorAllocator bindless_alloc_;
    VkDescriptorSet bindless_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline unlit_[2][3] = {};  // [skinned][BlendMode]
    rhi::Image white_;
    uint8_t render_pass_ = 0;
    rhi::BindlessTextureTable table_;
    const rhi::FrameRing* ring_ = nullptr;
    struct Deferred {
        rhi::Image image;
        uint64_t retire = 0;
    };
    std::vector<Deferred> deferred_images_;  // released, destroyed once the ring passes `retire`
    struct DeferredBuffer {
        rhi::Buffer buffer;
        uint64_t retire = 0;
    };
    std::vector<DeferredBuffer> deferred_buffers_;  // destroy_model() vertex/index buffers, same rule
    std::mutex deferred_mutex_;                     // guards deferred_images_ and deferred_buffers_
    void release_buffer(rhi::Buffer& b);
    std::vector<DrawListEntry> last_list_;
    // Frame graph state.
    struct Targets {
        rhi::RenderTarget scene, scene_tex, distortion, bloom_a, bloom_b;
        rhi::RenderTarget scene_ms;  // [port] MSAA scene colour + depth-stencil (msaa_ samples; empty when 1)
        bool ready = false;
    };
    Targets tg_;
    std::unique_ptr<SsaoPass> ssao_;  // [port] created on first use (RecordOptions::ambient_occlusion)
    std::unique_ptr<SmaaPass> smaa_;  // [port] created on first use (RecordOptions::smaa)
    std::unique_ptr<TaaPass> taa_;    // [port] created on first use (RecordOptions::taa)
    // taa.cpp: jittered camera (or `view`); fsr_phases > 0: FSR's jitter sequence of that length (FSR frame).
    ViewMatrices taa_begin(const ViewMatrices& view, const RecordOptions& opts, uint32_t fsr_phases = 0);
    std::unique_ptr<FsrPass> fsr_;   // [port] created on first use (RecordOptions::upscaler)
    std::unique_ptr<XessPass> xess_;  // [port] created on first use (RecordOptions::upscaler = Upscaler::Xess)
    // FSR / XeSS hooks in taa.cpp: true when this frame is drawn at `render` and upscaled by opts.upscaler.
    bool fsr_begin(VkExtent2D display, const RecordOptions& opts, VkExtent2D& render);
    VkFormat ds_format_ = VK_FORMAT_UNDEFINED;        // scene depth-stencil (D32FS8 here)
    uint8_t rp_scene_ = 0, rp_external_ = 0, rp_post_ = 0;
    // [port] MSAA: rp_main_ = the main scene's render pass (rp_scene_ with msaa_ samples; == rp_scene_ at 1 sample), the
    // class render_pass_ holds outside the single-sample world prepass and draw(); rp_world_ = the class the world passes
    // (terrain, decorations, water) build their pipelines for: rp_scene_ in the prepass, rp_main_ in the main scene.
    uint8_t rp_main_ = 0, rp_world_ = 0;
    uint32_t msaa_ = 1;
    VkPipeline unlit_ms_[2][3] = {};  // unlit fallback for rp_main_ when msaa_ > 1
    bool set_msaa(uint32_t samples);  // rp_main_ / unlit_ms_ / scene_ms for `samples` (GPU idle when it changes)
    bool external_ = false;                           // draw(): caller's attachment formats
    VkPipeline unlit_int_[2][3] = {};
    rhi::Buffer quad_vb_;                             // 6 FLOAT2 vertices at +-1.005 (TERRAIN_WATER_POST.md 4.2)
    rhi::Buffer prim_vb_;                             // per frame slot: 6 pre-lit prim vertices (stencil darken)
    rhi::Image white_3d_;
    std::shared_ptr<const materials::MaterialEffect> fx_post_[5];  // darken, darken-to-alpha, final blur, bloom, heat
    bool ensure_targets(VkExtent2D extent);
    const materials::MaterialEffect* post_effect(int which);
    struct PostSpec;
    struct GeoState;
    // One translated draw outside the mesh path (full-screen post passes, prim passes, terrain / water
    // patches): register files from the effect + engine semantics + PostSpec values, combined samplers
    // bound by texture parameter name / semantic, the half-pixel push offset. False while compiling.
    bool post_translated(rhi::Frame& frame, VkCommandBuffer cmd, const PostSpec& s, XConstCache* cache = nullptr);
    // The pieces of post_translated (shared with the batched world draws):
    //   program + pipeline + samplers (cached in `cache` per (pass, generation)), push block;
    bool translated_state(const PostSpec& s, XConstCache* cache, GeoState& out);
    //   register images into cc (full fill, or only PostSpec::patch_values when the cache is valid);
    void translated_fill(const PostSpec& s, XConstCache& cc, bool cached, bool int_bool);
    //   combined sampler bindings of set 1 (by texture parameter name / semantic, white defaults).
    uint32_t translated_textures(const PostSpec& s, const GeoState& g, rhi::TextureBinding* out) const;
    // Q3: register images of the uncached post_translated() calls (post chain, prim decorations), keyed by a digest of
    // every input (pass, spec values, engine frame): an unchanged input reuses its images (no fill, no allocation).
    struct PostCacheEntry;
    std::vector<std::unique_ptr<PostCacheEntry>> post_cache_;
    uint32_t post_cache_clock_ = 0;
    XConstCache& post_cache_entry(const PostSpec& s, bool& hit);
    WorldBindStats* bind_stats_ = nullptr;  // world draws: counts post_translated's commands while set
    // Background objects (camera-relative, near 10 / far 100000) of `view`.
    bool build_sky(rhi::Frame& frame, const ViewMatrices& view, std::span<const Instance> sky, DrawMode mode,
                   const RecordOptions& opts, Built& out, std::vector<Instance>& storage, DrawStats& st);
    SceneHook* scene_hook_ = nullptr;  // R4 particles
    // World hooks (world.cpp).
    WorldRenderer* world_ = nullptr;
    materials::float4 cloud_u_{0, 0, 0, 0}, cloud_v_{0, 0, 0, 0};  // CLOUD_TEX_U / V (world clouds; 0 = none)
    VkImageView cloud_view_ = VK_NULL_HANDLE;                        // CLOUD_TEXTURE of the world (null = white)
    uint32_t cloud_slot_ = 0;                                        // its bindless slot (hot-set / FF; 0 = white)
    float fade_begin_ = -1.0f, fade_end_ = -1.0f;
    void world_frame_constants();  // cloud matrices + texture of the attached world (before any build)
    void reset_world_constants();  // no world: white CLOUD_TEXTURE / FOW_TEXTURE, zero matrices
    bool world_prepass(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view, std::span<const Instance> instances,
                       std::span<const Instance> sky, DrawMode mode, const RecordOptions& opts);
    void world_draw_terrain(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view, uint32_t width, uint32_t height,
                            bool mirrored, bool count);
    void world_draw_water(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view, uint32_t width, uint32_t height);
    // Decorations (decorations.cpp): bucket-0 types 3, 2, 1, 0, 4 after the terrain patches; type 5 after the
    // water; the space FOW plane in the background pass (`view` = the background camera).
    void world_draw_decorations(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view, uint32_t width, uint32_t height);
    void world_draw_water_tracks(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view, uint32_t width, uint32_t height);
    void world_draw_space_fow(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view, uint32_t width, uint32_t height);
    struct FfGeoDraw;
    // Fixed-function draws of render-owned geometry (prim effects): one FrameCB / set 0 for the list, per draw
    // DrawData (identity world) + FfDrawData; returns the draws recorded (pipelines still compiling are skipped).
    uint32_t ff_geometry_draws(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view, std::span<const FfGeoDraw> draws,
                               uint32_t* pipeline_binds = nullptr);
    // Binds the state every draw of a batched world call shares (pipeline, dynamic state, push block,
    // vertex / index buffers).
    void bind_geometry_state(VkCommandBuffer cmd, const PostSpec& s, const GeoState& g, VkBuffer vb, VkBuffer ib);
    // reset_alpha: the soft path's pre-pass writing alpha 1 over the whole frame (colour write A only, no stencil test).
    bool post_darken(rhi::Frame& frame, VkCommandBuffer cmd, const materials::MaterialEffect& fx, uint32_t width, uint32_t height,
                     bool reset_alpha = false);
    // Track B state.
    std::unique_ptr<shaderxlat::RuntimeDxc> dxc_;
    std::unique_ptr<shaderxlat::ShaderCache> scache_;
    bool dxc_ok_ = false;
    std::string dxc_status_;
    rhi::StandardLayouts std_layouts_;
    VkPipelineLayout tlayout_[4] = {};  // translated: [int/bool constants | 2 x Q3 instanced form]
    rhi::Image white_cube_;
    rhi::Buffer zero_vb_;               // binding 1 for declared-but-unfed translated inputs
    struct XlatEntry {
        std::atomic<int> state{0};  // 0 pending, 1 ready, 2 failed
        std::shared_ptr<const materials::TranslatedProgram> tp;
        std::string error;
        std::string effect, path;   // the effect that first asked (shader_issues())
        double ms = 0;
    };
    mutable std::mutex xlat_mutex_;
    std::unordered_map<uint64_t, std::unique_ptr<XlatEntry>> xlat_;
    std::unique_ptr<jobs::JobCounter> xlat_counter_;
    const materials::TranslatedProgram* find_translated(uint32_t vs_id, uint32_t ps_id) const;
    const materials::TranslatedProgram* translated_program_impl(const std::shared_ptr<const materials::MaterialEffect>& effect,
                                                                const materials::ResolvedPass& pass, bool instanced);
    VkPipelineLayout translated_layout(const materials::TranslatedProgram& tp) const {
        return tlayout_[((tp.vs_int_bool || tp.ps_int_bool) ? 1 : 0) + (tp.vs.iface.instanced ? 2 : 0)];
    }
    bool build(rhi::Frame& frame, const ViewMatrices& view, std::span<const Instance> instances, DrawMode mode,
               const RecordOptions& opts, Built& out, DrawStats& st, bool main_list = true);
    void record_range(VkCommandBuffer cmd, const Built& b, size_t begin, size_t end, uint32_t width, uint32_t height,
                      bool set_viewport) const;
};

// The core of a Renderer (friend of Renderer): engine/render's sources, tests and tool app only.
inline RendererCore& renderer_core(Renderer& r) { return *r.core_; }
inline const RendererCore& renderer_core(const Renderer& r) { return *r.core_; }

}  // namespace alamo::render
