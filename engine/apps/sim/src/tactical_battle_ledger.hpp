// REF2 t01: the battle save ledger. Every data member of TacticalBattle is listed here once, as
//   SAVED(member, how)      written to a save: `state` by state_io, `part` by write_part (a module system), `chunk` by a
//                           save chunk of its own (fleet groups: write_fleet_groups);
//   REBUILT(member, "why")  set again from the same inputs when a load rebuilds the battle (the host, setup(), the map
//                           load) before the saved parts are read;
//   DERIVED(member, "why")  recomputed from saved state, a per-type cache, scratch, or the records of one tick.
// A member that is none of these is the bug the seven earlier "forgotten state" fixes repaired (111b08b0, e203c759,
// 32dccdff, 3cc0db7b, dcab66a0, 81a0907b, 5b1b6c24). Checks:
//   - tools/lint/check_battle_ledger.py (build_agent.cmd, ctest alamo_sim_battle_ledger_lint): every member is listed
//     once, every state_io field is SAVED(.., state), every SAVED(.., state) is in state_io and every SAVED(.., part)
//     in write_part;
//   - TacticalBattle::check_save_ledger (tactical_battle_save.cpp): state_io, run on a probe archive, walks exactly the
//     SAVED(.., state) members (alamo_sim_save_continue_tests calls it on running battles);
//   - the save-and-continue test (alamo_sim_save_continue_tests): each battle fixture saved, rebuilt and loaded at every
//     64-tick checkpoint gives the checkpoint CRCs and lookup answers of a run that never stopped.
// Scope: the members of TacticalBattle itself. The checks see a member, not its fields: a field added to a record
// (Landing, AttackBlock, ...) or to a struct state_io walks field by field (result_, stats_, rt_, vm_decl_) must be added
// to state_io by hand, and the module systems' own saves (write_system: movement, combat, abilities, ...) are outside
// this ledger. Only the save-and-continue test sees those, on the state its fixtures reach.
// Order: the declaration order in tactical_battle.hpp. Members declared under an #ifdef have their own list below.
// Included only by tactical_battle_save.cpp.
#pragma once

// clang-format off
#define ALAMO_TACTICAL_BATTLE_LEDGER(SAVED, REBUILT, DERIVED) \
    DERIVED(land_obstacle_none_, "per-type cache (filled from type data on first use)") \
    DERIVED(drop_pos_, "scratch inside one call or tick") \
    DERIVED(deploy_unload_, "scratch inside one call or tick") \
    DERIVED(drop_face_, "scratch inside one call or tick") \
    SAVED(landings_, state) \
    DERIVED(landings_unloaded_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(production_done_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(presentation_, "ref2 t02: presentation records since the host's last drain (never read by the sim)") \
    REBUILT(emit_presentation_, "ref2 t02: host switch, armed again by the host after a load (GameSession)") \
    SAVED(rt_, state) \
    SAVED(sp_spawners_, state) \
    SAVED(sp_debris_, state) \
    SAVED(sp_scanned_, state) \
    SAVED(sp_used_, state) \
    DERIVED(adopt_kind_, "per-type cache (filled from type data on first use)") \
    DERIVED(deploy_script_, "scratch inside one call or tick") \
    REBUILT(script_locks_, "host callback, registered again on a rebuild (story / AI VMs)") \
    REBUILT(script_ctls_, "host callback, registered again on a rebuild (story / AI VMs)") \
    DERIVED(script_ctl_, "per unit, made each tick from script_ctls_") \
    SAVED(fformations_, state) \
    SAVED(lformations_, state) \
    DERIVED(fchase_cache_, "per-type cache (filled from type data on first use)") \
    DERIVED(fchase_have_, "per-type cache (filled from type data on first use)") \
    DERIVED(presence_cache_, "per-type cache (filled from type data on first use)") \
    DERIVED(idle_gate_, "scratch inside one call or tick") \
    DERIVED(infantry_category_, "lazy lookup of a category bit") \
    SAVED(queues_, state) \
    SAVED(queue_group_, state) \
    SAVED(queue_used_, state) \
    SAVED(fleet_, chunk) \
    REBUILT(fleet_on_, "set by the host that starts the battle, the same on a rebuild") \
    SAVED(team_attacks_, state) \
    SAVED(attack_blocks_, state) \
    SAVED(fleet_cohesion_, state) \
    REBUILT(catalog_, "set by the host that starts the battle, the same on a rebuild") \
    REBUILT(galaxy_, "set by the host that starts the battle, the same on a rebuild") \
    REBUILT(rules_, "set by the host that starts the battle, the same on a rebuild") \
    REBUILT(req_, "set by the host that starts the battle, the same on a rebuild") \
    REBUILT(cfg_, "set by the host that starts the battle, the same on a rebuild") \
    DERIVED(engines_restarted_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(hyperspace_sfx_, "records of the last tick for the host (cleared at the next tick start)") \
    REBUILT(space_, "set by the host that starts the battle, the same on a rebuild") \
    REBUILT(map_, "set by the host that starts the battle, the same on a rebuild") \
    REBUILT(tables_, "made by setup(), the same on a rebuild; read_part adds the script-added types") \
    SAVED(types_used_, state) \
    SAVED(descs_, state) \
    REBUILT(tables_build_, "made by setup() from the battle inputs, the same on a rebuild") \
    SAVED(movement_, part) \
    DERIVED(clr_types_, "per-type cache (filled from type data on first use)") \
    DERIVED(clr_known_, "per-type cache (filled from type data on first use)") \
    DERIVED(avoid_members_, "the SPACE_AVOIDANCE members of this tick") \
    DERIVED(avoid_type_, "per-type cache (filled from type data on first use)") \
    SAVED(combat_, part) \
    SAVED(abilities_, part) \
    REBUILT(hooks_, "made by setup() from the battle inputs, the same on a rebuild") \
    REBUILT(anim_, "set by the host that starts the battle, the same on a rebuild") \
    REBUILT(dummy_type_, "made by setup() from the battle inputs, the same on a rebuild") \
    SAVED(parts_, state) \
    SAVED(units_, state) \
    SAVED(containers_, state) \
    SAVED(particles_, state) \
    SAVED(dummies_, state) \
    SAVED(death_clones_, state) \
    REBUILT(clone_models_, "set by the host that starts the battle, the same on a rebuild") \
    SAVED(effects_, state) \
    DERIVED(story_generics_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(story_reinforced_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(retreat_off_, "synced from the story session (saved) by CampaignBattles::story_sync_switches") \
    DERIVED(reinf_off_, "synced from the story session (saved) by CampaignBattles::story_sync_switches") \
    SAVED(bombard_delay_override_, state) \
    DERIVED(ability_finished_, "records of the last tick for the host (cleared at the next tick start)") \
    REBUILT(order_replaced_listeners_, "host callback, registered again on a rebuild") \
    REBUILT(next_listener_token_, "host callback, registered again on a rebuild") \
    DERIVED(story_garrisoned_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(story_captured_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(story_vacated_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(ability_toggles_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(strike_starts_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(strike_starts_mark_, "how much of an event list the last tick reported (reset with the list)") \
    DERIVED(summon_spawns_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(summon_spawns_mark_, "how much of an event list the last tick reported (reset with the list)") \
    DERIVED(ability_toggles_mark_, "how much of an event list the last tick reported (reset with the list)") \
    DERIVED(story_vacated_mark_, "how much of an event list the last tick reported (reset with the list)") \
    DERIVED(pad_events_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(pad_events_mark_, "how much of an event list the last tick reported (reset with the list)") \
    DERIVED(pad_build_sfx_, "scratch inside one call or tick") \
    SAVED(garrison_, part) \
    SAVED(captures_, part) \
    SAVED(cashes_, part) \
    SAVED(s4_used_, state) \
    REBUILT(s4_store_, "the battle mode pointer, set again by setup / the first tick") \
    DERIVED(s4_capture_influence_, "per-type cache (filled from type data on first use)") \
    DERIVED(sp_type_max_speed_, "per-type cache (filled from type data on first use)") \
    SAVED(anchor_, state) \
    SAVED(facing_, state) \
    SAVED(drop_facing_, state) \
    SAVED(placed_, state) \
    SAVED(frame_, state) \
    SAVED(retreat_allowed_frame_, state) \
    SAVED(retreat_side_, state) \
    SAVED(retreat_done_frame_, state) \
    SAVED(retreat_complete_, state) \
    SAVED(vm_declared_, state) \
    SAVED(vm_decl_, state) \
    SAVED(vm_countdown_, state) \
    SAVED(vm_hold_, state) \
    SAVED(vm_queue_, state) \
    SAVED(vm_records_, state) \
    SAVED(vm_tracked_, state) \
    SAVED(vm_tsw_seen_, state) \
    SAVED(vm_declared_frame_, state) \
    SAVED(vm_cp_armed_, state) \
    SAVED(vm_cp_timer_, state) \
    SAVED(vm_cp_player_, state) \
    DERIVED(vm_deaths_, "the combat deaths of this tick") \
    REBUILT(vm_mode_, "the battle mode pointer, set again by setup / the first tick") \
    SAVED(finished_, state) \
    SAVED(result_, state) \
    SAVED(stats_, state) \
    SAVED(fog_pings_, state) \
    SAVED(fog_, part) \
    REBUILT(fog_mode_, "the battle mode pointer, set again by setup / the first tick") \
    SAVED(fog_on_, state) \
    SAVED(fog_global_active_, state) \
    SAVED(fog_global_render_, state) \
    REBUILT(fog_reveal_beh_, "made by setup() from the battle inputs, the same on a rebuild") \
    REBUILT(fog_hide_beh_, "made by setup() from the battle inputs, the same on a rebuild") \
    REBUILT(fog_team_beh_, "made by setup() from the battle inputs, the same on a rebuild") \
    DERIVED(force_disable_range_, "per-type cache (filled from type data on first use)") \
    SAVED(l1_sw_, part) \
    SAVED(l1_desc_cache_, state) \
    DERIVED(l1_sw_events_, "records of the last tick for the host (cleared at the next tick start)") \
    SAVED(l1_jumps_, part) \
    SAVED(l1_engines_, part) \
    SAVED(l1_fields_, part) \
    REBUILT(l1_k_, "set by the host that starts the battle, the same on a rebuild") \
    SAVED(l1_in_asteroid_, state) \
    DERIVED(l1_asteroid_hits_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(gravity_power_, "records of the last tick for the host (cleared at the next tick start)") \
    DERIVED(oi_crush_, "scratch inside one call or tick") \
    DERIVED(hero_clash_hits_, "scratch inside one call or tick") \
    SAVED(hero_clash_, part) \
    REBUILT(hero_clash_data_, "set by the host that starts the battle, the same on a rebuild") \
    SAVED(l1_weather_, part) \
    REBUILT(l1_weather_data_, "set by the host that starts the battle, the same on a rebuild") \
    SAVED(l1_weather_started_, state) \
    DERIVED(l1_weather_env_, "read_part: the environment of the loaded battle (cu8g t03)") \
    REBUILT(env_index_, "read_part: the start environment of the map (cu8g t02 [port])") \
    REBUILT(env_saved_, "read_part: not remembered across a load (ui2 t08)") \
    SAVED(fog_weather_dirty_, state) \
    SAVED(l1_trackers_, state) \
    SAVED(l1_final_blow_, state) \
    REBUILT(l1_factions_, "set by the host that starts the battle, the same on a rebuild") \
    SAVED(l1_bombard_, state) \
    SAVED(l1_runs_, state) \
    REBUILT(l1_extra_projectiles_, "made by setup() from the battle inputs, the same on a rebuild") \
    REBUILT(l1_retreat_increments_, "set by the host that starts the battle, the same on a rebuild") \
    REBUILT(l1_flight_increments_, "set by the host that starts the battle, the same on a rebuild") \
    REBUILT(l1_mode_, "the battle mode pointer, set again by setup / the first tick") \
    SAVED(l1_used_, state) \
    SAVED(l1_planet_destroyed_, state) \
    DERIVED(l1_tsw_kill_owner_, "records of the last tick for the host (cleared at the next tick start)") \
    SAVED(l1_tsw_story_winner_, state) \
    SAVED(l1_tsw_debris_, state) \
    SAVED(pad_levels_, state) \
    SAVED(pads_, state) \
    SAVED(pads_used_, state) \
    REBUILT(pads_mode_, "the battle mode pointer, set again by setup / the first tick") \
    DERIVED(vr_cache_, "per-type cache (filled from type data on first use)") \
    SAVED(map_hints_, state) \
    SAVED(map_marker_names_, state) \
    REBUILT(map_stats_, "made by the map load in setup(), the same on a rebuild") \
    REBUILT(slot_memory_, "set by the host that starts the battle, the same on a rebuild; relinked by CampaignBattles::read") \
    REBUILT(slot_memory_write_, "set by the host: on for a new battle, off for a rebuild; read only by setup()") \
    REBUILT(have_focus_, "made by the map load in setup(), the same on a rebuild") \
    REBUILT(focus_, "made by the map load in setup(), the same on a rebuild") \
    REBUILT(focus_player_, "made by the map load in setup(), the same on a rebuild") \
    SAVED(bt_markers_, state) \
    REBUILT(bt_players_, "made by setup() from the battle inputs, the same on a rebuild") \
    REBUILT(pad_records_, "set by the host that starts the battle, the same on a rebuild; relinked by CampaignBattles::read") \
    REBUILT(pad_restore_, "set by the host: on for a new battle, off for a rebuild; read only by setup()") \
    DERIVED(sk_income_, "per-type cache (filled from type data on first use)") \
    SAVED(bt_removals_, state) \
    SAVED(bt_landing_, state) \
    SAVED(bt_shields_, state) \
    REBUILT(credits_get_, "host callback, registered again on a rebuild") \
    REBUILT(credits_add_, "host callback, registered again on a rebuild") \
    REBUILT(arrival_hold_, "host callback, registered again on a rebuild") \
    REBUILT(scoring_death_, "host callback, registered again on a rebuild") \
    REBUILT(before_map_load_, "host callback, registered again on a rebuild") \
    REBUILT(bt_rng_, "set by the host that starts the battle, the same on a rebuild") \
    SAVED(bt_attrition_ids_, state) \
    SAVED(bt_attrition_kills_, state) \
    SAVED(bt_lead_, state) \
    SAVED(bt_lead_participant_, state) \
    SAVED(bt_raid_, state) \
    SAVED(bt_sw_killed_, state) \
    SAVED(bt_used_, state) \
    SAVED(bt_have_ds_, state) \
    DERIVED(hg_spawn_off_, "scratch inside one call or tick") \
    REBUILT(bt_team_swap_, "made by the map load in setup(), the same on a rebuild") \
    SAVED(bt_ds_pos_, state) \
    SAVED(bt_ramps_, state) \
    DERIVED(oi_anim_plays_, "scratch inside one call or tick") \
    SAVED(oi_respawns_, state) \
    SAVED(oi_global_frame_, state) \
    SAVED(sabers_, state) \
    SAVED(arrivals_, state) \
    DERIVED(state_io_version_, "set only while read_part reads") \
    REBUILT(hg_holster_types_, "made by setup() from the battle inputs, the same on a rebuild") \
    REBUILT(hg_mode_, "the battle mode pointer, set again by setup / the first tick") \
    SAVED(contents_, part) \
    SAVED(production_, part) \
    SAVED(hg_order_cost_, state) \
    REBUILT(ai_build_time_mult_, "set by BattleAi when it attaches (rebuilt with the battle)") \
    SAVED(sk_garrisons_, state) \
    DERIVED(sk_redelivered_, "records of the last tick for the host (cleared at the next tick start)") \
    SAVED(hg_upgrade_objs_, state) \
    REBUILT(ga_upgrades_, "set by the host that starts the battle, the same on a rebuild (a rebuild keeps the saved objects)") \
    REBUILT(ga_spawn_, "set by the host: on for a new battle, off for a rebuild; read only by setup()") \
    SAVED(hg_orphans_, state) \
    SAVED(prod_used_, state) \
    SAVED(hangars_, part) \
    REBUILT(hangar_desc_, "made by setup() from the battle inputs, the same on a rebuild") \
    REBUILT(hg_land_bones_, "made by setup() from the battle inputs, the same on a rebuild") \
    SAVED(engine_loops_, part) \
    DERIVED(engine_loop_requests_, "records of the last tick for the host (cleared at the next tick start)") \
    SAVED(hg_tech_bonus_, state) \
    SAVED(hg_upgrades_, state) \
    SAVED(hg_launches_, state) \
    SAVED(hg_used_, state) \
    DERIVED(tick_, "the tick context; read_part resumes it (resume_tick_context)") \
    DERIVED(svc_graph_, "built on the first tick") \
    DERIVED(svc_owner_, "built on the first tick") \
    REBUILT(svc_mode_, "set by the host that starts the battle, the same on a rebuild") \
    REBUILT(svc_parallel_min_, "set by the host that starts the battle, the same on a rebuild") \
    REBUILT(svc_streams_, "set by the host that starts the battle, the same on a rebuild") \
    DERIVED(svc_check_, "made with the graph")

#ifdef ALAMO_SIM_HAVE_ANIM
#define ALAMO_TACTICAL_BATTLE_LEDGER_ANIM(SAVED, REBUILT, DERIVED) DERIVED(own_clone_models_, "made on first use")
#else
#define ALAMO_TACTICAL_BATTLE_LEDGER_ANIM(SAVED, REBUILT, DERIVED)
#endif
#ifdef ALAMO_SIM_HAVE_AI_TACTICAL
#define ALAMO_TACTICAL_BATTLE_LEDGER_AI(SAVED, REBUILT, DERIVED) SAVED(damage_tracking_, part)
#else
#define ALAMO_TACTICAL_BATTLE_LEDGER_AI(SAVED, REBUILT, DERIVED)
#endif
// clang-format on
