# Story-script presentation bindings: what they do on screen

Behaviour read from retail `StarWarsG.exe` (FoC Steam x64 v1.121), in words and tables; no decompiled code. Confidence per
item: **[H]** read in code, **[M]** inferred from code plus script usage, **[L]** guess. RVAs are image-relative (base
0x140000000). Companion specs: `TSW_PLANET_DESTROY.md` §11 (firing shot, shake), `BKC1_RE_ANSWERS.md` Q1 (Esc during a
cinematic), `MAIN_MENU_BACKGROUND.md` §3 (the cinematic presentation switch 0x49b1a0), `UI.md` (input order, selection
blobs), `MAIN_LOOP.md` §1.4 (per-frame work), `Q6_T27_ELIM_END_TIMING.md` §3 (advisor hint text), `LUA_API.md` (binding
table). The port side is `engine/lua/src/bind_presentation.cpp` (records `PresentationOp` events and linear `Ramp`s in sim
frames); §9 lists where it differs.

## 0. The three objects behind these calls

| Object | Address | Role |
|---|---|---|
| **Camera FX manager** ("overlay") | 0x140a28c10, update 0x44bc90, attach to a mode 0x44b500, detach 0x44b130, init 0x44ce30, save writer 0x44c120 / reader 0x44b770 | Owns three solid black 2D quads (full-screen fade quad, top bar, bottom bar), the letterbox / fade timers, the scripted camera **scroll**, **yaw rotation** and **follow** state, and a few mode flags. Everything in §1 and §3 is a method of it. The GameConstants name `Camera_FX_Manager_Letterbox_Height` is the bar height (global 0x140b27334, vanilla 0.085 [M]). [H] |
| **Cinematic controller** | 0x140b27a10 (`CinematicClass`; fields in TSW §11 and BKC1 Q1) | Owns the cinematic **key tracks** (camera track, target track, roll track, shake list), the shot clock (+0x154) and the Esc / mouse routing. Everything in §2 is a method of it. [H] |
| **Galactic / tactical HUD** | 0x140b27f60 | Owns the hint-text queue (`Game_Message`), the highlight slots (`Add_Planet_Highlight`) and the HUD hide flag (`0x140b2a5f9`). [H] |

Time bases (the single most important rule for the port, because the port converts all of these to sim frames):

| Effect | Clock | Runs while paused? | Runs at game speed 2x? |
|---|---|---|---|
| Fade, letterbox, scripted yaw rotation, scroll, follow | **wall-clock seconds of the render step**: the overlay update is called once per rendered frame from the mode's `Pre_Render(d)` (land 0x3bad20, space 0x4d9110, common 0x362c60) with d = wall delta, **not** from the 30 Hz tick. The galactic update 0x4b2c00 calls the overlay once with the real d and only then zeroes d for the rest, so the overlay keeps running while the galactic view is paused [H]. A tactical pause (mode vf +0x48 true) is **not** tested for the fade (it is tested only in the pre-start hold, §1.5) [M]. Letterbox progress additionally freezes while a modal GUI dialog has focus (global 0x140a78510 test inside 0x44bc90) [H] | yes (fade/letterbox); camera rotation / scroll too [M] | independent of game speed [H] |
| Cinematic camera keys | **mode frame clock** (controller +0x154 = frames since Start / last reset, advanced by the shot timer 0x4ad3a0 which records the mode frame). Keys are stamped in frames: `frames = int(FPS * seconds)` with FPS global 0x140b0a340 (30). Cinematic mode forces the tick rate to 30 (FS +0x37, MAIN_LOOP §1.5) | no (clock stops with the mode frame) [M] | the rate lock pins it to 30 ticks/s [H] |

So a script's `Sleep(5)` after `Fade_Screen_Out(2)` waits sim time while the fade runs on wall time; at normal speed and
30 Hz they agree. They diverge only when the user pauses, or at a game speed other than the locked 30 Hz (the cinematic
rate lock hides this inside a cinematic). The port may keep sim-frame ramps for determinism of the *recorded* state, but
the visible fade should be driven by the render clock [M].

## 1. Fade and letterbox

### 1.1 Binding table

| Lua | Lua_Call | Parameters | Method |
|---|---|---|---|
| `Fade_Screen_Out(t)` | 0x542180 | one number (seconds). Not a number: script error, then **0.5** is used. Zero parameters: error, nothing done [H] | 0x44b300 |
| `Fade_Screen_In(t)` | 0x542080 | same; default **0.5** on a bad type [H] | 0x44b2c0 |
| `Fade_On()` | 0x542060 | none | 0x44cad0: fade quad = black, alpha 255, shown, state "at black" |
| `Fade_Off()` | 0x542040 | none | 0x44ca50: fade quad = black, alpha 0 (the quad stays in the scene but is invisible), state "at clear" |
| `Letter_Box_In(t)` | 0x542e20 | one number; default 0.5 on a bad type | 0x44b550 |
| `Letter_Box_Out(t)` | 0x542f60 | same | 0x44b6f0 |
| `Letter_Box_On()` / `Letter_Box_Off()` | 0x542f40 / 0x542f20 | none | 0x44cc20 (instant full bars) / 0x44cb60 (instant none) |
| `Lock_Controls(v)` | 0x543060 | number; `v > 0` sets, else clears. Bad type: error and clears | sets byte 0x140a28c91 (the "controls locked / cinematic" flag, see §2.7) |

Only game-mode scripts may call them; nothing here checks the mode type except where noted in §3.

### 1.2 Fade model [H]

- **Colour: black only.** There is no colour parameter anywhere. The fade quad covers the whole screen (normalized rect
  0,0,1,1), colour RGB 0, alpha animated.
- **Curve: linear** in alpha over the duration (alpha = remaining / total, mirrored for the other direction). No easing.
- **Fade_Screen_Out(t)**: the quad is reset to alpha 0, shown, then alpha rises 0 → 1 over t seconds. It always starts
  from **clear**, whatever the screen showed before. At the end the quad **stays at alpha 1** (full black) with state
  "at black"; nothing removes it. `t = 0` (26 uses in shipped scripts) is black from the next overlay update.
- **Fade_Screen_In(t)**: first does Fade_On (**snaps to full black**), then alpha falls 1 → 0 over t seconds. It always
  starts from **black**, whatever the screen showed before (so a `Fade_Screen_In` with no earlier fade-out blinks to black
  and fades up; scripts always pair them, 151 in-calls with 1 s / 88 with 2 s / 34 with 0.5 s). At the end the quad is
  **removed** from the scene (state "at clear").
- A new fade call replaces the running one (no queue); a fade-out right after a fade-in restarts from clear, not from the
  current alpha.
- Return value: none. Scripts wait with `Sleep(t)`.
- Typical shapes (counts from the shipped FoC + EaW scripts): `Fade_Screen_Out(1)` 90, `(.5)` 35, `(0)` 26, `(2)` 21;
  `Fade_Screen_In(1)` 151, `(2)` 88, `(.5)` 34, `(5)` 2. `Fade_On` / `Fade_Off` only appear in mod-era scripts (5 hits).

### 1.3 What the fade covers [M]

The fade quad and the bars are 2D quads added to the 2D GUI scene that the overlay was initialised with (init 0x44ce30
at startup; scene global 0x140a619f0). Each fade call re-adds the quad, so it sits **on top of everything added before
it: the HUD, and the letterbox bars if they were added earlier**. In-game Bink movies are rendered by their own player and
their order against the fade was not traced [L]. The mouse cursor is a separate layer above the 2D scene [L].

### 1.4 Letterbox model [H]

- Two black bars (top rect 0,0,1,H; bottom rect 0,1-H,1,H), H = `Camera_FX_Manager_Letterbox_Height` (screen fraction).
- `Letter_Box_In(t)`: bar height grows linearly 0 → H over t seconds. `t = 0` gives full bars on the next update.
  Calling it while the bars are already in, or while they are animating **in**, does nothing. Calling it while they are
  animating **out** reverses them, with t scaled by the fraction currently showing.
- `Letter_Box_Out(t)`: shrinks H → 0 over t. It does nothing when there are no bars. On the way out from a settled state
  t is used as given; mid-animation it is scaled by the fraction showing. At the end the bars are removed.
- **Side effect: the first bar-in hides the HUD** (HUD hide flag 0x140b2a5f9 = 1, mode 2 in 0x140b2a5fb, HUD object
  0x140b27f60 notified; the selection/hover flag 0x140b28d63 and the second flag 0x140b28d60 are cleared; the HUD-hidden
  counter 0x140b2c198 is incremented) and the end of the bar-out restores exactly what it changed (0x44afe0 with 0). So
  `Letter_Box_In` without `Start_Cinematic_Camera` still hides the HUD / minimap. [H code, M for what 0x140b28d60 /
  0x140b28d63 are]
- Bars persist after the script ends. They are removed by `Letter_Box_Out` / `Letter_Box_Off`, by `End_Cinematic_Mode`
  (0x2cbb80: instant removal if mode +0x98 is 0, else a 0.5 s bar-out), or when the mode is detached (§1.5).
  `End_Cinematic_Camera` does **not** touch the letterbox.

### 1.5 Across mode changes, and mode start [H unless tagged]

- **Attach (0x44b500, called when a mode starts):** stores the mode pointer; if the cinematic start flag (+0x01) is clear,
  records the current HUD hide state so it can be restored.
- **Detach (0x44b130, called when the mode ends, 0x35cfc0):** removes all three quads from the scene, restores the HUD
  hide flags the overlay itself changed, and clears the fade state, letterbox state, scroll, rotation and follow. **A
  fade or bars left on at the end of a tactical battle do not carry into the next mode (galactic or the next battle).** The
  fade quad is gone, the screen is visible.
- **No implicit fade-in at mode start.** The overlay itself never fades in a new mode. Exceptions:
  1. When a mode starts with the "start in cinematic" flags (globals 0x140b15410 / 0x140b15414; a map or story that
     opens with a cinematic) the overlay gets its start flags (+0x0e, +0x0f). Then, once the mode is no longer in its
     pre-start hold (mode vf +0x48 false), the overlay does Fade_On (**full black**) and, one step later, instant
     letterbox-on. The screen stays black until the script calls `Fade_Screen_In`, so such intros need no
     `Fade_Screen_Out(0)` first. [H code, M exact condition]
  2. Loading a save restores the saved fade (the overlay's save writer lists 32 fields: both quads' flags, directions,
     the three timers, rotation, scroll) and, if the saved +0x83 byte is set, fades in from black over **1.0 s** [H].
  3. For non-Lua cinematic shots (controller state != 4, attract / TSW firing shot) the controller update 0x2cd870 does a
     **0.5 s Fade_Screen_In** when the screen is currently "at black" (overlay +0x0c = 0) and the benchmark flag is off.
     (TSW §11 attributes the 0.5 s slide in 0x44b2c0 to the letterbox; 0x44b2c0 is the **fade-in**, the letterbox-in is
     0x44b550. The letterbox for that shot comes from 0x2cd870's caller path, not from 0x44b2c0.) [M, flagged as a
     correction to TSW §11 step 3]
- The overlay's `Start_Cinematic_Camera` interaction: see §2.1.

## 2. The cinematic camera

### 2.1 Start / end

| Lua | Lua_Call | Behaviour |
|---|---|---|
| `Start_Cinematic_Camera([bool])` | 0x544a10 → 0x44cd80 → 0x2ccd80 | Optional boolean (default **true**; a non-boolean is an error and nothing happens). Needs a current mode with a camera object. **Copies the RTS camera's current pose into the keys**: camera eye = the RTS camera's world eye (camera +0xd4 / +0xe4 / +0xf4), target = its focus point (+0x18 / +0x1c / +0x20); a hold key for each at clock 0; the key tracks are cleared first; shot clock := 0; shot timer := effectively unlimited (100000 / FPS). Then enters the cinematic presentation state (0x49b1a0(controller, 0), below), sets +0x34b = +0x34a = 1 and **state +0x37c = 4** ("Lua cinematic", BKC1 Q1). The boolean, when true, additionally calls 0x28e800(GMM, 1) (a flag on the game mode manager, role [L]; the false form skips it). If the mode has no RTS camera (e.g. a mode with a scene camera only) the start still works from the scene camera pose (+0xc..+0x14) [M]. [H] |
| `End_Cinematic_Camera()` | 0x542000 → 0x44b120 → 0x2cbc90 | Sets state 0, active 0, clears the keys (0x49b670), runs 0x49b1a0(controller, 1) (restores the presentation state) and sets the "restore the RTS camera" request (0x140b27ee0 = 1, 0x140b27ee4 = 5). It does not touch the letterbox or the fade. [H] |
| `Start_Cinematic_Mode()` | 0x544ac0 → 0x2cca80 | The attract / auto-shot controller (picks a combat or list shot). Not a script camera. |
| `End_Cinematic_Mode()` | 0x542020 → 0x2cbb80 | Ends any controller state, **removes the letterbox** (instant when mode +0x98 is 0, else a 0.5 s bar-out), resets +0x34a / +0x348 / +0x37c and the shot data. [H] |

**Cinematic presentation state, 0x49b1a0(controller, 0)** (also documented in MAIN_MENU_BACKGROUND §3 and TSW §11):
projection of the mode camera set to near 1.0 / far 10000 (the original is saved and restored at the end); HUD hidden
(0x140b2a5f9, hide counter 0x140b2c198 += 1, flag 0x140b28d63 cleared); FOW render option 0x13ecd0 cleared (remembered
in +0x311); a second render switch (0x48a680 / 0x48a3a0, +0x312); the mouse cursor hidden unless the mode is paused or
benchmarking; global 0x140b0a357 := 1; and for every object of the local player a "select behaviour" call (0x3912c0)
that **hides its selection circle** [M, the enter / exit pair is 0x3912c0 / 0x3a6b30]. The `(…, 1)` call undoes each
item it recorded. The port's current "cinematic mode" flag should therefore hide: HUD (including the minimap and the
command bar), selection circles, the cursor, and the FOW overlay. The in-world health bars and unit models are untouched
[M].

Input while the Lua cinematic runs (state 4): keys other than Esc are not consumed by the controller (it does not set the
+0x348 "consume everything" byte for state 4) [M]; Esc → `Story_Handle_Esc` on every active plot, no pause menu
(BKC1 Q1). Mouse-button input is **not** swallowed by the controller in state 4 (TSW §11's "any button ends the shot" is
for the auto-shot states 2 / 3). Scripts therefore call `Lock_Controls(1)` first: that byte (0x140a28c91) blocks
orders and selection and hides selection circles (UI.md §3). [M]

### 2.2 The camera model

The cinematic camera is an **eye position, a look-at target point and a roll angle**, each driven by its own key track,
plus a field of view. There is no camera-attached-to-object matrix; "attached" means the keyed point follows an object.

Evaluation per camera update (0x49cd00(controller, frame, outMatrix, outFov)):

1. target point = target track at `frame` (0x49de50); eye point = camera track at `frame` (0x49c540).
2. roll (degrees) = roll track at `frame` (separate array at controller +0x50, five dwords per key); no Lua call writes
   it, so scripts see roll 0 plus shake roll [M].
3. field of view = 0x49bdf0(frame): no Lua call writes it; the mode camera's FOV applies [M].
4. Land modes only: when controller +0x315 is set the eye is raised so it stays at least 15 above the highest of the four
   terrain heights sampled ±15 around it (+15). Start_Cinematic_Camera clears +0x315, so Lua shots have **no terrain
   clamp** [H].
5. Optional smoothing blend with the previous frame (+0x270; 0 after Start) and shake offsets (TSW §11.1) are applied;
   the view matrix is a look-at(eye, target, roll). The shake and the eye-to-target direction are then re-stored.

Coordinate frame: world units; the key's own position is a **world position**, optionally **plus an offset in the frame of
an attached object** (§2.4). The camera never looks "through" the object; the target track is evaluated the same way as
the camera track.

### 2.3 Key tracks and interpolation

Each key is 0x54 bytes: position xyz (+0x00), the offset xyz used when attached (+0x18), frame (+0x24), interpolation type
(+0x28), attached object id (+0x2c, −1 none), two ease values (+0x44 / +0x48), a flag "offset is not rotated by the
object" (+0x51) and a flag "cinematic animation" (+0x52). Keys are inserted sorted by frame (a key at an equal frame goes
after the existing ones, so the latest Set at one frame wins). The segment of frame f is the last key with key.frame ≤ f.

| Interpolation type of the **earlier** key of a segment | Result between it and the next key |
|---|---|
| 2 (**hold**, written by every `Set_*_Key` and by the instant form of `Transition_*_Key`) | the earlier key's position, unchanged, until the next key's frame: a **cut** |
| 1 (**blend**, written by `Transition_*_Key` with t > 1/FPS, by Transition_To_Tactical_Camera, by Cinematic_Zoom) | position blended from the earlier key to the next with the ease function below |
| 0, 3 (other blend kinds, not written by any script binding) | 0x49aa40 / 0x49a610 [L] |

After the last key the camera holds its position. Ease function 0x49bb40 ("Accel"; TSW §11): with in-ease a and out-ease
b, e = 1 − b, v = 2 / (2 − a − b): below a it is v σ² / (2a), between a and e linear, after e it eases out. The keys the
script bindings write carry a = b = **0.5** [M for which field holds which], which makes it a plain symmetric
ease-in-out: 2σ² for σ < 0.5 and 1 − 2(1 − σ)² after. `Transition_To_Tactical_Camera` writes 0.25 / 0.25 [M].
The same blend parameter is applied to x, y and z of the eye (a straight line in world space, not an arc) [M].

### 2.4 `Set_Cinematic_Camera_Key` / `Set_Cinematic_Target_Key`

Lua_Call 0x543be0 / 0x5442e0 → controller 0x4ab160 / 0x4ab350 (camera track / target track; the two bodies are identical
except which track and which "last position" copy they update). **Need at least 8 arguments** (else error and nothing);
the retail error strings mention only the first two, but every argument is parsed.

| # | Name (script comment) | Parsed as | Meaning |
|---|---|---|---|
| 1 | `target_pos` | position of some sort: a vector, a `GameObjectWrapper` (its current position), a marker / hint object; failure → "Invalid parameter 1, expected a position." and **return** | base point, resolved **at call time**. If an attach object (6) is given the stored base is only used as a fallback |
| 2 | `xoffset_dist` | number (error and 0 on a bad type) | x offset, or the **distance** when (5) is set |
| 3 | `yoffset_pitch` | number | y offset, or **pitch in degrees** when (5) is set |
| 4 | `zoffset_yaw` | number | z offset, or **yaw in degrees** when (5) is set |
| 5 | `angles?` / `euler` | number, **true when > 0** | 0: (2,3,4) is a plain xyz offset. 1: (2,3,4) is spherical: a vector of length `dist` along −y is rotated by `−pitch` about x, then by `yaw` about z (positive pitch puts the point above the base point), and the result is the offset [H code, M for the handedness / sign of yaw] |
| 6 | `attach_object` | a GameObjectWrapper; **a number (the 1 or 0 the scripts pass) is silently ignored = no object** | the key follows this object every frame |
| 7 | `use_object_rotation` | number > 0 | with an attached object: 1 = the offset is expressed in the object's rotating frame (the camera turns with the unit; the frame is the object's transform turned a quarter circle [M]); 0 = the offset is added in world axes. Stored inverted as "+0x51 = not rotated" |
| 8 | `cinematic_animation` | number > 0 | with an attached object: sample the object's animated **muzzle bone** (`MuzzleA_00`, else `MuzzleA_01`, else its position) instead of its origin, while the object plays a cinematic animation (animation type 0x1b) or the key flag is set. Used with `Play_Animation("cinematic", …)` units. Ignored without an object [H code, M use] |

Details [H unless tagged]:
- The stored world position is `base + offset`; the key is written at the **current shot clock** with type hold.
  The "last key position" (+0xb4 camera, +0xa8 target) is updated for later Transition keys and for Scroll.
- Attached key evaluation (0x49d640): if the object cannot be found, the last good position is used. For an object with
  a render model the offset is applied through the model's world transform (object-local when rotated, else added to the
  transform's translation); for a model-less object it is position + offset. **The model case adds the type's
  `Ranged_Target_Z_Adjust` (type +0x4dc, default 6.0) to z**, as TSW §11 found for the TSW keys.
- A second `Set_*_Key` replaces the future of that track from the current clock on (hold), so a script that sets a camera
  key and a target key in the same step defines both ends of an instant cut.
- If the mode is not tactical the Lua call is still accepted by the binding but the controller has no camera and does
  nothing [M]. The shipped scripts call these only in tactical modes.

### 2.5 `Transition_Cinematic_Camera_Key` / `Transition_Cinematic_Target_Key`

Lua_Call 0x544fc0 / 0x545630 → 0x4ab990 (camera) / 0x4abdf0 (target). **At least 9 arguments**: the same eight as above
with the **time in seconds inserted as argument 2** (`target_pos, time, dist, pitch, yaw, euler, attach, use_rot, anim`).
Error strings as for Set. Behaviour:

- If `time > 1/FPS`: two blend keys are written: a start key at the current clock holding the **last key position**
  (so the move starts from wherever the camera was last put or ended up), and an end key at
  `clock + int(FPS × time)` at base + offset (object-attached if given). Blend per §2.3. After the end key the camera
  holds. A later Set / Transition issued at a clock inside the move starts from the old "last position", not from the
  mid-move eye [M]: the shipped scripts do `Sleep(t)` between them.
- If `time ≤ 1/FPS` (the shipped `…_Target_Key(obj, 0, 0, 0, 7, 0, obj, 0, 0)` instants): behaves like `Set_*_Key` (a hold key at the clock).
- The time is **game time**: frames of the mode clock, so a pause stops the move [M].
- Typical shapes: `Transition_Cinematic_Camera_Key(focus_unit, 0, 50, 25, 45, 1, 1, 1, 0)` (instant: 50 units out,
  25° up, 45° yaw, euler on, attached to the object, rotating frame), then `…(focus_unit, 5, 50, 25, 135, 1, 1, 1, 0)`
  (5 s orbit). Target key: `…_Target_Key(focus_unit, 0, 0, 0, 7, 0, focus_unit, 0, 0)`: after `time` come offset
  x = 0, y = 0, z = 7, euler off, attached, not rotated, no cinematic animation: look at a point 7 units above the unit.

### 2.6 `Transition_To_Tactical_Camera(t)`

Lua_Call 0x545ca0 → 0x4ac1d0. One number (seconds); a bad type is an error and **1.0** is used. Zero args: error.

- Reads the RTS camera: **snaps its zoom fraction to 1.0** (0x2d0600: the camera zoom `[0..1]` maps linearly between the
  camera settings' two distance values), refreshes the RTS camera, and takes its resulting eye and focus point.
- Clears the key tracks, resets the shot clock to 0, writes a start key at the *last cinematic eye / target* and an end
  key at `int(FPS × t)` (at least 1 frame after 0) at the **RTS camera's own eye and focus**, blend type, ease 0.25 / 0.25.
- It does **not** end the cinematic. The controller stays in state 4 (HUD hidden, Esc → `Story_Handle_Esc`) and holds the
  final pose. Scripts finish with `Sleep(t)`, then `Letter_Box_Out`, `Lock_Controls(0)`, `End_Cinematic_Camera()`
  (shipped pattern: `Transition_To_Tactical_Camera(6); Sleep(4); Letter_Box_Out(2); Sleep(2); Lock_Controls(0);
  End_Cinematic_Camera()`). The blend target is wherever the RTS camera was **when the script pointed it** (a
  `Point_Camera_At(hero)` before `Start_Cinematic_Camera` or before the transition decides where the player's camera
  lands); the cinematic is not blended back to the player's pre-cinematic camera automatically. [H code, M reading]
- 4 + 2 + … shipped t values: 2 (27), 6 (24), 1 (24), 0 (11), 5 (6), 4 (2).
- After `End_Cinematic_Camera` the controller requests the RTS camera restore (0x140b27ee0 / ee4) so the player camera
  resumes from its own pose; this is the mode update's per-frame check, not a timed blend [M].

### 2.7 What stays on screen and what does not

| Item | In the Lua cinematic camera (state 4) |
|---|---|
| Command HUD, minimap, bottom bar, hero icons | hidden (HUD hide flag set by 0x49b1a0) |
| Selection circles, ground blobs | hidden: the select behaviour is told to hide (0x3912c0) and the global cinematic byte 0x140a28c91 (Lock_Controls) hides them while set |
| Mouse cursor | hidden by 0x49b1a0 unless the mode is paused / benchmark |
| FOW overlay | render option cleared, so no fog-of-war draw |
| Letterbox | independent: only if the script calls `Letter_Box_In` (or the mode started with the cinematic flag, §1.5) |
| Fade | independent of the camera |
| Esc | `Story_Handle_Esc` on all active plots; no pause menu |
| Tab / other keys | not consumed [M] |
| Sim | the world keeps simulating; the camera is a pure view |

## 3. Script camera moves (RTS camera, tactical only)

All five call sites check "current mode is tactical" first (error string "Command only valid in a tactical game.", also
used for land-only wording by `Enable_Fog`); in the galactic view they do nothing but error. They move the **RTS camera
object** (mode vf +0x1e8: focus xy, yaw, distance), not the cinematic eye.

| Lua | Lua_Call | Parameters | Effect |
|---|---|---|---|
| `Point_Camera_At(pos)` | 0x5432d0 → 0x44bac0 | one position (object, marker or vector); invalid = error "Invalid position." | **Instant**: sets the RTS camera focus xy to the position (z ignored) and recomputes the camera. Not blocked by anything. Typical use: before `Start_Cinematic_Camera` / `Transition_To_Tactical_Camera` (§2.6) |
| `Scroll_Camera_To(pos [, obj])` | 0x543a60 → 0x44c770 | position, optional GameObjectWrapper (when given, the target tracks that object's position each update and, on arrival, the camera starts **following** it) | **Timed by frames, not seconds.** Each overlay update moves the focus 10 world units along the 3D direction to the target (xy part applied), until the 3D distance is below 10, then snaps to the target. So the speed is 10 units per *rendered frame* (frame-rate dependent: 600 units/s at 60 fps). In the Lua cinematic (state 4) the same call turns into a cinematic move (0x4aaae0, speed 10, duration = distance / 10 frames) that shifts the camera and target keys by the same vector. The player's own scroll input is not blocked while the RTS camera scrolls; input handlers that set the focus win on the frame they run [L]. Shipped: `Scroll_Camera_To(unit)` 8 |
| `Camera_To_Follow(obj [, bool])` | 0x541170 → 0x44aef0 | GameObjectWrapper (nil / dead = stop following); the optional boolean is parsed but **unused** [M] | Stores the object id in the mode (+0x1f8). Each mode update re-centres the camera focus xy on that object while the id is set and the object is in the local player's ordered list (0x1402ad080); when the object is gone the follow id is reset to −1. 0 shipped uses [M]. (The retail error strings in this function name `LuaScrollCameraTo` by copy-paste) |
| `Zoom_Camera(f, snap)` | 0x545eb0 → 0x44d120 → 0x2d0600 | `f` number clamped to [0, 1]; `snap` number, true when > 0 (a bad second argument is an error and snap = false) | Sets the camera's **target zoom distance** to the linear interpolation between the camera settings' two distance values by `f` (0 = first value, 1 = second). With `snap` true the current distance is set immediately; false: the camera's normal smoothing eases to it. Shipped: `Zoom_Camera(1.0, 1)` and `(0.7, 0)` [H code, M for which end is near] |
| `Rotate_Camera_By(a, t)` | 0x5435a0 → 0x44bfc0 | two numbers: delta yaw in **degrees**, duration in **seconds** | Starts a yaw rotation: yaw := start + a × (elapsed / t), linear in wall seconds, ends at start + a exactly, wrapped into [0, 360). Shipped: `Rotate_Camera_By(180, 30)` |
| `Rotate_Camera_To(a, t [, dir])` | 0x543790 → 0x44c020 | absolute yaw in degrees (clamped to ±360 and brought into [0, 360)), duration in seconds, optional boolean `dir` (default true) | Same rotation toward an absolute yaw; `dir` picks which way round when the difference is not exactly 180° (the delta is taken the long way when the chosen direction disagrees with the shorter path) [M]. 0 shipped uses |
| `Cinematic_Zoom(t, scale)` | 0x5412e0 → controller 0x49abe0 | two numbers; **only valid in state 4**, else error "only when the cinematic manager is in story mode" | Rebuilds both key tracks (camera, target) from the last pose: the camera moves along its line to the target to `scale` × the current distance over `t` seconds (blend type). `scale` < 1 zooms in (`(10, 0.9)`, `(8, 0.95)`), > 1 out (`(8, 1.3)`) [M, by name and values] |

Timing facts [H]: yaw rotation and scroll advance with the overlay update's wall delta (or per frame for scroll), never with
the 30 Hz tick. The RTS camera's yaw is camera +0x14 (degrees). All four RTS-camera moves can be interrupted by player
input in the sense that the player's camera controls still write the same fields; nothing locks them except
`Lock_Controls` / the cinematic state [L].

## 4. Fog, lighting environment, lightning

### 4.1 `Enable_Fog(bool)`, `Enable_Distance_Fog(bool)`, `Set_Cinematic_Environment(bool)`, `Set_New_Environment(int)`

| Lua | Lua_Call | Behaviour |
|---|---|---|
| `Enable_Fog(bool)` | 0x541f10 | tactical only. Sets the scene's **master fog switch** (scene +0xbd) and re-applies the environment (0x277830). Off: the scene's fog flag (+0x74) is forced 0 = **no fog at all** (distance fog and haze). On: fog is whatever the active environment / scene override says (next row). Shipped: `Enable_Fog(true)` 4, `(false)` 3 |
| `Enable_Distance_Fog(bool)` | 0x6aab90 | needs a scene of the class at 0x140a16d20 (the tactical RTS scene), else error "not supported in the current game mode". Sets the **active environment's** "distance fog enabled" byte (environment record +0xf8, record stride 0x198) and takes effect when the environment is next applied (a scene update). One parameter exactly. Shipped: `(true)` 2, `(false)` 2. [H code, M timing] |
| `Set_Cinematic_Environment(bool)` | 0x5441a0 | tactical only, needs the RTS scene. `true`: find the scene's environment named **"Cinematic"** (the map's environment list, 0x198-byte records, name compare), remember the current index in +0xa4 and switch to it (apply 0x1277830: sun / ambient light colours and directions, fog colour / near / far, shadow colour). `false`: restore the remembered index. Does nothing when the map has no such environment or it is already active. Shipped: `(false)` 8, `(true)` 4 |
| `Set_New_Environment(n)` | 0x5448a0 | tactical only. One integer: calls the game mode's virtual (vf +0xc8, [L] sets the active environment index), the numeric twin of the above (0 shipped uses) |

What an environment changes on screen: the record holds lighting (several light colours and directions applied to the
RTS scene's light set), fog flags / colour / near and far, and the scene's shadow tint. Switching is **instant** (no
blend) except for the time-of-day blend field (+0xfc / +0xe4) used by environments that animate. [M]

### 4.2 `Play_Lightning_Effect(name, from, to)`

Lua_Call 0x6a5fc0. Exactly 3 parameters.

| # | Meaning |
|---|---|
| 1 | string: the effect name in `LIGHTNINGEFFECTTYPES.XML` (`Hack_Electicity_Zaps`, `Kessel_Mouse_Droid_Hack`) |
| 2 | "position of some sort": a vector, an object, a bone / marker wrapper. Passed as the **end** point of the effect |
| 3 | same; the **start** point |

- Creates the effect in the lightning manager (0x140a2a850, 0x481ce0, keyed by the upper-cased name; the effect type
  provides texture, width min / max, colour start / end, `Detail`, `Displace`, `Number_Bolts` (23), the
  `Bolt_Creation_Interval_Min/Max` (0.0005 / 0.1 s) and `Fadeout_Time_Min/Max` (0.15 / 0.6 s)); adds it to the tactical
  scene; returns a `LightningEffectBlockStatus` (type 0x829). A failed lookup logs "hmm, somehow we failed to create
  effect %s" and returns nothing.
- What is drawn: a **jagged beam of textured, additive-looking polyline bolts** between the two points that re-rolls
  while bolts are created, each bolt fading from `Color_Start` to `Color_End` over its own fade-out time.
- **Blocking:** `BlockOnCommand(Play_Lightning_Effect(...))` waits until the effect object reports finished
  (IsFinished 0x6a6210 = effect missing **or** its "dead" byte +0x166 set, i.e. all bolts created and faded). Roughly 1–3 s
  for the shipped types [M]. The wait is in script time (the script thread polls the block each service).
- The two points are read once at the call (a moving bone is not tracked) [M]. Used only in the Kessel / Felucia R2 hack scenes.

## 5. Object, planet highlights, text

### 5.1 `GameObjectWrapper.Highlight(on [, zoffset])` / `Highlight_Small(on [, zoffset])`

Lua_Call 0x57a6f0 / 0x57a860. 1 or 2 parameters: boolean, optional number (default 0). Receiver must be a live object
(dead = silent no-op).

- `true`: spawns a **special-effect game object** of type `Object_Highlight` (model `W_HILIGHT_FX.ALO`) or
  `Object_Highlight_Small` (`W_HILIGHT_FX_SMALL.ALO`) from `SPECIALEFFECTS.XML` (decoration, idle, scale 1.0), owned by
  the local player, at the target's position with an offset of (0, 0, `zoffset`); if the target has a render model the
  effect is **attached** to it with that offset, so it follows the unit. The model is a pulsing ring / marker effect
  (the effect object plays its idle animation) [M for the look]. The handle is stored in the target's helper at
  +0x1a8 (+0x30). Calling `true` twice creates a second effect and loses the first handle [L].
- `false`: destroys the effect (0x391310) and clears the handle.
- The z offset is in world units (shipped `Highlight(true, 250)` for starbases, `Highlight_Small(true, -100)` for turret
  flags: tall objects need a raised or lowered marker).
- It is a world object, not a screen overlay: it is drawn in the 3D scene, hidden by FOW like any object of that
  visibility, and is not removed by `Remove_All_Text`. Shipped usage: 24 × `cargo_being_scanned.Highlight(false)`,
  `unit.Highlight(true)`, `Highlight(true, 250)`.

### 5.2 `Add_Planet_Highlight(planet, tag)` / `Remove_Planet_Highlight(tag)`

Lua_Call 0x6a6940 / 0x6a8ae0. Add: exactly 2 parameters, a planet GameObjectWrapper (dead object = crash-guard error
not checked [L]) and a **string tag** (`"0"` in the only shipped use). Remove: exactly 1 string.

- Add registers a **hint highlight** on the galactic HUD: HUD 0x140b27f60 method 0x2f55a0(planet type name, kind 3,
  slot −1, tag, 1, 0). Kind 3 selects the HUD's prebuilt planet-highlight element (HUD sub-object at +0xde0
  record +0x26 × 8), plays its start animation (0.5, flag 3) and registers it in the HUD's hint list under the tag
  (0x2fd210). Ten tagged hint slots exist (stride 0x68 from HUD +0x2288). The highlight is drawn by the HUD at the
  planet's screen position as a pulsing ring around the planet [M for the look; [L] for the animation name]. The tag
  string is only the lookup key.
- Remove clears the slot whose tag compares equal (case-sensitive byte compare) and kills its element (0x264c80); an
  empty tag clears the first active slot.
- Galactic only in practice (the planet type name resolves against the galactic object list); the shipped script
  highlights the planet that holds the tutorial objective.

### 5.3 `Game_Message(textkey)` and `Remove_All_Text()`

| Lua | Lua_Call | Behaviour |
|---|---|---|
| `Game_Message(key)` | 0x6a33e0 | exactly one string, otherwise "invalid number of parameters. Expected 1, got %d." Needs the HUD text method (global 0x140b31410), else "no method for text display found". Looks `key` up in the localisation table (`MASTERTEXTFILE` entries such as `TEXT_STORY_REBEL_ACT03_MISSION_EIGHT_13`; an unknown key shows the key text itself [M]). Pushes the text onto the HUD's **per-mode hint queue** (0x1402dd630(HUD, text, colour white 255,255,255,255, front = 1)) **at the front** and resets the hint timer (Q6_T27 §3). Uses the player's hint font (+0x2f68, not the HUD's default). Only effective when the HUD's mode kind (+0x211c) is nonzero (a tactical or galactic mode). **No sound, no icon.** |
| `Remove_All_Text()` | 0x543550 → 0x2fefc0 | Destroys every queued **and** currently shown hint text (all entries of the HUD text list at +0x1170, each with its font object) |

Display (shared with advisor hints, Q6_T27 §3): the GUI update (0x14030c3b0) shows the next queued text when none is up,
no Bink is open and the three GUI flags (+0x26a4 set, +0x2a34 and +0x2908 clear) allow it; the text stays for
(characters) × `Advisor_Hint_Duration` (0.1 s in FoC `GAMECONSTANTS.XML`) and fades out in eight alpha steps; **while it
is up the `Light_Loop` movie plays and can hold the galactic end-screen gate**. Placement and channel: the advisor hint
box of the HUD, not the tutorial `SCREEN_TEXT` channel; there is no position, duration or colour argument. Shipped uses
carry fixed `TEXT_…` ids only (LUA_SCRIPT_OUTPUT.md). Queue order: `Game_Message` pushes at the **front**, the engine's own
"planet changed hands" text at the back, so a script message pre-empts it [M].

## 6. Defaults and edge cases the port should copy [H unless tagged]

| Case | Retail |
|---|---|
| `Fade_Screen_In/Out` with a non-number | script error, then 0.5 s |
| `Letter_Box_In/Out` with a non-number | script error, then 0.5 s |
| `Transition_To_Tactical_Camera` with a non-number | script error, then 1.0 s |
| `Fade_Screen_In/Out(0)` | takes effect on the next overlay update; in-then-nothing for out |
| second fade call during a fade | replaces it |
| `Start_Cinematic_Camera("x")` | error, nothing started |
| `Start_Cinematic_Camera(false)` | camera starts; only the GMM flag call is skipped |
| camera key / target key with a non-position first argument | error and **return** (no key) |
| camera key with a number for the attach argument | treated as "no object" (silent) |
| `Set_Cinematic_*_Key` with < 8 arguments, `Transition_*` with < 9 | error "Expected 8 / 9 parameters" |
| `Cinematic_Zoom` outside state 4 | error, no effect |
| `Point_Camera_At`, `Scroll_Camera_To`, `Rotate_*`, `Zoom_Camera`, `Enable_Fog`, `Set_*Environment` outside a tactical mode | error, no effect |

## 7. Answers to the five questions in one table

| Question | Answer |
|---|---|
| Fade colour / curve | black, no parameter; linear alpha |
| Fade over HUD / bars | over HUD and over bars added before it; movies unknown [L] |
| Fade persists across mode change | no: detach clears quads and state; a left-black fade ends with the mode |
| Fade_Screen_Out at full black after the script | stays black until a fade-in / `Fade_Off`, the mode ends, or a save load |
| Implicit fade at mode start | none, except "start in cinematic" maps (black until the script fades in) and the 0.5 s fade-in of auto shots and the 1 s fade-in of a loaded saved fade |
| Duration clock | fades / letterbox / rotation: render wall seconds; camera keys: mode frames |
| Camera model | eye + look-at target + roll tracks, world units; keys may follow an object with an offset in its frame |
| Start pose | the RTS camera's current eye and focus |
| End blending | `Transition_To_Tactical_Camera` blends to the RTS camera pose; `End_Cinematic_Camera` is an instant restore request |

## 8. Open items

1. Exact meaning of the two ease fields in each key (which of a / b is in / out) and of interpolation kinds 0 and 3: [L].
2. Whether `Zoom_Camera(1.0)` is the near or far end of the camera range, and the exact meaning of `Rotate_Camera_To`'s
   direction flag: a retail probe (`Zoom_Camera`, `Rotate_Camera_To` with both flag values, screenshot) would settle both.
3. Z order of a running Bink against the fade quad.
4. The "Cinematic" environment record format (which map files carry it; FoC maps set it for the intro shots).
5. Highlight planet ring look (animation name inside the HUD element) and the lifetime of a duplicate `Highlight(true)`.

## 9. Port differences to fix (`engine/lua/src/bind_presentation.cpp`, `presentation.hpp`)

1. `Fade_Screen_In(t)` must start from **full black** (not from the current level) and end with the quad gone;
   `Fade_Screen_Out(t)` must start from **clear**. The port's `ramp_to` ramps from the current level; the retail
   behaviour only coincides when scripts pair them.
2. Fade and letterbox durations are render-clock seconds in retail, not sim frames; letterbox in / out scale their
   duration by the fraction currently shown and are ignored when redundant; letterbox is frozen while a GUI modal has
   focus.
3. `Letter_Box_In` hides the HUD even without a cinematic camera; `Start_Cinematic_Camera` hides HUD, selection circles,
   cursor and FOW render. `End_Cinematic_Camera` does not touch the letterbox; `End_Cinematic_Mode` does.
4. Camera keys are stamped in sim frames of the mode clock (`int(30 × seconds)`), hold vs blend types, 0.5 / 0.5 ease,
   attach-by-object with object-frame offsets, euler spherical offsets (dist / pitch / yaw), `Ranged_Target_Z_Adjust`
   on model objects, no terrain clamp. `Start_Cinematic_Camera` must key the **current RTS eye and focus** at clock 0.
5. `Transition_To_Tactical_Camera` blends to the RTS camera pose with zoom fraction snapped to 1.0, ease 0.25 / 0.25,
   and leaves the cinematic active.
6. Mode end clears the fade, letterbox, scroll, rotation and follow; the new mode starts with none of them unless it
   starts "in cinematic" (black + bars until the script fades in).
7. `Highlight` / `Highlight_Small` spawn `Object_Highlight(_Small)` special-effect objects at (0, 0, z) from the object;
   `Add_Planet_Highlight` keys a HUD highlight by tag; `Game_Message` pushes to the front of the hint queue (white, hint
   font); `Remove_All_Text` clears queued and shown hints.
   **Correction (ui2, 2026-11-16):** the later targeted read in `STORY_PRESENTATION_REWARDS.md` (sections 1.1, 1.5, 6.3) governs: the
   list at +0x1170 is the **screen-text list** (the SCREEN_TEXT rewards' lines), not the hint queue, and +0x2f68 is the Urgent
   sound, not a font. `Game_Message` shows a text line and `Remove_All_Text` clears the screen-text lines; the port follows that.
   Read section 5.3 above with this correction.
