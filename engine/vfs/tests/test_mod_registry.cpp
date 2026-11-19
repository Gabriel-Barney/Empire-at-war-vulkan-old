// alamo_vfs_registry_tests (mcx3 t01-t03): the modinfo reader, mod folder descriptors, the registry scan and stack
// resolution (mod_registry.hpp) on a synthetic install in its own temp folder. The modinfo shapes come from installed
// workshop mods: variant files with one dependency each (subA / subB), a file with a missing comma before "tags", a
// layout string first in "dependencies", a submod without any modinfo whose parent only the data shows.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "alamo/core/host_fs.hpp"
#include "alamo/vfs/host_path.hpp"
#include "alamo/vfs/install_locations.hpp"
#include "alamo/meg/meg.h"
#include "alamo/vfs/mod_registry.hpp"
#include "alamo/vfs/vfs.hpp"
#include "alamo/test/support.hpp"

namespace fs = std::filesystem;
namespace av = alamo::vfs;

using ::alamo::test::g_fail;

namespace {

using ::alamo::test::write_file;

std::string u8(const fs::path& p) { return alamo::path_to_utf8(p); }

bool has(const std::vector<std::string>& v, const std::string& needle) {
    for (const std::string& s : v)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : " | ") + x;
    return s;
}

void test_parse() {
    alamo::test::section("parse_modinfo");
    std::vector<std::string> w;
    const auto m = av::parse_modinfo(R"({
        // a comment
        "name": "Sub é A", "version": "1.2", /* block */
        "languages": [{"code": "EN", "support": 7}, {"code": "de", "support": 1},],
        "dependencies": ["ResolveLastItem", {"modtype": 1, "identifier": "1125571106", "name": "TR"}, {"modtype": 0, "identifier": "Mods\\Base"}],
        "steamdata": {"publishedfileid": "42", "tags": ["FOC", "Singleplayer"]},
        "custom": {"anything": [1, 2, {"x": null}]}
    })",
                                     &w);
    CHECK(m.has_value());
    if (!m) return;
    // JSON escapes in a name (the picker's old reader covered these, ums1 t03): \" \\ \uXXXX (a surrogate pair too), a BOM;
    // a nested "name" (steamdata) is not the mod's.
    const auto esc = av::parse_modinfo("\xEF\xBB\xBF{\"steamdata\": {\"name\": \"inner\"}, \"name\": \"Outer \\\"Q\\\" \\u00e9\\\\ \\ud83d\\ude00\"}");
    CHECK(esc && esc->name == "Outer \"Q\" \xC3\xA9\\ \xF0\x9F\x98\x80");
    CHECK(m->name == "Sub \xC3\xA9 A");
    CHECK(m->version == "1.2");
    CHECK(m->has_languages && m->languages.size() == 2);
    CHECK(m->languages.size() == 2 && m->languages[0].code == "en" && m->languages[0].support == 7 && m->languages[1].support == 1);
    CHECK(m->has_dependencies && m->dependencies.size() == 2);
    CHECK(m->layout == av::DependencyLayout::ResolveLastItem);
    CHECK(m->dependencies.size() == 2 && m->dependencies[0].modtype == 1 && m->dependencies[0].identifier == "1125571106" &&
          m->dependencies[1].modtype == 0 && m->dependencies[1].identifier == "Mods\\Base");
    CHECK(m->steam_tags.size() == 2 && m->steam_tags[0] == "FOC");
    CHECK(m->published_id == "42");
    CHECK(w.empty());

    // The installed sub-modinfo.json shape: no comma between "dependencies" and "tags" (accepted with a warning).
    w.clear();
    const auto m2 = av::parse_modinfo("{\"name\": \"Sub\", \"dependencies\": [{\"modtype\": 1, \"identifier\": \"1125571106\"}]\n"
                                      "  \"tags\": [\"FOC\"]}",
                                      &w);
    CHECK(m2.has_value() && m2->dependencies.size() == 1 && m2->steam_tags.size() == 1);
    CHECK(has(w, "missing comma"));

    // No name: a warning; not an object: nullopt; truncated: nullopt.
    w.clear();
    CHECK(av::parse_modinfo("{\"version\": \"1\"}", &w).has_value() && has(w, "no \"name\""));
    const auto num = av::parse_modinfo("{\"name\": \"N\", \"version\": 1.10}");
    CHECK(num.has_value() && num->version == "1.10");  // a number keeps its text
    // ums3: "description" stands in for a missing "summary" (the Command Center's mods page showed either).
    const auto desc = av::parse_modinfo("{\"name\": \"D\", \"description\": \"About D\"}");
    CHECK(desc.has_value() && desc->summary == "About D");
    const auto both = av::parse_modinfo("{\"name\": \"D\", \"summary\": \"S\", \"description\": \"About D\"}");
    CHECK(both.has_value() && both->summary == "S");
    CHECK(!av::parse_modinfo("[1, 2]").has_value());
    CHECK(!av::parse_modinfo("{\"name\": \"x").has_value());
    CHECK(!av::parse_modinfo("").has_value());
    // A UTF-8 BOM and an EAW-only tag list.
    const auto m3 = av::parse_modinfo("\xEF\xBB\xBF{\"name\": \"Old\", \"steamdata\": {\"tags\": [\"EAW\"]}}");
    CHECK(m3.has_value() && m3->steam_tags.size() == 1 && m3->steam_tags[0] == "EAW");
}

struct Fixture {
    fs::path T, game, lib;
    fs::path item(const std::string& id) const { return lib / "steamapps" / "workshop" / "content" / "32470" / id; }
};

void data_file(const fs::path& mod, const std::string& rel, const std::string& body = "x") { write_file(mod / "Data" / rel, body); }

// A total conversion: both object and faction lists.
void tc(const fs::path& mod) {
    data_file(mod, "XML/GameObjectFiles.xml", "<x/>");
    data_file(mod, "XML/FactionFiles.xml", "<x/>");
}

Fixture make_fixture() {
    Fixture f;
    f.T = fs::temp_directory_path() / "alamo_vfs_registry_tests";
    std::error_code ec;
    fs::remove_all(f.T, ec);
    f.game = f.T / "Steam" / "steamapps" / "common" / "Star Wars Empire at War" / "corruption";
    f.lib = f.T / "Steam";
    fs::create_directories(f.game / "Data", ec);
    // 100: a total conversion tagged FOC.
    tc(f.item("100"));
    write_file(f.item("100") / "modinfo.json", R"({"name": "Big TC", "version": "3.0", "steamdata": {"tags": ["FOC"]}})");
    // 200: an HD submod of 100 without modinfo (its parent only the data shows).
    data_file(f.item("200"), "Art/Textures/hd.dds");
    // 300: a base-EaW mod.
    data_file(f.item("300"), "XML/Units.xml");
    write_file(f.item("300") / "modinfo.json", R"({"name": "Old EaW Mod", "steamdata": {"tags": ["EAW"]}})");
    // 400: two variants (main modinfo plus subA / subB), each on a different parent.
    data_file(f.item("400"), "XML/Extra.xml");
    write_file(f.item("400") / "modinfo.json", R"({"name": "Pack", "languages": [{"code": "en", "support": 1}]})");
    write_file(f.item("400") / "subA-modinfo.json", R"({"name": "Pack A", "dependencies": [{"modtype": 1, "identifier": "100"}]})");
    write_file(f.item("400") / "subB-modinfo.json",
               R"({"name": "Pack B", "dependencies": [{"modtype": 1, "identifier": "500"}, {"modtype": 1, "identifier": "999", "name": "Gone"}]})");
    // 500: a second total conversion.
    tc(f.item("500"));
    // 600: variant files only.
    data_file(f.item("600"), "XML/Only.xml");
    write_file(f.item("600") / "x-modinfo.json", R"({"name": "Only X"})");
    // 700: not a mod (no Data); abc: not a workshop id.
    write_file(f.item("700") / "readme.txt", "x");
    data_file(f.item("abc"), "XML/a.xml");
    // Mods\AOW: a compatibility submod of 200, again without modinfo.
    data_file(f.game / "Mods" / "AOW", "XML/AOW.xml");
    // Mods\CycA <-> Mods\CycB.
    data_file(f.game / "Mods" / "CycA", "XML/a.xml");
    write_file(f.game / "Mods" / "CycA" / "modinfo.json", R"({"name": "A", "dependencies": [{"modtype": 0, "identifier": "Mods\\CycB"}]})");
    data_file(f.game / "Mods" / "CycB", "XML/b.xml");
    write_file(f.game / "Mods" / "CycB" / "modinfo.json", R"({"name": "B", "dependencies": [{"modtype": 0, "identifier": "Mods\\CycA"}]})");
    return f;
}

void test_registry(const Fixture& f) {
    alamo::test::section("scan_mod_catalog");
    const std::vector<std::string> libs{u8(f.lib)};
    av::InstallLocations installed;
    installed.foc = u8(f.game);
    installed.steam_libraries = libs;
    const av::ModCatalog r = av::scan_mod_catalog(installed);
    std::vector<std::string> keys;
    for (const auto& m : r.mods) keys.push_back(m.key());
    CHECK(joined(keys) == "100 | 200 | 300 | 400 | 400:subA | 400:subB | 500 | 600:x | AOW | CycA | CycB");
    std::printf("  keys: %s\n", joined(keys).c_str());

    const av::ModDescriptor* big = r.find_key("100");
    CHECK(big && big->name == "Big TC" && big->version == "3.0" && big->game_tag == av::GameTag::Foc && big->has_modinfo);
    CHECK(big && big->content_hash != 0 && big->arg() == "STEAMMOD=100");
    const av::ModDescriptor* hd = r.find_key("200");
    CHECK(hd && hd->name == "200" && !hd->has_modinfo && hd->languages.size() == 1 && hd->languages[0].code == "en");
    CHECK(r.find_key("300") && r.find_key("300")->game_tag == av::GameTag::Eaw);
    const av::ModDescriptor* a = r.find_key("400:subA");
    CHECK(a && a->name == "Pack A" && a->languages.size() == 1 && a->languages[0].support == 1);  // main file's languages
    CHECK(a && a->dependencies.size() == 1 && a->arg() == "STEAMMOD=400");
    CHECK(r.find_key("400") && r.find_key("400")->dependencies.empty());
    std::vector<av::ModDescriptor> scratch;
    CHECK(r.find_arg("STEAMMOD=600", scratch) == r.find_key("600:x"));  // variant files only: the first variant
    CHECK(r.find_arg("MODPATH=Mods\\aow", scratch) == r.find_key("AOW"));
    CHECK(r.find_arg(u8(f.game / "Mods" / "CycA"), scratch) == nullptr);  // not an argument
    CHECK(r.find_arg("MODPATH=" + u8(f.game / "Mods" / "CycA"), scratch) == r.find_key("CycA"));
    CHECK(r.find_arg("STEAMMOD=400:subB", scratch) == r.find_key("400:subB"));
    CHECK(r.find_arg("STEAMMOD=12345", scratch) == nullptr);
    CHECK(r.find_key("AOW") && r.find_key("AOW")->arg() == "MODPATH=Mods\\AOW");
    // A folder outside the registry is described on the spot.
    data_file(f.T / "Elsewhere", "XML/e.xml");
    const av::ModDescriptor* e = r.find_arg("MODPATH=" + u8(f.T / "Elsewhere"), scratch);
    CHECK(e && e->kind == av::ModKind::Path && e->id == "Elsewhere");

    // ums1 t02: the catalog keys (unique across kinds) name the same rows; names from modinfo say so.
    std::vector<std::string> ckeys;
    for (const auto& m : r.mods) ckeys.push_back(m.catalog_key());
    CHECK(joined(ckeys) == "100 | 200 | 300 | 400 | 400:subA | 400:subB | 500 | 600:x | Mods\\AOW | Mods\\CycA | Mods\\CycB");
    CHECK(r.find_key("Mods\\AOW") == r.find_key("AOW") && r.find_key("mods/aow") == r.find_key("AOW"));
    CHECK(r.find_key("400:SUBA") == a && r.find_key("Mods\\Nope") == nullptr);
    CHECK(big && big->named && hd && !hd->named);
    CHECK(r.find_key("600:x") && r.find_key("600:x")->name == "Only X" && r.find_key("600:x")->named);
    // Extra mod folders (a mod, or a folder of mods), after Mods; a folder listed already is skipped.
    {
        data_file(f.T / "Extra" / "One", "XML/one.xml");
        data_file(f.T / "Extra" / "Two", "XML/two.xml");
        write_file(f.T / "Extra" / "NoData" / "readme.txt", "x");
        data_file(f.T / "Single", "XML/s.xml");
        write_file(f.T / "Single" / "modinfo.json", R"({"name": "Single", "steamdata": {"publishedfileid": "424242"}})");
        av::InstallLocations where;
        where.foc = u8(f.game);
        where.steam_libraries = libs;
        where.mod_folders = {u8(f.T / "Extra"), u8(f.T / "Single"), u8(f.game / "Mods" / "AOW")};
        const av::ModCatalog c = av::scan_mod_catalog(where, false);
        CHECK(c.mods.size() == r.mods.size() + 3);
        if (c.mods.size() == r.mods.size() + 3) {
            const av::ModDescriptor& one = c.mods[r.mods.size()];
            CHECK(one.kind == av::ModKind::Path && one.id == "One" && one.catalog_key() == u8(f.T / "Extra" / "One"));
            CHECK(one.arg() == "MODPATH=" + u8(f.T / "Extra" / "One") && one.content_hash == 0);
            CHECK(c.mods.back().id == "Single" && c.mods.back().published_id == "424242");
            CHECK(c.find_key(u8(f.T / "Single")) == &c.mods.back());
        }
    }

    CHECK(av::mod_provides(u8(f.item("100")), "Data\\XML\\GameObjectFiles.xml"));
    CHECK(av::mod_provides(u8(f.item("100")), "DATA\\xml\\factionfiles.XML"));
    CHECK(!av::mod_provides(u8(f.item("200")), "Data\\XML\\GameObjectFiles.xml"));

    // The content hash moves when a file changes size.
    const uint64_t h0 = av::mod_content_hash(u8(f.item("200")));
    data_file(f.item("200"), "Art/Textures/hd.dds", "longer");
    CHECK(av::mod_content_hash(u8(f.item("200"))) != h0);
    CHECK(av::mod_content_hash(u8(f.item("700"))) == 0);
}

void test_resolve(const Fixture& f) {
    alamo::test::section("resolve");
    const std::vector<std::string> libs{u8(f.lib)};
    av::InstallLocations installed;
    installed.foc = u8(f.game);
    installed.steam_libraries = libs;
    const av::ModCatalog r = av::scan_mod_catalog(installed);

    // AOW -> 200 -> 100 from the AOW folder alone, through the data hint (as alamo_mod_check --resolve wires it).
    int asked = 0;
    const av::ParentHint hint = [&](const std::vector<std::string>& stack) -> std::optional<std::string> {
        ++asked;
        if (stack.empty()) return std::nullopt;
        if (stack[0] == "MODPATH=Mods\\AOW") return std::string("STEAMMOD=200");
        if (stack[0] == "STEAMMOD=200") return std::string("STEAMMOD=100");
        return std::nullopt;
    };
    const std::vector<std::string> aow{"MODPATH=Mods\\AOW"};
    const av::ModStack s = av::resolve(r, aow, hint);
    CHECK(joined(s.args) == "MODPATH=Mods\\AOW | STEAMMOD=200 | STEAMMOD=100");
    CHECK(s.errors.empty() && !s.base_eaw);
    CHECK(s.mods.size() == 3 && s.mods[1].reason_text() == "likely parent of Mods\\AOW" && s.mods[2].reason_text() == "likely parent of 200");
    CHECK(asked == 3);  // 100 declares nothing either: asked, no answer
    CHECK(s.fingerprint != 0);
    std::printf("  AOW stack: %s\n", joined(s.args).c_str());
    // Same stack, same fingerprint; without the hint only AOW.
    CHECK(av::resolve(r, aow, hint).fingerprint == s.fingerprint);
    CHECK(joined(av::resolve(r, aow).args) == "MODPATH=Mods\\AOW");

    // Declared dependencies: subA -> 100.
    const std::vector<std::string> suba{"400:subA"};
    const av::ModStack sa = av::resolve(r, suba);
    CHECK(joined(sa.args) == "STEAMMOD=400 | STEAMMOD=100");
    CHECK(sa.mods.size() == 2 && sa.mods[1].reason_text() == "dependency of 400:subA");
    // subB -> 500 and a parent that is not installed.
    const std::vector<std::string> subb{"STEAMMOD=400:subB"};
    const av::ModStack sb = av::resolve(r, subb);
    CHECK(joined(sb.args) == "STEAMMOD=400 | STEAMMOD=500");
    CHECK(has(sb.warnings, "workshop item 999 (Gone) is not installed"));
    CHECK(sb.errors.empty());

    // Picked parent before its submod: the submod still goes first (each mod before what it depends on).
    const std::vector<std::string> rev{"STEAMMOD=100", "400:subA"};
    CHECK(joined(av::resolve(r, rev).args) == "STEAMMOD=400 | STEAMMOD=100");

    // A cycle is an error naming it.
    const std::vector<std::string> cyc{"MODPATH=Mods\\CycA"};
    const av::ModStack sc = av::resolve(r, cyc);
    CHECK(has(sc.errors, "dependency cycle: Mods\\CycA -> Mods\\CycB -> Mods\\CycA"));
    CHECK(sc.mods.size() == 2);
    std::printf("  cycle: %s\n", joined(sc.errors).c_str());

    // A base-EaW mod is refused.
    const std::vector<std::string> eaw{"STEAMMOD=300"};
    const av::ModStack se = av::resolve(r, eaw);
    CHECK(se.base_eaw && has(se.errors, av::kBaseEawRefusal));

    // Two total conversions without a dependency between them: a warning naming the one retail runs.
    const std::vector<std::string> two{"STEAMMOD=100", "STEAMMOD=500"};
    const av::ModStack st = av::resolve(r, two);
    CHECK(st.errors.empty());
    CHECK(has(st.warnings, "two total conversions in one stack: 100 and 500"));
    // A submod of one of them: no warning between the submod and its parent.
    CHECK(!has(sb.warnings, "two total conversions"));

    // Unknown argument.
    const std::vector<std::string> bad{"STEAMMOD=31337"};
    CHECK(has(av::resolve(r, bad).errors, "not a mod this install has: STEAMMOD=31337"));

    // In memory: A (FullResolved, deps [B]) and D (ResolveRecursive, deps [B]); B depends on E. B is expanded once,
    // whichever mod reached it first, so E is in the stack.
    av::ModCatalog m;
    const auto mod = [&](const char* id, std::vector<const char*> deps, av::DependencyLayout layout) {
        av::ModDescriptor d;
        d.kind = av::ModKind::Workshop;
        d.id = d.name = id;
        d.layout = layout;
        for (const char* x : deps) d.dependencies.push_back({1u, x, ""});
        m.mods.push_back(d);
    };
    mod("1", {"2"}, av::DependencyLayout::FullResolved);
    mod("4", {"2"}, av::DependencyLayout::ResolveRecursive);
    mod("2", {"5"}, av::DependencyLayout::ResolveRecursive);
    mod("5", {}, av::DependencyLayout::ResolveRecursive);
    const std::vector<std::string> ad{"STEAMMOD=1", "STEAMMOD=4"};
    const av::ModStack x = av::resolve(m, ad);
    CHECK(joined(x.args) == "STEAMMOD=1 | STEAMMOD=4 | STEAMMOD=2 | STEAMMOD=5");
    std::printf("  expansion: %s\n", joined(x.args).c_str());
    // A parent picked above its submod: reordered, and said.
    const std::vector<std::string> up{"STEAMMOD=2", "STEAMMOD=4"};
    const av::ModStack u = av::resolve(m, up);
    CHECK(joined(u.args) == "STEAMMOD=4 | STEAMMOD=2 | STEAMMOD=5");
    CHECK(has(u.warnings, "2 is picked above its submod 4"));
    CHECK(!has(x.warnings, "is picked above"));
}

// ---- mcx3 t04: MEG tables as the game reads them (meg::Archive::lookup, docs/re/MCX3_MEG_LOOKUP.md) ----

struct Rec {
    uint32_t crc, index, name_index;
};

void put_u16(std::string& s, uint32_t v) {
    s.push_back(static_cast<char>(v & 0xFFu));
    s.push_back(static_cast<char>((v >> 8) & 0xFFu));
}
void put_u32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
}

// A V1 MEG with the record table exactly as given (any order, any CRC, any index field). Record i's data is "R<i>".
std::string meg_bytes(const std::vector<std::string>& names, const std::vector<Rec>& recs) {
    std::string head;
    put_u32(head, static_cast<uint32_t>(names.size()));
    put_u32(head, static_cast<uint32_t>(recs.size()));
    for (const std::string& n : names) {
        put_u16(head, static_cast<uint32_t>(n.size()));
        head += n;
    }
    uint32_t offset = static_cast<uint32_t>(head.size() + 20 * recs.size());
    std::string table, data;
    for (size_t i = 0; i < recs.size(); ++i) {
        const std::string payload = "R" + std::to_string(i);
        put_u32(table, recs[i].crc);
        put_u32(table, recs[i].index);
        put_u32(table, static_cast<uint32_t>(payload.size()));
        put_u32(table, offset);
        put_u32(table, recs[i].name_index);
        offset += static_cast<uint32_t>(payload.size());
        data += payload;
    }
    return head + table + data;
}

std::unique_ptr<alamo::meg::Archive> archive(const std::string& bytes) {
    return alamo::meg::Archive::open(alamo::meg::Source::from_memory(std::vector<uint8_t>(bytes.begin(), bytes.end())));
}

std::string data_of(const alamo::meg::Archive& a, const alamo::meg::Entry* e) {
    std::vector<uint8_t> b;
    if (!e || !a.read(*e, b)) return "<none>";
    return std::string(b.begin(), b.end());
}

// The independent oracle: the C runtime's own bsearch (the retail exe calls the same UCRT routine) over the stored
// CRCs, compared unsigned as the game's comparator does.
int crc_compare(const void* key, const void* rec) {
    const uint32_t k = *static_cast<const uint32_t*>(key), r = *static_cast<const uint32_t*>(rec);
    return k > r ? 1 : (k < r ? -1 : 0);
}
size_t crt_probe(const std::vector<Rec>& recs, uint32_t crc) {
    const void* hit = std::bsearch(&crc, recs.data(), recs.size(), sizeof(Rec), crc_compare);
    return hit ? static_cast<size_t>(static_cast<const Rec*>(hit) - recs.data()) : SIZE_MAX;
}

void test_meg() {
    alamo::test::section("meg lookup");
    using alamo::meg::name_crc;
    std::vector<std::string> names;
    for (int i = 0; i < 9; ++i) names.push_back("DATA\\XML\\F" + std::to_string(i) + ".XML");
    std::vector<Rec> sorted;
    for (uint32_t i = 0; i < names.size(); ++i) sorted.push_back({name_crc(names[i]), 0, i});
    std::sort(sorted.begin(), sorted.end(), [](const Rec& a, const Rec& b) { return a.crc < b.crc; });
    for (uint32_t i = 0; i < sorted.size(); ++i) sorted[i].index = i;

    // Well formed: lookup is find, no issue.
    const auto good = archive(meg_bytes(names, sorted));
    CHECK(good && !good->table_issues().any());
    if (good)
        for (const std::string& n : names) CHECK(good->lookup(n) == good->find(n) && good->lookup(n) != nullptr);
    if (good) CHECK(good->lookup("data/xml/f3.xml") == good->find("DATA\\XML\\F3.XML"));  // case and '/' as the game

    // Unsorted (reversed): every CRC probed as the CRT's bsearch probes; the records it cannot reach are lost.
    std::vector<Rec> rev(sorted.rbegin(), sorted.rend());
    for (uint32_t i = 0; i < rev.size(); ++i) rev[i].index = i;
    const auto un = archive(meg_bytes(names, rev));
    CHECK(un != nullptr);
    if (un) {
        uint32_t lost = 0;
        for (size_t i = 0; i < rev.size(); ++i) {
            const size_t want = crt_probe(rev, rev[i].crc);
            CHECK(un->probe(rev[i].crc) == want);
            CHECK(data_of(*un, un->lookup(names[rev[i].name_index])) == (want == SIZE_MAX ? "<none>" : "R" + std::to_string(want)));
            lost += want == SIZE_MAX ? 1u : 0u;
        }
        const alamo::meg::Archive::TableIssues t = un->table_issues();
        CHECK(t.unsorted && t.unreachable == lost && lost > 0);
        std::printf("  reversed table of %zu: %u lost\n", rev.size(), lost);
        CHECK(!un->verify().empty());
    }

    // Duplicate CRCs: three records with F4's CRC; the probe order picks the one bsearch stops at.
    std::vector<Rec> dup = sorted;
    const uint32_t c4 = name_crc(names[4]);
    size_t at4 = 0;
    for (size_t i = 0; i < dup.size(); ++i)
        if (dup[i].crc == c4) at4 = i;
    dup.insert(dup.begin() + static_cast<std::ptrdiff_t>(at4), 2, dup[at4]);
    for (uint32_t i = 0; i < dup.size(); ++i) dup[i].index = i;
    const auto du = archive(meg_bytes(names, dup));
    CHECK(du != nullptr);
    if (du) {
        CHECK(data_of(*du, du->lookup(names[4])) == "R" + std::to_string(crt_probe(dup, c4)));
        CHECK(du->table_issues().duplicate == 2 && !du->table_issues().unsorted);
    }

    // A stored CRC that is not its name's: found by CRC only (the name is never compared).
    std::vector<Rec> mis = sorted;
    std::vector<std::string> mis_names = names;
    for (Rec& r : mis)
        if (r.name_index == 2) mis_names[2] = "DATA\\XML\\RENAMED.XML";  // keeps F2's CRC
    const auto mi = archive(meg_bytes(mis_names, mis));
    CHECK(mi != nullptr);
    if (mi) {
        CHECK(mi->lookup(names[2]) != nullptr && mi->lookup(names[2])->name == "DATA\\XML\\RENAMED.XML");
        CHECK(mi->lookup("DATA\\XML\\RENAMED.XML") == nullptr);
        CHECK(mi->find(names[2]) == nullptr);  // the tools' exact-name find
        CHECK(mi->table_issues().name_mismatch == 1);
    }

    // An index field that is not the record's position: the data is the record it names; out of range: not found.
    std::vector<Rec> red = sorted;
    red[0].index = 1;
    red[2].index = 99;
    const auto re = archive(meg_bytes(names, red));
    CHECK(re != nullptr);
    if (re) {
        CHECK(data_of(*re, re->lookup_crc(red[0].crc)) == "R1");
        CHECK(re->lookup_crc(red[2].crc) == nullptr);
        CHECK(re->table_issues().index_redirect == 2);
    }

    // Sweep: table sizes 1..40, shuffled stored orders with repeated CRCs (a fixed LCG), every stored CRC and some
    // absent ones probed the way the CRT probes.
    {
        uint32_t seed = 12345u;
        const auto next = [&seed] {
            seed = seed * 1664525u + 1013904223u;
            return seed;
        };
        size_t mismatches = 0, probes = 0;
        for (uint32_t n = 1; n <= 40; ++n)
            for (int round = 0; round < 4; ++round) {
                std::vector<std::string> nn;
                std::vector<Rec> rr;
                for (uint32_t i = 0; i < n; ++i) {
                    nn.push_back("N" + std::to_string(i));
                    rr.push_back({(next() >> 8) % (2 * n + 1), i, i});  // small range: repeats and gaps
                }
                if (round % 2 == 0)
                    std::sort(rr.begin(), rr.end(), [](const Rec& a, const Rec& b) { return a.crc < b.crc; });
                for (uint32_t i = 0; i < n; ++i) rr[i].index = i;
                const auto ar = archive(meg_bytes(nn, rr));
                if (!ar) {
                    ++mismatches;
                    continue;
                }
                for (uint32_t key = 0; key <= 2 * n + 1; ++key) {
                    ++probes;
                    mismatches += ar->probe(key) == crt_probe(rr, key) ? 0u : 1u;
                }
            }
        CHECK(mismatches == 0);
        std::printf("  probe sweep: %zu probes, %zu differ from the CRT's bsearch\n", probes, mismatches);
    }

    // Mounted: the VFS reads through the same rule and records each finding (warnings and meg_issues).
    alamo::test::section("meg mount (conformance meg_unsorted)");
    const fs::path T = fs::temp_directory_path() / "alamo_vfs_registry_tests_meg";
    std::error_code ec;
    fs::remove_all(T, ec);
    write_file(T / "Data" / "MegaFiles.xml", "<Mega_Files><File>Data\\Unsorted.meg</File></Mega_Files>");
    write_file(T / "Data" / "Unsorted.meg", meg_bytes(names, rev));
    alamo::vfs::VfsConfig cfg;
    cfg.foc_root = u8(T);
    cfg.eaw_root = std::string();
    const auto v = alamo::vfs::Vfs::create(cfg);
    CHECK(v != nullptr);
    if (v) {
        size_t readable = 0;
        std::vector<uint8_t> b;
        for (const std::string& n : names) readable += v->read(n, b) ? 1u : 0u;
        CHECK(v->meg_issues().size() == 1 && v->meg_issues()[0].kind == alamo::vfs::Vfs::MegIssue::Kind::Unsorted);
        CHECK(!v->meg_issues().empty() && readable + v->meg_issues()[0].count == names.size());
        CHECK(has(v->warnings(), "meg_unsorted: MEG '"));
        std::printf("  mounted: %zu of %zu readable; %s\n", readable, names.size(), v->warnings().empty() ? "" : v->warnings().back().c_str());
    }
    fs::remove_all(T, ec);
}

}  // namespace

// ums4 t06: a mod folder reached through a junction is one folder (vfs::folder_key follows it): one catalog row, and
// find_root finds the row by either spelling. Skipped when mklink /J cannot make the junction.
void test_junction(const Fixture& f) {
    alamo::test::section("folder identity through a junction");
    const fs::path target = f.game / "Mods" / "AOW";
    const fs::path link = f.T / "AowLink";
    const std::string cmd = "cmd /c mklink /J \"" + link.string() + "\" \"" + target.string() + "\" >nul";
    if (std::system(cmd.c_str()) != 0 || !fs::is_directory(link)) {
        std::printf("  SKIP: junction (mklink /J failed)\n");
        return;
    }
    CHECK(av::same_folder(u8(link), u8(target)));
    CHECK(av::folder_key(u8(link)) == av::folder_key(u8(f.game / "mods" / "aow" / ".")));
    CHECK(!av::same_folder(u8(link), u8(f.game / "Mods" / "CycA")));
    CHECK(av::path_key("Mods/AOW/") == "mods\\aow");
    av::InstallLocations where;
    where.foc = u8(f.game);
    where.steam_libraries = {u8(f.lib)};
    where.mod_folders = {u8(link)};  // the user names the junction as an extra mod folder
    const av::ModCatalog c = av::scan_mod_catalog(where, false);
    size_t rows = 0;
    for (const av::ModDescriptor& m : c.mods) rows += av::same_folder(m.root, u8(target)) ? 1u : 0u;
    CHECK(rows == 1);  // listed once, as Mods\AOW
    const av::ModDescriptor* by_link = c.find_root(u8(link));
    CHECK(by_link && by_link->catalog_key() == "Mods\\AOW");
    std::error_code ec;
    fs::remove(link, ec);  // removes the junction itself, never what it points at
}

int main() {
    test_parse();
    test_meg();
    const Fixture f = make_fixture();
    test_registry(f);
    test_resolve(f);
    test_junction(f);
    std::error_code ec;
    fs::remove_all(f.T, ec);
    return alamo::test::report("alamo_vfs_registry_tests");
}
