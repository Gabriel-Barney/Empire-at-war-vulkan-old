// ums1 t04 / ums4 t05: the stack resolver and the submod hint (mod_resolve.hpp).
#include "alamo/game/mod_resolve.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <string_view>
#include <utility>

#include "alamo/core/compat_issue.hpp"
#include "alamo/core/hash.hpp"  // normalize_name
#include "alamo/core/host_fs.hpp"
#include "alamo/data/xml.hpp"
#include "alamo/data/xml_list.hpp"
#include "alamo/vfs/host_path.hpp"
#include "alamo/vfs/install_locations.hpp"
#include "alamo/vfs/mod_environment.hpp"
#include "alamo/vfs/mod_registry.hpp"
#include "alamo/vfs/vfs.hpp"

namespace alamo::game {

namespace {

// How many of `paths` the candidate's own files have: a Vfs with the candidate as its only tree (its loose Data, the
// MEGs its MegaFiles.xml / Patch*.meg name; no EaW, no FoC), so every hit is the candidate's.
uint32_t provided_by(const std::string& candidate, const std::vector<std::string>& paths) {
    vfs::VfsConfig cfg;
    cfg.foc_root = candidate;
    cfg.eaw_root = std::string();
    std::string err;
    const std::unique_ptr<vfs::Vfs> v = vfs::Vfs::create(cfg, &err);
    if (!v) return 0;
    uint32_t n = 0;
    for (const std::string& p : paths) n += v->exists(p) ? 1u : 0u;
    return n;
}

}  // namespace

// The list files the loaders read (game_database.cpp read_list, object_types.cpp): each <File> names Data\XML\<entry>.
// The same files and rule as the load report's list check (mod_check.cpp check_list_files), restricted to the lists a
// mod's loose layer wins and to their Data\XML entries.
std::vector<std::string> missing_list_entries(const vfs::Vfs& v) {
    std::set<std::string> out;
    std::vector<uint8_t> bytes;
    for (const std::string& list : v.list("Data\\XML\\*Files.xml")) {
        const auto where = v.resolve(list);
        if (!where || where->layer != vfs::Vfs::Resolution::Mod || !v.read(list, bytes)) continue;
        const data::XmlDocument doc = data::XmlDocument::parse(bytes, list);
        for (const data::ListedXmlEntry& en : data::listed_xml_entries(doc)) {  // none when !doc.ok()
            std::string path = normalize_name(en.path);
            if (path.starts_with("DATA\\XML\\") && !v.exists(path)) out.insert(std::move(path));
        }
    }
    return {out.begin(), out.end()};
}

void find_submod_parents(const std::vector<std::string>& missing, std::span<const vfs::ModDescriptor> mounted,
                         const std::vector<std::string>& args, const vfs::ModCatalog& catalog, SubmodHint& h) {
    h.missing = static_cast<uint32_t>(missing.size());
    if (missing.empty()) return;
    std::set<std::string> mentioned, in_stack;
    for (const vfs::ModDescriptor& m : mounted) {
        mentioned.insert(m.modinfo_numbers.begin(), m.modinfo_numbers.end());
        in_stack.insert(vfs::folder_key(m.root));
    }
    std::vector<ModParentCandidate> cands;
    std::set<std::string> seen;  // one candidate per folder (a folder's variants share it)
    for (const vfs::ModDescriptor& d : catalog.mods) {
        if (d.kind != vfs::ModKind::Workshop && d.kind != vfs::ModKind::LocalMods) continue;
        const std::string key = vfs::folder_key(d.root);
        if (!seen.insert(key).second || in_stack.count(key)) continue;  // already in the stack
        ModParentCandidate c;
        c.id = d.id;
        c.workshop = d.kind == vfs::ModKind::Workshop;
        c.path = vfs::native_utf8(path_from_utf8(d.root).make_preferred());
        c.arg = c.workshop ? "STEAMMOD=" + c.id : "MODPATH=Mods\\" + c.id;
        c.mentioned = c.workshop && mentioned.count(c.id) != 0;
        c.provides = provided_by(d.root, missing);
        if (c.provides > 0) cands.push_back(std::move(c));
    }
    std::sort(cands.begin(), cands.end(), [](const ModParentCandidate& a, const ModParentCandidate& b) {
        if (a.provides != b.provides) return a.provides > b.provides;
        if (a.mentioned != b.mentioned) return a.mentioned;
        if (a.workshop != b.workshop) return a.workshop;
        return a.id < b.id;
    });
    constexpr size_t kListed = 5;
    if (cands.size() > kListed) cands.resize(kListed);
    h.parents = std::move(cands);
    if (h.parents.empty()) return;
    const ModParentCandidate& top = h.parents.front();
    h.likely = top.provides >= SubmodHint::kMinProvided && 2 * top.provides >= h.missing;
    if (h.likely) {
        h.stacked_args = args;
        h.stacked_args.push_back(top.arg);
    }
}

SubmodHint submod_hint(const std::string& game, const std::vector<std::string>& args, const vfs::ModCatalog& catalog) {
    SubmodHint h;
    IssueSink issues;  // the VFS's findings: not this hint's business
    std::string err;
    vfs::ModEnvironmentOptions o;
    o.hash = false;
    const std::unique_ptr<vfs::ModEnvironment> env = vfs::ModEnvironment::open(game, args, issues, &err, std::move(o));
    if (!env || env->refused() || env->mounted().empty()) return h;
    find_submod_parents(missing_list_entries(env->vfs()), env->mounted(), args, catalog, h);
    return h;
}

vfs::ModStack resolve_mod_stack(const std::string& game, const std::vector<std::string>& picked,
                                std::span<const std::string> workshop_libraries) {
    vfs::InstallLocations where;  // the FoC folder as given, these libraries, no extra mod folders
    where.foc = game;
    where.steam_libraries.assign(workshop_libraries.begin(), workshop_libraries.end());
    const vfs::ModCatalog catalog = vfs::scan_mod_catalog(where, false);  // the stack's mods get hashed by resolve
    const vfs::ParentHint hint = [&](const std::vector<std::string>& stack_args) -> std::optional<std::string> {
        const SubmodHint h = submod_hint(game, stack_args, catalog);
        if (!h.likely || h.parents.empty()) return std::nullopt;
        return h.parents.front().arg;
    };
    return vfs::resolve(catalog, picked, hint);
}

std::vector<std::string> launch_args_for(const std::string& game, const std::vector<std::string>& picked,
                                         std::span<const std::string> workshop_libraries, vfs::ModStack* stack) {
    vfs::ModStack s = resolve_mod_stack(game, picked, workshop_libraries);
    std::vector<std::string> out = s.launch_args();
    if (stack) *stack = std::move(s);
    return out;
}

std::optional<std::vector<std::string>> play_launch_args(const std::string& game, const std::vector<std::string>& picked,
                                                         vfs::ModStack* stack, const std::vector<std::string>* steam_libraries) {
    const std::vector<std::string> libraries = steam_libraries ? *steam_libraries : vfs::default_install_at(game).steam_libraries;
    vfs::ModStack s;
    std::vector<std::string> launch = launch_args_for(game, picked, libraries, &s);
    const bool refused = s.base_eaw;
    if (stack) *stack = std::move(s);
    if (refused) return std::nullopt;
    return launch;
}

}  // namespace alamo::game
