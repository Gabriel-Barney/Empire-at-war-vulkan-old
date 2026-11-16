# q6 t33 / t35: what holds retail's galactic end screen after an elimination

Roadmap Q6, tasks t33 and t35 (backlog b0141). In the q6 t25 runs (2026-11-09, `../q6_t15_probes`) ELIM D ended
within 1 s of its owner change, ELIM E 4.3 to 5.3 s after it. The static read
(`docs/re/Q6_T27_ELIM_END_TIMING.md`) found the same victory path for both (declared at the owner change, countdown
30 frames), and a main-loop rule: the end screen shows only while **no message box is up and no Bink is open**. What
held E for 99 to 131 extra frames is the open question. This kit measures it directly.

## How it measures

- **Gate poller** (`q6t33_gates.py`, read-only `ReadProcessMemory`, every ~4 ms): the global logic frame, the galactic
  mode frame, the victory monitor (declared, countdown, hold counter), the message-box flag, the Bink handle, the
  end-screen-shown flag, the galactic GUI's hint / pop-up / movie flags and movie name (`Light_Loop`), the cursor. The
  addresses are listed in the script's docstring; they were checked read-only on a running retail game on 2026-11-16
  (identity OK, galactic mode, monitor found, frames advancing). One line per change.
- **Plot Lua** (`q6t33_story.lua`): the q6 t25 D / E script (same galaxy, same times: D gives Abregado Rae to the Rebel
  at 20 s; E gives Tatooine to the human at 10 s and makes it Neutral at 40 s), heartbeats every 0.25 s around the
  event, each with `Get_Game_Mode()` and where the human's `Star_Destroyer` is (planet, parent).
- **Screenshots**: half-size PNGs every 0.5 s from 3 s before the declare until 3 s after the end screen, plus one
  context shot per 10 s (`shots\shots.csv` ties each to a gate sample).

| Variant | What it changes | Separates |
|---|---|---|
| `E_BASE` | E as in q6 t25: Star Destroyer at Tatooine, 81-char `Game_Message` after the take | baseline; t35 (b) |
| `E_NOMSG` | no `Game_Message` after the take | the Lua hint text |
| `E_NOFLEET` | no Star Destroyer | the local-player retreat in `Change_Owner` |
| `E_SDBYSS` | Star Destroyer at Byss, not at Tatooine | t35 (a): the retreat with units elsewhere |
| `E_CURPLANET` | `E_BASE`, cursor resting on Tatooine | the encyclopedia pop-up |
| `D_BASE` | D as in q6 t25 (58-char message) | baseline |
| `D_LONGMSG` | D with a 150-char message | does the hint gate scale with text length |

Every other variant parks the cursor over empty space.

## Run (one variant per game launch)

Steam running, the game not running. From the worktree root:

```
tools\probe_mod\q6_t33_elim_end\run_q6t33.cmd E_BASE
```

It builds and installs `Mods\Q6T33Probe`, launches `StarWarsG.exe WINDOWED SCREENWIDTH=1920 SCREENHEIGHT=1080
MODPATH=Mods\Q6T33Probe` through `common\launch_foc.py`, prints the steps, polls until the game exits, writes the
report and removes the mod. By hand, per variant:

1. Main menu > Single Player > Galactic Conquest > **"Q6T33: <variant>"** > **Empire** > Start.
2. As soon as the galaxy shows, park the mouse: over empty black space at least 250 px from every planet, route and
   HUD element; for `E_CURPLANET` on the Tatooine planet sphere. Do not move it again.
3. Click nothing. The event comes at 40 s (E) or 20 s (D); the end screen should follow within about 10 s.
4. When the end screen shows (or 30 s after the event), wait 3 s, leave to the main menu (the end screen's button, or
   Esc > Quit to main menu; never save), then quit the game.

The run ends by itself with `Q6T33_RUN_DONE exit=<n>` (0 ok, 10 no records, 20 preflight, 30 interrupted, 31 error).
Several variants in one launch also work (`run_q6t33.cmd E_BASE E_NOMSG`); the report splits them by the plot records.

Cleanup runs at the end of every run, also after an error; after a crash run `cleanup_q6t33.cmd` (deletes
`corruption\Mods\Q6T33Probe` only when it holds `Q6T33_PROBE_MARKER.txt`). No retail file is changed.

## Output and analysis

`out\<yyyymmdd-hhmmss>-<variants>\`: `gates.log`, `records.log`, `shots\`, `report.txt`, `run.json`. To read several
runs together: `run_q6t33.cmd --report-only out\<run1> out\<run2> ...` (or `q6t33_parse.py`). Per campaign the
report gives the declare, ready and end-screen frames, every gate interval in between, and a verdict:

- `NOT HELD`: the end screen came at ready (30 frames after the declare): the port's rule.
- `HELD n gframes after ready: Bink open (GUI movie Light_Loop; advisor hint text shown)`: the hint text gate.
- `... encyclopedia pop-up shown`, `message box up`, `hold counter > 0`: the other candidates.

The findings go to `docs/re/Q6_T27_ELIM_END_TIMING.md`.

## Offline check

`toolchain\venv\Scripts\python.exe -I tools\probe_mod\q6_t33_elim_end\selftest_q6t33.py` (needs
`common\build_lua50.cmd`'s `lua50.exe`, built on first use): gate decoding on a fake memory image, the mod built from
`extracted\foc_data_text.zip`, every variant's Lua run under stock Lua 5.0.2 with a stub galactic engine, and the
analyser on a fake D (NOT HELD) and E (held by a Light_Loop Bink) run. Printed **Q6T33 SELFTEST OK** on 2026-11-16.

## Limits

- The poller samples every few ms from another process: a gate open or closed for less than one sample can be missed,
  and the frame of a change is exact to one frame. It reads, never writes.
- The end-screen-shown flag (`0xA61E08`) and the GUI object (`0xB27F60`) come from a decompile read, not a trace; if
  `ends` never turns 1, the heartbeats stopping is the fallback end time.
- Screenshots are a screen copy of the window's client area: keep the game window in front and uncovered.
