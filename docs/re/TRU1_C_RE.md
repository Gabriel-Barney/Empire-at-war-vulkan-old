# TRU1 t05 / t06 / t07: indigenous spawners, space retreat jump-out, breakoff debris (RE, 2026-11-15)

Read from the retail FoC x64 exe (v1.121) through REA (decompile, disassembly, byte search). Tags: [H] read in code,
[M] inferred with good confidence, [L] guess. No decompiled code is reproduced. FPS = 30 (0x140b0a340). "Synced
float" = `Get_Float(lo, hi)` on the synced LCG 0x140a13e24 (0x1ffbb0: one LCG step, `lo + (hi − lo) · ((s >> 10) &
0x7fff) / 32767`, lo / hi swapped when given reversed); "synced int" = 0x1ffb40 on the same LCG.

## 1. t05: SPAWN_INDIGENOUS_UNITS (behaviour 0x1b, `SpawnIndigenousUnitsBehaviorClass`, vtable 0x890d98)

Fields (GameObjectType): `Spawned_Indigenous_Units_Type` (+0xbf8, a type list; data +0xc00, count +0xc08),
`Spawned_Indigenous_Units_Delay_Seconds` (+0xbf0, float), `Spawned_Indigenous_Units_Quantity` (+0xc18, int),
`Spawn_Indigenous_Units_Chance` (+0xc1c, float), `Spawn_Indigenous_Units_Radius` (+0xc20, float),
`Spawn_Indigenous_Units_In_Packs` (+0xcd, bool), `Spawned_Indigenous_Pack_Type` (+0xc28 name, resolved type +0xc30),
`Max_Distance_From_Spawner` (+0x2134, int). Data pack (obj +0x120): the spawned-object list, the next spawn frame,
the activation state (−1 undecided, 0 off, 1 on).

### 1.1 Registration and attach [H]
- vf7 (attach): the object joins one **global spawner list** (0x140a43690, append order = attach order); vf15 removes
  it (story `DESTROY_ALL_INDIGENOUS_SPAWNERS` reads this list). vf9 (initialise): the spawned list is emptied and the
  next spawn frame := the current mode frame (so the first active service spawns). The state starts at −1.
- Service interval 30 (TACTICAL_GAPS §6.4; first due frame by the attach draw).
- vf16 (an object it listens to is removed): the object leaves the spawned list.

### 1.2 Service 0x531760 [H]
1. **Activation (state −1)**: decide for every spawner of this type at once (0x5310b0, keyed by the type's name CRC
   +0x11c): walk the global list; spawners of the same type still at −1 become 0 and form pool U (list order),
   those already at 1 form pool A. N = |A| + |U|. Chance c = this type's `Spawn_Indigenous_Units_Chance` clamped
   below at 0; **c > 1 means a percentage** (c · 0.01, clamped to [0, 1]). Target T = int(N · c). If T > |A|: repeat
   T − |A| times: k = synced int(0, |U| − 1), U[k] := 1, remove it from U. If T ≤ |A|: repeat |A| − T times the
   same on A, setting 0. (A spawner attached later re-balances its type the same way.) Shipped FoC XML: 52
   spawner entries at Chance `1.0f`, one at `0.9f`; 45 spawn in packs, 8 singly; Delay mostly 20 s; Quantity mostly 10.
2. Only state 1 and frame ≥ next spawn frame go on.
3. **Single units** (In_Packs false): Q = Quantity; if the spawned list holds ≥ Q objects nothing happens (and the next
   spawn frame is **not** advanced). Otherwise Q − count times: spawn one unit (§1.3) around the spawner; on success
   append it to the list, listen to its removal, and (when it is not a team member) give it a move to its own
   position (0x6218f0).
4. **Packs** (In_Packs true): only when the list is empty: spawn one unit around the spawner, append it, then Q − 1
   more, each around **the first unit's position with radius 50** (append each success); then make a team
   container of type `Spawned_Indigenous_Pack_Type` (resolved), else the first unit's type, over the list (0x2a5a60),
   set its +0x3a2 bit 0x10, and give the container a move to its own position.
5. next spawn frame := frame + int(FPS · Delay_Seconds). So Delay is the **refill interval**: dead units are replaced
   at the next due service after the delay (single units: any shortfall; packs: when the whole pack is gone).

### 1.3 Spawn one unit 0x531b60 [H]
Draws, in order: a = synced float(0, 1), b = synced float(0, 1) (facing (b, a, 0), not normalised); if the type list
is empty, stop (no more draws, no unit). Type index = int(synced float(0, n − 1)). The object is created for the
spawner's owner at (0, 0, 0) with that facing, +0x3a2 |= 0x10. Then up to 10 tries (0x140a436a0 = 10): radius R =
`Spawn_Indigenous_Units_Radius` (or 50 around a given point, packs); dy = synced float(−1, 1), dx = synced float(−1,
1); point = centre + (dx · R, dy · R, 0), plus the new type's `Hover_Offset` (+0x1b68) on z when > 0. Placement test
0x532140: clamp the point into the map rectangle (mode vf +0x1f8; a zero rectangle skips the clamp), snap to the
nearest passable cell of the land pathing grid (0x511520; none → abort), then test the object's footprint for
overlap (0x538200): free → teleport there and succeed; blocked → next try. Abort or 10 failed tries: the object is
destroyed (0x391310) and nothing is appended. On success the unit's INDIGENOUS_UNIT pack (if any) gets home := the
spawner's position and its leash := the spawner type's `Max_Distance_From_Spawner`.

### 1.4 INDIGENOUS_UNIT wander (behaviour 0x20, service 0x621d80) [H flow, M bounds]
Reads the home / leash written above: an idle indigenous unit (no target, idle animation, not moving) with
`Can_Indigenous_Unit_Stop` stays put 90 % of the time (synced float(0, 1) > 0.1), else picks a point: x = home.x +
synced float(−D, D), y = home.y + synced float(−√(D² − dx²) [M], +…), z = home.z when it has a home and D > 0;
otherwise a random point of the map rectangle (2 synced floats); the step length is clamped to a min / max pair
(globals 0x140a442c8.., another pair while burning) by one more synced float; then a group move there. Burning units
take `Burning_Damage_Per_Second` / FPS per service. **Not ported by TRU1** (separate behaviour; filed as a task).

### 1.5 Indigenous_Spawn_Destruction_Reward (GameConstants +0x11e34, vanilla 500) [H]
Read only by the **tactical sandbox objectives** builder (0x32bd10, strings `Sandbox_Objective_*`): for a spawner
type on the map it adds a story event `<name>_00` (`DESTROY_ALL_INDIGENOUS_SPAWNERS`, param 3 = the spawner type,
text `TEXT_TACTICAL_PLANETARY_CREDIT_BONUS`) and a follow-up `<name>_01` whose reward (type 4, credits) is this value.
It is not read by the spawner behaviour. The sandbox objectives belong to the story system (not ported by TRU1).

## 2. t06: space retreat jump-out (`SpaceRetreatCoordinatorClass`, vtable 0x890fe8; update vf5 0x5337c0)

Corrects BATTLE_TRANSITIONS §3.4 "States" (which said the flight goes toward `Space_Retreat_Off_Map_Dest_Pos`).

### 2.1 Start (vf14 0x533080) [H]
Allowed request → state 1, countdown := int(FPS · `Space_Retreat_Countdown_Seconds`); the faction's
`Space_Retreat_Off_Map_Dest_Pos` (+0x20c4..+0x20cc) is **copied into the coordinator (+0x70) and never read again**
(no reader in the coordinator's 16 slots or its base; the only other code touching faction +0x20c4 is the XML
parser). The retreatable list is built (vf8, below), every listed unit's locomotor gets the reverse of the
`ATTACKER ENTRY POSITION` marker's yaw as a heading (0x533220, vf +0x218), and the begin SFX plays.

vf8 (0x532c80): every object of the player with a locomotor (store query mode 1) that is not a team member, has a
playable locomotor state, is alive with hull, and passes the locomotor's "can retreat" (vf +0x210) is added (log
`Adding %s to retreatable units list`), in store order.

### 2.2 Countdown end (state 1 → 2) [H]
Units that died or lost their locomotor leave the list; vf12 (the attrition at the countdown end, battle host §4.9)
runs; state := 2. If **In_Game_Cinematics** (GameConstants 0x140b16dd2, vanilla True) and the current mode is space,
the retreat cinematic starts on the in-game cinematic camera (0x140b27a10): with listed units 0x2ccd00 → 0x4a5990
(camera shots around the player's star base, if any), then the formation step 0x534110; with none 0x2cc700. The
cinematic camera's frame counter (+0x154, 0x140b27b64) restarts at 0 and its direction (+0x198, 0x140b27ba8..) is
(−1, 0, 0) turned by the marker's pitch then yaw (0x49e060), i.e. **pointing opposite the marker's facing**.
Without the cinematic nothing moves: state 2 ends at its first update (2.4).

Formation 0x534110 [H]: the list is ordered by type `Tactical_Health` × the mode's health multiplier, largest first
(stable); each unit in that order is stopped (locomotor vf +0x200) and teleported (forced) to a free spot found
around the marker's position (radius 2500, 0x2a9670), oriented to marker yaw + 180°; then the list is re-ordered by
3D distance to the point 10000 units out along that yaw from the marker, **nearest first** (stable).

### 2.3 Jump-out step (state 2, 0x533ac0, once per update with the frames elapsed n) [H]
Done (returns true) when the cinematic is not active or its counter c > 89. Otherwise, with d = the direction:
- c > 19: the hyperspace stretch effect starts for the next n units of the list (cursor +0x64; presentation).
- c < 30: **every** listed unit moves 8.125 · d (non-forced teleport step) and nothing else happens.
- c ≥ 30: if the release cursor R (+0x60) equals the sound cursor (+0x6c), unit R plays `SFXEvent_Retreat_Engines_Jump`
  (its type's, else the faction's `SFXEvent_Exit_Into_Hyperspace`) at its position, unless obj +0x3a0 has bit 0x40 /
  0x80, and the sound cursor advances. Every listed unit i then moves d · s, s = the faction's
  `Space_Retreat_Flight_Move_Increment` when i ≤ R (released), else 8.125. The wait (+0x68) drops by n; when it is
  < 1, R := min(R + 1, count − 1) and wait := `Space_Retreat_Unit_Increment_Wait_Frames`. (Both cursors and the wait
  start at 0, so unit 0 is released on the first c ≥ 30 update, then one more unit every Wait frames.)
Units are **not** removed by the step; they fly off while the camera watches.

### 2.4 Finish [H]
- State 2 done: if the cinematic is still active → state 4, else → state 3.
- State 4 (0x533e30): done when the cinematic is no longer active; or when c > 180: every star base of the player
  (DUMMY_STAR_BASE, obj +0x3a0 bit 0x40 clear) spawns its type's object +0x1e78 [M: an explosion object, field name
  not found] and takes lethal damage (1e18); then done next update. [M: the camera's own end can come first.]
- State 3 entry: story `retreat_complete` (0x340750), the victory monitor release, then post-retreat vf11 (preserve the
  retreated units and anything inside a retreated flagship; destroy every other object of the player).

### 2.5 Port notes
The camera is presentation, but its counter times the flight. [port] The port runs the counter as one step per tactical
frame from the countdown end (c = frames since state 2 began), takes the cinematic as active whenever the battle is
space and In_Game_Cinematics is set (the shipped value), and ends state 4 at c > 180.

## 3. t07: DEBRIS (behaviour 0x28, `DebrisBehaviorClass`, vtable 0x8af578)

Fields: `Debris_Movement_Vector` (+0x1b78), `Debris_Facing_Rotate_Vector` (+0x1b84), `Debris_Min_Lifetime_Seconds`
(+0x1b90) / `Debris_Max_Lifetime_Seconds` (+0x1b94) — **ints** (field code 6; the shipped "15.0" parses as 15),
`Debris_Attached_Particle` (+0x1b98, resolved +0x1ba0). Serviced every frame (default interval).

- Initialise (vf9 0x62aca0) [H]: L = synced int(Min, Max); expiry := 0 (never) when L < 1, else mode frame + L · FPS.
  If obj +0x3a0 bit 0x40 is clear and the attached particle type exists, that particle is attached to the object
  (0x38d190) and remembered.
- Service (vf6 0x62ad30) [H]: position += `Debris_Movement_Vector` (world axes, per frame; non-forced teleport);
  orientation angles (obj +0x84 / +0x88 / +0x8c, degrees) += `Debris_Facing_Rotate_Vector` (0x3a8710). If Max > 0 and
  the expiry is set and reached: the attached particle is destroyed (0x391310, flag 1) and the debris takes 1e6
  damage (kind 1), so it dies through the normal death path (`Death_Explosions`, death clone, Remove_Upon_Death).
- vf16: the remembered particle is forgotten when it goes away first.

Shipped data: the 90 `Hardpoint_Breakoff_*` SpaceProps (SPACEPROPS.XML, SPACEPROPS_UNDERWORLD.XML): movement about
(±0.2, 0, −0.5) per frame, rotation (0.1..0.9) degrees per frame per axis, lifetime 15–25 s, a fire particle.
