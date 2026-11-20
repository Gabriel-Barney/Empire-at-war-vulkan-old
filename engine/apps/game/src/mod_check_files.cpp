// Mod check, files family (mod_check_detail.hpp): the mod layers' files per Data folder, files the data names that no
// layer has (list entries, object type models), the submod hint (modc t16) and the base-EaW heuristic (mcx3 t03).
#include "mod_check_detail.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "alamo/core/ascii.hpp"
#include "alamo/data/xml.hpp"
#include "alamo/data/xml_list.hpp"
#include "alamo/vfs/vfs.hpp"

namespace alamo::game::mod_check_detail {

namespace {

// "DATA\XML\UNITS.XML" -> "XML"; "DATA\MEGAFILES.XML" -> "".
std::string data_folder(std::string_view path) {
    constexpr std::string_view kData = "DATA\\";
    if (path.substr(0, kData.size()) != kData) return {};
    path.remove_prefix(kData.size());
    const size_t sep = path.find('\\');
    return sep == std::string_view::npos ? std::string() : std::string(path.substr(0, sep));
}

}  // namespace

void list_mod_files(const vfs::Vfs& v, ModCheckReport& r) {
    constexpr uint32_t kLower = vfs::Vfs::kLayerBase | vfs::Vfs::kLayerMeg | vfs::Vfs::kLayerFallback;
    const std::vector<std::string> mod = v.list("Data\\**", vfs::Vfs::kLayerMod);
    const std::vector<std::string> lower = v.list("Data\\**", kLower);  // both sorted by byte order
    std::map<std::string, ModFolderStats> folders;
    for (const std::string& p : mod) {
        ModFolderStats& f = folders[data_folder(p)];
        if (std::binary_search(lower.begin(), lower.end(), p)) ++f.overrides;
        else ++f.added;
    }
    for (auto& [name, f] : folders) {
        f.folder = name;
        r.mod_folders.push_back(f);
    }
}

// True when `path` resolves to a file of a mod folder (the VFS mod layer).
bool in_mod_layer(const vfs::Vfs& v, std::string_view path) {
    const auto res = v.resolve(path);
    return res && res->layer == vfs::Vfs::Resolution::Mod;
}

namespace {

using MissingMap = std::map<std::string, MissingFile>;  // by path: sorted, deterministic

void note_missing(MissingMap& m, std::string path, std::string_view by, bool in_mod) {
    MissingFile& f = m[path];
    if (f.references++ == 0) {
        f.path = std::move(path);
        f.referenced_by = std::string(by);
    }
    f.in_mod = f.in_mod || in_mod;
}

// Entries of every Data\XML\*Files.xml list (GameObjectFiles, FactionFiles, CampaignFiles, SFXEventFiles, ...): each
// <File> names Data\XML\<entry> (the convention of every list the loaders read: game_database.cpp read_list,
// object_types.cpp). A list that fails to parse is reported by its loader, not here.
void check_list_files(const GameData& d, MissingMap& m) {
    std::vector<uint8_t> bytes;
    for (const std::string& list : d.vfs->list("Data\\XML\\*Files.xml")) {
        if (!d.files->read(list, bytes)) continue;
        const bool in_mod = in_mod_layer(*d.vfs, list);
        const data::XmlDocument doc = data::XmlDocument::parse(bytes, list);
        for (const data::ListedXmlEntry& en : data::listed_xml_entries(doc)) {  // cu5 t02 (none when !doc.ok())
            std::string path = normalize_name(en.path);
            if (!d.files->exists(path)) note_missing(m, std::move(path), list, in_mod);
        }
    }
}

// The model of each object type (first definitions only: a later duplicate never loads). A name without an extension
// is also tried with .ALO (PROVISIONAL: how the original completes such names is not traced; this avoids a false report).
void check_models(const GameData& d, MissingMap& m) {
    static constexpr std::string_view kTags[] = {"Space_Model_Name", "Land_Model_Name", "Galactic_Model_Name"};
    const objects::ObjectTypeDatabase& db = d.raw;
    const objects::TypeColumns& t = db.types();
    const objects::FieldColumns& f = db.fields();
    std::set<std::string> present;  // paths already found (types share models)
    for (objects::TypeIndex i = 0; i < t.size(); ++i) {
        if (t.first_definition[i] != i) continue;
        for (objects::FieldIndex fi = t.field_begin[i]; fi < t.field_end[i]; ++fi) {
            if (f.depth[fi] != 1 || f.text[fi].empty()) continue;
            const std::string_view tag = db.tag_name(fi);
            const auto* hit = std::find_if(std::begin(kTags), std::end(kTags), [&](std::string_view k) { return iequals(k, tag); });
            if (hit == std::end(kTags)) continue;
            std::string path = normalize_name(std::string("Data\\Art\\Models\\") + std::string(f.text[fi]));
            if (present.count(path)) continue;
            bool found = d.files->exists(path);
            if (!found && path.find('.', path.rfind('\\')) == std::string::npos) found = d.files->exists(path + ".ALO");
            if (found) {
                present.insert(std::move(path));
                continue;
            }
            note_missing(m, std::move(path), std::string(t.name[i]) + " (" + std::string(*hit) + ")", in_mod_layer(*d.vfs, db.file_of(i)));
        }
    }
}

}  // namespace

void check_files(const GameData& d, ModCheckReport& r) {
    if (!d.vfs) return;
    MissingMap m;
    check_list_files(d, m);
    check_models(d, m);
    for (auto& [path, f] : m) r.missing_files.push_back(std::move(f));
}

namespace {

// Data\XML files the mod's own list files name that no layer has (MissingFile::in_mod; models are not counted: a full
// mod misses art too, only a mod whose own database lists another mod's XML files is a submod).
std::vector<std::string> missing_list_entries(const ModCheckReport& r) {
    std::vector<std::string> out;
    for (const MissingFile& f : r.missing_files)
        if (f.in_mod && f.path.starts_with("DATA\\XML\\")) out.push_back(f.path);
    return out;
}

}  // namespace

// The submod hint from this check's own missing-file list (mod_resolve.hpp: the resolver reaches the same answer from
// the files alone). Candidates: the catalog of <game> with the options' Steam libraries.
void check_submod(const GameData& d, ModCheckReport& r, const ModCheckOptions& o) {
    if (d.mods.empty()) return;
    if (o.catalog) {
        find_submod_parents(missing_list_entries(r), d.mods, r.args, *o.catalog, r.submod);
        return;
    }
    find_submod_parents(missing_list_entries(r), d.mods, r.args, stack_catalog(r.game, o.workshop_libraries), r.submod);
}

namespace {

// ---- mcx3 t03: base-EaW heuristic (design 5.4) ----

// The entries of `list`'s copies in the base layers: FoC's (FoC loose or a FoC MEG) and EaW's (an EaW MEG or the EaW
// GameData fallback). A copy counts once per side, the highest one; false when either side has none.
bool base_list_entries(const vfs::Vfs& v, std::string_view list, std::set<std::string>& foc, std::set<std::string>& eaw) {
    bool have_foc = false, have_eaw = false;
    std::vector<uint8_t> bytes;
    vfs::Vfs::Resolution where{};
    for (size_t n = 0; v.read_nth(list, n, bytes, &where); ++n) {
        bool is_foc = where.layer == vfs::Vfs::Resolution::Base;
        bool is_eaw = where.layer == vfs::Vfs::Resolution::Fallback;
        if (where.layer == vfs::Vfs::Resolution::Meg && where.layer_index >= 0 &&
            static_cast<size_t>(where.layer_index) < v.mounts().size()) {
            const std::string& reason = v.mounts()[static_cast<size_t>(where.layer_index)].reason;
            is_foc = reason.rfind("FoC", 0) == 0;
            is_eaw = reason.rfind("EaW", 0) == 0;
        }
        if ((is_foc && have_foc) || (is_eaw && have_eaw) || (!is_foc && !is_eaw)) continue;
        std::set<std::string>& out = is_foc ? foc : eaw;
        const data::XmlDocument doc = data::XmlDocument::parse(bytes, std::string(list));
        for (const data::ListedXmlEntry& en : data::listed_xml_entries(doc)) out.insert(normalize_name(en.path));
        (is_foc ? have_foc : have_eaw) = true;
    }
    return have_foc && have_eaw;
}

}  // namespace

void check_base_eaw_suspect(const GameData& d, ModCheckReport& r) {
    static constexpr std::string_view kList = "Data\\XML\\GameObjectFiles.xml";
    if (!d.vfs || d.mods.empty()) return;
    const std::optional<vfs::Vfs::Resolution> top = d.vfs->resolve(kList);
    if (!top || top->layer != vfs::Vfs::Resolution::Mod || top->layer_index < 0 ||
        static_cast<size_t>(top->layer_index) >= d.mods.size())
        return;  // the list is the base game's: nothing to judge
    std::set<std::string> foc, eaw;
    if (!base_list_entries(*d.vfs, kList, foc, eaw)) return;
    std::vector<std::string> foc_only;
    for (const std::string& e : foc)
        if (!eaw.count(e)) foc_only.push_back(e);
    if (foc_only.empty()) return;
    std::vector<uint8_t> bytes;
    if (!d.vfs->read(kList, bytes)) return;
    const data::XmlDocument doc = data::XmlDocument::parse(bytes, std::string(kList));
    if (!doc.ok()) return;
    for (const data::ListedXmlEntry& en : data::listed_xml_entries(doc))
        if (std::binary_search(foc_only.begin(), foc_only.end(), normalize_name(en.path))) return;
    r.base_eaw_suspect = d.mods[static_cast<size_t>(top->layer_index)].key() +
                         ": its GameObjectFiles.xml names none of the " + std::to_string(foc_only.size()) +
                         " object files FoC adds to EaW's list (first: " + foc_only.front() +
                         "); probably a base-EaW mod, which retail runs with sweaw.exe (a guess: not refused)";
}

}  // namespace alamo::game::mod_check_detail
