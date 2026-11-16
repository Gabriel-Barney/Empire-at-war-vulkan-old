# mov2 t05: the three real-map units stopped by a failed TerrainCollision detour (2026-11-16)

Question (roadmap mov2 t05, backlog b0175, from bkm2 t02 / b0041): in the port's real-map test
(`engine/tactical/movement/tests/test_movement.cpp`, `test_real_data`: 24 synthetic single units per land map with random
start and goal, 3240 in all) exactly three units never arrive: EM01_TRAIL_OF_A_JEDI unit 18 (stopped at frame 352),
EM08_SITH_TOMBS unit 15 (652), _MP_LAND_NABOO unit 13 (635). Each is stopped by its own TerrainCollision record whose
kind-3 Path Around failed (MOVEMENT §5.4.7 item 3). Would retail stop them too?

The units are the test's own (speed 1.0 / 1.6, radius 6 / 16), not FoC types, so retail cannot replay them; the
answer is whether each step that stops them follows retail's rules.

## Answer

**No: the stop comes from a port stand-in, not from a retail rule.** In all three cases the kind-3 Path Around accepts
11 rejoin goals and each fails the "goal inside an obstacle's grown rectangle" test, so the 11-goal cap ends the call.
In retail, goals 2 to 11 are accepted against the grown shapes of the previous goal's window, so those spots are
**rejected at acceptance and do not count** toward the 11. The scan then goes on along the path, and the detour is found.
[H for the rule (decompile read below); M that these three synthetic units would arrive in retail, from the emulation.]

| Unit | Detour state at the failed step | The 11 goals the port accepted |
|---|---|---|
| EM01_TRAIL_OF_A_JEDI 18, frame 352 | on a collision detour (pushed), waypoint 3 of 5 | 1 on the path, 2 past its end, 8 from the ring around the end; all inside a grown shape (12 obstacles: units standing at the detour's end) |
| EM08_SITH_TOMBS 15, frame 652 | on its order path, waypoint 1 of 7 | 11 on the path or past its end; all inside a grown shape (16 obstacles) |
| _MP_LAND_NABOO 13, frame 635 | on a collision detour, waypoint 2 of 6 | 1 on the path, 10 from the ring; all inside a grown shape (3 obstacles) |

## What retail does (decompile read, `re_reference/`, 2026-11-16) [H]

- The goal loop `0x675ed0` keeps one tracker pointer across its goals. For each goal it builds the window and its
  tracker (`0x677340`, the grown obstacle shapes, MOVEMENT §1.5.1 item 4a), then tests "goal inside a grown shape"
  (`0x6b3580`) on that tracker, and on failure goes to the next goal **with the tracker still set**. The generator
  `0x67ad20` gets that tracker and passes it to the acceptance `0x679aa0` of each (a) path point and (b) past-end step.
- `0x679aa0`: after the snap to the unit's region (within `LandDestinationProximity`), it places the unit's hard
  rectangle at Q (axis Q − E). **With a tracker it queries the tracker** (`0x6b4cb0`, the grown shapes of the
  previous window); **only with none** (the first goal) the game's layer-0x10 object grid. Each object met is judged
  by `0x67d580`(…, 1); one obstacle rejects the candidate. A rejected candidate is not an accepted goal.
- (c) ring candidates are tested only by `0x679e20` (the 2× path-length test), never by `0x679aa0`, so in retail a ring
  spot that lies inside a grown shape is still accepted and does use up a goal.

This settles MOVEMENT §5.4.7 "Rejoin candidates" item 2, "[H for the argument, M for the effect]": the effect is [H].

## How it was measured (port, local debug build, not committed)

- `alamo_tactical_movement_tests` with `MOVEMENT_DEBUG`, built in this worktree's tree (dev `bbe9e7ab`), and temporary
  prints in `path_around` / `around_search`: the three units, their failed kind-3 calls and the reason of every goal
  (all 33: the inside test).
- **Retail rule emulated** (temporary): for goals 2 to 11, an (a) / (b) candidate whose rectangle at Q overlaps a grown
  shape (half width + 0.5 + box expansion) of the call's obstacles is rejected without counting. Result: **all three
  units arrive**; 3239 / 3240 arrive; one other unit (_LAND_PLANET_SALEUCAMI_INTIMIDATION unit 14, frame 290) is
  stopped instead: its first goal fails and ten ring spots inside grown shapes use up the cap, which is retail's rule
  for ring spots. Failed kind-3 calls over all maps: 7 → 2. (The emulation tests all obstacles of the call, not only
  the previous window's, and keeps the layer query; retail queries only the tracker.)

## What this means for the port

- Tier-1 rule difference in `engine/tactical/movement` (`rejoin_accept`, `movement_system_part2.cpp`; the [port] note
  "the tracker argument of goals 2 - 11 is not modelled"): goals 2 to 11 must be accepted against the previous goal's
  window tracker (its grown shapes) instead of the live layer; the first goal keeps the layer query; ring candidates
  stay untested by it. Filed as a mov2 task (the movement code is held by another session).
- The real-map test's expectation then changes: the three named units arrive, and its "exactly 3 stopped" check must be
  re-measured (with the emulation: one, Saleucami unit 14, by the ring rule).
