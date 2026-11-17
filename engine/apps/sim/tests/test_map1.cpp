// alamo_sim_map1_tests (roadmap group map1): the battle's map load and start placement.
//   default (fast, synthetic data, battle_test_fixture.hpp):
//     * t03 a company placed on a land map is expanded into its members at the placement (Q6_T30 1, 0x2ab060), the
//       members carry the placement's hint and HINT;
//     * t04 a record's 0x4B7 extra behaviour (HINT) is attached and its 0x46E marker name kept;
//     * t05 a hangar placement with mini 0x29 = 0 has spawning off on a campaign / map battle too, one without the
//       mini keeps it on (BKG3 Q3.2);
//     * t06 a structure is placed on its remembered marker (pass 1) and the placement writes the galactic memory
//       (REMAINING_GAPS 3.2 step 4);
//     * t07 space start: the DUMMY_STAR_BASE of the battle's starbase level for the defender, the start units'
//       Space_Escort_Unit_Types, the defender's camera focus on the station marker (REMAINING_GAPS 3.2 steps 1 / 6).
//   --every-map (slow, ALAMO_FOC_DATA / ALAMO_EAW_DATA; skipped when unset): every retail .ted map (land and space) goes
//     through the TacticalBattle map load (start_map_battle, first battle tick): no crash, no unknown type, every company
//     placement expanded; per-map counts of companies, hangar-off placements, extra behaviours and marker names (t08).
#include "battle_test_fixture.hpp"

#include <chrono>
#include <functional>

#include "alamo/vfs/vfs.hpp"

namespace {

using namespace battle_fixture;

std::string map1_types(std::string t) {
    const std::string add =
        " <SpecialStructure Name=\"M1_Shield\"><Base_Position>In_Base</Base_Position><Tactical_Health>500</Tactical_Health>"
        "<CategoryMask>Structure</CategoryMask><Behavior>SELECTABLE</Behavior></SpecialStructure>\n"
        " <StarBase Name=\"M1_Star_Base_E\"><Variant_Of_Existing_Type>S10_Star_Base_1</Variant_Of_Existing_Type>"
        "<Affiliation>Empire</Affiliation><Behavior>DUMMY_STAR_BASE, SELECTABLE</Behavior></StarBase>\n"
        " <SpecialStructure Name=\"M1_Hangar\"><Tactical_Health>500</Tactical_Health><CategoryMask>Structure</CategoryMask>"
        "<Behavior>SELECTABLE, SPAWN_SQUADRON</Behavior><Starting_Spawned_Units_Tech_0>S10_Trooper, 1</Starting_Spawned_Units_Tech_0>"
        "</SpecialStructure>\n"
        " <SpaceUnit Name=\"M1_Escorted\"><Variant_Of_Existing_Type>S10_Frigate</Variant_Of_Existing_Type>"
        "<Space_Escort_Unit_Types>S10_Corvette, S10_Corvette</Space_Escort_Unit_Types></SpaceUnit>\n";
    t.insert(t.rfind("</S10>"), add);
    return t;
}

terrain::ObjectPlacement place(const char* type, float x, float y, uint32_t owner, uint32_t object_id = 0) {
    terrain::ObjectPlacement p;
    p.type_crc = name_crc(type);
    p.owner = owner;
    p.object_id = object_id;
    p.position[0] = x;
    p.position[1] = y;
    return p;
}

std::vector<objects::ObjectHandle> galactic_of(const galactic::GalacticSim& gs, uint32_t type) {
    std::vector<objects::ObjectHandle> out;
    for (const objects::ObjectHandle h : gs.store().all_objects())
        if (gs.store().alive(h) && static_cast<uint32_t>(gs.store().type(h)) == type) out.push_back(h);
    return out;
}

uint32_t count_of(const objects::ObjectStore& st, uint32_t type, int32_t owner) {
    uint32_t n = 0;
    for (const objects::ObjectHandle h : st.all_objects())
        if (st.alive(h) && static_cast<uint32_t>(st.type(h)) == type && st.owner(h) == owner) ++n;
    return n;
}

// One map battle on `map` (start_map_battle), ticked until the battle's first frame; `check` sees the running battle.
template <class Check>
void run_map_battle(const Data& d, bool land, const terrain::MapData& map, galactic::GalacticSetup setup,
                    const std::function<void(galactic::GalacticSim&, galactic::BattleRequest&)>& prepare, const Check& check) {
    jobs::JobSystem js(jobs::JobSystemConfig{0});
    SimConfig cfg;
    cfg.seed = 2026;
    cfg.players = 4;
    SimHost host(d.types, js, cfg);
    galactic::GalacticSim* gs = host.start_galactic(d.rules, setup);
    CHECK(gs != nullptr);
    if (!gs) return;
    CampaignBattleOptions opt;
    opt.battle.max_frames = 0;
    CampaignBattles battles(host, d.crules, *d.catalog,
                            [map](const std::string& name, terrain::MapData& m) {
                                if (name != "M1_Map") return false;
                                m = map;
                                return true;
                            },
                            opt);
    CHECK(battles.attach(nullptr));
    galactic::BattleRequest r;
    r.info.kind = land ? galactic::ConflictKind::Land : galactic::ConflictKind::Space;
    r.info.planet = 1;
    r.info.defender = 0;
    r.info.attacker = 1;
    r.info.map = r.map = "M1_Map";
    prepare(*gs, r);
    CHECK(battles.start_map_battle(std::move(r)));
    for (uint64_t t = 0; t < 40; ++t) {
        host.tick_once();
        if (battles.battle() && host.tactical() && battles.battle()->frame() >= 1) {
            check(*gs, *battles.battle(), *host.tactical());
            return;
        }
    }
    CHECK(false);  // the battle never ran
}

void test_land(const Data& d) {
    const uint32_t trooper = type_of(d, "S10_Trooper"), shield = type_of(d, "M1_Shield");
    static const objects::BehaviorTypeId kHint = objects::behavior_type_id("HINT");
    terrain::MapData m = land_map();
    m.objects.push_back(place("Defending Forces Position", 600.0f, 600.0f, 0));
    m.objects.push_back(place("Attacker Entry Position", 200.0f, 200.0f, 1));
    m.objects.push_back(place("In Base Special Structure Position", 500.0f, 400.0f, 0, 11));
    m.objects.push_back(place("In Base Special Structure Position", 900.0f, 400.0f, 0, 12));
    terrain::ObjectPlacement squad = place("S10_Squad", 400.0f, 800.0f, 0);  // t03: a company with a hint
    squad.hint = "squadhint";
    m.objects.push_back(squad);
    terrain::ObjectPlacement one = place("S10_Trooper", 450.0f, 850.0f, 0);  // t04 / t05: extra HINT, marker name, byte 0
    terrain::ObjectPlacement::ExtraBehavior e;
    e.name_crc = name_crc("HINT");
    one.extra_behaviors.push_back(e);
    one.has_marker_name = true;
    one.marker_name = "inf4";
    one.placement_raw.push_back(terrain::RawMini{0x29, {0}});
    m.objects.push_back(one);
    terrain::ObjectPlacement hangar = place("M1_Hangar", 1100.0f, 1100.0f, 0);  // t05: a hangar placement with byte 0
    hangar.placement_raw.push_back(terrain::RawMini{0x29, {0}});
    m.objects.push_back(hangar);
    m.objects.push_back(place("M1_Hangar", 1300.0f, 1100.0f, 0));  // t05: the same without the byte (creation value: on)
    galactic::GalacticSetup setup = make_setup(d, true);
    setup.forces.push_back({0, 1, shield});
    for (const int32_t remembered : {-1, 12}) {
        objects::ObjectId shield_gid = objects::kNoObjectId;
        run_map_battle(
            d, true, m, setup,
            [&](galactic::GalacticSim& gs, galactic::BattleRequest& r) {
                const auto sh = galactic_of(gs, shield);
                CHECK(sh.size() == 1);
                if (sh.empty()) return;
                r.structures = sh;
                shield_gid = gs.store().id(sh[0]);
                if (remembered >= 0) gs.set_structure_slot(shield_gid, 1, remembered);
            },
            [&](galactic::GalacticSim& gs, TacticalBattle& b, TacticalMode& tm) {
                const objects::ObjectStore& st = tm.primary();
                const MapLoadStats& s = b.map_load_stats();
                std::printf("  land (remembered %d): placements %u, companies %u (%u members), extra behaviours %u, marker names %u, "
                            "hangar off %u, unknown %u, remembered markers %u\n",
                            remembered, s.placements, s.companies, s.company_members, s.extra_behaviors, s.marker_names, s.hangar_off,
                            s.unknown_types, s.remembered_markers);
                CHECK(s.companies == 1 && s.company_members == 4 && s.extra_behaviors == 1 && s.marker_names == 1 && s.hangar_off == 1 &&
                      s.unknown_types == 0);
                // t03: four troopers stand at the squad's placement with its hint and HINT; t04: the single one has HINT and the name
                uint32_t members = 0, hinted = 0, named = 0;
                for (const objects::ObjectHandle h : st.all_objects()) {
                    if (!st.alive(h) || static_cast<uint32_t>(st.type(h)) != trooper || st.owner(h) != 0) continue;
                    const Vec3 p = st.position(h);
                    const objects::ObjectId id = st.id(h);
                    if (std::fabs(p.x - 400.0f) < 10.0f && std::fabs(p.y - 800.0f) < 10.0f) {  // created at the placement; the first frame separates them
                        ++members;
                        hinted += b.map_hint(id) == "squadhint" && st.has_behavior(h, kHint) ? 1u : 0u;
                    }
                    if (p.x == 450.0f && p.y == 850.0f) named += b.map_marker_name(id) == "inf4" && st.has_behavior(h, kHint) ? 1u : 0u;
                    if (std::getenv("MAP1_DEBUG"))
                        std::printf("    trooper %d at (%.2f, %.2f) hint '%s' HINT %d name '%s'\n", id, static_cast<double>(p.x),
                                    static_cast<double>(p.y), std::string(b.map_hint(id)).c_str(), st.has_behavior(h, kHint) ? 1 : 0,
                                    std::string(b.map_marker_name(id)).c_str());
                }
                CHECK(members == 4 && hinted == 4 && named == 1);
                // q6 t31 (Q6_T30 §1-§2 [H]): the company object itself is not findable: it is removed after expansion
                const uint32_t squad_type = type_of(d, "S10_Squad");
                uint32_t squads = 0;
                for (const objects::ObjectHandle h : st.all_objects())
                    squads += st.exists(h) && static_cast<uint32_t>(st.type(h)) == squad_type ? 1u : 0u;
                CHECK(squads == 0);
                // t05: the byte-0 hangar spawns nothing (+0x3CB off); the one without the byte spawns
                const uint32_t hangar_type = type_of(d, "M1_Hangar");
                uint32_t hangars_off = 0, hangars_on = 0;
                for (const objects::ObjectHandle h : st.all_objects()) {
                    if (!st.alive(h) || static_cast<uint32_t>(st.type(h)) != hangar_type) continue;
                    const bool on = b.hangars() && b.hangars()->spawn_enabled(st.id(h));
                    const float x = st.position(h).x;
                    hangars_off += x == 1100.0f && !on ? 1u : 0u;
                    hangars_on += x == 1300.0f && on ? 1u : 0u;
                }
                CHECK(hangars_off == 1 && hangars_on == 1);
                // t06: the structure on the remembered marker (pass 1), else the first free in-base marker; the memory follows
                Vec3 at{};
                CHECK(count_of(st, shield, 0) == 1);
                for (const objects::ObjectHandle h : st.all_objects())
                    if (st.alive(h) && static_cast<uint32_t>(st.type(h)) == shield) at = st.position(h);
                const galactic::GalacticSim::StructureSlot* mem = gs.structure_slot(shield_gid);
                CHECK(mem != nullptr);
                if (remembered < 0) {
                    CHECK(at.x == 500.0f && at.y == 400.0f && s.remembered_markers == 0 && mem && mem->marker == 11 && mem->slot == 0);
                } else {
                    CHECK(at.x == 900.0f && at.y == 400.0f && s.remembered_markers == 1 && mem && mem->marker == 12 && mem->slot == 1);
                }
            });
    }
}

void test_space(const Data& d) {
    const uint32_t fr = type_of(d, "S10_Frigate"), co = type_of(d, "S10_Corvette"), sb = type_of(d, "S10_Star_Base_1"),
                   picked = type_of(d, "M1_Star_Base_E"), esc = type_of(d, "M1_Escorted");
    terrain::MapData m = space_map();
    m.objects.push_back(place("Space Station Position", 1000.0f, 0.0f, 0));
    m.objects.push_back(place("Defending Forces Position", 800.0f, 0.0f, 0));
    m.objects.push_back(place("Attacker Entry Position", -2000.0f, 0.0f, 1));
    galactic::GalacticSetup setup = make_setup(d, false);
    setup.forces = {{0, 1, esc}, {0, 1, sb}, {1, 2, fr}};
    run_map_battle(
        d, false, m, setup,
        [&](galactic::GalacticSim& gs, galactic::BattleRequest& r) {
            const auto s0 = galactic_of(gs, esc), sbase = galactic_of(gs, sb), s1 = galactic_of(gs, fr);
            CHECK(s0.size() == 1 && sbase.size() == 1 && s1.size() == 1);
            galactic::BattleForces f0, f1;
            f0.player = 0;
            f0.start = s0;
            f1.player = 1;
            f1.start = s1;
            r.forces = {f0, f1};
            if (!sbase.empty()) r.starbase = sbase[0];
            r.base_level = 1;
        },
        [&](galactic::GalacticSim&, TacticalBattle& b, TacticalMode& tm) {
            const objects::ObjectStore& st = tm.primary();
            const MapLoadStats& s = b.map_load_stats();
            Vec3 focus{}, other{};
            const bool f0 = b.start_camera_focus(0, focus), f1 = b.start_camera_focus(1, other);
            std::printf("  space: starbase picked %d (M1_Star_Base_E %u, S10_Star_Base_1 %u), escorts %u (corvettes of the defender %u), "
                        "focus %d (%.0f, %.0f), attacker focus %d\n",
                        s.starbase_picked ? 1 : 0, count_of(st, picked, 0), count_of(st, sb, 0), s.escorts, count_of(st, co, 0), f0 ? 1 : 0,
                        static_cast<double>(focus.x), static_cast<double>(focus.y), f1 ? 1 : 0);
            CHECK(s.starbase_picked && count_of(st, picked, 0) == 1 && count_of(st, sb, 0) == 0);
            CHECK(s.escorts == 2 && count_of(st, co, 0) == 2 && count_of(st, esc, 0) == 1);
            CHECK(f0 && focus.x == 1000.0f && focus.y == 0.0f && !f1);
        });
}

// ---- t08: every retail map through the TacticalBattle map load ----

struct EveryMapTotals {
    uint32_t maps = 0, land = 0, space = 0, failed = 0, placements = 0, unknown = 0, unknown_maps = 0, companies = 0, members = 0;
    uint32_t hangar_off = 0, extra = 0, names = 0, picks = 0, allowed = 0, hinted = 0, hinted_no_beh = 0;
};

// Placements whose type is in no shipped XML or script of FoC or EaW (searched 2026-11-17: the CRCs match no name in the
// extracted data text), so retail cannot create them either: pre-release (_PM*), benchmark / testbed maps, the Alderaan
// skirmish demo maps and four types on UM08 (10 placements). Map -> the number of such placements (both data sets).
uint32_t unknown_allowance(std::string_view map) {
    static const std::pair<const char*, uint32_t> kAllow[] = {
        {"BENCHMARK_SPACE", 3}, {"SKIRMISH_DEMO_LAND_ALDERAAN", 1}, {"SKIRMISH_LAND_ALDERAAN_(SHIELD)", 1}, {"SPACE_UNIT_TESTBED", 3},
        {"UM08_REBEL_EVICTION", 10}, {"_PM1_REBEL_SPACE_PLANET_DANTOOINE_", 1}, {"_PM2_EMPIRE_SPACE_PLANET_FONDOR_", 1},
        {"_PM6_EMPIRE_LAND_PLANET_ILUM_", 1}, {"_PM9_EMPIRE_LAND_PLANET_ANAXES_", 15}};
    for (const auto& [n, c] : kAllow)
        if (ascii_iequals(map, n)) return c;
    return 0;
}

void every_map(const char* label, vfs::VfsConfig vc, EveryMapTotals& tot) {
    std::string err;
    auto files = vfs::Vfs::create(vc, &err);
    CHECK(files != nullptr);
    if (!files) return;
    auto d = std::make_unique<Data>();
    std::vector<std::string> diag;
    CHECK(d->db.load(*files, {}, &diag) && d->raw.load(*files) && d->types.build(d->raw, files.get(), &diag));
    CHECK(galactic::load_galactic_rules(*files, d->db, d->types, d->rules, &diag));
    CHECK(galactic::load_conflict_rules(*files, d->db, d->types, d->rules, d->crules, &diag));
    d->catalog = std::make_unique<BattleCatalog>(d->types, files.get(), &d->db.constants(), &diag);
    // The campaign with the most players (every faction a player, so map owners resolve as in a campaign battle).
    galactic::GalacticSetup setup;
    std::string campaign;
    for (const data::CampaignDef& c : d->db.campaigns()) {
        std::vector<std::string> cd;
        galactic::GalacticSetup s = galactic::build_campaign_setup(d->db, d->rules, c.name, {}, &cd);
        if (s.map.planets.size() > 0 && s.players.size() > setup.players.size()) {
            setup = std::move(s);
            campaign = c.name;
        }
    }
    CHECK(setup.players.size() >= 3);
    if (setup.players.size() < 3) return;
    const auto maps = files->list("Data\\Art\\Maps\\*.ted");
    std::printf("  %s: %zu maps, campaign %s (%zu players)\n", label, maps.size(), campaign.c_str(), setup.players.size());
    const auto t0 = std::chrono::steady_clock::now();
    for (const std::string& path : maps) {
        std::string name = path.substr(path.find_last_of("\\/") + 1);
        name = name.substr(0, name.size() - 4);
        terrain::MapData map;
        if (!terrain::load_map(*files, name, map, &err)) {
            std::printf("FAIL %s %s: %s\n", label, name.c_str(), err.c_str());
            ++tot.failed;
            ++g_fail;
            continue;
        }
        const bool land = !map.is_space();
        bool ran = false;
        jobs::JobSystem js(jobs::JobSystemConfig{0});
        SimConfig cfg;
        cfg.seed = 2026;
        cfg.players = session_players(setup.players.size());
        SimHost host(d->types, js, cfg);
        galactic::GalacticSim* gs = host.start_galactic(d->rules, setup);
        CHECK(gs != nullptr);
        if (!gs) return;
        CampaignBattleOptions opt;
        opt.battle.max_frames = 0;
        CampaignBattles battles(host, d->crules, *d->catalog,
                                [&map](const std::string&, terrain::MapData& m) {
                                    m = map;
                                    return true;
                                },
                                opt);
        CHECK(battles.attach(nullptr));
        galactic::BattleRequest r;
        r.info.kind = land ? galactic::ConflictKind::Land : galactic::ConflictKind::Space;
        r.info.planet = 0;
        r.info.defender = 0;
        r.info.attacker = 1;
        r.info.map = r.map = name;
        r.base_level = 5;  // [port] every map object (no base-level drop)
        CHECK(battles.start_map_battle(std::move(r)));
        for (uint64_t t = 0; t < 40 && !ran; ++t) {
            host.tick_once();
            const TacticalBattle* b = battles.battle();
            if (!b || b->frame() < 1) continue;
            ran = true;
            const MapLoadStats& s = b->map_load_stats();
            ++tot.maps;
            tot.land += land ? 1u : 0u;
            tot.space += land ? 0u : 1u;
            tot.placements += s.placements;
            tot.unknown += s.unknown_types;
            const uint32_t allow = unknown_allowance(name);
            if (s.unknown_types > allow) {
                std::printf("FAIL %s %s: %u unknown types (allowance %u)\n", label, name.c_str(), s.unknown_types, allow);
                ++g_fail;
            }
            tot.allowed += std::min(s.unknown_types, allow);
            tot.unknown_maps += s.unknown_types ? 1u : 0u;
            tot.companies += s.companies;
            tot.members += s.company_members;
            tot.hangar_off += s.hangar_off;
            tot.extra += s.extra_behaviors;
            tot.names += s.marker_names;
            tot.picks += s.spawn_table_picks;
            if (s.unknown_types || s.companies || s.hangar_off)
                std::printf("    %s %s: %u placements, unknown %u, companies %u (%u members), hangar off %u, HINT %u, names %u\n", label,
                            name.c_str(), s.placements, s.unknown_types, s.companies, s.company_members, s.hangar_off, s.extra_behaviors,
                            s.marker_names);
            // q6 t32: Find_Hint walks HINT objects only (Q6_T30 §2 [H]); a hint in a retail map implies HINT (§3), from the
            // type or the record's 0x4B7 list, so every object that got a map hint must have HINT or a script misses it.
            if (const TacticalMode* tm = host.tactical()) {
                static const objects::BehaviorTypeId kHint = objects::behavior_type_id("HINT");
                uint32_t bad = 0;
                for (const objects::ObjectHandle h : tm->primary().all_objects()) {
                    if (!tm->primary().exists(h) || b->map_hint(tm->primary().id(h)).empty()) continue;
                    ++tot.hinted;
                    if (!tm->primary().has_behavior(h, kHint)) ++bad;
                }
                tot.hinted_no_beh += bad;
                if (bad) {
                    std::printf("FAIL %s %s: %u objects with a map hint but no HINT\n", label, name.c_str(), bad);
                    ++g_fail;
                }
            }
            if (s.companies && s.company_members < s.companies) {  // a company that spawned nothing is a dropped placement
                std::printf("FAIL %s %s: %u companies spawned %u members\n", label, name.c_str(), s.companies, s.company_members);
                ++g_fail;
            }
        }
        if (!ran) {
            std::printf("FAIL %s %s: no battle frame\n", label, name.c_str());
            ++tot.failed;
            ++g_fail;
        }
    }
    std::printf("  %s: %.1f s\n", label, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
}

int run_every_map() {
    const std::string foc = alamo::test::env_or_empty("ALAMO_FOC_DATA"), eaw = alamo::test::env_or_empty("ALAMO_EAW_DATA");
    if (foc.empty() && eaw.empty()) {
        std::printf("every map: ALAMO_FOC_DATA / ALAMO_EAW_DATA not set, skipped\n");
        return 0;
    }
    EveryMapTotals tot;
    if (!foc.empty()) {  // FoC (its EaW base layer is mounted too, as the game does)
        vfs::VfsConfig vc;
        vc.foc_root = foc;
        every_map("FoC", vc, tot);
    }
    if (!eaw.empty()) {  // EaW alone: its own data and maps
        vfs::VfsConfig vc;
        vc.foc_root = eaw;
        vc.eaw_root = std::string();
        every_map("EaW", vc, tot);
    }
    std::printf("every map: %u maps (%u land, %u space), %u failed; %u placements, %u unknown types in %u maps, %u companies -> %u "
                "members, %u hangar-off placements, %u extra behaviours, %u marker names, %u spawn-table picks, %u hinted objects (%u "
                "without HINT)\n",
                tot.maps, tot.land, tot.space, tot.failed, tot.placements, tot.unknown, tot.unknown_maps, tot.companies, tot.members,
                tot.hangar_off, tot.extra, tot.names, tot.picks, tot.hinted, tot.hinted_no_beh);
    CHECK(tot.maps > 300 && tot.failed == 0 && tot.unknown == tot.allowed);
    std::printf(g_fail ? "alamo_sim_map1_tests --every-map: %d failure(s)\n" : "alamo_sim_map1_tests --every-map: OK\n", g_fail);
    return g_fail ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--every-map") return run_every_map();
    g_temp_prefix = "alamo_sim_map1_tests";
    g_types_patch = map1_types;
    auto d = make_data();
    g_types_patch = nullptr;
    std::printf("map1: land map load (companies, record data, hangar byte, structure memory)\n");
    test_land(*d);
    std::printf("map1: space start (starbase pick, escorts, camera focus)\n");
    test_space(*d);
    std::printf(g_fail ? "alamo_sim_map1_tests: %d failure(s)\n" : "alamo_sim_map1_tests: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
