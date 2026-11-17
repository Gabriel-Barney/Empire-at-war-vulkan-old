# BKX1 render RE: engine glow on/off (b0020) and Walk_Transition clips (b0059)

RE pass of 2026-11-12, read from the decompiled retail binary (StarWarsG.exe, FoC Steam build) and the exe's
data section. Behaviour only, with no code. Tags: [H] read directly, [M] inferred from the code, [L] guess.
RVAs are relative to image base 0x140000000.

## 1. b0020: what turns a ship's engine glow off and on

### 1.1 The mechanism: particle groups selected by proxy-name prefix [H]
- The object's model wrapper (object +0x2a0) has **8 particle groups**. A group is the set of the model's
  **particle sub-objects whose name starts with a fixed prefix** (case-insensitive `strnicmp` over the prefix
  length). Prefix table at 0x140a15d20:

  | index | prefix | switched by (call sites) |
  |---|---|---|
  | 0 | `pe` | engines: SimpleSpace locomotor service, TURBO / SPOILER_LOCK toggle, death, retreat, hyperspace arrival |
  | 1 | `pte` | turbo engines: same callers as 0 (the opposite of 0 while TURBO is active) |
  | 2 | `pi` | ion stun (`IonStunEffectBehavior`) |
  | 3 | `pgw` | INTERDICT (gravity well) ability (0x3a2240) [M for the ability] |
  | 4 | `pas` | ability case 0x44 in 0x38a350 [M: 68 STIM_PACK] |
  | 5 | `pptw` | POWER_TO_WEAPONS 29 and LEECH_SHIELDS 58 (0x3a4250); case 0x3e in 0x38a350 [M: 62 FULL_SALVO] |
  | 6 | `pem` | ability case 0x21 in 0x38a350 [M: 33] |
  | 7 | `prs` | ability case 0x20 in 0x38a350 [M: 32 MISSILE_SHIELD], plus 0x29f810 / 0x3a0930 |

- **Show / hide** (0x265ae0(wrapper, index, show) → 0x266790): walks the model's sub-object list. Every
  **particle** sub-object whose name matches gets its hidden bit (+0x30 bit 0) set to `!show`. Attached models
  (`Model_To_Attach`) are walked recursively. Meshes are never touched. The wrapper stores the group state at
  +0x20 + 4·index: 1 = shown, 2 = hidden, 0 = never set (0x264890 tests "== 1"). The state is saved, and the
  load path 0x264cd0 applies it again. [H]
- **Tint** (0x265a60(wrapper, index, value, seconds) → 0x266630): for the same matching particle systems it calls
  the scalar setter 0x173220 with index 4 (RGBA colour target) = (value, value, value, value), smoothed over
  `seconds` (0 means snap, STRATEGIC_GAPS §6.2). [H] It is a brightness multiplier on all four channels [M].
- The prefix test uses the prefix length only, so `pe` also matches `pem*` proxies (group 6), but not `pte*`. [H]
- Example: the Star Destroyer glow proxy `pe_stardestroyerengines` (bone 60 under `engines_big`) is in group 0.
  The hardpoint `Engine_Particles` subtree rule (SPACE_AND_OPEN §B.10) is separate and does not reach it. This
  is why the port, which has only the B.10 rule, keeps the glow on. [H]

### 1.2 The driver: SimpleSpace locomotor service (vf6 0x626420), every service [H]
This runs for corvettes, frigates, capitals and transports, after the motion step, in the normal states
(0 / 0x13 / 0x16). The special-state branch (vf +0x210 true: hyperspace and similar) skips it. Inputs:
locomotor pack (object +0xa8), with engines online in +0xd0, timed re-enable frame in +0xd4 (HARDPOINT_DISABLED
§9.1) and current speed in +0xec. The service then does the following:
1. G = 1 (`pte`) if unit ability 9 TURBO is active (0x39b950(obj, 9, 1)), else G = 0 (`pe`). SPOILER_LOCK is
   not tested here.
2. **Engines off (+0xd0 == 0):**
   - Timed disable running (+0xd4 > now): the group is **not hidden**. Tint := (n·0.5 + 0.5)·0.2, snapped
     (0.001 s). n = 3-D value noise 0x7d8310 at (x, x, x) with x = 2·(+0xd4 − now). x is a whole number, so n is a
     lattice value from the fixed table 0x140a45f20, in [−1, 1]. The result is a deterministic per-frame
     **flicker between 0 and 0.2** (free of RNG).
   - Otherwise (permanent loss or an indefinite disable): if G is shown, **hide G**.
3. **Engines on:** if G is not shown, **show G**. Then tint := clamp(0.8·v / V + 0.2, 0, 1), snapped. v is
   +0xec and V is the effective max speed 0x370f00; V ≤ 0 gives tint 0. So the glow is at 20 % at rest and at
   100 % at full speed (MOVEMENT.md "0.8·v/vmax + 0.2").
4. When now + 1 == +0xd4 (the timed disable ends next frame): tint := 1.0 over 0.1 s. From the next frame on,
   step 3 overrides it.
5. Then the engine audio loops (moving if v ≥ 1.1 × `SpaceIdleMovementSpeed`, STATION_HANGARS_SOUND).

### 1.3 Which events reach it
- **`Locomotor_Set_Engines` 0x5545a0 only writes data** (+0xd0 / +0xd1 / +0xd4). It touches no visual. [H]
  Its callers:
  - 0x3924f0 `Engines_Offline`: the last ENGINE hardpoint destroyed (destroyed = 1, permanent) or a station
    upgrade disable (indefinite). Result: the glow is **hidden** at the next service. [H]
  - Take_Damage 0x3a9e30 → energy-pool absorb: a projectile with `Projectile_Disables_Engines_When_Power_Drained`
    gives a timed disable for `Projectile_Disable_Engines_Duration` (CU8F §2.4). In FoC these are
    `Proj_Ship_Ion_Cannon_Small` 20 s, `Proj_Ion_Cannon_Planetary` 10 s, `Proj_Krayt_*Ion` 3 / 10 s.
    Result: the glow **flickers dim** (0–20 %) and is not hidden. [H]
- **Turned back on:** at the first service with +0xd0 == 1, the group is shown again. This happens after
  `Engines_Online` 0x393980 (an ENGINE hardpoint repaired to full health) or after the timed re-enable 0x557ba0.
  [H] In vanilla a destroyed engine hardpoint cannot be repaired, so the permanent hide stays (HARDPOINT_DISABLED
  §9.1). [M]
- Signals 0x16 / 0x17 (offline / online) have no draw-side listener among the callers read. The visual is polled
  from +0xd0 and not driven by the signal. [M]
- **TURBO (9) / SPOILER_LOCK (43)** toggle 0x3a4250: `pe` shown = !active, `pte` shown = active. [H]
  `Engines_Offline` forces TURBO off, which shows `pe` again, and the next service then hides `pe`. [M order]
  Correction: ABILITIES_HARDPOINTS.md §2 (row "1 DEFEND, 9 TURBO, …") calls these 0x265ae0 calls "animation
  states", and L1_S4_REMAINING.md:646 calls one of them a "model stretch effect". They are particle prefix groups.
- **POWER_TO_WEAPONS / LEECH_SHIELDS:** show / hide group 5 `pptw` only. The engine glow changes only through
  the lower speed (step 3). [H]
- **Ion stun (behaviour 0x45, the "Ion_Stun" of ion cannon shots):** at the stun start (0x62b920) it shows
  group 2 `pi`. At the end (vf6 0x62bba0) and on removal (vf9 0x62b8d0) it hides `pi` (third argument 0, read
  from the bytes at 0x62bbc8 / 0x62b8f8). It **does not touch +0xd0 or `pe`**, so the glow stays on and is
  dimmed only by the −50 % speed modifier through step 3. [H] Whether V in step 3 includes that modifier, so
  that the ratio stays the same, was not checked. [L]
- The land `STUN` (49) / `AREA_EFFECT_STUN` (23) path has no space engine-glow code. [L]
- **Death** (0x39bdb0): hide `pe` and `pte` (0x39c052 / 0x39c065). **Creation** (0x3989a0): hide `pte`,
  `pptw` and `prs` (0x398d64 / 77 / 8a). [H]
- **Hyperspace / retreat** (SpaceRetreatCoordinator): when a unit joins the retreat (0x534110), it shows G
  [M: the third argument was not resolved] with tint 0.1, snapped. During the countdown (step counter 0x14–0x59,
  0x533ac0), it shows G with tint 1.0 over 0.2 s. [H for the calls, M for the timing meaning] At hyperspace
  arrival step 0x5a (0x5cd680), it shows G (`pte` if TURBO is active). [M]

### 1.4 The grey smoke trails after the engine kill
- The smoke does not come from the glow logic, and no type has an engine-damaged particle field.
  `Damaged_Smoke_Asset_Name` (type +0xc58) is set only by land and air vehicles (`P_Smoke_Small02`). [H data]
- The smoke comes from the hardpoint destruction rule (SPACE_AND_OPEN §B.10). On the kill frame,
  `HP_Star_Destroyer_Engines`' `Damage_Particles` bone `HP_E_EmitDamage` (52) has its subtree shown: the three
  `p_hp_imperial_damage` proxies on bones 53–55 at the stern. [H rule] These are the grey trails. [M] The
  "~12 s later" is when the probe first looked (R1_T01 run 20261109-055324 says "at +12 s"), not a delay. No code
  path delays them. [M]

### 1.5 Port rule (b0020)
Per space unit with the SimpleSpace locomotor, every sim tick (the snapshot needs: engines_offline,
timed_disable_frames_left, turbo_active, speed / effective max speed, ion_stunned):
- Group `pe` = the particle proxies whose name starts with "pe" (case-insensitive), including those in attached
  models. Group `pte` = those starting with "pte". The engine group is G = turbo_active ? `pte` : `pe`; the
  other group is hidden while TURBO is active.
- **engines_offline with no timer (destroyed or indefinite) → hide G.** This fixes the SD glow.
- engines_offline with a timer (energy-pool ion) → keep G visible, tint it with a deterministic flicker in
  [0, 0.2] (visual only); tint 1.0 over 0.1 s on the last frame.
- engines on → show G, with tint clamp(0.8·v/V + 0.2, 0, 1).
- ion_stunned (behaviour 0x45) → show the `pi` group while it lasts. The engine glow is unchanged, apart from
  the speed tint.
- Death hides `pe` and `pte`. Creation hides `pte`, `pptw` and `prs`. Keep the B.10 `Damage_Particles` /
  `Engine_Particles` subtree rule as it is: it gives the stern smoke.

## 2. b0059: the Walk_Transition clips (Walk locomotor states 0x12 / 0x14)

Anim type ids (ANIM_PARTICLES §A.1): 2 MOVE, 66 (0x42) WALKMOVE, **16 MOVESTART** ("Transition To Move"),
**17 MOVE_ENDONE, 18 MOVE_ENDTWO, 19 MOVE_ENDTHREE, 20 MOVE_ENDFOUR** ("Transition From Move 0 / 40 / 80 / 120").

### 2.1 State 0x12 Start Move (0x61ff40), a started path and `Walk_Transition` [H]
- If the current clip is not 16: play 16 with index 0xFFFF (a synced-RNG variant draw over [0, count − 1]),
  loop 0, blend from the runtime global 0x140b39904 (BSS; the value was not read), rate 0x372ae0 = V / S (1.0
  when S = 0; S = `Scale_Factor` × `Movement_Animation_Speed`). Speed +0xec := 0. The move step runs. Return.
- While clip 16 plays: +0xf4 := max(0, frame + step − (count − 2)), the carry-over. If the clip has not ended
  (0x39a180: frame ≥ count − 2 − step, or the slot is finished), the move step runs in 0x12. Once ended:
  state 0x13 in the same frame.
- **One clip, no turn-direction choice.** Turning is separate (0xf–0x11, TURNL / TURNR, `Has_Pre_Turn_Anim`).
  Vanilla has only `_00` of each transition clip, so the draw always gives 0 (whether a count-1 draw still uses
  an RNG value: [M], per ANIM §A.4 item 3).
- **The clip length drives the duration, and the speed ramp is fitted to it.** The state ends when the clip
  ends. The acceleration a = (S/n)(V/S)² (n = the frame count of clip 16, MOVEMENT §3.4) brings v from 0 to V
  in exactly the clip's play time n·S/V. The move step moves the unit every frame meanwhile (the turn budget is
  × f; no brake-ahead or collision cap in 0x12, §3.5.2 B).
- After it: **0x13** starts MOVE (2), or WALKMOVE (66) / FIRE_MOVE (61) / crouch variants per ANIM §A
  "walk / move", at frame int(+0xf4), so the cycle continues from where MOVESTART left off.
- A path that was never started: → 0x14 if the current clip is 16 (the start is cut off and the end clip
  follows), else 0x15.

### 2.2 State 0x14 End Move (0x61f510) with `Walk_Transition` [H]
- Current clip MOVE (2) or WALKMOVE (66): k = the number of end types 17–20 the model has (0x61d220).
  - k > 0: L = MOVE frame count / k (integer division), seg = int(frame / L). The end clip is
    **17 + (seg mod k)**, chosen by the **phase of the MOVE cycle** (the k clips start the stop from k evenly
    spaced points of the walk cycle: 0 / 40 / 80 / 120 for the AT-AT's 4). The switch waits until the
    in-segment phase (frame − seg·L) ≤ one frame's step. Until then MOVE keeps playing and the speed holds
    (§3.5.2 B.7: T = v while the clip is 2 / 66). At the switch, +0xf4 := the phase.
  - k = 0: the target is the default idle type (0x395920).
- Current clip 16 (an interrupted start): the target is 17 MOVE_ENDONE.
- Any other current clip: the target is the current clip.
- Target == the current clip: once it has ended → **0x15** (no speed test). Otherwise the target is played with
  **index 0 (no draw), loop 0, blend 0 (hard cut), rate V/S**, at frame rate + +0xf4, and +0xf0 := +0xec is
  latched for the deceleration (u = max(0.75·V, v0); d = (S/(m−1))(u/S)², m = the frame count of clip 17). The
  move step runs every frame. **The end clip's end decides when the state ends**; the deceleration is only
  sized to it.
- After it: 0x15 → the default idle (index 0, `Loop_Idle_Anim_00`) and state 0x16 (§3.5.2 G).
- Data [H]: every vanilla `Walk_Transition` model has MOVESTART_00 and MOVE_ENDONE_00. End clips:
  AT-AT 4; AT-ST (also `Scavenger_Droid`, which uses `EV_AT-ST.ALO`), AT-AA, Pod walker, SPMAT, `AI_Rancor`
  (also Cuddles) and Night Sister 3; Bike 1.

### 2.3 Missing clips (mods only) [M]
- No MOVESTART: the index-0 play fails (ANIM §A.4 item 4), so the current clip stays (for example idle). Every
  frame 0x12 retries it, resets +0xec to 0 and runs the move step with a = V (n = 0). The unit keeps moving in
  0x12 until the last waypoint and never starts MOVE: it slides in its idle pose. Not a fallback to MOVE.
- No end clips (k = 0): the default idle plays one-shot, hard cut, at rate V/S. 0x15 follows when it ends.
- MOVESTART present but MOVE_ENDONE missing on an interrupted start: the play of 17 fails every frame and
  0x14 is held on the frozen MOVESTART pose. [L]

### 2.4 Port rule (b0059)
- Enter 0x12: play MOVESTART (16), variant 0, rate V/S, v := 0, and accelerate by a = (S/n)(V/S)² while moving.
- Leave for 0x13 in the frame MOVESTART reaches count − 2. Start MOVE at the carry-over frame.
- Enter 0x14 from MOVE / WALKMOVE: keep MOVE and hold v until the cycle phase reaches a boundary of k equal
  segments. Then hard-cut to end clip 17 + segment index (index 0, rate V/S, start at the phase) and decelerate
  with d = (S/(m−1))(u/S)².
- An interrupted start (clip 16 current) goes to 17.
- Leave for 0x15 / idle when the end clip ends, whatever the remaining speed.
- No end clips: play idle one-shot, then 0x15.
- A missing MOVESTART is a mod-only case: a port may fall back to MOVE (a `[port]` choice; retail slides in its
  idle pose).
