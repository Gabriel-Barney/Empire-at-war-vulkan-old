// alamo_render - the public Renderer (alamo/render/renderer.hpp): every call forwards to the RendererCore it owns
// (renderer_core.hpp, REF3 t07). No behaviour of its own.
#include "alamo/render/renderer.hpp"

#include "renderer_core.hpp"

namespace alamo::render {

Renderer::Renderer() : core_(std::make_unique<RendererCore>()) {}
Renderer::~Renderer() = default;

bool Renderer::init(rhi::Context& ctx, rhi::Allocator& alloc, rhi::Uploader& up, const RendererDesc& desc) {
    return core_->init(ctx, alloc, up, desc);
}
void Renderer::shutdown() { core_->shutdown(); }
bool Renderer::set_color_format(VkFormat format) { return core_->set_color_format(format); }
VkFormat Renderer::color_format() const { return core_->color_format(); }
VkFormat Renderer::depth_format() const { return core_->depth_format(); }
void Renderer::set_lighting(const Lighting& l) { core_->set_lighting(l); }
const Lighting& Renderer::lighting() const { return core_->lighting(); }
void Renderer::set_time(float seconds) { core_->set_time(seconds); }
void Renderer::set_anisotropy(uint32_t level) { core_->set_anisotropy(level); }
void Renderer::set_texture_reduction(uint32_t mips) { core_->set_texture_reduction(mips); }
bool Renderer::apply_gamma(rhi::Frame& frame, VkCommandBuffer cmd, VkImage color, VkImageView view, VkFormat format,
                           VkExtent2D extent, rhi::ImageState state, rhi::ImageState after, float gamma) {
    return core_->apply_gamma(frame, cmd, color, view, format, extent, state, after, gamma);
}

bool Renderer::upload_model(std::shared_ptr<const assets::ModelAsset> model, const TextureSource& textures, GpuModel& out,
                            jobs::JobSystem* js, std::string* error) {
    return core_->upload_model(std::move(model), textures, out, js, error);
}
bool Renderer::make_resident(GpuModel& m) { return core_->make_resident(m); }
bool Renderer::finish_uploads(const GpuModel& m) { return core_->finish_uploads(m); }
void Renderer::destroy_model(GpuModel& m) { core_->destroy_model(m); }
void Renderer::prepare_pipelines(const GpuModel& m, float light_scale_w) { core_->prepare_pipelines(m, light_scale_w); }

bool Renderer::draw(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view, std::span<const Instance> instances,
                    DrawMode mode, DrawStats* stats) {
    return core_->draw(frame, cmd, view, instances, mode, stats);
}
bool Renderer::draw(rhi::Frame& frame, VkCommandBuffer cmd, const ViewMatrices& view, std::span<const Instance> instances,
                    DrawMode mode, const RecordOptions& opts, DrawStats* stats) {
    return core_->draw(frame, cmd, view, instances, mode, opts, stats);
}
bool Renderer::render(rhi::Frame& frame, VkCommandBuffer primary, const PassTarget& target, const ViewMatrices& view,
                      std::span<const Instance> instances, DrawMode mode, const RecordOptions& opts, DrawStats* stats,
                      DrawTiming* timing, std::span<const Instance> sky) {
    return core_->render(frame, primary, target, view, instances, mode, opts, stats, timing, sky);
}
void Renderer::set_world(WorldRenderer* w) { core_->set_world(w); }
WorldRenderer* Renderer::world() const { return core_->world(); }
void Renderer::set_object_fade(float begin, float end) { core_->set_object_fade(begin, end); }
void Renderer::set_scene_hook(SceneHook* h) { core_->set_scene_hook(h); }

void Renderer::reset_taa_history() { core_->reset_taa_history(); }
bool Renderer::discard_taa_frame() { return core_->discard_taa_frame(); }
void Renderer::keep_taa_frame() { core_->keep_taa_frame(); }
uint32_t Renderer::msaa_samples(uint32_t requested) const { return core_->msaa_samples(requested); }
bool Renderer::fsr_supported() { return core_->fsr_supported(); }
bool Renderer::xess_supported() { return core_->xess_supported(); }
std::string Renderer::upscaler_status(Upscaler u) const { return core_->upscaler_status(u); }

bool Renderer::init_lasers(const TextureSource& textures) { return core_->init_lasers(textures); }
void Renderer::set_lasers(std::span<const Laser> lasers, const LaserConstants& constants) { core_->set_lasers(lasers, constants); }
bool Renderer::init_beams(const TextureSource& textures, const LightningLibrary* lightning) {
    return core_->init_beams(textures, lightning);
}
void Renderer::set_beams(std::span<const BeamInstance> beams, uint64_t tick) { core_->set_beams(beams, tick); }
void Renderer::release_beams() { core_->release_beams(); }

void Renderer::set_frame_ring(const rhi::FrameRing* ring) { core_->set_frame_ring(ring); }
uint32_t Renderer::collect_released() { return core_->collect_released(); }
uint32_t Renderer::textures_resident() const { return core_->textures_resident(); }
uint32_t Renderer::texture_slot_high_water() const { return core_->texture_slot_high_water(); }
uint32_t Renderer::texture_slots_pending() const { return core_->texture_slots_pending(); }

void Renderer::wait_pipelines() { core_->pipelines().wait_idle(); }
PipelineManager::Stats Renderer::pipeline_stats() const { return core_->pipelines().stats(); }
bool Renderer::dxc_available() const { return core_->dxc_available(); }
const std::string& Renderer::dxc_status() const { return core_->dxc_status(); }
void Renderer::wait_translations() { core_->wait_translations(); }
Renderer::TranslationStats Renderer::translation_stats() const { return core_->translation_stats(); }
std::vector<materials::ShaderIssue> Renderer::shader_issues() const { return core_->shader_issues(); }

}  // namespace alamo::render
