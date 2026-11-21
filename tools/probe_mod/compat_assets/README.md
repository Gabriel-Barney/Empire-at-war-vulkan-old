# compat_assets probe kit (roadmap mcx2 t02)

**Question.** For each defect class that the port's strict `alamo_formats` parser rejects in mod art
(`docs/MOD_AUDIT_2026-11-17.md` §3, `docs/MOD_COMPAT_DESIGN.md` §10.2), what does retail FoC (Steam x64 1.121) show:
the unit drawn as the control, drawn differently, not drawn, or does the game stop? Decision D5 (Gabriel,
2026-11-17): the port draws what retail draws and shows nothing (with an `asset_rejected` issue) where retail shows
nothing. The answers go to [`docs/re/MCX2_T02_ASSET_SALVAGE.md`](../../../docs/re/MCX2_T02_ASSET_SALVAGE.md) and decide
each salvage rule of mcx2 t03.

## Slots and predictions

Each slot is a variant type `CAP_<KEY>` (`Variant_Of_Existing_Type`) whose model `CAP_<KEY>.ALO` (or idle animation,
or particle system) is a broken copy of a shipped file, built from your install by `build_compat_assets_mod.py`. The
AT-ST slots use `EV_AT-ST.ALO`, with the walk cycle `EV_AT-ST_MOVE_00.ALA` as `IDLE_00` so a played animation is
visible as walking in place. Predictions come from reading retail's loaders (`re_reference/`, read only): retail's
chunk readers do not cross-check counts and sizes the way the port's parser does.

| Slot | Defect (real mod file it copies) | Prediction |
|---|---|---|
| `CTRL` | none: intact AT-ST, walking in place | drawn, walks |
| `NOCONN` | no connections chunk 0x600 (TR `MandaloreBattleaxe.alo`; 5 mod files) | drawn, every mesh at the root bone (stacked at the origin) |
| `NOINFO` | the body mesh without its info chunk 0x402 (synthetic) | drawn; the body mesh with default info |
| `SPILL` | one mesh chunk empty, its children at top level (TR `passive_z-95_headhunter.alo`) | drawn without that mesh (the muzzle flash, normally hidden) |
| `COLL` | collision-tree inner node's child index out of bounds (8 mod files) | drawn (collision is not needed to draw) |
| `TRAIL` | junk chunks after the complete model, the last overruns the file (TR `Defiler.alo`) | drawn as `CTRL` |
| `BORDER` | a bone listed after its child, hierarchy unchanged (synthetic) | drawn, maybe with the moved bone's subtree misplaced |
| `ANZ` | animation: rotation block size 0, rotation data kept (TR `Prop_Venator_Idle_00.ALA`) | drawn; every frame uses the first frame's rotations: no leg swing, the body bobs |
| `ANG` | animation: garbage block sizes, no block data, no bone track (TR `RobedVibroFighter_*.ALA`; most of the 53 "rotation block size" rejections) | drawn in the default (rest) pose, no motion |
| `SKCTRL` | none: intact stormtrooper | drawn |
| `SKIN` | one skinned vertex's bone index outside the bone map (TR `EV_1MTank_Swamp.alo`) | drawn; one vertex may be stretched |
| `PCTRL` | none: intact fire particle (`P_FIRE_MED01`, lifetime raised) | drawn (fire) |
| `PBAD` | emitter property `outward_speed` 8 bytes instead of 4 (synthetic) | drawn as `PCTRL` |
| `ANS` | animation: rotation data holds half the declared frames (synthetic; reads past the data) | drawn; the second half of the cycle garbled, or a crash |
| `OVR` | a chunk inside a mesh claims more bytes than its mesh holds (synthetic) | not drawn, or a crash |
| `EMPTY` | an empty `.ALO` (2 mod files) | not drawn (missing model) |

The slots most likely to stop the game come last. If it stops, the report names the slot that was spawning; rerun with
`--skip <KEY>` for the rest.

## Run (one land skirmish, about 3 minutes in battle, hands-off)

1. Steam running, the game closed. From the project root, in a normal PowerShell or cmd window (not a Claude terminal
   tab, so the game is not a child of Claude's shell):

   ```
   tools\probe_mod\compat_assets\run_compat_assets.cmd --no-launch --non-interactive
   ```

2. Launch the game windowed (second window, or a Claude shell):

   ```
   toolchain\venv\Scripts\python.exe -I tools\probe_mod\common\launch_foc.py MODPATH=Mods\CompatAssetsProbe
   ```

   Without `--no-launch` the runner launches it itself (add `--windowed`).

3. Main menu > Single Player > Skirmish > **Land**, map **Geonosis**. Slot 1 (you): **Empire**, team 1. Slot 2:
   **AI Easy**, **Rebel**, team 2. Every other slot **Closed**. Default credits and settings. Start.
4. Give no orders, click nothing on the map, never pause, never save, keep the game window in front: the runner copies
   the screen area of the window every 2 s from the moment the probe arms.
5. "COMPAT ASSETS PROBE armed" appears at 10 s. Then 16 slots, about 10 s each, on an open corridor far from both
   bases: a unit appears, the camera points at it, and the line "CAP SLOT <n> <KEY>: ..." names it. Then a closing
   overview of the row. Wait for "COMPAT ASSETS PROBE done", then Esc > Quit and quit the game.

The runner then writes `out\<run>\report.txt` / `report.json`, keeps the screenshots in `out\<run>\shots\`, removes
`Mods\CompatAssetsProbe`, restores the profile and ends with `CAP_RUN_DONE exit=<n>` (0 records found, 10 none, 20
preflight, 30 interrupted, 31 error). Options: `--only KEYS`, `--skip KEYS`, `--shot-sec S`, `--map NAME`,
`--report-only out\<run>\records.log`.

## Reading the result

`report.txt` gives per slot how it spawned and how many objects were alive when the camera arrived and left, and the
slot the game stopped on, if any. What retail drew is read from the screenshots: compare each slot with its control
(`CTRL`, `SKCTRL`, `PCTRL`); for `ANZ` / `ANG` / `ANS`, compare the motion across the slot's 4-5 shots with `CTRL`'s.
Write each verdict into `docs/re/MCX2_T02_ASSET_SALVAGE.md` with the run id.

## Cleanup

`cleanup_compat_assets.cmd` (`--dry-run` to preview) removes `Mods\CompatAssetsProbe` only if it holds
`CAP_PROBE_MARKER.txt`, and restores the profile snapshot of the newest run. No retail file is changed.

## Offline check

```
toolchain\venv\Scripts\python.exe -I tools\probe_mod\compat_assets\selftest_compat_assets.py --validator build-wt-msvc\bin\formats_validate.exe
```

Builds the mod from the install into a temp folder, checks that the port's strict parser rejects every broken copy
with the slot's reason and passes every control, and runs the probe Lua under stock Lua 5.0.2 with stubbed engine calls
through every slot.
