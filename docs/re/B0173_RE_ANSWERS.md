# b0173 RE answers: `Target` for a region goal and `LandTaskForceClass::Move_To`

Date: 2026-11-14. Binary: StarWarsG.exe (x64 Steam). Addresses are RVAs. Tags: [H] read directly in the
decompilation, [M] inferred from neighbouring code or an earlier doc, [L] guess.

Case: `AI_PLAN_EXPANSIONGENERIC_BUILDMDUITEM.LUA` (land plan, `IgnoreTarget = true`, one MDU in `MainForce`) for a goal
such as `Build_Rocket_Pod` (`Tactical_Location`), whose AI target is a land region (an AI hint zone,
`TacticalRegionAITargetLocationClass`, AI_TACTICAL §3.2). The script runs:

    BlockOnCommand(MainForce.Produce_Force())
    QuickReinforce(PlayerObject, AITarget, MainForce)
    BlockOnCommand(MainForce.Move_To(Target, MainForce.Get_Self_Threat_Max()))
    build_started = true
    BlockOnCommand(MainForce.Activate_Ability("DEPLOY", true))

Class tokens used below (LUA_API "Unit order bindings"): GameObjectWrapper 0x140a43b18 (object at +0x60, read by
0x5791e0), AITargetLocationWrapper 0x140a44280 (AI target at +0x38), TaskForceClass 0x140a441e0, PositionWrapper
0x140a44290 (x, y, z at +0x28 / +0x2c / +0x30), number 0x140a157c0, boolean 0x140a157b0. "Error" = 0x248190,
"warning" = 0x248620.

## Q1. How the Lua global `Target` is built (0x6bad60 -> 0x5f7090)

0x6bad60 only creates the plan object (0x5f6870), gives it the player, and calls 0x5f7090(plan, proposal). All the
work is in 0x5f7090 [H]:

1. Get the pooled script for the plan definition (0x244170) into plan +0x48. No script -> return false.
2. `aiTarget` = goal +0x58 (the goal is proposal +0x20). `object` = null.
3. If `aiTarget` is not null:
   - `object` = `aiTarget` vf+0x30 (the AI target's game object; null for a region target, LUA_API "Unit order
     bindings" table and the task premise [M: the region class's slot was not re-read here]);
   - create the AITargetLocationWrapper for (aiTarget, script) with 0x60e0a0;
   - only if `object` is not null: listen to the object's signals 0x1a and 0x10.
4. **Always** (target or no target, object or no object): `wrapper = 0x5751d0(object, script, 1)` and store it at
   plan +0x50. 0x5751d0 never returns null [H]:
   - an object whose flag byte +0x3a0 has bit 2 set is treated as null (a dying object) [M for the flag meaning];
   - it looks up the pair (object, script) in the wrapper cache 0x140b39380 and returns the cached wrapper, or creates
     a new GameObjectWrapper (0x5761a0, Lua id 0x802) and sets its object with 0x57ac10. With a null object 0x57ac10
     leaves wrapper +0x60 = null and registers no listener;
   - wrapper +0x70 = 1 (third argument; meaning not traced [L]).
5. If the script is fresh (script byte +0x122), call `Base_Definitions`.
6. **Unconditionally** set the Lua global `Target` = plan +0x50 (0x247000: push name, push value with 0x247700, raw
   set in the globals table). Then, **only when the AI target wrapper exists**, set `AITarget`.

So:

| Goal target | `Target` | `AITarget` |
|---|---|---|
| region (hint zone, space cell): AI target without an object | **a GameObjectWrapper userdata whose object is null** (+0x60 = 0). Not nil, not unset, not the AI target wrapper | the AITargetLocationWrapper for the region |
| unit / structure / planet: AI target with an object | a GameObjectWrapper for that object (null-object wrapper if the object is already dying) ; the plan listens to the object's 0x1a (owner changed) and 0x10 (destroyed) | the AITargetLocationWrapper |
| goal without an AI target (goal +0x58 null) | the same null-object GameObjectWrapper | **not written** by this function |

[H] for the table.

**Can a pooled script's old `Target` survive?** No. `Target` is written on every instantiation, after
`Base_Definitions`, so the previous plan's value is always replaced [H]. Only `AITarget` can keep an old value, and
only for a goal with no AI target at all (step 6); whether the pool clears globals when a script is returned was not
traced [L]. Because the wrapper cache is keyed by (object, script), every region plan run on the same pooled script
gets the same null-object wrapper for the key (null, script) [H]; this has no visible effect.

In Lua the value is a userdata, so `Target` is truthy and `if Target then` passes; `TestValid(Target)` is the test that
tells it from a live object [M: `TestValid` body not re-read here].

After the target object dies (signal 0x10) the plan's handler 0x5f7b00 raises plan event 10 and drops plan +0x50, but
the Lua global keeps pointing at the same wrapper, which by then has a null object [H for the handler; M for the
wrapper clearing its own object through the listener that 0x57ac10 registered].

## Q2. `LandTaskForceClass::Move_To(dest [, threat [, useZonePath]])` — 0x6ec410

Strings: "No destination set" 0x8c3b70, "Parameter 1 is not a valid destination" 0x8c3bc0, "Unexpected type for
parameter %d" 0x8c3c20.

**Parameter 1.** No argument at all -> error "LandTaskForceClass::Move_To -- No destination set for TaskForce %s",
returns nil. Otherwise the argument is class-tested four times (AI target wrapper, game object wrapper, task force,
position) and the destination point is chosen in this order [H]:

| # | Parameter 1 | Destination point | Result |
|---|---|---|---|
| 1 | AITargetLocationWrapper with an AI target (+0x38 not null) | the AI target's position, vf+0x68 (for a land region: the hint zone centre, AI_TACTICAL §3.2) | goes on to the block |
| 2 | GameObjectWrapper with a live object (0x5791e0 not null) | the object's position (+0x78, +0x7c, +0x80) | goes on to the block |
| 3 | TaskForce | its position, vf+0x170 (mean of the live members, (0, 0, 0) when empty, AI_TACTICAL §7.3.2) | goes on to the block |
| 4 | PositionWrapper | its x, y, z | goes on to the block |
| 5 | AITargetLocationWrapper whose AI target is null | — | **returns nil, no message** |
| 6 | **GameObjectWrapper whose object is null (dead object, or the region-goal `Target` of Q1)** | — | **returns nil, no message** |
| 7 | anything else: Lua nil, number, string, table, player, any other userdata | — | **error** "LandTaskForceClass::Move_To -- Parameter 1 is not a valid destination for TaskForce %s", returns nil |

Rows 5 and 6 are explicit: after all four conversions fail, the function returns silently if the argument was of the
AI-target-wrapper class or of the game-object-wrapper class, and only otherwise calls the error helper [H]. This is
the same rule as the space form (0x6e7940, AI_TACTICAL §7.3.2: "a dead object or an AI target with no location ->
nil with no message").

**Parameter 2 and 3** (read only for rows 1-4) [H]:
- If parameter 2 is a Lua number, it is the **threat tolerance** (float; default 1e18 when absent) and the next
  optional parameter is 3; if parameter 2 is not a number, it is itself tested as the next optional parameter.
- The next optional parameter, when present, must be a boolean: `useZonePath` (default false). Any other type ->
  error "LandTaskForceClass::Move_To -- Unexpected type for parameter %d." and nil.
- Both go to the block set-up 0x73c360(block, tf, point, threat, useZonePath, mode 0). The threat is stored in the
  block and handed to the group move; it only changes the route when `useZonePath` is true (A1_READINGS: zone route
  with tolerance = threat x 2; AI_TACTICAL §7.3 / §7.3.3). The script's `Get_Self_Threat_Max()` value is therefore
  stored but has no routing effect, because no third argument is passed.

**Return** (rows 1-4) [H]: a LandMovementBlockStatus (0x73baf0, Lua id 0x819) is created and set up. If it is already
finished right after set-up (0x73cab0: block byte +0x40, after one zone-path step when +0x55 is set), the function
returns **nil**; otherwise it returns the block (pushed on the return list 0x24a250), which `BlockOnCommand` polls
with `IsFinished`.

## Q3. This script with a region target, in retail

- `Target` is a GameObjectWrapper with a null object (Q1). `Move_To` takes row 6: **no script error, no warning, no
  block, no move order**. It returns nil. [H]
- `MainForce.Get_Self_Threat_Max()` is still evaluated first (Lua argument order), but its value is never read. [H]
- `BlockOnCommand(nil)` (shipped PGBASE.LUA): one `PumpEvents()` (one yield, i.e. one Planning service, 3 frames),
  then `if not block then return nil end`. [H, shipped Lua]
- The thread then sets `build_started = true` and calls `MainForce.Activate_Ability("DEPLOY", true)`: **the MDU
  deploys where it stands**. It does not walk to the hint zone centre. [H for the control flow]
- Where it stands is where `Produce_Force` / `QuickReinforce(PlayerObject, AITarget, MainForce)` put it: that call
  uses `AITarget` (the region wrapper, valid), so the landing point is chosen from the region, not from `Target`
  [M: `QuickReinforce` / `Reinforce` not re-read for this task].

The `Move_To(Target, ...)` line is thus a silent no-op in retail for every region goal of this plan (all nine
`Build_*` categories if they are `Tactical_Location`). It would only move for a goal whose AI target has an object.

## Q4. What a task-force method error does

The land form uses the same helper as the space form, 0x248190, and returns nil without unwinding the Lua stack, so
the Lua statement completes and the next lines of that resume run [H]. But an error is **not only a log line**:
0x248190 sets script byte +0x121 (with no logger installed in the shipped build that is all it does), and the script
service 0x247a90 tests +0x121 after each thread it resumes and stops resuming the script's threads once it is set
[H, re-read here]; LUA_API "What an error does to the script" records that the pass then ends in 0x2488e0, which
kills the script's threads (or resets the script). So after a real error the plan thread runs on only until its next
yield (the `PumpEvents` inside the following `BlockOnCommand`) and is then gone [H for the flag and the test; M for
the kill path, taken from LUA_API, not re-read].

AI_TACTICAL §7.3.2's wording for the space form ("error ..., nil") is correct about the return value but should not
be read as "the plan continues". For this script the point is moot in retail: **no error is raised**, so the plan
continues normally to the DEPLOY line.

## Not determined

- That `TacticalRegionAITargetLocationClass` vf+0x30 returns null was taken from LUA_API and the task statement; the
  vtable slot was not re-read.
- Whether the script pool clears Lua globals on release (only matters for a stale `AITarget` on a goal with no AI
  target).
- The meaning of GameObjectWrapper +0x70 (set to 1 here).

## Port consequence

For a goal whose AI target has no game object (region / cell target), the port must set the plan global `Target` to
a **GameObjectWrapper holding no object** (a userdata, not nil; `TestValid(Target)` false), written on every plan
instantiation, and `LandTaskForce.Move_To` (and the space form) must return nil **silently** for a GameObjectWrapper
with no object or an AI target wrapper with no AI target. The line "Parameter 1 is not a valid destination for
TaskForce MainForce" is **not** retail behaviour for this script: retail raises no error here, the MDU does not move,
and the plan goes straight on to `Activate_Ability("DEPLOY", true)`. (In retail a real error would also end the plan
script at the end of that service pass, so emulating the error would be a tier-1 difference.)
