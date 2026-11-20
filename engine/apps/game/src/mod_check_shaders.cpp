// Mod check, shaders family (mod_check_detail.hpp): the effect audit with the model cross-reference (modc t21) and the
// .fx prebuild of a mounted mod (prebuild_mod_effects, modc t36).
#include "mod_check_detail.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "alamo/core/host_fs.hpp"

#ifdef ALAMO_GAME_HAVE_MATERIALS
#include "alamo/assets/model.hpp"
#include "alamo/materials/effect_audit.hpp"
#include "alamo/materials/effect_loader.hpp"
#include "alamo/shaderxlat/dxc.hpp"
#include "fx_compile.hpp"  // materials/tools: the .fx prebuild (alamo_fxbuild, modc t36)
#endif

namespace alamo::game::mod_check_detail {

namespace {

// ---- modc t21: shaders ----
// "DATA\ART\SHADERS\TERRAIN\X.FX" -> "terrain"; ...\ENGINE\... -> "engine"; else "models".
[[maybe_unused]] std::string_view shader_drawn_by(std::string_view path) {
    const std::string p = normalize_name(path);
    if (p.find("\\SHADERS\\TERRAIN\\") != std::string::npos) return "terrain";
    if (p.find("\\SHADERS\\ENGINE\\") != std::string::npos) return "engine";
    return "models";
}

}  // namespace

// "MeshGloss.fx" / "Data\Art\Shaders\MESHGLOSS.FXO" -> "MESHGLOSS" (the form ShaderIssue::effect has).
std::string effect_base(std::string_view name) {
    const size_t slash = name.find_last_of("\\/");
    if (slash != std::string_view::npos) name.remove_prefix(slash + 1);
    const size_t dot = name.rfind('.');
    if (dot != std::string_view::npos) name = name.substr(0, dot);
    return normalize_name(name);
}

namespace {

#ifdef ALAMO_GAME_HAVE_MATERIALS
// The models the object types name (first definitions, every tag ending in Model_Name), found through the files, sorted.
std::vector<std::string> object_type_models(const GameData& d) {
    const objects::ObjectTypeDatabase& db = d.raw;
    const objects::TypeColumns& t = db.types();
    const objects::FieldColumns& f = db.fields();
    std::set<std::string> paths;
    constexpr std::string_view kSuffix = "MODEL_NAME";
    for (objects::TypeIndex i = 0; i < t.size(); ++i) {
        if (t.first_definition[i] != i) continue;
        for (objects::FieldIndex fi = t.field_begin[i]; fi < t.field_end[i]; ++fi) {
            if (f.depth[fi] != 1 || f.text[fi].empty()) continue;
            const std::string tag = normalize_name(db.tag_name(fi));
            if (tag.size() < kSuffix.size() || tag.compare(tag.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) continue;
            std::string path = normalize_name(std::string("Data\\Art\\Models\\") + std::string(f.text[fi]));
            if (path.find('.', path.rfind('\\')) == std::string::npos) path += ".ALO";
            if (path.size() < 4 || path.compare(path.size() - 4, 4, ".ALO") != 0) continue;
            if (d.files->exists(path)) paths.insert(std::move(path));
        }
    }
    return {paths.begin(), paths.end()};
}
#endif

}  // namespace

void check_shaders(const GameData& d, ModCheckReport& r, bool compile, const std::string& fx_cache_dir) {
#ifdef ALAMO_GAME_HAVE_MATERIALS
    if (!d.vfs) return;
    ShaderCheck& sc = r.shaders;
    shaderxlat::RuntimeDxc dxc;
    const bool have_dxc = compile && dxc.load();
    // As alamo_effect_audit (materials/tools/effect_audit_tool.cpp): the library gets the D.1 preload list, the audit
    // every effect of the shader directories including Shaders\Engine.
    materials::LoadOptions lo;
    if (!fx_cache_dir.empty()) lo.fx_cache_dir = path_from_utf8(fx_cache_dir);
    materials::EffectLibrary lib(*d.files, lo);
    lib.set_shader_files(materials::shader_files_from_vfs(*d.vfs));
    const std::vector<materials::ShaderFileEntry> files = materials::shader_files_from_vfs(*d.vfs, true);
    const materials::EffectAudit a = materials::audit_effects(lib, files, have_dxc ? &dxc : nullptr, nullptr);
    sc.checked = true;
    sc.effects = a.effects;
    sc.mod_effects = a.mod_effects;
    sc.loaded = a.loaded;
    sc.translated_passes = a.translated_passes;
    sc.translated_ok = a.translated_ok;
    sc.compiled = a.compiled;
    sc.fx_sources = a.sources;
    sc.fx_sources_compiled = a.sources_compiled;
    std::map<std::string, size_t> model_effects;  // effect base name -> index of its first issue with drawn_by "models"
    for (const materials::ShaderIssue& i : a.issues) {
        ShaderIssueEntry e;
        e.kind = materials::shader_issue_kind_name(i.kind);
        e.effect = i.effect;
        e.path = i.path;
        e.in_mod = i.in_mod;
        e.detail = i.detail;
        e.drawn_by = std::string(shader_drawn_by(i.path));
        // The device-init engine effects (GLOBAL, ALAMOENGINE, the Prim* sprites, ...) are the engine's, wherever they lie.
        const std::vector<std::string>& engine = materials::engine_effect_load_sequence();
        if (std::find(engine.begin(), engine.end(), e.effect) != engine.end()) e.drawn_by = "engine";
        if (e.drawn_by == "models") model_effects.emplace(e.effect, sc.issues.size());
        sc.issues.push_back(std::move(e));
    }
    if (model_effects.empty()) return;  // vanilla: no issue, no model read
    // Which models name the issue effects: every material of every object-type model (render submeshes and the
    // others alike: the effect is loaded for them either way).
    std::map<std::string, std::pair<uint32_t, std::string>> users;  // effect -> (models, first model)
    for (const std::string& path : object_type_models(d)) {
        assets::ModelAsset m;
        if (!assets::load_model(*d.files, path, m)) continue;  // a broken model is not this check's finding
        ++sc.models_read;
        std::set<std::string> named;
        for (const assets::Submesh& sub : m.submeshes) {
            std::string base = effect_base(sub.material.effect);
            if (model_effects.count(base)) named.insert(std::move(base));
        }
        for (const std::string& eff : named) {
            auto& u = users[eff];
            if (u.first++ == 0) u.second = path;
        }
    }
    for (ShaderIssueEntry& e : sc.issues) {
        if (e.drawn_by != "models") continue;
        const auto it = users.find(e.effect);
        if (it == users.end()) continue;
        e.models = it->second.first;
        e.first_model = it->second.second;
    }
#else
    (void)d;
    (void)r;
    (void)compile;
    (void)fx_cache_dir;
#endif
}

}  // namespace alamo::game::mod_check_detail

namespace alamo::game {

FxPrebuild prebuild_mod_effects(const GameData& d, const std::string& fx_cache_dir) {
    FxPrebuild out;
#ifdef ALAMO_GAME_HAVE_MATERIALS
    if (!d.vfs || !d.files || d.mods.empty()) return out;
    const materials::FxCache cache(fx_cache_dir.empty() ? materials::FxCache::default_directory() : path_from_utf8(fx_cache_dir));
    const std::vector<materials::ShaderFileEntry> files = materials::shader_files_from_vfs(*d.vfs, true);
    const materials::FxPrebuildRun run = materials::prebuild_effect_sources(*d.files, files, cache, materials::default_fxc_path());
    out.ran = true;
    out.fxc = run.fxc;
    out.cache = path_to_utf8(cache.directory());
    out.ms = run.ms;
    for (const materials::FxPrebuildItem& it : run.items) {
        FxPrebuildSource src;
        src.path = it.path;
        src.ok = it.ok;
        src.cached = it.cached;
        if (!it.ok) src.error = it.detail.empty() ? std::string("not compiled") : it.detail;
        out.sources.push_back(std::move(src));
    }
#else
    (void)d;
    (void)fx_cache_dir;
#endif
    return out;
}

}  // namespace alamo::game
