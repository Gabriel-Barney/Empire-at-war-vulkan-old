// alamo_vfs_registry_tests (mcx3 t01-t03): the modinfo reader, mod folder descriptors, the registry scan and stack
// resolution (mod_registry.hpp) on a synthetic install in its own temp folder. The modinfo shapes come from installed
// workshop mods: variant files with one dependency each (subA / subB), a file with a missing comma before "tags", a
// layout string first in "dependencies", a submod without any modinfo whose parent only the data shows.
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "alamo/core/host_fs.hpp"
#include "alamo/vfs/mod_registry.hpp"
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
    alamo::test::section("scan_installed_mods");
    const std::vector<std::string> libs{u8(f.lib)};
    const av::ModRegistry r = av::scan_installed_mods(u8(f.game), libs);
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
    alamo::test::section("resolve_stack");
    const std::vector<std::string> libs{u8(f.lib)};
    const av::ModRegistry r = av::scan_installed_mods(u8(f.game), libs);

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
    const av::ResolvedStack s = av::resolve_stack(r, aow, hint);
    CHECK(joined(s.args) == "MODPATH=Mods\\AOW | STEAMMOD=200 | STEAMMOD=100");
    CHECK(s.errors.empty() && !s.base_eaw);
    CHECK(s.mods.size() == 3 && s.mods[1].reason == "likely parent of AOW" && s.mods[2].reason == "likely parent of 200");
    CHECK(asked == 3);  // 100 declares nothing either: asked, no answer
    CHECK(s.fingerprint != 0);
    std::printf("  AOW stack: %s\n", joined(s.args).c_str());
    // Same stack, same fingerprint; without the hint only AOW.
    CHECK(av::resolve_stack(r, aow, hint).fingerprint == s.fingerprint);
    CHECK(joined(av::resolve_stack(r, aow).args) == "MODPATH=Mods\\AOW");

    // Declared dependencies: subA -> 100.
    const std::vector<std::string> suba{"400:subA"};
    const av::ResolvedStack sa = av::resolve_stack(r, suba);
    CHECK(joined(sa.args) == "STEAMMOD=400 | STEAMMOD=100");
    CHECK(sa.mods.size() == 2 && sa.mods[1].reason == "dependency of 400:subA");
    // subB -> 500 and a parent that is not installed.
    const std::vector<std::string> subb{"STEAMMOD=400:subB"};
    const av::ResolvedStack sb = av::resolve_stack(r, subb);
    CHECK(joined(sb.args) == "STEAMMOD=400 | STEAMMOD=500");
    CHECK(has(sb.warnings, "workshop item 999 (Gone) is not installed"));
    CHECK(sb.errors.empty());

    // Picked parent before its submod: the submod still goes first (each mod before what it depends on).
    const std::vector<std::string> rev{"STEAMMOD=100", "400:subA"};
    CHECK(joined(av::resolve_stack(r, rev).args) == "STEAMMOD=400 | STEAMMOD=100");

    // A cycle is an error naming it.
    const std::vector<std::string> cyc{"MODPATH=Mods\\CycA"};
    const av::ResolvedStack sc = av::resolve_stack(r, cyc);
    CHECK(has(sc.errors, "dependency cycle: CycA -> CycB -> CycA"));
    CHECK(sc.mods.size() == 2);
    std::printf("  cycle: %s\n", joined(sc.errors).c_str());

    // A base-EaW mod is refused.
    const std::vector<std::string> eaw{"STEAMMOD=300"};
    const av::ResolvedStack se = av::resolve_stack(r, eaw);
    CHECK(se.base_eaw && has(se.errors, av::kBaseEawRefusal));

    // Two total conversions without a dependency between them: a warning naming the one retail runs.
    const std::vector<std::string> two{"STEAMMOD=100", "STEAMMOD=500"};
    const av::ResolvedStack st = av::resolve_stack(r, two);
    CHECK(st.errors.empty());
    CHECK(has(st.warnings, "two total conversions in one stack: 100 and 500"));
    // A submod of one of them: no warning between the submod and its parent.
    CHECK(!has(sb.warnings, "two total conversions"));

    // Unknown argument.
    const std::vector<std::string> bad{"STEAMMOD=31337"};
    CHECK(has(av::resolve_stack(r, bad).errors, "not a mod this install has: STEAMMOD=31337"));
}

}  // namespace

int main() {
    test_parse();
    const Fixture f = make_fixture();
    test_registry(f);
    test_resolve(f);
    std::error_code ec;
    fs::remove_all(f.T, ec);
    return alamo::test::report("alamo_vfs_registry_tests");
}
