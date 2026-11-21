// REF4 t02: the save ledger of StoryModel. Every data member is listed once:
//   SAVED(member, hashed)    written by write(), restored by read() and walked by hash();
//   SAVED(member, unhashed)  written and restored, outside the CRC;
//   REBUILT(member, "why")   set again when the session loads the same plot files and attaches the model;
//   DERIVED(member, "why")   recomputed from saved state, scratch, or presentation records;
//   CHECKED(member, "why")   not restored, but write() / read() read it.
// tools/lint/check_battle_ledger.py (build_agent.cmd, ctest alamo_sim_battle_ledger_lint) checks it against
// story_model.hpp and the bodies of write / read / hash in story_model.cpp. write() and hash() walk the same record lists
// (story_model.cpp, namespace story_records: the event states, the flag / objective / blip / highlight / AI flag tables,
// the fired log).
// Order: the declaration order in story_model.hpp. Read by the lint only (not compiled).
#pragma once

// clang-format off
#define ALAMO_STORY_MODEL_LEDGER(SAVED, REBUILT, DERIVED, CHECKED) \
    REBUILT(on_reward, "host callback") \
    REBUILT(faction_of, "host callback") \
    REBUILT(objects_within, "host callback") \
    REBUILT(load_map_hero, "host callback") \
    REBUILT(text_lookup, "host callback") \
    SAVED(plots_, hashed) \
    REBUILT(plot_walk_, "the plot table order, built when the plot files load") \
    REBUILT(table_replaced_, "per plot: replaced by a later model's plot (StoryTable), built at attach") \
    REBUILT(table_, "the session's StoryTable") \
    REBUILT(script_vms_, "per plot Lua VMs, attached by the session") \
    DERIVED(script_errors_, "script error log (presentation)") \
    SAVED(flags_, hashed) \
    SAVED(objectives_, hashed) \
    SAVED(blips_, hashed) \
    SAVED(highlights_, hashed) \
    SAVED(ai_flags_, hashed) \
    SAVED(fired_, hashed) \
    DERIVED(dialog_log_, "dialog records for the presentation") \
    REBUILT(holocron_, "shared by the session's models (share_holocron); StorySession::write saves its list") \
    DERIVED(dialog_loads_, "dialog files asked for, for the presentation") \
    DERIVED(warnings_, "load warnings (presentation)") \
    SAVED(player_, hashed) \
    SAVED(frame_, hashed) \
    SAVED(time_, hashed) \
    SAVED(paused_, hashed) \
    DERIVED(fire_depth_, "recursion guard inside one fire") \
    DERIVED(firing_, "mcx6: the notification being fired, inside one fire") \
    DERIVED(reset_depth_, "RESET_BRANCH depth inside one reset")
// clang-format on
