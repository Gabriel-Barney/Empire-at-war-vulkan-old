#include "alamo/render/particles/particle_attach.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>

#include "alamo/core/hash.hpp"
#include "alamo/render/hardpoints.hpp"

namespace alamo::render::particles {

ProxyAttachments::~ProxyAttachments() { clear(); }

void ProxyAttachments::begin() {
    ++generation_;
    stats_ = AttachStats{};
}

uint64_t ProxyAttachments::static_key(const Instance& inst, uint64_t salt) {
    // FNV-1a 64 over the model uid then the transform bytes, from the offset basis xor salt.
    const uint64_t uid = inst.model ? inst.model->uid : 0;
    uint64_t h = fnv1a64(&uid, sizeof uid, kFnv1a64Offset ^ salt);
    h = fnv1a64(&inst.transform, sizeof inst.transform, h);
    return h | 1ull;  // never 0
}

SpawnMeshPtr ProxyAttachments::parent_mesh(const assets::ModelAsset& m, uint32_t proxy_bone, uint32_t* submesh) {
    const std::vector<int32_t>& parents = m.skeleton.parents;
    if (proxy_bone == 0 || proxy_bone >= parents.size() || parents[proxy_bone] < 0) return nullptr;
    const auto pb = static_cast<uint32_t>(parents[proxy_bone]);
    for (const assets::MeshInfo& mi : m.meshes) {
        if (mi.attach_bone != pb) continue;
        if (mi.submesh_count == 0 || mi.first_submesh >= m.submeshes.size()) return nullptr;
        const assets::Submesh& sm = m.submeshes[mi.first_submesh];
        const size_t stride = assets::vertex_stride(sm.layout);
        if (sm.vertex_count == 0 || sm.vertices.size() < static_cast<size_t>(sm.vertex_count) * stride) return nullptr;
        auto out = std::make_shared<SpawnMesh>();
        out->positions.resize(sm.vertex_count);
        for (uint32_t v = 0; v < sm.vertex_count; ++v) {  // position: the first 12 bytes of both vertex layouts
            float p[3];
            std::memcpy(p, sm.vertices.data() + static_cast<size_t>(v) * stride, sizeof p);
            out->positions[v] = {p[0], p[1], p[2]};
        }
        const uint32_t isz = assets::index_size(sm.index_type);
        const uint32_t n = std::min<uint32_t>(sm.index_count, static_cast<uint32_t>(sm.indices.size() / isz)) / 3u * 3u;
        out->indices.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            if (isz == 2) {
                uint16_t x;
                std::memcpy(&x, sm.indices.data() + static_cast<size_t>(i) * 2, 2);
                out->indices[i] = x;
            } else {
                std::memcpy(&out->indices[i], sm.indices.data() + static_cast<size_t>(i) * 4, 4);
            }
        }
        if (submesh) *submesh = mi.first_submesh;
        return out;
    }
    return nullptr;
}

namespace {
// The world transform of a parent mesh's vertex space (skinned submeshes: model space).
Mat3x4 mesh_space(const assets::ModelAsset& m, const Instance& inst, uint32_t sub) {
    const assets::Submesh& sm = m.submeshes[sub];
    if (sm.skinned()) return inst.transform;
    return attachment_transform(m, inst.transform, inst.bone_world, static_cast<int32_t>(sm.attach_bone));
}
bool needs_mesh(const SystemDef& d) {
    for (const EmitterDef& e : d.emitters)
        if (e.spawn_source >= 1 && e.spawn_source <= 3 && !e.weather) return true;
    return false;
}
}  // namespace

void ProxyAttachments::release_owner(Owner& o) {
    for (ProxySys& s : o.systems)
        if (s.h && world_->valid(s.h)) {
            world_->release(s.h);
            ++stats_.released;
        }
    o.systems.clear();
}

void ProxyAttachments::sync(uint64_t key, const Instance& inst, bool water, int alt) {
    if (!inst.model || !inst.model->asset) return;
    const assets::ModelAsset& m = *inst.model->asset;
    auto [it, fresh] = owners_.try_emplace(key);
    Owner& o = it->second;
    o.seen = generation_;
    ++stats_.owners;
    if (!fresh && o.model_uid != inst.model->uid) release_owner(o);  // the owner's model changed
    if (fresh || o.model_uid != inst.model->uid) {
        o.model_uid = inst.model->uid;
        for (uint32_t p = 0; p < m.proxies.size(); ++p) {
            const ProxyName pn = parse_proxy_name(m.proxies[p].name);
            SystemPtr def = lib_->get(pn.base);
            if (!def) continue;
            ProxySys s;
            s.proxy = p;
            s.bone = m.proxies[p].bone;
            s.alt = pn.alt;
            s.lod = pn.lod;
            const Mat3x4 t = attachment_transform(m, inst.transform, inst.bone_world, static_cast<int32_t>(s.bone));
            SpawnMeshPtr mesh;
            Mat3x4 mw;
            if (needs_mesh(*def)) {
                uint32_t sub = 0;
                mesh = parent_mesh(m, s.bone, &sub);
                if (mesh) {
                    s.mesh_sub = sub;
                    mw = mesh_space(m, inst, sub);
                }
            }
            s.h = world_->create(std::move(def), t, std::move(mesh), mw);
            o.systems.push_back(s);
        }
    }
    stats_.proxies += static_cast<uint32_t>(m.proxies.size());
    const size_t meshes = m.meshes.size();
    const int cur_alt = alt == kModelAlt ? inst.model->selection.alt : alt, cur_lod = inst.model->selection.lod;
    for (ProxySys& s : o.systems) {
        ++stats_.particle_proxies;
        if (s.alt >= 0 || s.lod >= 0) ++stats_.stripped;
        if (s.finished || !world_->valid(s.h)) {
            s.finished = true;  // one-shot system done: not recreated while the owner stays
            ++stats_.finished;
            continue;
        }
        ++stats_.systems;
        const Mat3x4 t = attachment_transform(m, inst.transform, inst.bone_world, static_cast<int32_t>(s.bone));
        world_->set_transform(s.h, t);
        if (s.mesh_sub >= 0) world_->set_spawn_mesh_transform(s.h, mesh_space(m, inst, static_cast<uint32_t>(s.mesh_sub)));
        const size_t sub = meshes + s.proxy;
        bool hidden = sub < inst.mesh_hidden.size() ? inst.mesh_hidden[sub] != 0 : m.proxies[s.proxy].hidden;
        if (s.alt >= 0 && s.alt != cur_alt) hidden = true;
        if (s.lod >= 0 && cur_lod >= 0 && s.lod != cur_lod) hidden = true;
        world_->set_visible(s.h, !hidden);
        world_->set_fade(s.h, inst.light_scale.w);
        world_->set_water(s.h, water);
        stats_.hidden += hidden ? 1u : 0u;
    }
    // Unresolved proxies (counted once per sync).
    for (uint32_t p = 0; p < m.proxies.size(); ++p) {
        bool found = false;
        for (const ProxySys& s : o.systems) found = found || s.proxy == p;
        if (!found) ++stats_.unresolved;
    }
}

void ProxyAttachments::sync_effect(uint64_t key, SystemPtr def, const Mat3x4& transform, float fade, bool visible, float intensity) {
    if (!def) return;
    auto [it, fresh] = owners_.try_emplace(key);
    Owner& o = it->second;
    o.seen = generation_;
    ++stats_.owners;
    const auto id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(def.get()));
    if (!fresh && o.model_uid != id) release_owner(o);
    if (fresh || o.model_uid != id) {
        o.model_uid = id;
        ProxySys s;
        s.h = world_->create(std::move(def), transform);
        o.systems.push_back(s);
    }
    for (ProxySys& s : o.systems) {
        if (s.finished || !world_->valid(s.h)) {
            s.finished = true;
            ++stats_.finished;
            continue;
        }
        ++stats_.systems;
        world_->set_transform(s.h, transform);
        world_->set_visible(s.h, visible);
        world_->set_fade(s.h, fade);
        if (intensity >= 0.0f && intensity != s.intensity) {
            world_->set_scalar(s.h, SystemScalar::Detail, std::span<const float>(&intensity, 1), 0.1f);
            s.intensity = intensity;
        }
    }
}

void ProxyAttachments::set_effect_scalars(uint64_t key, float brightness, float scale, float energy) {
    const auto it = owners_.find(key);
    if (it == owners_.end()) return;
    const float color[4] = {brightness, brightness, brightness, brightness};
    for (const ProxySys& s : it->second.systems) {
        if (s.finished || !world_->valid(s.h)) continue;
        world_->set_scalar(s.h, SystemScalar::Color, color, 0.0f);
        world_->set_scalar(s.h, SystemScalar::Emission, std::span<const float>(&scale, 1), 0.0f);
        world_->set_scalar(s.h, SystemScalar::Detail, std::span<const float>(&energy, 1), 0.0f);
    }
}

void ProxyAttachments::sync_static(std::span<const Instance> instances, uint64_t salt) {
    for (const Instance& in : instances) sync(static_key(in, salt), in);
}

void ProxyAttachments::end() {
    for (auto it = owners_.begin(); it != owners_.end();) {
        if (it->second.seen != generation_) {
            release_owner(it->second);
            it = owners_.erase(it);
        } else {
            ++it;
        }
    }
}

void ProxyAttachments::clear() {
    for (auto& [k, o] : owners_) release_owner(o);
    owners_.clear();
}

}  // namespace alamo::render::particles
