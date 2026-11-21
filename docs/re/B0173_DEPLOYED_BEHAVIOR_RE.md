# b0173 RE answers (second half): `DeployedBehavior`, deploy / undeploy and the build pad on a Mobile Defense Unit

Date: 2026-11-14. Binary: StarWarsG.exe (x64 Steam). Addresses are RVAs. Tags: [H] read directly in the
decompilation, [M] inferred from neighbouring code or an earlier doc, [L] guess.

Case: `Abstract_Mobile_Defense_Unit` (MOBILE_DEFENSE_UNITS.XML): `Deploys` Yes, `Auto_Deploys` default (yes),
`Deployment_Anim_Rate` 2.0, DEPLOY unit ability, `<DeployedBehavior>TACTICAL_BUILD_OBJECTS</DeployedBehavior>`,
`Tactical_Build_Attachment_Bone_Name` ATTACH_POINT_00, `Tactical_Buildable_Objects_Campaign / _Multiplayer` lists.
Its `LandBehavior` list has WALK_LOCOMOTOR and **no** TURRET, CAPTURE_POINT, TACTICAL_SELL or TACTICAL_BUILD_OBJECTS;
it sets no `Destroy_When_Child_Dies` and no `Minimum_Time_Before_Pad_Can_Build_Again` [H, shipped XML].

Vocabulary (earlier docs): behaviour id 0x29 = TACTICAL_BUILD_OBJECTS, object slot byte +0x35b (slot table at
obj +0x332, one byte per id, 0xff = absent); pad data pack = obj +0x150 (+8 under-construction (UC) object, +0x10
finished object, +0x24 / +0x28 cooldown, +0x2c lock; L1_S4_REMAINING §3.6); Walk pack state at obj +0xa8 → +0x48
(0x18 Deploying, 0x19 Deployed, 0x1a Undeploying; MOVEMENT §3.2). `Get_Behavior(id)` = object vf +0x10.
Type fields: `DeployedBehavior` list at type +0x730 (data +0x738, count +0x740, 0x20-byte string entries); behaviour
name registry 0x140a2ac90 (0x20-byte rows: name, …, id predicate at +0x10).

## Q1. 0x39fec0: what runs when the deploy clip ends

Only caller: Walk vf6 0x61e930, state 0x18, in the frame the Deploy clip (animation 0x19) has ended [H]. That
branch does, in this order, inside one call: state := 0x19; raise signal 0x29 on the object (0x220ed0 → 0x240940,
a synchronous walk over the listeners); call 0x39fec0(object).

0x39fec0(obj) [H]:

1. Nothing at all when the object has no type.
2. For each name of the type's `DeployedBehavior` list, **in list order**:
   - find the first registry row whose name matches (`_stricmp`); an unknown name is skipped silently;
   - the behaviour id is the lowest id 0..0x69 that the row's id predicate accepts;
   - **the object does not have that id** (slot byte 0xff): create the behaviour from the name (0x4c3520) and call
     `Add_Behavior` 0x38c570(obj, behaviour, 0). Third argument 0 means Attach (vf +0x38) **and** vf +0x48 both run
     (OBJECT_RUNTIME §4.2);
   - **the object already has that id**: the add is **skipped**. The message "Object %s already has the %s behavior,
     it should not be specified as a Deployed_Behavior too!" is logged only when the type's own static behaviour
     bitset holds the id (0x370320(type, id, set)); otherwise the skip is silent. [L] which bitset: the third
     argument of that call is not visible in the decompilation (the undeploy side passes −1 = the generic `Behavior`
     set, Q2).
3. After the loop, a local-GUI step [H code, M meaning]: when the object's owner (+0x58) is the local player, the
   object now has behaviour 0x29, the object's pack at +0xb0 has byte +8 set [M: it is selected], and the local
   player's record (0x2ad080: object manager +0x1c0, 0x48-byte rows) has +0x30 == 1 [M: exactly one object
   selected], it calls 0x2dcdd0(GUI 0x140b27f60, obj): the tactical build buttons are filled with the pad's buildable
   list for the local player's faction ("%s: Production options are..."). Not simulation state.

It raises no signal of its own, sets no object flag and does not touch the selection. The 0x29 signal is raised by
the caller, before it.

**What the add does for TACTICAL_BUILD_OBJECTS** (class vftable 0x86a240, factory 0x42db60, 0x40 bytes) [H]:
Attach vf7 0x42db20 creates the pad pack if the object has none (0x390f70) and **clears pack +8 and +0x10**. It does
not touch +0x24 / +0x28 / +0x2c, and the pack is never freed at undeploy, so a rebuild cooldown would survive an
undeploy / redeploy [H for the code; no MDU sets a cooldown]. The class does not override vf9 (+0x48), so the base
runs [M: read from the function list of the class, not from the vtable bytes]. vf13 0x42df80 (campaign restore,
pre-placed types) is not called by `Add_Behavior` [H for 0x38c570; M that nothing else calls it at deploy].

**Paths that reach state 0x19 without 0x39fec0** [H]:
- 0x61d300(1) from state 0x1a when the unit has a TURRET (slot +0x33d) that is not at rest: straight to 0x19 and
  signal 0x29, no 0x39fec0. An MDU has no TURRET, so its 0x1a → deploy request goes through 0x18 (the Deploy clip
  from the mirrored frame) and ends in the normal branch with 0x39fec0.
- State 0x16 with the walking-deployed flag (+0x2b3): only for `Max_Dist_Walk_While_Deployed` > 0 (destroyer
  droids); those behaviours were never removed.

Correction to earlier notes: TACTICAL_GAPS line 286 says "deploy 0x391260 (adds `DeployedBehavior`)". 0x391260 only
forwards the deploy request to Walk vf +0x118(1) (when the type `Deploys`, there is a locomotor and +0x2b3 is clear);
the behaviours are added later by 0x39fec0, when the clip ends [H].

## Q2. The undeploy side: 0x3a0580

The deployed behaviours are removed by 0x3a0580(obj), whose only caller is Walk vf6 0x61e930, state 0x1a [H]:

- Each frame in 0x1a: +0x2b3 := 0. If the current animation is not 0x1a (Undeploy): when the unit has no TURRET or
  the turret is at rest, start animation 0x1a at `Deployment_Anim_Rate` **and call 0x3a0580 in the same frame**;
  otherwise recentre the turret and try again next frame.
- So removal is at the **start of the undeploy clip** (the first Walk frame in state 0x1a for a unit without a
  turret), not at its end. When the clip ends: state := 0, signal 0x29, nothing else.

0x3a0580 [H]: for each `DeployedBehavior` name in list order, resolve the id as in Q1; **if the type's generic
`Behavior` bitset (set index 4, data type +0x790, built from the `<Behavior>` list only, 0x374dc0) does not hold the
id**, take `Get_Behavior(id)` and, when it is not null, remove it with 0x3a54c0 (out of the behaviour array, serviced
count and slot table fixed up, reference released). So:

- every `DeployedBehavior` entry is removed, except one that is also in the type's `<Behavior>` list (a name that is
  also in `LandBehavior` **is** removed) [H];
- the object not having the behaviour (for instance an undeploy ordered during 0x18, before the add) → nothing, no
  message [H];
- no Detach (vf15) is called first, no signal, no GUI refresh [H]. The pad pack (+0x150) stays allocated.

Reversing: Deploying 0x18 → undeploy (no behaviours yet, 0x3a0580 finds nothing) and Undeploying 0x1a → deploy
(0x18 again, then 0x39fec0 re-adds what 0x3a0580 removed; anything still present is skipped) are both consistent.

## Q3. The build pad on an MDU

**(a) Undeploy and move with an occupied pad** [H unless tagged].

- Walk `Deploy(false)` = 0x61d300(loco, 0) starts with: if the object has behaviour 0x29 and its pad pack holds a
  **finished object (+0x10, 0x42e3b0) or a UC object (+8, 0x42e3d0)** → return, nothing changes. No state 0x1a, no
  signal, no message. Every undeploy goes through this function (the cap test 0x3ac100, Walk vf +0x148 / +0x160), so
  **an MDU with anything on its pad cannot undeploy**, by any route. The child is not sold, destroyed or detached.
- DEPLOY off (0x3a1ce0(obj, 0)): the gates do not block the off direction; it tells TARGETING to stop (vf +0xd8(1)),
  leaves the coordinator (0x426d50 + 0x39bcb0), calls 0x3ac100(obj, 0) → `Deploy(false)` → refused as above, and
  **still returns true**. No 0x29 is raised, so a Lua block on `Activate_Ability("DEPLOY", false)` would not finish
  on its own [M: follows from AI_TACTICAL §7.3.3 part 3; no shipped script does this with an occupied MDU].
- Move order: the MDU is an auto-deployer, so the group move admits it and the formation plan calls 0x3ac100(member,
  path length) (MOVEMENT §3.2.1 item 2). With M = 0 that is `Deploy(false)`, refused; the unit stays in 0x19, where
  the Walk frame only zeroes the speed. **The MDU does not move and does not undeploy** [H for the refusal; M that
  the formation simply keeps its path with a standing member; L what happens to that pending order after the pad
  becomes empty: nothing re-issues `Deploy(false)` by itself that was found]. The coordinator admission 0x425e30
  also rejects a 0x29 builder with pad contents (MOVEMENT §5.4, item 4).
- An **empty** deployed MDU undeploys normally on DEPLOY off or a move order (state 0x1a, behaviours removed at the
  clip start, 0x29 at its end).
- Once the pad is empty again (child died, or was sold: 0x42ecd0 clears +0x10), undeploy works again.
- The behaviour's own removal has no handler: the class overrides no detach slot, and 0x3a0580 calls none. Removal
  with contents cannot happen through the undeploy path because of the gate above. [M] A build started in the short
  window between a state-0x1a entry and the removal frame is not guarded; it needs the pad to be empty at the
  `Deploy(false)` call and a `Build` before the next Walk frame, and the re-attach at the next deploy would then
  clear the slots while the child lives on. Not reachable from the shipped script.

**(b) Deaths** [H].
- MDU dies (`Die` 0x39bdb0): if the pad pack holds a finished object, else a UC object, `Die` is called on it with
  the same arguments. **The child dies with the MDU.**
- Child dies: its parent link (+0x2b0, set to the pad at build start) has behaviour 0x29 → 0x42e6d0(behaviour, pad,
  child): the slot is cleared (for the finished object also the lock +0x2c; persistence only with
  `Tactically_Built_Child_Object_Persists`, which the MDU sets to No). Then pad type `Destroy_When_Child_Dies`
  (+0x99) → the pad is killed (0x391310(pad, 1)). **The MDU does not set it, so the MDU survives**; it has no
  CAPTURE_POINT, so there is no owner reset; reveal is refreshed for every player; the cooldown starts only when
  `Minimum_Time_Before_Pad_Can_Build_Again` > 0 (unset on the MDU [M: default 0 not re-read]); pad animation 0x75.
  The MDU can then build again or undeploy.

**(c) Bone** [H]. The UC object (0x42dbd0) and the finished object (0x42e890, +0.1 z) are **created at** the world
translation of the pad model's `Tactical_Build_Attachment_Bone_Name` bone (0x42e280: pad position when the pad has no
model, the name is empty or the bone is missing), with the pad's facing (+0x84). After creation the only link is the
child's parent pointer (+0x2b0 := pad, 0x3a8f80), the pack slot and the pad behaviour listening to the child's signal
0x28. Nothing found moves the child with the bone afterwards [M: no follow code was found in the pad functions; a
deployed MDU with a child cannot move anyway, (a)].

## Q4. Lua `Build` (0x5739c0) and `Get_Build_Pad_Contents` (0x577e20)

`Build`, in order [H]:
1. wrapper holds no object → returns nothing, **no message**;
2. argument count not 1 or 2 → error;
3. parameter 1 not a type wrapper or a known type name → error ("invalid type" / "unknown game object type %s");
4. **`Get_Behavior(0x29)` is null → error "GameObjectWrapper::Build -- Object %s is not a valid build location."**
   That is the whole test: the object currently has the TACTICAL_BUILD_OBJECTS behaviour. Nothing else (no deploy
   state, no pack test) is read;
5. pad pack +8 or +0x10 not null → error "already has something built on it";
6. two arguments: parameter 2 must be a boolean (error); when true, the type must be in the pad's buildable list for
   the pad owner's faction and 0x42e400 must pass, else a **warning** and `false`;
7. 0x42dbd0(behaviour, pad, type, pad owner id, charge 0, sfx 0): false → error "unknown error starting build…";
   true → returns `true`. With one argument (the plan script's form) the type is **not** checked against the
   buildable list and nothing is charged.

So an MDU that is not deployed (never deployed, still in 0x18, or already past the removal frame of 0x1a) has no
behaviour 0x29 and `mdu.Build(x)` raises the "not a valid build location" **error** in retail too. The error helper
0x248190 sets script byte +0x121, which ends the plan script at the end of that service pass (B0173_RE_ANSWERS Q4)
[H for the flag write, re-read here].

`Get_Build_Pad_Contents` [H]: no object → error "this object is already dead"; no behaviour 0x29 → **warning**
(0x248620, which does not set +0x121) "object %s is not a build pad" and nothing; else the finished object, else the
UC object, else nothing. So the script's wait loop `while TestValid(mdu) and not
TestValid(mdu.Get_Build_Pad_Contents())` ends **as soon as the UC object exists**, i.e. on the first poll after a
successful `Build`, not when construction completes; on a non-pad it only warns each poll and keeps waiting.

## Q5. `BlockOnCommand(Activate_Ability("DEPLOY", true))` versus 0x39fec0

In Walk vf6 0x61e930, state 0x18, the three steps "state := 0x19", "raise 0x29", "0x39fec0" are consecutive
statements of one branch [H]. The signal dispatch is synchronous and only marks the ability block finished; Lua is
resumed later by the script service [M for the listener body, AI_TACTICAL §7.3.3]. So the behaviour is added in the
same Walk service call that raises 0x29, **before any Lua code can observe the block as finished**. After the block
returns the MDU already is a build pad; `Sleep(2)` is not needed for that.

The cases where `BlockOnCommand` returns and the MDU is **not** a pad are the ones where the 0x29 it saw was not the
deploy's: the block also ends on a new order (0xb), on any other ability ending (0x29) and on death (AI_TACTICAL
§7.3.3 part 2), and a refused deploy (0x3a1ce0 gates: moving locomotor, garrison, ability countdown) raises nothing
and the block waits. In those cases the following `Build` is the Q4 error and the plan script ends, in retail as
well.

## Not determined

- The bitset argument of the "already has the behavior" warning test in 0x39fec0 (third argument not visible). It
  only decides whether a log line is written; the add is skipped either way.
- The meaning of obj +0xb0 → +8 and of the per-player record +0x30 == 1 in the GUI step of 0x39fec0 ([M] selected /
  single selection).
- What becomes of a move order given to an MDU with an occupied pad after the pad empties ([L] it stays deployed
  until a new order or DEPLOY off).
- Whether anything outside the pad functions moves a built child with its parent ([M] no).
- vf9 (+0x48) of TacticalBuildObjectsBehaviorClass was taken as the base implementation from the class's function
  list; the vtable bytes were not read.

## Port consequence

1. When a Walk unit's deploy clip ends (0x18 → 0x19): set state 0x19, raise signal 0x29, then, in the same call, add
   every `DeployedBehavior` name of the type in list order (attach + initialise). Skip a name the object already has
   (optionally log the retail message when the type lists it statically); skip unknown names silently.
2. Attaching TACTICAL_BUILD_OBJECTS creates the pad pack if missing and clears its UC and finished slots; keep the
   cooldown and lock fields across undeploy / redeploy.
3. Do not add the deployed behaviours on the two other ways into 0x19 (turret-not-at-rest reversal out of 0x1a;
   walking-deployed return from 0x16).
4. In state 0x1a, in the frame the Undeploy clip is started (turret at rest or no turret), remove every
   `DeployedBehavior` entry the object has, in list order, except ids that are in the type's generic `<Behavior>`
   list. Missing behaviour: nothing. No signal. The clip end only sets state 0 and raises 0x29.
5. Walk `Deploy(false)` does nothing when the object has TACTICAL_BUILD_OBJECTS and its pad holds a UC or finished
   object. This covers DEPLOY off (which still returns true and raises no 0x29), move orders to an auto-deployer and
   every other undeploy caller: an MDU with a turret on it stays deployed and does not move.
6. MDU death kills the pad content (finished, else UC). Child death clears the pad slot (and lock); the MDU survives
   (`Destroy_When_Child_Dies` unset), no owner reset (no CAPTURE_POINT), cooldown only if the type sets one.
7. The built object is spawned at the attachment bone's world position (pad position as fallback) with the pad's
   facing and gets the pad as parent; it is not re-positioned afterwards.
8. Lua `Build`: no object → nothing, silently; object without the TACTICAL_BUILD_OBJECTS behaviour → **error** "is
   not a valid build location" (ends the plan script, as in retail); occupied → error; one-argument form builds for
   the pad owner, free, with no buildable-list or proximity test. `Get_Build_Pad_Contents`: non-pad → warning and
   nothing; returns the finished object, else the UC object.
9. Because rule 1 runs in the same call as the 0x29 that finishes the DEPLOY block, a plan that passed
   `BlockOnCommand(Activate_Ability("DEPLOY", true))` through the deploy's own 0x29 always finds the MDU buildable;
   the port's `Build` failure after a completed deploy is the missing rule 1, not a timing issue.
