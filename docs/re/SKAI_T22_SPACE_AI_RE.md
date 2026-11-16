# skai t22: skirmish start stations missing from the threat grid, and what ends a Space_Scout goal (RE, 2026-11-16) [H unless tagged]

Answers `SKAI_T22_RE_QUESTIONS.md` Q1.1-Q1.4 and Q2.1-Q2.3; Part 3 (second pass, same day) corrects Q1.1 on the live
lists; Part 4 answers `SKAI_T22_TIE_RE_QUESTIONS.md` (which cell wins the Space_Scout desire tie); Part 5 answers
`SKAI_T22_HANDOVER_RE_QUESTIONS.md` (a tactical free-store unit stays on the store after its hand-over). Read with ReVa
(Ghidra, `/StarWarsG.exe`, FoC Steam x64, image base 0x140000000) and REA, plus the shipped XML / Lua (`extracted/foc_data_text.zip`). Addresses are RVAs. [H] = read in the
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
  when that pack has **no** entries. So the start station is **never in the new grid's owner lists** (+0x88): every
  owner-list query misses it for the whole battle, for every player (`DistanceToNearestFriendly/Enemy`, the player force
  sums, `Find_Nearest`). **Corrected in Part 3:** it does reach the new grid's live cell lists (+0x10) and threat layer at
  its first UNIT_AI service, because the stored transform copy is stack residue and forces a re-projection; so
  `Location` force, concentration, bitfields and point / zone danger count it. An upgraded station
  (`Skirmish_*_Star_Base_2`, a new object created during the battle) registers normally. The start fighters are placed
  after the new grid exists and are registered normally (Q1.2).
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
  registers it with the threat grid (0x6537d0) **if the grid exists at that moment**, then calls 0x4f4c60 (writes the
  pack's transform copy from stack residue, not from the object: Part 3.1) and clears the per-player fogged bytes. Without
  a grid nothing is registered and the pack's entry count stays 0.
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
| Start station `Skirmish_<faction>_Star_Base_1` | 0x295cf0 marker swap, 0x29f810 at 0x2962f9 → hook | the `Load_Map` grid | **owner lists: no** (registered with the deleted grid; pack count > 0, so never re-registered). **Live lists and threat layer: yes**, from its first service (Part 3) |
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
- ~~A static station never changes its transform, so it never even re-enters the new grid's live lists.~~ Wrong (Part 3):
  the transform copy the hook stored is stack residue, so the station's first service re-projects it into the new grid's
  live lists and threat layer. It stays out of the owner lists [H code; M: no other affected object type found in space].
- Which path a single-player skirmish takes: Geonosis has no placed `SKIRMISH_*_STAR_BASE_1` and the free-for-all setup
  0x294fa0 has no marker swap, so the stations seen in the retail runs come from 0x295cf0 (BKG3_RE_ANSWERS Q3.1 [H scan,
  M placement]). The special weapons that 0x293d60 creates in the same setup are not affected in space:
  `Ground_Empire_Hypervelocity_Gun`'s SpaceBehavior is `SPECIAL_WEAPON` only (no UNIT_AI).
- Land skirmish: the same swap creates the team build pads and, through pad vf13 0x42df80, any pre-built structure; they
  are out of the land grid's owner lists and back in its cells the same way (SKAI_T15_TARGET_CHOICE §5, lane skai-a).

### 1.1.4 What reads the owner lists and the live lists (so what misses the start station) [H, from AI_TACTICAL_PERCEPTION; corrected by Part 3]
- Owner lists +0x88, **miss the start station**: `DistanceToNearestFriendly/Enemy` (0x72d000), the player sums 0x656400
  (player `Friendly/EnemyForceUnnormalized`, `Game.ForceUnnormalized`), Lua `Find_Nearest` (0x69e7c0), `Game.ForceVisibility`
  (0x654940).
- Live lists +0x10 and the threat layer, **count the start station** from its first service (Part 3): CellForce 0x6545d0
  (`Location.FriendlyForce/EnemyForce`, `Get_Most_Defended_Position`), concentration 0x656800
  (`Friendly/EnemyUnitConcentration`), bitfields 0x654d90, point / zone danger 0x653ff0 / 0x653e90, and the visible-cell
  test 0x656fe0.
- Not grid-based, so the start station still counts there: the AI's target list (perception slot 6 adds every existing
  object), `Applies`, unit tokens of the station itself (`Health`, `Type.IsType`, ...).

### 1.1.5 Port rule (superseded by Part 3.5)
~~Treat a skirmish start station as absent from the threat grid: not in any owner list or live list and never projected.~~
Part 3.5: absent from the **owner lists** only. `DistanceToNearest*` with the starbase type list then answers 1e18 until
the station is upgraded, for the owner and for every enemy; the cell and threat-layer reads count the station. Register
every other start object (map placements, start squadrons) as now. Record it as a retail quirk, not a [port] choice: on
Geonosis it removes term 1 of `Space_Area_Needs_Scouting` (desire 15 near the own start) and keeps term 2 open next to
both start stations.

## Q1.2 Start fighters [H]
No, they do not share the stations' fate. 0x2a4fc0 runs after 0xeed80 has built the new grid. The start squadrons (and
their team containers, through the team helper 0x2a5a60 → 0x2b3670 → 0x4f5950) register at create with the grid that
lasts the battle. They are in the owner lists, the live lists and the threat layer. `EnemyUnitConcentration` (live entries;
AI players are never fogged) counts the Empire's start TIEs, so `Current_Enemy_Location` in BusyTacticalFreeStore can find
the idle Empire's start squadrons as soon as their centre cells score. ~~What it cannot find is the Empire station.~~ It
counts the Empire station too, from the station's first service (Part 3); the owner-list reads do not.

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
- Part 3 (live lists) rests on the 0x4f6070 order and 0x659760 / 0x656d10 / 0x656a10 [H]; the one [M] is that the stack
  residue 0x4f4c60 stores never equals the station's transform (not observed at run time).
- Retail checks, if wanted: in a space skirmish, `EvaluatePerception` of `Distance_To_Nearest_Friendly_Starbase` with the
  AI's own start-station cell as target stays 1e18 until the upgrade (the probed runs already show it, 233 records), while
  `Variable_Target.EnemyUnitConcentrationUnnormalized` / `EnemyForceUnnormalized` of the enemy start-station cell is above 0
  from the first seconds (Part 3). A Frida hook on 0x659760 logging the object id over the first 300 frames would show the
  start station's re-projection directly.
- Q2 is limited to the rules above; the probed restart rate is not evidence of a retail rule.

---

## Part 3. The start station's first service in the battle grid (second pass, 2026-11-16) [H unless tagged]

Asked by the coordinator after lane skai-a's land reading (`SKAI_T15_TARGET_CHOICE.md` §5.3 on branch
`claude/skai-a-2c5f0f`) said the pre-built land structures come back into the cells at their first service, and that the
same likely holds for the space start station. It does. Functions: 0x4f4c60 (UNIT_AI pack reset after registration),
0x4f5950, 0x4f6070, 0x6537d0, 0x653100 (entry constructor), 0x659760, 0x656d10 / 0x656a10 (entry out of / into a grid's
cells), 0x653a50 (entry refresh against a grid), 0x6585f0 (grid signal handler), 0x550ed0 (`AIDataPackClass`
constructor), 0x384d00 (hard-point state change), 0x70f070 (station upgrade).

### 3.1 The transform copy stored at registration is stack residue [H code; M residue value]
- Neither the hook part 0x4f5950 nor the service stores the object's transform when it registers. Both call 0x6537d0 and
  then 0x4f4c60, which:
  - writes the pack's transform copy (+0x68..+0x94, twelve floats) from 48 bytes of its own stack frame that nothing in the
    function has written (its first instructions read rsp+0x2c..+0x5b after `sub rsp, 0x108`);
  - zeroes and recomputes the four directional firepower sums (+0x3c..+0x48) from the weapon hard points, then the
    preferred facing +0x4c.
- What is in those 48 bytes is what the registration's callees left there: allocation (0x769c58 for each new entry), list
  append (0x20a9b0) and the signal subscription (0x220e90). None of them builds a transform, so the copy never matches
  the object [M: not observed at run time]. The effect is that every registration forces a re-projection at the object's
  next due service. Without it no static object would ever reach the cells: 0x6537d0 creates entries with radius −1
  (0x653100) and never projects; 0x659760 is the only projector, and its only caller is the service.
- The hard-point state change path 0x384d00 (a weapon hard point enabled, disabled or destroyed) calls 0x4f4c60 again, so it
  forces the same re-projection.
- Check stamp +0x20: the pack constructor 0x550ed0 sets it to `frame at creation + R − 300` with R from 0x1ffb40(0, 300)
  [M: which random stream 0x140a13e24 is], so the first service after creation is due at once; each due check adds 300.

### 3.2 What the station's first battle service does [H]
In 0x4f6070, when the stamp is due and the global grid (0x140b31900, now the battle grid) exists:
1. The pack's entry count (+0x18) is above 0 (the entries the hook made in the `Load_Map` grid), so nothing is registered:
   no owner-list append, no category-mask OR into +0xc8, no per-player snapshot copy, no signal subscription.
2. The transform compare runs anyway: object transform (+0x248..+0x274) against the residue → different → 0x659760 with the
   **global** grid. Entries carry no grid pointer, so the stale entries are worked on in the battle grid.
3. For each entry, 0x656d10 (remove) then 0x656a10 (add):
   - Remove is skipped for an entry whose radius (+0x1c) is below 0. The station's entries still have the constructor's
     −1: no service ran between the create in 0x295cf0 and the delete in 0xeed80. Nothing is subtracted from the battle grid.
   - Add refreshes the entry against the battle grid (0x653a50: radius, position relative to this grid's origin, centre
     cell), adds its power to the threat layer of every hostile player, and appends it to the live list of each covered
     cell. An already destroyed weapon hard point gets radius −1 and is left out, as for any object.
4. The real transform is stored and the stamp moves on 300 frames. A static station is not re-projected again unless a
   hard point changes state (3.1).

### 3.3 Result: where the space start station is during the battle
| Grid part | Space start station (`Skirmish_<faction>_Star_Base_1`, team-marker swap) |
|---|---|
| Owner lists +0x88 | **absent all battle** [H] |
| Live cell lists +0x10 | **present** from its first UNIT_AI service, in the first frames of the battle [H code; M residue] |
| Threat layer +0x28 | **present**, same time (the +0x58 snapshot term follows the usual TEAM / HIDE_WHEN_FOGGED rule) |
| Fog memory +0x40 | as any object: the service's fogged-byte loop runs whether or not it registered, and adds / removes memory entries when the station turns fogged / unfogged for a player (0x657ce0 / 0x657db0). AI players in space: never fogged (Q1.4), so never in their memory. A human: yes |
| Category mask +0xc8 | the station's mask is not ORed in. An area query whose category mask has no bit in common with any registered object returns 0 at once, even over the station [M: matters only for such a query] |
| Signal subscription | none for the battle grid (3.4) |

Grid queries:
- **Miss it** (owner lists): `DistanceToNearestFriendly/Enemy` 0x72d000, so `Distance_To_Nearest_*_Starbase` = 1e18
  until the upgrade (the probe's `dfs` / `des`); the player force sums 0x656400; Lua `Find_Nearest` 0x69e7c0;
  `Game.ForceVisibility` 0x654940.
- **See it** (cells / threat layer): CellForce 0x6545d0 (`Location.FriendlyForce/EnemyForce[Unnormalized]`,
  `Get_Most_Defended_Position`), concentration 0x656800 (`Friendly/EnemyUnitConcentration`; it adds the type's
  `AI_Combat_Power` once per live entry whose centre cell is the queried cell [H read]), bitfields 0x654d90, point / zone
  danger 0x653ff0 / 0x653e90, the visible-cell test 0x656fe0.
- Not grid-based, unchanged: target list, `Applies`, the station's own unit tokens.

### 3.4 When the start station leaves the battle [H code; M removal; L effect]
- Entries leave the cells only through 0x6585f0 (the grid's signal handler), 0x658930 (null-list `Change_Owner`) and
  0x659760. The battle grid never subscribed to the station, so 0x6585f0 does not run for it: when the start station is
  destroyed or removed, its entries stay in the live lists and its power stays in the threat layer.
- The upgrade 0x70f070 creates `Skirmish_*_Star_Base_2` with 0x29f810 (registered normally, owner lists included) and
  then removes the old station (0x391310(old, 1) [M: object removal]). From then on the home cells hold the old station's
  power as well as the new one's [M]. What the live lists hold after the old object and its pack are freed was not traced
  [L]. The land pre-built structures have the same open point (SKAI_T15_TARGET_CHOICE §5.3).

### 3.5 Port rule (replaces Q1.1.5)
In a space skirmish the team-marker start station is:
- **absent from the owner lists** for the whole battle (`DistanceToNearest*`, force sums, `Find_Nearest`, `ForceVisibility`);
- **in the cells and the threat layer** like any registered object, projected at its first UNIT_AI service and then on the
  300-frame check (it never moves, so it is not re-projected);
- [M] left in the cells and the threat layer when it is destroyed or replaced by its upgrade.

The landed port rule (the start station out of the grid entirely, skai t22) is too strong in the cell part: `Location.*`
force, concentration, bitfields and point / zone danger must count it. This is the same split as skai-a's land rule for
pre-built base structures.

---

## Part 4. Which Space_Scout cell wins a desire tie (SKAI_T22_TIE_RE_QUESTIONS, 2026-11-16) [H unless tagged]

Read: proposal pass 0x6cb120, commit 0x6c8710, truncation 0x721b60, sort 0x720eb0 with comparator 0x7220c0, Resolve
0x6c8970, budget 0x6137b0 / 0x614590 / 0x613af0 / 0x6129c0 / 0x614880, `ForceVisibility` 0x737f80 → 0x654940, space cell
grid 0x6b9da0 / 0x4dc570; shipped XML / Lua; and the plan records of the three probe runs
(`tools/probe_mod/q2_traces/out/20261116-132329-, -132904-, -133417-skirmish-vanilla-lua/lua_records.txt`).

### Summary
**Retail breaks the tie the way the port does: among proposals with equal stored desire the first-inserted wins, through
Resolve's greedy phase, and cells are inserted x outer, y inner (CELL_0_0, CELL_0_1, CELL_0_2, ...).** The three runs
differ before the tie: at the first commit (frame 69-70) the Map_Control category, resolved before Tactical_Targeted,
accepted 2 / 1 / 0 `Secure_Build_Pad_Space` goals (runs 1 / 2 / 3), whose plans reserve the Rebel AI's two start X-wing
squadrons. Tactical_Targeted then accepted 0 / 1 / 2 scouts, in cell order: none, CELL_0_0, CELL_0_0 and CELL_0_1 (run 3's
two scouts start in the same frame; the record lists 0_1 first). A scout whose accept fails gets an activation failure,
−15 on that (Space_Scout, cell) pair for 20 s, so the cell is not proposed again for 20 s. The next commit (4.5 s) took
CELL_0_2 in all three runs, the first cell that was neither active nor penalised. The "first scout CELL_0_2 / 0_0 / 0_1"
of `SKAI_T22_GEONOSIS_SCOUT.md` is this, not a random tie-break.

### 4.1 Insertion order of space cell proposals (question 1) [H]
- 0x6b9da0 builds the space cells with x as the outer loop and y as the inner loop, names them `CELL_<x>_<y>` (0x4dc570)
  and appends each to the perception target vector (+0xe0) (AI_TACTICAL §3.2). Unit targets are appended and removed
  around them, keeping order, so the cells' relative order never changes.
- The pass cursor 0x6cb120 walks the proposal sources (the AI player type's (goal, function) pairs for the mode, 0x60f6c0)
  in order (+0x17c), and inside a source the target vector in order (+0x180). Each accepted candidate is appended to its
  category's proposed vector (0x721480; a repeated (type, target) keeps its first position). So Space_Scout proposals are
  in the order CELL_0_0, CELL_0_1, ..., CELL_0_4, CELL_1_0, ...
- The quota only spreads the cursor over frames. When a pass completes, the cursor is reset to (0, 0) and the pass flag is
  set, so every pass, the first included, starts at the first source and the first target. There is no random start,
  shuffle, hash order or resumption from the previous pass.

### 4.2 Nothing random before Resolve reorders or drops equal proposals (question 2) [H unless tagged]
- Desire = equation result (double) + learning terms, stored as **float** (0x693730 / 0x693950 take `(float)desire`).
  `Space_Area_Needs_Scouting` has no random token.
- Slot 10 accept (space 0x6b8850 → `Find_Plan_For_Goal` 0x6bae50) makes one synced draw per accepted candidate, but
  Space_Scout has one plan (`SpaceScout.lua` is the only shipped script whose `Category` names it), so the draw always
  returns it. The PotentialPlan solve (0x6c3da0) has no RNG and reserves nothing at proposal time.
- Commit step 5 (0x721b60) sorts each proposed category with std::sort (0x720eb0: insertion sort up to 32 entries,
  introsort above) and comparator 0x7220c0 (effective desire `a > b`, no tie-break), then cuts it to
  `GoalSetExtensionSize (2) + |active(category)|`. The insertion sort keeps equal entries in insertion order; above 32
  entries the order of equal entries is deterministic but not insertion order. The cut has no random part. Resolve filters
  by `Applies` and sorts again with the same comparator (0x7229d0).
- One float step [M]: cells that look tied can differ by one float step. The double desire of an unseen cell is 10 × (1 + e),
  with e of order 1e-8 from the `TimeLastSeen` normalisation, and e depends on the evaluation frame and the cell (PSTART
  readings: 9.99999987 at 2.3 s; 10.0000003 at 4.5 s; at 11.2 s two cells in the same frame, 10.00000049 and 10.00000020).
  Stored as float, a value above 10.000000477 becomes 10.00000095 and sorts ahead of earlier-inserted cells at 10.0. The
  first two commits of the three runs follow insertion order, so it did not decide them.

### 4.3 Applies and proposability of the cells (question 3) [H unless tagged]
- Region `Applies` (0x725730) tests only the Tactical_Location bit: no fog or visibility test (AI_TACTICAL §3.3). Every cell
  applies in every run.
- Space slot 9 (0x6b8900) skips only a (type, target) pair that is already active. Space_Scout has no `Is_Like`.
- `IgnoreTarget = true` only drops the contrast requirement (AI_GALACTIC §1.9.3). It does not block a target held by another
  goal.
- Learning: c1 (goal failures) × `Per_Failure_Desire_Adjust` (not set for Space_Scout) + c2 (activation failures within
  `Activation_Tracking_Duration` 20 s) × `Per_Activation_Failure_Desire_Adjust` −15. One failed activation takes the cell
  from 10 to −5, and a candidate needs desire > 0, so the cell is not proposed for 20 s. Activation failures are recorded
  only by Resolve (0x58a5d0(…, 0)), so nothing differs before the first commit.
- PotentialPlan for "Fighter | Corvette = 1" at proposal: units are reserved only at accept (0x6c2d60 marks the free-store
  entry +0x30), so during the first pass both start squadrons are free and every cell's solve succeeds [M: the solver was
  not traced for this plan].

### 4.4 The first commit: greedy, not random (question 4) [H code; M where tagged]
- **Pass count.** The first plans of all three runs start at t = 2.3 s (frame 69-70); later commits at 4.5, 7.9, 11.2 and
  14.6 s, so passes of about 66, 101, 99 and 102 frames. The first two passes run at the quota seeded at init, the later ones
  at the quota from the previous pass (AI_GALACTIC §1.3). So the 2.3 s commit ends pass 1 [M: from the timing; the port's
  first commit is also frame 69].
- **The greedy phase runs in Tactical_Targeted.** Resolve's greedy loop runs only while 0x613af0 reports available credits A
  (category +0x44) > 0; it has no mode test. In tactical, A is written once, at template init (0x6137b0: player credits ×
  normalised weight), and never again: the money parts of 0x614590 and 0x614880 and the goal add 0x610d30 are galactic only.
  `Test_Space` weights: Hero, NoBudget, StoryArc 50 each (`ReallyBig`), High_Priority 40, Map_Control 35, Med_Priority 25,
  Tactical_Targeted `Game.ForceVisibility`, Defensive 0.5, Tactical_Untargeted `1 − Game.ForceVisibility`.
  `ForceVisibility` (0x737f80 → 0x654940) counts other players' UNIT_AI objects in the owner lists, and those for which
  UNIT_AI vf+0xa0 reports visible. That slot is 0x17dc70 (`return true`), so the value is 1.0 whenever another player has
  a registered UNIT_AI object (the start squadrons are, from their create, Q1.2), else 0/0 = NaN. So Tactical_Targeted's
  weight is 1/251.5 and A = start credits / 251.5 > 0: the greedy phase runs at every commit.
  - Side result: Tactical_Untargeted's weight is 0, so its A is 0 and that category never uses the greedy phase; its goals
    come only from the weighted random phase [H code; M: ForceVisibility = 1 at the AI's first service].
  - This corrects AI_TACTICAL_PERCEPTION's `ForceVisibility` row ("[M: slot meaning]"): the visibility slot is constant true.
- The fund test 0x6129c0 returns true outside galactic, and the tactical goal ETA is 0, so the time test
  `0 < AllowedTime < ETA` never fires (A2H_T06_TACTICAL_ETA §4). The greedy stops that can hit a scout are: the head already
  kept or active, a failed PotentialPlan re-solve at accept (0x6c4a50), `Is_Like` the last old candidate, and
  `|Keep| ≥ limit`.
- **Category order.** Categories resolve in descending normalised weight (AI_GALACTIC §1.6 step 3, B0418_UTAPAU_AI_RUNS
  §7): the three 50s (Hero, NoBudget, StoryArc), High_Priority (`Tactical_Multiplayer_Build_Space_Units_Generic`),
  Map_Control (`Secure_Build_Pad_Space`), Med_Priority, then Tactical_Targeted, Defensive and Tactical_Untargeted. An accepted goal reserves its PotentialPlan's units (0x6c2d60), and a later re-solve
  skips reserved units (A2K_T08_RE_ANSWERS).
- At the first commit Tactical_Targeted has no active goal, so `limit` = 2 and the pool holds the first two tied cells,
  CELL_0_0 and CELL_0_1. The greedy phase takes CELL_0_0: if its re-solve finds a unit, it is accepted and the loop takes
  CELL_0_1. A failed re-solve records an activation failure and ends the greedy phase; the weighted random phase then draws
  among the pool entries that are not kept (weight = effective desire, one synced draw each) and re-solves the drawn goal;
  a failure there releases it and records another activation failure.

### 4.5 The three runs against this [H records; M reading]
| run | 2.3 s (first commit) | 4.5 s (second commit) |
|---|---|---|
| 1 (`132329`) | Build_Space_Units, Secure_Build_Pad_Space ×2, no scout | Space_Scout CELL_0_2 |
| 2 (`132904`) | Build_Space_Units, Secure_Build_Pad_Space ×1, Space_Scout CELL_0_0 | Space_Scout CELL_0_2 |
| 3 (`133417`) | Build_Space_Units, Space_Scout CELL_0_1 and CELL_0_0 (same frame) | Secure_Build_Pad_Space, Space_Scout CELL_0_2 |

- The Rebel AI starts with two units (`Space_Skirmish_AI_Default_Forces`: two `Rebel_X-Wing_Squadron`). Map_Control took
  2 / 1 / 0 of them, so 0 / 1 / 2 scouts could be accepted, always in cell order. The cells whose scout failed (run 1:
  CELL_0_0 and CELL_0_1; run 2: CELL_0_1) were at −5 until about 22 s, and cells with an active scout are skipped by slot 9,
  so at 4.5 s the first eligible tied cell was CELL_0_2 in every run.
- That the failed scouts failed at the re-solve for lack of a free unit is [M]: no record shows the failure, but it is the
  only greedy stop left for them (4.4).
- Why Map_Control accepted a different number of pad goals in the same commit (its `Need_To_Secure_Contestable_Space`
  desire at plan start was 10.0 in run 1 and 7.8 / 7.1 in run 2) is not answered here [L].
- Run 3 records CELL_0_1 before CELL_0_0 inside frame 70. Resolve keeps them in accept order (CELL_0_0 first) and
  instantiates them in that order (AI_GALACTIC §1.6 step 7); the order in which the plan threads first run, which is what
  the probe records, was not read [L].

### 4.6 For the port
- "First-inserted wins" is right; keep cells x outer, y inner, the greedy phase for Tactical_Targeted and the cut to
  `2 + |active|`.
- Which cell the first scout gets depends on how many start squadrons are left when Tactical_Targeted resolves, and on the
  activation-failure penalty of cells whose accept failed. Compare at the first commit (frame 69) the port's Map_Control
  accepts with retail's 2 / 1 / 0, and check that a failed scout accept records an activation failure that keeps the cell
  out for 20 s (retail's 4.5 s commit takes CELL_0_2 in every run).
- A port that computes the desire in exact or double arithmetic never sees the one-float-step differences of 4.2 [M]; they
  did not matter at the first two commits.
- Tactical_Untargeted has A = 0 in a space skirmish, so its goals come from the weighted random phase only (4.4).

---

## Part 5. How long a reserved unit stays on the tactical free store (SKAI_T22_HANDOVER_RE_QUESTIONS, 2026-11-16) [H unless tagged]

Read with ReVa and the `re_reference` decompile: free-store vtables (Space 0x8b6a38, Land 0x8b6b68, Galactic 0x8b63f8),
store constructors 0x4d22b0 (base), 0x690420 (Space), 0x690980 (Land), 0x68eaf0 (Galactic), hand-over test 0x6908f0
(tactical) and 0x690240 (galactic), free-store service 0x4d5a70, Execution service 0x526590, task hand-over 0x647df0,
AIBuildTask FSM 0x64aec0 (states 2, 3, 6, 0xb, 0xf, 8), PRODUCTION_END body 0x647ee0, WAIT_FOR_ALL test 0x64c110,
BUILD_TASK_LIST 0x648980, claim 0x4d2bd0, Add_Force (Space TF vf20 0x6e4e30, Land TF vf20 0x6e9240, base 0x5f9a90),
Space admission 0x6904b0, Space removal 0x6907d0, Space initial fill 0x6905c0, every caller of the store's removal slot
(vf+0xa0), plan instantiation 0x6bad60 (from Resolve 0x6c8970), FreeStore Setup A 0x7287a0, validity 0x6c45e0, lock test
0x39ad40, the context service 0x5f5ab0; shipped `SPACESCOUT.LUA`, `MOVETOLOCATIONRUSH.LUA` (the `Secure_Build_Pad_Space`
plan) and `PGBASE.LUA` (`BlockOnCommand`, `PumpEvents`).

### Summary
- **In tactical (land and space) a unit never leaves the free store when a build task receives it.** The store
  constructor's flag +0x48 is 1 only for the galactic store; Land and Space pass 0. On a hand-over, the service 0x4d5a70
  gives the unit to the task, lapses the other claims on that entry and empties its claim list, and erases the entry only
  when +0x48 is set. So in a battle the entry stays, with its reservation (+0x30 = the PotentialPlan), while the unit
  becomes a task-force member three frames later. Nothing in Add_Force removes it. The unit stays on the store for as long
  as its plan holds it.
- **So a running tactical plan's units can be stolen at every commit**, not only in a hand-over window. Commit step 4
  (0x6c0be0) clears every active plan's reservations. A goal accepted before the victim's goal is re-asserted (an earlier
  category in budget-weight order, or the same category while the victim is an old candidate) re-solves through Setup A,
  which sees the victim's task-force member as a free entry and may select it. The accept reserves it for the thief, the
  victim's re-test then fails in 0x6c45e0, and the victim is released. This is retail runs 1 and 3 at mf 134. No step
  holds a unit on the store for 60+ frames, because no step ever takes it off.
- **Question 1.** Land and Space share vf22 (+0xb0) = 0x6908f0. It answers "wait" only for a unit with a special weapon
  or lobbing superweapon whose recharge timer has not run out. It answers "give" when the entry's reservation is the
  task's PotentialPlan, otherwise "lapse". It has no movement, order, lock, hyperspace, idle or position test (the
  galactic 0x690240's "at a planet and not in transit" has no tactical counterpart).
- **Question 2.** With the Planning and Execution services on the same frames, the task gets the unit 0-2 frames after
  the commit (the first Planning frame), the unit joins the task force 3 frames later, the task ends 6 frames after the
  hand-over, and the plan's Lua thread passes `BlockOnCommand(Produce_Force())` 12 frames after its first run.
- **Question 3.** Nothing is special about the skirmish start squadrons: the store holds the squadron container (TEAM),
  never its fighters, and it is handed over at the first service like any reserved unit.

### 5.1 The tactical hand-over test, vf22 0x6908f0 (question 1) [H]
SpaceFreeStoreClass (vtable 0x8b6a38) and LandFreeStoreClass (0x8b6b68) both hold 0x6908f0 in slot 22 (+0xb0); the
galactic store holds 0x690240. The service calls it with the store, the unit, the claiming task and the entry's value
part (node +0x18, so the reservation is node +0x30). In order:
1. If the unit has the SPECIAL_WEAPON behaviour (obj +0x361 ≠ −1) or LOBBING_SUPERWEAPON (obj +0x369 ≠ −1):
   - no timer record at obj +0x158 → **2** (the claim lapses);
   - the timer's progress (0x4914e0: frames elapsed since its start, +8, over its span to its end, +0xc, clamped to
     0..1) below 1.0 → **1** (keep waiting; retried at the next service).

   The same weapon-ready test is Setup A's clause B, and `Fire_Special_Weapon` (0x6eb7f0) reads the same timer [H code;
   M: "recharge timer" as the meaning of obj +0x158].
2. The entry's reservation is not the task's PotentialPlan (task +0xa0 task force → +0xf0 plan → +0x88 goal → +0x68) →
   **2**. An unreserved entry (+0x30 = 0) lapses too.
3. Otherwise **0** (give).

So for an ordinary ship or squadron reserved by the task's own plan, the answer is "give" at the first service after the
claim. Moving, holding orders, `Lock_Current_Orders`, a hyperspace arrival, being idle or not: none of these is read here.

### 5.2 What a tactical hand-over does to the free-store entry [H]
- The base constructor 0x4d22b0 stores its third argument at store +0x48. The galactic constructor 0x68eaf0 passes 1;
  Space 0x690420 and Land 0x690980 pass 0.
- The service 0x4d5a70 walks the entries in list order and each entry's claimants in claim order, skipping a claimant
  that has ended (task +0xa8), already holds a unit (+0x68) or has issued production (0x6492e0). On "give" it calls
  0x647df0 (task unit = this unit, listen to its death signal 0x10, refund a charge if any, next state FREESTORE_ALLOC (3)
  in tactical), lapses (0x649070) every other claimant of the entry, and then:
  - **+0x48 set (galactic):** unsubscribes the store from the unit's signals 0x10 / 0x1a, unlinks and frees the node
    (A2L_T01 Q7.4);
  - **+0x48 clear (land, space):** only releases and empties the claim list and stops walking this entry's claimants.
    The node, its object and its reservation +0x30 stay.
- After the hand-over the task runs FREESTORE_ALLOC → PRODUCTION_END → `Add_Force` (§5.3). The space task force's
  Add_Force 0x6e4e30 (and the land one, 0x6e9240) subscribes the task force to the unit's signals and calls the base
  0x5f9a90, which appends the unit to the task-force member list (TF +0x78). None of them calls the store's removal slot.
- The store's removal slot (vf20: 0x4d5000 for land and galactic, 0x6907d0 for space = 0x4d5000 plus an unsubscribe of
  signal 0x16) is called only by: the store's own signal handler (unit destroyed 0x10, owner changed 0x1a), the
  production-complete handler 0x6498f0 (a produced unit for a task with a destination), the land Reinforce status
  0x6a1440, the spawn statuses 0x722e80 / 0x723ac0 and the galactic spawn 0x6a2520. The Produce_Force hand-over is not
  among them.
- The PotentialPlan's unit list (PP +0x108) keeps the unit (0x6c27d0 is not on the tactical path: A2M_T03 Q3, SKAI_T15
  §3.1 item 4). So at every later commit the unit is a store entry that its plan reserves, exactly like a unit that has
  not been handed over yet.

Consequences of these rules [H rules; M where tagged]:
- `On_Unit_Service` skips task-force members because their entry is reserved (+0x30 ≠ 0), not because they are off the
  store.
- `Is_Object_On_Free_Store` is true for them and `Get_Object_Count` counts them (map membership, A1_READINGS §28.5). No
  shipped script calls either.
- When a task force releases a unit (`Release_Unit`, `Release_Forces`, plan end), 0x6c27d0 finds it still on the store:
  it clears +0x30 only if this plan holds it, and drops the unit from the plan's list. There is no re-admission and no
  `On_Unit_Added` (BusyTacticalFreeStore's is empty anyway).

### 5.3 Timeline from the accept to the task force (question 2) [H unless tagged]
Frame order inside the tactical context (0x5f5ab0): Perception, Goal, Planning, Execution, Learning, each when due.
Planning and Execution have a 0.1 s interval (3 frames). Both start due at the context's first service, so they run on
the same frames [M: their initial next-service value was not read; the port's 2-frame accept-to-produce fits]. Let N be
the commit frame and R the first Planning frame at or after N (R − N = 0, 1 or 2).

| Frame | Step |
|---|---|
| N (Goal) | Resolve accepts the goal; 0x6c2d60 reserves its units (+0x30 = PP). At the end of that category's Resolve, 0x6bad60 instantiates the plan at once: pooled script, task forces with one wanted type per selection and no members, coroutines created, plan added to the Planning list. No per-goal delay. |
| R (Planning) | First resume of `MainForce_Thread`. `Produce_Force()` (0x5fe240) makes one build task per wanted entry and runs each task's FSM at once: WAIT_FOR_CREDITS → BUILD_TASK_LIST (0x648980). Tactical with `AllowFreeStoreUnits`: 0x4d2bd0 makes the task a claimant of every entry of its type (no reservation test) → PRODUCTION_QUEUED (2), uncharged; no entry of the type → PRODUCTION_FAILED (7). `BlockOnCommand` then yields in its first `PumpEvents`. |
| R (Execution, same frame) | Free-store service: the entry reserved by this task's PP answers 0 (§5.1), so the task gets the unit (state 3 pending); its claims on entries reserved by other plans, or unreserved, lapse. The entry stays (§5.2). Then the task list: PRODUCTION_QUEUED's update does nothing in tactical; FREESTORE_ALLOC's enter moves straight on to PRODUCTION_END (6), whose work runs only on update. |
| R+3 (Execution) | PRODUCTION_END update (0x647ee0): `Add_Force`, so the unit is a task-force member (and still a store entry). Then PATHING_DEST (0xb) enter: tactical with no destination → WAIT_FOR_ALL (0xf). |
| R+6 (Execution) | WAIT_FOR_ALL update: every task of this task force still in the Execution list is in 0xf (0x64c110) → PRODUCTION_FINISHED (8) → 0x64ad00: task ended, no re-admission (Add_Force happened). |
| R+3, R+6 (Planning) | The thread resumes and yields again (`BlockOnCommand` checks `IsFinished` only after a `PumpEvents`, and at R+6 Planning runs before the task ends). |
| R+9 (Planning) | `IsFinished` is true (every task ended, 0x6c6720); the closing `PumpEvents` yields. |
| R+12 (Planning) | `BlockOnCommand` returns. `SpaceScout.lua` then sets itself not removable, runs `QuickReinforce` and `Move_To`. |

- **Shortest:** unit given to the task in the commit frame, task-force member 3 frames after the commit, Lua past
  Produce_Force 12 frames after it. **Usual:** add 0-2 frames for the commit's phase. The port's "produce at 71 for an
  accept at 69" matches the hand-over time.
- **What can hold a task for 60+ frames:** only vf22's weapon-timer wait (a special-weapon or lobbing-superweapon unit).
  A late thread start is not possible (instantiation is synchronous and Planning runs every 3 frames), and movement is
  never waited for in tactical. None of this changes how long the unit stays on the store: in tactical that is always
  the plan's whole life.

### 5.4 The steal at the next commit (runs 1 and 3 at mf 134) [H rules; M application]
Rules (A1_READINGS §28.6, A2M_T03 Q4, SKAI_T15 §2.2; Setup A re-read here):
1. Commit step 4 (0x6c0be0) clears the reservations of every active goal's units, task-force members included.
2. Categories resolve in budget-weight order: Map_Control (`Secure_Build_Pad_Space`) before Tactical_Targeted
   (`Space_Scout`) (Part 4.4). Inside a category, a kept goal is re-asserted (0x6c2d60) when it is classified; a new
   accept re-solves (0x6c4a50 → 0x6c3da0, solve counter ≥ 1).
3. Setup A 0x7287a0 in that re-solve takes any store entry with +0x30 = 0 that passes: health ≥
   `Health_Low_Percent_Threshold`; clause A (for a plan without `AllowEngagedUnits`, such as `MoveToLocationRush.lua`: a
   TEAM whose locomotor pack obj +0xa8 has a dogfight cell, +0x1c4 ≠ −1, is skipped); clause B (the weapon timer of §5.1);
   not garrisoned (obj +0x210), not in limbo (+0x3a7), not in a hero clash (+0x3b4), not movement-locked (0x39ad40: lock
   count, or a Divert / unload-exit path flag; a plain `Move_To` does not lock); type not INDIGENOUS; no occupied build
   pad. Task-force membership is not tested.
4. The accept reserves the chosen units for the thief. When the victim's goal is re-tested later in the same commit,
   0x6c45e0 finds a unit of its list on the store with +0x30 ≠ 0 and ≠ its own PP: invalid, so it is released (budget
   release, plan end with `Release_Forces(1.0)`, goal release; SKAI_T15 §2.2, §4), unless its plan definition has the
   keep-despite-invalid flag (+0xa8). `Set_As_Goal_System_Removable(false)` does not protect against this; it only keeps
   a goal out of the old-candidate path.
5. The victim's `Release_Forces` takes the unit out of its task force, and 0x6c27d0 leaves the thief's reservation alone.
   The thief's plan was instantiated at the end of Map_Control's Resolve, so its `Produce_Force` runs at the next
   Planning frame and its claim is honoured at that frame's Execution (§5.3).

Applied to the runs [M]: at mf 67 the first pad (Map_Control, accepted first) reserved one start squadron and the scout
the other. At mf 134 step 4 cleared both. Map_Control re-asserted the kept first pad, then accepted a second pad (obj:98)
whose re-solve found the scout's squadron with +0x30 = 0 (the scout's category had not run yet) and took it.
Tactical_Targeted's re-test of the scout then failed in 0x6c45e0 (`retest_leave` 0; with `IgnoreTarget` the validity part
alone decides). In run 2 no second pad was accepted at 134, so nothing took the squadron and the scout lived to 934. The
same can happen at any later commit while the scout runs, which fits retail's shorter scout lives (median 464 frames
against the port's 909).

### 5.5 The start squadrons (question 3) [H unless tagged]
- Space admission 0x6904b0 admits an object of the store's player that is not a squadron member (type +0x29) or a company
  member (type +0x2a), and either has SPECIAL_WEAPON, or has a non-dummy type, behaviour 0x13 or TEAM (0x16) and a
  locomotor; its type must not be DUMMY_ORBITAL_STRUCTURE (0x60) or DUMMY_STAR_BASE (0x5c), it must not have behaviour
  0x2c (+0x35e), and its parent (if any) must have nothing at +0xf8. So the store holds the X-wing **squadron
  container**, never its fighters, and build tasks match the squadron type.
- Objects that exist when the store starts are admitted by the Space initial fill 0x6905c0 (every object of the player,
  through the same admission) [H code; M: its caller was not read]; objects created later come through the GOM add hook
  (AI_TACTICAL §5.1). The start forces (0x2a4fc0) are on the store before the first commit (Part 4.5).
- The tactical hand-over has no transit or position test (§5.1), and no tactical "just placed" state delays it. A start
  squadron reserved by a plan is handed over at the first service after its task's claim and, like every tactical unit,
  stays on the store afterwards.

### 5.6 Corrections to earlier docs
- SKAI_T15_PLAN_RETEST §3.1 item 3 ("the store node is erased") and §5 row 1 ("Another PP's solve taking task-force
  units: Out") hold for the galactic store only. In land and space the node stays, so task-force members are Setup A
  candidates whenever their reservation is suspended, and a steal is also a way for a land plan to end at a commit.
- A1_READINGS §28.1: 0x6907d0 is the **Space** removal override (base 0x4d5000 plus an unsubscribe of signal 0x16); land
  and galactic use 0x4d5000. §28.6 "the task force takes it off the store (Produce_Force / Collect)" holds only for the
  galactic store.
- AI_TACTICAL §5.2 ("or taken by a task force: erase from the map"): a Produce_Force hand-over does not erase the entry.
- A2L_T01 Q7.4 and A2M_T03 Q3 already tie the erase to the galactic +0x48 = 1; they stand.

### 5.7 For the port
For "a scout accepted with a pad at commit N loses its unit to a pad accepted at commit N+1" as in retail runs 1 and 3,
and not when no second pad is accepted:
1. **A tactical hand-over keeps the unit on the free store.** When a build task receives a reserved free-store unit
   (Produce_Force, land and space), keep its store entry with its reservation and clear only the entry's claims. The
   unit joins the task force one Execution service later while staying a store entry. Remove a tactical entry only on
   death, owner change, and the spawn / reinforcement / produced-with-destination paths of §5.2.
2. **Keep the reservation life cycle on those entries:** every commit suspends all active plans' reservations first and
   re-asserts each kept goal's when its category classifies it. The accept re-solve's Setup A sees task-force members
   whose reservation is suspended as free units, with the exclusions of §5.4 item 3 and no task-force-membership test.
3. **The stolen check** (0x6c45e0) covers task-force members: a listed unit that is on the store and reserved by another
   plan makes the plan invalid; release it as in SKAI_T15 §2.2.
4. **The free-store service skips reserved entries** (task-force members) for `On_Unit_Service`, and hands an entry only
   to a task of the plan that reserves it (vf22 answers "lapse" otherwise).
5. **Releasing a unit** (`Release_Unit`, `Release_Forces`, plan end) in tactical only clears this plan's reservation and
   drops the unit from this plan's list; the entry already exists.
6. **Timing:** hand-over at the first Execution frame at or after the first thread resume (0-2 frames after the commit),
   task-force membership 3 frames later. The port's numbers already match; only the removal from the store is wrong.
