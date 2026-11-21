# Context-compression tools for Claude Code (output trimmers, repo maps, packers, prompt compression), as of 2026-11-19

Scope note: repo stats were pulled with `gh api repos/<owner>/<repo>` and `.../commits?per_page=1` on 2026-11-19 (UTC). "pushed" = any branch push; "last commit" = newest commit on the default branch. Vendor / README numbers are marked **claim**; numbers the project produced with a published, reproducible harness are marked **claim (self-benchmarked)**; nothing below was measured independently by a third party unless marked **measured**. I found no independent third-party measurement for any of these tools.

## Q1. What each tool does and how it saves tokens (with figures)

### Takeaway
Two families matter for this setup: (a) shell-output trimmers that sit in a Claude Code PreToolUse/PostToolUse hook (RTK, tokf, squeez, chop, claude-compress, tersify) or keep output in a side index (context-mode); (b) code-structure tools that cut file re-reading (Aider-style repo maps, repomix `--compress`, LSP-backed Serena). All savings figures are vendor claims or self-benchmarks; the RTK README itself warns that output reduction is not cost reduction (its own pipeline benchmark: 20.2% total cost cut).

### Cited Findings

**Output / command trimmers**
- **RTK (Rust Token Killer), `rtk-ai/rtk`** — "CLI proxy that reduces LLM token consumption by 60-90% on common dev commands. Single Rust binary, zero dependencies" (**claim**). Its hook rewrites shell commands (`git status` -> `rtk git status`); built-in Read/Grep/Glob pass through untouched by default — [RTK README](https://github.com/rtk-ai/rtk)
  - Figures: "up to 90% less command output"; demo session 93.3% overall, `rtk cargo test` 94.2%, `rtk ls` 82.3% (**claim**). Token counts estimated as bytes/4. README warns "a 90% cut in command output is not a 90% cut in cost"; a pipeline benchmark cut total cost 20.2% (**claim, author's own benchmark**). Every bash filter must cut >=20% to ship. `rtk gain` reports local savings — [RTK README](https://github.com/rtk-ai/rtk)
  - Test runners: failures only with assertions, passes collapsed to a count (cargo/npm/pytest/go test); git status/diff/log compacted; ls/tree/grep/rg grouped; linters grouped by rule/file — [RTK README](https://github.com/rtk-ai/rtk)
  - Recovery: "Never blocks" (filter failure -> raw output, exit code passed through); cut output says where full output is; `rtk bypass <cmd>` runs unfiltered; `rtk recall` retrieves full output — [RTK README](https://github.com/rtk-ai/rtk)
- **tokf, `mpecan/tokf`** — "Config-driven CLI tool that compresses command output before it reaches an LLM context"; filters are plain TOML, first match wins; 60–90% reduction (**claim**); per-run byte counts in local SQLite, `tokf gain` — [docs.rs tokf README](https://docs.rs/crate/tokf/0.2.0/source/README.md); [GitHub repo API](https://github.com/mpecan/tokf)
  - README self-critique: reduction percentages on rewriting filters are biased upward; "Treat reduction percentages on rewriting filters as indicative, not as a claim." Pipeline-captured runs (`| tail`) are recorded with no savings attributed — [tokf README](https://github.com/mpecan/tokf)
  - Full output recoverable with `tokf raw <id>` / `tokf raw last`; by default tokf exits 0 and prepends `Error: Exit code N` on failure (`--no-mask-exit-code` disables) — [tokf README](https://github.com/mpecan/tokf)
  - Says it was heavily inspired by rtk — [docs.rs tokf README](https://docs.rs/crate/tokf/0.1.1/source/README.md)
- **context-mode, `mksglu/context-mode`** — "keeps tool output out of your coding agent's context window": commands run via its MCP tools (`ctx_execute`, `ctx_batch`, `ctx_index`, `ctx_search`, `ctx_resume`, `ctx_stats`, `ctx_doctor`, `ctx_upgrade`, `ctx_purge`); full output goes to a per-project SQLite FTS5 index under `~/.context-mode`, the agent gets only what it prints or searches — [context-mode README](https://github.com/mksglu/context-mode)
  - Figures (**claim, self-benchmarked**, reproducible with `cd bench && node run.mjs`, offline): "99.4% of output bytes saved", median over 36 rows with >=10 KB raw output, 5 runs per row, measured as `ctx_bytes` vs `raw_bytes` on macOS arm64, 2026-11-01. By category: Sandbox 99.5%, Retrieval 77.2%, Batch 93.1%, Hook redirect 99.8%. A debugging session 281,261 B -> 3,266 B (~70K -> 817 tokens at 4 B/token). Separately, its paid Gateway: "24.5% fewer dollars pooled over 176 sessions" (author's own account) — [context-mode README](https://github.com/mksglu/context-mode)
  - Session continuity: SessionStart hook prompts `ctx_resume`; a journal logs prompts/edits/commands/errors/decisions; after compaction a state block of at most 2,048 B is injected (a 442,893 B transcript -> 2,019 B block) — [context-mode README](https://github.com/mksglu/context-mode)
- **squeez, `claudioemmanuel/squeez`** — "Hook-based token compressor for 7 AI CLI hosts"; "Up to 95% bash compression, signature-mode for code reads, cross-call dedup, MCP server" (**claim**); compression is "reversible" (dropped content stashed, `squeez_retrieve`); a preservation guard scores identifier ("anchor") survival on >=90%-reduction calls and stashes the verbatim original when below the floor; 46 benchmark scenarios — [squeez README via GitHub API](https://github.com/claudioemmanuel/squeez); newer versions also rewrite Read/Grep/Glob output via PostToolUse — [docs.rs squeez 1.18.0 README](https://docs.rs/crate/squeez/1.18.0/source/README.md)
- **chop, `AgusRdz/chop`** — "CLI output compressor for Claude Code. Reduces token consumption by 50–90%" (**claim**), 52+ commands (git, docker, kubectl, npm, terraform) — [GitHub repo API description](https://github.com/AgusRdz/chop); PreToolUse hook rewrites Bash calls — [ecosyste.ms entry](https://awesome.ecosyste.ms/projects/github.com%2Fagusrdz%2Fchop)
- **claude-compress (PyPI)** — install adds a PreToolUse hook routing each Bash command's output through a local compressor, dedup cache, no network — [PyPI claude-compress](https://pypi.org/project/claude-compress/)
- **tersify (Rust crate)** — PostToolUse hook compresses Bash output — [docs.rs tersify hook.rs](https://docs.rs/crate/tersify/latest/source/src/hook.rs)
- No tool literally named "snip" or "distill" for agent log filtering turned up in search; the closest matches are the tools above — [web search results, see chop/squeez/claude-compress links above]

**Claude Code's own levers (no third-party tool)**
- PostToolUse hooks can replace output: `updatedToolOutput` "Replaces the tool's output with the provided value before it is sent to Claude. The value must match the tool's output shape" and works for built-in and MCP tools; a mismatched shape for built-in tools is ignored. `updatedMCPToolOutput` is MCP-only. PreToolUse `updatedInput` rewrites the tool input (this is how command-rewriting proxies such as RTK/tokf work) — [Claude Code hooks reference](https://code.claude.com/docs/en/hooks)
- `BASH_MAX_OUTPUT_LENGTH`: max characters of bash output read back (default 30000; max 150000); ignored if the `bashOutputMaxChars` setting is set. `CLAUDE_CODE_FILE_READ_MAX_OUTPUT_TOKENS`: file-read limit, default 25,000 tokens. `MAX_MCP_OUTPUT_TOKENS`: default 25000, warning above 10,000; MCP text results >50,000 chars without an annotation are saved to a file. `CLAUDE_AUTOCOMPACT_PCT_OVERRIDE` compacts earlier — [Claude Code env vars](https://code.claude.com/docs/en/env-vars)
- Hook-injected text (`additionalContext`, stdout etc.) is capped at 10,000 characters; beyond that Claude gets a file path plus the first 2,000 characters — [Claude Code hooks reference](https://code.claude.com/docs/en/hooks)

**Build/test tool flags (zero-install)**
- ctest: `--output-on-failure` ("Output anything outputted by the test program if the test should fail", also env `CTEST_OUTPUT_ON_FAILURE`); `-Q/--quiet` ("suppress all the output", log still written with `--output-log`); `--progress` ("short progress output", env `CTEST_PROGRESS_OUTPUT`); `--test-output-size-failed` / `--test-output-size-passed` (byte limits, since 3.4); `--test-output-truncation` tail/middle/head (since 3.24); `--output-junit <file>` (since 3.21); `-O/--output-log <file>` — [ctest(1) manual](https://cmake.org/cmake/help/latest/manual/ctest.1.html)
- Ninja: `--quiet` "don't show progress status, just command output" — [ninja.cc source](https://github.com/ninja-build/ninja/blob/master/src/ninja.cc); `NINJA_STATUS` sets the progress prefix (default `"[%f/%t] "`); `--status FMT` overrides it — [Ninja manual](https://github.com/ninja-build/ninja/blob/master/doc/manual.asciidoc)

**Repo maps / packers / code-structure tools**
- **Aider repo map** — a concise map of the whole git repo listing key classes/functions with signatures, ranked with a graph algorithm (files as nodes, dependency edges) and trimmed to a token budget; `--map-tokens` "defaults to 1k tokens", expanded when no files are in the chat — [Aider repo map docs](https://aider.chat/docs/repomap.html). Aider ships tree-sitter tag queries incl. `cpp-tags.scm`, `c-tags.scm`, `lua-tags.scm`, `python-tags.scm` — [aider/queries/tree-sitter-language-pack](https://github.com/Aider-AI/aider/tree/main/aider/queries/tree-sitter-language-pack)
- **RepoMapper, `pdavis68/RepoMapper`** — standalone map "Based entirely on the 'Repo Map' functionality in Aider.chat", MIT, 221 stars, last push 2025-12-08 — [GitHub](https://github.com/pdavis68/RepoMapper). Also `dereira/goldfish` (Go port of aider's repomap, tree-sitter + PageRank, 3 stars, pushed 2026-10-12) — [GitHub](https://github.com/dereira/goldfish)
- **repomix, `yamadashy/repomix`** — packs a repo into one AI-friendly file. `--compress` (experimental) uses tree-sitter to keep signatures, type/class structures and drop function bodies / loop logic / local variables; no reduction figure quoted on the page; related flags `--remove-comments`, `--remove-empty-lines` — [repomix code-compress guide](https://repomix.com/guide/code-compress). Tree-sitter queries exist for C (`queryC.ts`) and C++ (`queryCpp.ts`) but not Lua — [repomix src/core/treeSitter/queries](https://github.com/yamadashy/repomix/tree/main/src/core/treeSitter/queries)
- **code2prompt, `mufeedvh/code2prompt`** — "convert your codebase into a single LLM prompt with source tree, prompt templating, and token counting" — [GitHub](https://github.com/mufeedvh/code2prompt)
- **yek (now `prooflinedev/yek`)** — "fast Rust based tool to serialize text-based files in a repository or directory for LLM consumption" — [GitHub](https://github.com/prooflinedev/yek)
- **gitingest, `coderamp-labs/gitingest`** — "Replace 'hub' with 'ingest' in any GitHub URL to get a prompt-friendly extract of a codebase" — [GitHub](https://github.com/coderamp-labs/gitingest)
- **files-to-prompt, `simonw/files-to-prompt`** — "Concatenate a directory full of files into a single prompt" — [GitHub](https://github.com/simonw/files-to-prompt)
- **Serena, `oraios/serena`** — "MCP toolkit for coding, providing semantic retrieval and editing capabilities" via language servers; lists C/C++ among supported languages (also Lua, Python, PowerShell); README claims symbolic tools are "much more token-efficient than typical alternatives" (**claim**, no figure) — [Serena README](https://github.com/oraios/serena)
- **claude-context, `zilliztech/claude-context`** — "Code search MCP for Claude Code. Make entire codebase the context for any coding agent" — [GitHub](https://github.com/zilliztech/claude-context)
- **probe, `probelabs/probe`** — "semantic code search engine ... Combines ripgrep speed with tree-sitter AST parsing" — [GitHub](https://github.com/probelabs/probe); **codanna, `bartolli/codanna`** — "Local code intelligence MCP server and CLI for AI coding agents" — [GitHub](https://github.com/bartolli/codanna); **code-index-mcp, `johnhuang316/code-index-mcp`** — MCP server to "index, search, and analyze code repositories" — [GitHub](https://github.com/johnhuang316/code-index-mcp)

**Prompt compression**
- **LLMLingua / LongLLMLingua / LLMLingua-2, `microsoft/LLMLingua`** — a small LM (GPT2-small, LLaMA-7B) or a BERT-level classifier (LLMLingua-2) drops "non-essential" tokens; "up to 20x compression with minimal performance loss" (**claim**, research); example 2,365 -> 211 tokens; integrations listed are LangChain, LlamaIndex, Prompt flow — no Claude Code / MCP / coding-agent integration; sample outputs contain corrupted words and numbers ("sixlters", "Items I believe are11,,116 28") — [LLMLingua README](https://github.com/microsoft/LLMLingua)

### Inferences
- The user's `tools/lint/build_summary.py` already does for the in-house `.cmd` builds what RTK/tokf do generically; the incremental gain from a generic trimmer is on the *ad-hoc* commands agents run outside those scripts (git log/diff, grep/rg, ls/tree, direct `ctest -R ...`, pytest), not on the full build.
- context-mode's 99% figure measures bytes kept out of context, not task cost; the agent must issue follow-up `ctx_search` calls to see detail, so real savings are lower and depend on how often it searches.
- LLMLingua is not practically usable with Claude Code for code: it would need to sit in a hook or proxy, needs a local model (GPU for the 7B variant), and the README's own samples show corrupted identifiers/numbers, which is disqualifying for compiler errors and C++ symbols.

### Gaps
- No independent (third-party) measurement of RTK, tokf, context-mode, squeez or chop savings was found.
- No reduction figure for repomix `--compress` on C++ found.
- No primary doc found for "snip"/"distill" tools by those names.
- MSVC `/nologo` and CMake's default use of it with Ninja were not verified in this pass (expected: CMake's MSVC rules already pass `/nologo`, so little to gain).

## Q2. C++ support and relevance to MSVC / clang / ctest output

### Takeaway
Only RTK has a dedicated ctest handler today (merged, with an open bug that can hide assertion lines on crash backtraces); a CMake/Ninja/MSBuild handler is an open PR. tokf can be pointed at `cmake --build` / `ctest` via its TOML rewrite rules. For repo maps, tree-sitter-cpp is used by Aider and repomix, but tree-sitter parses unpreprocessed text, so macro-heavy and some template code misparses; an LSP (clangd) route via Serena avoids that.

### Cited Findings
- RTK has `src/cmds/system/ctest_cmd.rs` ("Compact CTest output while preserving failing-test details"); constants cap each failure block (2 head lines, CAP_WARNINGS tail lines) and keep the 3 slowest tests — [RTK ctest_cmd.rs](https://github.com/rtk-ai/rtk/blob/develop/src/cmds/system/ctest_cmd.rs); changelog entries "ctest: add compact output filter", "validate result lines by run total and bypass dashboard modes", "take the run total from the first result line and keep unparsed failures" — [RTK CHANGELOG](https://github.com/rtk-ai/rtk/blob/develop/CHANGELOG.md)
- Open bug: "`rtk ctest --output-on-failure` hides the assertion when a failing test prints a crash backtrace" — rtk ctest keeps first 2 and last 8 lines of each failed test, so a sanitizer/crash backtrace pushes the assertion into the elided middle — [rtk issue #5119](https://github.com/rtk-ai/rtk/issues/5119); fix proposed (open) in [PR #5120](https://github.com/rtk-ai/rtk/issues/5120)
- Open bug: exit-code guard — "rspec, phpunit, mvn, bun, deno, ctest and sbt still render green summaries on a non-zero exit" — [rtk issue #4421](https://github.com/rtk-ai/rtk/issues/4421)
- Open PR: "feat(cpp): add CMake, CTest, Make, Ninja, and MSBuild handlers" (non-zero exit cannot produce a success summary; raw failure fallback bounded to 40 lines / 4096 chars) — [rtk #3455](https://github.com/rtk-ai/rtk/issues/3455)
- RTK README's command categories do not mention cmake, ninja, MSVC or clang — [RTK README](https://github.com/rtk-ai/rtk)
- tokf README shows rewrite rules matching `^cmake --build` and `^ctest` in `.tokf/rewrites.toml` as examples of routing generic commands through a filter — [tokf README](https://github.com/mpecan/tokf)
- tree-sitter-cpp issues: macros in a class declaration make the class parse as a function definition — [tree-sitter-cpp #293](https://github.com/tree-sitter/tree-sitter-cpp/issues/293); a templated member function parsed as a binary operation — [tree-sitter-cpp #346](https://gh.r-mj.com/tree-sitter/tree-sitter-cpp/issues/346); function-like macros produce ERROR nodes that cascade — [tree-sitter-c #106](https://github.com/tree-sitter/tree-sitter-c/issues/106); Doxide notes parse errors where C++ is only valid after preprocessing — [Doxide parsing docs](https://doxide.indii.org/parsing/)
- Aider has `cpp-tags.scm` and `lua-tags.scm`; repomix has `queryCpp.ts` but no Lua query — [aider queries](https://github.com/Aider-AI/aider/tree/main/aider/queries/tree-sitter-language-pack); [repomix queries](https://github.com/yamadashy/repomix/tree/main/src/core/treeSitter/queries)
- Serena lists C/C++ and Lua among supported languages (language-server based) — [Serena README](https://github.com/oraios/serena)
- ctest's own `--test-output-size-failed` + `--test-output-truncation middle` bound failing-test output natively (CMake >= 3.24 for truncation mode) — [ctest(1) manual](https://cmake.org/cmake/help/latest/manual/ctest.1.html)

### Inferences
- For this project, ctest-native flags (`--output-on-failure --test-output-size-failed N --test-output-truncation middle`, `ninja --quiet`) are zero-risk and complement build_summary.py; RTK's ctest handler adds little over that and currently has two open correctness bugs (#5119 assertion hidden behind backtrace — directly relevant to `build_asan.cmd` sanitizer output; #4421 green summary on non-zero exit).
- No surveyed trimmer has an MSVC (`cl.exe` `file(line): warning C4xxx`) or clang diagnostic parser; generic dedup/grouping would apply only by luck. A small in-house PostToolUse hook that reuses build_summary.py's parsing is likely a better fit than any of them.
- Repo maps built on tree-sitter-cpp will be noisy on a templated, macro-using C++20 engine; a clangd-backed tool (Serena) uses the real compile commands (`compile_commands.json` from CMake/Ninja) and should handle templates/macros correctly, at the cost of clangd indexing ~1,100 files per worktree.

### Gaps
- No measurement of tree-sitter-cpp error rate on C++20 template-heavy code was found.
- Serena + clangd on Windows with MSVC-flavoured `compile_commands.json` was not verified (clangd can usually read cl-style flags via `--driver-mode=cl`, but unconfirmed here).

## Q3. Windows support

### Takeaway
RTK is the only trimmer with a first-class native-Windows story (winget, MSVC-built binary, native `rtk hook claude` since v0.37.2), but it hooks the Bash tool only; PowerShell routing is an open PR. context-mode works on Windows but requires Git Bash for its shell tools. squeez requires Git Bash (PowerShell/CMD unsupported). tokf ships no Windows binary (cargo build only) and has open Windows bugs. Packers (repomix via Node, code2prompt, yek) ship Windows binaries.

### Cited Findings
- RTK: "works fully on native Windows"; since v0.37.2 the hook "runs as a native binary command (`rtk hook claude`) — no Unix shell, bash, or jq required"; run from Command Prompt / PowerShell / Windows Terminal; some filters call `rg`, which must be on PATH; pre-v0.37.2 installs used `rtk-rewrite.sh` (re-run `rtk init -g` to migrate) — [RTK installation guide](https://www.rtk-ai.app/docs/getting-started/installation/). Install via `winget install rtk-ai.rtk`; install script is Linux/macOS/WSL only — [RTK README](https://github.com/rtk-ai/rtk). v1.2.1 (2026-11-19) ships `rtk-x86_64-pc-windows-msvc.zip` and `rtk-oss-x86_64-pc-windows-msvc.zip` — [RTK releases](https://github.com/rtk-ai/rtk/releases)
- RTK open Windows issues (329 issues match "windows"): "[BUG] RTK Windows PowerShell Compatibility Gaps" (RTK resolves real executables, not PowerShell aliases; at the time `rtk init -g` warned hook mode requires Unix and fell back to `--claude-md`) — [#1248](https://github.com/rtk-ai/rtk/issues/1248), opened 2026-04-12, still open (installation guide now says the native hook works since v0.37.2 — the two sources conflict; the issue may be stale); "rtk grep shows an aborted grep (exit 134) as an empty result with exit 0 on Windows Git Bash" — [#5073](https://github.com/rtk-ai/rtk/issues/5073); mixed path separators in `rtk find` — [#4824](https://github.com/rtk-ai/rtk/issues/4824); "os error 5" when PATH has MSIX dirs — [#5028](https://github.com/rtk-ai/rtk/issues/5028); escape codes in cmd/PowerShell console host — [#5034](https://github.com/rtk-ai/rtk/issues/5034); Windows Defender flagged v0.39.0 asset as Trojan — [#1782](https://github.com/rtk-ai/rtk/issues/1782); open PR "feat(windows): add native commands and PowerShell routing" (wrappers for `powershell`/`pwsh`, routing `Get-Content`, `Get-ChildItem`, `Select-String`) — [#3301](https://github.com/rtk-ai/rtk/issues/3301). Changelog: hooks "windows use 'rtk hook claude' no fallback"; output decoded using Windows console code page — [RTK CHANGELOG](https://github.com/rtk-ai/rtk/blob/develop/CHANGELOG.md)
- context-mode: "On Windows, add Git for Windows: the shell tools run in Git Bash, and doctor checks for it"; timeouts stop process groups with `taskkill /F /T`; CI runs install/handshake tests on Linux, macOS and Windows; needs Node >= 24.15.0 — [context-mode README](https://github.com/mksglu/context-mode). Recently closed Windows bugs: stripped PATH (`/usr/bin:/bin`) in shell-language ctx_execute breaking gh/git credential helper — [#1217](https://github.com/mksglu/context-mode/issues/1217); `setup` generated a PowerShell hook command that failed to parse — [#1375](https://github.com/mksglu/context-mode/issues/1375); Microsoft Store python alias caused "Internal error" — [#1373](https://github.com/mksglu/context-mode/issues/1373)
- squeez: "**Windows:** requires Git Bash ... PowerShell/CMD are not supported"; npm package downloads a Windows x86_64 binary; v1.47–1.48 added Windows fixes (no console windows, skips WSL `bash.exe`, kills MSYS children on timeout) — [squeez README](https://github.com/claudioemmanuel/squeez)
- tokf: latest release v0.2.54 (2026-09-06) ships only macOS and Linux archives, no Windows asset — [tokf releases](https://github.com/mpecan/tokf/releases); Windows config dir `%APPDATA%/tokf` documented — [tokf README](https://github.com/mpecan/tokf); fixed: PATHEXT not consulted (`.cmd`/`.bat` could not run) [#449](https://github.com/mpecan/tokf/issues/449), PATH joined with `:` [#451](https://github.com/mpecan/tokf/issues/451); open: concurrent cache writes fail the rename on Windows, warning every run [#455](https://github.com/mpecan/tokf/issues/455)
- Claude Code on Windows: with Git Bash installed, shell commands route through Git Bash unless `CLAUDE_CODE_USE_POWERSHELL_TOOL=1`; the PowerShell tool is on by default for claude.ai/Console accounts when Git Bash is installed — [Claude Code env vars](https://code.claude.com/docs/en/env-vars). Hook `shell` field accepts `"bash"` or `"powershell"`; exec-form hooks on Windows need a real `.exe` (not `.cmd` shims); without Git Bash a PreToolUse hook exiting other than 0/2 counts as failure and can block the call (`onFailure` controls it) — [Claude Code hooks reference](https://code.claude.com/docs/en/hooks)
- code2prompt v4.2.0 (2025-12-11) ships `code2prompt-x86_64-pc-windows-msvc.exe`; yek v0.26.0 (2026-11-15) ships `yek-x86_64-pc-windows-msvc.zip` — [code2prompt releases](https://github.com/mufeedvh/code2prompt/releases); [yek releases](https://github.com/prooflinedev/yek/releases)

### Inferences
- This setup runs both a PowerShell tool and Git Bash. Command-rewriting hooks registered on the `Bash` matcher (RTK, tokf, squeez, chop) will not see PowerShell-tool calls; savings apply only to the share of commands that go through Bash.
- The user's `.cmd` build scripts are invoked from either shell; any rewrite-hook must not wrap them (they already summarize). RTK passes unknown commands through, so this is low risk, but it should be checked with `--dry-run`.
- tokf on this machine would mean `cargo install` with the project-local Rust toolchain (`toolchain\rust` exists for the launcher) and living with the Windows cache warning — highest friction of the trimmers.

### Gaps
- Whether RTK issue #1248's fallback-to-CLAUDE.md behaviour still happens on current v1.2.1 is unconfirmed (docs say no, issue is open).
- chop, claude-compress, tersify Windows support not checked.

## Q4. How each plugs into Claude Code

### Takeaway
Trimmers plug in as hooks (PreToolUse command rewrite: RTK, tokf, chop, claude-compress, squeez; PostToolUse output replace: tersify, squeez for Read/Grep/Glob). context-mode is a Claude Code plugin bundling hooks + MCP + skills. Repo-map / search tools are MCP servers (Serena, claude-context, codanna, code-index-mcp) or CLIs whose output is written to a file (repomix, code2prompt, yek, RepoMapper). Claude Code's own `updatedToolOutput` makes a home-grown PostToolUse trimmer possible.

### Cited Findings
- RTK: `rtk init -g` installs the global Claude Code hook; plain `rtk init` only adds an RTK block to `./CLAUDE.md` and creates `.rtk/filters.toml` — [RTK installation guide](https://www.rtk-ai.app/docs/getting-started/installation/); [RTK README](https://github.com/rtk-ai/rtk)
- tokf: `tokf hook install --global` (or per project without `--global`); during install it creates `.claude/TOKF.md` and adds `@TOKF.md` to `.claude/CLAUDE.md` (`--no-context` skips); `inject_path = true` filters generate PATH shims — [tokf README](https://github.com/mpecan/tokf)
- context-mode: `claude plugin marketplace add mksglu/context-mode` then `claude plugin install context-mode@context-mode`; includes hooks, MCP and skills; slash commands `/ctx-stats`, `/ctx-doctor`, `/ctx-upgrade` — [context-mode README](https://github.com/mksglu/context-mode)
- squeez: `squeez setup` auto-detects CLIs and registers hooks; `squeez uninstall` removes them — [squeez README](https://github.com/claudioemmanuel/squeez)
- Claude Code: PreToolUse `updatedInput` "Replaces the entire input object"; PostToolUse `updatedToolOutput` replaces output for all tools (built-in + MCP) — [Claude Code hooks reference](https://code.claude.com/docs/en/hooks)

### Inferences
- Because Claude Code now supports `updatedToolOutput` for built-in tools, the user could write a ~100-line PostToolUse hook (Python, run via the project-local interpreter) that passes Bash/PowerShell output from ctest/ninja/cl through build_summary.py's parser, with the full log kept in `logs\`. This keeps everything in-repo, Windows-native, no new dependency — and covers the PowerShell tool, which third-party Bash-matcher hooks do not.
- Global hooks (`rtk init -g`, `tokf hook install --global`) apply to every worktree/lane automatically; project-level installs would write into each worktree's `.claude/`, which in this repo is tracked — prefer global or `settings.local.json`.

### Gaps
- None material.

## Q5. Maturity (stars, license, last commit) as of 2026-11-19

### Takeaway
RTK (87k stars, Apache-2.0 + BUSL engine), context-mode (34k, Elastic License 2.0, source-available), Serena (31.5k, GPL-3.0 app / MIT SolidLSP) and repomix (29k, MIT) are the heavily-used, actively-committed options. tokf and squeez are small single-maintainer projects. files-to-prompt and codebase-digest are abandoned; gitingest's default branch has had no commit since 2025-08-16.

### Cited Findings (all from `gh api repos/...` on 2026-11-19; links go to the repos)
| Tool | Stars | License | Last default-branch commit | Notes |
|---|---|---|---|---|
| [rtk-ai/rtk](https://github.com/rtk-ai/rtk) | 87,190 | Apache-2.0 (+ BUSL-1.1 engine; `rtk-oss` Apache-only) | 2026-11-19 | v1.2.1 released 2026-11-19 |
| [mksglu/context-mode](https://github.com/mksglu/context-mode) | 34,309 | Elastic License 2.0 (source-available; GitHub shows NOASSERTION) | 2026-11-19 | JS, Node >= 24.15 |
| [oraios/serena](https://github.com/oraios/serena) | 31,540 | GPL-3.0-or-later (app), MIT (SolidLSP) | 2026-11-19 | Python |
| [Aider-AI/aider](https://github.com/Aider-AI/aider) | 50,186 | Apache-2.0 | 2026-05-22 | ~6 months idle |
| [yamadashy/repomix](https://github.com/yamadashy/repomix) | 29,343 | MIT | 2026-11-14 | TS |
| [coderamp-labs/gitingest](https://github.com/coderamp-labs/gitingest) | 16,158 | MIT | 2025-08-16 (last: a dependency bump) | >12 months without a default-branch commit; pushed 2026-11-18 on other branches |
| [zilliztech/claude-context](https://github.com/zilliztech/claude-context) | 12,707 | MIT | 2026-11-12 | TS |
| [mufeedvh/code2prompt](https://github.com/mufeedvh/code2prompt) | 7,770 | MIT | 2026-11-03 | Rust |
| [microsoft/LLMLingua](https://github.com/microsoft/LLMLingua) | 6,844 | MIT | 2026-11-06 | research code |
| [simonw/files-to-prompt](https://github.com/simonw/files-to-prompt) | 2,806 | Apache-2.0 | 2025-02-19 | abandoned (>12 months) |
| [prooflinedev/yek](https://github.com/prooflinedev/yek) (was bodo-run/yek) | 2,478 | MIT | 2026-11-15 | Rust |
| [johnhuang316/code-index-mcp](https://github.com/johnhuang316/code-index-mcp) | 1,006 | MIT | 2026-07-27 | |
| [bartolli/codanna](https://github.com/bartolli/codanna) | 764 | Apache-2.0 | 2026-10-11 | Rust |
| [probelabs/probe](https://github.com/probelabs/probe) | 740 | Apache-2.0 | 2026-11-11 | Rust |
| [kamilstanuch/codebase-digest](https://github.com/kamilstanuch/codebase-digest) | 398 | none | 2024-10-21 | abandoned |
| [wrale/mcp-server-tree-sitter](https://github.com/wrale/mcp-server-tree-sitter) | 312 | MIT | 2026-05-21 | archived |
| [claudioemmanuel/squeez](https://github.com/claudioemmanuel/squeez) | 237 | Apache-2.0 | pushed 2026-11-13 | |
| [pdavis68/RepoMapper](https://github.com/pdavis68/RepoMapper) | 221 | MIT | pushed 2025-12-08 | |
| [mpecan/tokf](https://github.com/mpecan/tokf) | 200 | MIT | 2026-09-06 (v0.2.54) | pushed 2026-11-19 on non-default branches |
| [AgusRdz/chop](https://github.com/AgusRdz/chop) | 48 | MIT | pushed 2026-10-29 | |

- RTK license detail: "Production use is free"; offering RTK or a derivative to third parties as a commercial product/service requires contacting the vendor; `rtk-proxy` is BUSL-1.1 — [RTK README](https://github.com/rtk-ai/rtk)
- context-mode ELv2 restricts offering it as a hosted/managed service, circumventing license keys, removing notices — [context-mode README](https://github.com/mksglu/context-mode); LICENSE first line "Elastic License 2.0 (ELv2)" — [LICENSE](https://github.com/mksglu/context-mode/blob/main/LICENSE)
- Serena LICENSE: SolidLSP MIT, Serena application GPL-3.0-or-later — [Serena LICENSE](https://github.com/oraios/serena/blob/main/LICENSE)

### Inferences
- RTK's 87k stars in roughly a year plus same-day releases indicates heavy use, but also churn (329 Windows-matching issues). Pin a version.
- None of these licenses matter for a local dev tool that is not redistributed; the project's "no third-party deps in engine/" rule is not touched because none link into `engine/`.

### Gaps
- Star counts can be inflated; no download/usage numbers were collected.

## Q6. Setup effort and risks (files written, network, error-hiding)

### Takeaway
The main risk of every trimmer is hiding the line the agent needs (RTK #5119 and #4421 are concrete ctest examples; tokf masks exit codes to 0 by default). Network/cloud risk is low for RTK (telemetry opt-in), tokf, squeez, context-mode (zero telemetry, but `ctx_execute` runs arbitrary code as the user). Some installers write into the repo's `CLAUDE.md`/`.claude/`.

### Cited Findings
- RTK: telemetry anonymous and off by default, opt-in via `rtk telemetry enable`; "collects no command content and no file contents"; pipelines can send content to the models they declare; optional `rtk-proxy` routes provider API traffic — [RTK README](https://github.com/rtk-ai/rtk)
- RTK Windows asset once flagged by Defender as Trojan:Win32/Bearfoos.B!ml (v0.39.0) — [rtk #1782](https://github.com/rtk-ai/rtk/issues/1782)
- RTK correctness issues: crash backtrace hides ctest assertion — [#5119](https://github.com/rtk-ai/rtk/issues/5119); ctest green summary on non-zero exit — [#4421](https://github.com/rtk-ai/rtk/issues/4421); aborted grep shown as empty result with exit 0 on Git Bash — [#5073](https://github.com/rtk-ai/rtk/issues/5073)
- tokf: exits 0 by default and prepends `Error: Exit code N` on failure; writes `.claude/TOKF.md` and edits `.claude/CLAUDE.md` on hook install unless `--no-context` — [tokf README](https://github.com/mpecan/tokf)
- context-mode: `ctx_execute` "runs code on the host as your user, with your files, network, and environment"; "Sandbox" means output kept out of context, not isolation; hook errors exit 0 and are logged to `~/.context-mode/hook-errors.log`, so failures can be quiet; network uses limited to URL fetch for `ctx_index`, npm dist-tags check, upgrade/debug-report; Gateway (paid) routes requests through its server — [context-mode README](https://github.com/mksglu/context-mode). Open issue: tool failures, Glob/Agent/Skill/Task and git events aren't journalled by the Claude Code adapter — [#1408](https://github.com/mksglu/context-mode/issues/1408); one Node process per session (RAM scales with sessions) — [#1334](https://github.com/mksglu/context-mode/issues/1334)
- squeez: refuses net-loss compressions; preservation guard stashes verbatim output when identifier survival is low — [squeez README](https://github.com/claudioemmanuel/squeez)
- repomix `--compress` discards implementation logic, suited to overviews not review/editing; experimental — [repomix guide](https://repomix.com/guide/code-compress)
- gitingest is built around GitHub URLs (hub -> ingest), i.e. a hosted service path — [gitingest repo](https://github.com/coderamp-labs/gitingest)

### Inferences
- With 3 parallel lanes, context-mode spawns a Node MCP process per session plus a per-project SQLite DB keyed by project path — each worktree is a different path, so lanes would not share an index (unverified).
- tokf's exit-code masking conflicts with the project's build scripts' exit-code conventions (e.g. `land_on_dev.cmd` refusal exit 3); if used, set `--no-mask-exit-code`.
- Packers that write a single output file (repomix, code2prompt, yek) should write to the scratchpad or a gitignored path; a whole-repo pack of ~2,400 files is far larger than any context window and is only useful filtered (`--include engine/<module>`) or compressed.
- gitingest's hosted mode would upload/clone the repo through a third party; avoid for this private repo (local CLI mode exists per its README but was not verified here).

### Gaps
- No check made of what claude-context sends to embedding providers (from memory it needs an embedding API and a Milvus/Zilliz vector DB — unverified, so treat as a cloud-upload risk to confirm before use).

## Q7. Overlaps and which to pick when two do the same job

### Takeaway
Pick one command trimmer, not several (they all rewrite the same Bash calls). RTK > tokf/squeez/chop on Windows and on ctest coverage; context-mode is a different model (index + search) that overlaps RTK on shell output and adds session-resume. For code navigation, a clangd-backed MCP (Serena) beats tree-sitter repo maps on templated/macro C++; repomix/code2prompt/yek are one-shot packers that overlap each other almost completely.

### Cited Findings
- tokf "heavily inspired by rtk" (same proxy model) — [docs.rs tokf README](https://docs.rs/crate/tokf/0.1.1/source/README.md)
- RTK: filters stdout of commands; Read/Grep/Glob untouched by default — [RTK README](https://github.com/rtk-ai/rtk); squeez: also rewrites Read/Grep/Glob via PostToolUse — [docs.rs squeez 1.18.0](https://docs.rs/crate/squeez/1.18.0/source/README.md); context-mode: hooks redirect output into its index plus a session journal — [context-mode README](https://github.com/mksglu/context-mode)
- Aider repo map: ranked, budgeted (1k default) symbol map; RepoMapper/goldfish are standalone ports — [Aider docs](https://aider.chat/docs/repomap.html); [RepoMapper](https://github.com/pdavis68/RepoMapper); [goldfish](https://github.com/dereira/goldfish)

### Inferences
- Ranking for this setup (for the later writer to verify): (1) zero-install native flags + a small in-house PostToolUse hook reusing build_summary.py (covers MSVC/clang/ctest exactly, both shells, no new deps); (2) RTK global hook for ad-hoc git/grep/ls/test commands through Bash, pinned version, `rtk bypass` / `rtk recall` documented in CLAUDE.md; (3) Serena (clangd) or a per-directory summary approach (CLAUDE.md per module, already partly in place via `docs/`) for cutting file re-reads. context-mode is the alternative to (2) if session-continuity matters more than Windows simplicity (needs Git Bash, Node 24, ELv2).
- Running RTK and context-mode together double-handles the same Bash calls; choose one.
- Per-directory CLAUDE.md / AGENTS.md summaries and llms.txt overlap with repo maps; they are hand-maintained but immune to tree-sitter C++ parse issues.

### Gaps
- No primary source surveyed for per-directory CLAUDE.md effectiveness or llms.txt for code repos (not fetched in this pass); Claude Code's memory docs would be the primary source.
- No head-to-head benchmark of RTK vs tokf vs context-mode on the same workload was found.

## Q8. Abandoned or non-Windows tools (one line each)

### Takeaway
Drop these from the comparison table or list them as excluded.

### Cited Findings
- **simonw/files-to-prompt** — last commit 2025-02-19, abandoned (>12 months) — [GitHub](https://github.com/simonw/files-to-prompt)
- **kamilstanuch/codebase-digest** — last commit 2024-10-21, no license, abandoned — [GitHub](https://github.com/kamilstanuch/codebase-digest)
- **wrale/mcp-server-tree-sitter** — archived 2026 — [GitHub](https://github.com/wrale/mcp-server-tree-sitter)
- **coderamp-labs/gitingest** — no default-branch commit since 2025-08-16 (>12 months), though other branches were pushed 2026-11-18; treat as stale — [GitHub](https://github.com/coderamp-labs/gitingest)
- **restlessronin/llm-code-highlighter** — last push 2024-03-03, abandoned — [GitHub](https://github.com/restlessronin/llm-code-highlighter)
- **squeez** — Windows only via Git Bash; PowerShell/CMD unsupported — [squeez README](https://github.com/claudioemmanuel/squeez)
- **tokf** — no Windows release binary (cargo build only), open Windows cache bug — [tokf releases](https://github.com/mpecan/tokf/releases); [#455](https://github.com/mpecan/tokf/issues/455)
- **LLMLingua** — runs on Windows as Python but has no Claude Code integration and corrupts exact strings; not usable for this workflow — [LLMLingua README](https://github.com/microsoft/LLMLingua)

### Inferences
- Aider itself (last commit 2026-05-22) is not abandoned but slowing; its repo map is reachable only through Aider or third-party ports, none of which is mature.

### Gaps
- Windows support of chop, claude-compress, tersify, codanna, probe and code-index-mcp was not verified.
