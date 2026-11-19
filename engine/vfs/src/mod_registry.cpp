// Mod registry and stack resolution (mod_registry.hpp).
#include "alamo/vfs/mod_registry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <system_error>
#include <utility>

#include "alamo/core/host_fs.hpp"
#include "alamo/vfs/host_path.hpp"
#include "alamo/vfs/install_locations.hpp"
#include "alamo/meg/meg.h"

namespace alamo::vfs {
namespace fs = std::filesystem;

namespace {

// ---- lenient JSON ----

struct JValue {
    enum class T { Null, Bool, Num, Str, Arr, Obj } t = T::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<JValue> a;
    std::vector<std::pair<std::string, JValue>> o;  // member order kept
    const JValue* get(std::string_view k) const {
        for (const auto& [key, v] : o)
            if (key == k) return &v;
        return nullptr;
    }
};

class JParser {
public:
    JParser(std::string_view t, std::vector<std::string>* w) : t_(t), w_(w) {}
    bool parse(JValue& out) {
        skip();
        if (p_ + 3 <= t_.size() && static_cast<unsigned char>(t_[p_]) == 0xEF && static_cast<unsigned char>(t_[p_ + 1]) == 0xBB &&
            static_cast<unsigned char>(t_[p_ + 2]) == 0xBF) {
            p_ += 3;
            skip();
        }
        if (!value(out, 0)) return false;
        skip();
        if (p_ < t_.size()) warn("text after the top-level value ignored");
        return true;
    }

private:
    void warn(const std::string& m) {
        if (w_) w_->push_back("modinfo: " + m);
    }
    void skip() {
        for (;;) {
            while (p_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[p_]))) ++p_;
            if (p_ + 1 < t_.size() && t_[p_] == '/' && t_[p_ + 1] == '/') {
                while (p_ < t_.size() && t_[p_] != '\n') ++p_;
            } else if (p_ + 1 < t_.size() && t_[p_] == '/' && t_[p_ + 1] == '*') {
                const size_t e = t_.find("*/", p_ + 2);
                p_ = e == std::string_view::npos ? t_.size() : e + 2;
            } else {
                return;
            }
        }
    }
    static void put_utf8(std::string& s, uint32_t cp) {
        if (cp < 0x80) {
            s.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    bool hex4(uint32_t& v) {
        if (p_ + 4 > t_.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = t_[p_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& s) {
        const char q = t_[p_++];  // '"' (or '\'' leniently)
        while (p_ < t_.size() && t_[p_] != q) {
            char c = t_[p_++];
            if (c != '\\') {
                s.push_back(c);
                continue;
            }
            if (p_ >= t_.size()) return false;
            c = t_[p_++];
            switch (c) {
                case 'n': s.push_back('\n'); break;
                case 't': s.push_back('\t'); break;
                case 'r': s.push_back('\r'); break;
                case 'b': s.push_back('\b'); break;
                case 'f': s.push_back('\f'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && p_ + 6 <= t_.size() && t_[p_] == '\\' && t_[p_ + 1] == 'u') {
                        p_ += 2;
                        uint32_t lo = 0;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    put_utf8(s, cp);
                    break;
                }
                default: s.push_back(c); break;  // \" \\ \/ and, leniently, a backslash before anything else
            }
        }
        if (p_ >= t_.size()) return false;
        ++p_;
        return true;
    }
    bool value(JValue& v, int depth) {
        if (depth > 64) return false;
        skip();
        if (p_ >= t_.size()) return false;
        const char c = t_[p_];
        if (c == '{' || c == '[') {
            const bool obj = c == '{';
            const char close = obj ? '}' : ']';
            v.t = obj ? JValue::T::Obj : JValue::T::Arr;
            ++p_;
            bool need_sep = false;
            for (;;) {
                skip();
                if (p_ >= t_.size()) return false;
                if (t_[p_] == close) {
                    ++p_;
                    return true;
                }
                if (t_[p_] == ',') {  // a separator (or a stray / trailing one)
                    ++p_;
                    need_sep = false;
                    continue;
                }
                if (need_sep) warn("missing comma before a member (accepted)");
                if (obj) {
                    std::string key;
                    if (t_[p_] != '"' && t_[p_] != '\'') return false;
                    if (!string(key)) return false;
                    skip();
                    if (p_ >= t_.size() || t_[p_] != ':') return false;
                    ++p_;
                    JValue m;
                    if (!value(m, depth + 1)) return false;
                    v.o.emplace_back(std::move(key), std::move(m));
                } else {
                    JValue m;
                    if (!value(m, depth + 1)) return false;
                    v.a.push_back(std::move(m));
                }
                need_sep = true;
            }
        }
        if (c == '"' || c == '\'') {
            v.t = JValue::T::Str;
            return string(v.s);
        }
        if (t_.compare(p_, 4, "true") == 0) {
            v.t = JValue::T::Bool;
            v.b = true;
            p_ += 4;
            return true;
        }
        if (t_.compare(p_, 5, "false") == 0) {
            v.t = JValue::T::Bool;
            p_ += 5;
            return true;
        }
        if (t_.compare(p_, 4, "null") == 0) {
            p_ += 4;
            return true;
        }
        size_t e = p_;
        while (e < t_.size() && (std::isdigit(static_cast<unsigned char>(t_[e])) || t_[e] == '-' || t_[e] == '+' || t_[e] == '.' ||
                                 t_[e] == 'e' || t_[e] == 'E'))
            ++e;
        if (e == p_) return false;
        v.t = JValue::T::Num;
        v.s = std::string(t_.substr(p_, e - p_));  // as written: a version 1.10 stays "1.10"
        v.n = std::strtod(v.s.c_str(), nullptr);
        p_ = e;
        return true;
    }

    std::string_view t_;
    std::vector<std::string>* w_;
    size_t p_ = 0;
};

std::string str_of(const JValue* v) {
    if (!v) return {};
    if (v->t == JValue::T::Str) return v->s;
    if (v->t == JValue::T::Num) return v->s;
    return {};
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool read_text(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

uint64_t fnv(uint64_t h, const void* d, size_t n) {
    const auto* b = static_cast<const unsigned char*>(d);
    for (size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}
constexpr uint64_t kFnvBasis = 1469598103934665603ull;

bool is_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// Every run of ASCII digits in `text`, sorted, each once (ModDescriptor::modinfo_numbers).
std::vector<std::string> digit_runs(std::string_view text) {
    std::set<std::string> out;
    for (size_t i = 0; i < text.size();) {
        if (text[i] < '0' || text[i] > '9') {
            ++i;
            continue;
        }
        const size_t b = i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
        out.insert(std::string(text.substr(b, i - b)));
    }
    return {out.begin(), out.end()};
}

}  // namespace

std::optional<ModInfo> parse_modinfo(std::string_view text, std::vector<std::string>* warnings) {
    JValue root;
    JParser p(text, warnings);
    if (!p.parse(root) || root.t != JValue::T::Obj) return std::nullopt;
    ModInfo m;
    m.name = str_of(root.get("name"));
    m.summary = str_of(root.get("summary"));
    if (m.summary.empty()) m.summary = str_of(root.get("description"));  // ums3: some modinfo files say "description"
    m.icon = str_of(root.get("icon"));
    m.version = str_of(root.get("version"));
    if (m.name.empty() && warnings) warnings->push_back("modinfo: no \"name\"");
    if (const JValue* l = root.get("languages"); l && l->t == JValue::T::Arr) {
        m.has_languages = true;
        for (const JValue& e : l->a) {
            ModLanguage lang;
            lang.code = lower(str_of(e.get("code")));
            const JValue* s = e.get("support");
            lang.support = s && s->t == JValue::T::Num ? static_cast<uint32_t>(s->n) : 1u;
            if (!lang.code.empty()) m.languages.push_back(std::move(lang));
        }
    }
    if (const JValue* d = root.get("dependencies"); d && d->t == JValue::T::Arr) {
        m.has_dependencies = true;
        for (size_t i = 0; i < d->a.size(); ++i) {
            const JValue& e = d->a[i];
            if (e.t == JValue::T::Str) {  // the resolve layout, first element only
                if (i == 0 && e.s == "FullResolved") m.layout = DependencyLayout::FullResolved;
                else if (i == 0 && e.s == "ResolveLastItem") m.layout = DependencyLayout::ResolveLastItem;
                else if (i != 0 || e.s != "ResolveRecursive") {
                    if (warnings) warnings->push_back("modinfo: unknown dependency entry \"" + e.s + "\" ignored");
                }
                continue;
            }
            if (e.t != JValue::T::Obj) continue;
            ModDependency dep;
            const JValue* mt = e.get("modtype");
            dep.modtype = mt && mt->t == JValue::T::Num ? static_cast<uint32_t>(mt->n) : 0u;
            dep.identifier = str_of(e.get("identifier"));
            dep.name = str_of(e.get("name"));
            if (!dep.identifier.empty()) m.dependencies.push_back(std::move(dep));
        }
    }
    const auto tags = [&](const JValue* t) {
        if (!t || t->t != JValue::T::Arr) return;
        for (const JValue& e : t->a)
            if (e.t == JValue::T::Str) m.steam_tags.push_back(e.s);
    };
    if (const JValue* sd = root.get("steamdata"); sd && sd->t == JValue::T::Obj) {
        tags(sd->get("tags"));
        m.published_id = str_of(sd->get("publishedfileid"));
    }
    tags(root.get("tags"));  // uploader files that put steamdata's keys at the top level
    if (m.published_id.empty()) m.published_id = str_of(root.get("publishedfileid"));
    return m;
}

const char* mod_kind_name(ModKind kind) {
    switch (kind) {
        case ModKind::Workshop: return "workshop";
        case ModKind::LocalMods: return "local";
        case ModKind::Path: break;
    }
    return "path";
}

const char* game_tag_name(GameTag tag) {
    switch (tag) {
        case GameTag::Foc: return "FOC";
        case GameTag::Eaw: return "EAW";
        case GameTag::Unknown: break;
    }
    return "";
}

std::string StackMod::reason_text() const {
    switch (reason) {
        case StackReason::Picked: break;
        case StackReason::Dependency: return "dependency of " + of;
        case StackReason::LikelyParent: return "likely parent of " + of;
    }
    return "picked";
}

std::string ModDescriptor::key() const { return variant.empty() ? id : id + ":" + variant; }

std::string ModDescriptor::catalog_key() const {
    std::string k = kind == ModKind::Workshop ? id : kind == ModKind::LocalMods ? "Mods\\" + id : root;
    return variant.empty() ? k : k + ":" + variant;
}

std::string ModDescriptor::arg() const {
    switch (kind) {
        case ModKind::Workshop: return "STEAMMOD=" + id;
        case ModKind::LocalMods: return "MODPATH=Mods\\" + id;
        case ModKind::Path: break;
    }
    return "MODPATH=" + root;
}

uint64_t mod_content_hash(const std::string& root) {
    const fs::path data = path_from_utf8(root) / "Data";
    std::error_code ec;
    if (!fs::is_directory(data, ec)) return 0;
    struct F {
        std::string rel;
        uint64_t size;
        int64_t time;
    };
    std::vector<F> files;
    for (fs::recursive_directory_iterator it(data, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        F f;
        f.rel = upper(path_to_utf8(fs::relative(it->path(), data, ec)));
        f.size = it->file_size(ec);
        f.time = static_cast<int64_t>(it->last_write_time(ec).time_since_epoch().count());
        files.push_back(std::move(f));
    }
    std::sort(files.begin(), files.end(), [](const F& a, const F& b) { return a.rel < b.rel; });
    uint64_t h = kFnvBasis;
    for (const F& f : files) {
        h = fnv(h, f.rel.data(), f.rel.size() + 0);
        h = fnv(h, "\0", 1);
        h = fnv(h, &f.size, sizeof f.size);
        h = fnv(h, &f.time, sizeof f.time);
    }
    return h == 0 ? 1 : h;
}

std::vector<ModDescriptor> describe_mod_folder(const std::string& root, ModKind kind, const std::string& id, bool hash) {
    const fs::path dir = path_from_utf8(root);
    std::error_code ec;
    ModDescriptor base;
    base.kind = kind;
    base.id = id;
    base.root = root;
    base.name = id;
    base.has_data = fs::is_directory(dir / "Data", ec);
    base.languages = {{"en", 7u}};
    if (hash) base.content_hash = mod_content_hash(root);

    const auto apply = [](ModDescriptor& d, const ModInfo& m) {
        d.has_modinfo = true;
        if (!m.name.empty()) {
            d.name = m.name;
            d.named = true;
        }
        if (!m.published_id.empty()) d.published_id = m.published_id;
        if (!m.version.empty()) d.version = m.version;
        if (!m.summary.empty()) d.summary = m.summary;
        if (!m.icon.empty()) d.icon = m.icon;
        if (m.has_languages) d.languages = m.languages;
        if (m.has_dependencies) {
            d.dependencies = m.dependencies;
            d.layout = m.layout;
        }
        bool eaw = false, foc = false;
        for (const std::string& t : m.steam_tags) {
            const std::string u = upper(t);
            eaw = eaw || u == "EAW";
            foc = foc || u == "FOC";
        }
        if (foc) d.game_tag = GameTag::Foc;
        else if (eaw) d.game_tag = GameTag::Eaw;
    };
    const auto read = [&](const fs::path& p, ModDescriptor& d) {
        std::string text;
        if (!read_text(p, text)) return;
        if (&d == &base) d.modinfo_numbers = digit_runs(text);  // the main modinfo.json's (variants inherit them)
        std::vector<std::string> w;
        const auto m = parse_modinfo(text, &w);
        for (std::string& s : w) d.warnings.push_back(path_to_utf8(p.filename()) + ": " + s);
        if (m) apply(d, *m);
        else d.warnings.push_back(path_to_utf8(p.filename()) + ": not readable as JSON; the folder name is used");
    };

    std::vector<ModDescriptor> out;
    bool main_file = false;
    std::vector<std::pair<std::string, fs::path>> variants;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string n = lower(path_to_utf8(it->path().filename()));
        if (n == "modinfo.json") main_file = true;
        else if (n.size() > 13 && n.compare(n.size() - 13, 13, "-modinfo.json") == 0)
            variants.emplace_back(path_to_utf8(it->path().filename()).substr(0, n.size() - 13), it->path());
    }
    std::sort(variants.begin(), variants.end());
    if (main_file) read(dir / "modinfo.json", base);
    if (main_file || variants.empty()) out.push_back(base);
    for (const auto& [name, p] : variants) {
        ModDescriptor d = base;
        d.variant = name;
        d.warnings.clear();
        if (!main_file) d.name = id + " (" + name + ")";
        read(p, d);
        out.push_back(std::move(d));
    }
    return out;
}

ModDescriptor describe_mounted_mod(const std::string& dir, const std::string& game, bool hash) {
    const fs::path p = path_from_utf8(dir).lexically_normal();
    fs::path leaf = p;
    if (!leaf.has_filename()) leaf = leaf.parent_path();
    const std::string id = path_to_utf8(leaf.filename());
    const std::string parent = folder_key(path_to_utf8(leaf.parent_path()));
    ModKind kind = ModKind::Path;
    if (is_digits(id) && lower(path_to_utf8(leaf.parent_path().filename())) == "32470") kind = ModKind::Workshop;
    else if (parent == folder_key(path_to_utf8((path_from_utf8(game) / "Mods").lexically_normal()))) kind = ModKind::LocalMods;
    std::vector<ModDescriptor> ds = describe_mod_folder(path_to_utf8(leaf), kind, id, hash);
    if (ds.empty()) {
        ModDescriptor d;
        d.kind = kind;
        d.id = d.name = id;
        d.root = path_to_utf8(leaf);
        return d;
    }
    return std::move(ds.front());
}

const ModDescriptor* ModCatalog::find_key(std::string_view key) const {
    const std::string want = path_key(key);  // any case, '/' == '\\'
    for (const ModDescriptor& m : mods)  // the catalog key first: it is unique across kinds
        if (path_key(m.catalog_key()) == want) return &m;
    for (const ModDescriptor& m : mods)
        if (path_key(m.key()) == want) return &m;
    return nullptr;
}

const ModDescriptor* ModCatalog::find_root(std::string_view root) const {
    // The folder as spelled first (no disk access; a row reached under its own spelling keeps its kind, so MODPATH=Mods\X
    // stays the Mods row when a junction makes it a workshop item too), then the folder a junction or subst drive leads to.
    const auto find = [&](auto key_of) -> const ModDescriptor* {
        const std::string want = key_of(root);
        const ModDescriptor* first = nullptr;  // a folder with variant files only: its first variant
        for (const ModDescriptor& m : mods)
            if (key_of(m.root) == want) {
                if (m.variant.empty()) return &m;
                if (!first) first = &m;
            }
        return first;
    };
    if (const ModDescriptor* m = find([](std::string_view p) { return lexical_folder_key(p); })) return m;
    return find([](std::string_view p) { return folder_key(p); });
}

const ModDescriptor* ModCatalog::find_workshop(std::string_view id) const {
    const ModDescriptor* first = nullptr;
    for (const ModDescriptor& m : mods)
        if (m.kind == ModKind::Workshop && m.id == id) {
            if (m.variant.empty()) return &m;
            if (!first) first = &m;
        }
    return first;
}

const ModDescriptor* ModCatalog::find_dependency(const ModDependency& d) const {
    if (d.modtype == 1) {
        return find_workshop(d.identifier);
    }
    if (d.modtype == 0) {
        // A local path: absolute, relative to the game folder, or a Mods\ folder name.
        std::error_code ec;
        fs::path p = path_from_utf8(d.identifier);
        if (p.is_relative()) p = path_from_utf8(game) / p;
        if (const ModDescriptor* m = find_root(path_to_utf8(p.lexically_normal()))) return m;
        for (const ModDescriptor& m : mods)
            if (m.kind == ModKind::LocalMods && m.variant.empty() && lower(m.id) == lower(path_to_utf8(path_from_utf8(d.identifier).filename())))
                return &m;
    }
    return nullptr;
}

const ModDescriptor* ModCatalog::find_arg(std::string_view arg, std::vector<ModDescriptor>& scratch) const {
    const size_t eq = arg.find('=');
    if (eq == std::string_view::npos) return find_key(arg);
    const std::string sw = upper(std::string(arg.substr(0, eq)));
    const std::string val(arg.substr(eq + 1));
    if (val.empty()) return nullptr;
    if (sw == "STEAMMOD") {
        if (val.find(':') != std::string::npos) return find_key(val);
        return find_workshop(val);
    }
    if (sw != "MODPATH") return nullptr;
    fs::path p = path_from_utf8(val);
    if (p.is_relative()) p = path_from_utf8(game) / p;
    if (const ModDescriptor* m = find_root(path_to_utf8(p.lexically_normal()))) return m;
    std::error_code ec;
    if (!fs::is_directory(p, ec)) return nullptr;
    auto ds = describe_mod_folder(path_to_utf8(p), ModKind::Path, path_to_utf8(p.filename()));
    if (ds.empty()) return nullptr;
    scratch.push_back(ds.front());
    return &scratch.back();
}

ModCatalog scan_mod_catalog(const InstallLocations& where, bool hash) {
    ModCatalog r;
    r.game = where.foc;
    std::set<std::string> seen, seen_roots;
    std::error_code ec;
    const auto folders = [&](const fs::path& dir, bool digits) {
        std::vector<fs::path> v;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            const std::string n = path_to_utf8(it->path().filename());
            if (digits && !is_digits(n)) continue;
            if (!fs::is_directory(it->path() / "Data", ec)) continue;
            v.push_back(it->path());
        }
        ec.clear();
        std::sort(v.begin(), v.end(), [](const fs::path& a, const fs::path& b) { return path_to_utf8(a.filename()) < path_to_utf8(b.filename()); });
        return v;
    };
    const auto add = [&](const fs::path& folder, ModKind kind, const std::string& id) {
        const std::string root = path_to_utf8(folder);
        // An extra mod folder already listed (inside Mods, or a junction to a listed folder) is skipped. Workshop items
        // and Mods folders are retail's own argument forms: each keeps its row even when a junction makes two of them
        // one folder, so a saved MODPATH= / STEAMMOD= keeps naming the row it named. Only an extra folder opens the
        // listed folders to compare where they lead (folder_key).
        if (kind == ModKind::Path) {
            const std::string identity = folder_key(root);
            for (const ModDescriptor& m : r.mods)
                if (folder_key(m.root) == identity) return;
        }
        if (!seen_roots.insert(lexical_folder_key(root)).second) return;  // the same spelling twice
        for (ModDescriptor& d : describe_mod_folder(root, kind, id, hash)) r.mods.push_back(std::move(d));
    };
    std::vector<std::pair<std::string, fs::path>> workshop;  // (id, folder)
    for (const std::string& lib : where.steam_libraries) {
        const fs::path content = path_from_utf8(lib) / "steamapps" / "workshop" / "content" / "32470";
        for (const fs::path& item : folders(content, true)) {
            const std::string id = path_to_utf8(item.filename());
            if (!seen.insert(id).second) continue;  // the first library holding an id wins (the STEAMMOD= resolver)
            workshop.emplace_back(id, item);
        }
    }
    // Numeric order of the ids across all libraries (the picker's order before ums1).
    std::sort(workshop.begin(), workshop.end(), [](const auto& a, const auto& b) {
        return a.first.size() != b.first.size() ? a.first.size() < b.first.size() : a.first < b.first;
    });
    for (const auto& [id, item] : workshop) add(item, ModKind::Workshop, id);
    if (!where.foc.empty())
        for (const fs::path& local : folders(path_from_utf8(where.foc) / "Mods", false)) add(local, ModKind::LocalMods, path_to_utf8(local.filename()));
    for (const std::string& extra : where.mod_folders) {
        const fs::path dir = path_from_utf8(extra);
        if (fs::is_directory(dir / "Data", ec)) {
            add(dir, ModKind::Path, path_to_utf8(dir.filename()));
            continue;
        }
        for (const fs::path& sub : folders(dir, false)) add(sub, ModKind::Path, path_to_utf8(sub.filename()));
    }
    return r;
}

bool mod_provides(const std::string& root, std::string_view path) {
    const fs::path dir = path_from_utf8(root);
    std::error_code ec;
    std::string rel(path);
    for (char& c : rel)
        if (c == '\\') c = '/';
    if (fs::is_regular_file(dir / path_from_utf8(rel), ec)) return true;
    // Case-insensitive loose lookup (folders as authors spell them).
    {
        fs::path cur = dir;
        bool ok = true;
        size_t s = 0;
        while (ok && s <= rel.size()) {
            const size_t e = std::min(rel.find('/', s), rel.size());
            const std::string part = lower(rel.substr(s, e - s));
            bool found = false;
            for (fs::directory_iterator it(cur, ec), end; !ec && it != end; it.increment(ec))
                if (lower(path_to_utf8(it->path().filename())) == part) {
                    cur = it->path();
                    found = true;
                    break;
                }
            ec.clear();
            ok = found;
            s = e + 1;
        }
        if (ok && fs::is_regular_file(cur, ec)) return true;
    }
    for (fs::directory_iterator it(dir / "Data", ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || lower(path_to_utf8(it->path().extension())) != ".meg") continue;
        std::string err;
        const auto a = meg::Archive::open(path_to_utf8(it->path()), meg::Source::Mode::Stream, &err);
        if (a && a->lookup(path)) return true;  // the game's rule (docs/re/MCX3_MEG_LOOKUP.md)
    }
    return false;
}

std::vector<std::string> ModStack::launch_args() const {
    std::vector<std::string> out;
    if (base_eaw) return out;  // D1: refused
    std::set<std::string> seen;  // folders: two variants of one folder, or one folder spelled two ways, mount once
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string folder = i < mods.size() ? lexical_folder_key(mods[i].mod.root) : path_key(args[i]);
        if (seen.insert(folder).second) out.push_back(args[i]);
    }
    return out;
}

ModStack resolve(const ModCatalog& registry, std::span<const std::string> picked_args, const ParentHint& hint) {
    ModStack r;
    std::vector<ModDescriptor> scratch;
    scratch.reserve(picked_args.size() + 8);
    std::vector<StackMod> found;                    // discovery order
    std::vector<std::vector<size_t>> deps;          // found index -> found indices it depends on
    std::map<std::string, size_t> index_of;         // lower-case key -> found index
    const auto key_of = [](const ModDescriptor& m) { return path_key(m.catalog_key()); };  // unique across kinds
    const auto add = [&](const ModDescriptor& m, StackReason reason, std::string of) -> size_t {
        const auto it = index_of.find(key_of(m));
        if (it != index_of.end()) return it->second;
        found.push_back({m, reason, std::move(of)});
        deps.emplace_back();
        index_of[key_of(m)] = found.size() - 1;
        return found.size() - 1;
    };
    std::vector<size_t> queue;
    std::vector<bool> queued;  // a found mod whose own dependencies are (to be) expanded
    const auto enqueue = [&](size_t j) {
        if (queued.size() <= j) queued.resize(j + 1, false);
        if (!queued[j]) {
            queued[j] = true;
            queue.push_back(j);
        }
    };
    for (const std::string& a : picked_args) {
        const ModDescriptor* m = registry.find_arg(a, scratch);
        if (!m) {
            r.errors.push_back("not a mod this install has: " + a);
            continue;
        }
        enqueue(add(*m, StackReason::Picked, {}));
    }
    // Breadth-first over declared dependencies (nearest ancestor first), the hint for a mod that declares none.
    for (size_t q = 0; q < queue.size(); ++q) {
        const size_t i = queue[q];
        const ModDescriptor me = found[i].mod;
        const std::string me_key = me.catalog_key();
        if (!me.dependencies.empty()) {
            for (size_t k = 0; k < me.dependencies.size(); ++k) {
                const ModDependency& d = me.dependencies[k];
                const ModDescriptor* p = registry.find_dependency(d);
                if (!p) {
                    r.warnings.push_back("missing parent of " + me_key + ": " + (d.modtype == 1 ? "workshop item " : "") + d.identifier +
                                         (d.name.empty() ? std::string() : " (" + d.name + ")") + " is not installed");
                    continue;
                }
                const size_t j = add(*p, StackReason::Dependency, me_key);
                if (std::find(deps[i].begin(), deps[i].end(), j) == deps[i].end()) deps[i].push_back(j);
                const bool recurse = me.layout == DependencyLayout::ResolveRecursive ||
                                     (me.layout == DependencyLayout::ResolveLastItem && k + 1 == me.dependencies.size());
                if (recurse) enqueue(j);
            }
        } else if (hint) {
            std::vector<std::string> stack_args{me.arg()};
            for (size_t k = 0; k < found.size(); ++k)
                if (k != i) stack_args.push_back(found[k].mod.arg());
            if (const auto parent = hint(stack_args)) {
                if (const ModDescriptor* p = registry.find_arg(*parent, scratch)) {
                    const size_t j = add(*p, StackReason::LikelyParent, me_key);
                    if (j != i && std::find(deps[i].begin(), deps[i].end(), j) == deps[i].end()) deps[i].push_back(j);
                    enqueue(j);
                }
            }
        }
    }
    // Cycles among the dependency edges.
    {
        std::vector<int> state(found.size(), 0);  // 0 new, 1 on the path, 2 done
        std::vector<size_t> path;
        std::function<void(size_t)> dfs = [&](size_t v) {
            state[v] = 1;
            path.push_back(v);
            for (size_t w : deps[v]) {
                if (state[w] == 1) {
                    std::string cyc;
                    auto at = std::find(path.begin(), path.end(), w);
                    for (; at != path.end(); ++at) cyc += found[*at].mod.catalog_key() + " -> ";
                    cyc += found[w].mod.catalog_key();
                    r.errors.push_back("dependency cycle: " + cyc);
                } else if (state[w] == 0) {
                    dfs(w);
                }
            }
            path.pop_back();
            state[v] = 2;
        };
        for (size_t v = 0; v < found.size(); ++v)
            if (state[v] == 0) dfs(v);
    }
    // Order: a mod only after every mod that depends on it (submod first); ties keep discovery order. With a cycle the
    // remaining mods follow in discovery order.
    {
        std::vector<size_t> dependents(found.size(), 0);
        for (size_t v = 0; v < found.size(); ++v)
            for (size_t w : deps[v]) ++dependents[w];
        std::vector<bool> placed(found.size(), false);
        for (size_t n = 0; n < found.size(); ++n) {
            size_t pick = found.size();
            for (size_t v = 0; v < found.size() && pick == found.size(); ++v)
                if (!placed[v] && dependents[v] == 0) pick = v;
            if (pick == found.size())
                for (size_t v = 0; v < found.size() && pick == found.size(); ++v)
                    if (!placed[v]) pick = v;
            placed[pick] = true;
            for (size_t w : deps[pick])
                if (dependents[w] > 0) --dependents[w];
            r.mods.push_back(found[pick]);
        }
    }
    // The command line as picked: a mod listed above a mod that depends on it (design 15, "a submod above its parent").
    for (size_t v = 0; v < picked_args.size(); ++v)
        for (size_t w = v + 1; w < picked_args.size(); ++w) {
            // Copies: find_arg may describe a folder into `scratch`, which can move what an earlier call returned.
            const ModDescriptor* fv = registry.find_arg(picked_args[v], scratch);
            if (!fv) continue;
            const ModDescriptor dv = *fv;
            const ModDescriptor* fw = registry.find_arg(picked_args[w], scratch);
            if (!fw) continue;
            const ModDescriptor dw_mod = *fw;
            const ModDescriptor* pv = &dv;
            const ModDescriptor* pw = &dw_mod;
            const auto iv = index_of.find(key_of(*pv)), iw = index_of.find(key_of(*pw));
            if (iv == index_of.end() || iw == index_of.end()) continue;
            const std::vector<size_t>& dw = deps[iw->second];
            if (std::find(dw.begin(), dw.end(), iv->second) != dw.end())
                r.warnings.push_back(pv->catalog_key() + " is picked above its submod " + pw->catalog_key() +
                                     "; the earlier mod wins, so the stack puts " + pw->catalog_key() + " first");
        }
    for (StackMod& s : r.mods)
        if (s.mod.content_hash == 0 && s.mod.has_data) s.mod.content_hash = mod_content_hash(s.mod.root);
    r.fingerprint = kFnvBasis;
    for (const StackMod& s : r.mods) {
        r.args.push_back(s.mod.arg());
        r.fingerprint = fnv(r.fingerprint, &s.mod.content_hash, sizeof s.mod.content_hash);
        if (!s.mod.has_data) r.warnings.push_back(s.mod.catalog_key() + " has no Data folder: nothing to mount");
        if (s.mod.game_tag == GameTag::Eaw) {
            r.base_eaw = true;
            r.errors.push_back(s.mod.catalog_key() + ": " + kBaseEawRefusal);
        }
        for (const std::string& w : s.mod.warnings) r.warnings.push_back(s.mod.catalog_key() + ": " + w);
    }
    // Two total conversions without a dependency between them: retail runs the earlier one's lists.
    {
        std::vector<size_t> tc;
        for (size_t i = 0; i < r.mods.size(); ++i)
            if (mod_provides(r.mods[i].mod.root, "Data\\XML\\GameObjectFiles.xml") &&
                mod_provides(r.mods[i].mod.root, "Data\\XML\\FactionFiles.xml"))
                tc.push_back(i);
        const auto depends = [&](const ModDescriptor& a, const ModDescriptor& b) {  // a reaches b over found edges
            std::vector<size_t> st{index_of.at(key_of(a))};
            std::set<size_t> seen;
            const size_t target = index_of.at(key_of(b));
            while (!st.empty()) {
                const size_t v = st.back();
                st.pop_back();
                if (v == target) return true;
                if (!seen.insert(v).second) continue;
                for (size_t w : deps[v]) st.push_back(w);
            }
            return false;
        };
        for (size_t x = 0; x < tc.size(); ++x)
            for (size_t y = x + 1; y < tc.size(); ++y) {
                const ModDescriptor& a = r.mods[tc[x]].mod;
                const ModDescriptor& b = r.mods[tc[y]].mod;
                if (!depends(a, b) && !depends(b, a))
                    r.warnings.push_back("two total conversions in one stack: " + a.catalog_key() + " and " + b.catalog_key() +
                                         " each replace GameObjectFiles.xml and FactionFiles.xml; retail runs " + a.catalog_key() +
                                         "'s lists");
            }
    }
    return r;
}

}  // namespace alamo::vfs
