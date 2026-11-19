// Mod conflicts (mod_conflicts.hpp). format_conflicts_json lives in mod_check.cpp beside the other report writers.
#include "alamo/game/mod_conflicts.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <system_error>
#include <utility>

#include "alamo/core/file_provider.hpp"
#include "alamo/core/host_fs.hpp"
#include "alamo/data/xml.hpp"
#include "alamo/data/xml_list.hpp"
#include "alamo/meg/meg.h"
#include "alamo/game/mod_picker.hpp"  // is_mod_arg
#include "alamo/vfs/mod_registry.hpp"
#include "alamo/vfs/steam_workshop.hpp"
#include "alamo/vfs/vfs.hpp"

namespace alamo::game {
namespace fs = std::filesystem;

namespace {

// The list files the game opens from one layer only, with the words the findings use.
constexpr std::pair<const char*, const char*> kLists[] = {
    {"GameObjectFiles.xml", "object list"},
    {"HardPointDataFiles.xml", "hardpoint list"},
    {"TradeRouteFiles.xml", "trade route list"},
    {"CampaignFiles.xml", "campaign list"},
};

// Every loose file of <root>\Data as a normalised game path ("DATA\\XML\\X.XML").
void loose_files(const std::string& root, std::set<std::string>& out) {
    const fs::path data = path_from_utf8(root) / "Data";
    std::error_code ec;
    if (!fs::is_directory(long_path(data), ec)) return;
    for (fs::recursive_directory_iterator it(long_path(data), fs::directory_options::follow_directory_symlink, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it.depth() > 48) {  // a link loop
            it.disable_recursion_pending();
            continue;
        }
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        const fs::path rel = it->path().lexically_relative(long_path(data));
        out.insert(normalize_path("Data\\" + path_to_utf8(rel)));
    }
}

struct Layered {
    vfs::ModDescriptor mod;
    std::set<std::string> files;  // normalised paths
    std::vector<std::string> megs;
};

std::string display(const Layered& l) { return l.mod.name.empty() ? l.mod.id : l.mod.name; }

// The bytes of mod `m`'s own copy of `path`: the copy (resolve_all order) whose layer that mod owns.
bool own_copy(const vfs::Vfs& v, size_t m, const std::string& path, std::vector<uint8_t>& out) {
    const std::vector<vfs::Vfs::Resolution> all = v.resolve_all(path);
    for (size_t n = 0; n < all.size(); ++n) {
        const vfs::Layer* l = v.layer_of(all[n]);
        if (l && l->from_mod() && static_cast<size_t>(l->mod) == m) return v.read_nth(path, n, out);
    }
    return false;
}

std::vector<std::string> list_entries(const std::vector<uint8_t>& bytes, const std::string& name) {
    std::vector<std::string> out;
    const data::XmlDocument doc = data::XmlDocument::parse(std::span<const uint8_t>(bytes), name);
    for (const data::ListedXmlEntry& e : data::listed_xml_entries(doc)) out.push_back(e.name);
    return out;
}

}  // namespace

ConflictReport mod_conflicts(const vfs::ModCatalog& catalog, std::span<const std::string> args,
                             std::span<const std::string> workshop_libraries) {
    ConflictReport r;
    r.game = catalog.game;
    r.args.assign(args.begin(), args.end());
    vfs::VfsConfig cfg;
    cfg.foc_root = catalog.game;
    vfs::parse_command_line(args, cfg);
    cfg.steam_resolver = vfs::make_steam_workshop_resolver(std::vector<std::string>(workshop_libraries.begin(), workshop_libraries.end()));
    std::string err;
    const std::unique_ptr<vfs::Vfs> v = vfs::Vfs::create(cfg, &err);
    if (!v) {
        r.errors.push_back("the stack does not mount: " + err);
        return r;
    }
    for (const std::string& a : args) {  // FALLBACKPATH= and other switches are not mods
        std::vector<vfs::ModDescriptor> scratch;
        if (is_mod_arg(a) && !catalog.find_arg(a, scratch)) r.errors.push_back("not a mod this install has: " + a);
    }

    // The mods as mounted (the VFS's resolved search list), each with its files.
    std::vector<Layered> mods;
    for (const std::string& dir : v->mod_dirs()) {
        Layered l;
        const vfs::ModDescriptor* d = catalog.find_root(dir);
        l.mod = d ? *d : vfs::describe_mounted_mod(dir, catalog.game, false);
        loose_files(dir, l.files);
        mods.push_back(std::move(l));
    }
    // A mod's MEGs: the MEG layers the VFS says the mod owns (its MegaFiles.xml entries, Patch*.meg); their records the
    // game's lookup reaches count as the mod's files.
    for (const vfs::Layer& layer : v->layers()) {
        if (layer.kind != vfs::Layer::Kind::Meg || !layer.from_mod() || static_cast<size_t>(layer.mod) >= mods.size()) continue;
        Layered& l = mods[static_cast<size_t>(layer.mod)];
        l.megs.push_back(layer.host_root);
        std::string e2;
        const auto a = meg::Archive::open(layer.host_root, meg::Source::Mode::Stream, &e2);
        if (!a) continue;
        for (const meg::Entry& e : a->entries())
            if (a->lookup(e.name)) l.files.insert(normalize_path(e.name));  // the names the game's lookup finds
    }

    // Owner of the layer a resolution came from: a mod index, or SIZE_MAX for the base game / fallbacks.
    const auto owner = [&](const vfs::Vfs::Resolution& res) -> size_t {
        const vfs::Layer* l = v->layer_of(res);
        return l && l->from_mod() ? static_cast<size_t>(l->mod) : SIZE_MAX;
    };

    // Every path a mod has: who wins it. Paths in byte order (std::map), mods in load order.
    std::map<std::string, std::vector<size_t>> providers;
    for (size_t i = 0; i < mods.size(); ++i)
        for (const std::string& p : mods[i].files) providers[p].push_back(i);
    std::map<std::pair<size_t, size_t>, ConflictPair> pairs;
    std::vector<size_t> replaced(mods.size(), 0), base_wins(mods.size(), 0);
    for (const auto& [path, who] : providers) {
        const std::optional<vfs::Vfs::Resolution> res = v->resolve(path);
        const size_t winner = res ? owner(*res) : SIZE_MAX;
        for (const size_t i : who) {
            if (i == winner) continue;
            ++replaced[i];
            if (winner == SIZE_MAX) {
                ++base_wins[i];
                continue;
            }
            ConflictPair& cp = pairs[{winner, i}];
            cp.winner = winner;
            cp.loser = i;
            cp.files.push_back(path);
            if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".XML") == 0) ++cp.xml_files;
        }
    }
    for (size_t i = 0; i < mods.size(); ++i) {
        ConflictMod c;
        c.key = mods[i].mod.catalog_key();
        c.name = display(mods[i]);
        c.root = mods[i].mod.root;
        c.arg = mods[i].mod.arg();
        std::error_code ec;
        c.exists = fs::is_directory(long_path(path_from_utf8(c.root)), ec);
        c.has_data = mods[i].mod.has_data;
        c.files = mods[i].files.size();
        c.megs = mods[i].megs.size();
        c.replaced_files = replaced[i];
        c.base_wins = base_wins[i];
        r.mods.push_back(std::move(c));
    }
    for (auto& [k, p] : pairs) r.pairs.push_back(std::move(p));

    // list_ignored: one copy of each list is read, the winner's.
    for (const auto& [file, words] : kLists) {
        const std::string path = normalize_path(std::string("Data\\XML\\") + file);
        const auto it = providers.find(path);
        if (it == providers.end()) continue;
        const std::optional<vfs::Vfs::Resolution> res = v->resolve(path);
        if (!res) continue;
        const size_t winner = owner(*res);
        std::vector<uint8_t> bytes;
        if (!v->read(path, bytes)) continue;
        const std::vector<std::string> winning = list_entries(bytes, path);
        std::set<std::string> winning_keys;
        for (const std::string& e : winning) winning_keys.insert(normalize_path(e));
        const std::string a = winner == SIZE_MAX ? std::string("the base game") : display(mods[winner]);
        for (const size_t lower : it->second) {
            if (lower == winner || !own_copy(*v, lower, path, bytes)) continue;
            const std::vector<std::string> ignored = list_entries(bytes, path);
            std::set<std::string> ignored_keys, seen;
            ConflictFinding f;
            f.kind = "list_ignored";
            f.mods = {lower};
            if (winner != SIZE_MAX) f.mods.push_back(winner);
            for (const std::string& e : ignored) {
                const std::string k = normalize_path(e);
                ignored_keys.insert(k);
                if (!winning_keys.count(k) && seen.insert(k).second) f.lost.push_back(e);
            }
            const std::string b = display(mods[lower]);
            if (f.lost.empty()) {
                f.warning = false;
                f.text = b + "'s " + words + " is ignored because " + a + " also has " + file + ". " + a +
                         "'s list names every file " + b + "'s does, so nothing is lost.";
            } else {
                std::string examples;
                for (size_t n = 0; n < f.lost.size() && n < 3; ++n) examples += (n ? ", " : "") + f.lost[n];
                if (f.lost.size() > 3) examples += ", ...";
                const std::string count = f.lost.size() == 1 ? std::string("1 XML file") : std::to_string(f.lost.size()) + " XML files";
                f.order = winner != SIZE_MAX &&
                          std::all_of(winning_keys.begin(), winning_keys.end(), [&](const std::string& k) { return ignored_keys.count(k) > 0; });
                f.text = b + "'s " + words + " is ignored because " + a + " also has " + file + ". " + count + " only " + b + " lists (" +
                         examples + ") won't load unless " + a + " lists them." +
                         (f.order ? " " + b + "'s list includes everything in " + a + "'s, so " + b + " looks like it belongs above " + a + "."
                                  : std::string());
            }
            r.findings.push_back(std::move(f));
        }
    }
    // submod_order: a mod below a mod that declares it as its parent.
    for (size_t parent = 0; parent < mods.size(); ++parent)
        for (size_t sub = parent + 1; sub < mods.size(); ++sub)
            for (const vfs::ModDependency& d : mods[sub].mod.dependencies) {
                const vfs::ModDescriptor* p = catalog.find_dependency(d);
                if (!p || p->catalog_key() != mods[parent].mod.catalog_key()) continue;
                ConflictFinding f;
                f.kind = "submod_order";
                f.mods = {sub, parent};
                f.order = true;
                f.text = display(mods[sub]) + " is a submod of " + display(mods[parent]) + " (its modinfo.json says so) and should be above it.";
                r.findings.push_back(std::move(f));
                break;
            }
    for (size_t i = 0; i < mods.size(); ++i) {
        const ConflictMod& c = r.mods[i];
        if (c.files > 0 && c.replaced_files == c.files) {
            ConflictFinding f;
            f.kind = "no_effect";
            f.mods = {i};
            for (const ConflictPair& p : r.pairs)
                if (p.loser == i) f.mods.push_back(p.winner);
            f.order = true;
            f.text = c.name + " has no effect in this order: " +
                     (c.files == 1 ? std::string("its one file is") : "all " + std::to_string(c.files) + " of its files are") +
                     " also in layers the game reads first. Move it up if it is meant to change them.";
            r.findings.push_back(std::move(f));
        }
        if (c.exists && !c.has_data) {
            ConflictFinding f;
            f.kind = "no_data";
            f.mods = {i};
            f.text = c.name + " has no Data folder, so it changes nothing in the game.";
            r.findings.push_back(std::move(f));
        }
    }
    // Warnings before notes; within each, the launcher's ranks (list_ignored, submod_order, no_effect, no_data).
    const auto rank = [](const ConflictFinding& f) {
        const int k = f.kind == "list_ignored" ? 0 : f.kind == "submod_order" ? 1 : f.kind == "no_effect" ? 2 : 3;
        return std::pair<int, int>(f.warning ? 0 : 1, k);
    };
    std::stable_sort(r.findings.begin(), r.findings.end(), [&](const ConflictFinding& x, const ConflictFinding& y) { return rank(x) < rank(y); });
    return r;
}

}  // namespace alamo::game
