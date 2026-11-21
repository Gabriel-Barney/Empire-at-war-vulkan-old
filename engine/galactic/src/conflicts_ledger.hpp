// REF4 t02: the save ledger of ConflictManager. Every data member is listed once:
//   SAVED(member, hashed)    written by write(), restored by read() and walked by hash();
//   SAVED(member, unhashed)  written and restored, outside the CRC;
//   REBUILT(member, "why")   set again by the host (CampaignBattles::attach) or the constructor before read();
//   DERIVED(member, "why")   recomputed from saved state or scratch;
//   CHECKED(member, "why")   not restored, but write() / read() / hash() read it.
// tools/lint/check_battle_ledger.py (build_agent.cmd, ctest alamo_sim_battle_ledger_lint) checks it against
// conflicts.hpp and the bodies of write / read / hash / register_crc in engine/galactic/src. The fields of a ConflictInfo
// and a ConflictLogEntry are listed once, in galactic_records.hpp (save writer, reader and hash()).
// Order: the declaration order in conflicts.hpp. Read by the lint only (not compiled).
#pragma once

// clang-format off
#define ALAMO_CONFLICTS_LEDGER(SAVED, REBUILT, DERIVED, CHECKED) \
    REBUILT(decide, "host callback") \
    REBUILT(start_battle, "host callback") \
    REBUILT(pause, "host callback") \
    REBUILT(pick_map, "host callback") \
    REBUILT(on_survival_roll, "host callback") \
    REBUILT(on_space_decide, "host callback") \
    REBUILT(story_lock, "host callback") \
    CHECKED(sim_, "the GalacticSim: write() stores its planet row count, read() refuses another; handles as object ids") \
    REBUILT(rules_, "the conflict rules handed to the constructor") \
    REBUILT(rng_, "the session's random stream (saved by the session)") \
    REBUILT(cfg_, "the config handed to the constructor") \
    SAVED(space_q_, hashed) \
    SAVED(land_q_, hashed) \
    SAVED(state_, hashed) \
    SAVED(active_, hashed) \
    SAVED(tick_, hashed) \
    SAVED(timer_start_, hashed) \
    SAVED(force_, hashed) \
    SAVED(retreating_, hashed) \
    SAVED(pause_held_, hashed) \
    SAVED(has_choice_, hashed) \
    SAVED(choice_, hashed) \
    SAVED(ar_in_, unhashed) \
    SAVED(ar_objects_, unhashed) \
    SAVED(ar_cargo_, unhashed) \
    SAVED(last_ar_, hashed) \
    SAVED(request_, unhashed) \
    SAVED(planet_map_, hashed) \
    SAVED(log_, hashed)
// clang-format on
