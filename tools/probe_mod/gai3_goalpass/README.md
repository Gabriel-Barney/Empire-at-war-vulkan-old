# gai3 goalpass: per-candidate trace of the galactic AI goal passes (gai3 t06)

**Question** (`docs/re/BKG1_RE_ANSWERS.md` §B, probe §B.8): which (goal type, planet) candidates does each AI
player's galactic goal system evaluate in retail, pass by pass, and what are E and the quota of each pass? The port's
`GoalSystem` counts 3..5 more Underworld evaluations per pass than retail (§B.7); this trace gives the per-candidate
list to diff against the port's GoalSystem with the same columns.

No mod: vanilla FoC, Heart of the Maw, human Empire, up to galactic mode frame 700 (about 23 s of galactic time;
covers Naboo corruption at 617). The findings go in `docs/re/BKG1_RE_ANSWERS.md` §B (a results subsection), not here.

## Files

| File | What |
|---|---|
| `goalpass_dbg.py` | The debugger (Windows debug API via ctypes; imports the Win32 pieces and the `Tracer` base of `../cu8h_probes/cu8h_dbg.py`, and `../common/a1mem.py` for the exe identity). Hardware **execute** breakpoints (DR7 R/W 00, LEN 00) in every thread, new threads too: Dr0 RVA 0x6cb511 targeted evaluation, Dr1 0x6cb2ae global evaluation, Dr2 0x6cad26 Commit, Dr3 0x698de0 Weighted_Random_Pick entry (gai3 t05: the FindTarget candidate list, `PICK` lines). Sets the Resume Flag on every hit. Never writes game memory |
| `goalpass_parse.py` | `trace.log` → `report.txt`, `passes.tsv`, `evals.tsv`, `picks.tsv` (run by `goalpass_dbg.py --out-dir` at the end; can be rerun by hand) |
| `run_goalpass.cmd` | Starts the debugger waiting for `StarWarsG.exe`, writes `out\<yyyymmdd-hhmmss>-goalpass\` and `out\latest.txt`, parses at the end |
| `cleanup_goalpass.cmd` | Removes nothing from the game (no mod). Stops a leftover debugger through `out\stop` (it clears its breakpoints and detaches; the game keeps running) |
| `selftest_goalpass.py` | Offline check, no game: encodings, the parser on a synthetic log, and a live run against a fake process that carries the retail instruction bytes at the retail RVAs. Ends with `GOALPASS SELFTEST OK (<n> checks)` |

## Run steps (coordinator, computer use)

1. Steam running; no `StarWarsG.exe` running. Start the debugger first, in its own console:
   `Start-Process "<worktree>\tools\probe_mod\gai3_goalpass\run_goalpass.cmd" -ArgumentList '--non-interactive'`
   It waits up to 30 min for the game; console output goes to `out\run_goalpass.log`.
2. Launch FoC vanilla, windowed, with the shared launcher (the one that survives a Claude shell):
   `toolchain\venv\Scripts\python.exe -I tools\probe_mod\common\launch_foc.py` (no `MODPATH`).
   `tools\probe_mod\cu8h_probes\launch_foc_vanilla.cmd` is the older one-line alternative. The debugger attaches
   within a second (`ATTACH` / `EXE ... known=1` / four `SITE ... ok=1` / `ARM` in `out\<run>\trace.log`).
3. Esc through the intro movies. Single Player > Galactic Conquest > **Heart of the Maw** > **EMPIRE**, difficulty
   **Medium**, other settings default, **Start**.
4. Close the story box when it appears. Then hands off: no clicks, no pause, no speed change. Keep the game window in
   front (it pauses when it loses focus).
5. Wait until the debugger stops by itself: `trace.log` gets `STOP why=galactic_frame_700` and `DETACH ok=1`, and
   `out\run_goalpass.log` ends with `GOALPASS_DONE exit=0`. The game keeps running.
6. Quit without saving: Esc > Quit Game > Yes, Exit Game.
7. If anything went wrong: `cleanup_goalpass.cmd --no-pause` (stops a leftover debugger; it never touches the game
   folder). Stop early by hand: create `out\stop`.

Options (pass after `--non-interactive`): `--stop-galactic-frame N`, `--arm-file PATH` (arm only while PATH exists;
default is to arm at once: the galactic goal system does not run at the main menu, and any hit outside the galactic
mode is logged and left out of the report), `--label NAME`, `--wait-process-sec N`.

## What to send back

The run folder named in `out\latest.txt`: `report.txt`, `passes.tsv`, `evals.tsv`, `picks.tsv`, `trace.log`, and
`out\run_goalpass.log`.

## Reading the report

- Header: exe identity, site byte checks (all four must be `ok=1`; otherwise the debugger refuses to arm), stop
  reason, hit counts. `ignored (not galactic mode ...)` counts hits in a tactical mode (none expected).
- Players and goal systems: one goal system (`gs` = rdi) per AI player; owner id = `[[[gs+0x28]+0x28]+0x28]+0x4C`
  [M], with the chain pointers in each `GSYS` / `HIT` line so a wrong chain can be fixed offline; the faction comes
  from a std::string search of the player's faction object (as `a1mem.find_faction_name`).
- Per owner, one row per pass: Commit frame (global, mode), E logged by Commit (`[rdi+0x188]` before its reset), the
  evaluations seen since that goal system's previous Commit (T / G), delta, quota (`[rdi+0x178]`, the quota the pass
  ran at), the mode frames of its first and last evaluation. **E check**: every pass after the first must have delta
  0. The first pass is marked `first`: Init seeds E with S × max(1, T) (§B.2) and hits before arming are missing.
- `evals.tsv`: each evaluation in order, with the source cursor (`+0x17C`, the index in 0x60f6c0's (goal type,
  function) list), target cursor (`+0x180`), E before the increment, goal type, function, planet and index byte.
- Name tables and "source order": every goal type, function and planet pointer with the name found and how. When a
  goal type has no name, use the source order list: (owner, source cursor) → goal type pointer matches the order of
  the player's goal-function sets in the XML (§B.8 fallback).

## Offsets this kit relies on

| What | Offset | Source |
|---|---|---|
| Sites, registers | 0x6cb511 / 0x6cb2ae `inc [rdi+0x188]`, 0x6cad26 `call 0x6c8710`, 0x698de0 `push rbx; sub rsp, 0x30` (Weighted_Random_Pick: rcx = vector of 16-byte {target, float cum} entries, planet = [target+0x28], `A2M_T08_T10_T14_RE.md` t14.3); r14 = (goal type, function) pair, r15 = [r14], r12 = [r14+8], rsi = target | §B.3 / §B.8 [H]; re-read with `dumpbin /disasm` of the retail exe, 2026-11-15 |
| Goal system | +0x178 quota, +0x17C source cursor, +0x180 target cursor, +0x188 E | §B.1 [H] |
| Owner | `[[[rdi+0x28]+0x28]+0x28]+0x4C` | §B.8 [M] (the chain the slice 0x6cb120 reads) |
| Goal type name | MSVC std::string at goal type +0x00; +0x20 application flags, +0x28 category, +0x2C reachability | ctor 0x6b7c70 copies the type's name string into +0x00 of the new 0xb8-byte object (decompile read 2026-11-15: [H] for the field, [M] that it is the XML `Name`); +0x20 / +0x28 / +0x2C: §B.3, A2H_GALACTIC_AI_RE, A2L_T03 |
| Planet | object = target +0x28; name = std::string at (object +0x298 type) +0xF8; index byte object +0x335 | §B.4 [H] (+0x28); `a1mem.py` OBJ_TYPE / TYPE_NAME (Q2 runs) for the name; §B.8 [M] for +0x335 |
| Function name | none known: `cands=` only, else the pointer | — |
| Frames | global frame 0x140b0a320; GMM 0xb153e0 (+0x1C mode type, +0x38 current mode, mode +0x10 frame) | `cu8h_dbg.py` / `a1mem.py` (MAIN_LOOP) |

Every first sight of a goal type, function or planet also logs `cands=` (each std::string or pointer to ASCII in the
first 0x200 bytes, with its offset) and a hex dump, so a wrong name offset can be corrected from the log alone.

## Known limits

- Retail's galactic random seed cannot be chosen: compare the pass structure (E, quota, candidate order) against a
  port run from the same start, and expect target-dependent differences once the two games diverge.
- The stop is checked on every hit and polled every 0.1 s, so a few hits past frame 700 can follow; the last pass of
  each player is usually `open`.
- Each hit stops the game for a debug event (a few hundred µs). The game is frame-driven, so this slows it but does
  not change its frames.
- If the main-menu battle did run goal-system code, those hits would appear as `ignored` (mt 1 / 2); with many of
  them use `--arm-file` and create it before Start.

## Dr3: FindTarget candidates (gai3 t05, 2026-11-16)

The Rebel slice plan's `FindTarget(TechForce, "Is_Good_Slice_Target", "Enemy", 0.1)` runs at about mode frame 97 in
the hands-off Empire run (`A2M_T07_REBEL_SLICE_RUN.md`, run 1, target Nal Hutta), inside this kit's window. Its call
into Weighted_Random_Pick lists every candidate that passed the Enemy filter, in list order with the best at the end.
`picks.tsv` / the report's pick section show the count and names: 8 entries (all Empire and Underworld planets) means
no filter beyond t14.2 and the never-picked Corellia / Kashyyyk / Shola are the seed-0 draws; fewer entries name the
filter. Other callers of the same function (Underworld plans, the tactical AI) are logged too, with their caller RVA.
