# storyb t16: the environment sky dome object at tactical map load (static read, 2026-11-17)

Question (roadmap storyb t16, census of `STORY_T04_KESSEL_RUN.md` §7.1): retail Kessel UM01 has one
`NIGHT_STARS_NOFLARE` object (`LandPrimarySkydome`, `Is_Decoration` Yes, behaviour `SKY_DOME`) in the **primary** store
at battle frame 1, owned by the Neutral player (owner 3 in the trace), although map decorations go to the secondary
store. Which code creates it, in which store, with which owner, position and facing, when, and how is the type
picked? Is a space map's sky object made the same way?

Binary: FoC Steam x64 `StarWarsG.exe`, RVAs (image base `0x140000000`), read with ReVa plus the llvm-objdump listing
of `.text`. Map data: `DATA\ART\MAPS\UM01_A_CRIMELORD_UNLEASHED.TED` from `maps.meg` (no patch override), read with
`tools/meg/megtool.py` into a scratch folder. Builds on `CU8G_T02_ENV_PICK_RE.md` §3 step 9 (which named the call)
and `TERRAIN_WATER_POST.md` §5.2 (TEE fields). Tags: **[H]** read, **[M]** likely, **[L]** inferred.

## Results

| Item | Retail |
|---|---|
| Creator | Mode vf59 "create environment sky domes": land `0x3bc1c0`, space `0x4d98c0`. **Not a map object record** [H] |
| Source of the type | The **current environment record** of the map (`0x100/4/6` TEE record): mini `0x19` = sky dome 1 type name (env +0x20), mini `0x1A` = sky dome 2 (env +0x40). Up to two objects, one per non-empty name [H] |
| Which record | RtsScene +0xa0, the index chosen by the map-load environment pick (`CU8G_T02_ENV_PICK_RE.md` §1). UM01 has one record, `Nighttime_Rain` (start index `0x100/8` mini `25` = 0, so no draw): `19` = `NIGHT_STARS_NOFLARE`, `1A` empty, scales `1B` / `1C` = 1, angles `1D`…`20` = 0 [H, data] |
| Type lookup | By name in the type manager (`0x407c00` on `0x140b2f020`); XML `Night_Stars_noflare` matches the map's upper-case name [M: case-insensitive key]. Name with no type: that name is **cleared** in the environment record and no object is made [H] |
| Store | The **primary** store: plain object create `0x29f810` (object manager main list, id map, the owner's list, the trees its type qualifies for). `Is_Decoration` only stops the "object created" event 0x1c there; the secondary-store rule belongs to the map object reader `0x2b0a20` alone [H] |
| Owner | The player of the **first faction with `Is_Neutral`** (faction +0x108; `0x331cc0` over the faction list `0x140a172d0`, then `0x294d30`, player id at player +0x48) [H] |
| Position, land | From the terrain height map box (`0x1320d0` on mode +0x338): x = \|grid width × cell size\| / 2, y = \|grid height × cell size\| / 2, z = \|max height − min height\| / 2, where the heights are the samples × the height scale and min / max both start at 0 [H arithmetic] |
| Position, space | (0, 0, 0) [H] |
| Facing | Orientation triple (roll, pitch, yaw) = (angle A, **0**, angle B), angles in degrees: dome 1 env +0x124 / +0x128, dome 2 env +0x130 / +0x134 (stored in radians, converted by the getters `0x2731c0`, `0x2731f0`, `0x273250`, `0x273280`). TEE minis `1D`, `1F` (dome 1) and `1E`, `20` (dome 2) per `TERRAIN_WATER_POST.md` §5.2 [H getters, M mini mapping]. UM01: (0, 0, 0) |
| Scale | TEE `1B` / `1C` are **not** applied by vf59 [H: not read there] |
| When | Post-load start (land vf+0x2a0 `0x3b7af0`, space `0x4d75a0`) → `0x35d540` → vf59. After every map chunk (environments, terrain, all object records with their decorations, `Map_Load_Spawn_Table`) and after the environment pick; before mode vf+0x20 (signal 0x23, Lua `Game_Mode_Starting_Event`). So the sky object gets an object id **after all map objects** [H] |
| Space maps | Same function shape (`0x4d98c0`): same type source, owner, store and facing; position (0, 0, 0); no sky cube step [H] |

## 1. vf59 step by step (land `0x3bc1c0`) [H]

1. No RtsScene (mode +0x328): return.
2. Mode vf+0x1e0 (`0x3bac00`, shared by land and space): destroy the sky dome objects made earlier, whose ids the mode
   keeps at +0x1d0 (dome 1) and +0x1d4 (dome 2), and set both to −1.
3. Read the current record's two names (`0x273190`, `0x273220`). Both empty: return.
4. Land only: position from the terrain box (table above). The code takes (box max − box min) / 2 per axis. For a box
   that starts at 0 this is the centre in x and y and half the height range in z [M: that the terrain's local box
   starts at the world origin; compare with the trace's position of the object].
5. Owner: the player of the first `Is_Neutral` faction.
6. Dome 1, when its name is non-empty: look up the type. Found: create it with `0x29f810`(object manager, type,
   owner id, position, (angle A, 0, angle B), 1, 0) and store its id at mode +0x1d0. Not found: +0x1d0 := −1 and the
   record's dome 1 name := empty (`0x276b30`).
7. Dome 2 the same with its own angles (+0x1d4, `0x276c40`).
8. Land only: vf+0x288 (`0x3bc590`): re-render the water sky cube from the sky dome objects
   (`TERRAIN_WATER_POST.md` §3.5).

The object constructor `0x388b60` copies the orientation triple unchanged into obj +0x84…+0x8c and its second copy
+0x90…+0x98 [H copy, M that the copied pointer is this argument].

## 2. Other callers and other paths [H unless tagged]

- vf59 is called by post-load `0x35d540` (mode types 1 and 2 only) and by the map writers. `Set_New_Environment` and
  `Set_Cinematic_Environment` do not call it, so a later environment change keeps the load-time sky objects
  (`CU8G_T02_ENV_PICK_RE.md` §2).
- The `SKY_DOME` behaviour's service (`0x61c9f0`) only caches a model node of the object (render bookkeeping); it
  does not move the object and draws no random number [M: role].
- A sky dome type placed as an ordinary map object record would go through `0x2b0a20` and, being a decoration, into
  the secondary store (storyb t16 decorations reading). UM01 has no such record: the name `NIGHT_STARS_NOFLARE` occurs
  once in the map file, in the TEE record [H, data].

## 3. Port consequence

At post-load start, after the map objects and the environment pick, create one primary-store object per non-empty
sky dome name of the picked environment record, owned by the first `Is_Neutral` faction's player, at the terrain box
half-extents (land) or the origin (space), with orientation (angle A, 0, angle B). Clear an unknown name instead of
failing. Destroy the previous ones first.
