# skai t22: skirmish start stations missing from the threat grid, and what ends a Space_Scout goal (RE, 2026-11-16) [H unless tagged]

Answers `SKAI_T22_RE_QUESTIONS.md` Q1.1-Q1.4 and Q2.1-Q2.3. Read with ReVa (Ghidra, `/StarWarsG.exe`, FoC Steam x64, image
base 0x140000000) and REA, plus the shipped XML / Lua (`extracted/foc_data_text.zip`). Addresses are RVAs. [H] = read in the
binary or the shipped data; [M] = one inferred link; [L] = not checked. No decompiled code is reproduced.

Functions: 0x72d000 (`DistanceToNearestFriendly` / `Enemy` scan; entry thunks 0x72d5a0 friendly, 0x72d590 enemy),
0x6537d0 (threat-grid registration), 0x4f5950 (UNIT_AI part of the GOM add hook 0x2b3670), 0x4f6070 (UNIT_AI Service,
behaviour vtable slot 6), 0x29f810 (generic object create), 0x2b0a20 / 0x2b5ad0 (map object load / GOM add without the
hook), 0x56480 (`Load_Map`), 0xeed80 (single-player skirmish start), 0x295cf0 / 0x294fa0 (skirmish player setup: team
game / free-for-all), 0x2a4fc0 (start forces), 0x35eb40 / 0x4d7d10 (mode FOW setup, mode vf+0x248), 0x365540 / 0x4d9850
(mode FOW teardown, vf+0x250), 0x4dcfd0 / 0x4dd180 (threat grid create / delete), 0x6536a0 / 0x653cd0 (grid destructor),
0x38e5a0 (`Change_Owner`), 0x4f4ba0 / 0x658930 (UNIT_AI grid unregister), 0x659760 (re-projection), 0x35f470 / 0x35f3b0
(object / point fog tests), 0x6927f0 (effective desire), 0x692740 (replaceable), 0x722a30 (maintenance), 0x6b8950 (space
goal sleep).

## Summary

- **Q1 (the rule the port needs).** In a skirmish, the start station of every player is created by the team-marker swap
  in the player setup 0x295cf0 (`Team_NN_Space_Station` marker → `Skirmish_<faction>_Star_Base_1`, BKG3_RE_ANSWERS Q3.1).
  That create runs the GOM add hook, which registers the station with the threat grid that `Load_Map` built. Two calls later
  the skirmish start 0xeed80 **deletes that grid and builds a new one** (mode vf+0x250(0), then vf+0x248). The station's
  UNIT_AI data pack still holds its grid entries from the deleted grid, and the UNIT_AI service registers an object only
  when that pack has **no** entries. So the start station is **never in the new grid**: not in the owner lists (+0x88),
  not in the live cell lists (+0x10), not in the threat layer. Every grid query misses it for the whole battle, for every
  player: `DistanceToNearestFriendly/Enemy`, the player force sums, `Location` force, concentration, bitfields, point /
  zone danger. An upgraded station (`Skirmish_*_Star_Base_2`, a new object created during the battle) registers normally.
  The start fighters are placed after the new grid exists and are registered normally (Q1.2).
- **Q1.3.** Nothing else in 0x72d000 excludes a start station: the alive bit (+0x3a0 & 0x40) is the death flag, the
  category mask defaults to all bits, the type match is the station's own type, which is in the `Parameter_Type` list.
- **Q1.4.** No. The space mode does **not** clear the FOW-active flag (FOG_OF_WAR §8 item 2 corrected this). In a space
  battle the fog tests are live: AI players see through fog only where the caller passes force = 0 (the UNIT_AI fogged
  bytes, because `AIUsesFogOfWarSpace` is False); `DistanceToNearest*`, `Find_Nearest`, `Applies` and `TimeLastSeen`
  (force = 1) use real fog, for AI and human players alike.
- **Q2 (evidence suspect: probe hooks).** The tactical commit comes at the end of each proposal pass, every 76-151
  frames for a pass of more than 150 evaluations that is not quota-capped, with no sleep above Easy. The active goal's desire is re-evaluated at each
  commit and the incumbent gets the +1.6-scaled hysteresis (at desire 10: 11.20). A plan whose thread dies (a Lua error, or
  `ScriptExit`) ends at once, its goal is set Released and is dropped at the next commit, and the same commit can activate a
  new Space_Scout on another cell. That gives one restart per commit, which matches the probed 100-200-frame restarts with
  no end record. A plan that only blocks forever keeps its goal while the goal is not displaced.

---

## Q1.1 Which objects are in the owner lists (+0x88) and live lists (+0x10) in a space skirmish

### 1.1.1 The two registration paths [H]
- **GOM add hook** (0x2b3670): called only by the generic create 0x29f810 (for an object that is not dying and has no TEAM
  slot) and by the team helper 0x2a5a60. When the mode type is not galactic and the object has a UNIT_AI slot, 0x4f5950
  registers it with the threat grid (0x6537d0) **if the grid exists at that moment**, then stores the transform copy and
  clears the per-player fogged bytes. Without a grid nothing is registered and the pack's entry count stays 0.
- **UNIT_AI Service** (0x4f6070, behaviour vtable slot 6 = vf+0x30): when the pack's next-check stamp (+0x20) is due and
  the grid exists, it registers the object **only when the pack's entry count (+0x18) is 0**; it then compares the
  transform with the stored copy and re-projects (0x659760) when it changed. The mode gate vf+0xf0 is constant true in
  space (0x17dc70).
- Registration (0x6537d0) creates the entries (one per hard point of type 5..10 with flag +0x4c, else one whole-object
  entry), ORs the category mask into +0xc8, appends the object to the owner list of `PlayerList[obj owner]` (index =
  player +0x48), copies snapshots for players that have it fogged, and makes the grid listen to the object's signals.
- Re-projection (0x659760) removes and re-adds each entry in the **live** lists and the threat layer only. It never touches
  the owner lists.
- Map objects read by the map loader (0x2b0a20 → finish 0x3a0930 → GOM add 0x2b5ad0) do **not** pass the hook. The
  per-object post-load call (0x2a8730 → 0x394510, behaviour vf+0x68) is a no-op for UNIT_AI. So map-placed objects are
  registered at their first UNIT_AI service.

### 1.1.2 The grid's lifetime at a skirmish start [H]
1. `Load_Map` (0x56480): mode load vf+0x78 creates the map objects (no grid yet when the previous mode's grid was deleted
   with that mode, 0x35ad70 → 0x4dd180(0) [M: that this teardown runs before the new map load]); then vf+0x248 (space
   0x4d7d10 → 0x35eb40) builds the FOW grids and creates the threat grid (0x4dcfd0 creates only when none exists).
2. The load driver 0x8df00 then runs the queued post-load start, the GOM post-load (0x2a8730), object combining, and calls
   0xeed80. No game tick runs in between [M: no tick call seen on that path].
3. 0xeed80, in this order: player setup **0x295cf0** when the lobby record's team flag (+0xb5) is set, else 0x294fa0;
   0x2a74f0 (per object 0x3935b0, a colour setup with no grid work [M: what it sets]); **mode vf+0x250(0)** (space 0x4d9850 → 0x365540): frees the FOW grids and the
   FOW renderer and calls 0x4dd180(0), which **deletes the threat grid** and clears the global; **mode vf+0x248**: a new
   FOW setup, which creates a **new, empty threat grid**; credits; per player the start forces **0x2a4fc0**; victory
   records.
4. The grid destructor (0x6536a0 → 0x653cd0) deletes its own cell, memory and owner lists and stops listening for signals.
   It does not touch any object's UNIT_AI pack: the entry objects stay in the packs with count > 0.

### 1.1.3 What that does to each kind of start object [H unless tagged]
| Object | Created | Grid at create | In the final grid? |
|---|---|---|---|
| Map-placed objects (asteroids, pads, map units) | `Load_Map` step 6, no hook | none | **yes**, at the first UNIT_AI service, under the owner after the remap |
| Start station `Skirmish_<faction>_Star_Base_1` | 0x295cf0 marker swap, 0x29f810 at 0x2962f9 → hook | the `Load_Map` grid | **no**: registered with the deleted grid; pack count > 0, so never re-registered |
| Start squadrons (`Space_Skirmish_AI_Default_Forces`) | 0x2a4fc0 after step 3's vf+0x248 | the new grid | **yes**, at create (team helper → hook, A1_READINGS §29.1) |
| Units built or spawned later, the upgraded station (0x70f070 creates the next level with 0x29f810) | in the battle | the new grid | **yes** |

- The swap products carry +0x3a3 bit 0, and both remaps skip such objects, so no `Change_Owner` happens for the station
  either (0x295cf0 decompile; the remap loop tests that bit). Even when a remap does call `Change_Owner` (0x38e5a0), it
  passes the old default player list, and with a non-null list `Change_Owner` does not unregister from the grid. Only a
  null list runs 0x4f4ba0 (unregister + clear the pack), after which the next due service re-registers the object. 0x295cf0
  passes null only for objects it hands to a neutral-faction player.
- Correction to AI_TACTICAL_PERCEPTION §4.4: 0x658930 is not an owner-change handler. It is the unregister used by 0x4f4ba0
  (removes the owner-list entry, the memory entries of players that have it fogged, the live entries, and the signal
  listening). The grid has no owner-change hook; an owner change re-files the object only through that null-list path.
- **The start station is gated, not filtered:** the UNIT_AI first-service path would register it, but its pack is not
  empty. It is not a type test (SPACE_OBSTACLE, station, Is_Static), not a mode or setup-phase test, and not an
  already-serviced flag.
- A static station never changes its transform, so it never even re-enters the new grid's live lists. An object in the same
  state that moves would reappear in the live lists and the threat layer at its first re-projection, but never in the
  owner lists [H code; M: no other affected object type found].
- Which path a single-player skirmish takes: Geonosis has no placed `SKIRMISH_*_STAR_BASE_1` and the free-for-all setup
  0x294fa0 has no marker swap, so the stations seen in the retail runs come from 0x295cf0 (BKG3_RE_ANSWERS Q3.1 [H scan,
  M placement]). The special weapons that 0x293d60 creates in the same setup are not affected in space:
  `Ground_Empire_Hypervelocity_Gun`'s SpaceBehavior is `SPECIAL_WEAPON` only (no UNIT_AI).
- Land skirmish [M, not traced]: the same swap creates the team build pads and, through pad vf13 0x42df80, any pre-built
  structure. Those that have UNIT_AI would be missing from the land grid the same way.

### 1.1.4 What reads the owner lists and the live lists (so what misses the start station) [H, from AI_TACTICAL_PERCEPTION]
- Owner lists +0x88: `DistanceToNearestFriendly/Enemy` (0x72d000), the player sums 0x656400 (player
  `Friendly/EnemyForceUnnormalized`, `Game.ForceUnnormalized`).
- Live lists +0x10 and the threat layer: CellForce 0x6545d0 (`Location.FriendlyForce/EnemyForce`,
  `Get_Most_Defended_Position`), concentration 0x656800 (`Friendly/EnemyUnitConcentration`), bitfields 0x654d90, point /
  zone danger 0x653ff0 / 0x653e90, and the visible-cell test 0x656fe0.
- Not grid-based, so the start station still counts there: the AI's target list (perception slot 6 adds every existing
  object), `Applies`, unit tokens of the station itself (`Health`, `Type.IsType`, ...).

### 1.1.5 Port rule
Treat a skirmish start station (the team-marker-swap product) as **absent from the threat grid** for the whole battle:
not in any owner list or live list and never projected. `DistanceToNearest*` with the starbase type list then answers 1e18
until the station is upgraded, for the owner and for every enemy. Register every other start object (map placements,
start squadrons) as now. Record it as a retail quirk, not a [port] choice: on Geonosis it removes term 1 of
`Space_Area_Needs_Scouting` (desire 15 near the own start) and keeps term 2 open next to both start stations.

## Q1.2 Start fighters [H]
No, they do not share the stations' fate. 0x2a4fc0 runs after 0xeed80 has built the new grid. The start squadrons (and
their team containers, through the team helper 0x2a5a60 → 0x2b3670 → 0x4f5950) register at create with the grid that
lasts the battle. They are in the owner lists, the live lists and the threat layer. `EnemyUnitConcentration` (live entries;
AI players are never fogged) counts the Empire's start TIEs, so `Current_Enemy_Location` in BusyTacticalFreeStore can find
the idle Empire's start squadrons as soon as their centre cells score. What it cannot find is the Empire station.

## Q1.3 Other exclusions in 0x72d000 [H]
In order, for each player in player-list order: skip a player whose faction `Is_Neutral` (faction +0x108); skip unless
`relation(player → Self) == 0` matches the friendly flag (friendly scan: allied players, Self included; enemy scan: all
others). For each object in that player's owner list:
1. skip the location's own unit (when the target is a unit location);
2. skip when +0x3a0 bit 0x40 is set. That bit is set by the death handler 0x39bdb0 (the only `OR [reg+0x3a0], 0x40` in
   the image), so it means "dying". A start station is not dying;
3. skip when `Parameter_Category` (default all bits) misses the category mask (+0x1648) of the representative type
   0x3973b0. The starbase equations give no category;
4. skip when the object is fogged for Self under real fog (0x35f470, force = 1; Q1.4). The own station is never fogged
   for its owner: an allied object with REVEAL is "not fogged" before any cell test;
5. when a `Parameter_Type` list is given, the representative type must be **pointer-equal** to one of the types looked up
   by name (0x407c00 on the type manager). 0x3973b0 gives the object's own type for anything that is not FLEET, TRANSPORT
   or TEAM, so a station matches its own name. A `Variant_Of_Existing_Type` type is its own type object and does not match
   its base name [M: variant identity], but the shipped list names every `Skirmish_*_Star_Base_1..5` explicitly.

Distance: squared 2-D centre distance (float), first strict minimum wins, then square root minus the location unit's radius
and the found object's radius (0x397780). None found: 9.999999843067494e17.

## Q1.4 Fog in space [H]
- FOW-active flag 0x140a284c4: static initial value 1. Writers: land FOW setup 0x3b86f0 (sets 1), space FOW setup 0x4d7d10
  (clears it only while 0x140b15490 is set; that global is 0 in the shipped game), `FOW.Disable_Rendering` 0x6a53b0, story
  reward ENABLE_FOW 0x456970. So a space battle runs with the flag at 1 unless an earlier battle of the session left it 0.
  AI_TACTICAL_PERCEPTION §4.7's "cleared by the space mode" is wrong (already corrected in FOG_OF_WAR §8 item 2).
- Object test 0x35f470(mode, player, object, force) answers "not fogged" when: the flag is 0; the object has neither TEAM
  nor HIDE_WHEN_FOGGED; the object is allied to the player and has REVEAL; the player has no FOW grid or a Reveal_All grid.
  Otherwise, for a non-human player with force = 0 it answers "not fogged" while `AIUsesFogOfWarSpace` is False; with
  force = 1, or for a human, it tests the object's sample cells in the player's FOW grid. Point test 0x35f3b0: same flag,
  same AI switch for force = 0, else the FOW cell.
- Result in a space battle: the UNIT_AI fogged bytes (force 0) are never set for AI players, so the threat, force and
  concentration tokens see everything; `DistanceToNearest*`, `Find_Nearest`, `Applies` and `TimeLastSeen` (force 1) see
  real fog, for AI and human players alike. The port's fog on `DistanceToNearest*` (SKAI_T22_GEONOSIS_SCOUT "Ruled out")
  is right; the 1e18 of the start stations is Q1.1, not fog.

---

## Q2. What ends a Space_Scout goal (evidence suspect: probe hooks)

The lane reports that the three probed runs are distorted by the probe's own plan hooks (no space plan ever returned from
its first `BlockOnCommand`, and the Rebel behaviour differs from 8 unprobed runs). Only the cheap parts are answered here.

## Q2.1 Re-evaluation, hysteresis, and a plan that never progresses or dies [H unless tagged]
- **Re-evaluation.** Each commit first re-evaluates every active goal's desire function (no learning terms) when its target
  still `Applies`, and overwrites the stored desire (AI_GALACTIC §1.6 commit step 1, 0x7222c0). This applies to tactical
  goals.
- **Hysteresis.** Effective desire 0x6927f0 has no mode test: an active goal (state > 0) with 0.01 < d < 40 counts as
  `d + (1 − (d − 0.01)/39.99) × 1.6`. At d = 10 that is 11.20, so against a best proposal of 10 the incumbent is kept unless
  its re-evaluated desire falls below about 8.75.
- **Replaceable** (0x692740): a goal is protected only while its plan is running (plan +0x80 = 0, not ended) and has called
  `Set_As_Goal_System_Removable(false)` (removable flag plan +0x98, default 1). SpaceScout.lua sets false after its
  `Produce_Force` block returns and true again after `Move_To` returns. So the goal can be displaced while it waits for its
  unit and during `Explore_Area`, not during the flight.
- **Time_Limit.** The maintenance drop `AllowedTime < ETA` runs only in galactic (0x722a30 tests the context's mode field
  +0x34 == 0). In tactical, Time_Limit only enters the activation test `0 < AllowedTime < ETA` of Resolve; it never ends a
  running goal.
- **Plan thread dies** (a Lua error, a `false` yield or a return in its only thread, or `ScriptExit`): at the next Planning
  service (3 frames) the plan has no live thread and ends (0x5f6a50): forces released, learning outcome from
  `Set_Plan_Result` (not set → not a success [M: default false]), goal set Released (0x693750), PP dropped, script back to the pool. At the
  **next commit** the Released goal leaves the active set (maintenance on the kept set, or not re-picked from the old
  candidates). No activation failure is recorded, so `Per_Activation_Failure_Desire_Adjust` (−15) does not apply; Space_Scout
  sets no `Per_Failure_Desire_Adjust` [M: default 0]. The same commit's Resolve can activate a new Space_Scout from the pass
  that just ended; that pass skipped only (type, target) pairs that were active while it ran (space slot 9, AI_GALACTIC
  §1.5), so the new goal usually has another cell.
- **Plan thread never progresses** (blocks forever without error): the plan stays alive and the goal stays active. While it
  is still waiting on `Produce_Force` it is replaceable, so at each commit it is kept when its effective desire is ≥ the best
  proposal and otherwise displaced (Q2.3); its PotentialPlan re-test (0x6c47c0) can also drop it (SKAI_T15_PLAN_RETEST
  §2.1). With the 10-desire tie and the 1.2 hysteresis it is normally kept.
- **Reading of the probe [M].** A thread dying at its first `BlockOnCommand` gives exactly one new Space_Scout per commit,
  each on a new cell, with no end record before it. That matches the probed 100-200-frame spacing and `prev=none`, so those
  restarts are most likely the probe's hooks killing the plan thread, not a retail release rule. An unprobed scout keeps
  its goal at least until `Move_To` ends, as the port does.

## Q2.2 Tactical pass and commit cadence [H, AI_GALACTIC §1.3]
- Goal Service runs every frame. Each call evaluates at most `quota` candidates; when the pass is complete the commit runs,
  then the per-slice quota is reset to `clamp(min(ceil(E / 150), (int)(20 / max(1, players − activeHumans) + 0.5)), ≥ 1)`
  (E = evaluations of the pass just ended), then the sleep `Space_AI_Goal_Cycle_Sleep_Duration × 30` frames (space slot 11
  0x6b8950 reads the difficulty record +0x44; shipped 15 s on Easy, 0 otherwise).
- A pass of E evaluations at quota q takes floor(E/q) + 1 frames. When the cap does not bind, q = ceil(E/150), so for
  E > 150 a pass lasts between 76 and 151 frames whatever E is: one commit every 2.5-5 s on Medium and Hard. For E ≤ 150
  (q = 1) it lasts E + 1 frames. When the cap binds (E > 150 × cap) it is longer, floor(E / cap) + 1 frames. The first two
  passes use the init seed (AI_GALACTIC §1.3). E for a space AI is the number of (proposable space goal
  type × target) pairs that pass `Applies` and the active-set skip; it was not counted for Geonosis [L].

## Q2.3 Ties in the weighted random phase [H, AI_GALACTIC §1.6; not re-read]
A displaced active goal (an old candidate: effective desire below the best proposal and replaceable) is not re-entered as a
new pick: the greedy phase stops when the pool's head is already active, and the random phase weights only entries that are
not kept or active. An old candidate that is not kept is released at the end of Resolve. Equal-desire proposals are picked
by one synced draw weighted by effective desire. A running scout is therefore not displaced by a tie at 10 (it has 11.20);
it is displaced only when its own re-evaluated desire drops below about 8.75 while it is replaceable.

## Confidence and open items
- Q1.1 rests on the call order in 0xeed80 and `Load_Map` [H], the hook path of 0x29f810 [H], the count gate in 0x4f6070
  [H] and the grid destructor leaving the packs alone [H]. [M]: that no game tick runs between `Load_Map` and 0xeed80, and
  that the previous mode's grid is deleted before the new map loads (if it were not, map objects with the hook path would
  share the stations' fate; map objects do not use the hook, so the conclusion for the stations is unchanged).
- A retail check, if wanted: in a space skirmish, `EvaluatePerception` of `Distance_To_Nearest_Friendly_Starbase` with the
  AI's own start-station cell as target stays 1e18 until the upgrade (the probed runs already show it, 233 records).
- Q2 is limited to the rules above; the probed restart rate is not evidence of a retail rule.
