# Tools index: what every worker may use

Default workflow (Gabriel, 2026-11-15; `DECISIONS.md`, "Default tool access"): the main session, every agent in
`.claude/agents/` and every subagent spawned with the `Agent` tool may use **every** tool listed here whenever its task needs
one, without asking first. Limits come from the rules (CLAUDE.md, clean-room, determinism, landing), not from tool lists. The
only per-agent limit left is `reviewer`, which never edits files.

**Exception: the retail RE and tracing tools** (ReVa / Ghidra, REA, mcp-windbg, TTD, WinDbg / cdb, Frida, and any disassembler or debugger pointed at
retail `StarWarsG.exe`, `llvm-objdump` and `lldb` included; `PROBES.md`, "RE and
retail-tracing tools") are not for every worker. Clean-room rule: a worker that writes engine code never runs them and never
reads what they print. Only the `re-analyst` agent uses ReVa, mcp-windbg and REA (the two MCP servers are registered on it
alone); a probe or RE / doc task may use TTD, cdb and Frida. Everyone else asks through the `re-question` skill, which starts
`re-analyst` and gets back behaviour written in `docs/re/`. A TTD recording also needs a person to approve its Windows admin
prompt (UAC), so it never runs unattended. `tools\tc.cmd` does not put `toolchain\re` on PATH.

How it is wired:
- Agent frontmatter has no `tools:` allowlist, so an agent inherits every tool of the session (PowerShell, LSP, web search /
  fetch, built-in browser, computer use, MCP servers, `Skill`). `reviewer` uses `disallowedTools: Write, Edit, NotebookEdit`.
  A new agent follows the same pattern: no `tools:` line unless it must be read-only.
- A `SessionStart` hook (`.claude/settings.json`) runs `tools\worktree\worktree_tool.py link --quiet`: a worktree made by the
  app gets the `Data`, `extracted`, `toolchain` and `roadmap-store` junctions automatically (it never replaces a folder already
  there and does nothing in the main checkout).
- `.claude/settings.json` pre-approves the build scripts, read-only git, the build summary and the roadmap read commands, so
  background agents do not stall on a prompt. Destructive actions keep their pre-approved wrappers (`docs/BUILD.md`).

## Project scripts (root and `tools\`)

| Need | Use |
|---|---|
| Build and test a few modules (inner loop) | `build_agent.cmd msvc "<modules>" [changed\|regex\|all\|none]`; clang the same for sim code |
| One module in its own tree | `build_module.cmd <module> ["module;list"]` |
| Everything / clang / sanitizers / pre-merge gate | `build_msvc.cmd`, `build_clang.cmd`, `build_asan.cmd`, `build_gate.cmd` |
| Fuzz targets | `build_fuzz.cmd`, `tools\fuzz\run_fuzz.py` |
| Launcher (Rust) | `build_launcher.cmd`; by hand `launcher\cargo.cmd <args>` |
| Read a build result | `python tools\lint\build_summary.py <logs>` (the scripts print it) |
| Which tests cover my change | `python -I tools\lint\changed_tests.py` |
| Static checks | `python tools\lint\run_tidy.py` (clang-tidy), `check_mod_names.py`, `check_string_literals.py` (hook) |
| Goldens | `tools\regen_goldens.cmd` (skill `regen-goldens` first) |
| Parity against retail baselines | `tools\parity\parity_harness.py` (`tools/parity/README.md`) |
| Benchmarks, worker sweeps, build times | `tools\benchmark\` (`run_suite.cmd`, `worker_sweep.py`, `build_times.py`) |
| Land on `dev` | `tools\land_on_dev.cmd` |
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
| LLVM 20.1.8, all of it: `clang`, `clang-cl`, `clangd`, `clang-tidy`, `clang-format`, `clang-query`, `clang-include-cleaner`, `lld` / `lld-link`, `lldb` (debugger; does not start yet: exits 0xC0000135, a DLL it needs, probably Python's, is missing), `llvm-symbolizer`, `llvm-pdbutil`, `llvm-objdump`, `llvm-nm`, `llvm-readobj`, `llvm-cov` / `llvm-profdata` (coverage), `llvm-mca` (instruction throughput), `scan-build` (static analyzer) | `toolchain\llvm\bin` (`toolchain\5_portability_compilers.cmd`) |
| `dxc` | `toolchain\dxc\bin\x64` |
| `sccache` | `toolchain\sccache` |
| `cargo`, `rustc`, `rust-analyzer` | `toolchain\rust\cargo\bin` |
| `git` (MinGit) | `toolchain\mingit\cmd` |
| apitrace 14.0: `apitrace` (`trace`, `dump`, `diff`, `replay`), `d3dretrace`, `qapitrace` (viewer) | `toolchain\capture\apitrace-14.0\bin` (`toolchain\6_capture_tools.cmd`; last on PATH) |

apitrace records the graphics API calls a program makes, not its code, so every worker may use it, including on a capture of
retail (the G1 capture; RenderDoc is out of scope, `DECISIONS.md`). Running retail to make a new capture is a probe run
(`probe-kit`; findings go to `docs/re/`). LLVM tools are for the port's own binaries: disassembling or debugging retail
`StarWarsG.exe` with `llvm-objdump`, `lldb` or any other tool is RE work and falls under the exception above.

Builds still go through the build scripts, never a hand-run cmake configure (`BUILD.md`). A program that is missing gets a
pinned setup step in `toolchain\2_setup.cmd` (rule 5), then a row here.

## Skills (`.claude/skills/`; the `Skill` tool, or read its `SKILL.md`)

| When | Skill |
|---|---|
| Starting / finishing a roadmap group | `start-group`, `finish-group` |
| Full build; drive a build to green | `build`, `green-fast-tier` |
| Ticking the roadmap outside `finish-group` | `phase-status` |
| Sim code, parallel code, CRC difference | `determinism-check` |
| Unexplained failure, before a second fix attempt | `diagnosing-bugs` |
| Golden drift | `regen-goldens` |
| Original-game behaviour not in `docs/re/` | `re-question` (binary), then `probe-kit` (running game) |
| Question about a technique, API or format | `research` |
| Editing CLAUDE.md, a skill, an agent or memory | `writing-for-agents` |
| Review of a diff | `/code-review`, `reviewer` agent |

## Agents (`.claude/agents/`)

`lane` (one roadmap group end to end), `engine-module` (one-folder side task), `integrator` (merge lanes, full build),
`reviewer` (read-only review), `re-analyst` (binary questions, writes `docs/re/`). Built-in `general-purpose`, `Explore` and
`Plan` agents also have every tool (Explore and Plan without editing); name the skill or script in their prompt when it matters.

## Session tools

LSP (clangd, `eaw-clangd` plugin), web search / fetch, the built-in browser, computer use (retail probe runs only; never the
port's `alamo_game.exe`), ReVa and windbg MCP (`re-analyst` only, see the exception above), scheduled tasks and `/loop`. Approval rules for outward or
irreversible actions (pushing anywhere but `dev` via `land_on_dev.cmd`, merging PRs, deleting) are unchanged.
