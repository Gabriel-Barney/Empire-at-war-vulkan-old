# skai t22: RE questions, part 2 (which Space_Scout cell wins a desire tie)

**Answered 2026-11-16 (re-analyst): `SKAI_T22_SPACE_AI_RE.md` Part 4.** First-inserted wins through the greedy phase
(cells x outer, y inner; no RNG before Resolve; Tactical_Targeted's budget credits are above 0, so the greedy phase runs).
The runs differ in how many start X-wings Map_Control's `Secure_Build_Pad_Space` reserved before Tactical_Targeted
resolved (2 / 1 / 0), and failed scout accepts put −15 on their cell for 20 s, so the 4.5 s commit took CELL_0_2 in all
three runs. Part 3 of the same file also corrects Q1.1: the start station is in the cells and threat layer, only out of
the owner lists.

Asked 2026-11-16 by lane skai-b. Evidence: `docs/re/SKAI_T22_GEONOSIS_SCOUT.md` (sections "Retail plan records" and
"Port rule from the RE answer"). Answers go into `docs/re/SKAI_T22_SPACE_AI_RE.md` as a new part (behaviour in words,
never decompiled code).

## What is seen
Geonosis space skirmish, idle Empire, Rebel Medium AI. Right after the start (Game.Age < 200, every cell unseen except
around the Rebel start), `Space_Area_Needs_Scouting` gives desire 10 to most of the 25 space cells (term 2: both starbase
distances are 1e18, `10 × TimeLastSeen`). The start stations are out of the threat grid (Q1.1), so the Rebel home cells
are at 10 too.

- **Retail** (three runs, `PSTART` readings taken before any probe effect): the Rebel AI's first Space_Scout takes
  CELL_0_0 (run 2, 2.3 s), CELL_0_1 (run 3, 2.3 s) and CELL_0_2 (run 1, 4.5 s). The recorded desires differ from 10 only
  by float noise that follows the pick time, not the cell (at 11.2 s two runs both show 10.000000492, on CELL_2_2 and
  CELL_3_0). So the cells look tied, and the winner still differs between runs. Every retail run is seeded by the
  skirmish itself.
- **Port**: the first Space_Scout, at frame 69, takes CELL_0_0 in 9 of 10 seeds: the first-inserted of the tied cells.
  The port sorts proposals stably, truncates each category to `extension (2) + |active|` (AI_GALACTIC §1.6 step 5), and its
  greedy phase takes pool[0]. Cells are proposed in grid order with x as the outer loop (`CELL_x_y`).

## Questions
1. **Insertion order of tactical space proposals.** In a space AI's proposal pass (AI_GALACTIC §1.3 cursor, tactical
   goal system), in what order are the (goal type × target) pairs proposed for the space cell targets? Is the cell target
   list built in a fixed grid order (which axis is outer?), or in an order that varies between runs: a random start, a
   shuffle, hash order, an order from object creation, or the quota cursor continuing from where the previous pass
   stopped? Does the first pass after the battle starts begin at index 0?
2. **Randomness before Resolve.** Does any step before Resolve's greedy phase draw from the RNG in a way that can reorder
   or drop equal-desire proposals? For example: the plan pick at proposal (§1.5) failing for some targets, a random term in
   the desire or the effective desire, a random tie-break in the category sort (0x720eb0 insertion sort below 32
   elements), or a random cut in the truncation to `extension + |active|` (0x721b60).
3. **Applies / proposability of cells for Space_Scout.** Can a cell drop out of the proposals for a reason that differs
   between runs at 2-5 s: `IgnoreTarget = true`, a target already held by another goal of the same type or a like goal,
   the activation-tracking learning (Activation_Tracking_Duration 20, Per_Activation_Failure_Desire_Adjust −15), a failed
   plan pick or PotentialPlan for "Fighter | Corvette = 1" (no free fighter when the start squadrons are held by other
   plans)?
4. **The commit that starts the first scout.** At 2.3 s (frame 69) how many passes have run, and is the first scout
   started by the greedy phase (pool[0]) or by the weighted random phase (one synced draw weighted by effective desire)?
   For example: the greedy phase could stop early because pool[0] is like an active goal, or `keep` is already at
   `limit`. If the random phase starts it, all tied cells are equally likely.

Wanted for the port: the rule that decides the first scout's cell among tied cells, precisely enough to say whether
the port's "first-inserted wins" is right (and the difference is somewhere else) or what replaces it.
