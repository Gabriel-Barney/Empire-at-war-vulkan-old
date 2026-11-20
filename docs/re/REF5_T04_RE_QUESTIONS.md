# REF5 t04: ship names (GameObjectWrapper.Get_Name) - questions for re-analyst

Roadmap ref5 t04 wires `lua::ObjectBridge::ship_name` (engine/lua/include/alamo/lua/object_bridge.hpp) so that
`GameObjectWrapper.Get_Name` returns the retail ship name. What is known: `docs/re/MCX8_BINDINGS_OBJECTS.md`
§"GameObjectWrapper.Get_Name" (0x578e00 reads SelectionDataPack +0x40, drops the first and last character when longer than
2; only writer 0x3c2440 from SelectBehaviorClass vf6 0x3c2310; random name from TheShipNamesManager, none in multiplayer
0x4e5b30; tooltip 0x371a50) and `docs/re/MCX4_T14_RE_ANSWERS.md` §"GameConstants.ShipNameTextFiles" (loader
TheShipNamesManagerClass::NameListStruct 0x4e6b20). The shipped lists (GameData\Data\*.txt) are UTF-16LE, one plain name
per line ("Allecto", no quotes), keyed by GameConstants `ShipNameTextFiles` pairs (type name, file).

Answer into `docs/re/MCX8_BINDINGS_OBJECTS.md` (a "Ship names" section under Get_Name), behaviour in words, RVAs named:

1. **Which objects get a name** (0x3c2440 / 0x3c2310): the test that picks the list - the object's own type name matched
   against the `ShipNameTextFiles` keys (exact? case-insensitive? the representative / company type?), any category or
   owner test, galactic and tactical objects alike? When in the object's life does it run (first service, spawn)?
2. **The parent-mode copy**: when a tactical object has a parent-mode (galactic) object, is that object's name copied as is?
   Does a galactic object get its own name the same way (so a fleet's ship keeps one name into every battle)?
3. **The pick** (0x4e5b30 and the manager): which random generator (the synced game random, the free one, the CRT rand)?
   Uniform over the file's lines? Are names taken without repeats (a used-name list, a cursor), and what happens when the
   list runs out? Is the "none in multiplayer" test the game type or something else?
4. **The stored text**: what exactly 0x3c2440 writes to SelectionDataPack +0x40 - the bare name, or a formatted string (for
   example the name in quotes, which would explain Get_Name dropping the first and last characters)? Is the name saved in a
   save game (SelectionDataPack save)?
5. **The file reader** (0x4e6b20): line splitting, blank lines, BOM, trimming.

Port guess until answered: none in code (ship_name stays unwired; GameObjectWrapper.Get_Name returns "").
