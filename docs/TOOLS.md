# Tools index: what every worker may use

Default workflow (Gabriel, 2026-11-15; `DECISIONS.md`, "Default tool access"): the main session, every agent in
`.claude/agents/` and every subagent spawned with the `Agent` tool may use **every** tool listed here whenever its task needs
one, without asking first. Limits come from the rules (CLAUDE.md, the no-copy guard on decompiled code, determinism, landing), not from tool lists. The
only per-agent limit left is `reviewer`, which never edits files.

**Retail RE tools are for every worker too** (Gabriel, 2026-11-15; `DECISIONS.md`, "Retail RE access for every worker"): REA,
WinDbg / cdb, Frida, TTD replays, any disassembler or debugger pointed at retail `StarWarsG.exe`, and the decompiled output in
`re_reference/` may be used directly, also while writing engine code (`PROBES.md`, "RE and retail-tracing tools", says where each
lives). **One guard:** never copy decompiled or disassembled code into `engine/`, `tools/` or `docs/` (no pasted pseudo-code, no
line-by-line transliteration); write engine code in the port's own words and structure, and put the behaviour you learned into
the `docs/re/` spec in words and tables. Limits that are technical, not rules: ReVa (Ghidra MCP) is registered only on the
`re-analyst` agent because Ghidra allows one ReVa session at a time: a main session starts `re-analyst`, a subagent hands it a
questions file (`re-question`), and everyone can use REA, which reads the same analysis from many sessions at once. A TTD recording needs a person to approve its
Windows admin prompt (UAC).

How it is wired:
- Agent frontmatter has no `tools:` allowlist, so an agent inherits every tool of the session (PowerShell, LSP, web search /
  fetch, built-in browser, computer use, MCP servers, `Skill`). `reviewer` uses `disallowedTools: Write, Edit, NotebookEdit`.
  A new agent follows the same pattern: no `tools:` line unless it must be read-only.
- A `SessionStart` hook (`.claude/settings.json`) runs `tools\worktree\worktree_tool.py link --quiet`: a worktree made by the
  app gets the `Data`, `extracted`, `toolchain` and `roadmap-store` junctions automatically (it never replaces a folder already
  there and does nothing in the main checkout).
- `.claude/settings.json` pre-approves the build scripts, every project script under `tools\`, the roadmap tool and git's read
  and local steps, so lanes run unattended (`docs/WORKFLOW_TIPS.md`, "Unattended runs"). Destructive actions keep their
  pre-approved wrappers (`docs/BUILD.md`).

## Project scripts (root and `tools\`)

| Need | Use |
|---|---|
| Build and test a few modules (inner loop) | `build_agent.cmd msvc "<modules>" [changed\|regex\|all\|none]`; clang the same for sim code |
| One module in its own tree | `build_module.cmd <module> ["module;list"]` |
| Everything / clang / sanitizers / pre-merge gate | `build_msvc.cmd`, `build_clang.cmd`, `build_asan.cmd`, `build_gate.cmd` |
| Fuzz targets | `build_fuzz.cmd`, `tools\fuzz\run_fuzz.py` |
| Launcher (Rust) | `build_launcher.cmd`; by hand `launcher\cargo.cmd <args>` |
| Read a build result | `python tools\lint\build_summary.py <logs>` (the scripts print it) |
| Test tiers and budget | `python -I tools\lint\test_tiers.py regen` / `check`; flaky tests: `flaky_retry.py --report` |
| Writer checkouts (wr4) | `python -I tools\gate\writer_setup.py create` / `warm` / `status` / `sync <checkout>`; path guard hook `tools\lint\path_guard.py` |
| Which tests cover my change | `python -I tools\lint\affected_tests.py <tree> <out file>` (build graph; `--evaluate N`) |
| Build-tree name or exe path | `python -I tools\lint\build_trees.py name agent wt msvc`, `exe alamo_headless.exe` (Python: `import build_trees`) |
| Static checks | `python tools\lint\run_tidy.py` (clang-tidy), `check_mod_names.py`, `check_limits.py` (limit tags, §8), `check_mod_system.py` (mod-system shims, ums4), `check_string_literals.py`, `compile_check.py` (edit-time hook, both compilers; `--repro`) |
| Goldens | `tools\regen_goldens.cmd` (skill `regen-goldens` first; `--mods` for the mod goldens of `tests/goldens/mods`) |
| Grade every installed mod and stack (G0-G3) | `tools\modcorpus\run_corpus.cmd` (`tools/modcorpus/README.md`); weekly ASan sweep `tools\modcorpus\asan_sweep.cmd` |
| Parity against retail baselines | `tools\parity\parity_harness.py` (`tools/parity/README.md`) |
| Benchmarks, worker sweeps, build times | `tools\benchmark\` (`run_suite.cmd`, `worker_sweep.py`, `build_times.py`) |
| Workflow figures (builds per day, failed share, test hours, golden-touching and merge commits) | `python -I tools\analysis\workflow_stats.py --from <day> [--to <day>] [--markdown]`; baseline in `docs/PERFORMANCE.md`, "Workflow baseline" |
| Nightly dev gate: install, missed nights | `tools\install_gate_task.cmd`; history `logs\gate_history.txt`, missed nights `python -I tools\lint\gate_nights.py missed logs\gate_history.txt` (`docs/BUILD.md`, "Scheduled gate") |
| Land on `dev` | `python -I tools\gate\land_queue.py submit`, then `status <id>` (the verifier lands it; `docs/BUILD.md`, "Landing queue and verifier"); fallback `tools\land_on_dev.cmd` until wr2 t04 is applied |
| Computed goldens: behaviour check, scenario results, benchmark scenarios | `python -I tools\goldens\behaviour_check.py commit\|range\|workers\|compilers`, `scenario_results.py`, `bench_scenarios.py` (`docs/status/golden-checkpoints.md`, "Computed results") |
| Worktrees | `tools\new_worktree.cmd <name>`, `worktree_tool.py link`, `tools\clean_worktrees.cmd` |
| Stop a hung test exe / delete a merged branch | `python -I tools\worktree\stop_tree_procs.py`, `delete_merged_branch.py` |
| PR check without hosted CI | `tools\pr_check.cmd` |
| Test build for Gabriel | `tools\testbuild\package_testbuild.cmd` |
| Game archives and formats | `tools\meg\megtool.py`, `tools\formats\`, `tools\shaders\` |
| Retail binary questions (RE tasks only) | `tools\re\` through the `re-analyst` agent |
| In-game retail probes | `tools\probe_mod\<kit>\` (skill `probe-kit`), run with computer use |
| Roadmap | `python roadmap-store\roadmap_store.py <sync\|claim\|write\|backlog\|close-group\|lint\|batch>` |

## Toolchain programs: `tools\tc.cmd <program> [args]`

Nothing in `toolchain\` is on the system PATH (rule 5). `tools\tc.cmd` sets the MSVC / Windows SDK environment and puts the
toolchain folders on PATH for one command, so any program there runs by name. `tools\tc.cmd --where` lists the folders.
From PowerShell use `& .\tools\tc.cmd <program> <args>`, or `--%` on a line of its own for arguments with `;` or quotes.

| Program | Folder |
|---|---|
| `cl`, `link`, `lib`, `dumpbin`, `fxc`, `rc` | `toolchain\msvc` (MSVC 14.51, Windows SDK) |
| `python`, `cmake`, `ninja` | `toolchain\venv\Scripts` |
| LLVM 20.1.8, all of it: `clang`, `clang-cl`, `clangd`, `clang-tidy`, `clang-format`, `clang-query`, `clang-include-cleaner`, `lld` / `lld-link`, `lldb` / `lldb-dap` (debugger; `tc.cmd` gives it the Python 3.10.11 its `liblldb.dll` needs, from `toolchain\16_lldb_python.cmd`), `llvm-symbolizer`, `llvm-pdbutil`, `llvm-objdump`, `llvm-nm`, `llvm-readobj`, `llvm-cov` / `llvm-profdata` (coverage), `llvm-mca` (instruction throughput), `scan-build` (static analyzer) | `toolchain\llvm\bin` (`toolchain\5_portability_compilers.cmd`) |
| `dxc` | `toolchain\dxc\bin\x64` |
| `sccache` | `toolchain\sccache` |
| `cargo`, `rustc`, `rust-analyzer` | `toolchain\rust\cargo\bin` |
| `git` (MinGit) | `toolchain\mingit\cmd` |
| apitrace 14.0: `apitrace` (`trace`, `dump`, `diff`, `replay`), `d3dretrace`, `qapitrace` (viewer) | `toolchain\capture\apitrace-14.0\bin` (`toolchain\6_capture_tools.cmd`) |
| Retail RE tools (every worker; the no-copy guard above applies): WinDbg `cdb`, `kd`, `DbgX.Shell` (GUI); TTD `TTD` (recording needs administrator rights, which `tc.cmd` does not raise: record with `tools\re\ttd_record.cmd`, one UAC prompt; replay with `cdb -z <trace>.run`); Frida `frida`, `frida-trace`, `frida-ps`, ...; REA as `rea` (`rea call`, `rea tools`, `rea status`) | `toolchain\re\windbg-1.2611.4001.0`, `toolchain\re\ttd-1.11.611.0`, `toolchain\re\venv\Scripts`, `tools\re\rea.cmd` (`toolchain\14_re_tools.cmd`, `15_rea.cmd`); last on PATH, so their DLLs and Python 3.14 shadow nothing |

apitrace records the graphics API calls a program makes, not its code, so every worker may use it, including on a capture of
retail (the G1 capture; RenderDoc is out of scope, `DECISIONS.md`). Running retail to make a new capture is a probe run
(`probe-kit`; findings go to `docs/re/`). LLVM tools work on the port's binaries and on retail `StarWarsG.exe` alike; on retail
the no-copy guard above applies.

Pre-approved in `.claude/settings.json` (2026-11-15), so they run without a prompt in the default permission mode too: `tc.cmd`
with any program, except the `ask` rules, which always prompt: `frida*` and `TTD` (they attach to or record processes), `cargo`,
`cmake` and `ninja` (builds go through the build scripts).

Builds still go through the build scripts, never a hand-run cmake configure (`BUILD.md`). A program that is missing gets a
pinned setup step in `toolchain\2_setup.cmd` (rule 5), then a row here.

## Skills (`.claude/skills/`; the `Skill` tool, or read its `SKILL.md`)

| When | Skill |
|---|---|
| **Any request to complete roadmap work** ("complete all tasks under AUD1", a box, "next ready group") | this session works it in its own checkout: `start-group`, the work, `finish-group` (CLAUDE.md "Parallel work", since 2026-11-21). `run-group` (lane coordinator, up to three `lane` sessions) only when Gabriel types it |
| Starting / finishing a roadmap group | `start-group`, `finish-group` |
| Full build; drive a build to green | `build`, `green-fast-tier` |
| Ticking the roadmap outside `finish-group` | `phase-status` |
| Sim code, parallel code, CRC difference | `determinism-check` |
| Unexplained failure, before a second fix attempt | `diagnosing-bugs` |
| Golden drift | `regen-goldens` |
| Original-game behaviour not in `docs/re/` | `re-question` (binary), then `probe-kit` (running game) |
| Question about a technique, API or format | `research` |
| Editing CLAUDE.md, a skill, an agent or memory | `writing-for-agents` |
| Review of a diff | `/code-review` (correctness bugs), `spec-review` (standards + the group's spec), `reviewer` agent |
| Designing a module's interface; no seam for a regression test | `codebase-design` |
| Architecture review, refactoring opportunities | `improve-codebase-architecture` (proposes and files; refactors are tier 3) |
| Filing findings, cleaning the backlog or inbox | `triage` |
| Writing code or a regression test test-first | `tdd` |
| Splitting a plan, spec or finding into tasks or a new group | `to-tickets` |
| Feeling out a rule / state model, or comparing UI variants, before real code | `prototype` |
| Stress-testing a plan with Gabriel; recording terms and decisions | `grill-with-docs` |

`codebase-design`, `improve-codebase-architecture`, `triage`, `tdd`, `to-tickets`, `prototype`, `grill-with-docs` and `spec-review` (and parts of `diagnosing-bugs`) are adapted from
`mattpocock/skills` (MIT; `.claude/skills/LICENSE-mattpocock-skills.txt`).

## Agents (`.claude/agents/`)

`lane` (one roadmap group end to end), `engine-module` (one-folder side task), `integrator` (merge lanes, full build),
`reviewer` (read-only review), `re-analyst` (binary questions, writes `docs/re/`). Built-in `general-purpose`, `Explore` and
`Plan` agents also have every tool (Explore and Plan without editing); name the skill or script in their prompt when it matters.

## Session tools

LSP (clangd, `eaw-clangd` plugin), web search / fetch, the built-in browser, computer use (retail probe runs only; never the
port's `alamo_game.exe`), ReVa and windbg MCP (registered on `re-analyst`; main sessions start it, subagents use `re-question`), scheduled tasks and `/loop`. Approval rules for outward or
irreversible actions (pushing anywhere but `dev` via `land_on_dev.cmd`, merging PRs, deleting) are unchanged.
