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

## Q3 (roadmap mov2 t06): zone validation of a move to an object

A Move_To an object goal (not an attack) is composed with `skip_destination_check` in the port, so the composition pass's
zone validation (`0x50f480`(P, seed position, class, 30), MOVEMENT §4.2 step 2 "Destination") never runs and never ends the
pass. Find whether retail validates the object's goal point there (which point: the object's position, its goal point
`0x583150`, the hard-point?) and what a failed validation does for an object goal (the pass ends, later formations get no
move, as for position and attack orders?).

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

## Q5 (roadmap mov3 t07): readers of `Begin_Turn_Towards_Distance` (type +0x3a8)

22 FoC types set it (fighters and heroes, 100 - 900). Find every reader of type +0x3a8 (the XML tag's parse target)
and what it shapes (fighter turn-in toward a target or waypoint?), with the exact comparison. Answer in
`docs/re/MOVEMENT.md` §2.5 or `docs/re/FIGHTER_FLYING.md` (the fighter section it belongs to).

## Q6 (roadmap mov3 t08): the terrain height `0x135140`

Read `0x135140` (and `0x135350`): is it a bilinear blend of the four corner heights of the cell (which weights, which
cell, clamping at the map edge), or a triangle-linear sample over the cell's two triangles (which diagonal)? The port's
`Terrain::sample_height` (engine/terrain) is triangle-linear; the 0.5 under-water tests (SurfaceFX surface,
`Destroy_When_Stunned_Over_Water`) read it. Answer in `docs/re/ABILITIES_HARDPOINTS.md` (the 0x135140 bullet) or a
new short section in `docs/re/MOVEMENT.md` §3.6, marked [H] / [M].
