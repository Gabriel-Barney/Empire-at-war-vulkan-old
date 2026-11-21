# EaW:FoC engine rewrite — guide for Claude agents

C++20 rewrite of the Star Wars: Empire at War: Forces of Corruption (Alamo) engine, written in its own words (decompiled
retail code may be read, never copied):
headless multithreaded simulation core first, then a Vulkan 1.3 renderer. Any Claude agent
(Claude Code CLI, desktop, cloud session or subagent) working in this folder follows this file.

## Where things are
| Path | What |
|---|---|
| `engine/` | Engine. One static lib per module: Phase 2 `core`, `vfs`, `data`, `jobs`, `galactic`, `apps`; Phase 3 `assets`, `rhi`, `materials`, `shaderxlat`, `terrain`, `render` (Vulkan 1.3 renderer, `alamo_render.exe`); Phase 4 `objects` (game object model), `anim`, `commands`, `tactical`, `lua`, `ai`, `ui`, `audio`, `video`; header-only `test_support` (list: `engine/CMakeLists.txt`) |
| `launcher/` | Command Center (Rust / egui): launcher and control panel for `alamo_game.exe`. Not part of `engine/` or the CMake build: `build_launcher.cmd`, toolchain `toolchain\rust` (`docs/DECISIONS.md`, "Command Center launcher") |
| `docs/README.md` | **Index of all documentation (start here).** One place per kind: `DECISIONS.md` (binding decisions), `BUILD.md` (build / test / gate), `PERFORMANCE.md` (measurements), `PROBES.md` (in-game probe index), `ROADMAP_PROTOCOL.md` (roadmap rules); status lives only on the roadmap |
| `docs/PHASE4.md` | Phase 4 step map (what each step is, where its code is) and the open per-step port choices. Finished-work notes: `docs/PHASE4_HISTORY.md` |
| **Project roadmap** | **The one main roadmap: `roadmap-store/` in this folder (Gabriel, 2026-11-06: one location; before that the page was also tracked in this repo as `ROADMAP.html` and `docs/ROADMAP.html`, both removed). Open `roadmap-store/ROADMAP.html` in any browser; it needs no sign-in and is generated, so never edit it by hand.** Update it with `python roadmap-store/roadmap_store.py write <file>`; ordered stages, boxes and arrows, task groups, tasks, build records and the activity log live in `roadmap-store/data/`, the page layout in `roadmap-store/page_shell.html` (edit outside the `roadmap-data` markers, then `render`). Rules and field list: `docs/ROADMAP_PROTOCOL.md`. `roadmap-store/` is its own git repository (remote `Empire-Vulkan-Brenach`) and is not in worktrees; use the main checkout's copy. When the remote changed, run `python roadmap-store/roadmap_store.py sync` |
| `roadmap-store/` | Own git repository holding the roadmap data, so every account shares one copy. Rules in its `README.md` |
| `docs/PHASE3.md` | Phase 3 (renderer) results, assumptions and reference; no status board. Long details of finished work: `docs/archive/*.zip` (2026-10-30) |
| `tools/` | Phase 1 tools: `meg` (MEG archives, used by the VFS), `formats` (ALO/ALA/…), `shaders`, `re`, `probe_mod`; since then `lint` (build summary, checks), `parity`, `benchmark`, `goldens`, `worktree`, `visual`, `pr_check`, `testbuild`, `analysis`, and the `*.cmd` helpers (`land_on_dev`, `new_worktree`, `clean_worktrees`, `regen_goldens`, `pr_check`, `test_env`) |
| `docs/PHASE2.md` | Phase 2 results and design notes (determinism contract, file resolution order); no status board |
| `docs/re/` | Behavioural specs recovered from the binary (GALACTIC, HOTSPOTS, STRUCTS, SUBSYSTEMS, LUA_API, xml tags) |
| `docs/assets/`, `docs/formats/`, `docs/shaders/` | Asset catalog and format specs |
| `extracted/` | Game text (XML/Lua) zipped: `foc_data_text.zip`, `eaw_data_text.zip` |
| `re_reference/` | Decompiled output. Any worker may read it, also while writing engine code (Gabriel, 2026-11-15). **Never copy decompiled code into `engine/`, `tools/` or `docs/`**: write engine code in the port's own words, behaviour into `docs/re/` in words |
| `toolchain/` | Project-local MSVC 14.51, Windows SDK, CMake, Ninja. Nothing is installed system-wide |
| `logs/` | Build/test logs written by the build scripts |

## Platform and compilers (Gabriel, 2026-10-27; full record in `docs/DECISIONS.md`)
- **Windows x64 only.** Multiplayer, loading the original game's save files and RenderDoc are out of scope. Parity with the original is checked by behaviour, not by matching its CRC / hashes bit for bit; the port's own determinism rule 3 is unchanged. Same outcome, own method (Gabriel, 2026-11-09, "Parity tiers" in `docs/DECISIONS.md`): rules, data / Lua handling and feel match retail exactly; random-driven outcomes match retail's distribution over many seeds; internals (including random-number draw order) are the port's choice, recorded as `[port]` entries.
- **Two compilers: MSVC (ships) and clang (checks).** Every change: MSVC build + tests. Sim code (everything in `engine/` except render, audio, video and UI): also the clang build + tests, including the CRC selfchecks. `engine/jobs` or other threading code: also `build_asan.cmd`. Details and sanitizer limits: `docs/BUILD.md`.
- Shaders are compiled by DXC (`toolchain\dxc`) to SPIR-V; the C++ compiler choice doesn't affect them.

## Build and test (everything else is in `docs/BUILD.md`)
- Main build: `build_msvc.cmd` from the project root (local toolchain, builds everything, runs ctest). Finished when `logs\msvc_configure.txt` ends with `BUILD_DONE`. Every build script ends with `tools\lint\build_summary.py`: read that, not the whole log.
- Clang check `build_clang.cmd`; pre-merge gate `build_gate.cmd` (MSVC + clang + ASan, exits 0 only with 0 warnings); one module `build_module.cmd <module> ["module;list"]`; quick iteration `build_agent.cmd` (fast tier: tests labelled `slow` are skipped unless you pass a regex or `all`; while iterating pass `changed` to run only the changed modules' fast-tier tests, and run the whole fast tier before landing). Every build script records its result on the roadmap (`docs/ROADMAP_PROTOCOL.md`, "Build records"). An identical failing rerun is refused (exit 3; `ALAMO_RERUN=1` overrides), and the summary separates real failures from golden drift (`docs/BUILD.md`).
- Data-driven tests read `ALAMO_FOC_DATA` / `ALAMO_EAW_DATA` and skip when unset, so a pass without them proves less; the scripts default them (`tools\test_env.cmd`) and end with a `skips:` line.
- Tests run in parallel (`ctest -j`): two ctest entries must not share a fixed `%TEMP%` fixture or output path (separate paths, or a `RESOURCE_LOCK`); a flaky failure in one tree is a possible clash first. Long determinism tests get one ctest entry per worker count (`alamo_add_worker_count_check`), never `--check-workers`. Label any test over about 5 s `slow`. At most 2 test runs go at once on this machine (`tools\lint\test_slot.py`; a third waits). A hung test exe of yours: `python -I tools/worktree/stop_tree_procs.py`; merged `claude/*` branches on origin: `python -I tools/worktree/delete_merged_branch.py <branch>` (both pre-approved, never `taskkill /IM` or `git push --delete`; `docs/BUILD.md`, "Pre-approved commands").
- Line endings: text files are LF (`.gitattributes`; goldens are compared byte for byte), `.cmd` / `.bat` / `.ps1` are CRLF.
- Version control: the project is a git repository; commit before and after each agent task and push; `roadmap-store/` is its own repository. Remotes and new-machine setup: `docs/BUILD.md`.
- Branches (Gabriel, 2026-11-06; full record in `docs/DECISIONS.md`): **`dev` is the development branch (and GitHub's default branch); `main` is the released branch.** All work lands on `dev`; no PR to `main` from a task, group or session branch. Gabriel merges `dev` into `main` when he decides it is ready: `build_gate.cmd` on `dev`'s tip, then `gh pr create --base main --head dev`. Never push `dev` with a failing MSVC build or test. The app still starts every session on its own `claude/...` branch; treat it as scratch. **Landing (wr2, 2026-11-21): only the verifier pushes `dev`.** Commit, run the builds and tests the change needs, then `python -I tools/gate/land_queue.py submit` and go on working: the verifier (`tools/gate/verifier.py`, per-user task AlamoVerifier every 5 minutes) places the entry on `dev`'s tip, runs the MSVC fast tier, clang when sim code changed, the slow checks of changed goldens and the `Behaviour:` check, and fast-forwards `dev`; a failure or conflict rejects the entry and `dev` does not move. `land_queue.py status [<id>]` shows the state and the summary file; rejected: fix, commit, submit again. **Every commit that touches sim code ends with a `Behaviour:` trailer**, `Behaviour: none` or the CRC system columns it changes (`docs/status/golden-checkpoints.md`, "Computed results"); the verifier rejects a sim commit without one. Details: `docs/BUILD.md`, "Landing queue and verifier". `tools\land_on_dev.cmd` stays the fallback until wr2 t04's settings edit is applied.
- Tick profiler: `alamo_headless ... --profile`; wall-clock only, never let sim code read a profiler value. Baselines: `docs/PERFORMANCE.md`.
- C++ symbol questions (where defined, who calls it, what a header declares): the `LSP` tool before Grep or reading whole files; ask from a `.cpp` use for callers. Once per worktree, before opening C++ files: `build_agent.cmd clang all configure` (5 s; `.clangd` reads its `compile_commands.json`). Grep stays the tool for strings, Lua, XML, Python and docs.

## Rules
1. Zero warnings at MSVC `/W4 /permissive-` and clang `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`. Explicit narrowing casts; no shadowing.
2. No third-party dependencies in `engine/` without asking Gabriel. Approved so far: Vulkan-Headers, DXC, VMA (Phase 3); Lua 5.0.2 (vendored, original semantics), miniaudio, FFmpeg's Bink decoder (2026-10-27); the reference SMAA (2026-11-13, `engine/render/third_party/smaa`); AMD FidelityFX SDK 2.4.0 Vulkan upscaler DLLs for FSR (2026-11-13); Intel XeSS SDK 3.0.2 `libxess.dll` for XeSS (2026-11-13); see `docs/DECISIONS.md`.
3. Determinism (saves, replays and parity checks rely on the per-tick CRC): results read by game logic
   must be bit-identical across thread counts, runs, and the MSVC and clang builds. Parallel jobs write per-index outputs; reductions
   run in index order; no FMA contraction (`/fp:precise`, `-ffp-contract=off`); no iteration over
   pointer-keyed or seeded-hash containers in sim code.
4. Every module has a test exe registered with `add_test`. Keep them passing.
5. Software a task needs is installed project-locally under `toolchain\` with a pinned version and a
   setup step in `toolchain\2_setup.cmd` — never system-wide, no admin, no PATH/registry changes.
6. When a task finishes, update the roadmap (`roadmap_store.py write`, see `docs/ROADMAP_PROTOCOL.md`): it is the only place for status, open items and the
   flowchart (Gabriel, 2026-11-06: one main roadmap; the `docs/PHASE*.md` status boards, their Mermaid flowcharts and the `claude/*.md` project docs are not updated any more).
   Tick the item, update the flowchart box, record results and open items there. Results still go to one place each (`docs/README.md`): decisions in `docs/DECISIONS.md`,
   measurements in `docs/PERFORMANCE.md`, build / test rules in `docs/BUILD.md`, probe findings in `docs/re/` (index: `docs/PROBES.md`), per-step port choices in `docs/PHASE4.md`. There is one copy of the roadmap (Gabriel, 2026-11-06, replacing the 2026-11-04 two-copy rule): `roadmap_store.py write` updates `roadmap-store/` and pushes it, and nothing is regenerated or committed in this repository (`docs/ROADMAP_PROTOCOL.md`, "One location"). Keep edits minimal and factual. When all tasks in a roadmap group
   are finished, mark the group `done` on the roadmap in the same update (Gabriel, 2026-10-30, 2026-10-31): after closing tasks, re-read
   each group you touched, and if it has no `todo` task left set `status: done` (and drop its `preset`) in that same write; a group
   left open with every task closed is a mistake to fix before ending the task. A flowchart box's status is not derived from its
   groups: when the last group in a box is done, also set the box `done` (`nodes/<ID>`, same write). **A group is defined by an
   exit test, not by its task list** (Gabriel, 2026-11-10, replacing the follow-up-group rule): every group has `exit` and a task
   cap (8). **The backlog holds only S3** (Gabriel, 2026-11-15): an S1 or S2 finding (crash, hang, save corruption, tier-1
   difference, failing tier-2 metric, a rule that differs) is a task of the open group that owns that work, in its category's box
   (`roadmap_store.py backlog add ... --sev S1|S2 --to <group>`; none fits or all are full: make a group with an exit test via
   `write`, then `--to` it). An S3 finding (polish, a documented guess) that does not block the exit test goes to the backlog
   (`backlog add ... --from <group>`; rules file it into a category, `inbox` is untriaged), not into a new `…f` follow-up group.
   When the exit test passes, close the group with `roadmap_store.py close-group <group> --by <session> --passed "<evidence>"
   [--to <group>]`: open S3 tasks move to the backlog, open S1 / S2 tasks to the `--to` group, and the group (and its box) is set
   done. Backlog entries become work only when Gabriel picks them (`backlog promote`). Every task carries `sub` (Parity, Engine,
   Process or Research), the heading it is listed under inside its group; set it when adding a task by hand (`tag-tasks --apply`
   fills missing ones). `roadmap_store.py lint` shows what is wrong; rules:
   `docs/ROADMAP_PROTOCOL.md`, "Backlog and group exit tests".
   **Exception: Stage 6 · Parity and full QA pass and Stage 9 · Mod system keep their
   follow-ups** (Gabriel, 2026-11-12, box Q4; box QAFULL since the old Stage 7 was merged in on 2026-11-21; Stage 9, boxes MCX1-MCX6, added 2026-11-17, and Q5, Q6, moved there from
   Stage 6 on 2026-11-19, MODC folded into MCX6 on 2026-11-20): a follow-up of a group in these stages becomes a new task of the group it came from, not a backlog entry (`backlog add ... --from <group>` adds it there
   and reopens the group and its box if they are done; no task cap). Such a group closes only when its exit test passes and no task
   is open (finish or `--drop` each one), and its S3 follow-ups stay tasks too. Tier-3 findings still go to the backlog. Rules: `docs/ROADMAP_PROTOCOL.md`, "Follow-ups
   stay in Stage 6 groups".
   Stage numbers are fixed (each lane's `num`): the finished Stages 1, 3, 10 and 11 were removed on 2026-11-21 (their groups stay
   in the data) and Stage 8 · Port features was emptied into Stages 4 and 5, so the numbers have gaps.
   **Current focus (Gabriel, 2026-11-10, until he lifts it): parity tier 1 and tier 2 work only.** Tasks and backlog entries carry a `tier`; tier-3
   work (internals, draw order, performance, save format) is paused: do not start it, drop it or file it as S3 (`backlog add ... --tier 3`).
   Crashes, hangs and save corruption are still fixed. Exception: the unified mod system refactor (finished; its stage was removed
   2026-11-21) was tier 1 by Gabriel's choice (2026-11-18). Record: `docs/DECISIONS.md` ("Current focus", "Unified mod system refactor"), `docs/ROADMAP_PROTOCOL.md` ("Parity tier").
7. In-game probes: prepare them under `tools/probe_mod/` with run and cleanup scripts, then run them yourself, driving retail
   `StarWarsG.exe` with computer use (Gabriel, 2026-11-14: Claude runs all probe kit runs; `docs/DECISIONS.md`). Never drive
   the port's `alamo_game.exe`. Unknown retail behaviour that the binary can answer: the `re-question` skill first (the
   `re-analyst` agent reads the binary and writes `docs/re/`); a probe kit only for what the running game alone shows.

## Parallel work

**Default workflow for task requests (Gabriel, 2026-11-21; Stage 12 · Workflow redesign, groups wr0-wr6, is replacing the
lane workflow): a request to complete roadmap work ("do aud3", "work the AUDIO box") is worked by the session that got it, in
its own checkout.** One writing session at a time per checkout; it follows the `work` skill (`/work <id>`: plan with the `plan`
skill, build, one `review` pass, roadmap, `land_queue.py submit`; `start-group` / `finish-group` until wr6 removes them). Two
fixed writer checkouts (wr4, `tools/gate/writer_setup.py`): `.claude\worktrees\writer-sim` for sim groups and
`writer-nonsim` for the rest (a group's `writer` field); the path guard hook blocks sim edits from the non-sim checkout and
any edit in the verifier's. It starts no coordinator, lanes or lane sessions; read-only help
(`re-analyst`, `reviewer`, research, exploration) still runs as subagents. Until wr1 and wr2 are on `dev`, parity and mod groups
are started only when Gabriel names one; Stage 12 groups go first (crashes, hangs and save corruption are still fixed). The
`run-group` coordinator still works and runs only when Gabriel types `/run-group`. What each Stage 12 group replaces, and when:
`docs/DECISIONS.md`, "Workflow redesign".
**Unattended runs (Gabriel, 2026-11-15): Gabriel answers once, then walks away.** Ask every approval and open decision at the
start; after that take the documented default instead of asking and park only what truly needs him. Command spellings that run
without a prompt, and the wrappers that replace destructive commands: `docs/WORKFLOW_TIPS.md`, "Unattended runs".

Under `/run-group`, each group (or share) is a `lane` session (`.claude/agents/lane.md`) in its own worktree. Lanes own a group,
not a folder: two lanes must not edit the same files, and a landing commit names any golden that changed.
A change to `engine/core` public headers or the top-level CMake goes through the integrating agent. Single-module
`engine-module` agents remain for narrow side tasks. Definitions live in `.claude/agents/`.
Project skills (`.claude/skills/`; triggers in each description): `work` / `plan` / `review` / `upkeep` (weekly, wr5) / `run-group` (lane coordinator, only when Gabriel types it) / `start-group` / `finish-group` / `build` / `phase-status`
(the loop), `green-fast-tier` (build, summary, fix iteration after a large edit; speed habits below), `determinism-check` and `regen-goldens` (sim and goldens), `diagnosing-bugs` (an unexplained failure: loop first),
`research` (cited report in its `docs/` home), `re-question` (ask the binary through `re-analyst`), `probe-kit` (retail checks), `writing-for-agents` (editing any file agents read),
`codebase-design` / `improve-codebase-architecture` (deep-module vocabulary; architecture review that files tier-3 proposals), `triage` (file findings, clean the backlog), `tdd` (test-first, red then green), `to-tickets` (split work into tasks / groups), `prototype` (throwaway logic demo or UI variants), `grill-with-docs` (interview Gabriel on a plan, record decisions), `spec-review` (standards + spec axes, in `finish-group` next to `/code-review`;
adapted from `mattpocock/skills`, MIT, 2026-11-15),
`retro` (Gabriel-invoked review of how sessions went; lanes end with a one-line friction note for it).
**Default tool access (Gabriel, 2026-11-15): every worker (main session, agent, subagent) may use every project script,
toolchain program and skill whenever its task needs one, without asking; index `docs/TOOLS.md`. That includes the retail RE
tools (REA, cdb, Frida, TTD replays, `re_reference/`), also while writing engine code; decompiled code is never copied into the
repo. ReVa is registered on `re-analyst` only (one Ghidra session at a time): reach it through that agent (`re-question`), or use REA.** Agents in `.claude/agents/`
have no `tools:` allowlist (they inherit every session tool; only `reviewer` has `disallowedTools` for edits); toolchain
programs run by name through `tools\tc.cmd <program>`; a `SessionStart` hook links the worktree junctions. A subagent does not
inherit the main session's skills, so each agent preloads
the ones it always needs in `skills:` (`lane`: determinism-check, diagnosing-bugs; `reviewer`: determinism-check; `engine-module`:
determinism-check, diagnosing-bugs; `integrator`: build, phase-status, regen-goldens, diagnosing-bugs). A new agent gets `Skill` plus
its preloads; a new skill's `description` must say when to use it, and only `retro` and `run-group` set `disable-model-invocation`
(Gabriel runs them). Agents spawned by the `Agent` tool as `general-purpose` / `claude` already have every tool, `Skill` included; name the skill
in the prompt if it matters.
- Worktrees (2026-11-06): `tools\new_worktree.cmd <name> [--base origin/dev]` creates `.claude\worktrees\<name>` on `claude/<name>`
  from `origin/dev` (default since 2026-11-06), with junctions to `Data`, `extracted`, `toolchain` and `roadmap-store`, and syncs
  the roadmap. Use ONE build tree per worktree (`build_agent.cmd msvc "<modules>"` gives `build-wt-msvc`: the tree name defaults to `wt` since 2026-11-08). After the branch is
  on `dev`, `tools\clean_worktrees.cmd` (dry run; `--apply --only <name>`; `--trees` for idle build trees) removes merged (into
  `origin/dev` or `origin/main`), clean, pushed, idle worktrees, unlinking junctions first. Engine: `tools/worktree/worktree_tool.py` (`--selftest`).
- Speed habits (Gabriel, 2026-11-13; default for every session, full record `docs/WORKFLOW_TIPS.md`, agent form in `.claude/agents/lane.md`):
  `/start-group` sets `/goal` from the group's `exit` test; after a large edit use the `green-fast-tier` skill (`/loop /green-fast-tier <modules>`
  unattended; it never regenerates goldens, stops on golden drift, never polls CI); check `/context` at the start and `/compact` between tasks
  or groups above half full (or start a fresh session); side questions go to `/btw`; fixed commands run in shell mode (`!`); long builds can be
  watched from Remote Control. Read build summaries, not logs.
- Claims (2026-11-06): `python roadmap-store/roadmap_store.py claim <group> --by <session>` before starting a group (24 h, `release` when
  done), `claims` lists them, `batch [N]` prints ready groups whose `touches` do not overlap, for handing out a parallel batch.
  Build records are batched: `record-build` no longer pushes by itself; `write`, `sync` and `flush` do. See `docs/ROADMAP_PROTOCOL.md`.
- Dependencies: branch lanes from `origin/dev`. A lane that needs another lane's work waits until that work is on `dev` (or merges
  that lane's branch in, and lands after it).
- Golden CRC files (2026-11-06): `ALAMO_CRC 2` stores a checkpoint every 64 ticks instead of every tick, so a behaviour change is a
  small diff. `--crc-out` still writes full logs; `--crc-checkpoint N` writes v2. Regenerate after an intended behaviour change with
  `tools\regen_goldens.cmd` and say so in the commit message. Format and workflow: `docs/status/golden-checkpoints.md`. All 20 goldens
  were converted on 2026-11-06 (`tools\goldens\convert_goldens.py --all --in-place --check`; MSVC and clang 174/174).
- PR check without hosted CI (now mainly for the `dev` -> `main` PR): `tools\pr_check.cmd [PR] [--compiler msvc|clang|both] [--tests fast|all] [--post]`.
