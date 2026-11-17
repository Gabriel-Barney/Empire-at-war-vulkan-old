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
