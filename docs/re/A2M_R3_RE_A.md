# a2m round 3, RE answers A: Underworld starbase upgrade, ground forces, saboteur plan ends, cycle-1 income

Clean-room RE answers for the blocked a2m round-3 Rebel fix lane (2026-11-12). The questions are those in the r3 RE
request and in `docs/re/A2M_R3_REBEL_UW_TRACE.md` §6 on `claude/a2m-fix-rebel-r3` (fca618d9). The decompiled output was
read for this note. It holds behaviour only, no code. Tags: [H] read in the code, [M] inferred, [L] guess. Data facts come
from the shipped FoC XML (`extracted/foc_data_text.zip`).

Retail evidence is from the three HotM human-Empire traces in `tools/probe_mod/q2_traces/out/`, mainly run
`20261108-170824-galactic-vanilla-lua` (`trace.q2t`, `production.q2q`, `lua_records.txt`). The other two runs agree on
every event used here, so the three runs count as one sample. Port evidence is from the r3 lane's seed-0 dumps (`pp.txt`,
`goals.txt`).

Corrections to existing docs (made in this commit):
- `AI_GALACTIC.md`, budget, "Can fund": the accept-time fund test (slot 10) is 0x6139f0 and reads **P / I** (pool and
  income rate). The A / R test 0x6129c0 is used only by commit, resolve and maintenance. See §1.1.
- `AI_GALACTIC_2.md` §1.2: the generic-to-planet replacement 0x3f8fd0 is now spelled out (§1.2 below). It returns the
  planet's **next-level** starbase, makes no can-build test, and dedupes every expanded team type into one candidate.

Short answers:

| # | Question | Answer | Confidence |
|---|---|---|---|
| Q1a | Accept test for Upgrade_Starbase | Slot 10 accepts when a plan solves and the gross test 0x6139f0 passes: `P + max(min(ETA, TL), T) × I ≥ goal cost`. There is no "upgrade available" test of its own. The port accepts with A / R (0x6129c0), which is stricter. | rule [H]; effect on 11059 [M] |
| Q1b | What 0x3f8fd0 returns | The next-level starbase of the starbase already on the target planet (Underworld_Star_Base_1 → Underworld_Star_Base_2), after a level gate. On a planet with no starbase: Rebel_Star_Base_1 / Empire_Star_Base_1 for those two factions only, otherwise nothing. IgnoreTarget plans still use the goal's target planet. | [H] |
| Q1c | Type queued at run time; can a task wait forever? | The replaced type. A bound structure task never bids and is never released, but it fails as soon as 0x2804d0 refuses the type at its planet, so it cannot hold the plan forever. | [H] |
| Q1d | What changed by 11059 | Not settled by RE. The strongest lead is the goal-set truncation: competing Infrastructure goals that retail accepts and the port rejects. | [M/L]; needs a retail run |
| Q1e | Budget category and cost | Infrastructure. Budget marshaller uses cost(T, target) and btime(T, target), clamped to Time_Limit 30. The ETA uses the space queue, which is empty at NalHutta, so the r2 queue-wait rule does not gate it. | [H] |
| Q2 | What keeps MAL / Destroyer / Canderous out before 9664 | No admission rule. The Budget marshaller vetoes on the Offensive category's A / R, and the contrast float decides which of the affordable units are taken. Queued but unpaid PAYG items keep their allocation reserved out of A until paid. | rule [H]; per-frame amounts [M] |
| Q3 | Saboteur plans that end 8 / 105 / 118 frames after queuing | Tag 65 (2839): blockade by Rebel fleet 675 at Shola → Production_Failed → script exit → cancel with refund [M]. Tags 60 / 68: a goal-system release that reclaims the build [M]. The maintenance time drop cannot cause it. | [M]; needs a retail run |
| Q4 | Which planet pays at cycle 1 (1351) | Naboo, corrupted by the starting saboteur at 617. 2150 = 2105 base income + 90 × 0.5 (Intimidation). Retail corrupts nothing else until MonCalimari at 10802. | [H] trace |

---

## 1. Q1: the Underworld Upgrade_Starbase goal

### 1.1 Accept (slot 10, 0x6b8660) [H]

1. Find_Plan_For_Goal (0x6bae50) picks a plan and solves its PotentialPlan. If no plan solves, the goal is rejected.
2. If AllowedTime (0x692860) ≤ 0, or AllowedTime ≥ the goal ETA (+0x7c), the gross fund test 0x6139f0 must pass:
   - If the budget is switched off for the player, the test passes.
   - If the goal already has a budget record (0x612bd0), the test uses that record's own horizon and request.
   - Otherwise: horizon = goal ETA, replaced by Time_Limit when Time_Limit ≠ −1 and Time_Limit ≤ ETA. Then
     horizon = max(horizon, budget +0x70), where +0x70 is the budget period T (45 s).
     money = max(horizon × I + P, 0), where P = category +0x4c (the pool) and I = category +0x50 (the income rate).
     The test passes when money ≥ the goal's cost (0x692780).
3. There is no separate "upgrade available" test. Availability is decided only by whether the PotentialPlan solves
   (§1.2 and §1.4).

The A / R test 0x6129c0 (`max(horizon, lastDt) × R + A ≥ need`) is used at commit (0x6c8970), in the greedy and weighted
phases and by maintenance (0x722a30). It is **not** the accept test. [H]

Port: `PlayerAi::can_fund` (`engine/ai/src/galactic_ai.cpp`) calls `Budget::can_fund`, which is the A / R test. Accept
must use P / I with the horizon rule above. **Tier-1 difference.** The port's accept therefore rejects goals that retail
accepts whenever A is far below P, which happens once goals hold allocations.

### 1.2 Candidate replacement, 0x3f8fd0(planet behaviour, planet) [H]

Called by the candidate builder 0x6c1fb0 for every team type with behaviour 0x5C (DUMMY_STAR_BASE). 0x3f8c40 does the
same for 0x5B ground bases.

- The faction is the faction of the **planet's owner**, not the faction of the team entry. So the entry
  `Rebel_Star_Base_1` in UPGRADESTARBASEPLAN.LUA is only a generic placeholder.
- No starbase on the planet: Rebel owner → Rebel_Star_Base_1; Empire owner → Empire_Star_Base_1; any other owner,
  Underworld included → no replacement (0).
- A starbase on the planet: the candidate is that starbase type's Next_Level_Base (type +0x8c0).
- Level gate on the candidate:
  - If a starbase item (behaviour 0x5C) is in either production queue of the planet (0x401db0), the allowed level is 0.
  - Else, if the starbase has no owner (D+0x13c = −1), the allowed level is 1 when the planet's MaxStarbaseLevel is not 0.
  - Else, if the current level is below 5, the allowed level is level + 1, set to 0 when MaxStarbaseLevel (0x372780:
    the planet data D+0x238 from `Max_Space_Base`) is below level + 1.
  - The candidate is returned when its Base_Level (+0x8a0) ≤ the allowed level; otherwise nothing.
- **No can-build test (0x2804d0) is made here.** Buildability is checked later, in Production Setup C.
- The planet passed in is the goal's target planet, also for IgnoreTarget plans: IgnoreTarget changes contrast and
  costing, not the planet used for replacement.
- The team expansion (0x66fcc0 → 0x407b60) yields one type per faction and level. Every 0x5C type in it maps to the same
  result, and 0x6c1fb0 dedupes through its set, so the plan gets **one** starbase candidate.

HotM data: NalHutta and Shola have Max_Space_Base 5; Underworld_Star_Base_2 has Tech_Level 0, Base_Level 2,
Required_Star_Base_Level 1, cost 1600 and build time 20 s. So at NalHutta (Underworld_Star_Base_1, empty space queue)
0x3f8fd0 returns Underworld_Star_Base_2 from the start. The level gate does not explain the late start. [H]

Port: `GalacticAiWorld::Impl::next_starbase_type` returns the **first StarBase type for which `can_build_at` holds**. This
differs from retail in three ways:
1. It is not keyed on the current starbase's Next_Level_Base.
2. On an Underworld planet with no starbase, it can return Underworld_Star_Base_1, where retail returns nothing.
3. It applies the can-build gate early, so a temporary refusal (a hostile fleet in orbit, a starbase already queued)
   removes the candidate instead of letting Production Setup C veto it.

The port dump also shows "6 candidates" for UPGRADESTARBASEPLAN, where retail would have one plus the non-structure
types. The lane should check that the port dedupes. [M]

### 1.3 Run time (Produce_Force → AIBuildTask → AIPlanetBuildTask) [H]

- The type queued is the **replaced** type from the PotentialPlan selection, here Underworld_Star_Base_2 at the target.
- When 0x3f8fd0 returned nothing, the generic expansions themselves stay in the list. Production Setup C vetoes them
  permanently when 0x2804d0 refuses them (step 2, wrong faction, or step 20, the DUMMY_STAR_BASE level rule).
- A structure task is bound to its planet (build market 0x53cd60). It never bids for a slot and is never released by the
  market. It fails when 0x2804d0 no longer allows the type at that planet. So **a task cannot wait forever and hold the
  plan**: it is either buildable at its planet or it fails. (See `A2J_RE_BUILD_MARKET.md`.)

### 1.4 Budget category and cost [H]

- UpgradeStarbasePlan's goal is an Infrastructure goal. It is budgeted in the **Infrastructure** category.
- Budget marshaller: cost = cost(T, target planet), time = btime(T, target planet) for the replaced type, with the
  horizon clamped to the goal's Time_Limit (30).
- ETA 0x6639f0 puts a starbase in the **space** queue pool (q1). NalHutta's space queue is empty, so the r2 rule
  (`A2M_R2_RE_A.md` 1b: the ETA includes the queue wait at the target) does not delay this goal.
- The port's dump has cost 1600 and ETA 16.00 at NalHutta. Retail's ETA would use btime 20 s with the build-speed
  multiplier, as the port does. No difference is known here.

### 1.5 Why retail starts it only at 11059 [M/L]

Nothing in 0x3f8fd0, the accept test, the budget category or the run-time path blocks the goal at NalHutta. Retail
proposes it every pass with desire 34. What remains is competition in the commit:

- Commit truncates the accepted set to GoalSetExtensionSize (2) + |active| and then runs greedy and weighted phases by
  effective desire (`AI_GALACTIC.md` §1.6). A goal with desire 34 is cut whenever two or more accepted Infrastructure
  goals rank above it.
- Retail keeps proposing vehicle-factory goals (cost 2400, desire estimated at about 90 to 110, above 34). It finishes a
  second NalHutta vehicle factory at 9623, so it keeps those goals alive for a long time. With retail's lenient P / I
  accept (§1.1) these goals pass accept and fill the truncation slots. [M]
- The port shows the other side. From frame 74 to 5460, Build_Underworld_Vehicle_Factory at @2 / @8 / @9 and
  Upgrade_Starbase all fail accept with "fail Plan (6 candidates, 0 selected, last veto Plan)". At 5460 Upgrade_Starbase
  passes accept, and at 5512 it is taken greedily. Retail takes it at 11059. The port's Plan veto on the vehicle factory
  removes the competitor that retail keeps. [M]
- At 9626 NalHutta's ground level reaches 5, the maximum. NextGroundUpgrade becomes 0 and the vehicle-factory desire at
  NalHutta falls to about 18. The vehicle factories at Shola and Felucia were estimated still above 34, so this does not
  close the gap to 11059 on its own. [L]

Port actions:
1. Replace the accept test with 0x6139f0 (P / I, horizon rule of §1.1).
2. Make `next_starbase_type` follow 0x3f8fd0 (§1.2). Keep the can-build veto in Setup C, not in the replacement.
3. Find out why the port's Plan marshaller vetoes the vehicle factory from frame 74. Retail keeps proposing it and
   builds a second one at 9623, so retail does not veto a second vehicle factory on the same planet.

Then re-measure. If the port still starts the upgrade before about 11000, use the retail probe in §5 to read retail's
Infrastructure proposals around 5000-11000.

---

## 2. Q2: Underworld Build_Ground_Forces before 9664

**Rule [H].** Setup B has no admission rule that removes MAL_Rocket_Vehicle_Company, Destroyer_Droid_Company or
Canderous_Assault_Tank_Company:
- They are Underworld-affiliated, so 0x729550 admits them.
- Production Setup C finds Shola (U_Ground_Vehicle_Factory) and NalHutta (U_Ground_Droid_Works) as sources.
- TechTree passes.

The Budget marshaller then vetoes any unit whose cost does not fit in the Offensive category's money by the horizon:
`A + horizon × R`. Among the affordable units, the contrast float decides what is taken (`AI_GALACTIC_2.md` §1.5,
0x72b940).

**Retail amounts [M, from the trace].** `lua_records.txt` PSTART lines record the plan's unallocated amount, which equals
the net cost of its selection:

| Frame | Unallocated = net cost |
|---|---|
| 1501 | 1320 |
| 4174 | 1640 |
| 5785 | 1320 |
| 8368 | 2920 |

These sums fit MDU and Merc companies only. A MAL, Destroyer or Canderous company would push the selection past the
category's money at those frames.

**Port [M].** At 1412 the port has already vetoed MAL on budget, as retail would. The Destroyer company survives the
budget and wins the contrast float. So the port's Offensive A / R at that frame is higher than retail's. The likely cause
is the PAYG reservation below, or the accept-test difference of §1.1 changing which goals hold allocations.

**PAYG sub-question [H].** Queued but unpaid PAYG items **do** reduce the category's available amount:
- Add goal (0x610d30) computes need = req + alloc − a and takes it from A.
- The allocation stays reserved until the item is paid.
- Cancel (0x53ba60) refunds only the paid fraction; the unpaid part goes back to the goal's request.

Port action: check that the Offensive category's A in the port subtracts the allocations of queued-but-unpaid items, the
same way Add goal does. Then compare A and R at 1351 to 4300 against the retail probe of §5.

---

## 3. Q3: saboteur plans that end soon after queuing

Retail tags in run 170824: 60 queued at 1573 ended after 8 frames; 68 at 4146 after 105 frames; 65 at 2839 after 118
frames.

- **Maintenance time drop: impossible.** Once production has started, the goal production-time estimate 0x525200 is 0,
  so the maintenance step (0x722a30) cannot drop the goal for being too slow. [H]
- **Tag 65 (2839 → 2957) [M].** Rebel fleet 675 passes through Shola and arrives at 2945. A hostile fleet in orbit
  blockades the build planet. The plan gets Production_Failed, its script exits, and the plan-end cancel refunds the
  paid fraction (`A2M_R2_RE_B.md` §1).
- **Tags 60 and 68 [M].** The trace shows no hostile object near the source or target planet in that time. The remaining
  path is a goal-system release: the goal is dropped in resolve or replaced in the weighted phase. The release reclaim
  0x5f7d40 sets the plan's +0xD9 flag, and the queued saboteur is cancelled with refund.
- The trace cannot tell which goal-system path ends 60 and 68, because the goal type, target and desire are not
  recorded.

Port action: none from RE alone. Check that the port's blockade sends Production_Failed to the plan, and that a released
goal cancels its queued build. The exact path for 60 and 68 needs the retail probe in §5.

---

## 4. Q4: cycle-1 income

Retail [H, trace]:
- Before 1351 the Underworld corrupts **only Naboo**: the starting saboteur at frame 617.
- At 1351 the Underworld gets 2150. That is 2105 base income plus Naboo's corruption income: 90 (Planet_Income gross) ×
  0.5 (Corruption_Choice_Income_Percentage for Intimidation).
- The second starting saboteur, 595, dies at 1808 without corrupting anything.
- The next corruptions are MonCalimari at 10802 and Kuat at 12749.

Port: seed 0 corrupts **MonCalimari at 444**, because its Corrupt_Planet desire (25.5) is above Naboo's (24.0). This is
where the port's 2282 at cycle 1 comes from. The difference is in the Corrupt_Planet desire or target choice, not in the
income formula. The port's corruption income formula matches retail. [M]

The three retail runs make the same choices, so they are one effective sample. Whether retail **ever** picks MonCalimari
first needs more seeds, or the probe of §5.

---

## 5. Retail probe for the open parts (run 2026-11-16, gai3 t04 / t07: results in §7)

Kit: a variant of `tools/probe_mod/a2f_budget` with a **human Empire** (today the kit is fixed to a human Rebel), HotM,
run to frame 12000. Channels, written to `lua_records.txt` every fiscal cycle and on every event:
- Underworld budget categories (Infrastructure, Offensive): P, I, A, R.
- PSTART / PEXIT of UpgradeStarbasePlan, BuildGroundForcesPlan, BuildUnderworldVehicleFactory and
  AI_PLAN_UNDERWORLD_DEPLOYSABOTEUR, each with goal type, target planet, desire and exit reason.
- Corrupted planets and their corruption type at each cycle.

This answers Q1d (which Infrastructure goals rank above Upgrade_Starbase from 5000 to 11059), the exact paths for
saboteur tags 60 and 68 (Q3), and the Offensive A / R per cycle (Q2).

## 6. Other note

The contrast-float question from the earlier r3 diagnosis is already answered in `AI_GALACTIC_2.md` §1.5. It was
re-checked in 0x72b940 and needs no change. [H]

The commit's greedy-phase "already active" check (0x6c8970) runs after the active set has been cleared for the new
commit. A goal that was active in the last commit is not skipped as "already active". [M]

## 7. Probe result: human Empire to frame 12000 (gai3 t04 / t07, 2026-11-16)

Run `tools/probe_mod/a2f_budget/out/20261116-020754`:
- Kit: `run_a2f_uw.cmd`, which runs `a2f_run.py --human-empire --saboteur --uw-infra`.
- Setup: HotM, Medium, hands off, no battle prompts. Records reach frame 12001.
- Output: report section 10, `plans.tsv` (goal and target columns), `uw_infra.tsv`, `samples.tsv`.

**Probe effect [M].** Channel U evaluates perceptions every 30 frames, and EvaluatePerception shares the AI's leaf cache
(README of the kit). The Underworld's early plan sequence here differs from the hotm_galactic q2 reference runs:
- here: the first Build_Ground_Forces at 1582, a Destroyer company;
- the references: a Build_Ground_Forces at about 1501, MDU at Shola about 1537 and Merc at Felucia about 1552.

So use the frame numbers below for mechanisms, not as a trajectory to match.

### 7.1 The first Underworld ground company (t04, b0157)

| Frame | Event |
|---|---|
| 1351 | Cycle-1 income. Credits are 2150. |
| 1380–1486 | Underworld pools: Offensive A 303, Infrastructure A 297, rate 0. |
| 1487 | The budget is recomputed: Offensive A 1082 + 16.15/s, Infrastructure A 1068 + 31.63/s. This is 136 frames after the payment. |
| 1582 | `BuildGroundForcesPlan` starts. It has no target (`tgt=ERR`) and is a global Build_Ground_Forces goal. Its selection's net cost (unallocated) is **1200**: one Destroyer_Droid_Company. Credits are 2150, Offensive A is 1082 + 16.15/s. |
| 1635 | QB: Destroyer_Droid_Company at **Nal Hutta**, cost 1200, built from U_Ground_Droid_Works. |
| 1635 + 362 | Retail's build time for this company in the parity references: 15 s Build_Time_Seconds × 24.1 frames per second. |

Findings:
- **The selection admits a Destroyer company with A < cost [H for this run].** At 1582, Offensive A (1082) is below
  the company's 1200, but A + horizon × R covers it. So the §2 premise (a Destroyer company would not fit the
  Offensive money) holds for that human-Rebel trace, not in general. The port picking a Destroyer company first is
  not a parity failure by itself.
- **Pay-as-you-go runs at cost / build frames, without a stall [M].** Underworld credits fall 2150 → 1956.7 by 1693,
  about 3.3 per frame. That matches 1200 / 362 frames (the saboteur at Shola paid in the same window adds a little).
  Credits never reach 0, so the build takes exactly its build time.
- **Where the port's DESTROYER_DROID_COMPANY@NALHUTTA build.ticks (658–670 vs 362) comes from.** It is not the
  selection. The port's build stalls when the pay-as-you-go installments run out of credits between income payments.
  That is gai3 t03's stall, and the port's Underworld credit level (credits_at_13500: 570 vs retail 6620) feeds it.
  Fixing t03 / the Underworld spending fixes this metric; there is no separate t04 port change.

Later Build_Ground_Forces selections (unallocated = net cost):

| Frame | Net cost | Selection, from QB |
|---|---|---|
| 2806 | 1770 | Disruptor Merc at Felucia 2854 (450), MDU at Nal Hutta 2875 (1000), Merc at Felucia 3102 (320) |
| 5497 | 1400 | MAL at Shola 5536 |
| 10942 | 1970 | Disruptor Merc 450, Merc 320 and Destroyer 1200, all at Nal Hutta |

Build_Space_Forces ran at 52 (2400: Interceptor4 at The_Maw) and later at 6841, 8269 and 9541 (1400 each: Crusader at
Felucia).

Build plans with goal and target:
- **Palaces:** both start at 52, at Shola and Felucia. QB at Felucia 64 and Shola 80.
- **UpgradeStarbasePlan:** at 4183 for Shola (QB Underworld_Star_Base_2 at 4200), and at 10942 for Nal Hutta
  (QB 10966).
- **BuildUnderworldVehicleFactory:** at 9541 for Shola (QB 9566).
- No Barracks or Droid Works plan ran.

So the Shola starbase upgrade comes at 4183 here, against 11059 in the human-Rebel trace of §1.5.

`samples.tsv` decodes Offensive A as −19678 + 1178.64/s at 9625. That is a decode artifact of the budget block in that
frame (Infrastructure decodes negative too); it is not a game value.

### 7.2 Underworld infrastructure equations (t07, b0135)

Each equation was evaluated every 30 frames up to 12000 on every Underworld planet. `uw_infra.tsv` has one row per
sample; the report lists a row whenever a value changes.

| Frame | Felucia | Nal Hutta | Shola | The_Maw |
|---|---|---|---|---|
| 0 | SBB 34.92, NGU 0.88, SBS 17 | SSD 1, NGU 0.914, SBS 34 | SBB 24.89, NGU 0.766, SBS 17 | SBB 18, NGU 0, SBS 27 |
| 690 (palaces done) | SBB 0, SSD 2 | | SBB 0, SSD 2, SBS 34 | |
| 720 | SSD 1 | | SSD 1 | |
| 780–960 | SSC 1 | SSC 1 | SSC 1 | SSC 1 |
| 990–1770 | SSD 1, SBS 17 | SSD 1, SBS 34 | SSD 1, SBS 34 | SBB 18, SBS 27 |
| 1800–2970 | SSC 1 | SSC 1 | SSC 1 | SSC 1 |
| 3000 onward | SSD 1, SBS 17 | SSD 1, SBS 34 | SSD 1, SBS 34 | SBB 18, SBS 27 |
| 5700 | | | SBS 17 | |
| 10800 | | | SBS 0 | |
| 11700 onward | SSC 1 | SSC 1 | | SSC 1 |

Key: SBB Should_Build_Underworld_Barracks, SSD Should_Underworld_Save_For_Desirable_Structures, SSC
Should_Save_For_Corruption, NGU Needs_Groundbase_Upgrade, SBS Should_Underworld_Build_Starbase.

Notes on the table:
- **Corrupted planets.** All four are Underworld-owned and report `cor=true`. The equations' planet is the
  Underworld's own.
- **SSC = 1 zeroes the others.** While Should_Save_For_Corruption is 1, SBB, SSD and SBS read 0 on every planet. The
  windows are 780–960, 1800–2970 and from 11700.
- **SBB.** After the palaces, Should_Build_Underworld_Barracks is 0 everywhere except The_Maw (18). No Barracks plan ran; a likely reason is that The_Maw has no land map [M, not checked].
- **NGU.** Needs_Groundbase_Upgrade is constant per planet in this run.
- **SBS.** It follows the starbase level: Shola drops 34 → 17 after its upgrade at 4200, and to 0 at 10800.

Port check: evaluate the same five equations on the port's seed-0 HotM state at the same frames. Agreement here
removes the infrastructure ranking as a cause of the gai3 t03 spending difference.

**Port check (tst2 t07, 2026-11-20).** Command, one run per frame F (the run ends at galactic frame F and the equations are
evaluated after its last tick, so the AI leaf-cache entries they fill change no simulated state and no CRC):

```
alamo_headless --economy --galactic-ai --campaign Sandbox_Heart_Of_The_Maw_Empire --human Empire --seed 0 --story
  --workers 4 --quiet --ticks F --eval-player Underworld
  --eval-equations "Should_Build_Underworld_Barracks,Should_Underworld_Save_For_Desirable_Structures,Should_Save_For_Corruption,Needs_Groundbase_Upgrade,Should_Underworld_Build_Starbase"
```

`--eval-equations` also takes `NAME=EXPRESSION` entries (separated by `;`), which is how the token inputs below were read.
Context: Self = the Underworld (player 6), Target = Location = the planet, Human = the Empire; Enemy unbound (no function of
the five closures reads Variable_Enemy).
Port values (dev `1d7a7d74` + tst2), at frames 1, 30, 690, 720, 780, 960, 990, 1770, 1800, 2970, 3000, 4200, 5700, 10800, 11700
and 12000:

| Frames | Felucia | Nal Hutta | Shola | The_Maw |
|---|---|---|---|---|
| 1, 30 | SBB 34.92, NGU 0.88, SBS 17 | SSD 1, NGU 0.914, SBS 34 | SBB 24.89, NGU 0.766, SBS 17 | SBB 18, NGU 0, SBS 27 |
| 690–5700 | SSD 1, NGU 0.851, SBS 17 | SSD 1, NGU 0.914, SBS 34 | SSD 1, NGU 0.737, SBS 34 | SBB 18, SBS 27 |
| 10800 | as above | NGU 0.886 | SBB 18, SSD 0, NGU 0, SBS 0 | as above |
| 11700, 12000 | as above | NGU 0 | as 10800 | as above |

SSC is 0 at every sampled frame on every planet.

**Agreement.**
- Frame 0: all 20 values equal retail's.
- 690 onward, outside retail's SSC = 1 windows: SBB, SSD and SBS are equal on every planet up to 3000. Nal Hutta and The_Maw
  stay equal to 5700. The one-sample SSD 2 at retail 690 (Felucia, Shola) is the frame the palaces finished. The port's palaces
  finish between 660 and 690, so a sample at 690 already shows SSD 1, as retail's 720 does.
- So, given the same planet state, the five equations and their closures (Should_Build_Underworld_Vehicle_Factory,
  Should_Build_Underworld_Palace, Needs_Starbase_Upgrade) give retail's values. **The equations do not cause the gai3 t03
  spending gap.**

**Differences: all come from the AI's trajectory, not from an equation.**
1. **SSC is never 1.** Its inputs in the port: palaces complete = 3 (> 1) from 690; Game.PlanetsCorrupted 0.33, 0.42 from
   780, 0.5 by 10800, 0.58 by 11700 (always < 0.67). `Variable_Self.HasUnit {Underworld_Saboteur}` is 1 at every frame from 100
   to 2970. HasUnit counts queued builds too (AI_GALACTIC_TOKENS §3). The port does consume the saboteur: the complete-only
   count is 0 at 620 (after the Naboo corruption at 601) and at 1800. But a replacement is already queued (Saboteur_Team
   ordered at 140, 1542, 1635, 1790 by the DeploySaboteur plans). So the port's Underworld always owns or queues a
   saboteur, and retail had none in 780–960, 1800–2970 and from 11700. In those windows SSC = 1 zeroes SBB, SSD and SBS (and
   the vehicle-factory and palace desires), so retail holds infrastructure money back for about 1700 of the first 3000 frames.
   The port never does.
2. **Shola after 4200.** Retail upgraded Shola's starbase at 4183 (SBS 34 → 17 at 5700, 0 at 10800). The port has not
   upgraded it by 5700 (SBS 34). By 10800, Shola has lost its starbase and ground base: StarbaseLevel 0 and
   GroundbaseLevel 0, so SBS 0 because `StarbaseLevel > 0` fails, SBB 18 and SSD 0. Nal Hutta's NGU change at 10800 is
   more structures built there.
3. **NGU after the palaces: open.** The port's Needs_Groundbase_Upgrade drops by 0.2 / 7 on Felucia and Shola when their
   palace finishes (GroundbaseLevel 0.2 → 0.4: one more live ground structure). Retail's note above says NGU stayed constant.
   But retail's own SSD rising at 690 needs `GroundBaseLevelUnnormalized > 1`, and both tokens read D +0x130 (lifetime 0). So
   the "constant" note and the SSD row cannot both hold unless MaxGroundbaseLevel moved too. `uw_infra.tsv` is not on disk to
   settle it. The effect on SBB and the factory / palace desires is about 0.5 (18 × 0.029): no ranking changes.

Filed: item 1 is an S2 task of gai3, the owner of t03: the Underworld rebuys a saboteur at once, so it never saves for
corruption. Item 3 is an S3 backlog entry: re-read the NGU row in the next a2f_budget `--uw-infra` run.
