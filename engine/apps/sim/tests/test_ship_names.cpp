// ref5 t04: ship names (alamo/sim/ship_names.hpp; docs/re/MCX8_BINDINGS_OBJECTS.md §"Ship names"): the list file format,
// shared lists, drawing without replacement with Roman numerals after a wrap, the Start_Game reset, the parent-mode copy,
// SELECTABLE objects only, and the save chunk.
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "alamo/core/bytes.hpp"
#include "alamo/core/file_provider.hpp"
#include "alamo/core/host_fs.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/objects/object_store.hpp"
#include "alamo/objects/object_types.hpp"
#include "alamo/sim/ship_names.hpp"
#include "alamo/test/support.hpp"

using namespace alamo;
using ::alamo::test::g_fail;
using ::alamo::test::write_file;
namespace fs = std::filesystem;

namespace {

// A UTF-16LE list file: BOM, CRLF lines.
std::vector<uint8_t> utf16_file(const std::u16string& text, bool bom = true) {
    std::vector<uint8_t> out;
    if (bom) out = {0xFF, 0xFE};
    for (const char16_t ch : text) {
        out.push_back(static_cast<uint8_t>(ch & 0xFF));
        out.push_back(static_cast<uint8_t>(ch >> 8));
    }
    return out;
}

void file_format() {
    using sim::ShipNames;
    CHECK((ShipNames::parse_list(utf16_file(u"Allecto\r\n\r\nAvenger\r\n Grand Vizier\t\r\nLast")) ==
           std::vector<std::string>{"Allecto", "Avenger", " Grand Vizier\t", "Last"}));
    CHECK((ShipNames::parse_list(utf16_file(u"Xlecto\nB", false)) == std::vector<std::string>{"lecto", "B"}));  // no BOM: first unit lost
    CHECK((ShipNames::parse_list(utf16_file(u"Aé\nB")) == std::vector<std::string>{"A\xc3\xa9", "B"}));
    CHECK(ShipNames::parse_list(utf16_file(u"\r\n\r\n")).empty() && ShipNames::parse_list({}).empty());
    CHECK(ShipNames::roman(1) == "I" && ShipNames::roman(4) == "IV" && ShipNames::roman(19) == "XIX" &&
          ShipNames::roman(1994) == "MCMXCIV" && ShipNames::roman(3999) == "MMMCMXCIX" && ShipNames::roman(0).empty() &&
          ShipNames::roman(4000).empty());
}

struct World {
    fs::path root;
    std::unique_ptr<DirectoryProvider> files;
    objects::ObjectTypeDatabase db;
    objects::GameObjectTypes types;
    World() {
        root = fs::temp_directory_path() / "alamo_sim_ship_names_test";
        std::error_code ec;
        remove_all_long(root, ec);
        write_file(root / "Data/XML/GameObjectFiles.xml", "<GameObjectFiles><File>S.xml</File></GameObjectFiles>\n");
        write_file(root / "Data/XML/S.xml",
                   "<S><SpaceUnit Name=\"Star_Destroyer\"><Behavior>SELECTABLE</Behavior></SpaceUnit>\n"
                   "<SpaceUnit Name=\"Generic_Star_Destroyer\"><Behavior>SELECTABLE</Behavior></SpaceUnit>\n"
                   "<SpaceUnit Name=\"Corvette\"><Behavior>SELECTABLE</Behavior></SpaceUnit>\n"
                   "<SpaceUnit Name=\"Asteroid\"></SpaceUnit>\n</S>\n");
        files = std::make_unique<DirectoryProvider>(path_to_utf8(root));
        CHECK(db.load(*files) && types.build(db));
    }
    ~World() {
        std::error_code ec;
        remove_all_long(root, ec);
    }
    objects::TypeIndex t(const char* n) const { return db.find(n); }
};

std::unique_ptr<sim::ShipNames> make_names(const World& w, std::vector<std::string>* log = nullptr) {
    auto n = std::make_unique<sim::ShipNames>();
    const std::vector<uint8_t> sd = utf16_file(u"Avenger\r\nAllecto\r\n\r\nChimaera\r\n");
    n->load("star_destroyer, Data\\Star_Destroyer.txt\n Generic_Star_Destroyer DATA\\STAR_DESTROYER.TXT\tNone Data\\Other.txt\n"
            "Corvette Data\\Missing.txt Unknown_Type Data\\Star_Destroyer.txt Dangling",
            w.types,
            [&sd](const std::string& name) -> std::optional<std::vector<uint8_t>> {
                if (name == "Data\\Star_Destroyer.txt") return sd;
                if (name == "Data\\Other.txt") return std::vector<uint8_t>{0xFF, 0xFE};  // empty: no list
                return std::nullopt;
            },
            log);
    return n;
}

std::string bare(std::string_view quoted) { return quoted.size() > 2 ? std::string(quoted.substr(1, quoted.size() - 2)) : std::string(); }

void picks(const World& w) {
    std::vector<std::string> log;
    auto n = make_names(w, &log);
    CHECK(log.size() == 1 && log[0].find("Missing string value mapping type Dangling") != std::string::npos);
    CHECK(n->list_count() == 1);  // the two Star Destroyer types share one list; the empty and missing files make none
    const objects::TypeIndex sd = w.t("Star_Destroyer"), gsd = w.t("Generic_Star_Destroyer");
    // Without replacement over the shared list, then a numeral per wrap; the quotes are part of the stored text.
    std::set<std::string> first;
    for (int i = 0; i < 3; ++i) {
        const std::string p = n->pick(i == 1 ? gsd : sd, 77);
        CHECK(p.size() > 2 && p.front() == '"' && p.back() == '"');
        first.insert(bare(p));
    }
    CHECK((first == std::set<std::string>{"Avenger", "Allecto", "Chimaera"}));
    std::set<std::string> second;
    for (int i = 0; i < 3; ++i) second.insert(bare(n->pick(sd, 77)));
    CHECK((second == std::set<std::string>{"Avenger I", "Allecto I", "Chimaera I"}));
    CHECK(bare(n->pick(sd, 77)).ends_with(" II"));
    CHECK(n->pick(w.t("Corvette"), 77).empty() && n->pick(w.t("Asteroid"), 77).empty());
    // Start_Game: cursors and wraps back to 0 (names repeat), a new shuffle at the next pick.
    n->start_game(false);
    std::set<std::string> again;
    for (int i = 0; i < 3; ++i) again.insert(bare(n->pick(sd, 5)));
    CHECK(again == first);
    // The shuffle is a function of the seed: two managers with the same seed give the same order.
    auto a = make_names(w), b = make_names(w);
    for (int i = 0; i < 3; ++i) CHECK(a->pick(sd, 1234) == b->pick(sd, 1234));
}

objects::ObjectHandle spawn(objects::ObjectStore& st, objects::TypeIndex t, objects::ObjectId parent = objects::kNoObjectId) {
    objects::SpawnParams sp;
    sp.type = t;
    sp.owner = 0;
    sp.facing = {1.0f, 0.0f, 0.0f};
    sp.parent_mode_id = parent;
    return st.create(sp);
}

void service_and_save(const World& w) {
    auto n = make_names(w);
    objects::IdCounter ids;
    objects::ObjectStore gal(objects::ModeKind::Galactic, objects::StoreKind::Primary, w.types, &ids);
    objects::ObjectStore tac(objects::ModeKind::Space, objects::StoreKind::Primary, w.types, &ids);
    const objects::ObjectHandle g1 = spawn(gal, w.t("Star_Destroyer")), rock = spawn(gal, w.t("Asteroid"));
    const objects::ObjectHandle corvette = spawn(gal, w.t("Corvette"));
    n->service(gal, nullptr, 9);
    const std::string g1_name(n->name(gal.id(g1)));
    CHECK(!g1_name.empty() && n->assigned(gal.id(g1)));
    CHECK(!n->assigned(gal.id(rock)) && n->name(gal.id(rock)).empty());           // not SELECTABLE
    CHECK(n->assigned(gal.id(corvette)) && n->name(gal.id(corvette)).empty());    // assigned, no list: never again
    // The battle: Start_Game resets the cursors; the tactical copy of g1 keeps its galactic name, a new ship picks.
    n->start_game(false);
    const objects::ObjectHandle t1 = spawn(tac, w.t("Star_Destroyer"), gal.id(g1));
    const objects::ObjectHandle t2 = spawn(tac, w.t("Generic_Star_Destroyer"));
    n->service(tac, &gal, 11);
    CHECK(n->name(tac.id(t1)) == g1_name);
    CHECK(!n->name(tac.id(t2)).empty());
    // A parent without a name (or gone): the tactical object picks for its own type.
    const objects::ObjectHandle t3 = spawn(tac, w.t("Star_Destroyer"), gal.id(rock));
    n->service(tac, &gal, 11);
    CHECK(!n->name(tac.id(t3)).empty() && n->name(tac.id(t3)) != g1_name);
    // Save chunk: names and list state round-trip; the next pick continues where the saved one would.
    ByteWriter wr;
    n->write(wr);
    auto copy = make_names(w);
    ByteReader rd(wr.bytes());
    CHECK(copy->read(rd));
    CHECK(copy->name(tac.id(t1)) == g1_name && copy->assigned(gal.id(corvette)));
    CHECK(copy->pick(w.t("Star_Destroyer"), 1) == n->pick(w.t("Star_Destroyer"), 1));
    ByteReader bad(std::span<const uint8_t>(wr.bytes().data(), wr.bytes().size() - 1));
    CHECK(!make_names(w)->read(bad));
    // A new session forgets the names.
    n->start_game(true);
    CHECK(!n->assigned(gal.id(g1)));
}

}  // namespace

int main() {
    file_format();
    World w;
    picks(w);
    service_and_save(w);
    return ::alamo::test::report("alamo_sim_ship_names_tests");
}
