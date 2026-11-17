// alamo_game_asset_audit_tests (mcx2 t01, asset_audit.hpp): a fake FoC install with a mod folder in its own temp folder.
// The mod serves a good texture, a broken one, a model without its connections chunk (retail hangs on it: a
// retail-equivalent rejection), a valid model and an empty animation; the base game's own files are not audited.
// Checks the counts, the entries, the JSON and that a second run with a cache decodes nothing and agrees.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

#include "alamo/game/asset_audit.hpp"
#include "alamo/test/support.hpp"
#include "alamo/vfs/vfs.hpp"

namespace fs = std::filesystem;
using ::alamo::test::g_fail;

namespace {

using Bytes = std::vector<uint8_t>;

void put32(Bytes& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void putf(Bytes& b, float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, 4);
    put32(b, u);
}
Bytes chunk(uint32_t id, const Bytes& payload, bool children) {
    Bytes b;
    put32(b, id);
    put32(b, static_cast<uint32_t>(payload.size()) | (children ? 0x80000000u : 0u));
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}
Bytes mini(uint8_t id, uint32_t v) {
    Bytes b{id, 4};
    put32(b, v);
    return b;
}
Bytes cat(std::initializer_list<Bytes> l) {
    Bytes o;
    for (const auto& v : l) o.insert(o.end(), v.begin(), v.end());
    return o;
}
Bytes str(const char* s) { return Bytes(s, s + std::strlen(s) + 1); }

// One-bone skeleton, no meshes; with or without the connections chunk (0 connections, 0 proxies).
Bytes model(bool connections) {
    Bytes hdr;
    put32(hdr, 1);
    hdr.resize(128, 0);
    Bytes info;
    put32(info, 0xFFFFFFFFu);
    put32(info, 1);
    put32(info, 0);
    for (float f : {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f}) putf(info, f);
    const Bytes skel = chunk(0x200, cat({chunk(0x201, hdr, false),
                                         chunk(0x202, cat({chunk(0x203, str("Root"), false), chunk(0x206, info, false)}), true)}),
                             true);
    if (!connections) return skel;
    const Bytes conn = chunk(0x600, cat({chunk(0x601, cat({mini(1, 0), mini(4, 0)}), false)}), true);
    return cat({skel, conn});
}

// 2x2 uncompressed 32-bit TGA.
Bytes tga() {
    Bytes b{0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 8};
    for (int i = 0; i < 16; ++i) b.push_back(static_cast<uint8_t>(i * 16));
    return b;
}

void write(const fs::path& p, const Bytes& b) {
    ::alamo::test::write_file(p, std::string(b.begin(), b.end()));
}

const alamo::game::AssetEntry* find(const std::vector<alamo::game::AssetEntry>& v, const std::string& part) {
    for (const auto& e : v)
        if (e.path.find(part) != std::string::npos) return &e;
    return nullptr;
}

void test_bytes() {
    std::string rule;
    bool eq = false;
    CHECK(alamo::game::audit_asset_bytes("model", model(true), false, &rule, &eq) == 0 && rule.empty() && !eq);
    CHECK(alamo::game::audit_asset_bytes("model", model(false), false, &rule, &eq) == 2 && eq);
    CHECK(rule.find("connections") != std::string::npos && rule.find("MCX2_T02") != std::string::npos);
    CHECK(alamo::game::audit_asset_bytes("texture", tga(), false, &rule, &eq) == 0);
    CHECK(alamo::game::audit_asset_bytes("texture", Bytes{'D', 'D', 'S', ' ', 1}, false, &rule, &eq) == 2 && !eq);
    CHECK(alamo::game::audit_asset_bytes("animation", Bytes{}, false, &rule, &eq) == 2 && eq && rule.find("empty") != std::string::npos);
}

void test_mod_folder() {
    const fs::path root = fs::temp_directory_path() / "alamo_game_asset_audit_tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path foc = root / "corruption", mod = root / "Mods" / "AA";
    ::alamo::test::write_file(foc / "Data/XML/GameObjectFiles.xml", "<GameObjectFiles></GameObjectFiles>\n");
    write(foc / "Data/Art/Textures/BASE_BROKEN.DDS", Bytes{'x'});  // base data: never audited
    write(foc / "Data/Art/Textures/SHARED.TGA", Bytes{'x'});
    write(mod / "Data/Art/Textures/SHARED.TGA", tga());  // the mod's copy wins and is audited
    write(mod / "Data/Art/Textures/AA_GOOD.tga", tga());
    write(mod / "Data/Art/Textures/sub/AA_BAD.dds", Bytes{'D', 'D', 'S', ' ', 0});
    write(mod / "Data/Art/Models/AA_OK.ALO", model(true));
    write(mod / "Data/Art/Models/AA_NOCONN.ALO", model(false));
    write(mod / "Data/Art/Models/AA_EMPTY.ALA", Bytes{});

    std::string err;
    const std::vector<std::string> args{"MODPATH=" + mod.string()};
    auto v = alamo::vfs::mount_game(foc.string(), args, &err);
    CHECK(v != nullptr);
    if (!v) return;
    const fs::path cache = root / "cache";
    fs::create_directories(cache, ec);
    alamo::game::AssetAuditOptions opt;
    opt.cache_dir = cache.string();
    const auto a = alamo::game::audit_mod_assets(*v, opt);
    CHECK(a.ran && a.checked == 6);
    CHECK(a.decoded == 3 && a.salvaged_count == 0 && a.rejected_count == 3 && a.retail_equivalent == 2);
    CHECK(a.cache_hits == 0);
    const auto* noconn = find(a.rejected, "AA_NOCONN.ALO");
    CHECK(noconn && noconn->family == "model" && noconn->retail_equivalent);
    const auto* bad = find(a.rejected, "SUB\\AA_BAD.DDS");
    CHECK(bad && bad->family == "texture" && !bad->retail_equivalent);
    CHECK(find(a.rejected, "AA_EMPTY.ALA") != nullptr);
    CHECK(find(a.rejected, "BASE_BROKEN") == nullptr);
    CHECK(a.by_family.at("texture").checked == 3 && a.by_family.at("texture").rejected == 1);
    CHECK(a.by_family.at("model").checked == 2);
    const std::string json = alamo::game::asset_audit_json(a);
    CHECK(json.find("\"rejected_count\": 3") != std::string::npos);
    CHECK(json.find("\"retail_equivalent\": true") != std::string::npos);
    CHECK(json.find("\"by_family\"") != std::string::npos);

    // Second run: everything from the cache, same result.
    const auto b = alamo::game::audit_mod_assets(*v, opt);
    CHECK(b.cache_hits == 6 && b.checked == a.checked && b.rejected_count == a.rejected_count);
    CHECK(alamo::game::asset_audit_json(b) == json);

    // Strict and no mod.
    auto base = alamo::vfs::mount_game(foc.string(), {}, &err);
    CHECK(base && alamo::game::audit_mod_assets(*base).checked == 0);
    fs::remove_all(root, ec);
}

}  // namespace

int main() {
    test_bytes();
    test_mod_folder();
    std::printf("%s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
