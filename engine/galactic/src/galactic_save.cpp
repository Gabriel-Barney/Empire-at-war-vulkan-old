// N1 save / load of the galactic simulation (contract: alamo/core/save_file.hpp): GalacticSim ("galactic.state")
// and ConflictManager ("galactic.conflicts").
//
// GalacticSim layout (little-endian, version kGalacticSaveVersion):
//   u32 version
//   definition fingerprint (checked, not restored): u32 planet rows, u32 routes, u32 type count, u32 player count
//   mode: i32 frame, pause, last_cycle_frame, next_cycle_frame, cycle_counter; bool linked_battle_pending;
//         i32 neutral_player, local_player; bool auto_reveal, reveal_sfx, initial_build_locks (cu15: a constant true
//         placeholder, ignored on read; the locks are always on), emit_events, slot_choices
//   players (player order): PlayerState field by field + f32 last_income
//   u32 relation count (0 or P x P) + u8 each
//   planets (row order): bool is_planet + u32 type (checked against the session), then every other PlanetState field
//         except the definition-derived position and links (visibility lists, income breakdown, both build queues,
//         starbase / structures / surface handles, influence ids, orbit fleet indices, respawn list, the persisted
//         tactical built objects {u32 type, f32 x, y, z, i32 base level, i32 owner} in list order, i32 x 2 build
//         queue phases)
//   u32 fleet count + every FleetState (dead ones too: fleet indices are stable references) incl. path and slots
//   u32 + i32 stealthed ids; u32 + victory claims; i32 victory hold; u32 + pending S4 events (GalacticEvent,
//   emission order); u32 + i32 ids with Prefer_Ground_Over_Space false (v5); u32 + {i32 player, u8 type} deferred
//   victory tests (v6); u32 + {i32 id, str name} fleet / ship names, id-ascending, live objects only (v7, [port])
// Object references: a handle whose object exists (live or dead-not-flushed) is written as its ObjectId and resolved
// with ObjectStore::find on read (the store is restored first; an unknown id fails the read). A handle that refers to
// no object any more (stale, or null) is written as kNoObjectId + its raw value, which read keeps as the same stale
// handle when the restored store does not have that slot live (else null), so stale-handle behaviour is unchanged.
//
// Rebuilt on read, not saved: fleet_by_slot_ / planet_by_slot_ (from the restored handles, fleet / row order as at
// creation), derived_ (per-tick scratch, written before it is read), last_spawn_ (set before every read), the path
// scratch. Definitions (map, graph, planet positions and route links, rules) come from start() with the same setup.
// Not saved (the host's): the income / production hooks and their ids, the conflict hook pointer, the ParallelFor.
//
// ConflictManager layout (version kConflictSaveVersion): u32 planet rows (checked); both queues; the state machine
// (state, active kind, tick, timer start, force, retreating, pause held, choice); the auto-resolve in flight (input
// combatants - piece names are rebuilt from the type index -, sides, cargo, and the parallel object lists); the last
// auto-resolve result; the active BattleRequest; the per-planet random maps; the conflict log. Not saved: the config
// and the host callbacks (decide, start_battle, pause, pick_map, on_survival_roll, on_space_decide), re-attached by the host.
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "alamo/galactic/campaign.hpp"
#include "alamo/galactic/conflicts.hpp"
#include "galactic_records.hpp"

namespace alamo::galactic {

namespace {

// v2 (story t11): + i32 victory hold; v3 (q6, AI_GALACTIC_TOKENS §9.4): + PlanetState::built_objects; v4 (A2k t04 /
// t01): + i32 x 2 build queue phases per planet, - the two PlanetState build bonus floats (the production modifiers are
// derived, GalacticSim::production_bonus); v5 (cu8f t04b): + the Prefer_Ground_Over_Space false ids; v6 (q6 t21): + the
// deferred victory tests; v7 (2026-11-13, [port: Gabriel]): + the fleet / ship names. Only the current version is read,
// and v6 (no names: read as none), so saves from before the names still load. v8 (galab): + per player the home planet,
// per planet the siphon source type / object and the bonus holder, then the neutralized counts {player, type, count};
// v6 / v7 read them as the session's start() values (home) and none. v9 (map1 t06): + the structure marker memory
// {i32 object, i32 slot, i32 marker}, object-id-ascending, live objects only; v6 - v8 read it as none. v10 (mcx4 t01):
// + per planet the initial-placement list {u32 count, u32 type...} (Special_Case_Production), then the campaign's
// auto-resolve rules {bool disabled, bool story campaign, u32 count, {u32 planet type, bool land}...}; v6 - v9 keep the
// session's start() values. v11 (mcx4 t20): + last, the fleets holding the sticky story auto-resolve lock {u32 count,
// u32 fleet index...} ascending; v10 and older derive it from each fleet's current contents.
constexpr uint32_t kGalacticSaveVersion = 12;  // 12 (mcx4 t22): + the company survivor records after the locks
constexpr uint32_t kGalacticSaveVersionNoSurvivors = 11;
constexpr uint32_t kGalacticSaveVersionNoLocks = 10;
constexpr uint32_t kGalacticSaveVersionNoPlacement = 9;
// limit: guard not a limit: save format version 8 (before the marker slots)
constexpr uint32_t kGalacticSaveVersionNoSlots = 8;
constexpr uint32_t kGalacticSaveVersionNoGalab = 7;
constexpr uint32_t kGalacticSaveVersionNoNames = 6;
constexpr uint32_t kConflictSaveVersion = 4;  // v2: BattleRequest::linked (story t10); v3: BattleRequest::sandbox (q6); v4: ArCombatant::strength_bonus (mcx4 t06)

using records::get_handle;
using records::put_handle;

void put_handles(ByteWriter& w, const objects::ObjectStore& s, const std::vector<ObjectHandle>& v) {
    w.u32(static_cast<uint32_t>(v.size()));
    for (ObjectHandle h : v) put_handle(w, s, h);
}

bool get_handles(ByteReader& r, const objects::ObjectStore& s, std::vector<ObjectHandle>& v) {
    uint32_t n = 0;
    if (!r.count(n, 4)) return false;
    v.assign(n, ObjectHandle{});
    for (ObjectHandle& h : v)
        if (!get_handle(r, s, h)) return false;
    return true;
}

void put_u32s(ByteWriter& w, const std::vector<uint32_t>& v) {
    w.u32(static_cast<uint32_t>(v.size()));
    for (uint32_t x : v) w.u32(x);
}

bool get_u32s(ByteReader& r, std::vector<uint32_t>& v) {
    uint32_t n = 0;
    if (!r.count(n, 4)) return false;
    v.assign(n, 0u);
    for (uint32_t& x : v) x = r.u32();
    return r.ok();
}

bool get_str(ByteReader& r, std::string& s) {
    s = r.str();
    return r.ok();
}

}  // namespace

// ---- GalacticSim --------------------------------------------------------------------------------------------------------

void GalacticSim::write(ByteWriter& w) const {
    const objects::ObjectStore& st = *store_;
    w.u32(kGalacticSaveVersion);
    w.u32(static_cast<uint32_t>(planets_.size()));
    w.u32(static_cast<uint32_t>(map_.routes.size()));
    w.u32(static_cast<uint32_t>(rules_->types.size()));
    w.u32(static_cast<uint32_t>(players_.size()));

    w.i32(frame_).i32(pause_).i32(last_cycle_frame_).i32(next_cycle_frame_).i32(cycle_counter_);
    w.boolean(linked_battle_pending_);
    w.i32(neutral_player_).i32(local_player_);
    w.boolean(auto_reveal_).boolean(reveal_sfx_).boolean(true).boolean(emit_events_).boolean(slot_choices_);

    records::Writer rec(w, st);  // REF4 t02: the records' fields from one list (galactic_records.hpp)
    for (size_t i = 0; i < players_.size(); ++i) {
        records::visit(rec, players_[i]);
        w.f32(i < last_income_.size() ? last_income_[i] : 0.0f);
    }
    w.u32(static_cast<uint32_t>(relations_.size()));
    for (uint8_t rel : relations_) w.u8(rel);

    for (const PlanetState& p : planets_) records::visit(rec, p);

    w.u32(static_cast<uint32_t>(fleets_.size()));
    for (const FleetState& f : fleets_) records::visit(rec, f);

    w.u32(static_cast<uint32_t>(stealthed_.size()));
    for (int32_t id : stealthed_) w.i32(id);
    w.u32(static_cast<uint32_t>(claims_.size()));
    for (const VictoryClaim& c : claims_) w.i32(c.player).u8(c.type).i32(c.frame).i32(c.countdown);
    w.i32(victory_hold_);
    w.u32(static_cast<uint32_t>(events_.size()));
    for (const GalacticEvent& e : events_) {
        w.i32(e.frame).u8(static_cast<uint8_t>(e.kind)).i32(e.player).i32(e.other_player);
        w.u32(e.planet).u32(e.other_planet).u32(e.fleet).u32(e.type).i32(e.object).i32(e.value);
    }
    w.u32(static_cast<uint32_t>(ground_shy_.size()));  // cu8f t04b (v5)
    for (int32_t id : ground_shy_) w.i32(id);
    w.u32(static_cast<uint32_t>(victory_deferred_.size()));  // q6 t21 (v6)
    for (const auto& e : victory_deferred_) w.i32(e.first).u8(e.second);
    uint32_t live_names = 0;  // v7: names of live objects only (a dead object's name is dropped)
    for (const auto& e : names_) live_names += st.alive(st.find(e.first)) ? 1u : 0u;
    w.u32(live_names);
    for (const auto& e : names_)
        if (st.alive(st.find(e.first))) w.i32(e.first).str(e.second);
    for (const PlayerState& p : players_) w.u32(p.home);  // v8 (galab)
    for (const PlanetState& p : planets_) w.u32(p.siphon_type).i32(p.siphon_source).i32(p.bonus_holder);
    w.u32(static_cast<uint32_t>(neutralized_.size()));  // GA §7 neutralized counts, (player, type)-ascending
    for (const NeutralizedEntry& e : neutralized_) w.i32(e.player).u32(e.type).i32(e.count);
    uint32_t live_slots = 0;  // map1 t06 (v9): structure marker memory of live objects only
    for (const StructureSlot& e : structure_slots_) live_slots += st.alive(st.find(e.object)) ? 1u : 0u;
    w.u32(live_slots);
    for (const StructureSlot& e : structure_slots_)
        if (st.alive(st.find(e.object))) w.i32(e.object).i32(e.slot).i32(e.marker);
    for (const PlanetState& p : planets_) {  // mcx4 t01 (v10)
        w.u32(static_cast<uint32_t>(p.initial_placement.size()));
        for (uint32_t t : p.initial_placement) w.u32(t);
    }
    w.boolean(autoresolve_disabled_).boolean(story_campaign_);  // mcx4 t07 (v10)
    w.u32(static_cast<uint32_t>(autoresolve_exclusions_.size()));
    for (const auto& [t, land] : autoresolve_exclusions_) w.u32(t).boolean(land);
    uint32_t locked = 0;  // mcx4 t20 (v11)
    for (const FleetState& f : fleets_) locked += f.story_autoresolve_lock ? 1u : 0u;
    w.u32(locked);
    for (uint32_t i = 0; i < fleets_.size(); ++i)
        if (fleets_[i].story_autoresolve_lock) w.u32(i);
    w.u32(static_cast<uint32_t>(company_survivors_.size()));  // mcx4 t22 (v12), ascending ObjectId
    for (const auto& [id, members] : company_survivors_) {
        w.i32(id).u32(static_cast<uint32_t>(members.size()));
        for (const uint32_t m : members) w.u32(m);
    }
}

bool GalacticSim::read(ByteReader& r) {
    const objects::ObjectStore& st = *store_;
    const uint32_t version = r.u32();
    if ((version != kGalacticSaveVersion && version != kGalacticSaveVersionNoSurvivors && version != kGalacticSaveVersionNoLocks && version != kGalacticSaveVersionNoPlacement && version != kGalacticSaveVersionNoSlots && version != kGalacticSaveVersionNoGalab &&
         version != kGalacticSaveVersionNoNames) ||
        !r.ok())
        return false;
    const uint32_t rows = r.u32(), routes = r.u32(), types = r.u32(), np = r.u32();
    if (!r.ok() || rows != planets_.size() || routes != map_.routes.size() || types != rules_->types.size() ||
        np != players_.size())
        return false;
    const int32_t P = static_cast<int32_t>(np);
    const auto player_ok = [P](int32_t p) { return p >= kNoPlayer && p < P; };
    const auto planet_ok = [rows](PlanetIndex p) { return p == kInvalidPlanet || p < rows; };
    const auto types_ok = [types](const std::vector<uint32_t>& v) {
        for (uint32_t t : v)
            if (t >= types) return false;
        return true;
    };

    const int32_t frame = r.i32(), pause = r.i32(), last_cycle = r.i32(), next_cycle = r.i32(), cycle = r.i32();
    const bool linked = r.boolean();
    const int32_t neutral = r.i32(), local = r.i32();
    const bool auto_reveal = r.boolean(), reveal_sfx = r.boolean();
    r.boolean();  // initial_build_locks placeholder (cu15: always on)
    const bool emit = r.boolean(), slot_choices = r.boolean();
    if (!r.ok() || pause < 0 || !player_ok(neutral) || !player_ok(local)) return false;

    std::vector<PlayerState> players(np);
    std::vector<float> last_income(np, 0.0f);
    records::Reader rec(r, st);
    for (uint32_t i = 0; i < np; ++i) {
        PlayerState& p = players[i];
        records::visit(rec, p);
        last_income[i] = r.f32();
        if (!r.ok() || !types_ok(p.locked_types) || !types_ok(p.unlocked_types) || !types_ok(p.production_disabled)) return false;
        for (const auto& [type, count] : p.built)
            if (type >= types) return false;
    }
    uint32_t nrel = 0;
    if (!r.count(nrel, 1) || (nrel != 0 && nrel != np * np)) return false;
    std::vector<uint8_t> relations(nrel);
    for (uint8_t& rel : relations) {
        rel = r.u8();
        if (rel > static_cast<uint8_t>(Relation::Enemy)) return false;
    }

    std::vector<PlanetState> planets = planets_;  // keeps the definition-derived position / links of start()
    for (uint32_t i = 0; i < rows; ++i) {
        PlanetState& p = planets[i];
        records::visit(rec, p);  // is_planet and type are checked against the session's galaxy
        if (!r.ok()) return false;
        if (!player_ok(p.owner) || !player_ok(p.leaning) || !player_ok(p.corrupting_player) ||
            !player_ok(p.corruption_pending_player) || !player_ok(p.siphon_player) || !player_ok(p.starbase_owner) ||
            p.visibility.size() > np)
            return false;
        for (const IncomeEntry& e : p.income)
            if (e.def != 0xFFFFFFFFu && e.def >= rules_->special.abilities.size()) return false;
        for (const std::vector<BuildEntry>& q : p.queue)
            for (const BuildEntry& e : q)
                if (e.type >= types || e.player < 0 || e.player >= P) return false;
        for (const RespawnEntry& e : p.respawns)
            if (e.type >= types || !player_ok(e.owner)) return false;
        for (const PersistedBuiltObject& b : p.built_objects)
            if (b.type >= types || !player_ok(b.owner)) return false;
        for (const int32_t phase : p.queue_phase)
            if (p.is_planet ? phase < 1 || phase > econ::kFramesPerSecond : phase != 0) return false;
    }

    uint32_t nf = 0;
    if (!r.count(nf, 1)) return false;
    std::vector<FleetState> fleets(nf);
    for (FleetState& f : fleets) {
        records::visit(rec, f);
        if (!r.ok() || !player_ok(f.owner) || !planet_ok(f.planet) || !planet_ok(f.from) || !planet_ok(f.to) ||
            !planet_ok(f.want_planet) || f.path_index > f.path.size() || f.slot < -1 || f.slot >= kOrbitSlots ||
            f.want_slot < -1 || f.want_slot >= kOrbitSlots)
            return false;
        for (PlanetIndex hop : f.path)
            if (hop >= rows) return false;
        if (f.alive && f.owner == kNoPlayer) return false;
    }
    for (const PlanetState& p : planets)
        for (uint32_t fi : p.orbit)
            if (fi >= nf) return false;

    uint32_t ns = 0;
    if (!r.count(ns, 4)) return false;
    std::vector<int32_t> stealthed(ns);
    for (int32_t& id : stealthed) id = r.i32();
    uint32_t ncl = 0;
    if (!r.count(ncl, 13)) return false;
    std::vector<VictoryClaim> claims(ncl);
    for (VictoryClaim& c : claims) {
        c.player = r.i32();
        c.type = r.u8();
        c.frame = r.i32();
        c.countdown = r.i32();
        if (!r.ok() || !player_ok(c.player)) return false;
    }
    const int32_t hold = r.i32();
    if (hold < 0) return false;
    uint32_t ne = 0;
    if (!r.count(ne, 37)) return false;
    std::vector<GalacticEvent> events(ne);
    for (GalacticEvent& e : events) {
        e.frame = r.i32();
        const uint8_t kind = r.u8();
        if (kind >= static_cast<uint8_t>(GalacticEventKind::Count)) return false;
        e.kind = static_cast<GalacticEventKind>(kind);
        e.player = r.i32();
        e.other_player = r.i32();
        e.planet = r.u32();
        e.other_planet = r.u32();
        e.fleet = r.u32();
        e.type = r.u32();
        e.object = r.i32();
        e.value = r.i32();
    }
    std::vector<int32_t> ground_shy;  // cu8f t04b (v5)
    uint32_t ng = 0;
    if (!r.count(ng, 4)) return false;
    ground_shy.resize(ng);
    for (int32_t& id : ground_shy) id = r.i32();
    for (size_t i = 1; i < ground_shy.size(); ++i)
        if (ground_shy[i] <= ground_shy[i - 1]) return false;  // ascending, unique
    std::vector<std::pair<int32_t, uint8_t>> deferred;  // q6 t21 (v6)
    {
        uint32_t nd = 0;
        if (!r.count(nd, 5)) return false;
        deferred.resize(nd);
        for (auto& e : deferred) {
            e.first = r.i32();
            e.second = r.u8();
            if (!r.ok() || !player_ok(e.first)) return false;
        }
    }
    std::vector<std::pair<int32_t, std::string>> names;  // v7 ([port] fleet / ship names)
    if (version >= 7) {
        uint32_t nn = 0;
        if (!r.count(nn, 8)) return false;
        names.resize(nn);
        for (size_t i = 0; i < names.size(); ++i) {
            names[i].first = r.i32();
            names[i].second = r.str();
            std::string normalized;
            if (!r.ok() || !st.alive(st.find(names[i].first))) return false;  // a live object of the restored store
            if (!commands::normalize_object_name(names[i].second, normalized) || normalized != names[i].second ||
                normalized.empty())
                return false;
            if (i > 0 && names[i].first <= names[i - 1].first) return false;  // ascending, unique
        }
    }
    std::vector<NeutralizedEntry> neutralized;
    for (size_t i = 0; i < players.size(); ++i)  // galab (v8); older saves: the session's start() home, no state
        players[i].home = i < players_.size() ? players_[i].home : kInvalidPlanet;
    for (PlanetState& p : planets) {
        p.siphon_type = kNoIndex;
        p.siphon_source = 0;
        p.bonus_holder = kNoPlayer;
    }
    if (version >= 8) {
        for (PlayerState& p : players) {
            p.home = r.u32();
            if (!r.ok() || (p.home != kInvalidPlanet && p.home >= rows)) return false;
        }
        for (PlanetState& p : planets) {
            p.siphon_type = r.u32();
            p.siphon_source = r.i32();
            p.bonus_holder = r.i32();
            if (!r.ok() || (p.siphon_type != kNoIndex && p.siphon_type >= types) || !player_ok(p.bonus_holder)) return false;
        }
        const uint32_t nn = r.u32();
        if (!r.ok() || nn > types * np) return false;
        neutralized.resize(nn);
        for (uint32_t i = 0; i < nn; ++i) {
            NeutralizedEntry& e = neutralized[i];
            e.player = r.i32();
            e.type = r.u32();
            e.count = r.i32();
            if (!r.ok() || !valid_player(e.player) || e.type >= types || e.count < 1) return false;
            if (i > 0 && (neutralized[i - 1].player > e.player ||
                          (neutralized[i - 1].player == e.player && neutralized[i - 1].type >= e.type)))
                return false;  // ascending, unique
        }
    }
    std::vector<StructureSlot> slots;  // map1 t06 (v9)
    if (version >= 9) {
        uint32_t nslots = 0;
        if (!r.count(nslots, 12)) return false;
        slots.resize(nslots);
        for (size_t i = 0; i < slots.size(); ++i) {
            slots[i].object = r.i32();
            slots[i].slot = r.i32();
            slots[i].marker = r.i32();
            if (!r.ok() || !st.alive(st.find(slots[i].object))) return false;  // a live object of the restored store
            if (i > 0 && slots[i].object <= slots[i - 1].object) return false;  // ascending, unique
        }
    }
    if (version >= 10) {  // mcx4 t01; older saves keep the session's start() lists (planets is a copy of planets_)
        for (PlanetState& p : planets) {
            uint32_t nt = 0;
            if (!r.count(nt, 4)) return false;
            p.initial_placement.resize(nt);
            for (uint32_t& t : p.initial_placement) {
                t = r.u32();
                if (!r.ok() || t >= types) return false;
            }
        }
    }
    bool ar_disabled = autoresolve_disabled_, ar_story = story_campaign_;  // mcx4 t07; older saves: start()'s values
    std::vector<std::pair<uint32_t, bool>> ar_excl = autoresolve_exclusions_;
    if (version >= 10) {
        ar_disabled = r.boolean();
        ar_story = r.boolean();
        uint32_t n_excl = 0;
        if (!r.count(n_excl, 5)) return false;
        ar_excl.resize(n_excl);
        for (auto& [t, land] : ar_excl) {
            t = r.u32();
            land = r.boolean();
            if (!r.ok() || t >= types) return false;
        }
    }
    if (version >= 11) {  // mcx4 t20: the locked fleets, ascending
        uint32_t n_locked = 0;
        if (!r.count(n_locked, 4)) return false;
        uint32_t prev = 0;
        for (uint32_t k = 0; k < n_locked; ++k) {
            const uint32_t i = r.u32();
            if (!r.ok() || i >= fleets.size() || (k > 0 && i <= prev)) return false;
            fleets[i].story_autoresolve_lock = true;
            prev = i;
        }
    } else {  // v10 and older: each fleet's current contents
        for (FleetState& f : fleets)
            for (const ObjectHandle m : f.contents) {
                const uint32_t t = st.exists(m) ? static_cast<uint32_t>(st.type(m)) : kNoIndex;
                if (t < rules_->types.size() && rules_->types[t].prevents_story_autoresolve) f.story_autoresolve_lock = true;
            }
    }
    std::vector<std::pair<int32_t, std::vector<uint32_t>>> survivors;  // mcx4 t22 (v12)
    if (version >= 12) {
        uint32_t n_surv = 0;
        if (!r.count(n_surv, 8)) return false;
        survivors.resize(n_surv);
        for (uint32_t k = 0; k < n_surv; ++k) {
            survivors[k].first = r.i32();
            uint32_t n_m = 0;
            if (!r.count(n_m, 4) || (k > 0 && survivors[k].first <= survivors[k - 1].first)) return false;
            survivors[k].second.resize(n_m);
            for (uint32_t& m : survivors[k].second) {
                m = r.u32();
                if (!r.ok() || m >= rules_->types.size()) return false;
            }
        }
    }
    if (!r.ok() || !r.done()) return false;

    // ---- commit ----
    frame_ = frame;
    pause_ = pause;
    last_cycle_frame_ = last_cycle;
    next_cycle_frame_ = next_cycle;
    cycle_counter_ = cycle;
    linked_battle_pending_ = linked;
    neutral_player_ = neutral;
    local_player_ = local;
    auto_reveal_ = auto_reveal;
    autoresolve_disabled_ = ar_disabled;
    story_campaign_ = ar_story;
    autoresolve_exclusions_ = std::move(ar_excl);
    reveal_sfx_ = reveal_sfx;
    emit_events_ = emit;
    slot_choices_ = slot_choices;
    players_ = std::move(players);
    last_income_ = std::move(last_income);
    relations_ = std::move(relations);
    planets_ = std::move(planets);
    fleets_ = std::move(fleets);
    company_survivors_ = std::move(survivors);
    stealthed_ = std::move(stealthed);
    ground_shy_ = std::move(ground_shy);
    claims_ = std::move(claims);
    victory_hold_ = hold;
    victory_deferred_ = std::move(deferred);
    names_ = std::move(names);
    neutralized_ = std::move(neutralized);
    structure_slots_ = std::move(slots);
    neutralize_offer_.clear();
    events_ = std::move(events);
    // Rebuilt caches: store slot -> fleet / planet (creation order, so a reused slot maps to the newest fleet as
    // create_fleet left it), the per-tick owner derivation scratch, the spawn payload latch.
    fleet_by_slot_.clear();
    for (uint32_t i = 0; i < fleets_.size(); ++i) {
        const ObjectHandle h = fleets_[i].object;
        if (!h) continue;
        if (fleet_by_slot_.size() <= h.slot()) fleet_by_slot_.resize(static_cast<size_t>(h.slot()) + 1, kNoFleet);
        fleet_by_slot_[h.slot()] = i;
    }
    planet_by_slot_.clear();
    for (PlanetIndex i = 0; i < planets_.size(); ++i) {
        const ObjectHandle h = planets_[i].object;
        if (!planets_[i].is_planet || !h) continue;
        if (planet_by_slot_.size() <= h.slot()) planet_by_slot_.resize(static_cast<size_t>(h.slot()) + 1, kInvalidPlanet);
        planet_by_slot_[h.slot()] = i;
    }
    derived_.assign(planets_.size(), Derived{});
    last_spawn_ = {};
    return true;
}

// ---- ConflictManager ----------------------------------------------------------------------------------------------------

namespace {

bool valid_kind(ConflictKind k) { return k == ConflictKind::Land || k == ConflictKind::Space; }
bool get_info(ByteReader& r, const objects::ObjectStore& st, ConflictInfo& c) {
    records::Reader rec(r, st);
    records::visit(rec, c);
    return r.ok() && valid_kind(c.kind);
}

void put_f64s(ByteWriter& w, const std::vector<double>& v) {
    w.u32(static_cast<uint32_t>(v.size()));
    for (double x : v) w.f64(x);
}

bool get_f64s(ByteReader& r, std::vector<double>& v) {
    uint32_t n = 0;
    if (!r.count(n, 8)) return false;
    v.assign(n, 0.0);
    for (double& x : v) x = r.f64();
    return r.ok();
}

void put_u8s(ByteWriter& w, const std::vector<uint8_t>& v) {
    w.u32(static_cast<uint32_t>(v.size()));
    for (uint8_t x : v) w.u8(x);
}

bool get_u8s(ByteReader& r, std::vector<uint8_t>& v) {
    uint32_t n = 0;
    if (!r.count(n, 1)) return false;
    v.assign(n, 0);
    for (uint8_t& x : v) x = r.u8();
    return r.ok();
}

bool valid_state(int8_t s) {
    switch (static_cast<ConflictState>(s)) {
        case ConflictState::Idle:
        case ConflictState::Vote:
        case ConflictState::StartBattle:
        case ConflictState::Decide:
        case ConflictState::Dialog:
        case ConflictState::AutoSetup:
        case ConflictState::AutoRunning:
        case ConflictState::InBattle: return true;
    }
    return false;
}

}  // namespace

void ConflictManager::write(ByteWriter& w) const {
    const objects::ObjectStore& st = sim_->store();
    records::Writer rec(w, st);  // REF4 t02: ConflictInfo / ConflictLogEntry from one list (galactic_records.hpp)
    w.u32(kConflictSaveVersion);
    w.u32(static_cast<uint32_t>(sim_->planets().size()));
    for (const std::vector<ConflictInfo>* q : {&space_q_, &land_q_}) {
        w.u32(static_cast<uint32_t>(q->size()));
        for (const ConflictInfo& c : *q) records::visit(rec, c);
    }
    w.u8(static_cast<uint8_t>(state_)).u8(static_cast<uint8_t>(active_)).u64(tick_).u64(timer_start_);
    w.boolean(force_).i32(retreating_).i32(pause_held_).boolean(has_choice_).u8(static_cast<uint8_t>(choice_));

    // Auto-resolve in flight.
    w.boolean(ar_in_.space).i32(ar_in_.attacker).i32(ar_in_.retreating).boolean(ar_in_.from_tactical);
    w.u32(static_cast<uint32_t>(ar_in_.combatants.size()));
    for (const ArCombatant& c : ar_in_.combatants) {
        w.i32(c.owner);
        w.u32(static_cast<uint32_t>(c.pieces.size()));
        for (const ArPiece& p : c.pieces) w.u32(p.type).u64(p.categories).f32(p.power);
        w.f32(c.own_power).boolean(c.hero).boolean(c.always_dies).boolean(c.super).boolean(c.super_killer);
        w.u32(static_cast<uint32_t>(c.starbase.size()));
        for (const ArStarbaseLevel& l : c.starbase) w.u32(l.type).f32(l.power);
        w.u32(static_cast<uint32_t>(c.strength_bonus.size()));  // v4
        for (const auto& [bit, factor] : c.strength_bonus) w.u8(bit).f32(factor);
    }
    w.u32(static_cast<uint32_t>(ar_in_.players.size()));
    for (const ArSideInfo& s : ar_in_.players) w.i32(s.player).boolean(s.ai).boolean(s.playable);
    w.u32(static_cast<uint32_t>(ar_in_.cargo.size()));
    for (const AutoResolveInput::Cargo& c : ar_in_.cargo) w.i32(c.owner).boolean(c.hero);
    put_handles(w, st, ar_objects_);
    put_handles(w, st, ar_cargo_);

    // Last result.
    const AutoResolveResult& a = last_ar_;
    w.boolean(a.valid).boolean(a.degenerate).i32(a.winner).i32(a.loser).i32(a.retreating);
    put_u8s(w, a.survives);
    put_u32s(w, a.starbase_type);
    put_u8s(w, a.cargo_survives);
    for (int s = 0; s < 2; ++s) put_f64s(w, a.vec[s]);
    for (int s = 0; s < 2; ++s) put_f64s(w, a.rem[s]);
    for (int s = 0; s < 2; ++s) w.f64(a.strength[s]).f64(a.budget[s]).i32(a.side_player[s]).u32(a.losses[s]);
    w.u32(a.draws);

    // Active battle request.
    records::visit(rec, request_.info);
    w.str(request_.map).i32(request_.base_level);
    w.u32(static_cast<uint32_t>(request_.forces.size()));
    for (const BattleForces& f : request_.forces) {
        w.i32(f.player);
        put_handles(w, st, f.start);
        put_handles(w, st, f.reserve);
    }
    put_handle(w, st, request_.starbase);
    put_handles(w, st, request_.structures);
    w.boolean(request_.land_base_destruction_forces_retreat).boolean(request_.space_station_destruction_forces_retreat);
    w.boolean(request_.linked);
    w.boolean(request_.sandbox);  // v3

    w.u32(static_cast<uint32_t>(planet_map_.size()));
    for (const std::string& m : planet_map_) w.str(m);
    w.u32(static_cast<uint32_t>(log_.size()));
    for (const ConflictLogEntry& e : log_) records::visit(rec, e);
}

bool ConflictManager::read(ByteReader& r) {
    const objects::ObjectStore& st = sim_->store();
    const uint32_t version = r.u32();
    if (version < 1 || version > kConflictSaveVersion || !r.ok()) return false;
    const uint32_t rows = r.u32();
    if (!r.ok() || rows != sim_->planets().size()) return false;
    const int32_t P = static_cast<int32_t>(sim_->players().size());
    const auto player_ok = [P](int32_t p) { return p >= kNoPlayer && p < P; };

    std::vector<ConflictInfo> qs[2];
    for (std::vector<ConflictInfo>& q : qs) {
        uint32_t n = 0;
        if (!r.count(n, 21)) return false;
        q.resize(n);
        for (ConflictInfo& c : q)
            if (!get_info(r, st, c) || c.planet >= rows || !player_ok(c.attacker) || !player_ok(c.defender)) return false;
    }
    const int8_t state = static_cast<int8_t>(r.u8());
    const uint8_t active = r.u8();
    const uint64_t tick = r.u64(), timer_start = r.u64();
    const bool force = r.boolean();
    const int32_t retreating = r.i32(), pause_held = r.i32();
    const bool has_choice = r.boolean();
    const uint8_t choice = r.u8();
    if (!r.ok() || !valid_state(state) ||
        (active != static_cast<uint8_t>(ConflictKind::Land) && active != static_cast<uint8_t>(ConflictKind::Space)) ||
        !player_ok(retreating) || pause_held < 0 || choice > static_cast<uint8_t>(BattleChoice::Retreat))
        return false;

    AutoResolveInput in;
    in.space = r.boolean();
    in.attacker = r.i32();
    in.retreating = r.i32();
    in.from_tactical = r.boolean();
    uint32_t nc = 0;
    if (!r.ok() || !r.count(nc, 17)) return false;
    in.combatants.resize(nc);
    const auto& types = sim_->rules().types;
    for (ArCombatant& c : in.combatants) {
        c.owner = r.i32();
        uint32_t np = 0;
        if (!r.count(np, 16)) return false;
        c.pieces.resize(np);
        for (ArPiece& p : c.pieces) {
            p.type = r.u32();
            p.categories = r.u64();
            p.power = r.f32();
            p.name = p.type < types.size() ? std::string_view(types[p.type].name) : std::string_view();  // as combatant_of
        }
        c.own_power = r.f32();
        c.hero = r.boolean();
        c.always_dies = r.boolean();
        c.super = r.boolean();
        c.super_killer = r.boolean();
        uint32_t nsb = 0;
        if (!r.count(nsb, 8)) return false;
        c.starbase.resize(nsb);
        for (ArStarbaseLevel& l : c.starbase) {
            l.type = r.u32();
            l.power = r.f32();
        }
        if (version >= 4) {  // older saves: no pairs (base FoC sets no Unit_Strength_Factor)
            uint32_t nb = 0;
            if (!r.count(nb, 5)) return false;
            c.strength_bonus.resize(nb);
            for (auto& [bit, factor] : c.strength_bonus) {
                bit = r.u8();
                factor = r.f32();
            }
        }
        if (!r.ok()) return false;
    }
    uint32_t nsides = 0;
    if (!r.count(nsides, 6)) return false;
    in.players.resize(nsides);
    for (ArSideInfo& s : in.players) {
        s.player = r.i32();
        s.ai = r.boolean();
        s.playable = r.boolean();
    }
    uint32_t ncargo = 0;
    if (!r.count(ncargo, 5)) return false;
    in.cargo.resize(ncargo);
    for (AutoResolveInput::Cargo& c : in.cargo) {
        c.owner = r.i32();
        c.hero = r.boolean();
    }
    std::vector<ObjectHandle> ar_objects, ar_cargo;
    if (!r.ok() || !get_handles(r, st, ar_objects) || !get_handles(r, st, ar_cargo)) return false;
    // auto_finish walks the object lists in parallel with the combatant / cargo lists.
    if (ar_objects.size() != in.combatants.size() || ar_cargo.size() != in.cargo.size()) return false;

    AutoResolveResult a;
    a.valid = r.boolean();
    a.degenerate = r.boolean();
    a.winner = r.i32();
    a.loser = r.i32();
    a.retreating = r.i32();
    if (!r.ok() || !get_u8s(r, a.survives) || !get_u32s(r, a.starbase_type) || !get_u8s(r, a.cargo_survives)) return false;
    for (int s = 0; s < 2; ++s)
        if (!get_f64s(r, a.vec[s])) return false;
    for (int s = 0; s < 2; ++s)
        if (!get_f64s(r, a.rem[s])) return false;
    for (int s = 0; s < 2; ++s) {
        a.strength[s] = r.f64();
        a.budget[s] = r.f64();
        a.side_player[s] = r.i32();
        a.losses[s] = r.u32();
    }
    a.draws = r.u32();
    if (!r.ok()) return false;

    BattleRequest req;
    if (!get_info(r, st, req.info) || !get_str(r, req.map)) return false;
    req.base_level = r.i32();
    uint32_t nforces = 0;
    if (!r.ok() || !r.count(nforces, 12)) return false;
    req.forces.resize(nforces);
    for (BattleForces& f : req.forces) {
        f.player = r.i32();
        if (!r.ok() || !get_handles(r, st, f.start) || !get_handles(r, st, f.reserve)) return false;
    }
    if (!get_handle(r, st, req.starbase) || !get_handles(r, st, req.structures)) return false;
    req.land_base_destruction_forces_retreat = r.boolean();
    req.space_station_destruction_forces_retreat = r.boolean();
    if (version >= 2) req.linked = r.boolean();
    if (version >= 3) req.sandbox = r.boolean();

    uint32_t nmaps = 0;
    if (!r.ok() || !r.count(nmaps, 4)) return false;
    std::vector<std::string> maps(nmaps);
    for (std::string& m : maps)
        if (!get_str(r, m)) return false;
    uint32_t nlog = 0;
    if (!r.count(nlog, 54)) return false;
    std::vector<ConflictLogEntry> log(nlog);
    records::Reader rec(r, st);
    for (ConflictLogEntry& e : log) {
        records::visit(rec, e);
        if (!r.ok() || !valid_kind(e.kind)) return false;
    }
    if (!r.ok() || !r.done()) return false;

    // ---- commit ----
    space_q_ = std::move(qs[0]);
    land_q_ = std::move(qs[1]);
    state_ = static_cast<ConflictState>(state);
    active_ = static_cast<ConflictKind>(active);
    tick_ = tick;
    timer_start_ = timer_start;
    force_ = force;
    retreating_ = retreating;
    pause_held_ = pause_held;
    has_choice_ = has_choice;
    choice_ = static_cast<BattleChoice>(choice);
    ar_in_ = std::move(in);
    ar_objects_ = std::move(ar_objects);
    ar_cargo_ = std::move(ar_cargo);
    last_ar_ = std::move(a);
    request_ = std::move(req);
    planet_map_ = std::move(maps);
    log_ = std::move(log);
    return true;
}

}  // namespace alamo::galactic
