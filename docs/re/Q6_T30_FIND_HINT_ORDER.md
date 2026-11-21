# Q6 t30: map hints (static read, 2026-11-15)

Answers the three questions of q6 t30:

1. Does a `Map_Load_Spawn_Table` pick inherit its placeholder's hint (`.ted` chunk `0x482`)?
2. In which order is the object list searched when two objects share a hint?
3. What does `.ted` chunk `0x46E` hold?

Binary: FoC Steam x64 `StarWarsG.exe`, sha256 `07daeeae...d018a4`. Addresses are image addresses
(base `0x140000000`). Static read only; nothing here is confirmed by an in-game probe.

## Results

| Question | Answer | Port today |
|---|---|---|
| 1. Spawn-table pick | Gets **no hint** [H]. | Same (`tactical_battle_bt.cpp`, the `[port]` note at `note_hint` can become a retail reading). |
| 2. Search order | **Newest first**: the most recently added matching object wins [H]. | Same. |
| 3. Chunk `0x46E` | The object's **marker name**: one string, same layout as the hint chunk `0x482` [H]. | Not read. |

Two more findings that came out of the read:

- **Only objects with the `HINT` behaviour (id `0x4A`) can be found.** `Find_Hint` and
  `Find_All_Objects_With_Hint` walk the game mode's hint list, not the list of all objects [H].
- **A map object gets `HINT` from its record, not only from its type.** The object record carries
  a list of extra behaviours (chunk `0x4B7`); the loader attaches each one [H]. This is how types
  without `HINT` in XML (`STORY_TRIGGER_ZONE`, `UMP_Small_Gear`, ...) carry a hint: in the FoC
  scripts 1382 of 1854 `Find_Hint` calls with a literal type name a type that has no `HINT`
  behaviour in XML.

## 1. Spawn-table pick: no hint

In the object reader (`0x1402b0a20`), chunk `0x459` creates the object and reads its base record.
When the type has a `Map_Load_Spawn_Table` and `0x140b15490` is clear, the pick (`0x140378d40`)
runs right there, inside the `0x459` case [H]:

- The queued entry is 40 bytes: picked type, owner id, and six floats copied from the placeholder
  (position and facing). It has no hint field [H].
- The placeholder is destroyed and the reader's current-object pointer is cleared before the
  record's later chunks arrive. Chunks `0x46E` and `0x482` are read only when the current object
  exists, so the placeholder's hint is never read [H].
- After the record loop each queued entry goes to the generic create call (`0x14029f810`); its
  result is not used, so nothing sets a hint on it [H].

**Company expansion does inherit** (`0x1402ab060`, the step after the record loop; STORY_T04 calls
it company expansion) [H]: when the loaded company / squadron object has `HINT` and a hint pack,
every object created for it gets `HINT` attached if it lacks it, receives a copy of the hint
string, and is added to the hint list. The company object itself is not in the hint list (next
section).

## 2. Search order: newest first, hint list only

- `Find_Hint` (`LuaFindMarkerCommandClass`, `0x1406a2ab0`) walks the list at game mode `+0x408`
  from its head and stops at the first object whose type equals the resolved type and whose hint
  equals the given string (case-insensitive) [H].
  - No second argument, or an empty hint string: the first object of that type **in the hint
    list** is returned, with no hint compare [H]. This is the q6 t29 case; an object of the type
    without `HINT` is not found.
  - A type match with a different hint does not stop the walk [H].
- The list is a `MultiLinkedListClass<GameObjectClass>` (named by its vftable in the game mode
  constructor `0x14029a0c0`; sentinel at `+0x410`, head pointer at `+0x418`). OBJECT_RUNTIME.md
  already names `+0x408` the HINT list.
- Every insert into it uses the head-insert helper `0x14020a9b0` [H]. The tail-insert helper
  `0x14020aa60` is never called with this list. The insert sites, found by searching the whole
  image for instructions that take the address of `+0x408`:

  | Site | When | Condition |
  |---|---|---|
  | `0x14029f810` | generic object creation | the type has behaviour `0x4A` |
  | `0x1402b5ad0` | registering a loaded object | the object has `HINT` (object `+0x37c` is not −1) and its type has no `Company_Units` (`+0x960`) and no `Squadron_Units` (`+0x980`) |
  | `0x1402ab060` | company expansion | the company object has `HINT` and a hint pack (section 1) |
  | `0x14029bc30` | `Create_Cinematic_Transport` with a hint | the object has `HINT` |

- Both helpers skip the insert when the object is already in the list, so an object keeps the
  position of its first insert [H].
- `Find_All_Objects_With_Hint` (`0x1406a2860`) uses the same list [M] (same field use; its body
  was not read in full).

Not settled: what "newest" means for two map objects created during the same load. Loaded objects
are registered by `0x1402b5ad0`; whether that runs in record order was not read [L].

## 3. Chunk `0x46E`: marker name; chunk `0x4B7`: extra behaviours

- Object `+0x1b8` is a `MarkerDataPackClass`, created when `MarkerBehaviorClass` is attached
  (`0x140628d30` → `0x140390b70`). Object `+0x1c0` is a `HintDataPackClass`, created when
  `HintBehaviorClass` is attached (`0x140630cd0` → `0x1403908e0`) [H].
- Both packs are 40 bytes holding one string, and their vtables point at **the same** save
  (`0x1404ee370`) and load (`0x1404ee2e0`) functions. Chunk `0x46E` therefore has the layout of
  chunk `0x482` [H].
- The reader skips either chunk when the object has no such pack, and the writer
  (`0x1402bac30`) writes each chunk only when the pack exists [H]. A hint in a map file therefore
  implies the object had `HINT` when it was saved.
- Extra behaviours: the base record reader (`0x14039c3a0`) calls `0x14039cca0`, which reads chunk
  `0x4B7`. Each `0x4B8` child is one behaviour: mini-chunk 0 is the behaviour id (4 bytes), minis
  1 and 2 are 4 bytes each, mini 3 is 8 bytes. After the chunk, each listed behaviour is created
  by id and attached to the object [H]. The meaning of minis 1 to 3 was not read [L].
- `HINT` is attached at run time in only two other places: company expansion and
  `Create_Cinematic_Transport` (the only two code sites that build the name `HINT`) [H].

## What this means for the port

- Spawn-table picks and search order need no change.
- The port finds a hinted object by its `0x482` chunk whether or not it has `HINT`. That gives the
  same result as retail for map files written by the original editor (a hint implies `HINT`) [M].
- map1 t04 (2026-11-17): the port reads 0x4B7 (each 0x4B8 entry's mini 0 is the behaviour's name CRC; all 3954 shipped
  entries in FoC + EaW maps.meg are `HINT`) and attaches it, and keeps 0x46E per object (`TacticalBattle::map_marker_name`).
  Company expansion copies the hint and `HINT` to the members (map1 t03).
- q6 t29 / t31 / t32 (2026-11-17): `Find_Hint` and `Find_All_Objects_With_Hint` walk only objects with the `HINT`
  behaviour (newest first); `Find_Hint` with one argument, a non-string second argument (nil included) or an empty string
  returns the newest object of the type with `HINT`, without a hint compare; only a call without arguments is a count
  error. Company expansion (map1 t03) gives every member `HINT` and the hint, and the company object is removed, so it is
  never found (`test_map1`). As the reader (§3), the map load keeps a 0x482 hint only on an object with `HINT` (type or
  0x4B7), and a company's members inherit only when the company has `HINT`; the every-map test counts the skipped hints
  (FoC: 2 of 7100, a Star_Destroyer and a Calamari_Cruiser hinted "pointguard" on `_SPACE_PLANET_AETENII_PIRACY`; EaW: 0).

## How this was read

REA MCP server (rea-agents 6.8.0, Ghidra 12.1.4, under `toolchain\rea-trial`) next to the existing
`re_reference/ghidra` dump; both give the same pseudocode. The dump answered question 1; REA's
byte-pattern search, caller lists and batch decompile answered questions 2 and 3.
