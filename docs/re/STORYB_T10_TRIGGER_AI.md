# storyb t10: the story reward TRIGGER_AI (static read, 2026-11-17)

Question (roadmap storyb t10, tier 2): what does the story reward `TRIGGER_AI` do in retail, what do its parameters
mean, how does the AI receive it, and why do the AI-driven galactic events (`INTERVENTION_*`, `SANDBOX_EVENT_*`) stall
in the port?

Binary: FoC Steam x64 `StarWarsG.exe`. Addresses are RVAs (image base `0x140000000`). Static read of the decompiled
export (`re_reference/ghidra/StarWarsG_decompiled.zip`, `Global.c`) plus the shipped XML and Lua
(`extracted/*_data_text.zip`); no ReVa session, no in-game probe. This extends `STORY_REWARDS_GALACTIC.md` §5.x and
corrects one conclusion there (who fills Param2, see §2).

## Results

| Item | Retail | Port today |
|---|---|---|
| Handler | Inline case `0x1f` of the reward dispatcher `0x456970`, run at Fire step 5 (`STORY.md` §1.5), synchronously when the event fires [H] | `StorySession` reward branch `TRIGGER_AI` (`engine/apps/sim/src/story_session.cpp`, about line 1235) |
| What it does | Sets one boolean flag to true in the AI of the faction's player. Nothing else: it starts no plan, goal or perception pass and sends no notification [H] | Raises the flag on every plot set's `StoryModel` |
| Param1 | Flag name, compared byte for byte (case-sensitive) [H] | Flag name, upper-cased and trimmed |
| Param2 | Faction name; selects the AI player. Required [H] | Same |
| Param3 | Optional planet **type** name; the flag is keyed to the first live object of that type [H] | Same lookup; the planet's object id |
| Missing Param1 or Param2 | Silent return, nothing written [H] | Same |
| Unknown faction, no player, player without AI | One log line each, nothing written [H] | Unknown faction logs; "no AI" is not checked |
| Param3 given but not found | Log line; the flag is still set, with no target [H] | Same |
| Immediate or queued | Immediate write. Readers poll it: `Wait_On_Flag` once a second, perception leaves at every evaluation [H] | Immediate |
| Readers | `Check_Story_Flag` (`0x6a2f90`, optional clear) and the perception token `StoryTrigger` (player root `0x6e08e0`, planet root `0x65eb10`, never clears) [H] | `Check_Story_Flag` works only in story plot VMs. AI plan VMs have no story attached, so it returns false there; the `StoryTrigger` hook is never set, so the token is 0 |
| Who fills Param2 / Param3 | XML gives Param2 for 17 of the 37 shipped events; the AI plan scripts fill Param2 (and Param3 for 4) for 17 more with `Set_Reward_Parameter` before `plot.Activate()`; 3 events never get a Param2 and are dropped in retail too [H, data] | AI plan VMs cannot reach the plot (`Get_Story_Plot` returns nothing), so the plan script errors before it fills anything |

**Why the port stalls:** the reward itself is applied, but the AI side never sees the story. An intervention or
sandbox-event plan calls `Get_Story_Plot`, fills the reward parameters, calls `plot.Activate()` and then waits on the
flag. In the port's AI VMs the first call already fails, and `Check_Story_Flag` could not see the flag anyway (§4).

## 1. The handler (case `0x1f` of `0x456970`)

Steps, in order [H]:

1. Log "STORY REWARD - Triggering AI".
2. Param1 (event +0x50) empty, or Param2 (event +0x70) empty: return. No log, no default (the plot player is not used).
3. Find the faction by Param2's name (`0x331c40` on the faction list `0xb310b8`). Not found: log "...faction %s not
   found, trigger failed", stop.
4. Find the player of that faction in the player list (`0x294d30`). None: log "...cannot find player of faction %s,
   trigger failed", stop.
5. Take the player's AI system (player +0x360). Null (a player without AI, for example a human [M]): log "...player
   of faction %s has no AI, trigger failed", stop.
6. Target = none (the target pointer starts as 0 at the top of the dispatcher). If Param3 (event +0x90) is not empty:
   look up the object type by that name (`0x407e00`), then the live objects of that type in the current mode's object
   manager (`0x2a87d0`, mode +0x18). If the list is empty: log "STORY REWARD TRIGGER AI- Unable to find planet %s" and
   keep target none. Otherwise target = the first object in the list. [L] A Param3 that names no type at all was not
   followed (the log then reads the name through a null type).
7. Write the flag (`0x4b0d20`): walk the AI system's mode contexts (vector at AI +0x38 / +0x40) for the one whose
   mode (`0x5f55e0`) is the current game mode (global `0xb15418`). None, or that context has no perception system
   (context +0x38 null): nothing happens, no log. Otherwise `0x4e0190` stores the byte 1 in the system's flag map.

The flag map [H]: an unordered map at perception system +0x78 (list head +0x80, buckets +0x90, mask +0xa8). Key =
(target object pointer or 0, flag name as a string). The hash is 64-bit FNV-1a over the name's bytes plus
(target XOR `0xdeadbeef`); key equality is target equality plus a `memcmp` of the names, so case matters. Value = one
byte. Setting a flag that is already true leaves it true; nothing counts.

Scope [H]: one map per AI player **per mode context** (galactic, and each tactical mode the AI has a context for).
The write goes to the context of the mode that is current when the event fires. [M] A TRIGGER_AI that fires while a
tactical battle is the current mode lands in that battle's context, where a galactic plan never reads it; whether
galactic plots fire events during a battle was not checked.

Nothing else in the binary writes this map directly: the only callers of `0x4e0190` are `0x4b0d20` (this reward) and
`Check_Story_Flag`'s clear [H for direct calls; an indirect call was not searched, M]. `0x4df960` (called from the
mode / AI init functions `0x4e3e30` and `0x6b9580`) clears the map and refills it from a saved list at system +0xb8
(entries of 0x30 bytes); read as init and save-game load [M].

## 2. Parameters in the shipped data

37 TRIGGER_AI events in 24 files in FoC (`Data/XML/INTERVENTION_*.XML`, `SANDBOX_EVENT_*.XML`); EaW ships the same 24
files with the same 37 events (74 uses in 48 files in total). No campaign plot uses TRIGGER_AI. The plots are loaded
**suspended** in the human player's sandbox plot lists (`STORY_PLOTS_SANDBOX_*_<FACTION>.XML`,
`STORY_PLOTS_UNIVERSAL_TRIGGERS_*.XML`) and are started by AI plan scripts. The plan scripts are EaW's
`Data/Scripts/Interventions/*.lua` (the FoC text zip has no such folder; FoC reads them through the EaW data fallback
[M]); their goal categories are `Interventions` and `Sandbox_Events`.

The pattern, using `SANDBOX_EVENT_CONTRIVED_ATTACK.LUA` (run by the enemy of the player who gets the dialog):

1. `plot = Get_Story_Plot("Sandbox_Event_Contrived_Attack.xml")` finds the human player's suspended plot.
2. `plot.Get_Event("Contrived_Attack_05").Set_Reward_Parameter(1, PlayerObject.Get_Faction_Name())` writes the AI's
   own faction into **Param2** (the index is 0-based, `STORY.md` 0.2). The XML gives only Param1, the flag name.
3. `plot.Activate()`, then `Story_Event(...)` notifications and the plan's own work.
4. `while not Check_Story_Flag(PlayerObject, "CONTRIVED_ATTACK_NOTIFICATION_03", nil, true) do Sleep(1) end` waits
   until the plot reaches `Contrived_Attack_05`, whose TRIGGER_AI sets the flag; then `plot.Suspend()`,
   `plot.Reset()`, release the forces.

`PGInterventions.lua` wraps step 4 as `Wait_On_Flag(name, location)`: the same loop with `clear` true, and after
`InterventionExclusionPeriod` (120 s) it unblocks goal proposal, but it keeps waiting.

Where Param2 and Param3 come from, per event (FoC = EaW):

| Source | Events |
|---|---|
| Param2 in the XML (`Rebel` or `Empire`) | Confront_Boba_02, Empire_Oppression_03, Fleet_Rescue_02, Pirate_Ambush_01 / 05, Pirate_Defection_00a, Pirate_Enlightenment_00 / 02 / 03, Pirate_Enlightenment_b_01a / b_03, Rebel_Revolt_00a / 02, Refugee_Escort_00 / 02 / 03, Show_of_Strength_02 (17) |
| Param2 from Lua `Set_Reward_Parameter(1, PlayerObject.Get_Faction_Name())` | Accumulate_Credits_03, Build_Generic_Structure_01 / 04, Build_Units_03, Conquer_Planet_03, Hunt_Capital_Ships_03, Indigenous_Revolt_03, Scout_01 / 05, Upgrade_Space_Station_03, Windfall_01, Contrived_Attack_05 / 08, Hero_Gathering_06, Planet_Ownership_Flip_01, Trap_Galactic_04 / 05 (17) |
| Param3 from Lua `Set_Reward_Parameter(2, Target)` | Conquer_Planet_03, Indigenous_Revolt_03, Upgrade_Space_Station_03, Trap_Galactic_04 (4); their scripts wait with `Target` as the location, except Trap_Galactic (below) |
| No Param2 from anywhere: dropped by step 2 in retail | Pirate_Defection_02, Rebel_Sabotage_02, Indigenous_Revolt_08 (3). Their scripts' `Wait_On_Flag` never returns. Indigenous_Revolt's script fills Param2 / Param3 on `_07` (a REMOVE_STORY_GOAL event) instead of `_08` |

How `Target` becomes Param3 [H, from `B0173_RE_ANSWERS.md` Q1 and the converter `0x73e610`]: in a plan script
`Target` is a GameObjectWrapper of the goal's planet; the parameter converter turns a game object into its type's
name, so Param3 is the planet's type name, and step 6 finds the planet again as the first live object of that type.

`STORY_REWARDS_GALACTIC.md` §5.x said the Param1-only events are dropped; the binary rule is right, but the AI plan
scripts fill Param2 at run time before the plot is activated, so only the 3 events in the last row are dropped.

## 3. The readers

### 3.1 `Check_Story_Flag(player, name, target, clear)` (`0x6a2f90`) [H]

- Exactly 4 arguments, else error "Check_AI_Story_Flag -- invalid number of parameters. Expected between 4, got %d."
  Errors use the script error helper `0x248190`: nothing is returned and the calling script ends at the end of its
  service pass (`LUA_API.md`).
- Argument 1 must be a player, argument 2 a string, argument 4 a boolean (each with its own error).
- Argument 3: a GameObjectWrapper gives its object; a dead one errors "target specified is dead; unable to check
  flag.". An AITarget gives its game object; one without an object (a region) errors "target %s is not a game object;
  unable to check story flag.". Anything else (nil) gives target none.
- It takes the player's AI system (+0x360) and the context of the current mode (`0x4b06d0`). None: error "could not
  locate tactical AI manager.".
- It looks the key (target, name) up in that context's perception system (+0x38) with `0x4dd530`; absent = false. If
  the flag was true and `clear` is true, it writes false back (`0x4e0190` with 0). It returns the byte as a boolean.

So a flag set with a planet (Param3) is seen only by a check with that same planet, and a flag set without Param3 only
by a check with nil. Example from the data: `SANDBOX_EVENT_TRAP_GALACTIC.LUA` sets Trap_Galactic_04 with Param3 =
`Target` but its main loop checks `TRAP_GALACTIC_NOTIFICATION_01` with nil, which never matches in retail; the
planet-keyed flag is read by the perception below and cleared by the script's explicit `Check_Story_Flag(..., Target,
true)`.

### 3.2 Perception token `StoryTrigger` [H]

- Player root (`0x6e08e0`, `Variable_Self.StoryTrigger {Parameter_Name = "..."}`): reads `Parameter_Name`; absent:
  the token fails. Otherwise 1.0 if `0x4dd530(evaluator +0xa0, name, target none)` is true, else 0.0. Never clears.
- Planet root (`0x65eb10`, registered in the planet evaluator `0x65f7b0`): the same, with target = the evaluator's
  object (evaluator +0x98, the planet). Used by `SANDBOXEVENTEQUATIONS.XML` `Helper_Trap_Land_Check_Galactic_Flags`
  (`Variable_Target.StoryTrigger {Parameter_Name = "TRAP_GALACTIC_NOTIFICATION_01"}`, evaluated in galactic context).
- [M] evaluator +0xa0 is the same perception system that the context +0x38 path reaches (the target-list reads in
  `AI_GALACTIC_TOKENS.md` use the same field), so both readers see the TRIGGER_AI map.
- Other shipped `StoryTrigger` names (`ACT_ONE_BEGIN_NOTIFICATION`, `ACT_THREE_BEGIN_NOTIFICATION` in
  `INTERVENTIONEQUATIONS.XML`; `ACT_2`, `ACT_3`, `ACT_4` and the `..._PIRATE_SPAWN_DISABLED` names in
  `STORY_EMPIRE_EQUATIONS.XML`) are set by no shipped TRIGGER_AI, so they are always 0.0 in retail [H, data].

## 4. What this means for the port

The reward branch in `story_session.cpp` is right in its checks. The gaps are on the AI side and in the key:

1. **AI plan VMs have no story.** `GalacticAiWorld`'s `PlanScriptRuntime::VmSetup.prepare`
   (`engine/ai/src/galactic_ai.cpp`, about line 2472) attaches the player world, the AI host and the markup host, but
   not `lua::attach_story_table` / `lua::attach_story` (`engine/lua/include/alamo/lua/story_model.hpp` 521 / 527).
   Only `story_session.cpp` attaches them, to story plot VMs. In a plan VM `Get_Story_Plot` therefore logs
   "[story] Get_Story_Plot ignored: no story attached" and returns nothing, the next `plot.Get_Event` is a Lua error,
   and the plan dies before `plot.Activate()`. The plots stay suspended, so no TRIGGER_AI ever fires. `Story_Event` from
   a plan is ignored the same way.
2. **The "no target" value differs between writer and reader.** The reward stores location 0 when Param3 is absent;
   `g_check_story_flag` (`engine/lua/src/bind_story.cpp`, line 190) looks nil up as -1. Even with item 1 fixed, the
   flags set without a planet (30 of the 34 events that reach the AI) would never match a `nil` check. Use one value
   (retail: target none).
3. **`StoryTrigger` is never fed.** `GalacticSimTokenResolver::Hooks::story_flag`
   (`engine/ai/include/alamo/ai/galactic_ai_tokens.hpp`, line 279) is not set in `galactic_ai.cpp` (line 52 sets
   `active_goals` only), so the token is always 0. The hook also has no target argument, which the planet root needs.
4. Smaller differences: names are upper-cased in the port (`story_model.cpp` `ai_flag_key`) but compared exactly in
   retail (shipped data uses one spelling everywhere, so this matters only for mods); the port stores a count and
   erases on clear (same truth value); retail drops the flag for a player without AI and keeps one map per mode
   context, the port keeps one map per plot model. Record the last two as `[port]` if kept.

**Entry point the port needs.** One call on the galactic AI, made by the hosts where `attach_story_world` is made
today (`engine/apps/game/src/game_session.cpp` line 373; in the headless runs, next to the `set_global_values` calls in
`engine/apps/headless/ai_run.cpp` and `save_run.cpp`, where a story session runs), for example
`GalacticAiWorld::attach_story(lua::StoryTable* table, lua::StoryModel* flags)`, which:

- adds `lua::attach_story_table(vm, table)` and `lua::attach_story(vm, flags)` to `VmSetup.prepare`, and applies them
  to plan VMs already built or pooled, so plan scripts get `Get_Story_Plot`, the `StoryEventWrapper` setters,
  `plot.Activate / Suspend / Reset`, `Story_Event` and `Check_Story_Flag` on the session's galactic plots (the table
  the story session already builds: `StorySession`'s plot table over the galactic sets, q6 t03);
- sets `Hooks::story_flag` to read the same flag store with target none, plus a target-aware form for the planet-rooted
  `StoryTrigger` (key = that planet's object id).

With that, the TRIGGER_AI branch in `StorySession` can stay as it is once its "no target" value matches the reader's.
Open for the implementer: AI plan VMs then hold story wrapper userdata, so their Lua save has to carry them as the story
VMs' does; and the order of the AI service and the story update inside a frame decides on which frame a plan sees the
flag (retail order not read here [L]).

## 5. How this was read

- Dispatcher case `0x1f` of `0x456970` in `Global.c`, with the prologue (target pointer starts at 0); `0x4b0d20`,
  `0x4e0190`, `0x4da590` (map insert, hash), `0x4dd530` (lookup), `0x6aee0` (string compare: `memcmp` plus length),
  `0x4df960` (clear and refill), `0x6a2f90` (Check_Story_Flag), `0x6e08e0` and `0x65eb10` (StoryTrigger leaves), the
  planet evaluator registration `0x65f7b0` (installs `0x65eb10`), the parameter converter `0x73e610`.
- Callers of `0x4e0190`, `0x4b0d20`, `0x4dd530` and `0x4df960` by text search of `Global.c` (direct calls only).
- Shipped data: every `<Reward_Type>TRIGGER_AI` event in both text zips with its `Reward_Param*`, matched against the
  `Get_Event(...)` / `Set_Reward_Parameter(...)` pairs and the `Check_Story_Flag` / `Wait_On_Flag` calls of
  `Data/Scripts/Interventions/*.lua`, and the `StoryTrigger` uses in `Data/XML/AI/PerceptualEquations`.
- Port: `story_session.cpp` reward branch, `bind_story.cpp` (`g_check_story_flag`, `g_get_story_plot`,
  `attach_story*`), `story_model.cpp` (`raise_ai_flag`, `check_ai_flag`), `galactic_ai.cpp` (VM setup, hooks),
  `galactic_ai_tokens.{hpp,cpp}` (`STORYTRIGGER`). No engine code was changed.

## Port status (storyb t10, 2026-11-17)

- Item 1 done: `GalacticAiWorld::attach_story(table, model)` (`engine/ai/include/alamo/ai/galactic_ai.hpp`). The hosts call
  it before `load_plans` (`game_session.cpp`, `ai_run.cpp`, `save_run.cpp`) with `StorySession::galactic_story_table()`
  and `first_story_model()`, so every plan VM gets `attach_story_table` and `attach_story` in `VmSetup.prepare`
  (Get_Story_Plot, the event setters, plot.Activate, Check_Story_Flag / Wait_On_Flag on the shared flag table).
- Item 2 done earlier (t10 first part): TRIGGER_AI with no Param3 stores -1, as the nil reader looks it up.
- Item 3 done for the player root: `Hooks::story_flag` reads the attached model's AI flag with target none and does not
  clear it (`test_story_trigger_token`). The planet-root `StoryTrigger` (target = the planet's object id) reads its own
  key since mcx8 t07 (the hook takes the location; `test_mcx8_t07_tokens`).
- Not checked yet: an Intervention plan running end to end in a sandbox campaign (the headless smoke runs tried
  could not load those campaigns), and the Lua save of a plan VM that holds story wrappers.
