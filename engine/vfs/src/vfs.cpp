// alamo_vfs implementation. See include/alamo/vfs/vfs.hpp for the resolution rules.
#include "alamo/vfs/vfs.hpp"
#include "alamo/core/host_fs.hpp"
#include "alamo/vfs/steam_workshop.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <unordered_map>
#include <utility>

#include "alamo/core/compat_issue.hpp"
#include "alamo/core/hash.hpp"
#include "alamo/core/log.hpp"
#include "alamo/meg/meg.h"

namespace fs = std::filesystem;

namespace alamo::vfs {

namespace {

constexpr std::string_view kDataPrefix = "DATA\\";
constexpr const char* kLogChannel = "vfs";

bool is_data_path(std::string_view norm) {
    return norm.size() > kDataPrefix.size() && norm.substr(0, kDataPrefix.size()) == kDataPrefix;
}

constexpr char kHostSep = '\\';

// Converts '\\' and '/' to the host separator.
std::string to_host(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c == '\\' || c == '/') c = kHostSep;
    }
    return out;
}

bool is_absolute_path(std::string_view s) {
    if (s.empty()) return false;
    if (s[0] == '/' || s[0] == '\\') return true;
    const unsigned char c0 = static_cast<unsigned char>(s[0]);
    return s.size() >= 2 && s[1] == ':' && ((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z'));
}

std::string join_host(std::string_view base, std::string_view rel) {
    std::string out = to_host(base);
    while (!out.empty() && out.back() == kHostSep && out.size() > 1) out.pop_back();
    if (!out.empty()) out.push_back(kHostSep);
    out += to_host(rel);
    return out;
}

std::string_view trim(std::string_view s) {
    const auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && ws(s.front())) s.remove_prefix(1);
    while (!s.empty() && ws(s.back())) s.remove_suffix(1);
    return s;
}

// Long-path-safe file system queries: every std::filesystem call goes through long_path() (Windows
// "\\?\" form), so roots deeper than MAX_PATH work without the process-wide long-path opt-in. Paths
// stored and reported by the VFS stay in their plain form; long_path() is applied at each call.
bool exists_lp(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::exists(long_path(p), ec);
}
bool is_directory_lp(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(long_path(p), ec);
}

// Child of `dir` whose name equals `name` case-insensitively (exact match preferred; ties broken
// by smallest name so the result is deterministic). Empty path if none.
fs::path find_child_ci(const fs::path& dir, std::string_view name) {
    std::error_code ec;
    const fs::path exact = dir / path_from_utf8(name);
    if (exists_lp(exact)) return exact;
    std::string best;
    for (auto it = fs::directory_iterator(long_path(dir), fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string leaf = path_to_utf8(it->path().filename());
        if (iequals(leaf, name) && (best.empty() || leaf < best)) best = leaf;
    }
    return best.empty() ? fs::path() : dir / path_from_utf8(best);
}

// Resolves a host path whose components may differ in case from the disk (only matters on
// case-sensitive directories, e.g. WSL-enabled NTFS folders).
// Returns the input unchanged if it already exists or cannot be resolved.
fs::path resolve_ci(const std::string& host) {
    const fs::path p = path_from_utf8(host);
    if (exists_lp(p)) return p;
    fs::path cur = p.root_path();
    if (cur.empty()) cur = fs::path(".");
    for (const auto& comp : p.relative_path()) {
        const std::string c = path_to_utf8(comp);
        if (c.empty() || c == ".") continue;
        if (c == "..") {
            cur /= comp;
            continue;
        }
        const fs::path next = find_child_ci(cur, c);
        if (next.empty()) return p;
        cur = next;
    }
    return cur;
}

// One loose directory tier: index of <root>\Data, keyed by normalised "DATA\\..." paths.
struct LooseRoot {
    std::string root;      // as configured (host form)
    std::string data_dir;  // resolved host Data folder ("" if missing)
    std::unordered_map<std::string, std::string> index;  // normalised -> host path

    void build(const std::string& root_in) {
        root = to_host(root_in);
        index.clear();
        std::error_code ec;
        const fs::path r = resolve_ci(root);
        if (!is_directory_lp(r)) return;
        const fs::path d = find_child_ci(r, "Data");
        if (d.empty() || !is_directory_lp(d)) return;
        data_dir = path_to_utf8(d);
        // Walk the long form; entries are re-rooted on the plain `d` for the stored host paths.
        const fs::path ld = long_path(d);
        for (auto it = fs::recursive_directory_iterator(ld, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::error_code ec2;
            if (!it->is_regular_file(ec2)) continue;
            const fs::path rel = it->path().lexically_relative(ld);
            if (rel.empty()) continue;
            std::string key = std::string(kDataPrefix) + normalize_path(path_to_utf8(rel));
            std::string host = path_to_utf8(d / rel);
            auto [pos, inserted] = index.emplace(std::move(key), host);
            // Case-only duplicates (case-sensitive directories): keep the smallest host path, deterministically.
            if (!inserted && host < pos->second) pos->second = std::move(host);
        }
    }

    const std::string* find(const std::string& norm) const {
        const auto it = index.find(norm);
        return it == index.end() ? nullptr : &it->second;
    }
};

// CompatIssue::layer of a finding in a layer owned by mod `mod` (-1 = the base game): the mod's index, or kBaseLayer.
uint16_t issue_layer_for(int mod) { return mod >= 0 ? static_cast<uint16_t>(mod) : alamo::kBaseLayer; }

struct Hit {
    Vfs::Resolution::Layer layer = Vfs::Resolution::Base;
    int index = 0;
    const std::string* host = nullptr;        // loose
    const meg::Archive* archive = nullptr;    // meg
    const meg::Entry* entry = nullptr;
};

bool glob_rec(std::string_view p, std::string_view t) {
    while (!p.empty()) {
        if (p.size() >= 2 && p[0] == '*' && p[1] == '*') {
            p.remove_prefix(2);
            for (size_t k = 0; k <= t.size(); ++k) {
                if (glob_rec(p, t.substr(k))) return true;
            }
            return false;
        }
        if (p[0] == '*') {
            p.remove_prefix(1);
            for (size_t k = 0; k <= t.size(); ++k) {
                if (glob_rec(p, t.substr(k))) return true;
                if (k < t.size() && t[k] == '\\') break;
            }
            return false;
        }
        if (t.empty()) return false;
        if (p[0] == '?') {
            if (t[0] == '\\') return false;
        } else if (p[0] != t[0]) {
            return false;
        }
        p.remove_prefix(1);
        t.remove_prefix(1);
    }
    return t.empty();
}

}  // namespace

// ------------------------------------------------------------------ MegaFiles.xml

std::vector<std::string> parse_megafiles_xml(std::span<const uint8_t> bytes) {
    std::string text;
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        // UTF-16LE: keep ASCII, replace the rest with '?'.
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            const unsigned v = static_cast<unsigned>(bytes[i]) | (static_cast<unsigned>(bytes[i + 1]) << 8);
            text.push_back(v < 0x80 ? static_cast<char>(v) : '?');
        }
    } else {
        size_t start = 0;
        if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) start = 3;
        text.assign(reinterpret_cast<const char*>(bytes.data()) + start, bytes.size() - start);
    }
    // Strip comments.
    for (size_t c = text.find("<!--"); c != std::string::npos; c = text.find("<!--", c)) {
        const size_t e = text.find("-->", c + 4);
        text.erase(c, e == std::string::npos ? std::string::npos : e + 3 - c);
    }
    std::string upper = text;
    for (char& ch : upper) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    std::vector<std::string> out;
    size_t pos = 0;
    while (true) {
        const size_t open = upper.find("<FILE", pos);
        if (open == std::string::npos) break;
        const size_t after = open + 5;
        // Must be exactly the <File> element (allow attributes / whitespace), not <Files>.
        if (after >= upper.size() || (upper[after] != '>' && upper[after] != ' ' && upper[after] != '\t' &&
                                      upper[after] != '\r' && upper[after] != '\n' && upper[after] != '/')) {
            pos = after;
            continue;
        }
        const size_t gt = upper.find('>', after);
        if (gt == std::string::npos) break;
        if (upper[gt - 1] == '/') {  // <File/>
            pos = gt + 1;
            continue;
        }
        const size_t close = upper.find("</FILE", gt + 1);
        if (close == std::string::npos) break;
        const std::string_view v = trim(std::string_view(text).substr(gt + 1, close - gt - 1));
        if (!v.empty()) out.emplace_back(v);
        pos = close + 6;
    }
    return out;
}

bool path_glob_match(std::string_view pattern, std::string_view path) {
    return glob_rec(normalize_name(pattern), normalize_name(path));
}

// ------------------------------------------------------------------ command line

bool parse_command_line(std::span<const std::string> args, VfsConfig& cfg) {
    bool ok = true;
    for (const std::string& raw : args) {
        std::string_view a = trim(raw);
        if (a.size() >= 2 && a.front() == '"' && a.back() == '"') a = a.substr(1, a.size() - 2);
        const size_t eq = a.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = trim(a.substr(0, eq));
        std::string_view val = trim(a.substr(eq + 1));
        if (!val.empty() && val.front() == '"') val.remove_prefix(1);
        if (!val.empty() && val.back() == '"') val.remove_suffix(1);
        val = trim(val);
        const bool is_mod = iequals(key, "MODPATH");
        const bool is_fb = iequals(key, "FALLBACKPATH");
        const bool is_steam = iequals(key, "STEAMMOD");
        if (!is_mod && !is_fb && !is_steam) continue;
        if (val.empty()) {
            ok = false;
            continue;
        }
        if (is_steam) {
            cfg.mod_search.push_back({ModSearchEntry::Kind::SteamWorkshop, std::string(val)});
            continue;
        }
        std::string host = is_absolute_path(val) ? to_host(val) : join_host(cfg.foc_root, val);
        while (host.size() > 1 && host.back() == kHostSep) host.pop_back();
        if (is_mod) cfg.mod_search.push_back({ModSearchEntry::Kind::Path, std::move(host)});
        else cfg.fallback_paths.push_back(std::move(host));
    }
    return ok;
}

// ------------------------------------------------------------------ Vfs

struct Vfs::Impl {
    std::string foc_root;
    std::string eaw_root;  // "" = none
    std::vector<LooseRoot> mods;
    std::vector<std::string> mod_dirs;    // effective mod search list (host dirs), = mods[i].root input
    std::vector<std::string> mod_labels;  // describe(): "MODPATH" or "STEAMMOD=<id>"
    std::set<std::string> mounted_eaw, mounted_foc;  // normalize_path of requested MEG names
    LooseRoot base;
    std::vector<LooseRoot> fallbacks;  // FALLBACKPATH dirs, then GameData (if any)
    size_t user_fallbacks = 0;         // count of FALLBACKPATH dirs (GameData follows)
    std::vector<std::unique_ptr<meg::Archive>> archives;  // mount order
    std::vector<Mount> mounts;
    std::vector<int> mount_owner;  // per mount: the mod whose folder it was opened from (Layer::mod), -1 = base game
    std::vector<Layer> layers;     // resolution order (Vfs::layers), built at the end of create()
    alamo::IssueSink* issues = nullptr;  // create() only
    std::vector<std::string> warnings;
    std::vector<Vfs::MegIssue> meg_issues;  // mcx3 t04

    void warn(std::string msg) {
        log::warnf(kLogChannel, "%s", msg.c_str());
        warnings.push_back(std::move(msg));
    }

    // ums2 t03: a VFS finding is a warnings() line and, when create() was given a sink, a CompatIssue with its layer
    // (compat_issue.hpp: the owning mod's index, kBaseLayer, or kNoLayer when no layer is concerned).
    void finding(IssueKind kind, IssueSeverity severity, std::string subject, std::string source, uint16_t layer, std::string msg,
                 bool retail_equivalent = false) {
        if (issues) issues->add(kind, severity, std::move(subject), std::move(source), msg, retail_equivalent, layer);
        warn(std::move(msg));
    }

    bool find(const std::string& norm, Hit& out) const { return find_nth(norm, 0, out); }

    // The resolution order, the one walk every lookup uses: visit(hit) for each layer holding `norm` (mods, base, MEGs
    // newest first, fallbacks; loose layers for Data\ paths only) until visit returns true. True when it stopped.
    template <class Visit>
    bool walk(const std::string& norm, Visit&& visit) const {
        const bool data = is_data_path(norm);
        if (data) {
            for (size_t i = 0; i < mods.size(); ++i)
                if (const std::string* h = mods[i].find(norm))
                    if (visit(Hit{Resolution::Mod, static_cast<int>(i), h, nullptr, nullptr})) return true;
            if (const std::string* h = base.find(norm))
                if (visit(Hit{Resolution::Base, 0, h, nullptr, nullptr})) return true;
        }
        for (size_t i = archives.size(); i-- > 0;)
            if (const meg::Entry* e = archives[i]->lookup(norm))
                if (visit(Hit{Resolution::Meg, static_cast<int>(i), nullptr, archives[i].get(), e})) return true;
        if (data)
            for (size_t i = 0; i < fallbacks.size(); ++i)
                if (const std::string* h = fallbacks[i].find(norm))
                    if (visit(Hit{Resolution::Fallback, static_cast<int>(i), h, nullptr, nullptr})) return true;
        return false;
    }

    // Every copy of `norm` in resolution order.
    std::vector<Hit> find_all(const std::string& norm) const {
        std::vector<Hit> out;
        walk(norm, [&out](const Hit& h) {
            out.push_back(h);
            return false;
        });
        return out;
    }

    std::string location_of(const Hit& h) const {
        return h.archive ? mounts[static_cast<size_t>(h.index)].path + "#" + h.entry->name : *h.host;
    }

    // Vfs::layers(): mods, base, MEGs newest first, fallbacks (the order find_nth walks).
    void build_layers() {
        layers.clear();
        layers.reserve(mods.size() + 1 + mounts.size() + fallbacks.size());
        for (size_t i = 0; i < mods.size(); ++i) layers.push_back(Layer{Layer::Kind::Mod, static_cast<int>(i), mods[i].root, static_cast<int>(i)});
        layers.push_back(Layer{Layer::Kind::Base, 0, base.root, -1});
        for (size_t i = mounts.size(); i-- > 0;) layers.push_back(Layer{Layer::Kind::Meg, static_cast<int>(i), mounts[i].path, mount_owner[i]});
        for (size_t i = 0; i < fallbacks.size(); ++i) layers.push_back(Layer{Layer::Kind::Fallback, static_cast<int>(i), fallbacks[i].root, -1});
    }

    // Index into `layers` of (kind, index); layers.size() when out of range.
    size_t layer_slot(Resolution::Layer kind, int index) const {
        if (index < 0) return layers.size();
        const size_t i = static_cast<size_t>(index);
        switch (kind) {
            case Resolution::Mod: return i < mods.size() ? i : layers.size();
            case Resolution::Base: return i == 0 ? mods.size() : layers.size();
            case Resolution::Meg: return i < mounts.size() ? mods.size() + 1 + (mounts.size() - 1 - i) : layers.size();
            case Resolution::Fallback: return i < fallbacks.size() ? mods.size() + 1 + mounts.size() + i : layers.size();
        }
        return layers.size();
    }

    // The n-th copy of `norm` in resolution order (0 = what find() returns): the same walk, counting the layers that
    // hold the file instead of stopping at the first.
    bool find_nth(const std::string& norm, size_t n, Hit& out) const {
        return walk(norm, [&](const Hit& h) {
            if (n == 0) {
                out = h;
                return true;
            }
            --n;
            return false;
        });
    }

    // Newest-first over the first `count` mounted archives.
    bool find_meg(const std::string& norm, size_t count, Hit& out) const {
        for (size_t i = count; i-- > 0;) {
            if (const meg::Entry* e = archives[i]->lookup(norm)) {
                out = Hit{Resolution::Meg, static_cast<int>(i), nullptr, archives[i].get(), e};
                return true;
            }
        }
        return false;
    }

    // FileClass-style open during FoC mounting: mods -> base -> FALLBACKPATH dirs (GameData is
    // appended to the fallback list only after the FoC MEGs are mounted). *owner: the mod whose tree held it, else -1.
    const std::string* find_loose_for_mount(const std::string& norm, int* owner) const {
        *owner = -1;
        if (!is_data_path(norm)) return nullptr;
        for (size_t i = 0; i < mods.size(); ++i) {
            if (const std::string* h = mods[i].find(norm)) {
                *owner = static_cast<int>(i);
                return h;
            }
        }
        if (const std::string* h = base.find(norm)) return h;
        for (size_t i = 0; i < user_fallbacks; ++i) {
            if (const std::string* h = fallbacks[i].find(norm)) return h;
        }
        return nullptr;
    }

    // Add_Megafile: a requested name that is already mounted (on this side) is skipped.
    bool already_mounted(std::set<std::string>& requested, const std::string& norm) {
        return !requested.insert(norm).second;
    }

    void mount(const std::string& host, std::string reason, int owner) {
        // Opened through the long ("\\?\", native separators) form so archives under deep roots
        // mount; Mount::path keeps the plain host path for reporting.
        std::string err;
        const std::u8string native = long_path(path_from_utf8(host)).u8string();
        auto src = meg::Source::open(std::string(reinterpret_cast<const char*>(native.data()), native.size()),
                                     meg::Source::Mode::Auto, &err);
        auto a = src ? meg::Archive::open(std::move(src), &err) : nullptr;
        if (!a) {
            finding(IssueKind::AssetRejected, IssueSeverity::Warning, host, host, issue_layer_for(owner),
                    "cannot open MEG '" + host + "' (" + reason + "): " + err);
            return;
        }
        if (a->encrypted())
            finding(IssueKind::AssetRejected, IssueSeverity::Warning, host, host, issue_layer_for(owner),
                    "MEG '" + host + "' is encrypted; its entries are not visible");
        if (const meg::Archive::TableIssues t = a->table_issues(); t.any()) {  // mcx3 t04
            using K = Vfs::MegIssue::Kind;
            const auto add = [&](K kind, uint32_t count, const std::string& first, const std::string& what) {
                meg_issues.push_back(Vfs::MegIssue{kind, host, count, first});
                std::string msg = std::string(Vfs::MegIssue::kind_name(kind)) + ": MEG '" + host + "': " + what;
                // The game reads the table the same way (MODS.md §3.2): retail-equivalent. Index has no issue kind of its
                // own: a warnings() line only.
                if (kind == K::Index) {
                    warn(std::move(msg));
                    return;
                }
                const IssueKind ik = kind == K::Unsorted    ? IssueKind::MegUnsorted
                                     : kind == K::Duplicate ? IssueKind::MegDuplicate
                                                            : IssueKind::MegNameMismatch;
                finding(ik, IssueSeverity::Info, host, host, issue_layer_for(owner), std::move(msg), true);
            };
            if (t.unsorted)
                add(K::Unsorted, t.unreachable, t.first_unreachable,
                    "record table not sorted by CRC; " + std::to_string(t.unreachable) +
                        " record(s) the game's binary search cannot reach" +
                        (t.unreachable ? " (first " + t.first_unreachable + ")" : std::string()));
            if (t.duplicate)
                add(K::Duplicate, t.duplicate, t.first_duplicate,
                    std::to_string(t.duplicate) + " record(s) share a CRC with another; the search's probe order picks one (first shadowed " +
                        t.first_duplicate + ")");
            if (t.name_mismatch)
                add(K::NameMismatch, t.name_mismatch, t.first_name_mismatch,
                    std::to_string(t.name_mismatch) + " record(s) whose stored CRC is not their name's: found by CRC only (first " +
                        t.first_name_mismatch + ")");
            if (t.index_redirect)
                add(K::Index, t.index_redirect, t.first_index_redirect,
                    std::to_string(t.index_redirect) + " record(s) whose index field is not their position: the data read is the record it names (first " +
                        t.first_index_redirect + ")");
        }
        archives.push_back(std::move(a));
        mounts.push_back(Mount{host, std::move(reason)});
        mount_owner.push_back(owner);
    }

    void mount_eaw(const LooseRoot& eaw) {
        if (const std::string* xml = eaw.find("DATA\\MEGAFILES.XML")) {
            std::vector<uint8_t> bytes;
            if (read_host_file(*xml, bytes)) {
                for (const std::string& f : parse_megafiles_xml(bytes)) {
                    const std::string norm = normalize_path(f);
                    if (already_mounted(mounted_eaw, norm)) continue;
                    if (const std::string* h = eaw.find(norm)) {
                        mount(*h, "EaW MegaFiles.xml", -1);
                    } else {
                        finding(IssueKind::MissingFile, IssueSeverity::Warning, norm, "DATA\\MEGAFILES.XML", alamo::kBaseLayer,
                                "EaW MegaFiles.xml entry not found: " + f);
                    }
                }
            }
        } else {
            finding(IssueKind::MissingFile, IssueSeverity::Warning, "DATA\\MEGAFILES.XML", {}, alamo::kBaseLayer,
                    "EaW Data\\MegaFiles.xml not found under '" + eaw.root + "'");
        }
        static const char* const kPatches[][2] = {
            {"DATA\\PATCH.MEG", "EaW Patch"}, {"DATA\\PATCH2.MEG", "EaW patch2"}, {"DATA\\64PATCH.MEG", "EaW 64Patch"}};
        for (const auto& p : kPatches) {
            if (already_mounted(mounted_eaw, p[0])) continue;
            if (const std::string* h = eaw.find(p[0])) mount(*h, p[1], -1);
        }
    }

    void mount_foc() {
        const std::string xml_norm = "DATA\\MEGAFILES.XML";
        std::vector<uint8_t> bytes;
        bool have_xml = false;
        // MegaFiles.xml: mods -> base -> mounted (EaW) MEGs -> FALLBACKPATH dirs.
        const std::string* host = nullptr;
        int xml_owner = -1;  // the mod whose MegaFiles.xml is read, -1 = the base game's
        for (size_t i = 0; i < mods.size() && !host; ++i)
            if ((host = mods[i].find(xml_norm)) != nullptr) xml_owner = static_cast<int>(i);
        if (!host) host = base.find(xml_norm);
        if (host) {
            have_xml = read_host_file(*host, bytes);
        } else {
            Hit hit;
            if (find_meg(xml_norm, archives.size(), hit)) {
                have_xml = hit.archive->read(*hit.entry, bytes);
            } else {
                for (size_t i = 0; i < user_fallbacks && !have_xml; ++i) {
                    if (const std::string* fh = fallbacks[i].find(xml_norm)) have_xml = read_host_file(*fh, bytes);
                }
            }
        }
        if (have_xml) {
            for (const std::string& f : parse_megafiles_xml(bytes)) {
                const std::string norm = normalize_path(f);
                if (already_mounted(mounted_foc, norm)) continue;
                // Normal file open: a mod's copy of the same relative path wins, and the base copy
                // is then never mounted.
                int owner = -1;
                if (const std::string* h = find_loose_for_mount(norm, &owner)) {
                    mount(*h, "FoC MegaFiles.xml", owner);
                } else {
                    finding(IssueKind::MissingFile, IssueSeverity::Warning, norm, "DATA\\MEGAFILES.XML", issue_layer_for(xml_owner),
                            "FoC MegaFiles.xml entry not found: " + f);
                }
            }
        } else {
            finding(IssueKind::MissingFile, IssueSeverity::Warning, xml_norm, {}, alamo::kBaseLayer, "FoC Data\\MegaFiles.xml not found");
        }
        static const char* const kPatches[][2] = {
            {"DATA\\PATCH.MEG", "FoC Patch"}, {"DATA\\PATCH2.MEG", "FoC patch2"}, {"DATA\\64PATCH.MEG", "FoC 64Patch"}};
        for (const auto& p : kPatches) {
            if (already_mounted(mounted_foc, p[0])) continue;
            int owner = -1;
            if (const std::string* h = find_loose_for_mount(p[0], &owner)) mount(*h, p[1], owner);
        }
    }
};

Vfs::Vfs(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Vfs::~Vfs() = default;

std::unique_ptr<Vfs> Vfs::create(const VfsConfig& cfg, std::string* err, alamo::IssueSink* issues) {
    auto impl = std::make_unique<Impl>();
    impl->issues = issues;
    impl->foc_root = to_host(cfg.foc_root);
    if (cfg.foc_root.empty() || !is_directory_lp(resolve_ci(impl->foc_root))) {
        if (err) *err = "FoC root is not a directory: '" + cfg.foc_root + "'";
        return nullptr;
    }

    // EaW install.
    if (!cfg.eaw_root) {
        const fs::path parent = resolve_ci(join_host(impl->foc_root, ".."));
        const fs::path gd = find_child_ci(parent, "GameData");
        if (is_directory_lp(gd) && !find_child_ci(gd, "sweaw.exe").empty()) {
            impl->eaw_root = path_to_utf8(gd.lexically_normal());
            log::infof(kLogChannel, "Setting FALLBACK_PATH to %s", impl->eaw_root.c_str());
        } else {
            impl->finding(IssueKind::MissingFile, IssueSeverity::Info, "GameData\\sweaw.exe", {}, alamo::kBaseLayer,
                          "EaW install not found (expected <FoC>\\..\\GameData\\sweaw.exe); EaW MEGs and GameData "
                       "fallback disabled");
        }
    } else if (!cfg.eaw_root->empty()) {
        impl->eaw_root = to_host(*cfg.eaw_root);
        if (!is_directory_lp(resolve_ci(impl->eaw_root)))
            impl->finding(IssueKind::MissingFile, IssueSeverity::Warning, *cfg.eaw_root, {}, alamo::kBaseLayer,
                          "EaW root is not a directory: '" + *cfg.eaw_root + "'");
    }

    // One ordered mod search list (Q14): mod_search in order (STEAMMOD ids resolved through the
    // caller's resolver; unresolved ids add nothing, like the original).
    SteamModResolver steam_resolver;  // built on the first STEAMMOD= entry only (no disk / registry reads otherwise)
    bool steam_resolver_ready = false;
    for (const ModSearchEntry& e : cfg.mod_search) {
        if (e.kind == ModSearchEntry::Kind::Path) {
            if (e.value.empty()) continue;
            impl->mod_dirs.push_back(e.value);
            impl->mod_labels.emplace_back("MODPATH");
            continue;
        }
        // The caller's resolver, else the process-wide Steam library lookup if an app installed it (steam_workshop.hpp).
        if (!steam_resolver_ready) {
            steam_resolver = cfg.steam_resolver ? cfg.steam_resolver : default_steam_resolver(cfg.foc_root);
            steam_resolver_ready = true;
        }
        std::optional<std::string> dir;
        if (steam_resolver) dir = steam_resolver(e.value);
        if (!dir || dir->empty()) {
            impl->finding(IssueKind::MissingFile, IssueSeverity::Warning, "STEAMMOD=" + e.value, {}, alamo::kNoLayer,
                          "STEAMMOD=" + e.value + (steam_resolver ? ": workshop item not found; nothing mounted"
                                                               : ": no workshop resolver; nothing mounted"));
            continue;
        }
        impl->mod_dirs.push_back(std::move(*dir));
        impl->mod_labels.push_back("STEAMMOD=" + e.value);
    }
    impl->mods.resize(impl->mod_dirs.size());
    for (size_t i = 0; i < impl->mod_dirs.size(); ++i) {
        impl->mods[i].build(impl->mod_dirs[i]);
        if (impl->mods[i].data_dir.empty())
            impl->finding(IssueKind::MissingFile, IssueSeverity::Warning, impl->mod_dirs[i], {}, issue_layer_for(static_cast<int>(i)),
                          "mod path has no Data folder: '" + impl->mod_dirs[i] + "'");
    }
    impl->base.build(impl->foc_root);
    impl->user_fallbacks = cfg.fallback_paths.size();
    impl->fallbacks.resize(cfg.fallback_paths.size() + (impl->eaw_root.empty() ? 0u : 1u));
    for (size_t i = 0; i < cfg.fallback_paths.size(); ++i) {
        impl->fallbacks[i].build(cfg.fallback_paths[i]);
        if (impl->fallbacks[i].data_dir.empty())
            impl->finding(IssueKind::MissingFile, IssueSeverity::Warning, cfg.fallback_paths[i], {}, alamo::kBaseLayer,
                          "fallback path has no Data folder: '" + cfg.fallback_paths[i] + "'");
    }
    if (!impl->eaw_root.empty()) impl->fallbacks.back().build(impl->eaw_root);

    if (cfg.mount_megs) {
        if (!impl->eaw_root.empty()) impl->mount_eaw(impl->fallbacks.back());
        impl->mount_foc();
    }
    impl->build_layers();
    impl->issues = nullptr;  // written during create() only
    return std::unique_ptr<Vfs>(new Vfs(std::move(impl)));
}

std::optional<Vfs::Resolution> Vfs::resolve(std::string_view path) const {
    Hit h;
    if (!impl_->find(normalize_path(path), h)) return std::nullopt;
    return Resolution{h.layer, impl_->location_of(h), h.index};
}

std::vector<Vfs::Resolution> Vfs::resolve_all(std::string_view path) const {
    std::vector<Resolution> out;
    for (const Hit& h : impl_->find_all(normalize_path(path))) out.push_back(Resolution{h.layer, impl_->location_of(h), h.index});
    return out;
}

const std::vector<Layer>& Vfs::layers() const { return impl_->layers; }

const Layer* Vfs::layer_of(const Resolution& r) const {
    const size_t slot = impl_->layer_slot(r.layer, r.layer_index);
    return slot < impl_->layers.size() ? &impl_->layers[slot] : nullptr;
}

uint16_t Layer::issue_layer() const { return issue_layer_for(mod); }

std::vector<std::string> Vfs::layer_files(const Layer& layer) const {
    std::vector<std::string> out;
    const size_t slot = impl_->layer_slot(static_cast<Resolution::Layer>(layer.kind), layer.index);
    if (slot >= impl_->layers.size() || impl_->layers[slot].host_root != layer.host_root) return out;  // not one of ours
    const size_t i = static_cast<size_t>(layer.index);
    const auto loose = [&out](const LooseRoot& lr) {
        out.reserve(lr.index.size());
        for (const auto& kv : lr.index) out.push_back(kv.first);
        std::sort(out.begin(), out.end());  // the index is unordered
    };
    switch (layer.kind) {
        case Layer::Kind::Mod: loose(impl_->mods[i]); break;
        case Layer::Kind::Base: loose(impl_->base); break;
        case Layer::Kind::Fallback: loose(impl_->fallbacks[i]); break;
        case Layer::Kind::Meg: {
            const meg::Archive& a = *impl_->archives[i];
            for (const meg::Entry& e : a.entries())
                if (a.lookup(e.name)) out.push_back(normalize_path(e.name));
            break;
        }
    }
    return out;
}

bool Vfs::read(std::string_view path, std::vector<uint8_t>& out) const {
    out.clear();
    Hit h;
    if (!impl_->find(normalize_path(path), h)) return false;
    const bool ok = h.archive ? h.archive->read(*h.entry, out) : read_host_file(*h.host, out);
    if (!ok) out.clear();
    return ok;
}

bool Vfs::read_nth(std::string_view path, size_t n, std::vector<uint8_t>& out, Resolution* where) const {
    out.clear();
    Hit h;
    if (!impl_->find_nth(normalize_path(path), n, h)) return false;
    const bool ok = h.archive ? h.archive->read(*h.entry, out) : read_host_file(*h.host, out);
    if (!ok) {
        out.clear();
        return false;
    }
    if (where) {
        where->layer = h.layer;
        where->layer_index = h.index;
        where->location = impl_->location_of(h);
    }
    return true;
}

bool Vfs::exists(std::string_view path) const {
    Hit h;
    return impl_->find(normalize_path(path), h);
}

std::string Vfs::origin(std::string_view path) const {
    const auto r = resolve(path);
    return r ? r->location : std::string();
}

std::vector<std::string> Vfs::list(std::string_view glob) const { return list(glob, kLayerAll); }

std::vector<std::string> Vfs::list(std::string_view glob, uint32_t layers) const {
    // mcx3 t13 (MCX4_GAL2_RE_ANSWERS Q6, UI.md §9.2) [H]: the order of the MEG-aware find 0x217e80 / 0x218e40 - loose
    // files first, then every mounted MEG in mount order (first mounted first), each archive's records in stored order,
    // names through the record's name index, normalised; a path already returned is skipped (a per-search set), and
    // nothing is filtered through the CRC lookup. A loose tree is listed in upper-case byte order, NTFS's directory
    // order for ASCII names [M]. Callers that load files in listing order (AI plans, equations, the skirmish map table)
    // therefore see retail's order.
    const std::string pat = normalize_path(glob);
    std::set<std::string> seen;
    std::vector<std::string> out;
    const auto take = [&](std::string n) {
        if (seen.insert(n).second) out.push_back(std::move(n));
    };
    const auto add_loose = [&](const LooseRoot& lr) {
        std::vector<std::string> hits;
        for (const auto& kv : lr.index)
            if (glob_rec(pat, kv.first)) hits.push_back(kv.first);
        std::sort(hits.begin(), hits.end());  // the index is unordered
        for (std::string& n : hits) take(std::move(n));
    };
    if (layers & kLayerMod)
        for (const auto& m : impl_->mods) add_loose(m);
    if (layers & kLayerBase) add_loose(impl_->base);
    if (layers & kLayerMeg) {
        for (const auto& a : impl_->archives) {
            for (const auto& e : a->entries()) {
                std::string n = normalize_path(e.name);
                if (glob_rec(pat, n)) take(std::move(n));
            }
        }
    }
    if (layers & kLayerFallback)
        for (const auto& f : impl_->fallbacks) add_loose(f);
    return out;
}

std::string Vfs::describe() const {
    const Impl& m = *impl_;
    std::string s;
    const auto loose = [&s](const char* label, size_t i, const LooseRoot& lr) {
        s += "  ";
        s += label;
        s += "[" + std::to_string(i) + "] " + lr.root + "  (" +
             (lr.data_dir.empty() ? std::string("no Data folder") : std::to_string(lr.index.size()) + " files") + ")\n";
    };
    s += "VFS resolution order (first match wins):\n";
    s += "1. mod dirs (Data\\ paths only):\n";
    for (size_t i = 0; i < m.mods.size(); ++i) {
        loose("mod", i, m.mods[i]);
        s.pop_back();
        s += "  [" + m.mod_labels[i] + "]\n";
    }
    s += "2. base loose:\n";
    loose("base", 0, m.base);
    s += "3. MEGs, newest-mounted first:\n";
    for (size_t i = m.mounts.size(); i-- > 0;) {
        s += "  meg[" + std::to_string(i) + "] " + m.mounts[i].path + "  (" + m.mounts[i].reason + ", " +
             std::to_string(m.archives[i]->entries().size()) + " entries)\n";
    }
    s += "4. fallback dirs (Data\\ paths only):\n";
    for (size_t i = 0; i < m.fallbacks.size(); ++i) loose(i < m.user_fallbacks ? "FALLBACKPATH" : "GameData", i, m.fallbacks[i]);
    s += "EaW root: " + (m.eaw_root.empty() ? std::string("(none)") : m.eaw_root) + "\n";
    for (const auto& w : m.warnings) s += "warning: " + w + "\n";
    return s;
}

const std::vector<Vfs::Mount>& Vfs::mounts() const { return impl_->mounts; }
const std::vector<std::string>& Vfs::mod_dirs() const { return impl_->mod_dirs; }
const std::vector<std::string>& Vfs::warnings() const { return impl_->warnings; }
const std::vector<Vfs::MegIssue>& Vfs::meg_issues() const { return impl_->meg_issues; }
const char* Vfs::MegIssue::kind_name(Kind k) {
    switch (k) {
        case Kind::Unsorted: return "meg_unsorted";
        case Kind::Duplicate: return "meg_duplicate";
        case Kind::NameMismatch: return "meg_name_mismatch";
        case Kind::Index: return "meg_index";
    }
    return "meg_unknown";
}
const std::string& Vfs::eaw_root() const { return impl_->eaw_root; }
const std::string& Vfs::foc_root() const { return impl_->foc_root; }

std::unique_ptr<Vfs> mount_game(std::string_view foc_root, std::span<const std::string> game_args, std::string* err,
                                std::optional<std::string> eaw_root) {
    VfsConfig cfg;
    cfg.foc_root = std::string(foc_root);
    cfg.eaw_root = std::move(eaw_root);
    parse_command_line(game_args, cfg);
    std::string e;
    std::unique_ptr<Vfs> v = Vfs::create(cfg, &e);
    if (!v && err) *err = "VFS: " + e;
    return v;
}

}  // namespace alamo::vfs
