# mcx8 t12: retail behaviour of the `[inferred]` player, story and AI Lua bindings

Purpose: the corpus mods (vanilla FoC, Thrawn's Revenge and others) call these bindings, and the port's versions carry an
`[inferred]` marker. Each section gives what retail StarWarsG.exe does and whether the port matches. Sources: the
decompiled retail functions in `re_reference/ghidra/StarWarsG_decompiled.zip` (`Global.c`; read 2026-11-18), plus the
specs cited per section. Behaviour only, in words.

Conventions (LUA_API.md "Unit order bindings", SCRIPT_GAPS §0): "error" = script error helper 0x248190 (no value is
returned; the calling script ends at the end of its service pass), "warning" = 0x248620 (no value, script continues).
Lua class ids: PlayerWrapper 0x140a44260, GameObjectWrapper 0x140a43b18, AITargetLocationWrapper 0x140a44280,
TaskForce 0x140a441e0, PositionWrapper 0x140a44290, table 0x140a15790, string 0x140a157d0, number 0x140a157c0,
bool 0x140a157b0, WeightedTypeList 0x140a44448. Retail message texts have **two spaces** after the first full stop
("parameters.  Expected"); the port mostly writes one. The shipped error helper logs nothing (LUA_API.md), so the texts
are not visible to players; they are quoted for completeness.

## GameObjectWrapper.Get_Owner (0x5792e0) [H]

- Dead wrapper (object null): returns nothing.
- Owner id = the object's owner field (+0x58). If the object has the PLANET behaviour, the owner comes from the planet's
  political control record instead (0x3956c0, `{control, owner}`; OBJECT_RUNTIME.md "owner player").
- The id is looked up in the player list (0x294bc0); no such player: returns nothing (nil). Else the player wrapper.

Port: matches for planets (the galactic-sim planet owner). Edge: when the owner id is not a valid player, retail returns
nil while the port pushes a player wrapper for that id; push nil when `!w->valid(owner)` (or the id is negative).

## StringCompare (0x255690, LuaStringCompare) [H]

- Needs **at least** 2 arguments (more are ignored); fewer: returns `false` (no message).
- Arguments 1 and 2 must both be Lua strings (a number is not converted); otherwise `false`.
- Result: a boolean, true when `_stricmp` says equal (ASCII case-insensitive).

Port: differs only for 3 or more arguments: retail compares the first two, the port returns false. Change the count test
to `c.nargs < 2`.

## GiveDesireBonus (0x699ab0, GiveDesireBonusClass) [H]

Arguments: exactly 5 (message says 4; AI_GALACTIC.md §1.5). 1 player ("invalid type for parameter 1.  Expected
player."), 2 goal type name string ("parameter 2.  Expected string."), 3 an **AITargetLocationWrapper or a
GameObjectWrapper** ("invalid type for parameter 2.  Expected AI target location or game object."), 4 bonus number
("invalid type for parameter 3.  Expected number."), 5 seconds number ("invalid type for parameter 4.  Expected
number."). Then:
- goal type not found by name: error "Give_Desire_Bonus : unrecognised goal type %s.";
- a game object is turned into the AI target location of the player's AI context for the current mode (context +0x40,
  vf +0x40); none: error "Give_Desire_Bonus : No AI target location corresponds to game object of type %s.";
- creates a **blocking status** (class 0x811) holding goal type, target, bonus (float) and deadline =
  `NOW + (int)((float)FPS * (float)seconds)`, and returns it. Its poll (0x699eb0) finishes true when the bonus was
  added to a matching proposal, false at the deadline (AI_GALACTIC.md §1.5).

Port: differs.
- Return an AIBlockingStatus tied to the queued `PendingBonus` (finished true when applied, false at its deadline)
  instead of nil: `BlockOnCommand(GiveDesireBonus(..., 15, 5))` in PGTaskForce `FundBases` waits up to 5 s in retail and
  returns at once in the port.
- Accept an AITargetLocationWrapper as argument 3; an unknown goal type name should be the error above.
- (Messages) the port numbers the last three type errors 3 / 4 / 5; retail says 2 / 3 / 4.

## Declare_Goal_Type [H]

Not registered by the exe: no registration string in `Global.c` (registrations such as "StringCompare" and
"Check_Story_Flag" are there), no row in the string cross-references. In retail the global is nil, so
`if Declare_Goal_Type then` in PGInterventions.lua is false and nothing is called.

Port: differs in form only: the port registers a no-op global, so the guard is true and the no-op runs. Same visible
result for the shipped scripts; to be exact, do not register it (leave the global nil), so a mod that tests it or
defines its own sees the retail state.

## BudgetWrapper.Flush_Category (0x6bce70) [H]

- Exactly 1 argument ("BudgetWrapper::Flush_Category -- Invalid number of parameters.  Expected 1, got %d."), a string
  ("... -- invalid type for parameter 1.  Expected string").
- The name goes through the goal-category enum (0x140b31088); unknown: error "BudgetWrapper::Flush_Category --
  unrecognized goal category %s.".
- Acts **at once** on the budget (goal system +0x190) of the AI that owns the wrapper (wrapper +0x28 chain), not on a
  global: 0x612cb0 finds the category in the budget's hash map and sets its available credits (+0x44) to 0
  (AI_GALACTIC.md §1.8). Unknown category in the map: nothing. Returns nothing.

Port: matches in effect (queued, drained after the plan's service in `galactic_ai.cpp`, with the plan's player as
fallback). Differs on an unknown category name: retail errors (script ends after the pass), the port queues it and
ignores it; add the error when the category name does not resolve.

## Evaluate_In_Galactic_Context (0x69e1b0, EvaluateGalacticContextClass) [H]

(A1_READINGS.md §38.1 has the full order; re-read here.)
- First: the mode stack (newest first, 0x28aa00) must have a mode under the current one. None, which includes **any
  plain galactic game** (the galactic mode is the bottom of the stack): **warning** "Evaluate_In_Galactic_Context: This
  function may only be used in tactical games spawned from campaign mode.", no value.
- Then exactly 2 arguments, 1 a known perception function ("Unrecognised perception function %s."), 2 a player
  ("Expected player object."); the player's galactic AI (0x4b06d0(ai, 0)) must exist ("AI player %s has no galactic
  AI.", which prints the AI player type's name, not the faction name).
- Target = the galactic planet of the current battle; no battle record: no value, no error. Evaluate failure: "Evaluate_Perception:
  Error evaluating perception function %s.", no value. Success: one number.

Port: differs.
- In a galactic mode the port evaluates the galactic perception with no target (the `[port]` note in LUA_API.md, "Mode
  gates"); retail gives the warning and returns nothing. Make the no-parent-galaxy case the warning in every mode.
- The warning text: use the retail sentence above (the port writes "-- only valid in tactical games spawned from a
  campaign.").
- With a parent galaxy (`evaluate_in_parent_galaxy`) the port matches, except the "no galactic AI" message names the
  faction instead of the AI player type.

## Perception Target binding (EvaluatePerception 0x699430, argument 3) [H, from LUA_API.md "EvaluatePerception"]

Retail: argument 3, when present, must be an AITargetLocationWrapper (binds its target, vf +0x28) or a GameObjectWrapper
(a planet binds its planet perception id, data +0x354; another object binds its name, 0x4dc3a0). Neither, or dead:
error, no value. Only with 2 arguments is no Target bound.

Port: differs. `perception_target` reads game objects only, so an AITargetLocationWrapper (the plans' `AITarget`)
evaluates with no target, and a dead or wrong-type argument 3 evaluates untargeted instead of returning nil. Resolve an
AI target wrapper to its planet row, and return nil (script error) for an argument 3 that is present but not a live
object or target.

## WeightedTypeListClass.Parse (0x68df70) [H; L1_S4_REMAINING.md §1.8]

- At least 2 arguments ("WeightedTypeListClass -- Not enough parameters"); both tables ("... -- Invalid parameters").
- Clears the list, then for each index i of the names table:
  - i past the end of the weights table: error "WeightedTypeListClass -- Types and Weights are mismatched!" and
    **stop** (entries already added stay);
  - weight not a number: error "WeightedTypeListClass -- Invalid Entry: %d, for Weight list.!" (i counted from 0), the
    entry is skipped and the loop continues;
  - otherwise the entry (weight, name) is appended and the name resolved, in order, as a GameObjectCategoryType mask
    (enum 0x140b31020), else an AI unit-class enum (0x140b31068) [M: which enum], else as a type-name list (0x4c5360);
    nothing found: error "WeightedTypeListClass -- Unable to find type or category: %s!" (the entry stays) [M].
  - A non-string name with a number weight is not checked (retail would dereference an empty string object) [M].
- Returns nothing.

Port: differs. The port stores names raw and gives a missing or non-number weight 0. Retail stops at the first missing
weight (with the mismatch error), skips an entry whose weight is not a number (error), and resolves names at Parse time
(category first, then type), erroring on unknown names. Resolve names to category mask / type at Parse, and apply the
two weight rules and the three errors.

## EvaluateTypeList (0x69ade0, EvaluateTypeListClass; workers 0x69afd0 galactic, 0x69b670 tactical) [H for the flow, M for the leaves]

- Exactly 3 arguments; 1 a player ("Evaluate_Type_List: Invalid parameter type for parameter 1. Expected Player
  Wrapper.").
- 2 must be an **AITargetLocationWrapper**. Anything else (a game object included): returns nothing, **no message**. A
  target whose AI target is gone: error "Evaluate_Type_List: AI target location is defunct.".
- 3 a WeightedTypeList ("... parameter 3. Expected weighted type list.").
- Each list entry is bound as a perception parameter: a type entry as `Parameter_Type`, a category entry as
  `Parameter_Category`. A bitmask leaf is evaluated; bits 0..T-1 belong to the type entries in list order, then one bit
  per category entry. Result: a table of booleans, one per entry in list order.
- Galactic (target's mode kind 0): the target must be a planet target, else error "Evaluate_Type_List: parameter 2
  should be a planet target.". Paths `Target.EnemyForce.HasGroundUnitsBitfield` and
  `Target.EnemyForce.HasSpaceUnitsBitfield` (token ids 0x140a43dc0 / 0x140a43dbc, names from their registration), ORed.
  Self = argument 1.
- Tactical: path `Target.Location.EnemyUnitsBitfield` when the target has a game object (vf +0x30), else
  `Target.EnemyUnitsBitfield` (tokens 0x140a43e08 `Location`, 0x140a43ee4 `EnemyUnitsBitfield`).
- The leaves (e.g. 0x6da230) walk the planet's object lists, keep objects passing the force-side filter (enemy of Self)
  and expand fleets to their members, then test each against the bound types and categories [M: leaf bodies only
  skimmed].

Port: differs.
- Only **enemy** units (relative to argument 1) count; the port counts any object at the target, the target itself
  included.
- Argument 2 is an AI target wrapper (HeroPlanAttach's `Get_Target_Weight` gets FindTarget results); a game object
  gives nil silently, a defunct target the error; in galactic a non-planet target errors.
- Matching is by the list's resolved category mask / type (see Parse), not by type-name or CategoryMask-token strings;
  the "All" special case has no retail basis unless "All" resolves as a category [M].

## LuaGetTime.Galactic_Time (0x5460a0) [H; BKG1_RE_ANSWERS.md b0102]

- Finds the galactic mode (0x28a950(GMM, 0)); none: returns **nothing**.
- Else `(double)((float)galacticModeFrame * spf)`, spf = 0.033333335f (float 1/30), multiplied in float and then widened.

Port: differs. (1) It returns `frame / 30.0` in double; use the float form (`StoryModel::t9_seconds(frame, 1.0f/30.0f)`
already computes it): frame 150 gives 5.000000477 in retail, 5.0 in the port. (2) Without a galactic sim it returns
the VM time; retail returns nil.

## GameObjectWrapper.Get_Is_Planet_AI_Usable (0x578d40) [H]

- Dead wrapper: nothing. Object without the PLANET slot (+0x335 = -1): error "GameObjectWrapper::Get_Is_Planet_AI_Usable
  -- object %s is not a planet." (the type name), no value.
- Else a boolean = `Is_Planet_Revealed` (0x3fa420; GALACTIC_REVEAL_CORRUPTION.md §1.1): true in multiplayer; false
  without planet data or with the hidden byte (+0x2E0) set; true with galactic auto-reveal; else the planet's revealed
  byte (+0x2C9). It takes **no player**: the local human's reveal state, also for the AI (keep this quirk). Destroyed is
  not tested.

Port: differs. It returns "not hidden and not destroyed". Return `sim->is_planet_revealed(row)` (already the 0x3fa420
semantics), drop the destroyed test, and give the error for a non-planet object instead of false.

## GameObjectWrapper.Can_Land_On_Planet (0x5742b0) [H]

- Dead wrapper: nothing. Exactly 1 argument ("GameObjectWrapper::Can_Land_On_Planet -- invalid number of parameters.
  Expected 1, got %d."), a GameObjectWrapper ("... invalid type for parameter 1.  Expected game object.").
- false unless all hold: the argument's object is alive and is a planet (+0x335 set); **this** object has the TRANSPORT
  slot (+0x337 set: a galactic land unit or company, not a fleet); the planet's surface is accessible (0x375220, planet
  type and planet); the planet's terrain does not forbid this unit's representative type (0x373490 with 0x3973b0's
  company type; AI_GALACTIC_2.md "veto if ... forbidden on that terrain").
- When all hold: true iff the planet's ground count (0x3f9530, surface-list entries not being removed, any owner) is
  **below** `Max_Ground_Forces_On_Planet` (0x140b168c8, 10). (If the planet object has no PLANET behaviour at that
  point: nothing.)

Port: differs. Replace the port's rules (ground unit or fleet with a ground member; not hidden; not destroyed) with
retail's: self must be a galactic land unit itself (a fleet gives false), add `planet_restricts(row, type)` and the ground
count `< Max_Ground_Forces_On_Planet` cap, and drop the hidden / destroyed tests (GalacticFreeStore pairs it with
`Get_Is_Planet_AI_Usable`, which does the reveal test).

## Check_Story_Flag (0x6a2f90, CheckStoryFlagClass) [H; STORYB_T10_TRIGGER_AI.md §3.1, STORY_REWARDS_GALACTIC.md TRIGGER_AI]

- Exactly 4 arguments; 1 player, 2 string (messages as the port has them, two spaces).
- 3: a GameObjectWrapper gives its object (dead: error "Check_AI_Story_Flag -- target specified is dead; unable to check
  flag."); an AITargetLocationWrapper gives its game object (none: error "Check_AI_Story_Flag -- target %s is not a game
  object; unable to check story flag."; target gone: the "dead" error); anything else (nil) = no target.
- 4 must be a Lua boolean: else error "Check_AI_Story_Flag -- invalid type for parameter 4; expected boolean.".
- The flag map is the player's AI context **for the current mode** (0x4b06d0); none: error "Check_AI_Story_Flag --
  could not locate tactical AI manager.". Key = (target or none, flag name **as given, case kept**). Absent = false; a
  true flag with argument 4 true is written back to false. Returns the boolean.

Port: differs.
- Argument 4: check it is a boolean (the port takes any value's truth).
- Argument 3: accept an AITargetLocationWrapper (its game object) and give the two errors; the port treats a target
  wrapper as no target, so a check with a target wrapper misses a planet-keyed flag.
- The port's key upper-cases and trims the name; retail compares the name exactly.
- (Scope, [M] impact) retail keeps one map per player AI mode context, so a galactic flag is not seen in a tactical
  battle; the port keeps one map per story model.

## Add_Objective (0x69df00, LuaAddObjectiveClass) [H]

- Exactly 2 arguments ("Add_Objective -- invalid number of parameters.  Expected 2, got %d."), 1 string, 2 Lua boolean
  (messages as the port's). No current game mode: error "Add_Objective -- no game mode currently playing.".
- Adds to the **current game mode's** objective list (mode +0xa0, 0x326460): text id, its text looked up once
  (GALACTIC_DIALOGS.md), status 0, the boolean, and a serial number from a per-list counter.
- The boolean picks the section of the objectives panel (0x2fdef0): **true = a "battle info" line** listed under
  `TEXT_TEST_BATTLE_INFO_HEADER`, numbered within that section; **false = a numbered objective** under
  `TEXT_TEST_OBJECTIVE_HEADER` (numbered by its serial, with the completed / failed marks from the status). The engine's
  own planetary-bonus lines are added with true.

Port: differs: pass the boolean through (`m->add_objective(text, boolean)`) and show true entries as battle info, false
as objectives (the port forces true). The "no game mode" error fits the port's no-story path.

## Add_Radar_Blip (0x6a6ac0, AddRadarBlipClass) [H]

- **Galactic mode** (mode kind 0): exactly 1 argument ("Add_Radar_Blip -- invalid number of parameters.  Expected 1, got
  %d."), a live planet GameObjectWrapper ("Add_Radar_Blip -- invalid type for parameter 1.  Expected planet."); it turns
  the planet's highlight on (0x3d01d0, on, duration -1). Remove_Radar_Blip(planet) turns it off.
- **Tactical**: exactly 2 arguments ("... Expected 2, got %d."). Argument 1 is anything a position can be taken from
  (0x4990b0: AI target, game object, task force, position wrapper; else error "Add_Radar_Blip -- could not deduce a
  position from parameter 1."); a game object is also stored so the blip can follow it. Argument 2 a string ("invalid
  type for parameter 2.  Expected strng." [sic]), hashed (0x215a30).
- The add (0x2de700) always **appends** a new blip; there is no lookup of an existing name, so the same name twice gives
  two blips. `Remove_Radar_Blip(name)` (0x2ff240) removes only the **first** (oldest) blip with that name hash.

Port: differs.
- Same name: keep both blips (append); Remove_Radar_Blip removes the oldest one with that name, not "replace".
- Argument 1: also accept an AI target, a task force or a position (the port needs a live game object).
- Galactic mode: the 1-argument planet form (highlight on), which the port rejects with the 2-argument count error.

## _FindStageArea (0x6990f0, FindStageAreaClass) [H; A2K_T02_RE_ANSWERS.md Q1]

- Argument 1 a player (else error "FindStageAreaClass -- Expected a player object for parameter 1"); 2 an
  AITargetLocationWrapper or a GameObjectWrapper (else "FindStageAreaClass -- Expected a AITargetLocationWrapper/
  GameObjectWrapper for parameter 2"); a game object resolves to its parent planet's AI target (none: "FindStageAreaClass
  -- Couldn't find a parent planet for target"); 3 an optional task force giving the level (its goal's Reachability; 0
  without).
- Return: the first stage of the reachability list at that level (level 4: the target; > 5 or empty: nil). **An AI target
  argument returns an AI target wrapper; a game object argument returns the stage's game object.**

Port: differs in the return type: it always returns the stage planet's game object. When argument 2 was an
AITargetLocationWrapper, push the stage as an AI target (`push_target`). Also the three argument errors (the port returns
nil silently). Stage choice itself matches (galactic_ai.cpp `lua_stage_area`).
