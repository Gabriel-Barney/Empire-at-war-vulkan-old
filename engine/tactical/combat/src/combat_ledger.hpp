// REF4 t02: the save ledger of CombatSystem. Every data member is listed once:
//   SAVED(member, hashed)    written by write(), restored by read() and walked by a hash_* / register_*crc function;
//   SAVED(member, unhashed)  written and restored, outside the CRC;
//   REBUILT(member, "why")   set again by the host or setup when a load rebuilds the battle;
//   DERIVED(member, "why")   recomputed from saved state, a cache, scratch or the records of one tick;
//   CHECKED(member, "why")   not restored, but write() / read() read it (a fingerprint read() checks).
// tools/lint/check_battle_ledger.py (build_agent.cmd, ctest alamo_sim_battle_ledger_lint) checks it against
// combat_system.hpp and the bodies of write / read / hash_* / register_*crc. Scope: the members of CombatSystem; a field
// of Unit, Hardpoint, Projectile, ... is written field by field and is not seen here.
// Order: the declaration order in combat_system.hpp. Read by the lint only (not compiled).
#pragma once

// clang-format off
#define ALAMO_COMBAT_LEDGER(SAVED, REBUILT, DERIVED, CHECKED) \
    CHECKED(data_, "the host's type tables; write() stores their counts, read() refuses others") \
    REBUILT(cfg_, "set by the battle at construction") \
    SAVED(players_, unhashed) \
    SAVED(enemy_, unhashed) \
    SAVED(sw_hp_enabled_, hashed) \
    SAVED(player_mods_, hashed) \
    SAVED(units_, hashed) \
    SAVED(hps_, hashed) \
    DERIVED(cands_, "phase C scratch, written before it is read") \
    SAVED(projectiles_, hashed) \
    DERIVED(sweep_, "phase E scratch, written before it is read") \
    SAVED(delayed_, hashed) \
    SAVED(deaths_, unhashed) \
    SAVED(hp_events_, unhashed) \
    DERIVED(stun_visuals_, "vis1 t08: presentation records, read() clears them") \
    DERIVED(stun_visual_serial_, "vis1 t08: presentation serial (never read by the sim)") \
    SAVED(smoke_events_, unhashed) \
    SAVED(damage_events_, unhashed) \
    SAVED(hit_events_, unhashed) \
    SAVED(shot_events_, unhashed) \
    DERIVED(lob_events_, "records of one tick, drained by the battle in the same tick") \
    DERIVED(attack_events_, "records of one tick, drained by the battle in the same tick") \
    SAVED(survivor_events_, unhashed) \
    SAVED(base_power_, hashed) \
    SAVED(shield_upgrade_, hashed) \
    DERIVED(bshields_, "tru1 t02: rebuilt each tick from the units") \
    DERIVED(bshield_sets_, "r3h t06: this tick's Set_State calls") \
    SAVED(repairs_, hashed) \
    REBUILT(obstacle_cell_, "host callback") \
    REBUILT(eng_, "host engagement queries (set_engagement), set again by the battle") \
    REBUILT(precise_hit_, "host callback") \
    REBUILT(garrison_host_, "host callback") \
    REBUILT(credits_get_, "host callback") \
    REBUILT(credits_add_, "host callback") \
    DERIVED(effect_scratch_, "scratch inside one call") \
    DERIVED(emitters_, "phase F scratch") \
    SAVED(rockets_, unhashed) \
    DERIVED(rocket_next_, "phase E scratch") \
    REBUILT(surface_, "host terrain surface queries") \
    REBUILT(rocket_terrain_, "host terrain, re-attached by the host") \
    REBUILT(hooks_, "host hooks") \
    REBUILT(bound_rng_, "the battle's random stream (its state is saved by the battle)") \
    DERIVED(tick_rng_, "set for the length of one tick") \
    DERIVED(flush_hps_, "scratch of flush()") \
    DERIVED(leaders_, "phase A scratch") \
    SAVED(team_heads_, unhashed) \
    REBUILT(team_pos_, "host-set each tick") \
    REBUILT(clash_pairs_, "host-set (running hero clashes)") \
    DERIVED(team_members_, "cu8f t04a: from the units, built in phase A and by read()") \
    DERIVED(land_heads_, "phase A scratch read in phase D") \
    DERIVED(land_teams_, "built in phase A") \
    REBUILT(dim_x_, "diminishing-firepower spline from the config") \
    REBUILT(dim_y_, "diminishing-firepower spline from the config") \
    REBUILT(dim_y2_, "diminishing-firepower spline from the config") \
    SAVED(next_serial_, hashed) \
    SAVED(frame_, unhashed) \
    DERIVED(cand_reserve_, "a capacity hint") \
    SAVED(stats_, unhashed) \
    DERIVED(trees_, "the host's tree set for one tick") \
    REBUILT(last_trees_, "the host's tree set (set_trees and each tick)") \
    DERIVED(ctx_mutex_, "query context pool") \
    DERIVED(ctx_all_, "query context pool") \
    DERIVED(ctx_free_, "query context pool") \
    DERIVED(serial_ctx_, "query context") \
    SAVED(weather_acc_, unhashed)
// clang-format on
