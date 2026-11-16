# mov2 t07: squad stop distance, re-order braking and MovingVsMoving holds in retail

Roadmap mov2 t07 (backlog b0093, found in s78n). Since s78n t26.5 the port's squad containers spend most of their End
Move frames coasting (FIGHTER_FLYING §7.7 item 5, MOVEMENT §3.5.2 G; 5791 to 11662 container coast frames per Utapau
seed). This kit measures the same things in retail, frame by frame, so the comparison does not depend on feel:

| Trial | What is measured | Spec |
|---|---|---|
| `stop` (squads Rows / Dartboard, a T4B) | cruise speed, frames and distance from the brake start to rest, End Move (0x14) / 0x15 frames, rest point against the goal, last member at rest | FIGHTER_FLYING §7.7 items 1, 3.4, 4; MOVEMENT §3.5.2 B / C |
| `same` | a new Move_To straight on, past the old goal, while marching: speed kept (aligned) or a coast | MOVEMENT §3.5.2 G ("aligned moving unit keeps its state and speed") |
| `turn90`, `reverse` | a new Move_To to the side / back while marching: End Move coast at the deceleration, distance run on, frames to restart | MOVEMENT §3.5.2 G ("not aligned"), FIGHTER_FLYING §7.7 item 5 |
| `mvm_headon`, `mvm_offset12`, `mvm_atst_t4b`, `mvm_headon2` | two tanks ordered head-on (and 12 to the side; an AT-ST against a T4B): each one's holds at speed 0, longest hold, arrival | MOVEMENT §4 MovingVsMoving; port s78n t25.4 seeds held 166 / 232 frames, retail state-4 hold ~160 by RE (f8afef8d) |

## How it measures

- **Probe Lua** (`mov2_t07_probe.lua`, appended to the shipped `BusyTacticalFreeStore.lua` with the s78n t14 kit's
  record helpers and arming drivers, records `M7Q|...`): runs in the Rebel AI's free store of a land skirmish with a
  human Empire, spawns each trial's units for the human on a strip the builder checked (900 x 120, passable, map
  objects >= 40 away, far from the starts; Geonosis: start (880, 1040), heading 345°), makes them invulnerable and
  unable to fire, and gives the orders. It names every object id in a `TRACK` record.
- **Tracker** (`mov2_t07_track.py`, read-only `ReadProcessMemory`): finds each id in the mode's object store and, on
  every new global logic frame, writes position, the facing floats, the locomotor state (pack +0x48) and the speed
  (pack +0xEC). The Lua records carry g = NOW (the same clock), so orders line up with the per-frame trace.

## Run (one land skirmish, about 12 minutes in battle, hands-off)

Steam running, the game not running. From the worktree root:

```
tools\probe_mod\mov2_t07_stops\run_mov2_t07.cmd
```

It backs up the profile, builds and installs `Mods\Mov2T07Probe`, launches `StarWarsG.exe WINDOWED SCREENWIDTH=1920
SCREENHEIGHT=1080 MODPATH=Mods\Mov2T07Probe` through `common\launch_foc.py`, scans and tracks until the game exits,
writes the report, removes the mod and restores the profile. By hand:

1. Main menu > Single Player > Skirmish > **Land**, map **Geonosis**.
2. Slot 1 (you): **Empire**, team 1. Slot 2: **AI Easy**, **Rebel**, team 2. Every other slot **Closed**. Default
   credits and settings. Start.
3. Give no orders, click nothing on the map, never pause or change the game speed, never save, keep the window in
   front. "MOV2 T07 PROBE: ... trials armed" appears in the first seconds; the probe points the camera at the trials.
4. Wait for "MOV2 T07 PROBE: trials recorded" (about 12 minutes), then Esc > Quit and quit the game.

The run ends with `MOV2_T07_RUN_DONE exit=<n>` (0 ok, 10 no records, 20 preflight, 30 interrupted, 31 error).
`--dry-run` builds the mod into the run folder and prints the trials without touching the game.
After a crash: `cleanup_mov2_t07.cmd` (removes `corruption\Mods\Mov2T07Probe` only when it holds
`MOV2T07_PROBE_MARKER.txt`, restores the last run's profile). No retail file is changed.

## Output

`out\<yyyymmdd-hhmmss>\`: `records.log`, `track.log`, `config.json`, `report.txt`, `run.json`, `profile_backup\`.
`run_mov2_t07.cmd --report-only out\<run>` rebuilds the report. It has one block per trial (STOP / REORDER lines with
the state sequence, MvM holds) and a summary table. The findings go to `docs/re/MOVEMENT.md` (§3.5.2 G) and
`docs/re/FIGHTER_FLYING.md` (§7.7), compared with the port's numbers for the same moves.

## Offline check

`toolchain\venv\Scripts\python.exe -I tools\probe_mod\mov2_t07_stops\selftest_mov2_t07.py`: tracker decoding, the mod
built from `extracted\foc_data_text.zip` (offline corridor) and compiled, the generated probe Lua run under stock Lua
5.0.2 with a stub land engine that writes the tracker's lines, and the analyser on the result (every trial ends, STOP
and REORDER lines per trial, the stub's 40-frame MvM hold found). Printed **MOV2 T07 SELFTEST OK** on 2026-11-16.

## Limits

- Retail skirmish needs the human slot: the probe units are the human's, idle otherwise; the Rebel AI (Easy) plays on
  far away. An AI unit wandering onto the strip shows in the trace as an unplanned hold.
- Object offsets (+0x78 position, +0xA8 -> +0x48 state, +0xEC speed) come from the docs/re specs and earlier kits
  (`s7s8_probes`); the state names in the report follow MOVEMENT §3.2 (0x13 move, 0x14 End Move, 0x15 arrive,
  0x16 stopped).
- A move whose re-order point is never reached (the unit stops short) ends at the 1500-frame leg limit (`LEGTO`).
