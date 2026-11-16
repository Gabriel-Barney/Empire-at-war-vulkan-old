# skai t22: RE questions, part 3 (how long a reserved unit stays on the space free store)

Asked 2026-11-16 by lane skai-b. Answers go into `docs/re/SKAI_T22_SPACE_AI_RE.md` as Part 5 (behaviour in words, never
decompiled code).

## What the unhooked retail runs show
Three Geonosis runs without the Lua probe (`run_skai_t22_space.cmd`, read-only Frida, 2026-11-16: `20261116-173353`,
`-173917`, `-174437`; `analysis/space_goals.py`):
- Commit mf 67: the Rebel AI accepts `Secure_Build_Pad_Space` obj:93 (Map_Control, category 18) and `Space_Scout`
  CELL_0_0 (Tactical_Targeted, category 5), both by the greedy phase (activation ra 0x6c97cd / 0x6c90cb). Two start X-wing
  squadrons are on the free store.
- Commit mf 134 (run 1): category 18 resolves first and accepts a second pad, obj:98 (greedy, 0x6c90cb). Then category 5
  re-tests the running scout: `retest_leave` ret 0 (test_contrast 1, but Space_Scout is `IgnoreTarget`, so the result is
  the validity check 0x6c45e0), budget release from 0x6c8c9a, plan end, goal release. So at mf 134 one of the scout PP's
  units was still **on the free store** and reserved by another PP: the new pad took it, since Commit step 4 (0x6c0be0)
  had suspended the scout's reservation (SKAI_T15_PLAN_RETEST §1.1, A1_READINGS §28).
- Run 2: no second pad is accepted at 134; the scout survives to mf 934. Run 3: as run 1 (scout released at 134).
- Scout goals over the three runs live a median 464 frames (67..1472); 148 failed scout accepts per run.

The port hands a PP's free-store units to the task force a few frames after the accept (`tf MainForce produce: 1 units`
at frame 71 for an accept at 69). Its scout's unit is therefore never stealable, and its scouts live a median 909 frames.

## Questions
1. **The space hand-over test.** In tactical space, the free-store service 0x4d5a70 (every 3 frames) gives an entry's
   unit to a claiming AIBuildTask when the store's vf+0xb0 answers 0. What does `SpaceFreeStoreClass` (vtable 0x8b6a38)
   vf+0xb0 test (the land one is 0x6908f0, the galactic one 0x690240: reservation, then "at a planet and not in
   transit")? When does it answer 1 (keep waiting): a unit that is moving, has orders, is locked (`Lock_Current_Orders`),
   is in hyperspace / arriving, is not idle?
2. **The timeline after an accept.** From the Resolve accept of a tactical goal (0x6c2d60 reserves the PP's units) to the
   hand-over: when is the plan instantiated and its `MainForce_Thread` started (planning service 0x5f8310 cadence), when
   does `Produce_Force` (0x5fe240) run its BUILD_TASK_LIST (0x648980) and claim the entries (0x4d2bd0), and when does the
   AIBuildTask FSM 0x64aec0 next update? What is the shortest and the usual number of frames before a selected free-store
   unit leaves the store? Which of these steps can hold a space unit on the store for 60+ frames?
3. **The start squadrons.** At Geonosis mf 67-134 the Rebel AI's start X-wing squadrons came from the skirmish start
   forces (0x2a4fc0). Is there anything about them (team containers, the free store holding the TEAM container vs the
   members, a just-placed unit being "in transit") that delays their hand-over?

Wanted for the port: the hand-over rule (and its timing) precisely enough that a Space_Scout accepted together with a pad
can lose its unit to a pad accepted at the next commit, as in retail run 1 and 3, and not when no second pad is accepted.
