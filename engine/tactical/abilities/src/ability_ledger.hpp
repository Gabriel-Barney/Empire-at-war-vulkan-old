// REF4 t02: the save ledger of AbilitySystem. Every data member is listed once:
//   SAVED(member, hashed)    written by write(), restored by read() and walked by hash() / register_crc;
//   SAVED(member, unhashed)  written and restored, outside the CRC;
//   REBUILT(member, "why")   set again by the host or setup when a load rebuilds the battle;
//   DERIVED(member, "why")   recomputed from saved state, a cache, scratch or the records of one tick;
//   CHECKED(member, "why")   not restored, but write() / read() read it (a fingerprint read() checks).
// tools/lint/check_battle_ledger.py (build_agent.cmd, ctest alamo_sim_battle_ledger_lint) checks it against
// ability_system.hpp and the bodies of write / read / hash / register_crc. Scope: the members of AbilitySystem; a field
// of Obj, Mine, ... is written field by field and is not seen here.
// Order: the declaration order in ability_system.hpp. Read by the lint only (not compiled).
#pragma once

// clang-format off
#define ALAMO_ABILITY_LEDGER(SAVED, REBUILT, DERIVED, CHECKED) \
    SAVED(anim_plays_, unhashed) \
    REBUILT(host_, "host callbacks (set_host)") \
    REBUILT(specific_faction_, "s78n t04: resolved by set_host from the type data") \
    DERIVED(rng_, "the synced random stream bound by the battle (its state is saved by the battle)") \
    SAVED(spawns_, unhashed) \
    SAVED(spawn_serial_, hashed) \
    SAVED(mines_, hashed) \
    SAVED(buzz_, hashed) \
    SAVED(player_entries_, hashed) \
    SAVED(source_entries_, hashed) \
    SAVED(remote_bombs_, hashed) \
    DERIVED(scratch_, "per-call query scratch") \
    DERIVED(member_scratch_, "per-call host query output") \
    SAVED(direct_ctx_, unhashed) \
    CHECKED(combat_, "the battle's CombatSystem; write() stores its player count, read() refuses another") \
    CHECKED(data_, "the host's ability tables; write() stores their counts, read() refuses others") \
    REBUILT(cfg_, "set by the battle at construction") \
    SAVED(objs_, hashed) \
    SAVED(events_, unhashed) \
    DERIVED(visuals_, "vis1 t06: presentation records, read() clears them") \
    DERIVED(visual_serial_, "vis1 t06: presentation serial (never read by the sim)") \
    DERIVED(activations_, "aud1 t10: presentation records, read() clears them") \
    DERIVED(activation_serial_, "aud1 t10: presentation serial (never read by the sim)") \
    SAVED(overrides_, hashed) \
    SAVED(type_overrides_, hashed) \
    SAVED(bm_blocked_, unhashed) \
    REBUILT(fog_reveal_all_, "host callback (fog follow-up)")
// clang-format on
