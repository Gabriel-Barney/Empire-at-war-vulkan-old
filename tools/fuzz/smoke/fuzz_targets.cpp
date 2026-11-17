#include "fuzz_targets.hpp"

#include <cstring>
#include <initializer_list>
#include <string_view>

#if defined(ALAMO_FUZZ_HAVE_TERRAIN)
#include "alamo/terrain/ted.hpp"
#endif
#if defined(ALAMO_FUZZ_HAVE_UI)
#include "alamo/ui/gui_resource.hpp"
#include "alamo/ui/hud_components.hpp"
#endif
#if defined(ALAMO_FUZZ_HAVE_ANIM)
#include "alamo/anim/clip.hpp"
#endif
#if defined(ALAMO_FUZZ_HAVE_PARTICLES)
#include "alamo/render/particles/particle_data.hpp"
#endif
#if defined(ALAMO_FUZZ_HAVE_LUA)
#include <lauxlib.h>
#include <lua.h>
#endif
#if defined(ALAMO_FUZZ_HAVE_AUDIO)
#include "alamo/audio/decode.hpp"
#endif
#if defined(ALAMO_FUZZ_HAVE_VIDEO)
#include "alamo/video/bink_header.hpp"
#endif

namespace alamo::fuzz {

namespace {

void put32(Bytes& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put16(Bytes& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v & 0xFF));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
// One Alamo chunk: id, size (bit 31 = children), body.
Bytes chunk(uint32_t id, const Bytes& body, bool children) {
    Bytes b;
    put32(b, id);
    put32(b, static_cast<uint32_t>(body.size()) | (children ? 0x80000000u : 0u));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}
Bytes mini(uint8_t id, const Bytes& body) {
    Bytes b{id, static_cast<uint8_t>(body.size())};
    b.insert(b.end(), body.begin(), body.end());
    return b;
}
Bytes u32b(uint32_t v) {
    Bytes b;
    put32(b, v);
    return b;
}
Bytes cat(std::initializer_list<Bytes> l) {
    Bytes o;
    for (const auto& v : l) o.insert(o.end(), v.begin(), v.end());
    return o;
}

#if defined(ALAMO_FUZZ_HAVE_TERRAIN)
void run_ted(std::span<const uint8_t> b) {
    terrain::MapData m;
    std::string err;
    (void)terrain::parse_ted(b, m, &err);
}
std::vector<Bytes> seeds_ted() {
    terrain::MapData m;
    return {terrain::write_ted(m)};
}
#endif

#if defined(ALAMO_FUZZ_HAVE_UI)
void run_dat(std::span<const uint8_t> b) {
    ui::TextDatabase d;
    std::string err;
    if (d.parse(b, &err)) (void)d.text("TEXT_BUTTON_BEGIN");
}
std::vector<Bytes> seeds_dat() {
    Bytes b;
    put32(b, 2);
    const char* keys[2] = {"TEXT_A", "TEXT_BB"};
    put32(b, 0x1234u);
    put32(b, 2);
    put32(b, 6);
    put32(b, 0x5678u);
    put32(b, 3);
    put32(b, 7);
    for (char16_t c : std::u16string_view(u"hiabc")) put16(b, static_cast<uint16_t>(c));
    for (const char* k : keys) b.insert(b.end(), k, k + std::strlen(k));
    return {b};
}
void run_mtd(std::span<const uint8_t> b) {
    ui::TextureAtlas a;
    std::string err;
    if (a.parse(b, &err)) (void)a.find("i_button_ok");
}
std::vector<Bytes> seeds_mtd() {
    Bytes b;
    put32(b, 1);
    Bytes rec(81, 0);
    std::memcpy(rec.data(), "i_button_ok", 11);
    rec[72] = 16;
    rec[76] = 16;
    b.insert(b.end(), rec.begin(), rec.end());
    return {b};
}
void run_rc(std::span<const uint8_t> b) {
    ui::DialogScript s;
    std::string err;
    (void)s.parse(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()), &err);
}
std::vector<Bytes> seeds_rc() {
    const std::string_view rc =
        "IDD_TEST DIALOGEX 0, 0, 200, 100\r\nSTYLE DS_SETFONT | WS_POPUP\r\nFONT 8, \"MS Sans Serif\"\r\nBEGIN\r\n"
        "    PUSHBUTTON \"OK\",IDC_OK,10,10,50,14\r\n    LTEXT \"Text\",IDC_STATIC,10,30,100,8\r\n"
        "    CONTROL \"\",IDC_LIST,\"ListBox\",LBS_NOTIFY,10,50,100,40\r\nEND\r\n";
    return {Bytes(rc.begin(), rc.end())};
}
#endif

#if defined(ALAMO_FUZZ_HAVE_ANIM)
void run_clip(std::span<const uint8_t> b) {
    anim::Clip c;
    std::string err;
    (void)anim::parse_clip(b, "FUZZ_IDLE_00.ALA", c, &err);
}
// Block-layout ALA: 2 frames, 1 bone with a rotation track (index 0) in a rotation block of 4.
std::vector<Bytes> seeds_clip() {
    const Bytes info = chunk(0x1001, cat({mini(1, u32b(2)), mini(2, u32b(0x41F00000u)), mini(3, u32b(1)),
                                          mini(0x0B, u32b(4)), mini(0x0C, u32b(0)), mini(0x0D, u32b(0))}),
                             false);
    Bytes name = {'R', 'o', 'o', 't', 0};
    Bytes zero12(12, 0), one12;
    for (int i = 0; i < 3; ++i) put32(one12, 0x3F800000u);
    const Bytes bone = chunk(0x1002, chunk(0x1003, cat({mini(4, name), mini(5, u32b(0)), mini(6, zero12), mini(7, zero12),
                                                        mini(8, one12), mini(9, one12), mini(0x0E, {0xFF, 0xFF}),
                                                        mini(0x0F, {0xFF, 0xFF}), mini(0x10, {0, 0}),
                                                        mini(0x11, {0, 0, 0, 0, 0, 0, 0xFF, 0x7F})}),
                                                   false),
                             true);
    Bytes rot;
    for (int f = 0; f < 2; ++f) {
        put16(rot, 0);
        put16(rot, 0);
        put16(rot, 0);
        put16(rot, 0x7FFF);
    }
    return {chunk(0x1000, cat({info, bone, chunk(0x1009, rot, false)}), true)};
}
#endif

#if defined(ALAMO_FUZZ_HAVE_PARTICLES)
void run_particle(std::span<const uint8_t> b) {
    render::particles::SystemDef d;
    std::string err;
    if (render::particles::is_particle_file(b)) (void)render::particles::load_system(b, "P_FUZZ.ALO", d, &err);
}
std::vector<Bytes> seeds_particle() {
    const Bytes props = chunk(0x02, cat({mini(0x04, u32b(1)), mini(0x05, u32b(1)), mini(0x09, u32b(0x3F800000u))}), false);
    const Bytes emitter = chunk(0x700, cat({chunk(0x16, {'e', 0}, false), props}), true);
    return {chunk(0x900, cat({chunk(0x0, {'P', '_', 'F', 0}, false), chunk(0x800, emitter, true)}), true)};
}
#endif

#if defined(ALAMO_FUZZ_HAVE_LUA)
// Lua 5.0 chunk loading (luaL_loadbuffer: lundump for "\033Lua" bytecode, the parser for source), as LuaVm::compile.
void run_lc(std::span<const uint8_t> b) {
    lua_State* L = lua_open();
    if (!L) return;
    if (luaL_loadbuffer(L, reinterpret_cast<const char*>(b.data()), b.size(), "@fuzz.lc") == 0) lua_pop(L, 1);
    lua_close(L);
}
int dump_writer(lua_State*, const void* p, size_t n, void* ud) {
    auto* out = static_cast<Bytes*>(ud);
    out->insert(out->end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n);
    return 1;
}
std::vector<Bytes> seeds_lc() {
    std::vector<Bytes> out;
    const char* src[] = {"local a = 1 return a + 2", "function F(x) if x then return {x, 'y'} end return nil end",
                         "for i = 1, 3 do local t = {} t[i] = i * 2.5 end"};
    for (const char* s : src) {
        lua_State* L = lua_open();
        if (!L) continue;
        Bytes b;
        if (luaL_loadbuffer(L, s, std::strlen(s), "=seed") == 0) lua_dump(L, dump_writer, &b);
        lua_close(L);
        if (!b.empty()) out.push_back(std::move(b));
    }
    return out;
}
#endif

#if defined(ALAMO_FUZZ_HAVE_AUDIO)
void run_wav(std::span<const uint8_t> b) {
    std::string err;
    if (!audio::probe_audio(b, &err)) return;
    if (b.size() > (1u << 20)) return;  // decoding cost grows with the claimed length; headers are the target
    audio::PcmBuffer pcm;
    (void)audio::decode_audio(b, pcm, &err);
}
Bytes wav(uint16_t fmt, uint16_t channels, uint32_t rate, uint16_t bits, uint16_t block, const Bytes& extra,
          const Bytes& data) {
    Bytes f;
    put16(f, fmt);
    put16(f, channels);
    put32(f, rate);
    put32(f, rate * block);
    put16(f, block);
    put16(f, bits);
    f.insert(f.end(), extra.begin(), extra.end());
    Bytes b = {'R', 'I', 'F', 'F'};
    put32(b, static_cast<uint32_t>(4 + 8 + f.size() + 8 + data.size()));
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(b, static_cast<uint32_t>(f.size()));
    b.insert(b.end(), f.begin(), f.end());
    b.insert(b.end(), {'d', 'a', 't', 'a'});
    put32(b, static_cast<uint32_t>(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    return b;
}
std::vector<Bytes> seeds_wav() {
    Bytes pcm;
    for (int i = 0; i < 64; ++i) put16(pcm, static_cast<uint16_t>(i * 512));
    Bytes adpcm_extra;
    put16(adpcm_extra, 2);
    put16(adpcm_extra, 505);  // samples per block of 256 bytes, mono
    return {wav(1, 1, 22050, 16, 2, {}, pcm), wav(0x11, 1, 22050, 4, 256, adpcm_extra, Bytes(256, 0x11))};
}
#endif

#if defined(ALAMO_FUZZ_HAVE_VIDEO)
void run_bink(std::span<const uint8_t> b) {
    if (const auto h = video::probe_bink_header(b)) (void)video::bink_unsupported_reason(*h);
}
std::vector<Bytes> seeds_bink() {
    Bytes b = {'B', 'I', 'K', 'i'};
    for (uint32_t v : {1000u, 25u, 4096u, 25u, 640u, 480u, 15u, 1u, 0u, 1u}) put32(b, v);
    b.resize(256, 0);
    Bytes k = b;
    k[0] = 'K';
    k[1] = 'B';
    k[2] = '2';
    k[3] = 'j';
    return {b, k};
}
#endif

std::vector<Target> make_targets() {
    std::vector<Target> t;
#if defined(ALAMO_FUZZ_HAVE_TERRAIN)
    t.push_back({"ted", "Data\\Art\\Maps\\*.TED", run_ted, seeds_ted});
#endif
#if defined(ALAMO_FUZZ_HAVE_UI)
    t.push_back({"dat", "Data\\Text\\*.DAT", run_dat, seeds_dat});
    t.push_back({"mtd", "Data\\Art\\Textures\\*.MTD", run_mtd, seeds_mtd});
    t.push_back({"rc", "Data\\Resources\\GUIDialog\\*.RC", run_rc, seeds_rc});
#endif
#if defined(ALAMO_FUZZ_HAVE_ANIM)
    t.push_back({"clip", "Data\\Art\\Models\\*.ALA", run_clip, seeds_clip});
#endif
#if defined(ALAMO_FUZZ_HAVE_PARTICLES)
    t.push_back({"particle", "Data\\Art\\Models\\P_*.ALO", run_particle, seeds_particle});
#endif
#if defined(ALAMO_FUZZ_HAVE_LUA)
    t.push_back({"lc", "Data\\Scripts\\**\\*.LC;Data\\Scripts\\Library\\*.LUA", run_lc, seeds_lc});
#endif
#if defined(ALAMO_FUZZ_HAVE_AUDIO)
    t.push_back({"wav", "Data\\Audio\\SFX\\*.WAV;Data\\Audio\\Units\\*.WAV", run_wav, seeds_wav});
#endif
#if defined(ALAMO_FUZZ_HAVE_VIDEO)
    t.push_back({"bink", "Data\\Art\\Movies\\Binked\\*.BIK", run_bink, seeds_bink});
#endif
    return t;
}

}  // namespace

const std::vector<Target>& targets() {
    static const std::vector<Target> t = make_targets();
    return t;
}

const Target* find_target(const std::string& name) {
    for (const auto& t : targets())
        if (name == t.name) return &t;
    return nullptr;
}

}  // namespace alamo::fuzz
