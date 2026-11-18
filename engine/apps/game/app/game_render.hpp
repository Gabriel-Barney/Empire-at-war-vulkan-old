// alamo_game render side (P host): the GPU objects shared by every scene, the galactic map view (R2) and the battle
// view (R1 + R3 + R4 + R5 over a running S10 battle). Both views only read the sim through immutable snapshots taken
// between ticks (capture*), so the per-tick CRC is the same with presentation on or off.
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/assets/asset_cache.hpp"
#include "alamo/game/battle_fx.hpp"  // S8 t31: lobbing charge-up cues
#include "alamo/game/game_data.hpp"
#include "alamo/game/game_session.hpp"
#include "alamo/game/script_cinematics.hpp"  // mcx6 t05
#include "alamo/jobs/job_system.hpp"
#include "alamo/render/fleet.hpp"
#include "alamo/render/ability_fx.hpp"  // vis1 t06 ability target / status particles
#include "alamo/render/beams.hpp"       // R5 ability beams
#include "alamo/render/blob_shadows.hpp"  // vis1 t02 object blob shadows
#include "alamo/render/fog_view.hpp"
#include "alamo/render/galactic/galactic_snapshot.hpp"
#include "alamo/render/galactic/galactic_view.hpp"
#include "alamo/render/galactic/planet_portrait.hpp"
#include "alamo/render/jam_fx.hpp"  // R5 sensor jamming blobs
#include "alamo/render/map_scene.hpp"
#include "alamo/render/renderer.hpp"
#include "alamo/render/scene_anim.hpp"  // R3/R5 render: T6 scene models
#include "alamo/render/sim_beams.hpp"
#include "alamo/render/sim_decorations.hpp"
#include "alamo/render/sim_snapshot.hpp"
#include "alamo/render/sim_view.hpp"
#include "alamo/render/surface_fx.hpp"
#include "alamo/render/weather_fx.hpp"  // vis1 t01 weather emitter / lightning / fog plane
#include "alamo/rhi/rhi.hpp"
#include "alamo/ui/camera.hpp"
#include "alamo/ui/hud_renderer.hpp"
#include "alamo/ui/picking.hpp"
#include "alamo/ui/selection_blob.hpp"
#ifdef ALAMO_RENDER_HAVE_ANIM_DRIVER
#include "alamo/render/anim/anim_pose_cache.hpp"
#endif
#ifdef ALAMO_PARTICLES_HAVE_SIM
#include "alamo/render/particles/particle_attach.hpp"
#include "alamo/render/particles/particle_data.hpp"
#include "alamo/render/particles/particle_gpu.hpp"
#include "alamo/render/particles/particle_sim.hpp"
#include "alamo/render/particles/particle_sim_attach.hpp"
#endif

namespace alamo::game {

inline constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
inline constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

// Per frame-in-flight colour (RGBA8, world + HUD) and depth (HUD 3D shells) images; one readback buffer for PNGs.
struct FrameTargets {
    struct Slot {
        rhi::Image color, depth;
    };
    std::vector<Slot> slots;
    rhi::Buffer readback;
    uint32_t width = 0, height = 0;
    bool create(rhi::Allocator& a, uint32_t slots, uint32_t w, uint32_t h);
    void destroy(rhi::Allocator& a);
};

// Renderer + HUD pass + asset streaming, shared by the galactic and battle views.
class GpuCore {
public:
    bool init(rhi::Context& ctx, const GameData& data, jobs::JobSystem& js, const std::string& cache_dir, std::string* err);
    void shutdown();
    rhi::Context* ctx = nullptr;
    rhi::Allocator alloc;
    rhi::Uploader up;
    rhi::FrameRing ring;
    render::Renderer renderer;
    ui::HudRenderer hud;
    std::unique_ptr<render::EffectCache> effects;
    std::unique_ptr<assets::AssetCache> cache;
    std::unique_ptr<render::ModelStreamer> streamer;
    std::unique_ptr<render::GameTypeResolver> resolver;
    jobs::JobSystem* js = nullptr;
    const GameData* data = nullptr;
};

// ---- galactic map -------------------------------------------------------------------------------------------
class GalacticView {
public:
    void init(GpuCore& gpu, const GameData& data, std::vector<render::FactionColor> colors, render::RouteLineStyle style);
    void attach(const galactic::GalacticSim& sim, int32_t local_player);  // palette, builder, camera start
    void capture(const sim::SimHost& host);                               // after every tick (const reads)
    // frame thread (now_s: wall clock, particles). GALAXY_MAP_PRESENTATION §3: `selected_planet` (planet object id, 0 =
    // none) and the camera (`cam`, null = the builder's default) drive the selection model (GalacticSceneBuilder::set_selection).
    void prepare(float alpha, double now_s = -1.0, uint32_t selected_planet = 0, const ui::CameraParams* cam = nullptr);
    void use_scene(GpuCore& gpu);                                         // lighting / fade / hooks for galactic frames
    void destroy();
    bool attached() const { return builder_ != nullptr; }
    // Particle detail option (0..1, ANIM_PARTICLES B.4): active slots of every emitter (presentation only).
    void set_particle_detail(float detail) {
        if (pworld_) pworld_->set_detail(detail);
    }

    ui::GalacticCameraController& camera() { return camera_; }
    // ui4 t06: the fleet drag preview segments of the next prepare() (GalacticSceneBuilder::set_drag_lines).
    void set_drag_lines(std::vector<render::GalacticDragLine> lines) {
        if (builder_) builder_->set_drag_lines(std::move(lines));
    }
    // The camera's planet box (ui::galactic_scroll_box; the camera widens it by 15 %).
    void set_camera_bounds(const ui::CameraBox& b) { camera_.set_bounds(b); }
    ui::CameraParams camera_params(uint32_t w, uint32_t h) const;
    const render::GalacticFrame& frame() const { return frame_; }
    const render::GalacticTypeTable& types() const { return types_; }
    const render::GalacticPalette& palette() const { return palette_; }
    const render::GalacticSceneBuilder* builder() const { return builder_.get(); }
    // Picking: planets (planet radius) and fleets (at their draw position) of the current frame.
    std::vector<ui::PickSphere> pick_items() const;
    Vec3 fleet_position(const render::GalacticFleetSnap& f) const;
    // Object id of the planet the fleet `fleet_id` orbits this frame (0: in transit / unknown).
    uint32_t fleet_planet_id(uint32_t fleet_id) const;
    float planet_radius(objects::TypeIndex t) const;
    bool settled() const;
    // Planet portraits ("planet_portrait:<type>" image pages: Planet Info IDC_PLANET_ICON, the battle load screen's
    // planet icon). Own GalacticModels so they also render while the galaxy is not drawn (battle load). Frame thread,
    // after Uploader::record_acquires and outside any render pass.
    void record_portraits(rhi::Frame& frame, VkCommandBuffer cmd);
    render::PlanetPortraits* portraits() { return portraits_.get(); }

    std::vector<render::Instance> main, background;
    render::RecordOptions ropt;

private:
    render::GalacticTypeTable types_;
    render::GalacticPalette palette_;
    render::GalacticSnapshotBuffer buffer_;
    std::unique_ptr<render::GalacticModels> models_;
    std::unique_ptr<render::GalacticSceneBuilder> builder_;
    std::unique_ptr<render::GalacticModels> portrait_models_;
    std::unique_ptr<render::PlanetPortraits> portraits_;
    render::GalacticFrame frame_;
    std::vector<render::FactionColor> colors_;
    render::RouteLineStyle style_;
    render::CorruptionPathSettings corruption_path_;  // FIXPASS_GALACTIC §3.2 (GameConstants)
    float cycle_speed_ = 5.0f, cycle_darken_ = 0.5f;   // mcx4 t14: GUI_Cycle_Speed / GUI_Darken_Level
    uint32_t cycle_planet_ = 0;                        // the selection the cycle started for
    double cycle_t0_ = 0.0;
    ui::GalacticCameraController camera_;
    GpuCore* gpu_ = nullptr;
    int32_t local_ = -1;  // fog viewpoint (attach)
    // Underworld corruption smoke (render agent host ask 3): the galactic map's own particle world; the effects of
    // galactic_corruption_effects are synced by key every frame (each Particle type's Galactic_Model_Name).
    render::GalacticCorruptionSettings corruption_;
    std::vector<render::GalacticCorruptionEffect> corruption_fx_;
    double last_frame_s_ = -1.0;
#ifdef ALAMO_PARTICLES_HAVE_SIM
    std::unique_ptr<render::particles::ParticleWorld> pworld_;
    std::unique_ptr<render::particles::ParticleLibrary> plib_;
    render::particles::ParticleRenderer prender_;
    std::unique_ptr<render::particles::ProxyAttachments> pattach_;
#endif
};

// ---- battle -------------------------------------------------------------------------------------------------
struct BattleViewStats {
    // corpses: detached corpses + drawable death-clone objects, summed over captures.
    uint32_t objects = 0, drawn = 0, fog_hidden = 0, effects = 0, corpses = 0;
    uint64_t particles_spawned = 0;
    bool fog = false, fog_drawn = false;
    uint64_t decals = 0;
    uint64_t muzzles = 0;  // muzzle flashes handed to the snapshots (sum over captures; R3 attack row)
    uint32_t lasers_peak = 0;  // most laser projectiles handed to the renderer in one frame (playtest R1)
    // q6 t02: most model projectiles (Projectile_Custom_Render 0, e.g. Thrawn's Revenge's Proj_Space_Laser_*.alo) drawn in one frame
    uint32_t model_projectiles_peak = 0;
};

class BattleView {
public:
    // Loads the battle's map scene (terrain / water / sky; placements off - the battle's objects replace them).
    bool start(GpuCore& gpu, const GameData& data, const GameSession& session, uint32_t width, uint32_t height, std::string* err);
    void stop(GpuCore& gpu);
    bool active() const { return scene_ != nullptr; }
    // Particle detail option (0..1, ANIM_PARTICLES B.4): active slots of every emitter (presentation only).
    void set_particle_detail(float detail) {
        if (pworld_) pworld_->set_detail(detail);
    }
    // vis1 t02: shadow detail of the object blob shadows (render::blob_shadow_mode; 0 = Off, kPortShadowDetailOn = On).
    void set_shadow_detail(int32_t detail) { shadow_detail_ = detail; }
    // vis1 t06: GameConstants Object_Visual_Status_Particle_Attach_Bone_Names (stun / infection particle bones).
    void set_status_particle_bones(std::string bones) { status_bones_ = std::move(bones); }
    // vis1 t01: the weather the last capture read (land battles; inactive otherwise).
    const render::WeatherView& weather() const { return weather_; }
    // Scripted runs: every model the frame requests is loaded and resident before it is built (ModelStreamer::
    // finish_pending), so a new type's first drawn frame and its particles do not depend on load timing; likewise each
    // snapshot's fog texture (WorldRenderer::finish_fow_upload).
    void set_blocking_models(bool on) { blocking_models_ = on; }
    void capture(const GameSession& session);  // after every battle tick (const reads)
    void pre_frame();  // before begin_frame (fog uploads)
    void prepare(GpuCore& gpu, float alpha);
    void use_scene(GpuCore& gpu);
    // World bounding radius of a type (drawn bounding sphere of its model x Scale_Factor); 0 until the model is loaded.
    float type_radius(objects::TypeIndex t);
    // refixf t05: half-extents of the type's drawn model box x Scale_Factor (TSW firing shot E, TSW_PLANET_DESTROY.md §11.2
    // [M: the model box]); zero until the model is loaded.
    ui::Vec3 type_half_extents(objects::TypeIndex t);
    float load_progress() const;
    bool settled(const render::DrawStats& ds) const;

    ui::TacticalCameraController& camera() { return camera_; }
    ui::CameraParams camera_params(uint32_t w, uint32_t h) const;
    const render::InterpolatedFrame& frame() const { return frame_; }
    const render::SnapshotTypeTable& table() const { return table_; }
    const render::MapScene* scene() const { return scene_.get(); }
    bool space() const { return space_; }
    const BattleViewStats& stats() const { return stats_; }
    float clear_color(int i) const;
    // P2 selection circles of this frame (TacticalHud::selection_blobs): blob instances in the main list, one model per
    // ShadowBlobMaterials entry (vis1 t03: the blob's own material, UI.md 1.3; unknown / empty: the first Selection*
    // material); land: on the terrain under the unit (ui::selection_blob_ground_instance). Set before prepare().
    void set_selection_blobs(std::span<const ui::SelectionBlob> blobs) { blobs_.assign(blobs.begin(), blobs.end()); }
    // vis1 t08: the local player's selected object ids (ascending; empty in a cinematic / outside the battle screen): the
    // Force_Healing range blob is drawn under a selected owner (also while an ability target is picked). Set before prepare().
    void set_heal_selection(std::vector<uint32_t> ids) { heal_sel_ = std::move(ids); }
    // mcx4 t14: the object under the cursor (0 = none), brightened by Mouse_Over_Highlight_Scale. Set before prepare().
    void set_hover(uint32_t id) { hover_ = id; }
    // mcx4 t15 (MCX4_PRES_RE_ANSWERS_2 Q7): start an object light effect at wall time `now`, unless one is running on
    // the object (0x48e4d0: a running pulse is not replaced). set_pulse_time sets the clock of the next prepare().
    void start_light_pulse(uint32_t id, render::LightPulse p, double now);
    void set_pulse_time(double now) { pulse_now_ = now; }
    float shield_flash_duration() const { return shield_flash_duration_; }
    const float* shield_flash_color() const { return shield_flash_color_; }
    // cu9g t03: the bombing-run targeting blob of this frame (HUD_GAPS §9.5; nullopt = none). Set before prepare().
    void set_bombing_blob(const std::optional<render::JamBlob>& blob) { bombing_blob_ = blob; }
    // ui2 t08 (STORY_PRESENTATION_LUA.md 4.1 / 4.2): the story script's Play_Lightning_Effect bolts of this frame (extra beams
    // drawn with the ability beams, lightning only) and its Enable_Fog / Enable_Distance_Fog switches (scene fog off when either
    // is false; the environment's own setting decides otherwise). Presentation only.
    void set_script_beams(std::vector<render::BeamInstance> beams) { script_beams_ = std::move(beams); }
    void set_fog_switches(bool master, bool distance) { fog_master_ = master; fog_distance_ = distance; }
    const render::FogRenderer& fog() const { return fog_; }
    // --- R3/R5 render ---
    // Space reinforcement overlay (palette 3 + W_Space_Reinforce_FOW_Grid): on while the local player picks a space
    // reinforcement drop point (game_main: TacticalHud BeginReinforcement until the drop / cancel).
    void set_space_reinforcement_overlay(bool on);
    // --- end R3/R5 render ---
    // P1 move acknowledgement (P1_DOUBLE_CLICK_MOVE.md §2.3; presentation only): spawns the particle object type `type`
    // (a GameConstants GUI_*_Command_Ack_Effect value: the type's model for this mode, else `type` as a particle file) at
    // `p` scaled by `scale` (GUI_Move_Acknowledge_Scale_Land / _Space). `normal`: a GUI_Move_Command_Ack_Effect object (the
    // kind a double-click kills). False when nothing was spawned (unknown type, no particle renderer).
    bool spawn_move_ack(std::string_view type, ui::Vec3 p, float scale, bool normal);
    // The nearest live normal move ack to `p` (no radius) is removed silently (hidden, no linger). False when none.
    bool kill_nearest_move_ack(ui::Vec3 p);

    std::vector<render::Instance> main, sky;
    render::RecordOptions ropt;

private:
    // S8 t31 (TACTICAL_COMBAT.md "Charge-up"): the charging lobbing units' Lobbing_Superweapon_Chargeup_Particle on
    // Lobbing_Superweapon_Chargeup_Particle_Bone_Name as attached effects (percent kEffectLobChargeup).
    void append_lob_chargeups(const sim::TacticalBattle& battle, render::SimSnapshot& snap);

    GpuCore* gpu_ = nullptr;
    const objects::GameObjectTypes* types_ = nullptr;  // S8 t31: the lobbing XML tags
    std::vector<TswCue> lob_chargeups_;               // S8 t31: track_lob_chargeups (battle_fx.hpp)
    render::AbilityFxTracker ability_fx_;             // vis1 t06 (ability_fx.hpp)
    std::string status_bones_ = "B_HEAD, Turret_00, B_Turret_00, B_Turret, B_Turret_Base, ROOT";  // vis1 t06 (vanilla value)
    // cu8g t03: the battle's active environment at the last capture (TacticalBattle::environment_index); prepare()
    // re-applies it on the scene (MapScene::set_environment) and the particle wind when it changed.
    uint32_t env_wanted_ = 0;
    // mcx6 t05: the story's Show_Emitter state at the last capture (lua::PresentationState::emitters as particle group bits,
    // game::emitter_overrides); the builder's emitter source reads it.
    std::map<objects::ObjectId, game::EmitterGroups> emitter_groups_;
    std::vector<render::BeamInstance> script_beams_;  // ui2 t08
    bool fog_master_ = true, fog_distance_ = true;     // ui2 t08
    void apply_environment();
    // vis1 t01 (WEATHER_VISUALS §1): the scenario emitter in the particle world, its intensity as the Detail scalar.
    void sync_weather_emitter();
    render::WeatherView weather_;
    render::LightningFlash lightning_;
    std::chrono::steady_clock::time_point last_prepare_{};
    bool have_last_prepare_ = false;
    uint64_t weather_fx_ = 0;  // ParticleWorld::Handle of the weather emitter (0 = none)
    std::string weather_fx_name_;
    // vis1 t02: object blob shadows (land).
    render::BlobShadowBuilder blob_shadows_;
    std::vector<terrain::ShadowBlob> shadow_blobs_;
    int32_t shadow_detail_ = render::kPortShadowDetailOn;
    bool space_ = true;
    bool blocking_models_ = false;
    int32_t local_ = 0;
    render::SnapshotTypeTable table_;
    render::SnapshotBuffer buffer_;
    std::unique_ptr<render::MapScene> scene_;
    std::unique_ptr<render::StreamedSimModels> models_;
    std::unique_ptr<render::SimInstanceBuilder> builder_;
    render::InterpolatedFrame frame_;
    std::vector<render::SimWorldEffect> world_fx_;  // vis1 t08: decoration effects + the frame's (shield penetration)
    std::vector<uint32_t> heal_sel_;                  // vis1 t08: set_heal_selection
    uint32_t hover_ = 0;                              // mcx4 t14: set_hover
    std::map<uint64_t, render::LightPulse> pulses_;   // mcx4 t15: running object light effects
    double pulse_now_ = 0.0;
    float shield_flash_duration_ = 0.1f;              // GameConstants Shield_Flash_Duration
    float shield_flash_color_[3] = {1.0f, 1.1f, 1.25f};  // GameConstants Shield_Flash_Scale
    float hover_scale_ = 1.5f;                        // GameConstants Mouse_Over_Highlight_Scale
    std::vector<render::Instance> bg_;
    render::SimInstanceStats istats_;
    ui::TacticalCameraController camera_;
    render::FogRenderer fog_;
    render::SurfaceFxLibrary surface_fx_;
    std::unique_ptr<render::SimDecorations> decorations_;
    std::unique_ptr<render::SceneModelAnimator> scene_anim_;  // R3/R5 render: Model_To_Attach idle clips (T6)
    render::SimInstanceBuilder::PoseSource pose_fn_;
#ifdef ALAMO_RENDER_HAVE_ANIM_DRIVER
    render::AnimPoseCache poses_;
#endif
#ifdef ALAMO_PARTICLES_HAVE_SIM
    std::unique_ptr<render::particles::ParticleWorld> pworld_;
    std::unique_ptr<render::particles::ParticleLibrary> plib_;
    render::particles::ParticleRenderer prender_;
    std::unique_ptr<render::particles::ProxyAttachments> pattach_;
    render::particles::SimAttachOptions popt_;
#endif
    struct MoveAckFx {
        uint64_t handle = 0;  // ParticleWorld::Handle
        ui::Vec3 p{};
        bool normal = false;
    };
    std::vector<MoveAckFx> acks_;
    std::vector<ui::SelectionBlob> blobs_;
    // vis1 t03: selection blob models, uploaded on first use per material (index into blob_mats_; ok = false: the upload
    // failed, not retried).
    struct SelectionModel {
        uint32_t material = 0;
        render::GpuModel model;
        bool ok = false;
    };
    std::vector<terrain::ShadowBlobMaterial> blob_mats_;
    uint32_t blob_fallback_ = UINT32_MAX;  // the first Selection* material (UINT32_MAX: none)
    std::vector<std::unique_ptr<SelectionModel>> sel_models_;
    render::GpuModel* selection_model(GpuCore& gpu, const std::string& material);
    std::optional<render::JamBlob> bombing_blob_;  // cu9g t03: the land bombing-run targeting blob (drawn through jam_models_)
    render::JamBlobModels jam_models_;  // R5 sensor jamming radius blobs (one model per material + colour)
    render::BeamStyleTable beam_styles_;  // R5 ability beams: per-special styles over the GameConstants fallbacks (ABILITY_VISUALS 4.2)
    render::BeamBoneFn beam_bone_;        // bone world positions (HP_trac_bone, hardpoint / muzzle bones)
    bool beams_on_ = false;               // the renderer's beam pass is initialised for this battle
    render::LaserConstants laser_k_;      // GameConstants Laser_Beam / Kite_Z_Scale_Factor
    uint64_t last_tick_ = 0, fog_tick_ = ~uint64_t{0};
    uint32_t pending_ticks_ = 0;
    bool captured_any_ = false;
    BattleViewStats stats_;
};

}  // namespace alamo::game
