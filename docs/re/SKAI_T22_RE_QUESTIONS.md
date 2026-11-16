# skai t22: RE questions (space skirmish AI on Geonosis)

Asked 2026-11-16 by lane skai-b. Evidence: `docs/re/SKAI_T22_GEONOSIS_SCOUT.md`, "Retail plan records" (three retail
runs with read-only plan hooks). Answers go into a new `docs/re/SKAI_T22_SPACE_AI_RE.md` (behaviour in words, never
decompiled code), with pointers from `AI_TACTICAL_PERCEPTION.md` §4.3 and `AI_TACTICAL.md`.

Answered 2026-11-16 in `docs/re/SKAI_T22_SPACE_AI_RE.md` (Q2 marked "evidence suspect: probe hooks").

## Q1. Which objects are in the perception grid's owner lists in space, and when?
Retail `EvaluatePerception("Distance_To_Nearest_Friendly_Starbase", <Rebel AI>, <cell>)` (the equation in
OFFENSIVESPACEEQUATIONS.XML: `Variable_Target.DistanceToNearestFriendly{Parameter_Type = ... "Skirmish_Rebel_Star_Base_1",
"Skirmish_Rebel_Star_Base_2", ...}`) returns 1e18 (nothing found) on every call while the Rebel's station is the
pre-placed skirmish start station `Skirmish_Rebel_Star_Base_1`, and a real distance on every call after the AI upgraded
it (the upgrade makes a new `Skirmish_Rebel_Star_Base_2` object). The Empire's start station (`Skirmish_Empire_Star_Base_1`,
never replaced) is never found by `Distance_To_Nearest_Enemy_Starbase`.

AI_TACTICAL_PERCEPTION §4.3 says grid entries are registered by the UNIT_AI behaviour's first service (0x4f6070 ->
0x6537d0) and the GOM add hook (0x2b3670 -> 0x4f5950); the 0x72d000 scan walks the owner lists (+0x88).
1. Which objects end up in the owner lists +0x88 (and the live cell lists +0x10) in a **space skirmish**, and which do
   not? In particular: are objects created before the grid exists (skirmish start forces and stations, 0x2a4fc0 /
   SKIRMISH_SETUP §2.2; map-placed objects) ever registered? Does the UNIT_AI first-service path register them, or is it
   gated (already-serviced flag set before the grid existed, a mode / setup-phase test, a type test such as
   SPACE_OBSTACLE, a station / Is_Static test)?
2. Is the answer the same for the start **fighters** (TIE / X-Wing squadrons placed by 0x2a4fc0) as for the stations?
   If the start units are missing too, they are also missing from the threat layers and `EnemyUnitConcentration`
   (so `Current_Enemy_Location` in BusyTacticalFreeStore would see no Empire unit while the idle Empire owns only start
   objects).
3. Does anything else exclude the start station from 0x72d000: the representative type (0x3973b0) of a skirmish
   station, the type-list match (a variant `Variant_Of_Existing_Type` resolving to the base name?), or the alive /
   flag test (obj +0x3a0)? `Skirmish_Rebel_Star_Base_2` is a variant too and is found.
4. AI_TACTICAL_PERCEPTION §4.7 says the FOW-active flag 0x140a284c4 is cleared by the space mode. Confirm: in a space
   battle every fog test (0x35f470, 0x35f3b0) answers "not fogged", for AI and human players alike, so
   `DistanceToNearest*`, `Find_Nearest`, `Applies` and `TimeLastSeen` see through fog in space?

## Q2. What ends a Space_Scout plan so soon?
Retail starts a new `Space_Scout` plan (SpaceMode/SpaceScout.lua: `MainForce` "Fighter | Corvette = 1",
`IgnoreTarget = true`; goal OFFENSIVESPACEGOALS `Space_Scout`: Tactical_Location, Time_Limit 30, Activation_Tracking_Duration
20, Per_Activation_Failure_Desire_Adjust -15) in the same pooled script instance every 100-200 frames, each time for a
new cell, about 80 starts per 5400-frame battle; nearly every pick has desire 10 (a tie over many cells). The port keeps
one scout goal about 1000 frames (until the plan's Move_To / Explore_Area end), about 11 starts per battle.
1. Which rule releases the running Space_Scout goal (or ends its plan) that quickly: Resolve displacing it (§1.6 step 3:
   is the +1.6 hysteresis applied to tactical goals, and is the active goal's desire re-evaluated each commit so that a
   cell the squadron is flying over drops below the others?), the running-plan re-test (0x6c47c0: what makes a
   Space_Scout plan invalid: its unit taken by another plan's PotentialPlan, its target re-test, `IgnoreTarget`), the
   goal Time_Limit / AllowedTime (0 < AllowedTime < ETA in tactical?), or the plan's own task force (Produce_Force
   failing for "Fighter | Corvette = 1")?
2. How long is a tactical goal-system pass (proposal slice quota and commit cadence, §1.3) for a space AI with 25 cells
   x the space goal types? A commit every ~100 frames would match the restart spacing.
3. In Resolve's weighted random phase (§1.6 step 6), are ties of equal desire picked by weight each commit, so that
   with many cells at 10 a running scout is displaced whenever it is not re-picked (is a kept goal re-entered in Pool, or
   only an OldCand)?
