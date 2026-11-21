// alamo_sim_save_continue_tests (roadmap ref2 t01, serves map3): save and continue on every battle fixture.
// Each fixture (battle_test_fixture.hpp sessions: a space battle, a land invasion, a space battle with a retreat, a land
// battle whose map carries placement records - hints, a company with a hint, a marker name, a build pad with a base
// level on a planet with a ground structure - roadmap map3) runs twice from the same start:
//   continuous   ticked straight through;
//   reloaded     at every 64-tick checkpoint (the CRC golden checkpoints, ALAMO_CRC 2) the session is saved, a fresh
//                session is built like a new game, the save is loaded into it (a battle is rebuilt, then its parts are
//                read) and the run goes on in the fresh session.
// The checkpoint CRC records of both runs agree (every registered system, at every checkpoint, including the record
// the fresh session takes of the loaded state), and so do the answers the battle's Lua finders give after a load:
// Find_Hint / Get_Hint (TacticalBattle::map_hint, which lua::ObjectBridge::map_hint reads), the marker names and the
// pads' base levels, for every object of the battle; no object created after the battle start (after a load included)
// answers a map hint. On every running battle the save ledger self-check holds too
// (TacticalBattle::check_save_ledger: state_io walks exactly the SAVED members of tactical_battle_ledger.hpp).
// Modes:
//   (none)                        every fixture at 0 workers
//   --workers N [--crc-out F]     every fixture at N workers; --crc-out writes the continuous runs' checkpoint records
//   [--golden F]                  and --golden compares them with that file (alamo_add_worker_count_check: 0 / 1 / 4)
#include "battle_test_fixture.hpp"

#include <sstream>

namespace {

constexpr uint64_t kCheckpoint = 64;  // the golden checkpoint interval (docs/status/golden-checkpoints.md)

struct Fixture {
    const char* name;
    bool land;
    float retreat_ratio;
    bool records;  // land map with placement records (hints, a hinted company, a marker name, a levelled pad)
    uint64_t ticks;
};

const Fixture kFixtures[] = {
    {"space", false, 0.0f, false, 2500},
    {"land", true, 0.0f, false, 3000},
    {"space_retreat", false, 0.95f, false, 2600},
    {"land_records", true, 0.0f, true, 3000},
};

terrain::ObjectPlacement placement(const char* type, float x, float y, uint32_t owner) {
    terrain::ObjectPlacement p;
    p.type_crc = name_crc(type);
    p.owner = owner;
    p.position[0] = x;
    p.position[1] = y;
    return p;
}

// The land map with placement records (map1 t03 / t04, q6 t28): a company with a hint (its members inherit it), a
// trooper with an extra HINT, a hint and a marker name, and a hinted trooper of the attacker.
terrain::MapData records_map() {
    terrain::MapData m = land_map();
    terrain::ObjectPlacement::ExtraBehavior hint;
    hint.name_crc = name_crc("HINT");
    terrain::ObjectPlacement squad = placement("S10_Squad", 400.0f, 800.0f, 0);
    squad.hint = "squadhint";
    squad.extra_behaviors.push_back(hint);
    m.objects.push_back(squad);
    terrain::ObjectPlacement one = placement("S10_Trooper", 450.0f, 850.0f, 0);
    one.hint = "FalconGear1";
    one.extra_behaviors.push_back(hint);
    one.has_marker_name = true;
    one.marker_name = "inf4";
    m.objects.push_back(one);
    terrain::ObjectPlacement two = placement("S10_Trooper", 300.0f, 300.0f, 1);
    two.hint = "attackerhint";
    two.extra_behaviors.push_back(hint);
    two.has_marker_name = true;
    two.marker_name = "inf9";
    m.objects.push_back(two);
    // map3: a build pad with base level 1 (mini 0x0B; the planet's ground structure makes the battle's base level 1)
    terrain::ObjectPlacement pad = placement("S10_Build_Pad", 600.0f, 700.0f, 0);
    pad.placement_raw.push_back(terrain::RawMini{0x0B, {1}});
    m.objects.push_back(pad);
    return m;
}

// map3: the records fixture's extra types - a ground structure (its galactic copy at S10_B gives the land battle base
// level 1) and a build pad.
std::string records_types(std::string t) {
    const std::string end = "</S10>";
    const size_t at = t.rfind(end);
    if (at == std::string::npos) return t;
    t.insert(at, " <GroundStructure Name=\"S10_Barracks\"><Tactical_Health>500</Tactical_Health><Behavior>DUMMY_GROUND_STRUCTURE</Behavior>"
                 "</GroundStructure>\n <SpecialStructure Name=\"S10_Build_Pad\"><Behavior>TACTICAL_BUILD_OBJECTS</Behavior>"
                 "<Tactical_Health>500</Tactical_Health></SpecialStructure>\n");
    return t;
}

// One session of a fixture, built as run_campaign builds it. `launch`: the new-game start (the Underworld fleet moves
// to B); a session that is going to load a save skips it (the save holds the galaxy).
struct Session {
    std::unique_ptr<jobs::JobSystem> js;
    std::unique_ptr<SimHost> host;
    galactic::GalacticSim* gs = nullptr;
    std::unique_ptr<CampaignBattles> battles;
#ifdef ALAMO_SIM_HAVE_AI_TACTICAL
    std::vector<ai::tactical::TacticalBudgetCategory> budget = std::vector<ai::tactical::TacticalBudgetCategory>(1);
#endif
    bool land = false, invaded = false;
    size_t crc_seen = 0;  // records of crc_log() already collected

    Session(const Data& d, const Fixture& f, uint32_t workers, bool launch) : land(f.land) {
        js = std::make_unique<jobs::JobSystem>(jobs::JobSystemConfig{static_cast<int32_t>(workers)});
        SimConfig cfg;
        cfg.seed = 2026;
        cfg.players = 4;
        host = std::make_unique<SimHost>(d.types, *js, cfg);
        galactic::GalacticSetup setup = make_setup(d, f.land);
        if (f.records) setup.forces.push_back({0, 1, type_of(d, "S10_Barracks")});  // map3: battle base level 1
        gs = host->start_galactic(d.rules, setup);
        CHECK(gs != nullptr);
        if (!gs) return;
        gs->set_emit_events(true);
        CampaignBattleOptions opt;
        opt.conflict.human_choice = galactic::BattleChoice::Fight;
        opt.battle.max_frames = 6000;
        opt.battle.retreat_ratio = f.retreat_ratio;
        opt.battle.fog = true;
#ifdef ALAMO_SIM_HAVE_AI_TACTICAL
        battles_budget_hook(opt, budget, *gs);
#endif
        const bool records = f.records;
        battles = std::make_unique<CampaignBattles>(*host, d.crules, *d.catalog,
                                                    [records](const std::string& name, terrain::MapData& m) {
                                                        if (name == "S10_Space_B") m = space_map();
                                                        else if (name == "S10_Land_B") m = records ? records_map() : land_map();
                                                        else return false;
                                                        return true;
                                                    },
                                                    opt);
        CHECK(battles->attach(nullptr));
        if (!launch) return;  // the save holds the galaxy (the caller carries `invaded` over)
        if (f.land) {
            std::vector<objects::ObjectHandle> up;
            for (objects::ObjectHandle h : gs->planets()[2].surface)
                if (gs->store().owner(h) == 1) up.push_back(h);
            CHECK(gs->launch_units(1, 2, up) == commands::RejectReason::None);
        }
        const std::vector<uint32_t> uf = gs->fleets_at(2, 1);
        CHECK(!uf.empty());
        if (!uf.empty()) CHECK(gs->move_fleet(1, uf[0], 1) == commands::RejectReason::None);
    }

    void tick() {
        host->tick_once();
        if (land && !invaded && host->modes().depth() == 1) {
            const std::vector<uint32_t> f = gs->fleets_at(1, 1);
            if (!f.empty() && !gs->fleets()[f[0]].moving) {
                CHECK(gs->invade(1, 1, {}) == commands::RejectReason::None);
                invaded = true;
            }
        }
    }

    TacticalBattle* battle() const {
        TacticalBattle* b = battles ? battles->battle() : nullptr;
        return b && host->tactical() && b->frame() >= 1 ? b : nullptr;
    }
};

// A checkpoint record with its system names (a fresh session may register them in another order).
struct Checkpoint {
    uint64_t tick = 0, combined = 0;
    uint32_t parity = 0;
    std::map<std::string, uint64_t> systems;
    friend bool operator==(const Checkpoint&, const Checkpoint&) = default;
};

void collect(Session& s, std::vector<Checkpoint>& out) {
    const CrcLog& log = s.host->crc_log();
    for (; s.crc_seen < log.records.size(); ++s.crc_seen) {
        const CrcRecord& r = log.records[s.crc_seen];
        if (r.tick % kCheckpoint != 0) continue;
        Checkpoint c;
        c.tick = r.tick;
        c.combined = r.combined;
        c.parity = r.parity;
        for (size_t i = 0; i < r.systems.size() && i < log.systems.size(); ++i) c.systems[log.systems[i]] = r.systems[i];
        out.push_back(std::move(c));
    }
}

// What the battle's Lua finders answer per object: Find_Hint / Get_Hint / Find_All_Objects_With_Hint read
// TacticalBattle::map_hint through lua::ObjectBridge::map_hint; the marker name is the placement's 0x46E record; the
// pad level is the map pad's base level (AI_GALACTIC_TOKENS 9.4, the persistence record's level).
std::vector<std::string> lookup_answers(const TacticalBattle& b, const TacticalMode& tm) {
    std::vector<std::string> out;
    const objects::ObjectStore& st = tm.primary();
    for (const objects::ObjectHandle h : st.all_objects()) {
        if (!st.exists(h)) continue;
        const objects::ObjectId id = st.id(h);
        const std::string_view hint = b.map_hint(id), marker = b.map_marker_name(id);
        const int32_t level = b.pad_base_level(id);
        if (hint.empty() && marker.empty() && level == 0) continue;
        out.push_back(std::to_string(id) + " hint '" + std::string(hint) + "' marker '" + std::string(marker) + "' pad " +
                      std::to_string(level));
    }
    std::sort(out.begin(), out.end());
    return out;
}

struct RunResult {
    std::vector<Checkpoint> checkpoints;
    std::map<uint64_t, std::vector<std::string>> answers;  // checkpoint tick -> lookup answers (the reloaded run: after the load)
    uint32_t battle_checkpoints = 0, reloads = 0, battle_reloads = 0, refused = 0;
    uint32_t ledger_checks = 0;
    bool ledger_ok = true, load_ok = true;
    uint64_t last_tick = 0;
};

void check_ledger(const TacticalBattle& b, RunResult& r, const char* fixture) {
    std::string err;
    ++r.ledger_checks;
    if (!b.check_save_ledger(&err)) {
        if (r.ledger_ok) std::printf("  %s: save ledger: %s\n", fixture, err.c_str());
        r.ledger_ok = false;
    }
}

RunResult run(const Data& d, const Fixture& f, uint32_t workers, bool reload) {
    RunResult r;
    auto s = std::make_unique<Session>(d, f, workers, true);
    if (!s->gs) return r;
    for (uint64_t t = 0; t < f.ticks; ++t) {
        s->tick();
        collect(*s, r.checkpoints);
        const uint64_t tick = s->host->frame();
        r.last_tick = tick;
        if (tick % kCheckpoint != 0) continue;
        TacticalBattle* b = s->battle();
        if (b) {
            ++r.battle_checkpoints;
            check_ledger(*b, r, f.name);
            if (!reload) r.answers[tick] = lookup_answers(*b, *s->host->tactical());
        }
        if (!reload) continue;
        std::string why;
        if (!s->host->save_allowed(&why)) {
            ++r.refused;
            continue;
        }
        SaveFile save;
        if (!s->host->save(save, &why)) {
            std::printf("  %s: save at tick %" PRIu64 " failed: %s\n", f.name, tick, why.c_str());
            r.load_ok = false;
            break;
        }
        auto fresh = std::make_unique<Session>(d, f, workers, false);
        if (!fresh->gs) break;
        fresh->invaded = s->invaded;  // the test's own invasion order, given once (the save holds its effect)
        LoadReport rep;
        if (!fresh->host->load(save, &why, &rep)) {
            std::printf("  %s: load at tick %" PRIu64 " failed: %s\n", f.name, tick, rep.text().c_str());
            r.load_ok = false;
            break;
        }
        CHECK(fresh->host->frame() == tick);
        ++r.reloads;
        s = std::move(fresh);
        if (TacticalBattle* lb = s->battle()) {
            ++r.battle_reloads;
            check_ledger(*lb, r, f.name);
            r.answers[tick] = lookup_answers(*lb, *s->host->tactical());
        }
    }
    return r;
}

// The first difference of two checkpoint lists, by tick (the reloaded run has the loaded state's record twice).
std::string first_difference(const std::vector<Checkpoint>& want, const std::vector<Checkpoint>& got) {
    std::map<uint64_t, const Checkpoint*> by_tick;
    for (const Checkpoint& c : want) by_tick[c.tick] = &c;
    for (const Checkpoint& c : got) {
        const auto it = by_tick.find(c.tick);
        if (it == by_tick.end()) return "tick " + std::to_string(c.tick) + " is not a checkpoint of the continuous run";
        if (*it->second == c) continue;
        std::ostringstream o;
        o << "tick " << c.tick << ":";
        for (const auto& [name, v] : it->second->systems) {
            const auto g = c.systems.find(name);
            if (g == c.systems.end()) o << " " << name << " missing";
            else if (g->second != v) o << " " << name;
        }
        for (const auto& [name, v] : c.systems)
            if (!it->second->systems.count(name)) o << " " << name << " extra";
        if (it->second->parity != c.parity) o << " parity";
        return o.str();
    }
    std::map<uint64_t, bool> seen;
    for (const Checkpoint& c : got) seen[c.tick] = true;
    for (const Checkpoint& c : want)
        if (!seen.count(c.tick)) return "checkpoint " + std::to_string(c.tick) + " missing from the reloaded run";
    return {};
}

std::string checkpoints_text(const std::vector<Checkpoint>& cps) {
    std::ostringstream o;
    for (const Checkpoint& c : cps) {
        o << c.tick << " " << std::hex << c.combined << " " << c.parity;
        for (const auto& [name, v] : c.systems) o << " " << name << "=" << v;
        o << std::dec << "\n";
    }
    return o.str();
}

std::string test_fixture(const Data& d, const Fixture& f, uint32_t workers) {
    const RunResult a = run(d, f, workers, false);
    const RunResult b = run(d, f, workers, true);
    std::printf("  %s (%u workers): %zu checkpoints (%u in battle), %u reloads (%u in battle, %u refused), %u ledger checks\n", f.name,
                workers, a.checkpoints.size(), a.battle_checkpoints, b.reloads, b.battle_reloads, b.refused,
                a.ledger_checks + b.ledger_checks);
    CHECK(a.battle_checkpoints >= 4);   // the fixture's battle spans several checkpoints
    CHECK(b.battle_reloads >= 4);       // and was saved / rebuilt / loaded at them
    CHECK(a.ledger_ok && b.ledger_ok);  // state_io walks the ledger's SAVED members
    CHECK(b.load_ok);
    CHECK(a.last_tick == b.last_tick);
    const std::string diff = first_difference(a.checkpoints, b.checkpoints);
    if (!diff.empty()) std::printf("  %s: checkpoint CRCs differ after a load: %s\n", f.name, diff.c_str());
    CHECK(diff.empty());
    uint32_t compared = 0, hinted = 0, padded = 0;
    // map3: the map load names every object with a hint, a marker name or a pad level; an object created later (also one
    // created after a load, which a rebuild-era id table would have named) answers none of them.
    std::map<std::string, bool> first_ids;
    if (!a.answers.empty())
        for (const std::string& line : a.answers.begin()->second) first_ids[line.substr(0, line.find(' '))] = true;
    for (const auto& [tick, got] : b.answers)
        for (const std::string& line : got)
            if (!first_ids.count(line.substr(0, line.find(' ')))) {
                std::printf("  %s: tick %" PRIu64 ": an object created after the map load answers '%s'\n", f.name, tick, line.c_str());
                CHECK(false);
            }
    for (const auto& [tick, want] : a.answers) {
        const auto it = b.answers.find(tick);
        if (it == b.answers.end()) continue;  // no battle at that checkpoint of the reloaded run
        ++compared;
        hinted += want.empty() ? 0u : 1u;
        for (const std::string& line : want) padded += line.size() > 6 && line.compare(line.size() - 6, 6, " pad 1") == 0 ? 1u : 0u;
        if (it->second != want) {
            std::printf("  %s: lookup answers differ after the load at tick %" PRIu64 " (%zu vs %zu):\n", f.name, tick,
                        want.size(), it->second.size());
            for (size_t i = 0; i < std::max(want.size(), it->second.size()) && i < 6; ++i)
                std::printf("    want %-48s got %s\n", i < want.size() ? want[i].c_str() : "-",
                            i < it->second.size() ? it->second[i].c_str() : "-");
            CHECK(false);
            break;
        }
    }
    CHECK(compared == b.battle_reloads);
    if (f.records) CHECK(hinted == compared && compared > 0);  // the records fixture has hints at every checkpoint
    if (f.records) CHECK(padded == compared);                  // and its pad keeps its base level
    return "# " + std::string(f.name) + "\n" + checkpoints_text(a.checkpoints);
}

}  // namespace

int main(int argc, char** argv) {
    uint32_t workers = 0;
    std::string out_path, golden;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--workers") workers = static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 10));
        else if (a == "--crc-out") out_path = argv[i + 1];
        else if (a == "--golden") golden = argv[i + 1];
    }
    static std::string prefix;  // a data folder per worker count (the ctest entries run side by side)
    prefix = "alamo_sim_save_continue_w" + std::to_string(workers);
    g_temp_prefix = prefix.c_str();
    std::printf("REF2 t01: save and continue at every %" PRIu64 "-tick checkpoint, %u workers\n", kCheckpoint, workers);
    auto d = make_data();
    // map3: the records fixture's data adds a ground structure and a build pad (its own temp folder)
    static std::string records_prefix;
    records_prefix = prefix + "_records";
    g_temp_prefix = records_prefix.c_str();
    g_types_patch = records_types;
    auto dr = make_data();
    g_types_patch = nullptr;
    g_temp_prefix = prefix.c_str();
    std::string text;
    for (const Fixture& f : kFixtures) text += test_fixture(f.records ? *dr : *d, f, workers);
    if (!out_path.empty()) {
        std::ofstream o(out_path, std::ios::binary | std::ios::trunc);
        o << text;
        CHECK(static_cast<bool>(o));
    }
    if (!golden.empty()) {
        std::ifstream in(golden, std::ios::binary);
        std::stringstream want;
        want << in.rdbuf();
        CHECK(!want.str().empty());
        if (want.str() != text) {
            std::printf("checkpoint records differ from the 0-worker run (%s)\n", golden.c_str());
            ++g_fail;
        }
    }
    std::error_code ec;
    remove_all_long(d->root, ec);
    remove_all_long(dr->root, ec);
    if (g_fail) {
        std::printf("%d check(s) FAILED\n", g_fail);
        return 1;
    }
    std::printf("all save-and-continue checks passed\n");
    return 0;
}
