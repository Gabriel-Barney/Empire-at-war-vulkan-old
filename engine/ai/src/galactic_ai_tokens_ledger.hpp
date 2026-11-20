// REF4 t02: the save ledger of GalacticSimTokenResolver. Every data member is listed once:
//   SAVED(member, hashed)    written by write() / visit_saved, restored by read() and walked by hash() / visit_saved;
//   SAVED(member, unhashed)  written and restored, outside the CRC (visit_saved's save_only fields);
//   REBUILT(member, "why")   set again by the owning GalacticAiWorld before read();
//   DERIVED(member, "why")   recomputed from saved state or static map data;
//   CHECKED(member, "why")   not restored, but write() / read() read it.
// tools/lint/check_battle_ledger.py (build_agent.cmd, ctest alamo_sim_battle_ledger_lint) checks it against
// galactic_ai_tokens.hpp and the bodies of write / read / hash / visit_saved (galactic_ai_save.cpp). write() and
// hash() walk one list (visit_saved); GalacticAiWorld::hash adds the resolver's hash last.
// Order: the declaration order in galactic_ai_tokens.hpp. Read by the lint only (not compiled).
#pragma once

// clang-format off
#define ALAMO_GALACTIC_AI_TOKENS_LEDGER(SAVED, REBUILT, DERIVED, CHECKED) \
    CHECKED(sim_, "the GalacticSim: read() refuses a planet count other than 0 or the sim's") \
    REBUILT(types_, "the AI type table, set by the owning world") \
    REBUILT(s2_, "the S2 resolver, set by the owning world") \
    REBUILT(hooks_, "host callbacks, set by the owning world") \
    REBUILT(categories_, "the category enum, set by the owning world") \
    SAVED(frame_, hashed) \
    SAVED(logic_frame_, hashed) \
    SAVED(player_stamps_, hashed) \
    SAVED(active_, hashed) \
    SAVED(conflict_log_seen_, hashed) \
    SAVED(stamp_space_, hashed) \
    SAVED(stamp_ground_, hashed) \
    SAVED(stamp_conversion_, hashed) \
    SAVED(stamp_corruption_, hashed) \
    SAVED(last_owner_, hashed) \
    SAVED(last_corruption_, hashed) \
    SAVED(last_battle_, hashed) \
    DERIVED(neighbours_, "planet evaluator lists, rebuilt from the sim's static map data") \
    SAVED(visible_, hashed) \
    SAVED(last_surface_, hashed) \
    CHECKED(mu_, "the leaf cache's lock, held by write() / hash() / read()") \
    SAVED(cache_, hashed) \
    SAVED(hits_, unhashed)
// clang-format on
