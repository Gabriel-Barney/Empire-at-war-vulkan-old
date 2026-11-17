# mcx2 t02: what retail draws for broken mod art

Roadmap mcx2 t02 / t03, [`MOD_COMPAT_DESIGN.md`](../MOD_COMPAT_DESIGN.md) §10.2, decision D5 (Gabriel, 2026-11-17: the
port draws what retail draws; where retail shows nothing, the port shows nothing and reports `asset_rejected`). Probe
kit: [`tools/probe_mod/compat_assets/`](../../tools/probe_mod/compat_assets/README.md).

## 1. The defects as they occur in the reference mods

`formats_validate` on Thrawn's Revenge (workshop 1125571106) loose `Data\Art\Models`, 2026-11-17; the shapes the
probe copies:

| Strict-parser reason | Real shape | Example |
|---|---|---|
| ALA "rotation block data size mismatch" | (a) info mini 0x0B (rotation block size) is 0 while chunk 0x1009 holds frames x 4 int16 and the bone's rotation index is 0 | `Prop_Venator_Idle_00.ALA` (1 bone, 25 frames, 100 int16) |
| same | (b) minis 0x0B / 0x0C / 0x0D hold float bit patterns (e.g. `a860ac3d`), no 0x1009 / 0x100A / 0x100B chunk exists, every bone's track indices are 0xFFFF: every track is the bone's default value | `RobedVibroFighter_*.ALA` (49 bones, 92 frames), `NB_ASTEROID_MINING_*.ALA` |
| "chunk overruns parent" | the model is complete (0x200, 3 x 0x400, 0x600); after it a tool wrote ten empty chunks (id 0, size 0) and one "chunk" 0x1000000 claiming 21,626,881 bytes with 514 present | `Defiler.alo` |
| "mesh without info chunk 0x402" | a mesh chunk 0x400 written as an empty data chunk, its children (0x401, 0x402, 0x10100, 0x10000) following at top level | `passive_z-95_headhunter.alo` |
| "model has no connections chunk 0x600" | the file is 0x200 + 0x400 only | `MandaloreBattleaxe.alo`, `Proj_Space_Ion_Red_Quad_Line.alo` |
| "skin bone index out of range of bone map" | one skinned vertex indexes past its submesh's bone map | `EV_1MTank_Swamp.alo` |

## 2. Retail's readers (read from the decompiled binary, in words)

- **Animation info and block data** (the ALA reader around RVA 0x19fb00): the info chunk's minis 1, 2, 3, 0x0B, 0x0C
  and 0x0D are copied into the animation as they are; nothing compares the block sizes with the data. Chunks 0x1009
  (rotation, int16), 0x100A (translation, uint16) and 0x100B (scale) are each read whole into an array sized from the
  chunk's own length. The reader returns success in every case. So (a) and (b) above load: with block size 0, a frame's
  rotation is read at `0 x frame + index`, i.e. the first frame's value on every frame; with no data and every index
  0xFFFF, nothing is read and every bone keeps its default value.
- Model, collision, skin and particle readers: not yet read; the probe answers them.

## 3. Probe results

**Run 1 (2026-11-17, `out\20261117-175406`, coordinator session; all 16 slots).** `CTRL` spawned through `Spawn_Unit`
and drew like a shipped AT-ST (walk pose). `NOCONN` (no connections chunk) did not come back from `Spawn_Unit`; the
probe's fallback `Create_Generic_Object` returned an object, and from that moment the simulation stopped: the credits
stayed at 6,272, no further record was written (its `HOLD_END`, due 270 frames later, never came), the camera showed
`NOCONN`'s spot with nothing drawn, and the game kept presenting the same frame until a system bugcheck (0x133
DPC_WATCHDOG_VIOLATION) rebooted the PC about 2.5 minutes later. So **retail hangs on a model without a connections
chunk** (at least when such an object is created; whether the bugcheck is the game's or the driver's is not known).
`NOINFO`, `OVR` and `ANS` were left out of the reruns after this (Gabriel, 2026-11-17: too risky after the bugcheck);
they stay unprobed guesses.

**Run 2 (2026-11-17, `out\20261117-183558`, `--skip NOINFO,OVR,ANS`).** Same start: `CTRL` drawn (walk pose),
`NOCONN` again only through `Create_Generic_Object` (f=606). The presented frame then stayed byte-identical for about
30 s (shots 11-25), and from there on the AT-ST's pose and the credits (6,272, the same value as run 1) never changed
again; `NOCONN` was never drawn at its spot, 87 units beside `CTRL`. The script still wrote `NOCONN`'s `HOLD_END`
(f=876) and `SPILL`'s `SPAWN_BEGIN` (f=909), then nothing; the game was still running, unresponsive, when the run
ended. So both runs agree: **once a model without a connections chunk is created, retail stops (the picture, then the
game); the object is never drawn.** Run 2 says nothing about `SPILL` (it spawned into an already stalled game); the
next run skips `NOCONN` as well.

Per slot, from `out\<run>\report.txt` and the screenshots:

| Slot | Retail | Port rule (t03) |
|---|---|---|
| `CTRL` / `SKCTRL` / `PCTRL` | | controls |
| `NOCONN` | **hangs** (runs 1 and 2: never drawn; the picture, then the game stop once the object is created; run 1 ended in a bugcheck) | do not draw it, `asset_rejected` marked retail-equivalent (port shows nothing, keeps running) |
| `NOINFO` | unprobed (skipped after run 1) | |
| `SPILL` | unknown (run 2 reached it only after `NOCONN` had stalled the game) | |
| `COLL` | | |
| `TRAIL` | | |
| `BORDER` | | |
| `ANZ` | | |
| `ANG` | | |
| `SKIN` | | |
| `PBAD` | | |
| `ANS` | unprobed (skipped after run 1) | |
| `OVR` | unprobed (skipped after run 1) | |
| `EMPTY` | | |

## 4. Texture, font and movie lookups (t05, read with REA 2026-11-17)

- **No retry after a decode failure.** `Load_Texture`'s inner step (0x13db20) asks the existence search (0x13f450:
  the name as given, then each registered texture directory + the part after the last `\`) and, only when nothing
  exists, repeats it for the `.dds` name. As soon as a candidate exists it builds the texture object from that path;
  a file that then fails to decode is not followed by a `.dds` attempt. The port's `load_texture` does the same. [H]
- **`.dds` substitute: cut at the first `.`.** The name is copied and cut by a C-runtime tokeniser with the delimiter
  string `"."` (bytes at 0x14080329c: `2e 00`), then `.dds` is appended. Matches `texture_lookup` (`s.find('.')`). [H]
- **Content decides the image format.** The texture object loads through D3DX, which detects the file type from its
  bytes; Thrawn's Revenge ships 32 PNG files named `.TGA` (`Data\Art\Textures\Icons\I_BUTTON_*.TGA`, `89 50 4E 47`
  header), so retail draws PNGs whatever the extension. Port: `decode_texture` sniffs PNG and decodes it
  (`engine/assets/src/png_decode.cpp`). [M] (D3DX's documented behaviour; not traced in the binary)
- **Fonts are not looked up through the file system.** The only font-registration name in the binary is the string
  `AddFontMemResourceEx` (resolved at run time); there is no `.ttf` / `.otf` string. Retail registers the TrueType
  fonts embedded in its exe from memory and otherwise uses installed system fonts; a font file in a mod's MEG or Data
  folder is not loaded. The port's extra loose-file lookup (`ui::register_game_fonts`) is a superset; nothing to add. [M]
- **Bink 2:** retail x64 1.121 imports `bink2w64.dll` (shipped with the game) and plays KB2 movies; the port's FFmpeg
  path cannot (t09, honour gap until decided). [H]
