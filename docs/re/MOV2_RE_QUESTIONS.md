# mov2 questions for re-analyst (lane-mov, 2026-11-17)

Answers go into `docs/re/MOVEMENT.md` (the sections named per question), in words, with the RVAs they rest on and
read / inferred kept apart. Already read without ReVa (MOVEMENT §3.5.2 G, "mov2 t09" bullet): `0x410170`, `0x61dff0`,
`0x61d890`, `0x61ca90`, `0x372560`, `0x371310` - obj +0x348 is the TEAM slot, so squad containers are aligned; the
plan-mode wanted yaw is the leg direction + `0x42d440`, snapped by the type's movement directions.

## Q1 (roadmap mov2 t09): why does a re-ordered T4B keep 0x13 at full speed?

Retail measurement (MOVEMENT §3.5.2 G, run `out/20261116-022245`): a T4B (single unit, no TEAM, Movement_Directions_Count
unset) given a new Move_To while marching straight, 90 and 180 degrees off stays in locomotor state 0x13 at full speed
and turns on the move in 5 of 6 re-orders (once it went to 0x14, braked to 0, stood one frame in 0x16 and restarted).
`0x410170` (the aligned test, P +0x6d := 1) should fail for 90 / 180 degrees. Find:
1. every writer of the path record's started byte P +0x6d (besides `0x410170` and the Walk 0x16 handler), and every path
   set / push that runs for a moving land unit given a new order - in particular whether the new order's path goes on
   the stack while the old one is still walked, or the old top path is kept started (re-used record, +0x6d copied).
2. The Walk move step C.1 (MOVEMENT §3.5.2 C): exactly when "top path not started" sends 0x13 to 0x14; is there a case
   (a path whose first waypoint lies behind the unit, a turn-arc start, the old path's remaining record) that keeps 0x13.
3. Why the one straight-on re-order braked: what differs for a goal straight ahead.
Port today: `MovementSystem::set_path_started` (engine/tactical/movement/src/movement_system.cpp) - not aligned -> End
Move coast to 0x15, start from 0x16 with the 0x10 turn.

**Answer (re-analyst, 2026-11-20; `0x410170`, `0x371310`, `0x20b6d0`, `0x41c1a0` (`0x14041ce4d`), `0x4192b0`,
`0x554310` / `0x5c45d0` / `0x554200`, `0x42ced0`, `0x61e930`, `0x61e4b0`, `0x61ccd0`; FoC GAMECONSTANTS.XML)
[H unless tagged].** The T4B re-order **is aligned**: the earlier reading that `0x410170` fails at 90° / 180° was
wrong.
1. **`0x410170` passes every angle for a type with no `Close_Enough_Angle_For_Move_Start`.** After its gates (a top
   path whose waypoint 0 has a speed fraction > 0; TEAM slot → aligned at once; the state switch below) it takes δ
   = the angle from the facing to the wanted yaw of path index 0, wrapped to (−180°, 180°] (`0x20b6d0`), and
   returns aligned when |δ| ≤ R (`0x372560`) **or |δ| ≤ CE** (`0x371310`: type +0x16b4 when > 0, else GameConstants
   `CloseEnoughAngleForMoveStart`, **180 in FoC**). With CE = 180 the second test is always true. The T4B
   (`GROUNDVEHICLES.XML`) sets no CE, so a 90° or 180° re-order is aligned and `0x41c1a0` sets the new top path
   +0x6d := 1 in the same plan call; the next frame the move step finds a started path, keeps 0x13 and turns on
   the move (§3.5.2 C.6, corner cap B.4 with waypoint 0's fraction). This explains the 5 / 6 turns at speed. Only
   types with CE < 180 (`AT_AT_Walker` / `Veers_AT_AT_Walker` 30, `SPMAT_Walker` 45) can fail the angle test.
   State gate: 0xf-0x16 and 0x21-0x23 run the angle test, 0x24 is aligned at once, every other state is not aligned.
2. **Writers of P +0x6d** (image-wide byte-store scan: immediate 1 and register stores at +0x6d): the plan
   `0x41c1a0` and the formation copy branch `0x4192b0` (both after `0x410170`); the generic start `0x42ced0` (the
   locomotor reports idle → top path started, state 0); Walk vf6 `0x61e930` (the 0x16 handler: not started →
   started, state 0; the deployed-walk start of state 0x19 → started, state 0x12); `0x61e4b0` (a Walk helper that
   pushes a one-point path, started); the Path Around push `0x5c80e0`; the push `0x6859a0`; the team container
   service `0x5ad690`; the fighter, SimpleSpace, Bike, LandBomber and other locomotors' own handlers; and the
   record copies `0x5c45d0` / `0x554200`, which copy +0x6d from the source record. The push `0x554310` builds the
   stack record with `0x5c45d0` from the path it is given, so an order path is pushed with its source's +0x6d (0 for
   a newly planned path [M]). For a moving land unit given a new order the plan first destroys the whole stack
   (`0x554480`; no record is re-used and no old +0x6d survives), pushes the new path, then runs `0x410170`. The old
   path is never walked on.
3. **0x13 → 0x14** happens only in the move step (§3.5.2 C.1: top path not started → 0x14, controller T = a), so only
   for a re-order that `0x410170` calls not aligned: for a T4B that means waypoint 0's speed fraction is 0, or the
   path has fewer than 2 points.
4. **The one straight-on brake** [M: the run did not record the waypoint fractions]. The likely cause is a waypoint-0
   fraction of 0 from the corner pass (§3.5.2 D): for the first corner (j = 0) the probe is accepted only when the
   corridor test `0x5093c0` passes (the "v < 0.2 vmax" escape needs j ≠ 0); each failure divides v by 1.5, and below
   0.1 vmax v becomes vmin (0 for the T4B, no `Min_Speed`) or 0 when the last static line test failed, so fraction
   0. A unit standing in the straight-ahead corridor, or a static line test failing on terrain, would do it. Then
   0x14 brakes at a to 0, 0x15, 0x16 starts the path (+0x6d := 1), and the start test takes 0x12 directly because
   |δ| ≤ R straight ahead: exactly the measured "braked to 0, one frame in 0x16, restarted".
Port: aligned = TEAM slot, or (waypoint 0 fraction > 0, ≥ 2 points, state in the gate and |δ| ≤ R or ≤ CE with CE
default 180). Written into MOVEMENT §3.5.2 G (the mov2 t09 bullet corrected).

## Q2 (roadmap mov2 t10): head-on MovingVsMoving pairs never hold at speed 0

Retail measurement (MOVEMENT "Retail measurement of MovingVsMoving head-on pairs"): T4B / T4B (twice), T4B offset 12, AT-ST /
T4B, ordered toward each other's start 600 apart: neither stands at speed 0 before its goal or leaves 0x13; one swerves
(up to ~40 off the line) at full speed, the other slows to 0.11 - 0.22 for 27 - 66 frames, then runs on. Find, for this
head-on case on open ground:
1. which record state the chooser `0x67fb10`-family picks (Path Around 1, Pass 4, ...), who is M, and the speed each
   state's service answers S and M frame by frame (`0x67f080` reset, the hold-line tag stop, line A / line B of state 4,
   `0x5c69f0` follow speed): which rule floors the slowed unit at ~0.1 - 0.2 instead of 0.
2. Whether the slowed unit's speed answer passes through the locomotor's Min_Speed / a minimum speed fraction (0x13 never
   leaves the state while the answer is > 0?).
Port today: `MovementSystem::mvm_service` state PathAround (M stops on its hold line: vm = 0) and Pass (S stops at line B:
vs = 0), engine/tactical/movement/src/movement_system_part2.cpp.

**Answer (re-analyst, 2026-11-20; `0x67f7b0`, `0x682510`, `0x680510`, `0x67f590`, `0x67f130`, `0x5c69f0`, `0x61ccd0`,
with MOVEMENT §1.5.1 items 3 / 5 and §5.4.6) [H for each rule; M that M never reaches its line in these four runs,
which depends on speeds and the moment of the decision].**
1. **State and roles.** Head-on vehicles: not one formation, no forced path, facings opposed (step 4 skipped), no
   Giant, relation 4 at the decision; allied units never crush → **state 1 Path Around**, mode 2, swap allowed. M =
   the higher `UnitCollisionClass`: AT-ST (Large Vehicle 8) over T4B (Vehicle 7), so the AT-ST is M (slowed) and the
   T4B detours. T4B / T4B (equal class, each in its own formation): the larger `0x41a5c0` extent, equal here → M =
   p1 when the id of p0's formation's member 0 is lower than p1's, else p0. S (the detourer) gets a kind-4 path
   around M (`0x683140`).
2. **Per frame (`0x682510`).** Reset (both max, flags 0); **S := `0x5c69f0`** = S's max speed (a one-member
   formation's speed is its own max, and the detour length / chord ratio ≥ 1 only raises the bound): the swerver
   runs at full speed. **M := `0x67f590`(M, S)**: max speed only for separate lanes (facings parallel or opposed
   within 10° and S at least hy_M + hy_S to the side of M's line; the 12-offset run qualifies only if 12 ≥ hy_M +
   hy_S), else `0x67f130`(M, S). M := 0 (with the flag) only while S's top path has a **pending** hold line
   (`0x680510`: the last waypoint tagged with M at an index above S's current index) **and** M is at or past it.
   Pass (state 4, lines A / B) does not occur head-on.
3. **Why M slows but never stands.** The hold line is the tagged corner of S's detour minus (hx_M + hx_S) along M's
   facing (§1.5.1 item 5), and that corner belongs to M's **swept** box: M's rectangle (+1 per side) stretched along
   its approximation by L = clamp((d − r_S − r_M) / closing, 0, 45 frames) × M's max speed, grown by hy_S + 0.5
   (§1.5.1 item 3). At a decision (made when the predictor sees contact within 45 frames) L is up to ~45 frames of
   M's travel, so the line lies about L ahead of M. M covers that distance under the slave speed while S, at full
   speed, reaches and passes the tagged corner first; from then on the line is no longer pending and M is never
   held. The slave speed itself is `0x67f130` with S closing (cy ≤ 0, the "not moving away" branch): **vmin + (vmax −
   vmin)·k, k = clamp(τ / 45)**, τ = gap / closing speed minus the braking term (v − follow) / dec, follow = clamp(0.8
   × |cy / cx| × vmax, vmin, vmax). **No rule floors it**: vmin = `Min_Speed` × factors is 0 for both T4B and AT-ST
   (neither sets `Min_Speed`). The 0.11 - 0.22 low is vmax·τ/45 with τ ≈ 4.5 - 9 frames: as S swerves, the gap
   stays at the detour's lateral clearance while the closing speed along the centre line falls (both facings turn
   away from d), so τ never reaches 0. When the pair stops closing (cx + cy ≥ 0, S abeam or past) the answer is
   vmax: "then runs on". The answer is 0 only at gap = 0 with S moving away, or at the hold line.
4. **0x13 is kept whatever the answer** (also 0): the Walk controller `0x61ccd0` takes T := min(T, cap) and steps v
   toward it (at once only with the flag, i.e. never here); the collision cap never sets 0x14 (only the formation
   speed 0, the corner cap and the brake-ahead do, §3.5.2 B). `Min_Speed_Fraction_For_Turn` is read only by the corner
   pass (§3.5.2 D), not with the collision cap.
Port: keep M on the slave speed, place the hold line from the **swept** box (L along the approximation) and test it
only while the tagged waypoint is ahead of S's current index; a line at M's current front holds M at 0 where retail
never does. Written into MOVEMENT §5.4.6 (new bullet after "A ring of held movers").

## Q3 (roadmap mov2 t06): zone validation of a move to an object

A Move_To an object goal (not an attack) is composed with `skip_destination_check` in the port, so the composition pass's
zone validation (`0x50f480`(P, seed position, class, 30), MOVEMENT §4.2 step 2 "Destination") never runs and never ends the
pass. Find whether retail validates the object's goal point there (which point: the object's position, its goal point
`0x583150`, the hard-point?) and what a failed validation does for an object goal (the pass ends, later formations get no
move, as for position and attack orders?).

**Answer (re-analyst, 2026-11-20; `0x43c1b0`, `0x583150`, `0x583680`, `0x583670`, `0x50f480`, caller `0x43e5b0`)
[H].** Retail validates an object goal exactly like a position goal; only block flag 0x20 skips it, and 0x20 is never
set by a player or Lua move (garrison exit, unload and the garrison corrective move only, P1_DOUBLE_CLICK_MOVE §3.3).
1. **The point** is `0x583150`(block, out, **the seed** as viewer) = `0x583680` clamped to the playable box (x, y)
   and to ±5000 (z). For a kind-3 block with an object: the block's stored point when it is set (≠ 1e18) and the
   object is fogged for the seed's owner (`0x35f470`, force 1); the seed's own position when the object is stealthed
   and the seed's type lacks `Target_Stealth_Units` (+0xa4); else the **hard point's** position when the block names
   one (`0x396ce0`); else, in land mode, for a team-container object (object +0x348 ≠ −1), the member the seed's
   TARGETING picks (`0x566aa0`); else the object's position. A kind-3 block without an object gives (0, 0, 0).
2. **The test** is the same call as for positions: `0x50f480`(grid, P, seed position, seed `OccupationStyle`
   (+0x1698), `MovementClass` (+0x1688), 30) (a locomotor answering vf +0x240 uses the free-position finder
   `0x1409cf220` instead). P in the seed's zone → kept; else the nearest cell of the seed's zone on the first ring
   around P that has one; none within 29 rings → failure.
3. **A failure ends the composition pass for an object goal too**: the bucket vector is freed and `0x43c1b0` returns
   0 at once (`0x14043d20f` → `0x14043d281`), skipping the remaining members of this bucket and every later bucket.
   The caller `0x43e5b0` ignores the return value and builds one formation per descriptor already made, so the
   formations composed before the failing seed move, and the failing seed and every unit not yet placed get no
   formation and no move from this order [H for the formations; M for what those units then do: their old paths
   were not traced]. The same early return (value 1) happens when a seed has no type, no locomotor or a max speed
   of 0.
4. **On success** the validated point is thrown away for an object goal: the formation's block is a copy of the
   order block (a fresh kind-1 block at V is made only for a position goal whose V ≠ P).
So the port's `skip_destination_check` for an object Move_To differs from retail only when the goal point lies in
another zone than the seed and no cell of the seed's zone is within 29 rings of it: retail then gives the seed and
the units after it no move. Written into MOVEMENT §4.2 step 2.

## Q4 (roadmap mov2 t11, lane mov2, 2026-11-20): what ends this three-record MovingVsMoving hold in retail?

Port run (`test_real_data`, TM05_IMPERIAL_POLICY, three synthetic vehicles 13 / 14 / 22, max speed 2.496, square
footprints 16, `Min_Speed` 0), from frame 370 to the end (12000 frames), with no new push or order:
- record A (14 v 22, state 1 Path Around, M = 14, S = 22): 14 stands on its hold line (a waypoint of 22's detour
  tagged with 14), speed 0 with flag 1;
- record B (13 v 22, state 1 Path Around, M = 22, S = 13): 22 is M, so its answer is the slave speed `0x67f590`(22, 13)
  = `0x67f130`(22, 13) with 13 standing: vmin + (vmax − vmin)·k, vmin = 0, k = τ / 45 with τ = gap / closing minus the
  braking term. 22 creeps toward 13, its speed falling geometrically (0.14 at frame 408, 0.001 at 640), and never
  reaches its hold line;
- record C (13 v 14, state 6 Moving With Speed Only, M = 14, S = 13): the two footprints overlap (gap 0), 14 stands,
  13 faces toward 14 (closing), so `0x67f130`(13, 14) = vmin = 0, flag 1. The state-6 re-run gives 6 again (same
  direction f0·f1 = 0.54, neither ahead < −0.001; relation 0x10; M = 14 since ahead1 < ahead0), and the predictor
  (footprints overlap) still says contact.
So 14 waits for 22, 22 (held by B's slave speed) for 13, 13 (held by C) for 14. MOVEMENT §5.4.6 "A ring of held
movers" lists the breakers as stamps (a push onto a held unit), new orders and validity; none happen here.
Find, reading `0x6814f0` cases 1 and 6, `0x67f130`, `0x67f590`, `0x442540`, `0x680650`, `0x509ea0`, `0x681d30`:
1. Is any of the three answers different in retail for this geometry (e.g. `0x67f130` with y at speed 0: does vmin
   come from `Min_Speed` × the mode factor `0x3728f0` also when `Min_Speed` is 0; does the τ braking term apply; is
   the record's M speed in state 1 really the slave speed while M is before its hold line)?
2. Does `0x442540` report contact for two overlapping footprints when one is at speed 0 with a path and faces away
   (14 faces 47.8°, 13 is behind-right of it), i.e. does the state-6 predictor ever end C?
3. Is there any other per-frame rule (the speed fold `0x509ea0` floor, a "stuck" counter in the locomotor, the
   approximation rebuild, detection re-checks of an existing pair) that would change the state of A, B or C, or push a
   path, while all three stand / creep? If none: say so ("retail holds too, [H] per step"), and the port keeps it.

**Answer (re-analyst, 2026-11-20, ReVa + REA on retail `StarWarsG.exe`): no breaker, retail holds too** [H per step,
M for the long-run outcome: not seen in a retail run]. Read: `0x6814f0` (stamp test at its top, case 6 inline),
`0x682510` (case 1), `0x67f130`, `0x67f590`, `0x3728f0`, `0x442540` with its disassembly (`0x140442898`,
`0x140442c38`, `0x1404431ac`, `0x14044322f`), `0x4422f0`, `0x680650`, `0x680510`, `0x680d00`, `0x67f080`, `0x509ea0`,
`0x61ccd0`, `0x61f840`, `0x67f7b0` (steps 1-2), `0x5c69f0`.
1. **The three answers are retail's.**
   - A (state 1, `0x682510`): reset; M := max speed when M can crush S, else `0x67f590`(M, S); S := `0x5c69f0`; then,
     when S's top path has a pending hold line (`0x680510`: the last waypoint tagged with M's id, at an index above
     S's current index) and (M.P − point)·dir ≥ −0.001 and M cannot crush S: M := 0 and flag M := 1. The function
     writes only +0x84, +0x88, +0xbd and the state (8 / 9 / 0): no counter, no timer. 14 at its line: 0, at once.
   - B (state 1, M = 22 before its line): M's answer is the slave speed whenever M is before its line or no line is
     pending. `0x67f590` gives max speed only for separate lanes (|f22·f13| ≥ 0.9848 and 13 at least hy22 + hy13 to
     the side of 22's line), else `0x67f130`(22, 13). Inside it **vmin = `0x3728f0`(type, unit) = `Min_Speed` (type
     +0x3b4) × the mode factor × the global factor (both skipped by type flag +0x790 byte 1 bit 0) × 0.4 when the
     unit's pack +0xd0 = 0**: a product, so `Min_Speed` 0 gives vmin = 0 exactly (the unit argument is passed: rdx =
     x at the call `0x14067f1c0`). 13 standing: cy = 0 exactly, so the "not moving away" branch: vmin + (vmax −
     vmin)·k; follow = clamp(0, vmin, vmax) = vmin; the braking term applies (follow < v and dec > 0: τ := max(0, τ −
     (v − follow) / dec)); k = clamp(τ / 45, 0, 1). With vmin = 0 the answer is vmax·k, and k stays > 0 while the gap
     is > 0 (at the fixed point τ − v/dec = τ / (1 + vmax / (45·dec)) > 0), so 22 closes geometrically and never
     reaches gap 0; the flag needs gap = 0 exactly. Floats: once 22's step is below half an ulp of its coordinates
     its position stops changing and its tiny positive answer is constant, so the scene is fully static.
   - C (state 6, case 6 of `0x6814f0`): validity, ids, reset (both max, flags 0), the chooser re-run into stack
     locals (≠ 6 → state 0 and nothing else), M := max, S := max when S can crush M, else `0x67f130`(S, M), then
     `0x442540`(M, S, 1.5 s, 0) false → 8. `0x67f130`(13, 14): 13 closing (cx < 0), 14 at speed 0 (cy = 0), gap 0
     → flag S := 1, τ = 0, k = 0 → vmin = 0.
2. **The predictor keeps C.** Assumed speeds (MOVEMENT §5.3.1 item 3): a unit facing away from the other is projected
   at its current speed (14: 0, so it stands for the whole look-ahead), a unit facing the other at its max speed
   whatever its real speed (13: max, along its approximation). A standing unit's radius is its side half extent (its
   motion vector is 0). When the two radii overlap at the start of the first step, `0x4422f0` reports contact at
   t = 0, or at the exit time when the relative motion leaves the overlap inside the step; the result is contact
   when elapsed + that time < 45 frames. Nothing moves, so the answer is the same every frame: chooser step 1 and the
   end of case 6 never end C. (Two quirks of the time count, not needed here: MOVEMENT §5.3.1 item 3, mov2 t11.)
3. **No other per-frame rule touches A, B or C.**
   - States 1 and 6 have no counter: the starvation fields +0xac / +0xb0 are written only by `0x681d30`, which is
     reached only from cases 2 and 14 (`0x681750`); chooser step 2 (→ 15) needs +0xb0 = 0 with +0xac set, which only
     an earlier state 2 can leave. +0xb4 (retry frame) is used only by states 10 / 12 and `0x680c00`, +0xb8 only by
     `0x680c00`.
   - The re-decide stamp (+0x70) is set only by a path push (`0x554310` → `0x50a020`); none happens. Validity
     `0x680650` holds (all three have a path on the stack; none stands on an impassable cell).
   - Speed fold `0x509ea0`: the minimum over every record's vf +0x40, starting from the caller's target; no floor and
     no self-exclusion. Walk controller `0x61ccd0`: T := min(T, cap), then a step toward T by acc / dec (at once with
     the flag), clamped to [0, vmax]; the collision cap never changes the locomotor state (a cap of 0 keeps 0x13 with
     v = 0) and there is no stuck counter. The 0x13 handler `0x61f840` runs the move step, then only chooses the
     animation.
   - Detection `0x508170` skips any pair with a live record (`0x5c7900`), so the three get no new record; the
     approximation rebuild every 8 frames rebuilds the same polyline from the same input. A position-goal formation
     re-paths only when its goal moved more than R (FIGHTER_FLYING §4.11 E); the driver `0x50a780` removes records
     only by vf +0x28 (states 8 / 9).
   - The hold-line tolerance is −0.001: a creeping M that meets its line is stopped there, not freed.
4. **When retail would not hold** (for checking the port, not this run): (a) a type with `Min_Speed` > 0: at gap 0
   with y not moving away `0x67f130` returns **vmin, not 0** (with the flag), so 22 would drive on into 13 at vmin
   and the ring unwinds once it passes 14's tagged corner [M for the unwind]; FoC: most vehicles have no
   `Min_Speed`, heroes and some walkers 0.1 - 0.3. (b) The re-run of C tests "same direction" (§5.4.6 step 4) before
   the relation: if 13 were behind 14 along 14's facing (ahead1 < −0.001), overtake(13, 14) would be tested first,
   and a true result (4, Pass) sends C to state 0 and a Pass set-up that pushes a detour onto 13. (c) The relation
   reads **p0's** collision cap: were p0 = 14 (held at 0 by A), rel would be 2 → state 1, not 6. The port's p0 must
   be the record's first participant as in retail.
Written into MOVEMENT §5.4.6 "A ring of held movers" (new bullet) and §5.3.1 item 3 (correction).

## Q5 (roadmap mov3 t07): readers of `Begin_Turn_Towards_Distance` (type +0x3a8)

22 FoC types set it (fighters and heroes, 100 - 900). Find every reader of type +0x3a8 (the XML tag's parse target)
and what it shapes (fighter turn-in toward a target or waypoint?), with the exact comparison. Answer in
`docs/re/MOVEMENT.md` §2.5 or `docs/re/FIGHTER_FLYING.md` (the fighter section it belongs to).

**Answer (re-analyst, 2026-11-20; type tag table entry `0x140a2dad0`, `0x622b80`, `0x6236b0`, `0x623340`, `0x623450`,
`0x623fe0`; confirms and sharpens MCX4_T11_T12_RE_ANSWERS B2) [H for the reads; M for the in-game effect, none in
FoC].** The tag's table entry is {name, float, **+0x3a8**} (neighbours: +0x3a0 `Max_Lift`, +0x3a4
`Stopping_Distance`). An image-wide scan of every SSE access at displacement 0x3a8 finds 19, and only the three in
`0x622b80` read through obj +0x298 (the type); the fighter locomotor routines (`0x5cb830`, `0x5cbea0`, `0x5cc220`,
`0x5cd8e0`, `0x5ce010`, `0x5ce4f0`, `0x5ca390`, `0x5ca430`) never touch +0x3a8. `0x622b80` is the fly step of the
legacy **STARSHIP_LOCOMOTOR** (vf6 `0x6236b0`). **No FoC type lists STARSHIP_LOCOMOTOR**: the 33 FoC type entries that
set the tag (variants counted) are 31 `FIGHTER_LOCOMOTOR` and 2 `FLYING_LOCOMOTOR` types, whose locomotors do not read
it. **So in FoC the field does nothing.** What the legacy step does with it (BTTD = the value), T = the target
(`0x623340`: the squad leader's position for a TEAM member, else the TARGETING target, else the move point):
1. "Target off the nose" `0x623450`: the direction to T, as angles relative to the orientation, has its yaw or pitch
   in (70°, 290°). When that holds, `Bank_Turn_Angle` (+0x39c) ≠ 0 and the roll rate (`0x3724d0`) ≠ 0: throttle 0.4,
   a slow attitude step (`0x3abf80`, turn rate / 3, pitch only) [M: its zero-vector argument], then **3-D distance to
   T > BTTD (strict)** → a banked turn toward T (`0x623fe0`: bank ±`Bank_Turn_Angle` by one step of the synced LCG
   `0x140a13e24`, throttle 0.15, steering vector toward T); ≤ BTTD → nothing more this frame (it does not turn in
   toward a near target).
2. Otherwise, the altitude rule: when T's z < `Layer_Z_Adjust` (+0xdfc): |T.z − z| < BTTD → T.z := `Layer_Z_Adjust`;
   then (with the possibly new T.z) |T.z − z| > 2·BTTD → T.z := `Layer_Z_Adjust`. Then the usual turn / throttle
   toward T. (MCX4's note missed the "T's z below `Layer_Z_Adjust`" gate.)
Written into MOVEMENT §2.5 (Legacy StarshipLocomotor).

## Q6 (roadmap mov3 t08): the terrain height `0x135140`

Read `0x135140` (and `0x135350`): is it a bilinear blend of the four corner heights of the cell (which weights, which
cell, clamping at the map edge), or a triangle-linear sample over the cell's two triangles (which diagonal)? The port's
`Terrain::sample_height` (engine/terrain) is triangle-linear; the 0.5 under-water tests (SurfaceFX surface,
`Destroy_When_Stunned_Over_Water`) read it. Answer in `docs/re/ABILITIES_HARDPOINTS.md` (the 0x135140 bullet) or a
new short section in `docs/re/MOVEMENT.md` §3.6, marked [H] / [M].

**Answer (re-analyst, 2026-11-20; `0x135140`, `0x135350`, callers `0x42bb40`, `0x136660`, `0x134180`) [H]:
bilinear, not triangle-linear.** On the heightmap object hm (land mode +0x338): W = hm +0x10, H = hm +0x14 (vertex
counts), cell = hm +0x9c, inv = hm +0xa0 (its reciprocal), height scale = hm +0xa4, vertex records of 4 bytes at hm
+0x20, row-major (index W·j + i), the height being the record's first **int16** × scale. World (x, y) is used as is
(vertex (i, j) at (i·cell, j·cell); `0x134180` gives the map centre as (W·cell / 2, H·cell / 2)).
1. i = floor(x·inv), j = floor(y·inv) (float × reciprocal, then a true floor: truncation, minus 1 for a negative
   non-integer); tx = (x − i·cell)·inv, ty = (y − j·cell)·inv, from the **unclamped** i, j.
2. The four corners (i, j), (i, j+1), (i+1, j), (i+1, j+1), **each index clamped on its own** to [0, W−1] and
   [0, H−1]. Outside the grid the two samples of a pair become the same vertex, so the result is the edge (or corner)
   height whatever the weight: clamp to edge.
3. Blend order (it matters for bit-exact floats): a = h(i, j) + (h(i, j+1) − h(i, j))·ty; b = h(i+1, j) + (h(i+1,
   j+1) − h(i+1, j))·ty; **h = a + (b − a)·tx**. No diagonal, no triangle choice.
4. `0x135350`(hm, x, y, out point, out normal) writes (x, y, the same h, same blend order) and, when asked, a normal:
   the four vertices' normals (a byte index per vertex at hm +0x50 into the table `0x231da0`) blended the same way
   (y first, then x) and normalised (`0x139800`). So the two differ only in what they return.
Consequence: the port's triangle-linear `Terrain::sample_height` equals retail only on planar cells; on a warped cell
it is off by up to a quarter of |h00 − h10 − h01 + h11| (at the cell centre), which moves the 0.5 under-water tests
(SurfaceFX surface, `Destroy_When_Stunned_Over_Water`) and the conform near shores. The port also divides by the
spacing where retail multiplies by the stored reciprocal. Written into MOVEMENT §3.6 and the ABILITIES_HARDPOINTS
0x135140 bullet.
