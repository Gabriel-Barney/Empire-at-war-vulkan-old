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

Not run yet. Per slot, from `out\<run>\report.txt` and the screenshots:

| Slot | Retail | Port rule (t03) |
|---|---|---|
| `CTRL` / `SKCTRL` / `PCTRL` | | controls |
| `NOCONN` | | |
| `NOINFO` | | |
| `SPILL` | | |
| `COLL` | | |
| `TRAIL` | | |
| `BORDER` | | |
| `ANZ` | | |
| `ANG` | | |
| `SKIN` | | |
| `PBAD` | | |
| `ANS` | | |
| `OVR` | | |
| `EMPTY` | | |
