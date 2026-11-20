// REF4 t02: the save ledger of StorySession. Every data member is listed once:
//   SAVED(member, hashed)    written by write() / a write_* chunk, restored by read() / read_* and walked by hash() or a
//                            hash_* section;
//   SAVED(member, unhashed)  written and restored, outside the CRC;
//   REBUILT(member, "why")   set again by the host or the constructor (StoryStart) before read();
//   DERIVED(member, "why")   recomputed from saved state, scratch, or a request the host takes in the same frame;
//   CHECKED(member, "why")   not restored, but the save functions read it.
// tools/lint/check_battle_ledger.py (build_agent.cmd, ctest alamo_sim_battle_ledger_lint) checks it against
// story_session.hpp and the bodies of write* / read* / hash* / register_crc in engine/apps/sim/src.
// Order: the declaration order in story_session.hpp. Read by the lint only (not compiled).
#pragma once

// clang-format off
#define ALAMO_STORY_SESSION_LEDGER(SAVED, REBUILT, DERIVED, CHECKED) \
    REBUILT(self_, "a2k t12: the handle the sim's spawn hook reaches this session through") \
    REBUILT(sim_, "the GalacticSim handed to the constructor") \
    REBUILT(store_, "the object store handed to the constructor") \
    REBUILT(types_, "the type table handed to the constructor") \
    REBUILT(rng_, "the session's random stream (saved by the session)") \
    REBUILT(files_, "the file provider handed to the constructor") \
    REBUILT(global_values_, "Int-final: StoryStart::global_values") \
    REBUILT(lua_gc_pf_, "OPT2 t04: the host's ParallelFor") \
    SAVED(lua_, hashed) \
    SAVED(sets_, hashed) \
    SAVED(triggers_, hashed) \
    SAVED(pending_host_, unhashed) \
    SAVED(pending_ui_, unhashed) \
    SAVED(destroyed_seen_, unhashed) \
    SAVED(destroyed_init_, unhashed) \
    DERIVED(scratch_, "event scratch inside one call") \
    SAVED(battle_, unhashed) \
    SAVED(tactical_, hashed) \
    SAVED(linked_, unhashed) \
    SAVED(owner_seen_, unhashed) \
    SAVED(tactical_headers_, unhashed) \
    SAVED(corruption_player_, unhashed) \
    SAVED(corruption_type_, unhashed) \
    SAVED(tactical_map_, hashed) \
    REBUILT(enable_fow_, "host callback") \
    REBUILT(enable_victory_, "host callback") \
    REBUILT(victory_, "host callback") \
    REBUILT(drain_tap_, "host callback") \
    REBUILT(scoring_tap_, "host callback") \
    REBUILT(switch_control_, "host callback") \
    SAVED(gates_, hashed) \
    SAVED(tutorial_, hashed) \
    SAVED(reinforce_off_, hashed) \
    SAVED(retreat_off_, hashed) \
    DERIVED(load_campaign_, "a LOAD_CAMPAIGN request the host takes in the frame it was made (read() clears it)") \
    REBUILT(tactical_reward_, "host callback") \
    DERIVED(reward_firing_planet_, "inside one reward") \
    SAVED(def_edits_, hashed) \
    DERIVED(def_undo_, "the undo records of def_edits_, made again when read() applies them") \
    DERIVED(tactical_applied_, "how many tactical edits read() applied to the attached set") \
    SAVED(random_story_count_, hashed) \
    SAVED(random_story_rng_, hashed) \
    SAVED(autoresolve_disabled_, unhashed) \
    SAVED(autoresolve_planets_, unhashed) \
    SAVED(comp_local_, hashed) \
    SAVED(comp_other_, hashed) \
    REBUILT(raid_test_, "mcx6 t03: host callback") \
    DERIVED(resume_tactical_, "the tactical story a load resumes, set by read() from the saved one") \
    SAVED(log_, unhashed) \
    SAVED(lua_messages_, unhashed) \
    SAVED(lines_, unhashed) \
    SAVED(frame_, unhashed) \
    SAVED(seconds_, unhashed)
// clang-format on
