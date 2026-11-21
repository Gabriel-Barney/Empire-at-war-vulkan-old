# mcx6 RE questions (Stage 9, box MCX3: story rewards, Lua bindings, script runtime)

Asked 2026-11-18 by lane mcx0 for group mcx6. The answers go into the spec named under each part, in words, with the RVAs
they rest on and each line tagged [H] (read) or [M] / [L] (inferred). Never copy decompiled code.

Anchors: the story reward dispatcher is 0x456970 (STORY_REWARDS_GALACTIC.md §0); the Reward_Type enum values are in its
§0.1 table; the reward parameters are Reward_Param1..N of the event (§0 "Common frame").

## Part A: story rewards (answers into docs/re/STORY_REWARDS_GALACTIC.md, new §1.10 onward)

The port has no handler for these retail rewards (engine/apps/sim/src/story_rewards.cpp, owner Missing). For each: the
parameters it reads (which Reward_Param, how converted: type name, planet name, faction, number), what it changes (which
object, player, planet or manager field), what it refuses (missing params, unknown names) and any save state.

1. SWITCH_SIDES (0x13). Thrawn's Revenge and Fall of the Republic use it in galactic plots. Which player changes faction,
   to which side; what happens to the local player's camera / GUI / AI player; is the AI re-seeded.
   answered: STORY_REWARDS_GALACTIC.md §1.10
2. FORCE_RETREAT (0x4e). Thrawn's Revenge uses it. Which units retreat from which battle or planet, in galactic or
   tactical mode, and through which retreat path (the normal retreat, or a direct move).
   answered: STORY_REWARDS_GALACTIC.md §1.11
3. SET_HEALTH (0x25): the objects it targets and the value's meaning (fraction or absolute).
   answered: STORY_REWARDS_GALACTIC.md §1.12
4. SET_SPAWN (0x42): §1.8 says P1 and P2 (atof) go to 0x531070, not traced. What does 0x531070 set and who reads it.
   answered: STORY_REWARDS_GALACTIC.md §1.13 (and §1.8: the SET_PLANET_SPAWN map reader)
5. SET_WEATHER (0x4b): what it sets (a tactical weather / environment choice for the next battle at a planet?) and how
   the battle-environment pick (CU8G_T02_ENV_PICK_RE.md) reads it.
   answered: STORY_REWARDS_GALACTIC.md §1.14
6. RANDOM_STORY (0x3f): STORY.md says it builds a random plot (0x4bfdc0). Its parameters, how it picks, which random
   stream it draws from (synced or not), and what the plot it builds contains.
   answered: STORY_REWARDS_GALACTIC.md §1.15
7. PICK_PLANET (0x12), STATISTIC_CHANGE (0x08), INFORMATION (0x06): what each does (or that it does nothing in FoC).
   answered: STORY_REWARDS_GALACTIC.md §1.16, §1.17, §1.18
8. ENABLE_OVERWHELMING_ODDS (0x63): the field it writes and its reader (the port treats it as having no effect).
   answered: STORY_REWARDS_GALACTIC.md §1.19
9. DISABLE_SPECIAL_STRUCTURE (0x40): TRU1_A_RE_ANSWERS.md covers the write; the reader of the flag it sets.
   answered: STORY_REWARDS_GALACTIC.md §1.20
10. The readers of RESTRICT_BLACK_MARKET (0x87, manager +?), RESTRICT_ALL_ABILITIES (0x85, list +0x2a0),
    ENABLE_SABOTAGE (0x82, +0x29a; readers 0x2e6770, 0x3cf2e0) and ENABLE_COMBAT_CINEMATIC (0x8a): §3.2 / §3.7 mark the
    readers [M]; read what each reader refuses or changes.
    answered: STORY_REWARDS_GALACTIC.md §1.21

## Part B: Lua bindings and script runtime (answers into docs/re/LUA_API.md and docs/re/GAME_SCORING.md)

11. LandTaskForceClass.Enable_Attack_Positioning (0x6eaa80; LUA_API.md line 998 has its two error strings) and
    Release_Reinforcements (0x6e8a70): what each does to the task force (fields written, who reads them), the argument
    checks, and the return value. The port treats both as no-ops (engine/lua/src/bind_ai_tactical.cpp). The space
    variant of Enable_Attack_Positioning (0x17d070) is believed a no-op: confirm.
    answered: LUA_API.md §"Task force: Enable_Attack_Positioning and Release_Reinforcements"
12. Script lookup: LUA_API.md says LUA_PATH defaults to `./?.lua;./?.lc`. Do the AI plan, FreeStore and Evaluator script
    loaders (not `require`) also try `.lc` after `.lua`, or only one extension? Does retail's `require` set
    `_LOADED[name]` before or after the chunk runs (stock 5.0.2 sets it after a successful run)?
    answered: LUA_API.md §"Script lookup and `require`"
13. Skirmish GameScoring: which player / world object the skirmish scoring script gets (GAME_SCORING.md), and when the
    engine raises `Player_Quit_Event` (callers 0x499420 / 0x4994c0, roles [M]).
    answered: GAME_SCORING.md §9 (§9.1 player and world, §9.2 Player_Quit_Event)

## Part C: battle composition (answer into docs/re/STORY.md, the WIN/LOSE_BATTLES row and a note below the table)

14. WIN_BATTLES / LOSE_BATTLES (0x13 / 0x14, match 0x455a50 / 0x466f40) test Param3 (FRIENDLY / ENEMY (_ONLY) _CONTAINS
    + CLASS_* list) against a composition the dispatch carries. After an **auto-resolved** battle (no tactical battle),
    what composition does the dispatch carry: the forces of each side as they were before the auto-resolve (from the
    conflict record), the survivors, or none (so a Param3 test never matches)? Name the dispatch site(s) and the list
    they pass. The port passes empty sides after auto-resolve (engine/apps/sim/src/story_session.cpp, BattleWon /
    BattleLost case). Also: for the Enter family (ENTER, LAND_ON, ...), what Param5 and Param7 mean (STORY.md marks
    them [L]).
    answered: STORY.md §5 (WIN/LOSE_BATTLES and Enter family rows; notes "Battle composition for WIN/LOSE_BATTLES" and
    "Enter family match")
