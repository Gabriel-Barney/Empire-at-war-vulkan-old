// REF4 t02: the save ledger of GalacticSim. Every data member is listed once:
//   SAVED(member, hashed)    written by write(), restored by read() and walked by hash_state or one of its hash_*
//                            sections;
//   SAVED(member, unhashed)  written and restored, outside the CRC;
//   REBUILT(member, "why")   set again by start() / the host with the same setup before read();
//   DERIVED(member, "why")   recomputed from saved state, scratch, or presentation records;
//   CHECKED(member, "why")   not restored, but write() / read() read it (a fingerprint read() checks, a lookup).
// tools/lint/check_battle_ledger.py (build_agent.cmd, ctest alamo_sim_battle_ledger_lint) checks it against
// campaign.hpp and the bodies of write / read / hash_* / register_crc in engine/galactic/src. The fields of the records
// (PlayerState, PlanetState, FleetState) are declared once in galactic_records.hpp (one visit per record for the save
// writer, the reader and the CRC).
// Order: the declaration order in campaign.hpp. Read by the lint only (not compiled).
#pragma once

// clang-format off
#define ALAMO_GALACTIC_LEDGER(SAVED, REBUILT, DERIVED, CHECKED) \
    CHECKED(rules_, "start(): write() stores the type count, read() refuses another") \
    CHECKED(store_, "the session's object store (restored first): handles are saved as object ids") \
    REBUILT(rng_, "the session's random stream (saved by the session)") \
    REBUILT(pf_, "the host's ParallelFor") \
    CHECKED(map_, "start(): write() stores the route count, read() refuses another") \
    REBUILT(graph_, "start(): the planet graph of the map") \
    SAVED(players_, hashed) \
    SAVED(relations_, hashed) \
    SAVED(planets_, hashed) \
    SAVED(fleets_, hashed) \
    DERIVED(fleet_by_slot_, "from the restored fleet handles (read())") \
    SAVED(slot_choices_, unhashed) \
    DERIVED(planet_by_slot_, "from the restored planet handles (read())") \
    SAVED(neutral_player_, unhashed) \
    SAVED(frame_, hashed) \
    SAVED(pause_, hashed) \
    SAVED(linked_battle_pending_, hashed) \
    SAVED(last_cycle_frame_, hashed) \
    SAVED(next_cycle_frame_, hashed) \
    SAVED(cycle_counter_, hashed) \
    DERIVED(scratch_, "path search scratch") \
    DERIVED(derived_, "per-tick derivation output, written before it is read") \
    SAVED(emit_events_, unhashed) \
    DERIVED(destroy_auto_resolve_, "mcx4 Q4: true only inside destroy_object_auto_resolve") \
    SAVED(company_survivors_, hashed) \
    REBUILT(scoring_hooks_, "host callbacks") \
    DERIVED(last_spawn_, "set before every read (event payloads only)") \
    SAVED(events_, unhashed) \
    REBUILT(conflicts_, "the host's ConflictHooks (not owned)") \
    REBUILT(income_hooks_, "listeners, registered again by the hosts") \
    REBUILT(credit_hooks_, "listeners, registered again by the hosts") \
    REBUILT(next_hook_id_, "listener ids, handed out again as the hosts register") \
    SAVED(last_income_, unhashed) \
    REBUILT(production_hook_, "host callback") \
    REBUILT(spawn_hook_, "host callback") \
    REBUILT(installment_hook_, "host callback") \
    SAVED(stealthed_, hashed) \
    SAVED(ground_shy_, hashed) \
    SAVED(claims_, hashed) \
    SAVED(victory_hold_, hashed) \
    REBUILT(story_gates_, "STY1: set by the story session, which saves them") \
    SAVED(autoresolve_disabled_, unhashed) \
    SAVED(story_campaign_, unhashed) \
    SAVED(autoresolve_exclusions_, unhashed) \
    SAVED(victory_deferred_, hashed) \
    SAVED(names_, unhashed) \
    SAVED(structure_slots_, hashed) \
    SAVED(neutralized_, hashed) \
    DERIVED(neutralize_offer_, "UI records (read() clears them)") \
    REBUILT(victory_rules_, "q6 t21: set_victory_rules from the definitions") \
    REBUILT(victory_records_, "q6 t21: built by set_victory_rules") \
    REBUILT(victory_side_, "q6 t21: built by set_victory_rules") \
    SAVED(local_player_, hashed) \
    SAVED(auto_reveal_, hashed) \
    SAVED(reveal_sfx_, unhashed)
// clang-format on
