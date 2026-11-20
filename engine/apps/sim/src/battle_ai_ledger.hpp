// REF4 t02: the save ledger of BattleAi. Every data member is listed once:
//   SAVED(member, hashed)    written by write() / visit_saved, restored by read() and walked by hash() / visit_saved;
//   SAVED(member, unhashed)  written and restored, outside the CRC (visit_saved's save_only fields);
//   REBUILT(member, "why")   set again by the constructor or start() when a load rebuilds the battle;
//   DERIVED(member, "why")   recomputed from saved state, a per-type cache, scratch, or a diagnostic counter;
//   CHECKED(member, "why")   not restored, but write() / read() read it.
// tools/lint/check_battle_ledger.py (build_agent.cmd, ctest alamo_sim_battle_ledger_lint) checks it against
// battle_ai.hpp and the bodies of write / read / hash / visit_saved in battle_ai.cpp. write() and hash() walk the saved
// fields through one list (visit_saved), so a field the save gains enters the CRC unless it is marked save_only.
// Order: the declaration order in battle_ai.hpp. Read by the lint only (not compiled).
#pragma once

// clang-format off
#define ALAMO_BATTLE_AI_LEDGER(SAVED, REBUILT, DERIVED, CHECKED) \
    DERIVED(type_facts_, "per-type cache (filled from type data on first use)") \
    REBUILT(allow_reinforcement_fraction_, "A2 tactical: from GameConstants at start()") \
    REBUILT(use_reinforcement_points_, "a1g t04: from GameConstants at start()") \
    SAVED(follows_, hashed) \
    REBUILT(b_, "the battle handed to the constructor") \
    REBUILT(mode_, "the tactical mode handed to the constructor") \
    REBUILT(host_, "the host handed to the constructor") \
    REBUILT(zones_, "hint zones from the map, built by start()") \
    REBUILT(zone_locator_, "position -> zone, built by start()") \
    SAVED(world_, hashed) \
    REBUILT(order_listener_, "S7 t31: the listener token, registered again by start()") \
    SAVED(ai_side_, hashed) \
    SAVED(stale_, unhashed) \
    SAVED(lane_batch_, hashed) \
    DERIVED(view_, "sp3 t02: captured again at the next AI slot (read() invalidates it)") \
    DERIVED(lane_thinking_, "ref5 t06: scratch, set only while think() runs") \
    DERIVED(lane_abilities_, "sp3 slice 4b: set at each capture, cleared by a load") \
    REBUILT(live_remaining_, "live hook sets, installed again by start()") \
    REBUILT(live_tactical_, "live hook sets, installed again by start()") \
    DERIVED(lane_refused_spawns_, "diagnostic counter") \
    DERIVED(lane_unloaded_, "sp3 slice 4d: consumed within the tick of its apply (never pending at a save)") \
    REBUILT(weather_category_, "per type, from the type data at start()") \
    REBUILT(weather_categories_, "from the type data at start()") \
    SAVED(lane_locks_, hashed) \
    SAVED(lane_job_, unhashed) \
    SAVED(lane_digest_, hashed) \
    SAVED(lane_seq_, hashed) \
    DERIVED(lane_mismatch_, "diagnostic counter") \
    DERIVED(attack_refused_, "q6 t19: the last execute's refusals, read at once") \
    SAVED(executed_, unhashed) \
    SAVED(opp_prev_, hashed) \
    SAVED(unit_ai_stamps_, hashed) \
    SAVED(unit_ai_second_, hashed) \
    REBUILT(tactical_, "script hooks of the tactical AI VMs, installed again by start()") \
    REBUILT(fog_hooks_, "script hooks of the tactical AI VMs, installed again by start()") \
    REBUILT(remaining_, "script hooks of the tactical AI VMs, installed again by start()")
// clang-format on
