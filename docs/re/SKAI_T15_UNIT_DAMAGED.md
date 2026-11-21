# skai t15: how the engine raises the plan event Unit_Damaged (RE, 2026-11-16) [H unless tagged]

Answers `SKAI_T15_UNIT_DAMAGED_QUESTIONS.md` Q1-Q6. Read with REA (rea-agents on the FoC Steam x64 image, base
0x140000000: decompile, disassembly, byte search, cross-references) and the shipped FoC Lua / XML; no ReVa. Addresses
are RVAs. [H] = read in the binary or the shipped Lua; [M] = inferred.

Functions: 0x3a9e30 (`GameObject::Take_Damage`), 0x4f59d0 (UNIT_AI damage notice, the only raiser of signal 0x18),
0x4f5e30 (UNIT_AI return fire), 0x6e8370 (SpaceTaskForceClass signal handler), 0x6ed230 (LandTaskForceClass signal
handler), 0x6e4e30 / 0x6e9240 (space / land `Add_Force` override, vf +0xa0), 0x6e8aa0 / 0x6ee5e0 (space / land unit
removal override, vf +0x188), 0x5f9a90 (base `Add_Force`), 0x5fd250 (is a TF event pending), 0x25a250 (queue search),
0x6006b0 (plan event post), 0x25ab50 (queue append), 0x243640 (synchronous Lua call), 0x688820 (TF handler table
build), 0x6887c0 (handler table read), 0x397a40 (an object's current attack target), 0x39b1a0 (is a TEAM member),
0x2aca60 (object by id), 0x39bdb0 (`Die`). Signal raise 0x220ed0 → 0x240940; subscribe 0x220e90 → 0x2406c0;
unsubscribe 0x220eb0 → 0x2408c0.

## Summary

- **Raise site.** `Take_Damage` (0x3a9e30), at its end, hands the damage to the UNIT_AI behaviour (slot 0x12) of the
  victim, or of the victim's container when the victim has none. That behaviour (0x4f59d0) raises object **signal 0x18**
  on its owner, with a `UnitDamagedDataClass` payload {attacker, deliberate}. The land and space task-force signal
  handlers turn signal 0x18 into plan event **14 `Unit_Damaged`**, **queued** (0x6006b0 with queued = 1).
- **Who gets it.** Only land and space task forces subscribe to signal 0x18, once per member, in `Add_Force`. They
  unsubscribe when the unit leaves. Free-store units, galactic task forces and human units never get the event. The
  free store has no damage callback of any kind.
- **Filter.** The engine has no cooldown, no minimum damage, no owner / hostility test and no structure test. A hit
  that the shield absorbs completely still raises it. These are the only gates:
  - there must be a resolvable attacker, alive and not removed;
  - the victim's health and the pre-shield amount must both be > 0;
  - a killing blow on the UNIT_AI holder raises nothing;
  - **the event is dropped if a `Unit_Damaged` is already waiting in that task force's queue.** So each task force has
    at most one pending event: the first hit after the last `PumpEvents` wins, and later hits are discarded, not
    deferred.
- **Arguments** `(tf, unit, attacker, deliberate)`:
  - `unit` = the signal owner. That is the UNIT_AI holder and the TF member: an infantry squad's or fighter squadron's
    TEAM container, not the member that was hit.
  - `attacker` = the projectile's shooter, looked up by id when the damage is applied, or the damaging object itself.
    A TEAM member is replaced by its TEAM container.
  - `deliberate` = the attacker's **current** attack target is `unit`.
- **Handler.** `<TF>_Unit_Damaged`, else `Default_Unit_Damaged`. The engine resolves it once when the plan starts
  (0x688820), as it does for every plan event. The shipped scripts define no `<TF>_Unit_Damaged`, so the handler is always
  `Default_Unit_Damaged`.

## 1. Q1: raise site and signal

### 1.1 `Take_Damage` → UNIT_AI (0x3a9e30, tail, disassembly 0x3ab1e9-0x3ab23b)

After the hull step (TACTICAL_COMBAT §3.6 step 18) and before the damage-state model check, `Take_Damage` runs:

1. The **attacker** A is the object resolved earlier in the function (§3.1). If A is null, or A has obj +0x3a0 bit
   0x40 set (dead / death-clone; `Die` 0x39bdb0 sets it first thing), nothing is raised.
2. **Holder** H:
   - the victim, when it has a UNIT_AI behaviour (slot byte obj +0x344 ≠ −1);
   - otherwise the victim's container (obj +0x2b0), when that container has UNIT_AI;
   - otherwise nothing is raised.
3. Call 0x4f59d0(H's UNIT_AI, amount in xmm1 (unused), A).

0x4f59d0 is the only caller-side producer: its only call site is 0x3ab23b. It is also the only code that builds a
`UnitDamagedDataClass` (vtable referenced only there; type id 0x140a2bf00, returned by 0x4f5930).

### 1.2 The UNIT_AI notice (0x4f59d0)

1. Return if A is null or H has obj +0x3a0 bit 0x40 set. **A killing blow on H raises nothing.** A killing blow on a
   squad member whose container is H still raises, on the container.
2. If A is a TEAM member (0x39b1a0: A's container exists and has the TEAM slot, +0x348 ≠ −1), A := A's container.
3. Build the payload:
   - attacker (+0x08) = A;
   - deliberate (+0x10) = (0x397a40(A) == H), see §3.
4. Raise **signal 0x18** on H (0x220ed0 → 0x240940). Dispatch calls, synchronously, every listener subscribed to
   (H, 0x18).
5. Then 0x4f5e30(H's UNIT_AI, A, 1), the unit's own return fire [M: summary only]. When A's owner is not allied with H's
   owner, A is not a special weapon, H can attack (flag 1, slot 9 weapon, targeting data not locked) and A is a better
   or the only target, H is ordered to attack A (0x38d730). This runs for every UNIT_AI holder, in a task force or not.

### 1.3 Who listens: the land and space task forces

- **Subscription.** The tactical `Add_Force` overrides subscribe the task force's listener (TF +0x28) to the new
  member's signals 0x18 and 0x2a, then call the base `Add_Force` 0x5f9a90 (signals 0x1a, 0x10, 0x1e, 0x1f, 0x20, 0x21,
  0x2b, 0x2c, 0x29, 0x2d; TACTICAL_COMBAT §3.1.1, "Signal 0x1E listeners"):
  - space: 0x6e4e30 (SpaceTaskForceClass vt 0x8c1b88, slot +0xa0);
  - land: 0x6e9240 (LandTaskForceClass vt 0x8c36a8, slot +0xa0).
  - The member is the object passed to `Add_Force`. On land, the land override also subscribes each TEAM member to
    0x2a (diversion), not to 0x18.
- **Unsubscription.** The unit-removal overrides (vf +0x188: space 0x6e8aa0, land 0x6ee5e0) call the base removal
  0x5ff610 and then unsubscribe 0x18 and 0x2a. A unit released by `Release_Unit` / `Release_Forces`, or removed on its
  death / owner change, stops raising the event for that task force.
- **No other listener.** A byte search over the image for a constant 0x18 passed as the subscribe code (`mov r9d,
  0x18`) finds only 0x6e4e30 and 0x6e9240 among the 0x220e90 callers. A search for a handler test of the code against
  0x18 (`cmp r8d, 0x18`) finds only 0x6e8370 and 0x6ed230. TACTICAL_COMBAT's survey of register-valued codes lists
  none that can be 0x18. The galactic TF handler (0x6cf810) and the base TF handler 0x5fe5d0 have no 0x18 case.
- **Every add path goes through the virtual.** The base 0x5f9a90 is called directly only from the two overrides and
  from 0x6cd1a0 (the galactic TF range [M]). `Collect_All_Free_Units` (0x6e5aa0) and the other AI add sites call vf +0xa0 [H for the
  sites found by the `call [rax+0xa0]` search; M that no other path fills the member list TF +0x78].

### 1.4 The TF handler (space 0x6e8370, land 0x6ed230; same code for signal 0x18)

On signal 0x18 whose payload's type chain contains 0x140a2bf00:

1. Return if the payload attacker is null.
2. Return if the attacker has obj +0x3a0 bit 0x02 set (removed / destroyed, `Detach`).
3. Return if 0x5fd250(TF, 14) is true (§2.2).
4. Build the parameter list:
   - a GameObjectWrapper for the signal owner H (sender sub-object − 0x38);
   - a wrapper for the payload attacker;
   - a Lua bool with the payload's deliberate byte.
5. 0x6006b0(TF, 14, list, **queued = 1**).

The same handlers turn signal 0x2a into event 20 `Unit_Diversion_Finished`. Space queues it (1). Land calls it
synchronously (0). On land, for a TEAM member, it fires once for the container, and only when no other member of the
team still has a diversion order [M: the per-member test is formation state bit 6, 0x41af10 +0x48]. That is the only
difference between the two handlers.

### 1.5 Which damage reaches the notice (gates inside `Take_Damage`)

The notice runs on every `Take_Damage` call that passes the early gates and reaches the tail. There is no return
between the amount gate and the tail.

- **Early returns, no event** (TACTICAL_COMBAT §3.6 steps 1-3):
  - build-pad redirect: the event comes from the redirected call instead;
  - team Redirect / Share: the per-member recursive calls raise their own notices;
  - the invulnerable gate;
  - `Immune_To_Damage` (type +0xa3);
  - garrisoned (obj +0x3a7);
  - the global damage-enabled gate;
  - obj +0x3a0 bit 0x20;
  - a test through obj +0x2b8 → +0x20 → +0x28 → byte +0x20 [M: meaning].
- **Amount gate:** the victim's health (obj +0x5c) must be > 0 and the amount after steps 4-9 must be > 0. Steps 4-9 are
  TAKE_DAMAGE, CAUSE_DAMAGE, base shield, diminishing firepower, under construction and retreat.
- **After the gate, nothing stops the notice.** A hit still raises it when:
  - `Get_Damage_Multiplier` scales it;
  - a damage listener cancels it (event 10, amount set to 0);
  - the shield or energy absorbs all of it;
  - `Projectile_Does_Hitpoint_Damage` is false;
  - it lands on a hardpoint;
  - it ignites a combustible victim instead of damaging it.
- **One hit can notify several times.** Team Share / Redirect and the recursive build-pad call each pass through the
  tail. A squad hit with Share damage notifies the container once per member, and §2.2 keeps only the first.

## 2. Q2: filter and rate

### 2.1 What is and is not filtered

| Case | Raised? | Where |
|---|---|---|
| Any damage ≥ the gates of §1.5, any amount > 0 | yes | 0x3a9e30 |
| Shield-only hit (fully absorbed), cancelled hit, zero hull damage | yes | 0x3a9e30 tail is after absorption |
| Friendly, neutral or own-player attacker | yes, no owner test | 0x4f59d0, 0x6e8370 / 0x6ed230 |
| Attacker is a structure | yes. `Default_Unit_Damaged` returns at once for `attacker.Is_Category("Structure")` | PGEVENTS.LUA line 238 |
| No source, or a projectile whose shooter id is unset (0x3fffff) or no longer in the object table | no (attacker null) | 0x3a9e30 attacker resolution |
| Shooter dead (+0x3a0 bit 0x40) or removed (+0x3a0 bit 0x02) | no | 0x3ab1f2; 0x6e8370 / 0x6ed230 step 2 |
| Killing blow on the UNIT_AI holder itself | no | 0x4f59d0 step 1 |
| Victim and container both without UNIT_AI | no | 0x3ab200-0x3ab224 |
| Holder not a member of a land / space AI task force | no listener | §1.3 |
| A `Unit_Damaged` already pending for that task force | **dropped** | 0x5fd250 |

There is no per-unit cooldown, no frame counter, no minimum damage and no "first hit per frame" test anywhere on the
path.

### 2.2 The per-task-force pending test (0x5fd250 → 0x25a250)

- 0x5fd250(TF, code):
  - looks up the TF's handler for the code (0x6887c0); with no handler it returns false;
  - otherwise it tail-jumps to 0x25a250(script queues +0xd8, handler, TF index +0xf8).
- 0x25a250 walks that task force's pending-handler list (queue object +0x28, one list per TF index). It returns true if
  any pending entry holds the same Lua function (0x2440b0, Lua reference equality).
- 0x5fd250 is called only by the two Unit_Damaged raise sites. No other event is coalesced.

Effect: **a task force holds at most one pending `Unit_Damaged`.** It carries the first qualifying hit on any member
since that task force's queue was last drained. Every later hit is discarded until the drain. The drain is `PumpEvents`
on the TF's thread, every Planning service (3 frames, §4) while the thread runs. So a task force sees about one
`Unit_Damaged` per 3 frames while it is under fire, always for the first member hit in the window.
[H mechanism, M cadence: it assumes the thread yields only through `PumpEvents`, as all shipped plan Lua does.]

- **Several task forces in one plan** have separate queues (TF index +0xf8), so each gets its own event.
- **A finished thread blocks its task force.** If a task force's thread has ended while the plan lives on, its one
  pending event is never drained, and that task force gets no more `Unit_Damaged` [M].

## 3. Q3: the arguments

| Lua arg | Value | Notes |
|---|---|---|
| `tf` | the task force | 0x6006b0 puts the TF in front of the parameter list |
| `unit` | wrapper of the signal owner H | The UNIT_AI holder = the TF member. For infantry and fighters this is the TEAM container (CONTAINERS.XML `Team`, `*_Squadron_Container`, `*_Team` carry UNIT_AI; GROUNDINFANTRY.XML / SPACEUNITSFIGHTERS.XML list none). For vehicles, ships, heroes, turrets and structures with UNIT_AI in their behaviour list, it is the object itself |
| `attacker` | wrapper of A after the TEAM mapping | See below |
| `deliberate` | Lua bool | 0x397a40(A) == H |

**Attacker resolution in `Take_Damage`** (source = argument 4, pack = source +0xe8, the projectile data pack):
- **No pack** (a non-projectile source: crush, ability, direct `Take_Damage`): A = the source object itself.
- **A pack** whose shooter id (+0x58, TACTICAL_COMBAT §3.1 step 11) is set: A = the object with that id in the object
  table (0x2aca60, a hash lookup that returns null when absent). The lookup happens **when the damage is applied**,
  not when the shot was fired. A shooter that is gone by then gives no event.
- **No source:** A = null, no event.

After that, 0x4f59d0 replaces a TEAM member by its TEAM container (0x39b1a0). A fighter's or a trooper's shot names its
squadron / squad.

**`deliberate`** = the attacker's current attack target, as 0x397a40 returns it, is H:
- 0x397a40(A) returns the formation's attack target (0x41a2e0 on the formation at A's, or A's container's, data +0xa8
  → +0x1f0) when there is one;
- otherwise A's targeting-data target (obj +0xf0 → +0x08).
- It is the attacker's target **at impact**. In these cases it is false:
  - a splash or blast victim that was not the target;
  - a shooter that switched targets while its projectile flew;
  - a unit hit while its attacker is attack-moving at someone else.
- An ordered attack and an automatic target count the same; there is no "ordered" test.
- Because attack targets on a team member are stored as the container (TACTICAL_COMBAT §2.8 B), a squad hit by a unit
  that targets the squad gives true.

## 4. Q4: timing

- **Signal and handler: synchronous.** Signal 0x18 and the TF handler run synchronously inside `Take_Damage`, in the
  sim frame where the damage is applied.
- **The Lua call: queued** (0x6006b0, queued = 1, disassembly 0x14060087f-0x14060089a). The parameter list (r9) and the
  handler go to the back of the task force's queue (0x25ab50(queues, handler, TF index, params): two parallel lists,
  handlers at +0x28 and parameter lists at +0x40, per TF index).
- **When the call runs.** `PumpEvents` (PGBASE.LUA) yields, and on resume pops `GetEvent()` / `GetEvent.Params()` in
  FIFO order until the queue is empty, calling each handler as `handler(unpack(params))`. The Planning service resumes
  every plan's threads every 3 frames (AI_TACTICAL §2), so the Lua runs 1-3 frames after the hit. It runs at the start
  of the TF thread's next resume, before that thread's own Lua continues, and in FIFO order with the TF's other queued
  events.
- **No pile-up.** Several `Unit_Damaged` events for one unit, or for different units of one task force, cannot pile up
  between two pumps. The second and later ones are dropped at raise time (§2.2), not merged.
- **Contrast:** `Target_In_Range` (12, from signal 0x1e) is posted with queued = 0 and runs synchronously inside the
  frame (TACTICAL_COMBAT §3.1.1, "Signal 0x1E listeners").
- **Dispatch is skipped** when the plan's script is exiting (script +0x121) or has no script.
- **A unit that left the task force.** A `Unit_Damaged` queued before the unit left is not purged by the removal
  overrides; they only unsubscribe. So it can still run with a unit that is no longer a member [M: 0x5ff610 was not read
  for a queue purge]. `Default_Unit_Damaged` begins with `TestValid(unit)`, so a dead unit is skipped.

## 5. Q5: handler lookup

- **Built once per task force, at plan instantiation** (AI_GALACTIC §1.9.4 Instantiate step 7). 0x688820 walks the
  event codes 23 → 0 through the name table DAT_140b3a208 (AI_GALACTIC_2 §5.2). For each code:
  1. name = `<TF name>_<event name>`. If that global is a Lua function, use it.
  2. Otherwise build `Default_<event name>` (the literal "Default_" sits in code as an 8-byte immediate, so no string
     search finds it). If that global is a Lua function, use it.
  3. Otherwise there is no handler. 0x6006b0 then returns at once, and 0x5fd250 returns false.
- The result goes into the plan's handler table (plan +0x90 → [TF index +0xf8][code]); 0x6887c0 reads it.
- It is the same for every plan event. A handler defined after instantiation is not picked up [M: no re-scan seen].
- No shipped FoC script defines a `<TF>_Unit_Damaged`: only PGEVENTS.LUA mentions `Unit_Damaged`. So every land and
  space task force uses `Default_Unit_Damaged`.

## 6. Q6: free store and other modes

- **Free store: no.** Free-store units are not task-force members, and nothing else subscribes to signal 0x18 (§1.3).
  The tactical free-store scripts (`BusyTacticalFreeStore.lua`, `GenericFreeStore.lua`) have no damage callback. They
  poll in `On_Unit_Service` (for example `Service_Heal` on `Get_Hull()`, `Service_Kite`, `Service_Attack`). The only
  engine reaction a free-store unit has to a hit is UNIT_AI's own return fire (§1.2 step 5).
- **Galactic: no.** The galactic task force's handler has no 0x18 case.
- **Human units: no.** Human units are never in an AI task force.

## 7. Side notes

- **A correction to AI_TACTICAL §7.7.3 item 6.** It says "damage from the plan's own `AITarget` is ignored". The FoC
  `Default_Unit_Damaged` has no `AITarget` test. It ignores attackers of category `Structure` (and invalid units /
  attackers). A command centre without weapons is a structure, so the conclusion there (it never triggers the handler)
  still holds.
- **Space only, meaning not read.** The space `Add_Force` / removal overrides also write 0 to the member's UNIT_AI data
  (0x4f6450: obj +0x108 → +0x38) [M].
- **The amount is not passed.** xmm1 at the 0x4f59d0 call is not read, and the payload holds only the attacker and the
  bool. Lua reads health and shield itself (`Get_Hull`, `Get_Shield`).

## Port consequence

For `engine/ai/tactical/src/tactical_ai.cpp` (`tf_event`), fed by the battle's damage records:

1. **Raise point: every applied damage instance** that passes the port's equivalents of the `Take_Damage` early gates.
   This includes hits the shield absorbs completely and hits whose hull damage is zero. The victim's health and the
   pre-shield amount must both be > 0.
2. **Attacker.** For a projectile, the shooter looked up by id at impact; for other damage, the source object. Skip the
   event when it is absent, dead or removed. Replace a TEAM member by its TEAM container.
3. **Unit = holder.** The victim if it has UNIT_AI, else its container if that has UNIT_AI, else no event. Skip if the
   holder died from this hit.
4. **Only if the holder is a member of a land or space AI task force.** Never for free-store, galactic or human units.
5. **`deliberate`** = the attacker's current attack target (formation target first) is the holder.
6. **No owner, structure or amount filter in the engine.** Leave those to `Default_Unit_Damaged`.
7. **Coalesce per task force.** If a `Unit_Damaged` is already queued for that task force, drop the new one.
8. **Queue it.** Do not call it synchronously. Run it at that task force's next `PumpEvents`: FIFO with its other
   queued events, before the thread's own Lua continues, at most one per task force per 3-frame Planning service.
9. **Handler:** `<TF>_Unit_Damaged`, else `Default_Unit_Damaged`, resolved at plan start.
10. **Membership.** Subscribe on `Add_Force`. Stop on release or removal. A unit released while its event is queued
    may still receive that one event.
11. **Determinism [port].** "The first hit since the last drain" depends on the order in which damage is applied
    within the frames. Apply the damage records in the battle's deterministic damage order; matching retail's exact
    pick is tier 3.
