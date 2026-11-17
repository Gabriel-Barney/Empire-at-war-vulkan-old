// mcx2 t01: the `assets` section of the mod load report (asset_audit.hpp).
#include "alamo/game/asset_audit.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "alamo/core/host_fs.hpp"
#include "alamo/formats/ala.hpp"
#include "alamo/formats/alo.hpp"
#include "alamo/formats/common.hpp"
#include "alamo/vfs/vfs.hpp"
#include "json_text.hpp"

#if ALAMO_GAME_HAVE_MATERIALS
#include "alamo/assets/model.hpp"
#include "alamo/assets/skeleton.hpp"
#include "alamo/assets/texture.hpp"
#endif
#include "alamo/terrain/ted.hpp"
#if ALAMO_GAME_HAVE_AUDIO
#include "alamo/audio/decode.hpp"
#endif
#if ALAMO_GAME_HAVE_VIDEO
#include "alamo/video/bink_header.hpp"
#endif

namespace alamo::game {
namespace {

constexpr int kDecoded = 0, kSalvaged = 1, kRejected = 2;

struct FamilyGlob {
    const char* family;
    const char* glob;
};
// "model" covers both kinds of ALO: the bytes decide model or particle (audit_one).
constexpr FamilyGlob kGlobs[] = {
    {"model", "Data\\Art\\Models\\**.ALO"},     {"animation", "Data\\Art\\Models\\**.ALA"},
    {"texture", "Data\\Art\\Textures\\**.DDS"}, {"texture", "Data\\Art\\Textures\\**.TGA"},
    {"texture", "Data\\Art\\Textures\\**.BMP"}, {"map", "Data\\Art\\Maps\\**.TED"},
    {"sound", "Data\\Audio\\**.WAV"},           {"sound", "Data\\Audio\\**.MP3"},
    {"movie", "Data\\Art\\Movies\\**.BIK"},
};

// Rejections retail shares: the reason text (substring) and the docs/re finding that says retail does not show the
// asset either. Filled from the compat_assets probe (docs/re/MCX2_T02_ASSET_SALVAGE.md); an entry here is a claim
// about retail and needs that citation.
struct RetailEquivalent {
    const char* reason_part;
    const char* citation;
};
constexpr RetailEquivalent kRetailEquivalent[] = {
    {"no connections chunk 0x600", "retail does not draw a model without its connections chunk (docs/re/MCX2_T02_ASSET_SALVAGE.md 3, run 2)"},
};

bool retail_equivalent_reason(const std::string& reason, std::string* citation) {
    for (const auto& r : kRetailEquivalent) {
        if (reason.find(r.reason_part) != std::string::npos) {
            if (citation) *citation = r.citation;
            return true;
        }
    }
    return false;
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (char& c : s)
        if (c == '/') c = '\\';
    return s;
}

std::string join_notes(const std::vector<SalvageNote>& notes) {
    std::string r;
    for (const auto& n : notes) {
        if (r.find(n.rule) != std::string::npos) continue;  // one mention per rule
        if (!r.empty()) r += ';';
        r += n.rule;
    }
    return r;
}

uint64_t fnv1a(const std::vector<uint8_t>& b) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t c : b) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h ^ static_cast<uint64_t>(b.size());
}

// ---- per family ----

int audit_animation(const std::vector<uint8_t>& b, bool strict, std::string* rule) {
    try {
        (void)ala::parse(b);
    } catch (const std::exception& e) {
        if (strict) {
            *rule = e.what();
            return kRejected;
        }
        std::vector<SalvageNote> notes;
        try {
            (void)ala::parse(b, ParseOptions{true, &notes});
        } catch (const std::exception& e2) {
            *rule = e2.what();
            return kRejected;
        }
        *rule = notes.empty() ? std::string("ala_salvage") : join_notes(notes);
#if ALAMO_GAME_HAVE_MATERIALS
        assets::AnimationClip clip;
        std::string err;
        if (!assets::build_animation(b, "AUDIT.ALA", clip, &err)) {
            *rule = err;
            return kRejected;
        }
#endif
        return kSalvaged;
    }
#if ALAMO_GAME_HAVE_MATERIALS
    assets::AnimationClip clip;
    std::string err;
    if (!assets::build_animation(b, "AUDIT.ALA", clip, &err)) {
        *rule = err;
        return kRejected;
    }
#endif
    return kDecoded;
}

int audit_alo(const std::vector<uint8_t>& b, std::string* family, std::string* rule) {
    const bool particle = b.size() >= 4 && b[0] == 0x00 && b[1] == 0x09 && b[2] == 0x00 && b[3] == 0x00;
    *family = particle ? "particle" : "model";
    try {
        const alo::AloFile f = alo::parse(b);
        if (particle != (f.kind == alo::AloFile::Kind::ParticleSystem)) {
            *rule = "ALO kind does not match its first chunk";
            return kRejected;
        }
    } catch (const std::exception& e) {
        *rule = e.what();
        return kRejected;
    }
#if ALAMO_GAME_HAVE_MATERIALS
    if (!particle) {
        assets::ModelAsset m;
        std::string err;
        if (!assets::build_model(b, "AUDIT.ALO", m, &err)) {
            *rule = err;
            return kRejected;
        }
    }
#endif
    return kDecoded;
}

int audit_texture(const std::vector<uint8_t>& b, bool strict, std::string* rule) {
#if ALAMO_GAME_HAVE_MATERIALS
    assets::TextureAsset t;
    std::string err;
    if (!assets::decode_texture(b, t, &err)) {
        *rule = err.empty() ? std::string("texture decode failed") : err;
        return kRejected;
    }
    if (!t.warnings.empty()) {
        std::string w;
        for (const auto& s : t.warnings) {
            if (!w.empty()) w += "; ";
            w += s;
        }
        *rule = "texture: " + w;
        return strict ? kRejected : kSalvaged;
    }
    return kDecoded;
#else
    (void)b;
    (void)strict;
    *rule = "not checked (built without the assets module)";
    return kDecoded;
#endif
}

int audit_map(const std::vector<uint8_t>& b, std::string* rule) {
    terrain::MapData m;
    std::string err;
    if (!terrain::parse_ted(b, m, &err)) {
        *rule = err.empty() ? std::string("map parse failed") : err;
        return kRejected;
    }
    return kDecoded;
}

int audit_sound(const std::vector<uint8_t>& b, std::string* rule) {
#if ALAMO_GAME_HAVE_AUDIO
    std::string err;
    if (!audio::probe_audio(b, &err)) {
        *rule = err.empty() ? std::string("not a sound the port reads") : err;
        return kRejected;
    }
    audio::PcmBuffer pcm;
    if (!audio::decode_audio(b, pcm, &err)) {
        *rule = err.empty() ? std::string("sound decode failed") : err;
        return kRejected;
    }
    return kDecoded;
#else
    (void)b;
    (void)rule;
    return kDecoded;
#endif
}

int audit_movie(const std::vector<uint8_t>& b, std::string* rule) {
#if ALAMO_GAME_HAVE_VIDEO
    const auto h = video::probe_bink_header(b);
    if (!h) {
        *rule = "not a Bink movie (bad header)";
        return kRejected;
    }
    std::string why = video::bink_unsupported_reason(*h);
    if (!why.empty()) {
        *rule = std::move(why);
        return kRejected;
    }
    return kDecoded;
#else
    (void)b;
    (void)rule;
    return kDecoded;
#endif
}

// ---- cache ----

struct CacheKey {
    std::string family;
    uint64_t hash;
    bool strict;
    bool operator==(const CacheKey& o) const { return hash == o.hash && strict == o.strict && family == o.family; }
};
struct CacheKeyHash {
    size_t operator()(const CacheKey& k) const { return static_cast<size_t>(k.hash ^ (k.strict ? 1u : 0u)); }
};
struct CacheValue {
    std::string family;  // resolved family (model -> particle)
    int state = kDecoded;
    std::string rule;
    bool retail_equivalent = false;
};
using Cache = std::unordered_map<CacheKey, CacheValue, CacheKeyHash>;  // looked up only, never iterated for output

// The second number is the rules version: bump it whenever a decoder, salvage rule or retail-equivalent entry changes
// what a file's verdict is, so a cache written by an older build is ignored.
constexpr const char* kCacheHeader = "alamo asset audit cache v1 rules 2";

std::string cache_file(const std::string& dir) { return dir + "\\asset_audit_v1.txt"; }

// One entry per line: family \t hash(hex) \t strict \t resolved family \t state \t retail_equivalent \t rule.
void load_cache(const std::string& dir, Cache& c) {
    std::ifstream f(path_from_utf8(cache_file(dir)), std::ios::binary);
    std::string line;
    if (!f || !std::getline(f, line) || line != kCacheHeader) return;
    while (std::getline(f, line)) {
        std::vector<std::string> p;
        size_t s = 0;
        for (int i = 0; i < 6; ++i) {
            const size_t t = line.find('\t', s);
            if (t == std::string::npos) break;
            p.push_back(line.substr(s, t - s));
            s = t + 1;
        }
        if (p.size() != 6) continue;
        CacheKey k{p[0], std::strtoull(p[1].c_str(), nullptr, 16), p[2] == "1"};
        CacheValue v{p[3], std::atoi(p[4].c_str()), line.substr(s), p[5] == "1"};
        if (v.state < kDecoded || v.state > kRejected) continue;
        c.emplace(std::move(k), std::move(v));
    }
}

void save_cache(const std::string& dir, const std::vector<std::pair<CacheKey, CacheValue>>& entries) {
    std::ofstream f(path_from_utf8(cache_file(dir)), std::ios::binary | std::ios::trunc);
    if (!f) return;
    f << kCacheHeader << '\n';
    for (const auto& [k, v] : entries) {
        std::string rule = v.rule;
        for (char& ch : rule)
            if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
        char hex[24];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(k.hash));
        f << k.family << '\t' << hex << '\t' << (k.strict ? 1 : 0) << '\t' << v.family << '\t' << v.state << '\t'
          << (v.retail_equivalent ? 1 : 0) << '\t' << rule << '\n';
    }
}

bool starts_with_ci(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
        char b = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[i])));
        if (a == '/') a = '\\';
        if (b == '/') b = '\\';
        if (a != b) return false;
    }
    return true;
}

// True when `v` serves `path` from a mod: a mod's loose Data tree, or a MEG whose host file lies in a mod folder.
bool served_by_mod(const vfs::Vfs& v, const std::string& path) {
    const auto r = v.resolve(path);
    if (!r) return false;
    if (r->layer == vfs::Vfs::Resolution::Mod) return true;
    if (r->layer != vfs::Vfs::Resolution::Meg) return false;
    const auto& mounts = v.mounts();
    if (r->layer_index < 0 || static_cast<size_t>(r->layer_index) >= mounts.size()) return false;
    const std::string& meg = mounts[static_cast<size_t>(r->layer_index)].path;
    for (const auto& d : v.mod_dirs()) {
        std::string dir = d;
        if (!dir.empty() && dir.back() != '\\' && dir.back() != '/') dir += '\\';
        if (starts_with_ci(meg, dir)) return true;
    }
    return false;
}

}  // namespace

int audit_asset_bytes(const std::string& family_in, const std::vector<uint8_t>& bytes, bool strict, std::string* rule_out,
                      bool* retail_equivalent_out) {
    std::string family = family_in, rule;
    int state = kDecoded;
    if (bytes.empty()) {
        // A 0-byte file: mods blank a base file this way (Thrawn's Revenge empties 50 base maps to drop them). There is
        // nothing to decode, for retail either.
        if (rule_out) *rule_out = "empty file (0 bytes) [retail-equivalent: nothing to decode, for retail either]";
        if (retail_equivalent_out) *retail_equivalent_out = true;
        return kRejected;
    }
    if (family == "model" || family == "particle") state = audit_alo(bytes, &family, &rule);
    else if (family == "animation") state = audit_animation(bytes, strict, &rule);
    else if (family == "texture") state = audit_texture(bytes, strict, &rule);
    else if (family == "map") state = audit_map(bytes, &rule);
    else if (family == "sound") state = audit_sound(bytes, &rule);
    else if (family == "movie") state = audit_movie(bytes, &rule);
    bool equivalent = false;
    if (state == kRejected) {
        std::string citation;
        if (retail_equivalent_reason(rule, &citation)) {
            equivalent = true;
            rule += " [retail-equivalent: " + citation + "]";
        }
    }
    if (state == kDecoded) rule.clear();
    if (rule_out) *rule_out = rule;
    if (retail_equivalent_out) *retail_equivalent_out = equivalent;
    return state;
}

AssetAudit audit_mod_assets(const vfs::Vfs& v, const AssetAuditOptions& opt) {
    AssetAudit a;
    a.ran = true;
    if (v.mod_dirs().empty()) return a;

    // Paths in sorted order, each once (a path matched by two globs keeps the first family).
    std::vector<std::pair<std::string, const char*>> files;
    for (const auto& g : kGlobs) {
        for (const auto& p : v.list(g.glob, vfs::Vfs::kLayerMod | vfs::Vfs::kLayerMeg)) files.emplace_back(upper(p), g.family);
    }
    std::sort(files.begin(), files.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    files.erase(std::unique(files.begin(), files.end(), [](const auto& x, const auto& y) { return x.first == y.first; }),
                files.end());

    Cache cache;
    if (!opt.cache_dir.empty()) load_cache(opt.cache_dir, cache);
    std::vector<std::pair<CacheKey, CacheValue>> seen;  // written back in path order

    std::vector<uint8_t> bytes;
    for (const auto& [path, family0] : files) {
        if (!served_by_mod(v, path)) continue;
        bytes.clear();
        CacheKey key{family0, 0, opt.strict};
        CacheValue val;
        if (!v.read(path, bytes)) {
            val = CacheValue{family0, kRejected, "unreadable (the archive entry or file could not be read)", false};
        } else {
            key.hash = fnv1a(bytes);
            const auto it = cache.find(key);
            if (it != cache.end()) {
                val = it->second;
                ++a.cache_hits;
            } else {
                val.family = family0;
                val.state = audit_asset_bytes(family0, bytes, opt.strict, &val.rule, &val.retail_equivalent);
                if (std::string(family0) == "model" && bytes.size() >= 4 && bytes[0] == 0 && bytes[1] == 9 && bytes[2] == 0 &&
                    bytes[3] == 0)
                    val.family = "particle";
            }
            seen.emplace_back(key, val);
        }
        ++a.checked;
        AssetFamilyCount& fc = a.by_family[val.family];
        ++fc.checked;
        if (val.state == kDecoded) {
            ++a.decoded;
            ++fc.decoded;
        } else if (val.state == kSalvaged) {
            ++a.salvaged_count;
            ++fc.salvaged;
            a.salvaged.push_back(AssetEntry{path, val.family, val.rule, false});
        } else {
            ++a.rejected_count;
            ++fc.rejected;
            if (val.retail_equivalent) ++a.retail_equivalent;
            a.rejected.push_back(AssetEntry{path, val.family, val.rule, val.retail_equivalent});
        }
    }
    if (!opt.cache_dir.empty()) save_cache(opt.cache_dir, seen);
    return a;
}

std::string asset_audit_json(const AssetAudit& a, int indent) {
    using json_text::json_str;
    const std::string pad(static_cast<size_t>(indent), ' '), pad2 = pad + "  ", pad3 = pad2 + "  ";
    std::string o = "{\n";
    o += pad2 + "\"checked\": " + std::to_string(a.checked) + ",\n";
    o += pad2 + "\"decoded\": " + std::to_string(a.decoded) + ",\n";
    o += pad2 + "\"salvaged_count\": " + std::to_string(a.salvaged_count) + ",\n";
    o += pad2 + "\"rejected_count\": " + std::to_string(a.rejected_count) + ",\n";
    o += pad2 + "\"retail_equivalent\": " + std::to_string(a.retail_equivalent) + ",\n";
    o += pad2 + "\"salvaged\": [";
    for (size_t i = 0; i < a.salvaged.size(); ++i) {
        const auto& e = a.salvaged[i];
        o += i ? ",\n" : "\n";
        o += pad3 + "{\"path\": ";
        json_str(o, e.path);
        o += ", \"family\": ";
        json_str(o, e.family);
        o += ", \"rule\": ";
        json_str(o, e.rule);
        o += "}";
    }
    o += a.salvaged.empty() ? "],\n" : "\n" + pad2 + "],\n";
    o += pad2 + "\"rejected\": [";
    for (size_t i = 0; i < a.rejected.size(); ++i) {
        const auto& e = a.rejected[i];
        o += i ? ",\n" : "\n";
        o += pad3 + "{\"path\": ";
        json_str(o, e.path);
        o += ", \"family\": ";
        json_str(o, e.family);
        o += ", \"reason\": ";
        json_str(o, e.rule);
        o += std::string(", \"retail_equivalent\": ") + (e.retail_equivalent ? "true" : "false") + "}";
    }
    o += a.rejected.empty() ? "],\n" : "\n" + pad2 + "],\n";
    o += pad2 + "\"by_family\": {";
    bool first = true;
    for (const auto& [fam, c] : a.by_family) {  // std::map: sorted by name
        o += first ? "\n" : ",\n";
        first = false;
        o += pad3;
        json_str(o, fam);
        o += ": {\"checked\": " + std::to_string(c.checked) + ", \"decoded\": " + std::to_string(c.decoded) +
             ", \"salvaged\": " + std::to_string(c.salvaged) + ", \"rejected\": " + std::to_string(c.rejected) + "}";
    }
    o += first ? "}\n" : "\n" + pad2 + "}\n";
    o += pad + "}";
    return o;
}

}  // namespace alamo::game
