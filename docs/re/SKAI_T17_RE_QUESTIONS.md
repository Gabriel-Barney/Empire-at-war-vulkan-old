# skai t17: space skirmish Rebel AI on Geonosis: empty escort task force, build order (questions, 2026-11-16)

Answers go into `docs/re/SKAI_T17_SPACE_BUILD_RE.md` (new), with a short pointer in `AI_TACTICAL.md` §7.4 / §7.5.

## What the retail traces show

Eight retail runs of the parity scenario `geonosis_skirmish` (`tools/parity/scenarios.json`: `_MP_SPACE_GEONOSIS`, idle
human Empire against the Rebel Medium AI, 6000 credits; run folders in the main checkout under
`tools/probe_mod/q2_traces/out/`, e.g. `20261116-003910-skirmish-vanilla-lua`, `20261115-225742-skirmish-vanilla-lua`).
The Rebel starts with its station and two X-Wing squadrons (teams of 5). In all eight runs:

1. **Pads are secured and held.** Each X-Wing team flies to a different neutral `Mineral_Extractor_Pad` (Capture_Point_Radius
   550, 10 s transition, no `Ownership_Sticks`), the pad turns Rebel (run 003910: frame 597 at (1000, 3848), 912, 1090), the
   team stays near it, and a `UC_Rebel_Mineral_Extractor` (850 credits, 35 s) starts about 40 frames after the capture
   (637). The first extractor starts between frame 637 and 1738 in every run. This matches
   `Data/Scripts/AI/SpaceMode/MoveToLocationRush.lua` (Category `Secure_Build_Pad_Space`: MainForce "Fighter | Corvette = 1",
   EscortForce "Fighter | Corvette | Frigate = 0,4"; MainForce moves to the pad, then `Guard_Target` until a structure
   stands there, waiting forever on a refinery pad) followed by `BuildRefinerySpace.lua`.
   Since each team holds its own pad, each such plan's **EscortForce got no unit**, yet the plan lived on.
2. **Build order.** `RS_Level_Two_Starbase_Upgrade` (2000 credits, 80 s = 2400 frames) completes at frame 2470-2472 in all
   eight runs, so it was queued at about frame 70 (the first `Tactical_Multiplayer_Build_Space_Units_Generic` plan). The
   first new X-Wing squadron appears after it (frame 2941-3532); no Y-Wing is built early in any run.

## What the port does (dev bc3391f0, `alamo_headless --skirmish-ai skirmish --map _MP_SPACE_GEONOSIS --human Empire --squadron-size 0`)

1. Every `Secure_Build_Pad_Space` plan (MOVETOLOCATIONRUSH) ends in failure about 15 frames after it starts, on the frame
   its MainForce gets the `Move_To`. A call-stack print at `_ScriptExit` showed the exit comes from the **EscortForce
   thread**: `EscortForce_Thread` -> `QuickReinforce(PlayerObject, AITarget, EscortForce, MainForce)` ->
   `PGTaskForce.lua:350` (`if tf_to_reinforce.Get_Force_Count() == 0 then ScriptExit() end`, the space branch). The port's
   EscortForce has 0 units, `Produce_Force` finishes at once, each `Reinforce` returns nil ("has no reinforcements"; the
   port follows AI_TACTICAL §7.4), so QuickReinforce falls through to ScriptExit. No pad is held, pads decay back to
   neutral, no extractor is ever built in 5400 frames.
2. The first build plan's ReserveForce orders Y-Wing_Squadron, Rebel_X-Wing_Squadron, RS_Level_Two_Starbase_Upgrade,
   Rebel_X-Wing_Squadron at frame 71 (PP pick order), so the Y-Wing comes out at 581, an X-Wing at 1031 and the upgrade
   only at 3431.

Port code: `engine/ai/tactical/src/tactical_ai.cpp` (`claim_force`, plan start, task-force threads), `tactical_pp.cpp`
(the solve), `engine/lua/src/bind_ai.cpp` (task-force bindings).

## Questions (Ghidra / REA)

Q1. For a tactical plan task force whose count range allows 0 (`= 0,4`) and whose PotentialPlan selection gives it no unit:
   a. Is the task force created and its `<TF>_Thread` started at plan start (0x6cfbe0 and callers)? If not, what is the TF
      global in Lua (nil?).
   b. What does its to-produce list (TF +0x60) hold, i.e. does the tactical solve give a 0-minimum list "to be produced"
      entries (no source) up to the maximum? With such entries, what do their AIBuildTasks do in a space skirmish plan
      that is not a build plan (`AllowFreeStoreUnits` default): fail at once (PRODUCTION_FAILED, Production_Failed event),
      wait (WAIT_FOR_CREDITS / other state, so `BlockOnCommand(Produce_Force())` never returns), or something else?
   c. What `Get_Force_Count()` returns for it, and what `SpaceTaskForceClass::Reinforce(MainForce)` (0x6e8620) returns for
      it (the "has no reinforcements" warning path returns no value, so `BlockOnCommand` returns nil at once).
   d. Net answer wanted: which line of the escort thread retail stops at, so that `PGTaskForce.lua:350` `ScriptExit()` is
      not reached (or, if it is reached, why the plan's MainForce keeps guarding).
Q2. `Tactical_Multiplayer_Build_Space_Units_Generic` ReserveForce (`TacticalMultiplayerBuildSpaceUnitsGeneric.lua`: upgrade
   lines `= 0,1`, the faction unit line `= 0,3`, `AllowFreeStoreUnits = false`): in what order does retail queue the
   selected items (Produce_Force's AIBuildTask order, 0x5fe240 loop over TF +0x60, and the order the tactical solve fills
   that list), and how many units of the `0,3` line does the first solve pick with 6000 credits? Retail's queue finishes
   the station upgrade first in all eight runs.
