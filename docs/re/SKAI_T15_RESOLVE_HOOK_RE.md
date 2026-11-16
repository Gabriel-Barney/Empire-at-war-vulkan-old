# skai t15: what a Resolve_Category hook can read (RE, 2026-11-16) [H unless tagged]

Offsets for a read-only Frida hook on Resolve_Category (0x6c8970) that logs, at each commit of a land tactical AI, every
candidate and active goal with its goal type, target, desire and effective desire, and which goals survive. Its purpose is
to name the `Tactical_Targeted` proposal that displaces a running `Destroy_Structure` plan (SKAI_T15_TARGET_CHOICE Q3,
SKAI_T15_PLAN_RETEST "Limit"). Read with ReVa and REA (Ghidra, `/StarWarsG.exe`, FoC Steam x64 1.121, image base
0x140000000). Addresses are RVAs. [H] = read in the binary; [M] = one inferred link; [L] = not checked. No decompiled
code is reproduced. Ghidra plate comments were added at 0x6c8970, 0x721d90, 0x691710 and 0x58a5d0.

Functions read: 0x6c8970 (Resolve_Category), 0x6c8710 (Commit) and its caller 0x6cacd0, 0x6c7970 (goal system base
constructor), 0x6b86e0 (land goal system factory), 0x721360 / 0x721f70 / 0x721d90 / 0x721480 / 0x7218c0 / 0x7227a0 /
0x721b60 (goal set), 0x691710 (goal constructor), 0x6cb120 (proposal pass), 0x6927f0 / 0x692740 / 0x692d10 / 0x692d20 /
0x5791e0 / 0x5f7650 / 0x693550 / 0x693750 / 0x693730 / 0x693950 (goal accessors), 0x7222c0 (Commit step 1, callees
only), 0x6b7c70 / 0x5e6890 (goal type constructor and loader), 0x66ec20 / 0x66f570 (PlanDefinition constructor and
name), 0x724c40 / 0x7251f0 / 0x4dc3a0 (unit target), 0x725310 / 0x725340 / 0x7253d0 / 0x6b8d80 (region target),
target vtables 0x8ce4a0 / 0x8ce548, 0x613af0 (budget credits), 0x2824f0 (player relation), 0x58a5d0 (activation record).

## Summary

- **Arguments.** Resolve_Category takes three: rcx = goal system, rdx = the proposed set (always goal system +0xd8),
  r8d = category id. The kit's `skai_t15_hooks.js` logs `args[1].toInt32()` as the category for `resolve_enter`: that is
  the low half of the proposed-set pointer. The category is `args[2]`. For maintenance 0x722a30 `args[1]` is the
  category, but `args[0]` is a goal set, not the goal system (§1.1).
- **Both lists are members of the goal system, so the entry hook can read them.** Candidates = proposed set [cat]
  (already sorted and cut by Commit). Active goals = goal system +0x38 [cat]. Each is a vector of goal pointers reached
  through the set's category map (§1.2, §1.3). Resolve's own lists (filtered proposals, old candidates, pool, kept set)
  are on its stack, but no hook on them is needed: at leave, goal system +0x38 [cat] holds exactly the survivors, and the
  proposed set is unchanged and still alive (§1.5). An optional hook on 0x58a5d0 tells, per goal, which test failed or
  which phase accepted it (§1.6).
- **Goal:** +0x48 → {type, function}, +0x50 desire, +0x54 proposal-time desire, +0x58 target, +0x60 running plan,
  +0x68 PotentialPlan, +0x74 state. Effective desire is computed from +0x50 and +0x74 in float (§2).
- **Type name:** std::string at goal type +0x00 (§3). **Plan name** of a candidate's PotentialPlan: PP +0x50 → +0x40.
- **Target:** unit target +0x30 = the object id (cached), +0x28 = the object; region target +0x28 = the AI hint zone,
  whose name is a std::string at zone +0x10; centre at target +0x1c (§4).
- **Player:** goal system +0x28 (context) → +0x28 (AIPlayer) → +0x28 (PlayerClass) → +0x4c (player id). Mode =
  context +0x34 (1 = land); a land goal system has vtable 0x8be308 (§5).

## 1. Resolve_Category's arguments and lists

### 1.1 Arguments [H]

- Called only by Commit 0x6c8710, at 0x6c88ca (the other references to 0x6c8970 are data, not calls). Commit's caller
  0x6cacd0 passes rdx = goal system +0xd8 (`lea rdx,[rdi+0xd8]` at 0x6cad1c). Commit loops over the categories in
  budget-weight order and calls Resolve with rcx = goal system, rdx = that same set, r8d = the category id.
- Maintenance 0x722a30 takes (goal set, category, budget = goal system +0x190). Resolve calls it three times: on goal
  system +0x38 when the proposed category is empty (return 0x6c89c6), on goal system +0x38 when no proposal survives the
  `Applies` filter (0x6c8ada), and on its local kept set after the classify step (0x6c8cf3). Every goal set's +0x80 holds
  its owning goal system: the base constructor 0x6c7970 sets goal system +0xb8 and +0x158 (the +0x80 of its two member
  sets), and Resolve stores the goal system into +0x80 of each local set. So `args[0].add(0x80).readPointer()` gives the
  goal system for any maintenance call.

### 1.2 Goal set layout (0xA0 bytes, constructor 0x721360) [H]

Two MSVC `std::unordered_map`s and the owner pointer.

| Offset | Field |
|---|---|
| +0x00..+0x3f | target index: map keyed by the target pointer (goal +0x58), whose value is an inner map keyed by the goal type pointer; used by insert (0x721480), the like-type lookup 0x721a50 and erase 0x7225d0. Not needed by the hook |
| +0x44 | category map: max load factor (float 1.0) |
| **+0x48** | category map: list head (sentinel node pointer) |
| +0x50 | category map: list size (number of categories present) |
| +0x58 / +0x60 / +0x68 | category map: bucket vector (pairs of node pointers) |
| +0x70 | category map: bucket mask |
| **+0x80** | owning goal system |

Category map list node (0x20 bytes): +0x00 next, +0x08 prev, **+0x10 key (int category id)**, **+0x18 pointer to a
vector** of goal pointers. Vector: **+0x00 begin, +0x08 end**, +0x10 capacity end. Element = `InstantiatedGoalClass*`
(8 bytes, reference-counted through vtable slot 0 AddRef and slot 1 Release).

- Count (0x721f70) = (end − begin) / 8 of the category's vector, 0 when the category has no node.
- Get (0x721d90) returns element *index* and takes a reference.
- A category node can exist with an empty vector: 0x7218c0 (remove a category) erases each goal from the target index
  and then empties the vector, but leaves the node.
- Insert 0x721480 keys the category by goal type +0x28. A second goal with the same (target, type) is not added: the
  existing entry takes the higher desire and the newcomer's {type, function} pair (AI_GALACTIC §1.5 "Insert").

### 1.3 Walking one category from the hook [H]

For a set S and a category c:
1. head = pointer at S +0x48.
2. node = pointer at head +0x00; repeat until node equals head: if the int at node +0x10 equals c, the vector is the
   pointer at node +0x18; else node = pointer at node +0x00.
3. begin = pointer at vector +0x00, end = pointer at vector +0x08; the goals are the pointers at begin + 8·i,
   i < (end − begin)/8.

A list walk avoids the bucket hash (`(c ^ 0xdeadbeef) & 0x7fffffff`, then the minstd step, masked by +0x70); the map
holds one node per category, so the list is short.

### 1.4 What each list holds at entry [H]

- **Active A = goal system +0x38 [cat]**: every goal of this category that is Active (state 1) or Executing (2), sorted
  by effective desire, descending (Commit step 2, AI_GALACTIC §1.6). Resolve reads its count first;
  limit = `GoalSetExtensionSize` (int at AIPlayerType +0xe8, default 2) + |A|.
- **Proposals P = rdx [cat]**: this commit's new proposals of this category (state 0, fresh goal objects; a candidate
  already like-type active on the same target was skipped at proposal by slot 9). Commit step 5 has already sorted them
  with the same comparator (effective desire `a > b`, so desire for state 0) and cut them to that limit (0x721b60), so P
  holds at most `2 + |A|` entries, best first. Proposals cut there never reach Resolve and cannot displace anything.
- Resolve then keeps the P entries whose target still `Applies` (global goals: type flag 0x10) in a local set, sorts it
  again, and classifies A against `best` = the effective desire of its first entry (AI_GALACTIC §1.6 steps 2-3). The hook
  cannot see that filter directly; a P entry that `Applies` removed is simply not accepted at leave.

### 1.5 What has changed by leave [H]

- **Goal system +0x38 [cat] = the kept set** (Resolve's local set, inserted at Finalize in kept order: incumbents kept by
  the classify step and not dropped by the maintenance on the kept set, then greedy accepts, then random accepts).
  Goal system +0x38 [cat] was emptied right after the classify step (0x7218c0 at 0x6c8cd8). In the two early paths
  (§1.1) maintenance works on goal system +0x38 directly, so its contents at leave are again the survivors.
- **The proposed set is not modified by Resolve.** Resolve reads `rdx` only in its first loop (count and get); the set is
  cleared by Commit (0x721820) after the last category. Every P goal is therefore still alive at leave, held by the set.
- **State (+0x74) at leave:**

| Goal | +0x74 at leave | In goal system +0x38 [cat] at leave |
|---|---|---|
| proposal accepted (greedy or random) | 1 (Activate 0x693550 runs only when state < 1) | yes |
| proposal not accepted (filtered out, greedy stop, random-phase failure, never drawn) | 0 (release runs only when state > 0) | no |
| incumbent kept | 1 or 2, unchanged | yes |
| incumbent dropped at classify (state 3, no running plan, or failed re-test) or by maintenance | unchanged; only the budget release 0x614880 runs (no-op in tactical) | no |
| incumbent sent to the old candidates and not picked again, or drawn by the random phase and failing its re-check | 3 (0x693750 at 0x6c99a2, or 0x6c95bc / 0x6c965f / 0x6c9716) | no |
| old candidate accepted again (greedy or random) | unchanged (no new activation record) | yes |

- **Do not read a dropped incumbent at leave.** Its last reference may be the one Resolve's local sets release just before
  returning (the goal destructor 0x691840 then ends its plan, SKAI_T15_PLAN_RETEST §2.2 [M]); keep the values read at entry and compare pointers only.
  Kept goals and all P goals are alive at leave. No goal is allocated inside Resolve, so a pointer cannot be reused for a
  different goal between entry and leave.

So survivors = A ∩ kept (incumbents) plus P ∩ kept (accepted proposals); displaced incumbents = A − kept. When an
incumbent is displaced, the P entries of that call, with their effective desires against the incumbent's, name the
displacing proposal(s) without a hook inside the function.

### 1.6 Optional per-decision hooks [H]

All are called only on Resolve's paths listed (return address = call + 5). They tell why a candidate was not accepted.

| Function | Arguments | Call sites (return RVA) | Meaning |
|---|---|---|---|
| 0x58a5d0 activation record (only caller: Resolve) | rcx learning (context +0x58), rdx goal, r8b ok | ok 1: greedy accept 0x6c90cb, random accept 0x6c97cd. ok 0: greedy plan, fund or time failure 0x6c914f (the greedy phase ends there); random plan failure 0x6c95d3, fund 0x6c9676, time 0x6c972d | one call per activation attempt that reached the plan test; the record itself is skipped inside when the type's `Activation_Tracking_Duration` is 0, but the call is made |
| 0x6c4a50 plan re-solve | rcx PotentialPlan (goal +0x68) | greedy 0x6c9024, random 0x6c9589 | returns al: false = plan cannot be activated |
| 0x692860 AllowedTime | rcx goal, edx 0 | greedy 0x6c9052, random 0x6c96c8 | xmm0: failure when 0 < AllowedTime < goal +0x7c (ETA) |
| 0x692d30 Is_Like | rcx accepted goal, rdx lastOld | greedy 0x6c9104 | true ends the greedy phase after this (kept) accept |
| 0x6c47c0 running-plan re-test (already hooked) | rcx PotentialPlan | kept path 0x6c8be4, old-candidate path 0x6c8c42 | false drops the incumbent |
| 0x532480 synced draw | | random phase 0x6c94b5 | one draw per random-phase pick |

The greedy phase runs only while the category has credits: 0x613af0(budget = goal system +0x190, cat) is false when the
budget's category map (list head budget +0x30, node +0x10 key int category, +0x18 → category record) has the category
and its record's available credits (+0x44, float) are > 0. In tactical those credits are written once, at template
init, from the template's budget weights (SKAI_T22_SPACE_AI_RE §4.4: in space Tactical_Targeted has them, so its greedy
phase runs). The land template's weights were not read here [L], so the hook should log that float once per category.

## 2. The goal (`InstantiatedGoalClass`, 0x98 bytes, vtable 0x8b6cd8, constructor 0x691710) [H]

| Offset | Type | Field |
|---|---|---|
| +0x00 | ptr | vtable 0x8b6cd8 (also +0x10 0x8b6d00 and +0x20 0x8b6d20: signal-listener sub-objects) |
| +0x08 | int | reference count |
| **+0x48** | ptr | the proposal-source pair: **pair +0x00 = goal type** (`AIGoalType*`), pair +0x08 = the desire function (`PerceptionFunctionClass*`) |
| **+0x50** | float | **desire**. Proposal: function value + learning terms (c1·`Per_Failure_Desire_Adjust` + c2·`Per_Activation_Failure_Desire_Adjust`), clamped to [0, 1e18] (0x693730). Active goal: overwritten at every Commit step 1 (0x7222c0 → 0x693730) by the bare function value, no learning terms; kept when the evaluation fails (SKAI_T15_TARGET_CHOICE Q3) |
| **+0x54** | float | **proposal-time desire** (same value as +0x50 at proposal, same clamp). Written only by 0x693950, whose only caller is the proposal pass 0x6cb120; Commit step 1 does not touch it. For an incumbent it is the desire it was activated with, learning terms included |
| **+0x58** | ptr | **target** (`AITargetLocation*`); null for a global goal (type +0x20 == 0x10) |
| +0x60 | ptr | running plan (null until Planning instantiates it after Finalize). Plan +0x60 = PlanDefinition, +0x80 = ended byte, +0x88 = goal, +0x98 = removable byte [M: name] |
| **+0x68** | ptr | **PotentialPlan**. PP +0x44 cost, +0x48 ETA, +0x4c valid byte, **+0x50 PlanDefinition** |
| +0x70 | u32 | creation frame (global logic frame, FrameSynchronizer +0 = 0xb0a320) |
| **+0x74** | int | **state**: 0 proposed, 1 Active, 2 Executing, 3 Released |
| +0x78 | float | cost |
| +0x7c | float | ETA (seconds), written by the plan re-solve |
| +0x90 | ptr | goal system |

**Effective desire** (0x6927f0), all in single-precision float, constants 0.01f, 40.0f, 1.0f, 1.6f:
- d = +0x50; when state (+0x74) ≥ 1 and 0.01 < d < 40: eff = d + (1 − (d − 0.01) / (40 − 0.01)) · 1.6, every operation
  rounded to float (the divisor is computed as 40.0f − 0.01f, not a stored 39.99);
- otherwise eff = d. So a proposal's eff is its desire; a Released goal (state 3) still gets the bonus.
- In JavaScript: wrap each step in `Math.fround` to reproduce the value bit for bit.

**Replaceable** (0x692740): true when +0x60 is null, or plan +0x98 ≠ 0, or plan +0x80 ≠ 0. False only for a running,
not-ended plan whose +0x98 is 0 [M: +0x98 is the byte `Set_As_Goal_System_Removable` writes]. An incumbent with
eff < best that is not replaceable is kept.

**Plan name.** PlanDefinition +0x40 is a std::string (size +0x50, capacity +0x58), set when the definition is created
(0x66f570, from its lookup key; +0xd8 holds that name's CRC). That the key is the script's file base name is AI_GALACTIC
§1.9.1 [M].

## 3. The goal type name [H]

- Goal type = pointer at (pointer at goal +0x48) +0x00.
- **Name = std::string at goal type +0x00**: size (u64) at +0x10, capacity (u64) at +0x18. Capacity ≤ 15: the characters
  are inline at +0x00; capacity > 15: +0x00 holds a pointer to them. The constructor 0x6b7c70 sets capacity 15, size 0 and
  assigns the name before parsing the fields (+0x20 onwards).
- The name is the string the loader 0x5e6890 reads for each element of `Data/XML/AI/Goals/*.xml`, i.e. the element tag
  [M: as written in the XML, not upper-cased; compare case-insensitively]. `Destroy_Structure` (17 characters) is on the
  heap; `Clear_Infantry` (14) is inline.
- Other type fields a hook may log: +0x20 application flags (0x10 = global), +0x28 category id, +0x2c reachability,
  +0x30 `Time_Limit`, +0x34 `Build_Time_Delay_Tolerance` (AI_GALACTIC §1.4).

## 4. The target [H unless tagged]

Tell the two land kinds apart by the vtable at target +0x00 (module base + RVA).

**Unit target** (`TacticalUnitAITargetLocationClass`, vtable 0x8ce4a0, 0x38 bytes, constructor 0x724c40, bind 0x7251f0):

| Offset | Field |
|---|---|
| +0x00 | vtable 0x8ce4a0 (+0x18: signal-listener vtable 0x8ce520) |
| +0x10 | owner (the perception system that made it) |
| **+0x28** | **game object** (holds a reference; vf+0x30 = 0x6021a0 returns it) |
| **+0x30** | **object id**: copy of the object's +0x50 taken at bind; 0x3fffff when none. Readable without touching the object |
| +0x34 | PerceptionTokenType id of the name `OBJECT_<id>` (0x4dc3a0), −1 when none |

On the object: +0x50 id, +0x58 owner player id, +0x298 type → type +0xf8 upper-cased type name (std::string, same
layout: size +0x108, capacity +0x110) (q2_layout.hpp, OBJECT_TYPES §5.1). The target's debug name (vf+0x50 = 0x665690)
is that type name [M: the copy source], or "Unknown Target".

**Region target** (`TacticalRegionAITargetLocationClass`, vtable 0x8ce548, 0x38 bytes):

| Offset | Field |
|---|---|
| +0x00 | vtable 0x8ce548 |
| +0x18 | PerceptionTokenType id of the region name (−1 when none) |
| **+0x1c / +0x20 / +0x24** | **centre x, y, z** (float); in land the hint zone's +0x30..+0x38 |
| **+0x28** | **`AIHintZoneClass*`** (land: set by 0x7253d0, called from land perception 0x6b8d80; null for space grid cells, 0x725340) |
| +0x30 | `ZoneReachabilityClass*` (0x308 bytes, land only) |

- **Zone name** = std::string at hint zone +0x10 (size +0x20, capacity +0x28; inline when capacity ≤ 15). 0x6b8d80 reads
  it there to make the token. That is the readable id for a land region target.
- The zone index is not stored in the target. Region targets are appended to the perception target list (perception
  +0xe0) after the unit targets, in zone-list order (AI_TACTICAL §3.2), so a position in that list would also work, at
  the cost of a list walk.
- After a save load the region targets are rebuilt by 0x725310 and re-linked to zones by name (AI_TACTICAL §3.2); whether
  +0x28 is set again then was not read [L]. A fresh skirmish run does not load.

**Null target:** a global goal; the type's +0x20 is 0x10.

## 5. Which player's goal system [H]

| Step | Offset | Meaning |
|---|---|---|
| goal system +0x00 | vtable | land 0x8be308 (`LandGoalSystemClass`, set by 0x6b86e0), galactic 0x8be298, space 0x8be378 |
| goal system +0x28 | ptr | per-mode context (`TacticalAIManagerClass`, ServicedAISystemClass +0x28) |
| context +0x34 | int | mode: 0 galactic, 1 land, 2 space |
| context +0x30 | int | opponent player index |
| context +0x28 | ptr | `AIPlayerClass` |
| AIPlayerClass +0x28 | ptr | `PlayerClass` |
| AIPlayerClass +0x30 | ptr | `AIPlayerType` (+0xe8 = `GoalSetExtensionSize`) |
| **PlayerClass +0x4c** | int | **player id** (the index into the relation table, 0x2824f0; the owner id objects store at +0x58) |
| PlayerClass +0x62 | u8 | is human |

The same chain is what Resolve itself uses for the limit and for `Applies` (it passes context +0x28). The kit's
q2_layout.hpp records PlayerClass +0x48 as the list index, observed equal to the id [M: +0x4c is the field the AI code
reads]. Filter: vtable 0x8be308 (or context +0x34 == 1) and PlayerClass +0x4c == the AI player's id.

## 6. Offsets the hook needs

| What | Where | Tag |
|---|---|---|
| Resolve_Category | RVA 0x6c8970; args[0] goal system, args[1] proposed set (= goal system +0xd8), args[2] category (int) | H |
| Maintenance | RVA 0x722a30; args[0] goal set (its +0x80 = goal system), args[1] category, args[2] budget | H |
| Active set | goal system +0x38 | H |
| Proposed set | goal system +0xd8 | H |
| Set → category list head | set +0x48 (node +0x00 next, +0x10 int category, +0x18 → vector) | H |
| Vector | +0x00 begin, +0x08 end; element = goal pointer | H |
| Set → goal system | set +0x80 | H |
| Goal → {type, function} | goal +0x48 → pair (+0x00 type, +0x08 function) | H |
| Goal desire / proposal-time desire | goal +0x50 / +0x54 (float) | H |
| Goal target | goal +0x58 (null = global) | H |
| Goal running plan / PotentialPlan | goal +0x60 / +0x68 | H |
| Goal state | goal +0x74 (0 proposed, 1 active, 2 executing, 3 released) | H |
| Goal cost / ETA | goal +0x78 / +0x7c (float) | H |
| Effective desire | state ≥ 1 and 0.01 < d < 40: d + (1 − (d − 0.01)/(40 − 0.01))·1.6, float steps; else d | H |
| Type name | type +0x00 std::string (size +0x10, capacity +0x18, heap pointer when capacity > 15) | H (M: tag case) |
| Type category / flags | type +0x28 / +0x20 | H |
| Plan name | PP +0x50 → PlanDefinition +0x40 std::string (size +0x50, capacity +0x58) | H |
| Unit target | vtable 0x8ce4a0; +0x28 object, +0x30 object id | H |
| Region target | vtable 0x8ce548; +0x28 hint zone → +0x10 name std::string (size +0x20, capacity +0x28); +0x1c/+0x20 centre | H |
| Player | goal system +0x28 → +0x28 → +0x28 → +0x4c (id); mode = goal system +0x28 → +0x34; land goal system vtable 0x8be308 | H |
| Activation record | RVA 0x58a5d0 (learning, goal, ok); return RVAs in §1.6 | H |

## 7. Side findings

- **Kit bug:** `frida/skai_t15_hooks.js` emits `cat` for `resolve_enter` from `args[1]`, the proposed-set pointer; the
  category is `args[2]`. The `maint_enter` value is right. Earlier `resolve_enter` records' `cat` fields are therefore not
  category ids (the brackets and return addresses are unaffected).
- **AI_GALACTIC §1.6 step 5 wording:** the chain to `GoalSetExtensionSize` that 0x721b60 follows starts at the proposed
  set's +0x80 (its goal system), then +0x28 → +0x28 → +0x30 → +0xe8. Resolve follows goal system +0x28 → +0x28 → +0x30
  → +0xe8. Fixed there.
