# AITAC retail questions (roadmap group `aitac`, 2026-11-19)

Questions for `re-analyst` (ReVa). Answers go into the spec named under each question, in words, with the RVAs they rest
on and read / inferred kept apart. Lane `sim-queue` implements from those specs.

## Q1 (aitac t06) `Should_Switch_Weapons(target)`: the body

- Port today: a stub that returns false (`engine/lua/src/bind_ai_tactical.cpp`, search `Should_Switch_Weapons`).
  `docs/re/AI_TACTICAL.md` §8 (`Try_Weapon_Switch`) and §12 item 6 say the body was never traced.
- Anchors: the Lua method name string `Should_Switch_Weapons` (GameObjectWrapper method table); PGCommands.lua
  `Try_Weapon_Switch` calls it for SWAP_WEAPONS units without a special case (`extracted/foc_data_text.zip`, Scripts/Library/PGCommands.lua).
- Asked: argument checks and error strings; what it compares (the unit's current / alternate weapon set, damage vs the
  target's armor, range, target category, `Is_Good_Against`-style tables?); which unit state it reads (SWAP_WEAPONS
  behaviour fields, the current weapon index); whether it draws a random number; what it returns (bool / nil) for a dead
  target, a unit without SWAP_WEAPONS, a team container.
- Spec: `docs/re/AI_TACTICAL.md` (new subsection under §8, and close §12 item 6).

## Q2 (aitac t07) Explore_Area's point pick: 0x6558b0 / 0x655be0

- Port today: `Explore_Area` moves straight to the target (`engine/ai/tactical/src/tactical_ai.cpp`, `unit_explore_area`
  and the task-force Explore_Area). `docs/re/L1_S4_REMAINING.md` §2.3: ExploreAreaBlockStatus (id 0x82b, 0x6afec0), init
  0x6b0320, start 0x6b06f0 calls the helper 0x6558b0 (target position) / 0x655be0 (target object) with the player and the
  start frame; guessed [M] "least-recently-seen FOW area"; a point of 1e18 means none. functions.csv names 0x6558b0
  `ExploreAreaBlockStatus`; both call 0x35f3b0 (a fog test).
- Asked: what the helpers compute - the search area (radius around the target, the hint zone / threat-grid cells, the
  TacticalPerceptionGrid's TimeLastSeen cells?), the score and tie-break, the fog test's role, whether any random draw is
  made, when 1e18 is returned; and the status's service: does it re-pick points (when, how often), IsFinished, Result,
  any timeout. Also what the object-target variant adds over the position one.
- Spec: `docs/re/L1_S4_REMAINING.md` §2.3 (replace the [M] guesses).

## Q4 (aitac t05) Tactical build signals 0x22 and 3 on the AIBuildTask

- Port today: a ProduceForce status drops an entry when its builder is gone; the build signals are not modelled
  (`tactical_ai.cpp`, StatusKind::ProduceForce, "[port: the build signals 0x22 / 3 are not modelled]"). Signal 0x22 of
  land builds that went to the reinforcement pool is modelled (q6 t19, `production_completed`).
- `docs/re/AI_GALACTIC_3.md` §1.11: in a skirmish the AIBuildTask (0x524ee0, state BUILD_TASK_LIST 0x648980) registers
  with the PP (0x6c1e80) and listens to player signals 0x22 and 3. `docs/re/AI_TACTICAL.md` §7 (Build_All): when no
  under-construction object appeared, signal 0x22 is raised on the pad and the cost refunded.
- Asked: the AIBuildTask's signal handler for 0x22 and for 3 in tactical mode - which event raises each (production
  started / finished / cancelled / refused, the builder destroyed, the queue id matching), what the task does on each
  (finish, fail, decrement a counter, retry with the next builder), and what the TF / plan sees (Lua events such as
  Production_Failed / Unit_Produced?).
- Spec: `docs/re/AI_GALACTIC_3.md` §1.11.

## Q5 (aitac t05) Space reinforcement arrival time (signal 0x19)

- Port today: a SpaceReinforceBlockStatus spawn creates the unit at once and it joins the task force at the drop
  (`tactical_ai.cpp`, `space_reinforce_poll`, "[port: the arrival signal 0x19 / in-flight count +0x78 are not modelled]").
  `docs/re/AI_TACTICAL.md` §7.4 step 6: spawn 0x2bf2d0(GOM, unit, P, player) (hyperspace arrival), subscribe to signal
  0x19, +0x78 += 1; arrival 0x723ac0: +0x78 -= 1 and the unit joins the TF.
- Asked: what 0x2bf2d0 does to the new unit (the hyperspace-in sequence: locomotor state, duration in frames, whether the
  unit is targetable / counted before it arrives), and which code raises signal 0x19 at the end (the frame relative to the
  spawn). Is this the same arrival the start fleet uses (cu8l t03, `docs/re/CU8K_T03_T08_T09_RE.md` §1.9 / §4)?
- Spec: `docs/re/AI_TACTICAL.md` §7.4.

## Q6 (aitac t05) The space movement block's signal handler

- Port today: aitac t05 applies A1_READINGS §33.2's signal table (handler 0x73ce80, the land movement block status) on
  land only: signal 0xb of the block's object finishes the block, 0xb of a unit takes it out of the all-units / pending
  lists, 0x1a turning the guarded object hostile finishes a guard (`tactical_ai.cpp`, `movement_block_signal`). Space keeps
  the earlier rules (an object block ends on the target's death / owner change by polling, `step_object_block`).
- Anchors: SpaceMovementBlockStatus (its Result 0x6af930, `docs/re/A1_READINGS.md` §33.2.2 / `tactical_ai.cpp`
  step_object_block comment), the space collectors 0x739200 / 0x73a650 (§33.3).
- Asked: the space movement block status's signal handler - which signals it subscribes on the object and on the units
  (0xb, 0x10, 0x1a, 10), and what each does (finish, remove the unit, the hostile tests), compared with the land table.
- Spec: `docs/re/A1_READINGS.md` §33.2 (a space row or subsection).
