// ums0 t05: the layer seam changes no result. shader_files_from_vfs (materials) and GameSampleSource::default_sfx_megs
// (audio) moved from the mod-folder tests on Vfs::mod_dirs() to vfs::Layer / Vfs::resolve_all; this test keeps the
// pre-ums0 versions as reference copies and compares both on the installed reference mods (docs/MODS.md §10.3, plus
// the AotR / AotCW stack of the 2026-11-18 corpus sweep, §10.4) and on base FoC. Data-driven: skips without
// ALAMO_FOC_DATA, and a mod that is not installed in the Steam library next to it is skipped by name.
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/audio/sample_source.hpp"
#include "alamo/core/hash.hpp"
#include "alamo/core/host_fs.hpp"
#include "alamo/materials/effect_loader.hpp"
#include "alamo/test/support.hpp"
#include "alamo/vfs/vfs.hpp"

namespace fs = std::filesystem;
using ::alamo::test::g_fail;

namespace {

namespace vfs = alamo::vfs;
namespace materials = alamo::materials;

// ---- Reference copies of the pre-ums0 code (dev 2f30b33e), kept only for this comparison ----

std::vector<materials::ShaderFileEntry> old_shader_files_from_vfs(const vfs::Vfs& v, bool include_engine) {
    std::vector<materials::ShaderFileEntry> out;
    const int mods = static_cast<int>(v.mod_dirs().size());
    std::vector<const char*> globs = {"DATA\\ART\\SHADERS\\*.FX", "DATA\\ART\\SHADERS\\*.FXO",
                                      "DATA\\ART\\SHADERS\\TERRAIN\\*.FX", "DATA\\ART\\SHADERS\\TERRAIN\\*.FXO"};
    if (include_engine) {
        globs.push_back("DATA\\ART\\SHADERS\\ENGINE\\*.FX");
        globs.push_back("DATA\\ART\\SHADERS\\ENGINE\\*.FXO");
    }
    for (const char* glob : globs) {
        for (auto& p : v.list(glob)) {
            auto r = v.resolve(p);
            if (!r) continue;
            materials::ShaderFileEntry e;
            e.loose = r->layer != vfs::Vfs::Resolution::Meg;
            if (r->layer == vfs::Vfs::Resolution::Mod) e.dir_order = r->layer_index;
            else if (r->layer == vfs::Vfs::Resolution::Fallback) e.dir_order = mods + 1 + r->layer_index;
            else e.dir_order = mods;
            if (r->layer == vfs::Vfs::Resolution::Mod) {
                e.from_mod = true;
            } else if (r->layer == vfs::Vfs::Resolution::Meg && r->layer_index >= 0 &&
                       static_cast<size_t>(r->layer_index) < v.mounts().size()) {
                const std::string& host = v.mounts()[static_cast<size_t>(r->layer_index)].path;
                for (const auto& d : v.mod_dirs())
                    if (materials::host_path_under(host, d)) e.from_mod = true;
            }
            e.path = std::move(p);
            out.push_back(std::move(e));
        }
    }
    return out;
}

std::vector<std::string> old_default_sfx_megs(const vfs::Vfs& v, std::string_view language) {
    const std::string names[3] = {"sfx2d_" + std::string(language) + ".meg", "sfx2d_non_localized.meg",
                                  "sfx3d_non_localized.meg"};
    std::vector<std::string> out;
    auto add = [&out](const fs::path& p) {
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) return;
        const std::string s = alamo::path_to_utf8(p);
        for (const std::string& o : out)
            if (alamo::iequals(o, s)) return;
        out.push_back(s);
    };
    for (const std::string& n : names) {
        for (const std::string& dir : v.mod_dirs()) add(alamo::path_from_utf8(dir) / "Data" / "Audio" / "SFX" / n);
        add(alamo::path_from_utf8(v.foc_root()) / "Data" / "Audio" / "SFX" / n);
        auto res = v.resolve("Data\\Audio\\SFX\\" + n);
        if (res && res->layer == vfs::Vfs::Resolution::Fallback) add(alamo::path_from_utf8(res->location));
    }
    if (!v.eaw_root().empty()) {
        for (const std::string& n : names) {
            fs::path p = alamo::path_from_utf8(v.eaw_root()) / "Data" / "Audio" / "SFX" / n;
            std::error_code ec;
            if (fs::exists(p, ec)) {
                const std::string s = alamo::path_to_utf8(p);
                bool dup = false;
                for (const std::string& o : out) dup = dup || alamo::iequals(o, s);
                if (!dup) out.push_back(s);
            }
        }
    }
    return out;
}

// ---- Comparison ----

// The same host file: the old code built paths from the folder names as written, the VFS reports them as found on
// disk, so the strings may differ in case and separators only.
bool same_host_file(const std::string& a, const std::string& b) {
    if (alamo::normalize_name(a) == alamo::normalize_name(b)) return true;
    std::error_code ec;
    return fs::equivalent(alamo::path_from_utf8(a), alamo::path_from_utf8(b), ec);
}

struct Counts {
    size_t shaders = 0, shaders_from_mod = 0, sfx = 0;
};

Counts compare(const char* label, const vfs::Vfs& v) {
    Counts c;
    for (const bool engine : {false, true}) {
        const auto want = old_shader_files_from_vfs(v, engine);
        const auto got = materials::shader_files_from_vfs(v, engine);
        CHECK(got.size() == want.size());
        size_t diff = 0;
        for (size_t i = 0; i < want.size() && i < got.size(); ++i) {
            const bool same = got[i].path == want[i].path && got[i].loose == want[i].loose &&
                              got[i].dir_order == want[i].dir_order && got[i].from_mod == want[i].from_mod;
            if (!same && diff++ < 5)
                std::printf("  %s: shader entry %zu differs: old %s loose=%d dir=%d mod=%d, new %s loose=%d dir=%d mod=%d\n", label, i,
                            want[i].path.c_str(), want[i].loose, want[i].dir_order, want[i].from_mod, got[i].path.c_str(), got[i].loose,
                            got[i].dir_order, got[i].from_mod);
        }
        CHECK(diff == 0);
        if (engine) {
            c.shaders = got.size();
            for (const auto& e : got) c.shaders_from_mod += e.from_mod ? 1u : 0u;
        }
    }
    for (const char* lang : {"English", "German"}) {
        const auto want = old_default_sfx_megs(v, lang);
        const auto got = alamo::audio::GameSampleSource::default_sfx_megs(v, lang);
        CHECK(got.size() == want.size());
        size_t diff = 0;
        for (size_t i = 0; i < want.size() && i < got.size(); ++i) {
            if (!same_host_file(got[i], want[i]) && diff++ < 5)
                std::printf("  %s: sfx archive %zu differs: old %s, new %s\n", label, i, want[i].c_str(), got[i].c_str());
        }
        CHECK(diff == 0);
        if (std::string_view(lang) == "English") c.sfx = got.size();
    }
    std::printf("  %s: %zu shader entries (%zu from the mod), %zu SFX archives: equal\n", label, c.shaders, c.shaders_from_mod, c.sfx);
    return c;
}

struct Setup {
    const char* label;
    std::vector<const char*> ids;  // workshop ids, search order (a submod first)
};

}  // namespace

int main() {
    const std::string foc = alamo::test::foc_data_root();
    if (foc.empty()) {
        alamo::test::print_skip("layer equivalence on the reference mods");
        return alamo::test::kSkipReturnCode;
    }
    const std::string eaw = alamo::test::eaw_data_root();
    const size_t at = foc.find("\\steamapps\\common\\");
    const std::string workshop = at == std::string::npos ? std::string() : foc.substr(0, at) + "\\steamapps\\workshop\\content\\32470\\";

    const std::vector<Setup> setups = {
        {"base FoC", {}},
        {"Thrawn's Revenge (1125571106)", {"1125571106"}},
        {"Fall of the Republic (1976399102)", {"1976399102"}},
        {"EAW-Remake (1770851727)", {"1770851727"}},
        {"Age of Legends (2858787558)", {"2858787558"}},
        {"AotR (1397421866)", {"1397421866"}},
        {"AotCW > AotR (3387971722 > 1397421866)", {"3387971722", "1397421866"}},
    };
    size_t mods_run = 0, from_mod_total = 0;
    for (const Setup& s : setups) {
        alamo::test::section(s.label);
        vfs::VfsConfig cfg;
        cfg.foc_root = foc;
        if (!eaw.empty()) cfg.eaw_root = eaw;
        bool installed = true;
        for (const char* id : s.ids) {
            const std::string dir = workshop + id;
            std::error_code ec;
            if (workshop.empty() || !fs::is_directory(alamo::path_from_utf8(dir) / "Data", ec)) installed = false;
            cfg.mod_search.push_back({vfs::ModSearchEntry::Kind::Path, dir});
        }
        if (!installed) {
            std::printf("  SKIP %s: not installed next to ALAMO_FOC_DATA\n", s.label);
            continue;
        }
        std::string err;
        const auto v = vfs::Vfs::create(cfg, &err);
        CHECK(v != nullptr);
        if (!v) {
            std::printf("  VFS failed: %s\n", err.c_str());
            continue;
        }
        const Counts c = compare(s.label, *v);
        CHECK(c.shaders > 0);
        if (s.ids.empty()) {
            CHECK(c.shaders_from_mod == 0);
        } else {
            ++mods_run;
            from_mod_total += c.shaders_from_mod;
        }
    }
    std::printf("%zu mod setups compared, %zu shader entries attributed to a mod\n", mods_run, from_mod_total);
    CHECK(mods_run == 0 || from_mod_total > 0);  // the comparison saw mod-owned entries, not only base ones
    return alamo::test::report("alamo_audio_layer_equivalence_tests");
}
