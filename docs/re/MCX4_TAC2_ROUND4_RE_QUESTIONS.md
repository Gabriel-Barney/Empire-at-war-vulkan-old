# MCX4 tac2 round 4: manual hardpoint targeting (lane mcx4-tac2, 2026-11-18)

Row: `HardPoint.Manual_Hardpoint_Firing_Cooldown_Secs` (and the rule behind `Requires_Manual_Target_Assignment`, hardpoint
type +0x50; vanilla user: `HP_Underworld_L3_Station_Main_Cannon`). Known so far (`MCX4_T10_RE_ANSWERS.md` §10, §18;
`TACTICAL_COMBAT.md` turret slew): the manual-target setter is 0x3822e0 (hardpoint, target, player id). It accepts only
when the hardpoint type has the flag, the target is an enemy of the owner (0x39a450), passes 0x540140 and the cone test
0x383ba0 and the visibility test 0x35f470, and lies within range + target radius and at least `Fire_Min_Range_Distance`;
then it stores the target (+0xc0), the player id (+0xc8) and the frame (+0xcc). `Fire_At` stamps the player cooldown
(0x285d70) and clears +0xc8. Callers of 0x3822e0: 0x40a3b0 (reads a player id at +0x50 of its first argument) and
0x576220. Answers go into `MCX4_T10_RE_ANSWERS.md` §10 as a "Round 4" paragraph, with RVAs and [H] / [M] / [L].

1. What are 0x40a3b0 and 0x576220: a player command (event, network message, HUD click), an AI or Lua path, or something else? For the
   command, which object does it take (the station, the hardpoint by index) and what condition picks the 0x3822e0 branch
   (the byte at +0x38e / +0x369 of the unit it fetches)?
2. Does a `Requires_Manual_Target_Assignment` hardpoint ever fire **without** a manual target? `Update_Weapon` 0x387400
   tries `Fire_At` on the primary target (+0x40): is +0x40 set for such a hardpoint by the owner's normal targeting
   (and does that targeting skip manual-flag hardpoints), or does `Fire_At` / `Does_Weapon_State_Allow_Firing_At`
   refuse when +0xc0 is empty? Does opportunity fire (§2.6) apply to it?
3. When it has a manual target, does it shoot that target (+0xc0) rather than +0x40, and when is +0xc0 cleared (after the
   shot, at target death, the 300-frame rule in TACTICAL_COMBAT `Update_Weapon` step 3)?
4. Who reads player +0x47c / +0x480 (the cooldown stamp)? Does anything in the sim (0x3822e0's callers, the AI) refuse a
   new manual assignment while frame < +0x47c + +0x480, or only the HUD button?
5. Does the AI (tactical AI or Lua) ever assign a manual target for its own station?

## Further rows (same round)
Answers go into the named section of `MCX4_T11_T12_RE_ANSWERS.md` as a "Round 4" paragraph.
6. **`GameObjectType.Harass_Enemy_Exclusion_Range` / `Avoid_Enemy_Exclusion_Range`** (§B3 / AI rows): the HARASS (behaviour
   0x41) and AVOID_DANGER (0x42) services. What does 0x370e60 return (the "strongest enemy" key used by the harass pick)?
   What does the avoid move 0x58e8e0 do: its destination rule, how the exclusion range is used, and how often it re-plans?
   Which FoC types list those behaviours?
7. **`GameObjectType.Ignore_For_Reoptimization`**: the battle-end company re-optimiser 0x2b3e90. When does it run (the
   battle end, retreat, the galactic side)? Which companies does it merge or split, how does the flag exclude a unit,
   and what does the result change (galactic fleet contents, the unit count)?
8. **`GameConstants.MovementReevaluationFrameCount`**: in the space coordinator re-plan 0x5d55f0, what do the member
   tests 0x5672b0 / 0x568340 check, and what does the re-plan 0x5d4730 issue (new attack move, formation re-form)?
9. **`GameConstants.ShouldSkipLandFormup` = false path** (0x41c1a0): after a member's form-up path to pack +0x120, what
   starts the formation's own path (all members arrived, a timeout), and is there a frame limit?
10. **`GameObjectType.Max_Distance_From_Spawner`** (INDIGENOUS_UNIT wander 0x621d80, §B2): the bounds of the last
    random draw marked [M] there.
