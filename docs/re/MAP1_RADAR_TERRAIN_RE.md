# Land radar terrain picture (StarWarsG.exe FoC x64 v1.121, 2026-11-17)

How retail builds the passability picture behind the tactical radar (minimap) on land, which
`RadarMap.xml` colour block it uses, and how texels map to the map. Read with REA and the Ghidra
pseudo-C dump; behaviour in words only. Addresses are VAs. Confidence: **[H]** read in code and
cross-checked, **[M]** read but intent inferred, **[L]** inferred, not read.

## Results

| # | Question | Answer | Conf. |
|---|---|---|---|
| 1 | Builder, when | `0x140359ae0` fills the texture. Runs when the land mode is activated (`0x1403b7af0` -> `0x140358b00` with radar mode 1), on display-size change (`0x140304770` -> `0x1403585f0`) and on a debug hot reload of RadarMap.xml (`0x140356960`). Not rebuilt when passability changes later. | H (call sites), M (no later rebuild) |
| 2 | Colour block | Block index = the game mode's environment index (mode +0x8), copied from map info (top chunk 0) mini-chunk **0x06** (u32) when the value is < 8. 0..7 = TEMPERATE, ARCTIC, DESERT, FOREST, SWAMP, VOLCANIC, URBAN, SPACE. Missing / >= 8 leaves the default -1: no block, the picture is fully transparent. | H |
| 3 | Per-cell class | Water test first, then fixed bit order (below). Uses the cell's *effective* flags (static map bits OR footprints of objects registered on the cell, plus Obstacle outside the playable box). No height shading; alpha comes from the XML colour. | H |
| 4 | Size, orientation | One texel per screen pixel of the radar rectangle. Texel (x, y) samples world X = minX + w*x/W, world Y = maxY - h*y/H: **row 0 is the max-Y (north) edge**. Fog is not baked in. | H (mapping), M (pixel size) |
| 5 | Space | Not built: radar mode 2 clears the texture to zero (`0x140269a30`). | H |

## RadarMap.xml colour table (`0x1403572c0`, `0x140356bb0`)

- Class index of each `<Color name=...>` inside `Passability_Color_Settings` [H]: STEEP 1, WATER 2,
  OBSTACLE 3, WALL 4, IMPASSABLE 5, INFANTRYONLY 6, CLEAR 7. Index 0 is never written or read.
  An unknown name logs a warning and is skipped.
- Block offset per settings name [H]: TEMPERATE 0, ARCTIC 1, DESERT 2, FOREST 3, SWAMP 4, VOLCANIC 5,
  URBAN 6, SPACE 7 (x 0x20 bytes). Any other name (vanilla uses `Default`) selects a ninth block;
  a colour written there is also copied into the same slot of all 8 blocks, so `Default` must come
  first (the XML says so too) and later named blocks override it.
- Before parsing every slot is opaque black (0xFF000000, `0x1403564e0` / `0x140358a80`).
- Colours are packed as A8R8G8B8 from the XML's `r,g,b,a` [M: byte order of the colour helper
  `0x1401f94d0` read from its use, not its body].
- The parse-time "current block" pointer (+0x58) is reused at run time: `0x140358b00` sets it to
  `table + 0x20 * env_index`, or null when the index is -1.

## Block selection (`0x140360970`, `0x14035a5e0`, `0x1403b7af0`)

- The GameMode base constructor sets mode +0x8 to -1 [H].
- The base-map loader walks the .ted: in top chunk 0 it reads mini-chunk 0x06 as a u32; after the
  world chunk (1) it stores that value to mode +0x8 only if it is below 8 [H]. In the port's
  `ted.hpp` `MapInfo` this is `counts[4]` (the "02..07" run).
- Land activation passes mode +0x8 as the block index; space activation passes it too, but the
  space path never builds the picture.
- The planet's `<Terrain>` tag (Planets.xml) is **not** used here: the only TEMPERATE..URBAN string
  compare in the binary (`0x14028fda0`) serves something else (transport landing dust) [M].

## Per-texel rule (`0x140359ae0`)

For each texel: convert to a world point (`0x14026b270`), clamp it to
[rect min + 0.001, rect max - 10 - 0.001] on each axis, find the passability cell
(20 x 20 world-unit cells, index = (coord + 10) / 20, grid = terrain vertex counts; `0x1405116c0`).
A texel whose cell is outside the grid stays 0 (transparent). Otherwise:

1. If the tactical camera has a bounding box (camera +0x10F set) and the point lies outside it,
   the texel is opaque black. [M: box from the 0x10C camera chunk]
2. **Water**: the map has water (terrain +0x204) and the water height at the point is at or above
   the terrain height (global level, or a water volume) -> WATER. The cell's Water bit (0x20) also
   gives WATER. Water wins over every other bit.
3. Else Steep (0x2) -> STEEP.
4. Else Obstacle (0x4) -> OBSTACLE.
5. Else InfantryOnly (0x80) -> INFANTRYONLY.
6. Else Impassable (0x100) -> IMPASSABLE.
7. Else RebelWall or EmpireWall (0x8 | 0x10) -> WALL.
8. Else -> CLEAR (Clear, Shield 0x40 alone and an empty cell all land here).

Effective flags (cell +0x14, `0x14059f4b0`, `0x14059f110`) [H]: the stored map bits (chunk 0x10A,
or the old byte chunk 0x104: 0 Clear, 1 Steep, 2 Obstacle) OR the bits of every object footprint
on the cell (object hard-point / footprint flags and the type's ground passability field). After
loading, every cell outside the playable box (chunk 0x103 centre / half extents) also gets
Obstacle. If any bit besides Clear is set, Clear is dropped. The radar reads the flags at
build time; the "passability changed" flag (0x140a2bed8) has no reader, so buildings
placed or destroyed later do not repaint the picture [M].

## Size, rectangle, orientation (`0x140358cd0`, `0x14035f140`, `0x14026b270`)

- Texture W x H = ceil(screen width x NDC width / 2) by ceil(screen height x NDC height / 2) of
  the land radar rectangle, i.e. one texel per screen pixel [M: screen-size helper `0x14025e960`].
- World rectangle (`0x14035f140`): the map's playable bounds (chunk 0x103); when the tactical
  camera has a bounding box it is instead a square around that box's centre with half side =
  max(half x, half y) [M].
- Mapping [H]: u = x/W from min X to max X; v = y/H from **max Y down to min Y**. Samples are taken
  at the texel corner (no half-texel offset). Pixel layout is row-major, y rows.
- A 180-degree flip flag (radar widget +0x4C0) exists; the only setter call passes 0 [M].
- Fog of war: mode 1 hands the widget a backdrop texture and one colour (radar +0x118 / +0x120,
  likely `Land_Backdrop_Texture_Name` / `Land_FOW_Color`); the fog is drawn by the widget, not
  written by this builder [M].

## What this means for the port

- **Per-cell class rule**: water (water bit, or water surface at or above the terrain) > Steep >
  Obstacle > InfantryOnly > Impassable > wall (either faction) > Clear. Shield alone is Clear. Use
  the effective cell flags (map bits OR object footprints, Obstacle outside the playable box),
  taken once when the battle starts; no height shading.
- **Biome selection**: map info mini-chunk 0x06 (`MapInfo::counts[4]`), 0..7 =
  Temperate, Arctic, Desert, Forest, Swamp, Volcanic, Urban, Space; any other value means no
  passability picture (transparent). Colour slots not given by a named block keep `Default`.
- **Size / orientation**: texel row 0 = world max Y (north), column 0 = world min X; cover the
  playable rectangle (or the camera square); size to the radar's on-screen pixels. Cells outside
  the grid are transparent, outside the camera box opaque black. Space maps get no picture.
