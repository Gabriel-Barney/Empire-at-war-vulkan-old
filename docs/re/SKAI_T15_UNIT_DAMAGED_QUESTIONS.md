# skai t15: how the engine raises the plan event Unit_Damaged (questions, 2026-11-16)

Answer goes into `docs/re/SKAI_T15_UNIT_DAMAGED.md` (new). REA and `re_reference/` only.

## Why

The port's tactical AI never raises plan event 14 `Unit_Damaged` (AI_GALACTIC_2 §5.2), so the shipped
`Default_Unit_Damaged(tf, unit, attacker, deliberate)` (PGEVENTS.LUA: kite / heal with `Release_Unit`, `Divert`,
garrison, INVULNERABILITY / Defend / MISSILE_SHIELD, DEPLOY_TROOPERS, fighter dogfight release) never runs for task-force
members. In retail that handler releases damaged units from a running plan one at a time (AI_TACTICAL §7.7.3 item 6),
which frees units for new proposals and makes the running-plan re-test fail (SKAI_T15_PLAN_RETEST §1.3: "released singly").
The port's land Destroy_Structure plans keep their units for ~5000 frames where retail's churn every ~400-800 frames.

## Questions

1. **Raise site.** Which code posts plan event 14 (through the plan event post 0x6006b0 or the TF event path)? From which
   object signal or damage callback (hull / shield damage taken by a task-force member), in land and in space? Name the
   RVAs.
2. **Filter and rate.** Is it raised for every damage instance, or throttled (a per-unit cooldown, a minimum damage, only
   the first hit per frame, only when the unit is a member of a running plan's task force, only for AI players)? Is damage
   from a friendly / neutral source, from a structure, or to a shield only included?
3. **Arguments.** What are `unit`, `attacker` (the damaging object, the projectile's owner object, its TEAM container?)
   and `deliberate` (what makes it true: the attacker's current target is the unit? an ordered attack?)?
4. **Timing.** Is the handler called synchronously inside the damage call, or queued to the plan and dispatched at its
   next PumpEvents (like the other plan events)? If queued, can several Unit_Damaged events for one unit pile up between
   two pumps, or is the queue coalesced?
5. **Handler lookup.** `<TF>_Unit_Damaged` first, else `Default_Unit_Damaged` from the plan's globals (PGEvents)? Same as
   the other events?
6. **Free store.** Is the event (or a free-store equivalent) raised for units on the free store, or only for task-force
   members?

The port change is an event raise in `engine/ai/tactical/src/tactical_ai.cpp` (`tf_event`) fed by the battle's damage
records; the answer decides its filter, arguments and timing.
