// REF4 t02: the save ledger of CampaignBattles. Every data member is listed once:
//   SAVED(member, hashed)    written by a write_* / register_*_chunks save participant, restored by the matching read_*
//                            and walked by a hash_* function or a register_crcs chunk;
//   SAVED(member, unhashed)  written and restored, outside the CRC;
//   REBUILT(member, "why")   set again by the host or attach() when a load rebuilds the session;
//   DERIVED(member, "why")   recomputed from saved state, scratch, or presentation records;
//   CHECKED(member, "why")   not restored, but the save functions read it.
// tools/lint/check_battle_ledger.py (build_agent.cmd, ctest alamo_sim_battle_ledger_lint) checks it against
// campaign_battles.hpp and the bodies of write_* / read_* / register_*_chunks / hash_* / register_crcs in
// engine/apps/sim/src. battle_, cm_ and ai_ save and hash themselves (their own ledgers: tactical_battle_ledger.hpp,
// engine/galactic/src/conflicts_ledger.hpp, battle_ai_ledger.hpp); here they are SAVED because their chunks are
// registered here.
// Order: the declaration order in campaign_battles.hpp. Read by the lint only (not compiled).
#pragma once

// clang-format off
#define ALAMO_CAMPAIGN_BATTLES_LEDGER(SAVED, REBUILT, DERIVED, CHECKED) \
    DERIVED(resume_, "load pass 1: the running battle to rebuild, set by read_state") \
    REBUILT(gate_, "story-block forwarder, made by attach()") \
    CHECKED(host_, "the host whose save participants and CRC registry these chunks are") \
    REBUILT(rules_, "the conflict rules handed to the constructor") \
    REBUILT(catalog_, "the battle catalog handed to the constructor") \
    REBUILT(loader_, "map loader, made by the constructor") \
    REBUILT(opt_, "options handed to the constructor") \
    SAVED(cm_, hashed) \
    SAVED(battle_, hashed) \
    SAVED(pending_, unhashed) \
    SAVED(direct_, unhashed) \
    SAVED(upgrades_seen_, unhashed) \
    SAVED(pending_req_, unhashed) \
    REBUILT(pending_map_, "loaded again from pending_req_'s map by read_state") \
    SAVED(log_, unhashed) \
    DERIVED(last_losses_, "the last battle's removal records for the host, read in the frame the battle ends") \
    DERIVED(last_end_, "ui3 t05: presentation of the last battle end") \
    SAVED(fought_, unhashed) \
    DERIVED(rebuilding_, "true only inside rebuild()") \
    SAVED(fog_active_, unhashed) \
    SAVED(fog_render_, unhashed) \
    SAVED(ar_requested_, unhashed) \
    DERIVED(ar_log_seen_, "the auto-resolve log position, taken at the first hook (also after a load)") \
    DERIVED(ar_log_init_, "the auto-resolve log position, taken at the first hook (also after a load)") \
    DERIVED(ar_reset_sent_, "the auto-resolve log position, taken at the first hook (also after a load)") \
    SAVED(corruption_queued_, unhashed) \
    SAVED(lk_, hashed) \
    SAVED(corruption_active_, unhashed) \
    SAVED(last_corruption_, unhashed) \
    SAVED(story_bridge_, unhashed) \
    DERIVED(bridge_before_spawn_, "inside one battle start (story_begin)") \
    REBUILT(story_hooks_, "hooks of the tactical story VMs, installed again at the battle start") \
    REBUILT(story_remaining_, "hooks of the tactical story VMs, installed again at the battle start") \
    SAVED(story_transports_, unhashed) \
    SAVED(story_motion_, unhashed) \
    SAVED(story_base_ids_, unhashed) \
    SAVED(story_linked_, unhashed) \
    REBUILT(story_fog_session_, "the session the ENABLE_FOW hooks are installed on") \
    SAVED(story_fog_, unhashed) \
    SAVED(credits_, hashed) \
    SAVED(ai_, hashed) \
    SAVED(ai_log_, unhashed) \
    DERIVED(lua_close_wait_, "OPT3 t11: the last battle's VM-freeing job")
// clang-format on
