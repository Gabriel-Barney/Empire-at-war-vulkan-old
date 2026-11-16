# skai t15: why retail's Empire AI leaves the vehicle factory alone while merc teams stand by it (questions)

Lane skai-a, 2026-11-16. Answers go to `docs/re/SKAI_T15_TARGET_CHOICE.md`.

## What is measured

- Utapau `_MP_LAND_UTAPAU`, Empire Medium AI against an idle Underworld human, 36000 ticks.
- **Retail** run `20261116-133939` (`tools/probe_mod/q2_traces/out`, `analysis/land_attack.py`):
  - first AI unit within 1500 of the Underworld base: 8681 (Dark Troopers);
  - all six merc teams die 13538-19174;
  - then the structures fall: vehicle factory 20818, Droid Works 22990, barracks 25227, palace 28196.
  - Runs B2 / B3 and run 1 show the same order: structures from 14368 at the earliest.
  - Run 1 (`SKAI_T15_PLAN_RETEST` "Retail hook runs") started a Destroy_Structure plan on the vehicle factory at 6639. That plan ended at the 7421 commit by displacement, with no plan started at that commit.
- **Port** seed 1, dev 91654afb:
  - plan 14 Destroy_Structure on the vehicle factory runs 4847-9896, and the factory falls at 10085;
  - the merc teams die 10201-11401;
  - Droid Works falls at 11831, barracks 13337, palace 18692.
  - The port's `Should_Destroy_Structure` for the factory stays 15.001 throughout.
- **Already settled:**
  - `SKAI_T15_TARGET_LOCATION_FORCE.md`: the factory's `Location.EnemyForce` is about 0.006 in retail, so the desire term is not what holds retail back.
  - `SKAI_T15_PLAN_RETEST`: the cadence and the slot-10 rule match.
  - `SKAI_T15_UNIT_DAMAGED`: ported.
- Shipped data: `LANDGOALS.XML` `Destroy_Structure` = Enemy_Structure, Tactical_Targeted, `Reachability Low_Threat`, Time_Limit 30, Activation_Tracking_Duration 20, Per_Activation_Failure_Desire_Adjust -15. `Clear_Infantry` = Tactical_Location, `High_Threat`.

## Questions (most likely first)

### Q1. Land slot 9 reachability skip for the vehicle factory

The port implements A1_READINGS §25.3: the goal is skipped when the target zone's flood set at the goal's Reachability is empty. Each zone must have zone danger 0x653e90 == 0 or ≤ the Low_Threat tolerance (GC +0x157c, shipped 1000).

With six idle merc teams standing about 300 east of the factory (centres near x 5100, y 1260; the factory at (4819,1334) is in zone 85, "New Zone 86"):
- What is retail's zone danger of zone 85, and of each zone next to it? Exact units:
  - the raw sum over the zone's +0x90 cells;
  - which power (AI_Combat_Power × health?);
  - which player's cell lists;
  - whether the AI's memory lists count refogged cells twice.
- Is it above 1000, so that the factory is skipped while the mercs live?
- The port's danger is `perc.zone_threat_fn(w, player)` (`tactical_ai.cpp` flood_set). In the same units, would it come out lower? The RE of `SKAI_T15_TARGET_LOCATION_FORCE` §5 D2 / D3 says retail's +0x90 list is the union of the cells of every zone a unit passed through, while the port keeps it fixed. Can that make zone 85's danger include the merc cells in retail but not in the port?

### Q2. Other enemy-near-target gates between proposal and commit for Destroy_Structure

If Q1 does not hold it back, check these for a rule that depends on enemy force near the target:
- the PotentialPlan solve: Contrast Score 0x72b940's "everything met" gate, the enemy force at the target the contrast lists must cover, and `MinimumTotalForce` in `DestroyStructure.lua`'s TaskForce definitions;
- the FreeStore stage choice (Setup A flood pick around the target at Low_Threat);
- any check in Commit or plan start that drops the plan.

### Q3. Run 1's 7421 displacement

Which waiting proposal outranked the vehicle-factory plan at the 7421 commit, given that its target and desire were not recorded?
- Commit step 1 re-evaluates the incumbent's desire. Does it re-run the slot 9 reachability test, so that an incumbent whose target became unreachable is released?
- Or does the incumbent's re-evaluation apply the Per_Activation_Failure_Desire_Adjust?

### Q4. Plan Time_Limit 30 for a running Destroy_Structure

Does a running tactical plan's goal end or lose desire when Time_Limit (30 s = 900 frames) passes?
- Port: `gcfg.nominal_allowed_time = true`.
- Retail's Destroy_Structure plans end about 400 frames after they start; the port's plan 14 runs 5000 frames.
