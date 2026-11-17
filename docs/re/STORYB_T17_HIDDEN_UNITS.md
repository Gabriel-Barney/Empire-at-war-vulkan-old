# storyb t17 / t11: hidden units, Suspend_AI, Prevent_All_Fire, Make_Invulnerable, Face_Immediate (static read, 2026-11-17)

Questions (roadmap storyb t17 and t11, tier 1): in the hands-off Kessel UM01 run (`STORY_T04_KESSEL_RUN.md` §7.1) the
human's hero Tyber (`UMP_TYBER_ZANN`, owner 6) neither moves nor takes damage until the cinematic walks him at battle
frame 3303. The mission Lua has called `Cinematic_Suspend_Units()`, `Suspend_AI(1)`, `Lock_Controls(1)`,
`Start_Cinematic_Camera()` and, at about frame 380, `tyber.Hide(true)`. In the port Tyber turns and moves at frame 482
and Empire units shoot him from frame 1194. Which retail rules keep him still? And what does `Face_Immediate` write?

Binary: FoC Steam x64 `StarWarsG.exe`. Addresses are RVAs (image base `0x140000000`). Read with ReVa (Ghidra 12.1.4,
`/StarWarsG.exe`). The reader census of §1.2 comes from a full linear disassembly of `.text` (`toolchain\llvm`
`llvm-objdump`, Intel syntax) plus a raw byte scan for the displacement `A2 03 00 00`. REA's first call timed out at
Ghidra startup and was not used. Mission script: `STORY_UNDERWORLD_ACTI_M01_LAND.LUA` (`extracted/foc_data_text.zip`).
Tags: **[H]** read in code, **[M]** likely (partly read), **[L]** inferred, not read.

## Results

| Q | Question | Answer | Conf. |
|---|---|---|---|
| 1 | Who reads obj +0x3a2 bit 0 (set by `Hide`) in tactical? | **No simulation code.** The one reader outside save / load / state copy is the 3D sound visibility factor `0x2d4b00` (audio). Target scans, opportunity fire, auto-acquire, the targeting service, locomotors, collision and the object update never test it. A hidden unit is still a target, still scans, fires, moves and collides | H |
| 2 | What does `Suspend_AI(1)` suspend? | Only the AI **players'** AI systems (perception, goals, planning, execution, learning, templates) through TheAI's master byte. Unit behaviours of every player (targeting, auto-acquire, auto-fire, hard point opportunity fire, chase and return, UNIT_AI, idle) keep running, AI-owned units included | H |
| 3 | Does `Make_Invulnerable` or `Prevent_All_Fire` remove a unit from enemy targeting? | **No.** `Make_Invulnerable(true)` adds an INVULNERABLE behaviour that only the damage path reads. `Prevent_All_Fire(true)` stops the unit's **own** targeting and firing (suspends its TARGETING and WEAPON behaviours, switches its weapon hard points off). Neither one is tested by the enemy's target scan | H |
| 4 | Any retail rule that keeps an idle human unit from engaging while `Lock_Controls`, cinematic camera or letterbox is on? | **None found.** `Lock_Controls` is read only by input and on-screen markers; the "cinematic running" test is read by presentation, UI and two spawners. The rules that do keep a unit still are per unit: `Prevent_All_Fire`, `Suspend_Locomotor`, and the land chase rules (an idle land unit chases only through a formation's chase block) | M |
| 5 | What does `Face_Immediate(target)` write? | It **snaps the object's orientation** (roll 0, pitch, yaw toward the target) with no turn rate. That orientation is the one heading the locomotor uses; there is no separate locomotor heading for the primary facing. Orders, path, locomotor state and speed are untouched. On land it also re-seats the unit on the terrain (pitch / roll from the ground) unless the type has `Targeting_Land_Model_Stay_Horiz_Flat`. The facing holds while the unit is idle; a moving unit turns back toward its path at its turn rate | H (hold rules M) |

So `Hide` does not explain retail Tyber's stillness. What the binary does show: nothing in the cinematic state stops a
human unit's own targeting. Retail Tyber therefore had no acquirable enemy in his scan cube up to frame 3303, or did
not chase one [L: map and fog not checked; §4.3 lists the filters that apply]. The retail Empire units near him are the
ones `Cinematic_Suspend_Units` and the Begin state's bribe squad put under `Prevent_All_Fire` (and `Suspend_Locomotor`,
`Make_Invulnerable` for the squad), so they cannot shoot even with Tyber in range.

## 1. `Hide` and obj +0x3a2 bit 0

### 1.1 What `GameObjectWrapper::Hide` (`0x57a5b0`) does [H]

1. No object behind the wrapper (wrapper +0x60): return. No parameter: log "Missing required boolean parameter". A
   non-boolean: log "Expected bool parameter".
2. The object has no model (obj +0x2a0 null): log "object %s has no model" and return. **Nothing is set**, not even
   the bit.
3. Otherwise the model is hidden or shown (`0x264c80`: model node +0x48 → +0x30 bit 0 := flag; a second render node at
   model +0x68 gets the inverse visibility), and obj +0x3a2 bit 0 := flag.

No call into the object manager, the spatial trees, the behaviours, the locomotor, targeting or the orders.

Two Lua globals write the same bit [H]: `Hide_Object(obj, f)` (`0x542970`, also through the story manager `0x44b420`)
and **`Hide_Sub_Object(obj, f, name)`** (`0x542b20` → `0x44b470`). The second one sets obj +0x3a2 bit 0 := (f > 0)
for the **whole object** after hiding the named sub-mesh. In UM01, `tyber.Hide(false)` followed by
`Hide_Sub_Object(tyber, 1, "newgun")` (script lines 1051-1052) leaves Tyber's model shown but bit 0 set again, until
`Hide_Sub_Object(tyber, 0, "newgun")` (line 1374). Bit 0 is therefore not a reliable "model hidden" flag; only the
audio factor of §1.2 sees the difference [H code, M effect: Tyber's attached 3D sounds are muted in that stretch].

### 1.2 Every reader of obj +0x3a2 bit 0 [H]

The disassembly of `.text` has 103 instructions with displacement 0x3a2 (the 9 other raw byte hits are jump
offsets). No dword or word access covers obj +0x3a2 (the dword accesses at +0x3a0 that exist belong to other
classes or to the targeting pack obj +0x100). The bit-0 accesses:

| Kind | Where |
|---|---|
| **Read (test)** | `0x2d4b00` only: 3D sound visibility factor = 0 when bit 0 is set, the model is hidden (`0x264cb0`) or the object is fogged for the local player (not during a cinematic); else 1. Callers `0x2d7450`, `0x2d7a40` (sound update). This is the `AUDIO.md` §3 "hidden" factor, now [H] |
| Set / clear | Lua `Hide` `0x57a5b0`; `Hide_Object` `0x542970` / `0x44b420`; `Hide_Sub_Object` `0x542b20` / `0x44b470`; hyperspace states of the Fighter locomotor (`0x5c9800`, `0x5c9a80`, `0x5cb020`, `0x5cbc10`, `0x5cc970`, `0x5cd010`) and the SimpleSpace locomotor (`0x6251d0`, `0x6269f0`, `0x626c80`, `0x627590`); TransportLanding (`0x642c70`, `0x643a30`, `0x643b00`) |
| Copy / persist | object init `0x388720` / `0x388b60` (whole byte); state copy `0x472560`; save / load `0x3a5ad0` / `0x39ce40` (byte address passed) |

Not readers: `Scan_Player` `0x5662b0`, `Acquire_Target` `0x568a00`, `Validate_Current_Target` `0x567c50`, the
targeting service `Service_Land` `0x634810`, the hard point opportunity scan `0x385190` (`TACTICAL_COMBAT.md` §2.3,
§2.6), the fog test `0x35f470`, `Order_Attack` `0x38d730`, the object update `0x3a6b80` (it services every behaviour
whose suspend count is 0, `0x4c3700`, hidden or not), the locomotors' move and turn code, and the collision system.

### 1.3 Readers of the model's hidden state (`0x264cb0`) [H list, M roles]

`Hide` also hides the model, so these are the other places a hidden unit differs. All are presentation or space
hyperspace bookkeeping:

- scene render `0x266d10`, `0x267680`; surface FX `0x5efe50`; 3D sound `0x2d4b00`;
- radar blip filter `0x35b4d0`; fog ghost manager `0x4eb020`; mouse picking of an enemy under the cursor `0x2ad280`
  (called from the input code `0x39980`) [M: role];
- `Model_Visible_To_Enemy` sync in the object update `0x3a6b80`: for a type with that flag clear (type +0xa2), the
  model is hidden while the local player is an enemy of the owner and shown otherwise. That re-shows a `Hide`-hidden
  unit of such a type for its own side [H code];
- hyperspace in / out states of the Fighter and SimpleSpace locomotors (`0x5cbc10`, `0x5cc970`, `0x6269f0`,
  `0x626c80`).

No targeting, Walk locomotor or collision function calls it.

### 1.4 Answers to the sub-questions [H]

- **Skipped as a target?** No. The enemy scan's candidate filter (`TACTICAL_COMBAT.md` §2.3) has no hidden test, and
  `Hide` leaves the object in its player's collidable tree. Enemies acquire and shoot a hidden unit like any other.
- **Does a hidden unit stop acquiring?** No. Its TARGETING behaviour keeps being serviced.
- **Does it stop moving?** No. Its locomotor keeps running; a hidden unit with an order or a chase block walks.

## 2. `Suspend_AI` (Lua `0x544ec0`) [H]

1. Argument missing: log "Expected float parameter 1", nothing changes. Not a number: log "Invalid parameter 1,
   expected a float" and **un-suspend**. Else suspended = (x > 0).
2. `0x44cd90` (story manager `0x140a28c10`): story byte +0x82 := suspended; TheAI (`0x140a16d90`, created on first
   use) master byte +0xb := !suspended (`0x4d9fa0` with index 3).
3. TheAI's enable bytes are read only by `0x4d9f80` (master clear → off, else the per-context byte), and that function
   has one caller: the AI player's per-context update `0x5f5ab0`. With the master off, it services none of the six AI
   systems of that context (+0x38 … +0x60: perception, goal, planning, execution, learning, template). The galactic
   context alone may still run one system (+0x48) while the auto-resolve object's vf +0xe0 is non-zero.
4. The story manager reset `0x44b130` clears +0x81 / +0x82 and re-enables TheAI.

No reader of story byte +0x82 other than these two functions was found (no direct reference to `0x140a28c92`) [M].
Unit behaviours do not read TheAI: the targeting service, `Acquire_Target`, `Auto_Engage` (`0x42d0f0`), UNIT_AI
(`0x4f6070` and its chase-and-return step `0x4f6470`), the hard point opportunity fire and the locomotors have no AI or
story test. So under `Suspend_AI(1)` the AI players issue no new orders and run no plan scripts, but every unit of
every player, the AI's included, still auto-acquires, fires, chases inside its formation's chase range and returns.
`AI_GALACTIC.md` §1.2 states the same write; this adds the reader census.

## 3. `Make_Invulnerable` and `Prevent_All_Fire`

### 3.1 `Make_Invulnerable(b)` (`0x57d550`) [H]

- Exactly one boolean argument, else a log line ("invalid number of parameters" / "Expected boolean").
- Works only while the current mode is tactical (mode vf +0xf0, land or space; `BKC1_RE_ANSWERS.md` Q1).
- Not a team container (obj +0x348 = −1): true adds the behaviour `INVULNERABLE` (id 0x4b, built by name through the
  factory `0x4c3520`, `Add_Behavior` `0x38c570`) unless the object already has one (slot obj +0x37d ≠ −1); false
  removes it (`0x3a54c0`). Not a counter.
- Team container: the same for **each member**; the container itself gets nothing.

Readers of the INVULNERABLE slot (+0x37d) or of behaviour 0x4b in the whole image: `Take_Damage` `0x3a9e30` (damage gate
at `0x3aa1c7`, `TACTICAL_COMBAT.md` §3.6), the projectile ability `0x4cc010` (adds INVULNERABLE to allies), the
hyperspace arrival states `0x5cc970` / `0x626c80`, and `Make_Invulnerable` itself. **No targeting function reads it**:
an invulnerable unit stays a normal target and is shot for zero damage.

### 3.2 `Prevent_All_Fire(b)` (`0x57ee20`) [H]

- Exactly one boolean argument, else a log line.
- For a team container, first for **each member**, then for the object itself:
  - **true:** weapon hard points off (`0x392570`: every hard point of type 5-10, the weapon types, gets hp +0x6c := 0
    and its target cleared through `0x387b70`), then the TARGETING (9) and WEAPON (10) behaviours are **suspended**
    (behaviour vf +0xc0: suspend count +0x3c += 1; base `0x4c30b0` for TargetingBehavior and TeamTargetingBehavior;
    WeaponBehavior's override `0x54ac30` does the same and also hides its muzzle-flash meshes).
  - **false:** weapon hard points on (`0x393b20`: hp +0x6c := 1), TARGETING and WEAPON resumed (vf +0xb8: count −1,
    floored at 0).
- The suspend count is a counter: two `true` calls need two `false` calls. (UM01's `Cinematic_UnSuspend_Units` calls
  `Prevent_All_Fire(true)` again on `first1` / `first2`, so those two stay suspended; shipped script.)
- The object update `0x3a6b80` services a behaviour only while its count is 0 (`0x4c3700`). With TARGETING suspended
  the land service `Service_Land` `0x634810` (team: `0x62eeb0`) never runs, so the unit does not scan, does not keep or
  validate a target, does not push the chase block (`Auto_Engage`, §4.2) and does not snap to face a target (§5.3).
  With WEAPON suspended its own `Projectile_Types` weapon does not fire. Hard point fire stops through hp +0x6c [M: the
  reader of hp +0x6c was not traced].
- Nothing here touches what the enemy's scan reads (§1.2 list), so the unit can still be targeted.

## 4. Lock_Controls, cinematic camera, letterbox (Q4)

### 4.1 `Lock_Controls(x)` (`0x543060`) [H write, M roles of the readers]

Writes story byte +0x81 (`0x140a28c91`) := (x > 0); a non-number clears it. Readers: the keyboard window procedure
`0x60c60`, the hotkey area `0x63700`, the input function `0x2ee40` (input [M]); the selection circle and health bar
markers `0x3c2710` (hidden while locked or while a cinematic runs) and an object status visual `0x6fc3d0` (presentation
[M]). No targeting, locomotor, behaviour or AI function reads it.

### 4.2 Cinematic running (`0x2cd0c0` on the controller `0x140b27a10`) [M]

63 call sites. The ones in simulation code: LocomotorCommon vf41 `0x42ced0` (skips only a stop sound), the ability
button test `0x39b310` (abilities cannot be used from the UI), and SpawnSquadron `0x593350` (no fighter spawning during a
cinematic unless the type flag +0xa0 or `0x140b27d90` is set). The rest are presentation, sound, UI and mode code. Not
called by `Acquire_Target`, `Scan_Player`, `Service_Land`, `Auto_Engage`, UNIT_AI or `Order_Attack`. The letterbox
(`STORY_PRESENTATION_LUA.md` §1.4) changes only the HUD state.

### 4.3 What does keep an idle land unit from engaging [H unless tagged]

- `Prevent_All_Fire(true)` on it (§3.2) or `Suspend_Locomotor(true)` (locomotor suspended, `MOVEMENT.md`).
- **Chasing needs a formation.** `Auto_Engage` (`0x42d0f0`, called by `Service_Land` every service with a target)
  acts only when the unit's locomotor pack has a formation (+0x1f0) and the formation's chase test `0x4160a0` passes;
  it then pushes an attack block. Without a formation, an acquired target is only set (`Order_Attack` `0x38d730` with
  ordered = 0 sets the targeting and hard point targets and starts no move on land). UNIT_AI's return step `0x4f6470`
  also needs a formation. The land scan cube (`0x566f50`): R without a formation; inside one, R + the chase range when
  the chase test passes, else 0 unless the unit has a TURRET. Table of formation states: `BKR1_RE_ANSWERS.md`, Q8.
- The scan's own filters (`TACTICAL_COMBAT.md` §2.3): only players whose relation is enemy, candidates in the player's
  collidable tree, not fully fogged for the attacker's player (`0x35f470`; Tyber's type has HIDE_WHEN_FOGGED, so he can
  be fogged for the Empire), the attacker's priority set. For Tyber: `Targeting_Max_Attack_Distance` 90,
  `Idle_Chase_Range` 30, `Guard_Chase_Range` 60 (`UNITS_HERO_UNDERWORLD_TYBER_ZANN.XML`).

Which of these held for retail Tyber up to frame 3303 was not established [L]. Tyber is not in
`Cinematic_Suspend_Units`, so neither suspension applies to him.

## 5. `Face_Immediate(target)` (Lua `0x576110`) (t11)

### 5.1 What it writes [H]

1. No object behind the wrapper: return. No argument: log "Missing required position or gameobject parameter". The
   argument goes through the position extractor `0x4990b0` (object → its position, position → itself, task force →
   its position); failure: log "could not extract a position from parameter 1". No death or owner check.
2. `Face_Position` `0x394440`(obj, P):
   - angles `0x20acd0` from the object position (obj +0x78) to P, in degrees: **roll 0**; **yaw** = atan2(dy, dx),
     wrapped to [0, 360), and **0 when P has the same x and y** (not "unchanged"); **pitch** = −atan2(dz, horizontal
     distance), 0 when dz = 0 (positive pitch is nose down, `FIGHTER_FLYING.md` §1.1).
   - Land mode (mode type 1) and the type's `Targeting_Land_Model_Stay_Horiz_Flat` (type +0xad) set: pitch := 0.
   - Orientation setter `0x3a8710`: unless obj +0x3a0 bit 0x10 (stunned / weapons disabled) is set, writes (roll,
     pitch, yaw) to **both** orientation copies obj +0x84…+0x8c and +0x90…+0x98 and marks the object dirty
     (obj +0x3a1 bit 0). No turn rate.
   - Land mode, flag clear, and the object has a locomotor: locomotor vf95 (+0x2f8) at the current position: terrain
     conform and commit (`MOVEMENT.md` §3.6): z from the terrain, pitch and roll recomputed from the ground along the
     new yaw. This runs even when the setter was skipped (stunned).
3. Nothing else: no stop, no path change, no new order, no locomotor state change, no speed change, no turn target,
   no targeting change.

So the port's single "store facing" write is half of it: in retail the object orientation **is** the locomotor's
current heading.

### 5.2 Is the facing held? [H for land Walk, M for the rest]

- **One heading.** The Walk locomotor reads its current yaw from the object orientation (obj +0x84…+0x8c / +0x98,
  `MOVEMENT.md` §3.5.1, "Arrival turn"). Its wanted yaw is recomputed from the path each frame; there is no stored
  desired heading to reset. The only separate yaw is the **secondary facing** (pack +0x268…+0x270, types with
  `UseSecondaryFacing` only), which `Face_Immediate` does **not** update. At the next formation block pop such a unit's
  body snaps back to it (`MOVEMENT.md` §3.5.1 (c)).
- **Idle** (Walk state 0x16, no path): nothing turns the unit; the new facing holds.
- **Moving**: the move step keeps steering toward the path from the new yaw, at the turn rate. The unit turns back
  gradually; it does not snap back. The arrival turn to a final facing (§3.5.1) still applies.
- **With a target in range** (land): `Service_Land` may itself snap a non-deploying unit to face its target with the
  same `0x394440` (state 4 branch, `0x634ea2`) or turn it through the locomotor (vf +0x218). A unit with a target
  therefore faces the target again on its next targeting service. TeamTargeting `0x62eeb0` does the same for squads.

### 5.3 By unit kind [H for the code paths, M for the consequences]

- `0x576110` / `0x394440` have **no team loop** (unlike `Prevent_All_Fire`): on a squad container only the container
  turns. Members keep their own yaw. The slots are not re-laid by an orientation change (re-lay events:
  `FIGHTER_FLYING.md` §7.2), and an idle member turns only for the recovery walk when it is more than 4.0 from its slot
  (§7.3 items 6-7).
- Infantry and vehicles (land): the same path, the only difference being `Targeting_Land_Model_Stay_Horiz_Flat` (flat,
  no terrain conform) versus conform.
- Ships (space mode): no flat rule, no conform; the pitch toward the target's height is kept. A SimpleSpace ship that is
  following a path recomputes its yaw from the path curve every frame (`MOVEMENT.md` §2.5), so the snap lasts only
  until the next locomotor update; a stopped ship keeps it while its roll decays toward 0 [M].

## 6. Port consequences (for storyb t17 / t11)

- `Hide` must not gate any simulation: no target exclusion, no scan stop, no movement stop. It should hide the model
  and drive the 3D sound mute; `Hide_Object` and `Hide_Sub_Object` set the same object bit.
- `Prevent_All_Fire(true)` = suspend (counted) the unit's own targeting and weapon behaviours and switch its weapon
  hard points off, members first for a squad; the unit stays targetable. `false` undoes one level.
- `Make_Invulnerable` = an on / off INVULNERABLE behaviour per object (members for a squad), read only by damage.
- `Suspend_AI` = stop the AI players' systems only.
- `Lock_Controls`, the cinematic camera and the letterbox are not simulation inputs.
- `Face_Immediate` = snap the one object orientation that movement also uses (and re-seat on land terrain), leaving
  orders and paths alone.

## Sources (RVAs)

`0x57a5b0` Hide; `0x264c80` / `0x264cb0` model hide / is hidden; `0x542970` / `0x44b420` Hide_Object; `0x542b20` /
`0x44b470` Hide_Sub_Object; `0x2d4b00` 3D sound visibility; `0x3a6b80` object update; `0x4c3700` behaviour active;
`0x4c30a0` / `0x4c30b0` behaviour resume / suspend; `0x544ec0` Suspend_AI; `0x44cd90` story suspend write; `0x44b130`
story reset; `0x4d9f80` / `0x4d9fa0` TheAI enable read / write; `0x5f5ab0` AI player context update; `0x543060`
Lock_Controls; `0x3c2710` markers; `0x2cd0c0` cinematic running; `0x42ced0`, `0x39b310`, `0x593350` cinematic readers;
`0x57d550` Make_Invulnerable; `0x3a9e30` Take_Damage; `0x4cc010` invulnerability projectile; `0x57ee20`
Prevent_All_Fire; `0x392570` / `0x393b20` weapon hard points off / on; `0x5662b0` Scan_Player; `0x568a00`
Acquire_Target; `0x566f50` land scan range; `0x634810` Service_Land; `0x42d0f0` Auto_Engage; `0x38d730` Order_Attack;
`0x4f6070` / `0x4f6470` UNIT_AI service / chase return; `0x35f470` fog test; `0x576110` Face_Immediate; `0x394440`
Face_Position; `0x20acd0` angles between two points; `0x3a8710` orientation setter; `0x39a370` type `Deploys`.
