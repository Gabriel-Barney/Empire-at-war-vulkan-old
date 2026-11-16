# STY1 lane B: story reward semantics (questions for re-analyst)

Dispatcher: the story reward switch at 0x456970 (STORY.md §6.1). Handlers already named by string xrefs: SET_PLANET_RESTRICTED
0x461850, FORCE_RESPAWN 0x45f1d0, DISABLE/ENABLE_REINFORCEMENTS 0x45eca0, BOMBARD_DELAY / REMOVE_CORRUPTION / RESTRICT_* in the
0x456970 range (`docs/re/ghidra/strings_xrefs.csv`, strings at 0x86e180..0x8721xx). Write the answer as behaviour in words to
`docs/re/STORY_REWARDS_GALACTIC.md` (new): one section per reward: enum code, parameters, what state it writes, who reads that
state and what the reader then refuses / changes, failure messages, defaults and save-state. Separate read from inferred. No code.

Shipped parameter shapes (data): SET_PLANET_RESTRICTED planet,0|1; PLANET_FACTION planet,faction; MOVE_FLEET from,to,
FILTER_ENEMY_ONLY|FILTER_FRIENDLY_ONLY; DISABLE_REINFORCEMENTS 0|1 [faction] or "UNDERWORLD 1"; SET_PLANET_SPAWN and
SET_SANDBOX_OBJECTIVES are in no shipped XML.

## Galactic
1. SET_PLANET_RESTRICTED: what flag does it write (planet field), and which readers check it: fleet movement into / out of it
   (human, AI, both?), invasion, build, GUI selection, hyperspace routes, AI target choice? Is the restriction per faction or global?
2. PLANET_FACTION (planet, faction): sets ownership (does it run the full change-owner path with GALACTIC_PLANET_OWNER events, spawn /
   destroy structures, fleets) or only writes the owner field? Planet resolved by map name or type name?
3. FORCE_RESPAWN (hero type, [planet]?): which parameters; where is the hero placed (planet choice, owner = plot player?), what if
   the hero is alive / queued for respawn; does it skip the respawn timer?
4. REMOVE_UNIT: parameters, owner (plot player or all), which objects (all of that type everywhere / at a planet / fleet), does it
   schedule a hero respawn or free population / raise destruction events?
5. REMOVE_POWER_FROM_ALL / NEW_POWER_FOR_ALL: parameters, what "power" is (the galactic Power / ability slot of the player? per-faction
   override?), where it is stored and read.
6. MOVE_FLEET: params and FILTER_* values (which fleets: owned by plot player, enemy of, friendly), destination handling, does it
   teleport or start travel, merge, battle on arrival?
7. DISABLE_DIRECT_INVASION: flag written, readers (which command refuses), per player / global.
8. SET_PLANET_SPAWN: parameters and effect (even if unshipped).
## Tutorial
9. LOAD_CAMPAIGN (what loads, when: immediately or at end of frame, how mode switches), TUTORIAL_PLAYER (what it sets: player faction
   for the tutorial's local player?), FINISHED_TUTORIAL (name -> persisted "tutorial done" flag? where?), ENABLE_EVENT / DISABLE_EVENT
   (GUI event lockout names such as TUTORIAL_CLICK_GUI b_retreat, TUTORIAL_ALL, b_fast_forward: how readers test them), SKIRMISH_RULES,
   RESET_GALACTIC_FILTERS, SET_SANDBOX_OBJECTIVES.
## Underworld
10. ENABLE_INVASION (planet, 0/1), RESTRICT_SABOTAGE (planet), RESTRICT_CORRUPTION (planet), DISABLE_INVASION, ENABLE_FLEET_COMBINE,
    ENABLE/DISABLE_BOUNTY_COLLECTION (enum 0x7C clears), ENABLE_CAMPAIGN_VICTORY_MOVIE: flags written, defaults at campaign start,
    readers and exactly what each refuses (invade / sabotage / corruption action by whom, fleet merge command, auto-resolve bounty
    payout, victory movie played where).
## Tactical
11. DISABLE_REINFORCEMENTS (0x45eca0): parameters, player selection, effect on reinforcement / hyperspace-in calls, GUI and AI.
12. CHANGE_OWNER (object name, faction), DESTROY_OBJECT (object name): lookup (by name / type, in the current battle), effect,
    explosion / death event or silent removal, raises destroy notifications for story?
13. BOMBARD_DELAY (one parameter): unit (seconds?), what it overrides (the orbital bombardment ability's delay, L1_S4_REMAINING.md
    §OB), scope (battle only / persists).

Also list the enum code and handler address for every one of the rewards above.
