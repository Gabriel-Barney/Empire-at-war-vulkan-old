// REF4 t02: the save ledger of MovementSystem. Every data member is listed once:
//   SAVED(member, hashed)    written by write(), restored by read() and walked by a hash_* function;
//   SAVED(member, unhashed)  written and restored, outside the CRC;
//   REBUILT(member, "why")   set again by the host or setup when a load rebuilds the battle;
//   DERIVED(member, "why")   recomputed from saved state, a cache, scratch or the records of one tick;
//   CHECKED(member, "why")   not restored, but write() / read() read it (a fingerprint read() checks).
// tools/lint/check_battle_ledger.py (build_agent.cmd, ctest alamo_sim_battle_ledger_lint) checks it against
// movement_system.hpp and the bodies of write / read / hash_* / register_crc. Scope: the members of MovementSystem; a
// field of Unit, FighterSquad, ... is walked field by field (io_unit, io_squad) and is not seen here.
// Order: the declaration order in movement_system.hpp. Read by the lint only (not compiled).
#pragma once

// clang-format off
#define ALAMO_MOVEMENT_LEDGER(SAVED, REBUILT, DERIVED, CHECKED) \
    CHECKED(cfg_, "set by the battle at construction; write() stores the mode, read() refuses another") \
    SAVED(grid_, hashed) \
    DERIVED(regions_, "a cache of the grid's effective values, rebuilt by read() when regions_built_") \
    SAVED(regions_built_, unhashed) \
    REBUILT(height_, "host callback (set_height_fn), set again by the battle") \
    REBUILT(water_, "set_water_plane from the map, set again by the battle") \
    REBUILT(water_height_, "set_water_plane from the map, set again by the battle") \
    SAVED(space_, hashed) \
    SAVED(units_, hashed) \
    SAVED(tree_, unhashed) \
    DERIVED(qctx_, "query scratch") \
    SAVED(quota_used_, hashed) \
    SAVED(coll_, hashed) \
    SAVED(stats_, unhashed) \
    DERIVED(attack_failed_, "records of one tick, cleared by the battle at the tick start") \
    REBUILT(hostile_, "host callback") \
    REBUILT(scatters_, "host callback") \
    REBUILT(locked_, "host callback") \
    REBUILT(model_bound_, "host callback") \
    SAVED(crush_, hashed) \
    SAVED(push_kills_, hashed) \
    SAVED(formation_attacks_, hashed) \
    DERIVED(deploy_pose_, "records of one tick, read by the battle in the same tick") \
    REBUILT(threat_, "host callback") \
    REBUILT(anim_len_, "host callback") \
    SAVED(air_, unhashed) \
    SAVED(map_floor_, unhashed) \
    SAVED(despawned_, hashed) \
    SAVED(squads_, hashed) \
    SAVED(fgrids_, hashed) \
    REBUILT(favoid_, "host callback") \
    SAVED(fevents_, hashed) \
    SAVED(fforms_, hashed) \
    DERIVED(fform_internal_, "scratch inside the formation tick (read() clears it)") \
    SAVED(pending_, unhashed) \
    SAVED(pend_serial_, unhashed) \
    SAVED(parked_, hashed) \
    SAVED(deploy_kept_, hashed) \
    DERIVED(team_ix_, "from units_ (team_ix_rebuild, called by read())") \
    DERIVED(team_all_, "from units_ and parked_ (team_ix_rebuild, called by read())") \
    REBUILT(deferred_compose_, "host setup (the host runs coordinator_update)") \
    SAVED(form_serial_, unhashed) \
    SAVED(coord_serial_, unhashed) \
    REBUILT(fire_test_, "host callback") \
    REBUILT(attack_range_fn_, "host callback") \
    REBUILT(min_attack_fn_, "host callback") \
    REBUILT(fhooks_, "host callbacks, set again by the loader") \
    REBUILT(fly_hooks_, "host callbacks") \
    REBUILT(deploy_clip_, "host callback") \
    REBUILT(undeploy_block_, "host callback") \
    REBUILT(deploy_block_, "host callback") \
    REBUILT(turret_rest_, "host callback") \
    REBUILT(turret_aim_, "host callback") \
    DERIVED(devents_, "records of one tick, drained by the battle (take_deploy_events) in the same tick") \
    DERIVED(exit_arrivals_, "records of one tick, drained by the battle (take_exit_arrivals) in the same tick") \
    DERIVED(tevents_, "records of one tick, drained by the battle (take_turn_clip_events) in the same tick")
// clang-format on
