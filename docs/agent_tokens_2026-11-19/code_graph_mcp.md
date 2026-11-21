# Code-graph and code-intelligence tools (MCP servers, plugins, CLIs) for Claude Code, as of 2026-11-19

Scope: tools that let Claude Code ask scoped questions about a large codebase (symbols, callers, references, structure, semantic search) instead of re-reading files and grepping. Judged against: Windows 11, Claude Code desktop + CLI, up to 3 parallel lanes each in its own git worktree, ~1,100 C++20 files (MSVC + clang, heavy templates, CMake/Ninja, compile_commands.json available from CMake), Python, Lua 5.0, XML, Markdown; already using ReVa (Ghidra MCP) and a local clangd LSP plugin ("eaw-clangd").

Maturity data (stars, license, last commit on the default branch) was pulled with `gh api repos/OWNER/REPO` and `gh api repos/OWNER/REPO/commits?per_page=1` on 2026-11-19; the cited source for those numbers is the repo page. "claim" = vendor / README / author-run number; "measured" = run by someone not shipping the tool. Nothing was installed or run in the project tree.

## Q1. Per-tool fact sheet (what it does, mechanism, C++, Windows, Claude Code hookup, maturity, tree risks)

### Takeaway
Four tools stand out as live, Windows-capable and C++-aware: Claude Code's own LSP tool with clangd (already in use), Serena (LSP-backed, clangd + compile_commands.json, adds symbolic edits and memories), codebase-memory-mcp (single native binary, tree-sitter + built-in C/C++ type resolution, index outside the repo, worktree-aware) and CodeGraph (colbymchenry, tree-sitter graph, strongest published A/B numbers, plus one independent reproduction). Graphify (the reference point) is very active but is a skill + hooks + CLI first, MCP second, and has an open Windows C++ bug that matches this machine's drive layout.

### Cited Findings

#### Graphify (Graphify-Labs/graphify; PyPI `graphifyy`)
- What: turns a repo (code, docs, configs, PDFs) into a queryable knowledge graph; "A /graphify skill for Claude Code, Cursor, Codex, and Gemini CLI: local deterministic AST parsing, every edge explained, no vector store" — [repo](https://github.com/Graphify-Labs/graphify)
- Maturity: 150,591 stars, Apache-2.0, last commit 2026-11-19 — [repo](https://github.com/Graphify-Labs/graphify) (GitHub API; `safishamsi/graphify` redirects here)
- Parser: tree-sitter AST for ~40 languages, `calls` / `imports` / `inherits` edges resolved across files; code parsing is local and needs no LLM; docs/PDFs/images go through a "semantic pass" using the assistant's model or a configured API key — [README](https://github.com/Graphify-Labs/graphify)
- `.h` is ambiguous (C or C++); graphify picks one global default and it can be overridden per project in `.graphifyrc` — [README](https://github.com/Graphify-Labs/graphify)
- Claude Code hookup: `graphify install` writes a skill and, on Claude Code, a PreToolUse hook that fires before search-style calls and before Read/Glob and `cat`/`head`/`sed -n` in Bash, nudging the agent to the graph; `--project --strict` makes Read and Grep share one denial per session unless a graph query happened in the last 30 minutes; an MCP stdio server is an optional extra (`graphifyy[mcp]`); `graphify hook install` adds git hooks that rebuild on commit and branch checkout — [README](https://github.com/Graphify-Labs/graphify)
- Files written: graph output in `graphify-out/` inside the project; `.graphifyignore`, `.graphifyrc`; every query (question, nodes returned, duration) is logged to `~/.cache/graphify-queries.log`; "No telemetry" — [README](https://github.com/Graphify-Labs/graphify)
- Windows: documented install path for "Claude Code (Windows)" (`graphify install --platform windows`) — [README](https://github.com/Graphify-Labs/graphify). Open issues: #5145 "C++ declaration and definition reconciliation fails when cache and source are on different Windows drives" (2026-11-11) — [issue](https://github.com/Graphify-Labs/graphify/issues/5145); #2987 "trouble with the graphify mcp server on Windows" — [issue](https://github.com/Graphify-Labs/graphify/issues/2987); #3600 Windows skill docs assume POSIX `mktemp` paths — [issue](https://github.com/Graphify-Labs/graphify/issues/3600)
- Other open C/C++ issue: #5218 angle-bracket includes are never resolved to corpus headers; a standard header name can bind to a same-named project file — [issue](https://github.com/Graphify-Labs/graphify/issues/5218); closed 2026-11-18: #3397 declaration-only `.h` headers with templates/references/default args misdetected as C — [issue](https://github.com/Graphify-Labs/graphify/issues/3397)
- Paid tier: "Graphify Cloud" advertised as cutting "agent token spend by 80%" (claim, no method on the README) — [README](https://github.com/Graphify-Labs/graphify)

#### Claude Code native LSP tool (plugins with `lspServers`; official `clangd-lsp`)
- What: a read-only `LSP` tool that can "Jump to a symbol's definition", "Find all references", "Get type information at a position", "List symbols in a file", "Search for a symbol by name across the workspace", "Find implementations of an interface", "Trace call hierarchies"; plus diagnostics pushed into context after each edit — [tools reference](https://code.claude.com/docs/en/tools-reference.md)
- Official C/C++ plugin is `clangd-lsp` from `claude-plugins-official`; the plugin does not ship the binary, `clangd` must be on PATH; LSP servers are not started in cloud sessions — [code intelligence docs](https://code.claude.com/docs/en/plugins/code-intelligence.md)
- Config fields per server: `command`, `extensionToLanguage`, `args`, `env`, `initializationOptions`, `settings`, `workspaceFolder`, `startupTimeout`, `shutdownTimeout`, `requestTimeout` (default 60000 ms), `restartOnCrash`, `maxRestarts`, `diagnostics` (default true); "Claude Code accepts `socket` but runs every server over stdio" — [plugin manifest reference](https://code.claude.com/docs/en/plugins-reference)
- Troubleshooting page lists "High memory use: memory use increases while the server indexes the project" — [code intelligence docs](https://code.claude.com/docs/en/plugins/code-intelligence.md)
- Community marketplace with LSP plugins for many languages incl. clangd: Piebald-AI/claude-code-lsps, 520 stars, no license file, last commit 2026-11-15 — [repo](https://github.com/Piebald-AI/claude-code-lsps)

#### Serena (oraios/serena)
- What: MCP toolkit for "semantic retrieval and editing"; tools: find symbol, symbol overview (file outline), find referencing symbols, find declaration/implementations, diagnostics, rename, replace symbol body, insert before/after symbol, safe delete, plus a memory system; its basic file/search/shell tools are "typically disabled by default" inside Claude Code — [repo](https://github.com/oraios/serena)
- Maturity: 31,540 stars, license reported by GitHub as NOASSERTION, last commit 2026-11-19 — [repo](https://github.com/oraios/serena)
- Backends: language servers (free, default, 40+ languages incl. C/C++, Lua, Python, Markdown) or a paid JetBrains plugin ("Rider and CLion are unsupported") — [repo](https://github.com/oraios/serena)
- C++: clangd by default (Serena "automatically downloads and manages clangd"), ccls optional; "Your project must have a `compile_commands.json` file at the repository root"; relative paths are rewritten into a new `.serena/compile_commands.json`; files created externally after server start are not indexed until opened or the server restarts — [C/C++ setup guide](https://github.com/oraios/serena/blob/main/docs/03-special-guides/cpp_setup.md); "for best results, provide a `compile_commands.json` at the repository root" — [language docs](https://oraios.github.io/serena/01-about/020_programming-languages.html)
- Open clangd issues (Oct 2026): #2191 references empty right after start because Serena does not wait for the background index — [issue](https://github.com/oraios/serena/issues/2191); #2190 clangd re-parses a file on every tool call because Serena closes it after each request — [issue](https://github.com/oraios/serena/issues/2190); #2192 queued tool calls time out before they start — [issue](https://github.com/oraios/serena/issues/2192); #1373 "C++: clangd uses single-header TU resolution" — [issue](https://github.com/oraios/serena/issues/1373); #2223 a symbol cache that fails to load is overwritten, losing the whole index — [issue](https://github.com/oraios/serena/issues/2223)
- Windows issues: #2196 Windows 11 Smart App Control blocks the uv-generated `serena.exe` — [issue](https://github.com/oraios/serena/issues/2196); #2285 encoding fallback: a wrong charset guess is written into the whole file on the next edit — [issue](https://github.com/oraios/serena/issues/2285)
- Multi-client: #2284 "Shared HTTP server: the active project is global, so one client's activate_project redirects another client's edits" (open, 2026-11-18) — [issue](https://github.com/oraios/serena/issues/2284); #2173 request so gitignored worktree dirs stay indexable — [issue](https://github.com/oraios/serena/issues/2173)
- Evaluation: README quotes agent testimonials (Opus 4.6 in Claude Code on a large Python codebase, etc.) from a "~20 routine coding tasks" prompt; no token figures on the README — [repo](https://github.com/oraios/serena)

#### codebase-memory-mcp (DeusData)
- What: local code intelligence, persistent graph, 17 MCP tools; symbol / BM25 (SQLite FTS5) / semantic search, callers, dependencies, outlines, snapshot diffs, test-impact selection — [repo](https://github.com/DeusData/codebase-memory-mcp)
- Maturity: 48,875 stars, MIT, last commit 2026-11-19 — [repo](https://github.com/DeusData/codebase-memory-mcp)
- Parser: native C binary with "164 vendored tree-sitter grammars" plus "Hybrid LSP" type resolution embedded in the binary (no language server process); C/C++ row: "C side handles macros + `typedef` chains + header-vs-source linking; C++ side handles templates, namespaces, `auto` inference, and method resolution via class hierarchy" (sharpened in v0.7.0); experimental test-selection narrowing "currently supports C/C++ family code" — [README](https://github.com/DeusData/codebase-memory-mcp)
- Semantic search uses bundled `nomic-embed-code` embeddings "compiled into the binary; no API key"; "indexes and queries locally and collects no telemetry" — [README](https://github.com/DeusData/codebase-memory-mcp)
- Storage: SQLite under `~/.cache/codebase-memory-mcp/` (outside the repo); an in-repo `.codebase-memory/graph.db.zst` snapshot is opt-in (`persistence: true`, default false) — [README](https://github.com/DeusData/codebase-memory-mcp)
- Worktrees: "Linked git worktrees are indexed as their own projects by default"; `config set ignore_worktrees true` skips them; a detached shared daemon serves sessions — [README](https://github.com/DeusData/codebase-memory-mcp)
- Windows: release zip `codebase-memory-mcp-windows-amd64.zip`, PowerShell setup script; "SmartScreen may show a warning for unsigned software" — [README](https://github.com/DeusData/codebase-memory-mcp). Issues: #1183 session root cwd became `C:\Windows\system32` with auto_index — [issue](https://github.com/DeusData/codebase-memory-mcp/issues/1183); #1145 indexing worker crashed on a file — [issue](https://github.com/DeusData/codebase-memory-mcp/issues/1145); #2552 request for signed Windows executable — [issue](https://github.com/DeusData/codebase-memory-mcp/issues/2552)

#### CodeGraph (colbymchenry/codegraph)
- What: "Pre-indexed code knowledge graph, auto syncs on code changes … fewer tokens, fewer tool calls, 100% local"; main tool `codegraph_explore` — [repo](https://github.com/colbymchenry/codegraph)
- Maturity: 78,119 stars, MIT, last commit 2026-11-19 — [repo](https://github.com/colbymchenry/codegraph)
- Parser: native Rust kernel with tree-sitter grammars, 20 languages incl. C, C++, Lua, Python (CUDA/Metal ride the C++ path); file watcher re-syncs ~300 ms after a save (~0.4 s on the 27k-file Swift compiler repo) — [README](https://github.com/colbymchenry/codegraph)
- Storage: `.codegraph/codegraph.db` (SQLite + FTS5) inside the project; `codegraph init --local-only` keeps `.codegraph/` out of git; respects `.gitignore` — [README](https://github.com/colbymchenry/codegraph)
- Telemetry: anonymous usage telemetry exists; off via `codegraph telemetry off`, `CODEGRAPH_TELEMETRY=0`, `DO_NOT_TRACK=1` or `--no-telemetry` on the MCP server — [README](https://github.com/colbymchenry/codegraph)
- Claude Code hookup: MCP server entry in `~/.claude.json`, optional auto-allow in `~/.claude/settings.json`, installer for many agents — [README](https://github.com/colbymchenry/codegraph)
- Worktrees: open requests #1236 "Share .codegraph index across Git worktrees" and #3243 "Git Worktree Index Sharing" (so today: one index per worktree) — [#1236](https://github.com/colbymchenry/codegraph/issues/1236), [#3243](https://github.com/colbymchenry/codegraph/issues/3243)
- Windows: supported badge and PowerShell installer — [README](https://github.com/colbymchenry/codegraph); issues #3137 watchdog kill/respawn loop under memory exhaustion "crashes unrelated apps (Windows, v1.2.0)" — [issue](https://github.com/colbymchenry/codegraph/issues/3137); #3286 intermittent 45 s+ explore timeouts on Windows — [issue](https://github.com/colbymchenry/codegraph/issues/3286)
- Context caveat stated by the vendor: in multi-turn sessions "CodeGraph's responses leave about 80% more retrieval context resident" — [README](https://github.com/colbymchenry/codegraph)

#### GitNexus (abhigyanpatwari/GitNexus)
- What: "Zero-Server Code Intelligence Engine"; tree-sitter parsing into LadybugDB (embedded graph DB, formerly KuzuDB) with vector support; MCP tools, CLI, web UI; C and C++ in the language list — [repo](https://github.com/abhigyanpatwari/GitNexus)
- Maturity: 48,665 stars, last commit 2026-11-19; license PolyForm Noncommercial 1.0.0 (commercial licence sold separately) — [repo](https://github.com/abhigyanpatwari/GitNexus)
- Tree risk: "`analyze` builds the index, installs agent skills, registers Claude Code hooks, and writes `AGENTS.md` / `CLAUDE.md` context files"; for several agents run one `gitnexus mcp --http --port 3111` server — [README](https://github.com/abhigyanpatwari/GitNexus)
- Windows issues: #3706 tools crash or hang on `@ladybugdb/core` 0.21.1 on Windows — [issue](https://github.com/abhigyanpatwari/GitNexus/issues/3706); #3818 blank-id defect "live on Windows" — [issue](https://github.com/abhigyanpatwari/GitNexus/issues/3818)

#### code-graph-rag (vitali87/code-graph-rag, CLI `cgr`)
- What: tree-sitter knowledge graph of a monorepo, "sharpened by compiler-grade frontends", to "query, understand, and edit" code; graph in embedded SQLite at `~/.cgr/graph.sqlite` by default, Memgraph/Neo4j optional — [repo](https://github.com/vitali87/code-graph-rag)
- Maturity: 5,393 stars, MIT, last commit 2026-11-19 — [repo](https://github.com/vitali87/code-graph-rag)
- C++: `CPP_FRONTEND=hybrid` (default) = tree-sitter plus libclang: macro `Function` nodes, macro-expansion `CALLS` edges, `#include` `IMPORTS` edges, `using`/`typedef` alias nodes; needs `pip install "code-graph-rag[cpp]"` and a `compile_commands.json`, otherwise falls back to tree-sitter — [C/C++ semantic mode doc](https://github.com/vitali87/code-graph-rag/blob/main/docs/guide/cpp-semantic-mode.md)
- Windows: native extension `cgr-graph-rs` ships for CPython 3.12+ on Windows x64 — [README](https://github.com/vitali87/code-graph-rag)
- Open issues: #3152 non-UTF-8 source files indexed but unreadable — [issue](https://github.com/vitali87/code-graph-rag/issues/3152); #1374 an end-to-end benchmark suite (token cost etc.) is still an open task — [issue](https://github.com/vitali87/code-graph-rag/issues/1374)

#### CodeGraphContext (CodeGraphContext/CodeGraphContext)
- What: "MCP server plus a CLI tool that indexes local code into a graph database"; tree-sitter plus optional SCIP indexers — [repo](https://github.com/CodeGraphContext/CodeGraphContext)
- Maturity: 4,439 stars, MIT, last commit 2026-11-18 — [repo](https://github.com/CodeGraphContext/CodeGraphContext)
- C++: with `SCIP_INDEXER=true`, C/C++ use scip-clang, which "requires a `compile_commands.json`" — [README](https://github.com/CodeGraphContext/CodeGraphContext)
- DB / Windows: default FalkorDB Lite is "Unix-only (Linux/macOS/WSL)"; KuzuDB / LadybugDB are "Windows Native"; embedded DBs default to a 4 GiB buffer pool; config in `~/.codegraphcontext/.env`; `.cgcignore` in the repo root — [README](https://github.com/CodeGraphContext/CodeGraphContext). Windows issues #580, #581, #691 ("Urgent: Issues in windows setup"), #599 — [#691](https://github.com/CodeGraphContext/CodeGraphContext/issues/691)
- Claim: opt-in GCF output "Reduce tool response tokens by ~62%" — [README](https://github.com/CodeGraphContext/CodeGraphContext)

#### Claude Context (zilliztech/claude-context)
- What: "Code search MCP for Claude Code" via vector search; needs an embedding provider (OpenAI, VoyageAI, Ollama, Gemini) and Milvus / Zilliz Cloud (`MILVUS_TOKEN`) — [repo](https://github.com/zilliztech/claude-context)
- Maturity: 12,707 stars, MIT, last commit 2026-11-12 — [repo](https://github.com/zilliztech/claude-context)
- Claim: "~40% token reduction under the condition of equivalent retrieval quality" — [README](https://github.com/zilliztech/claude-context)
- Windows issues: #233 Windows path casing creates separate Milvus collections — [issue](https://github.com/zilliztech/claude-context/issues/233); #166 `spawn C:\Windows\system32\cmd.exe ENOENT` — [issue](https://github.com/zilliztech/claude-context/issues/166)

#### Codanna (bartolli/codanna)
- Local code-intelligence MCP server + CLI; tree-sitter for 15 languages incl. C, C++, Lua; index under `.codanna/` in the repo; local embedding model (~150 MB) downloaded once; lookups under 10 ms; "Windows support is experimental" (PowerShell installer exists) — [repo](https://github.com/bartolli/codanna)
- Maturity: 764 stars, Apache-2.0, last commit 2026-10-11 — [repo](https://github.com/bartolli/codanna)

#### Probe (probelabs/probe)
- "AI-friendly semantic code search engine": ripgrep + tree-sitter AST, "Zero Indexing", `--max-tokens` budget and session dedup; C/C++ supported; MCP config for Claude; PowerShell install documented — [repo](https://github.com/probelabs/probe)
- Maturity: 740 stars, Apache-2.0, last commit 2026-11-11 — [repo](https://github.com/probelabs/probe)

#### Octocode (bgauryy/octocode, formerly octocode-mcp)
- Local + GitHub code research (ripgrep, tree-sitter structural search, skeleton/minified reads "a 100 KB file in a few hundred tokens", LSP via `lspGetSemantics`: definition, references, callers/callees, call hierarchy, hover, diagnostics); MCP or CLI; home dir `~/.octocode` / `%USERPROFILE%\.octocode`; GitHub token for remote tools — [repo](https://github.com/bgauryy/octocode)
- Claim: ~2.0x fewer characters than plain `gh` at near-parity correctness (characters, not tokens; vendor benchmark) — [README](https://github.com/bgauryy/octocode)
- Maturity: 966 stars, MIT, last commit 2026-08-25 — [repo](https://github.com/bgauryy/octocode)

#### narsil-mcp (postrv/narsil-mcp)
- Rust MCP server, "90 tools, 32 languages", tree-sitter (C++: functions, classes, structs, namespaces); Windows via Scoop/PowerShell (building from source needs VS Build Tools); optional neural embeddings via Voyage/OpenAI API or local ONNX; claim "Minimal preset: 61% fewer tokens vs Full" (tool-schema overhead) — [repo](https://github.com/postrv/narsil-mcp)
- Maturity: 191 stars, Apache-2.0, last commit 2026-10-07 — [repo](https://github.com/postrv/narsil-mcp)

#### ast-grep MCP (ast-grep/ast-grep-mcp)
- "An experimental" MCP server for structural (AST pattern) search via ast-grep; C/C++ supported; text output "~75% fewer tokens" than JSON (claim) — [repo](https://github.com/ast-grep/ast-grep-mcp)
- Maturity: 499 stars, MIT, last commit 2026-11-05 — [repo](https://github.com/ast-grep/ast-grep-mcp)

#### ck (BeaconBay/ck)
- Local semantic + hybrid BM25 grep with MCP tools (`semantic_search` etc.); C++ tree-sitter chunking "Classes, structs, namespaces, templates"; offline embeddings; CI runs on Windows; index "~2x source code size"; index location given both as `~/.cache/ck/project-<hash>/` and as a deletable `.ck/` directory (README is inconsistent) — [repo](https://github.com/BeaconBay/ck)
- Maturity: 1,773 stars, Apache-2.0, last commit 2026-10-11 — [repo](https://github.com/BeaconBay/ck)

#### code-index-mcp (johnhuang316/code-index-mcp)
- MCP server for indexing/search; tree-sitter for 10 languages (Python, JS, TS, Java, Kotlin, C#, Go, Objective-C, Zig, Rust); "C/C++" only via the generic "fallback strategy" — [repo](https://github.com/johnhuang316/code-index-mcp)
- Maturity: 1,006 stars, MIT, last commit 2026-07-27 — [repo](https://github.com/johnhuang316/code-index-mcp)

#### Kit (cased/kit)
- Python toolkit for "codebase mapping, symbol extraction, and many kinds of code search", with a local `kit-dev` MCP server — [repo](https://github.com/cased/kit)
- Maturity: 1,311 stars, MIT, last commit 2026-03-03 (8.5 months idle) — [repo](https://github.com/cased/kit)

#### cclsp (ktnyt/cclsp)
- MCP bridge exposing LSP servers (clangd config shown) to Claude Code — [repo](https://github.com/ktnyt/cclsp)
- Maturity: 676 stars, MIT, last commit 2026-02-22 — [repo](https://github.com/ktnyt/cclsp). Windows issues: #34/#35 "Connection closed" despite "Connected" — [#35](https://github.com/ktnyt/cclsp/issues/35); #47 LSP children orphaned on MCP disconnect on Windows — [#47](https://github.com/ktnyt/cclsp/issues/47)

#### Sourcegraph MCP / Code Finder
- Sourcegraph's MCP server exposes an instance's code search and navigation over HTTP (`claude mcp add --transport http sourcegraph https://<instance>/.api/mcp`), OAuth or token auth; supported on Enterprise plans; "Code Finder" agentic search is in Beta and free while in Beta, billable with credits at GA — [Sourcegraph MCP docs](https://sourcegraph.com/docs/api/mcp), [Code Finder changelog](https://sourcegraph.com/changelog/code-finder-beta)
- The public Sourcegraph repo snapshot is archived (last commit 2024-08-22) — [repo](https://github.com/sourcegraph/sourcegraph-public-snapshot)

#### Sourcebot (sourcebot-dev/sourcebot)
- Self-hosted code search "for humans and agents"; needs Node.js, Docker and Docker Compose — [repo](https://github.com/sourcebot-dev/sourcebot). Maturity: 4,036 stars, MIT, last commit 2026-11-18 — [repo](https://github.com/sourcebot-dev/sourcebot)

#### RepoMapper (pdavis68/RepoMapper)
- Aider "Repo Map" clone as CLI + MCP: tree-sitter definitions/references ranked to fit a token budget; cpp supported — [repo](https://github.com/pdavis68/RepoMapper). Maturity: 221 stars, MIT, last commit 2025-12-08 — [repo](https://github.com/pdavis68/RepoMapper)

#### Others checked
- potpie-ai/potpie "Context Graph for AI Native SDLC", 5,799 stars, Apache-2.0, last commit 2026-10-30 — [repo](https://github.com/potpie-ai/potpie)
- blarApp/blarify "A tool to build a graph from a codebase", 233 stars, MIT, last commit 2026-08-17 — [repo](https://github.com/blarApp/blarify)

### Inferences
- The project already has the core of the "LSP path" (native LSP tool + eaw-clangd). Additional tools earn their place only by adding what LSP lacks: cross-session persistent graph queries (callers-of-callers, hubs, impact), compact outlines of big files, and search over non-C++ content (Lua, XML, Markdown specs).
- For a heavily templated C++20 tree, anything that is tree-sitter-only resolves calls by name heuristics; clangd/libclang-backed tools (native LSP, Serena, code-graph-rag hybrid, CodeGraphContext+scip-clang) are the only ones that see real template instantiation and macro expansion. codebase-memory-mcp's built-in C++ type resolution is a middle ground whose accuracy on MSVC-specific code is unverified.

### Gaps
- code-graph-rag: whether its MCP server mode and natural-language query need an LLM API key was not verified in this pass.
- Kit and Probe: no C++-specific limits documented; no Kit Windows statement found.
- scip-clang Windows support (needed by CodeGraphContext's accurate C++ mode) was not verified.
- Serena's licence shows as NOASSERTION in the GitHub API; the actual licence text was not read.
- CodeGraph's telemetry default (on or off) is implied by the opt-out switches but not stated in the lines read.
- Nuanced: only `nuanced-dev/nuanced-py` exists (archived, Python call graphs) — no C++ product found.

## Q2. Token / time savings: measured vs claimed

### Takeaway
Every headline number is vendor-run. The only third-party data found: one independent reproduction of CodeGraph on Hono (tool calls -55%, tokens -23%, cost +7%) and one 936-run benchmark by the author of a competing tool (graphlens) in which CodeGraph and Serena both cut cost 2-12x versus grep on a 400k-LOC Python/TS repo. No measurement on a C++ repo exists for any tool.

### Cited Findings
- CodeGraph (claim): `claude -p` A/B with Claude Opus 4.8, `--strict-mcp-config`, CLI blocked in both arms; across 7 repos "88% fewer tool calls · 53% faster · 62% fewer tokens · 44% cheaper · file reads cut to zero"; e.g. VS Code 155k vs 670k tokens, Tokio 201k vs 573k; no C++ repo in the suite — [README](https://github.com/colbymchenry/codegraph)
- CodeGraph (measured, independent): Hono (TypeScript, ~280 files), Opus 4.8, 40 runs: tool calls 14.0 -> 6.3 (-55%), tokens 9,385 -> 7,261 (-22.6%), latency -20.3%, cost $0.338 -> $0.361 (+6.8%); "The Tool-Call Savings Reproduce — the Cost Savings Don't" — [Harrison Guo, DEV](https://dev.to/harrisonsec/i-tested-codegraph-on-hono-the-tool-call-savings-reproduce-the-cost-savings-dont-389p)
- Serena and CodeGraph (measured by a competitor's author): apache/superset 6.0.0 (~400k LOC Python/TS), Claude Code 2.1.187 with haiku/sonnet/opus, 936 runs, built-in tools disabled. Simple tasks: filesystem 1780 tokens / 10 calls / $0.063; serena 402 / 3 / $0.031; codegraph 372 / 1 / $0.022; accuracy tie (0.97-0.99). Hard tasks: filesystem 0.71 accuracy, $0.424; serena 0.85, $0.065; codegraph 0.93, $0.036. Author built graphlens, one of the four arms — [Nikita Rybalchenko, DEV](https://dev.to/neko1313_4/how-much-does-context-cost-an-ai-coding-agent-grep-vs-graph-vs-lsp-measured-across-936-runs-33m8)
- codebase-memory-mcp (claim, author preprint): 31 languages, one repo each, Claude Opus 4.6, MCP agent vs grep/read explorer: ~1,000 vs ~10,000 tokens per question (10x), 2.3 vs 4.8 tool calls, but quality 0.83 vs 0.92; worst case "macro-heavy C" 0.58 vs 1.00 because macros are not in the tree-sitter AST; graded by the first author; no separate C++ result — [arXiv 2603.27277](https://arxiv.org/html/2603.27277v1)
- Graphify (claim): ERPNext (~1M LOC) cross-tool test, n=6 questions: key-fact coverage 82.0% vs grep/read baseline 70.8% "at about 140K tokens per query" (a quality gain, not a token saving); other rows are memory benchmarks (LOCOMO 45.3%, LongMemEval-S 76%) — [BENCHMARKS.md](https://github.com/Graphify-Labs/graphify/blob/main/BENCHMARKS.md)
- Claude Context (claim): ~40% token reduction at equal retrieval quality — [README](https://github.com/zilliztech/claude-context)
- CodeGraphContext (claim): GCF output ~62% fewer response tokens — [README](https://github.com/CodeGraphContext/CodeGraphContext)
- ast-grep MCP (claim): text output ~75% fewer tokens than JSON — [repo](https://github.com/ast-grep/ast-grep-mcp)
- narsil-mcp (claim): minimal preset 61% fewer tokens than full (tool schema) — [repo](https://github.com/postrv/narsil-mcp)
- Octocode (claim): ~2.0x fewer characters than plain `gh` — [repo](https://github.com/bgauryy/octocode)
- Serena: no token figures; qualitative agent testimonials only — [repo](https://github.com/oraios/serena)
- CodeGraph's own caveat: its results leave ~80% more retrieval context resident across multi-turn sessions — [README](https://github.com/colbymchenry/codegraph)

### Inferences
- Tool-call reduction reproduces more reliably than cost reduction; for this project the real win is likely fewer exploratory Read/Grep rounds on big C++ files, not a guaranteed bill cut.
- The preprint's macro-heavy C result (0.58) is a warning for tree-sitter-only indexes on a codebase with MSVC macros and heavy templates.

### Gaps
- No benchmark of any tool on a C++ (let alone templated C++20 / MSVC) repository was found.
- No independent measurement of Graphify, codebase-memory-mcp, GitNexus or Claude Context was found.

## Q3. C++ fitness for a templated C++20 / MSVC + clang / CMake tree

### Takeaway
Ranked by C++ semantic depth: clangd-backed (native LSP tool, Serena, cclsp) > libclang/SCIP-backed graph builders (code-graph-rag hybrid, CodeGraphContext with scip-clang) > tree-sitter graph with built-in type resolution (codebase-memory-mcp) > plain tree-sitter (CodeGraph, Graphify, GitNexus, Codanna, Probe, narsil, ck) > fallback text (code-index-mcp).

### Cited Findings
- clangd needs a correct compile database; Serena requires `compile_commands.json` at repo root and rewrites relative paths into `.serena/compile_commands.json` — [Serena C/C++ guide](https://github.com/oraios/serena/blob/main/docs/03-special-guides/cpp_setup.md)
- code-graph-rag hybrid mode adds macro nodes, macro-expansion call edges, include edges via libclang + compile DB; "Nothing is skipped when libclang cannot parse a file; tree-sitter still covers it" — [doc](https://github.com/vitali87/code-graph-rag/blob/main/docs/guide/cpp-semantic-mode.md)
- CodeGraphContext uses scip-clang for C/C++ when enabled, requiring `compile_commands.json` — [README](https://github.com/CodeGraphContext/CodeGraphContext)
- codebase-memory-mcp C++ side "handles templates, namespaces, `auto` inference, and method resolution via class hierarchy" without running a language server — [README](https://github.com/DeusData/codebase-memory-mcp)
- Tree-sitter macro blindness measured: macro-heavy C 0.58 vs 1.00 for grep — [arXiv 2603.27277](https://arxiv.org/html/2603.27277v1)
- Graphify C/C++ open bugs: angle-bracket includes not resolved (#5218), decl/def reconciliation fails across Windows drives (#5145) — [#5218](https://github.com/Graphify-Labs/graphify/issues/5218), [#5145](https://github.com/Graphify-Labs/graphify/issues/5145)
- CodeGraph parses C/C++ with tree-sitter in a Rust kernel (no compile DB) — [README](https://github.com/colbymchenry/codegraph)
- code-index-mcp handles C/C++ only through its non-tree-sitter fallback — [repo](https://github.com/johnhuang316/code-index-mcp)
- Serena clangd issues: empty references before background index finishes (#2191), re-parse per call (#2190), single-header TU resolution (#1373) — [#2191](https://github.com/oraios/serena/issues/2191), [#2190](https://github.com/oraios/serena/issues/2190), [#1373](https://github.com/oraios/serena/issues/1373)

### Inferences
- The project's CMake can already emit `compile_commands.json`, so the clangd/libclang tools are usable; but clangd must understand the MSVC flags in that file (clang-cl driver mode) — the clang build tree's compile DB is the safer input.
- Lua 5.0, XML game data and Markdown specs are only covered by the graph tools (Graphify maps docs; codebase-memory and CodeGraph parse Lua); clangd covers none of them.

### Gaps
- None of the tools document MSVC-specific behaviour (`__declspec`, `#pragma`, MSVC-only intrinsics) or C++20 modules/concepts handling.

## Q4. Windows support and multi-worktree behaviour

### Takeaway
Native Windows: native LSP tool, Serena, codebase-memory-mcp, CodeGraph, Graphify, Probe, narsil, ck, code-graph-rag (Py 3.12+), Codanna (experimental), CodeGraphContext (with Kuzu/Ladybug, not default FalkorDB Lite). Docker-only: Sourcebot. Remote/paid: Sourcegraph MCP. Worktrees: only codebase-memory-mcp documents worktree handling; CodeGraph has open requests to share the index; Serena's shared HTTP server has a cross-client project bug.

### Cited Findings
- codebase-memory-mcp indexes each linked worktree as its own project, with `ignore_worktrees` to skip; index stored outside the repo in `~/.cache/codebase-memory-mcp/` — [README](https://github.com/DeusData/codebase-memory-mcp)
- CodeGraph keeps `.codegraph/` per project; sharing across worktrees is an open request — [#1236](https://github.com/colbymchenry/codegraph/issues/1236), [#3243](https://github.com/colbymchenry/codegraph/issues/3243)
- Serena shared HTTP server: "the active project is global, so one client's activate_project redirects another client's edits" — [#2284](https://github.com/oraios/serena/issues/2284)
- GitNexus recommends one HTTP MCP server for multiple concurrent agents — [README](https://github.com/abhigyanpatwari/GitNexus)
- Graphify writes `graphify-out/` per project and offers git hooks that rebuild on commit and checkout — [README](https://github.com/Graphify-Labs/graphify)
- CodeGraphContext: FalkorDB Lite default is Unix-only; Kuzu/Ladybug work on Windows natively — [README](https://github.com/CodeGraphContext/CodeGraphContext)
- Sourcebot needs Docker and Docker Compose — [repo](https://github.com/sourcebot-dev/sourcebot)
- Windows-specific open bugs: Serena Smart App Control block (#2196); CodeGraph memory-exhaustion respawn loop (#3137); GitNexus LadybugDB crash (#3706); claude-context path casing (#233); cclsp connection closed (#35); Graphify cross-drive C++ bug (#5145) — [#2196](https://github.com/oraios/serena/issues/2196), [#3137](https://github.com/colbymchenry/codegraph/issues/3137), [#3706](https://github.com/abhigyanpatwari/GitNexus/issues/3706), [#233](https://github.com/zilliztech/claude-context/issues/233), [#35](https://github.com/ktnyt/cclsp/issues/35), [#5145](https://github.com/Graphify-Labs/graphify/issues/5145)

### Inferences
- With 3 lanes in 3 worktrees, any per-worktree index costs 3x indexing and disk; tools that write into the tree (`.serena/`, `.codegraph/`, `graphify-out/`, `.codanna/`, GitNexus's `CLAUDE.md`/`AGENTS.md` edits) need `.gitignore` entries or out-of-tree config, and GitNexus's CLAUDE.md rewrite collides with this project's maintained CLAUDE.md.
- Graphify's PreToolUse "strict" guard that denies Read/Grep would interfere with lane workflows and skills that read files directly; it is optional.
- Repo on E: with caches under C:\Users matches the trigger of Graphify #5145.

### Gaps
- Whether Serena (stdio mode, one process per session) behaves correctly with three simultaneous sessions on three worktrees of the same repo was not documented.

## Q5. Overlaps and which wins for this setup

### Takeaway
LSP-bridge tools overlap with the native LSP tool; graph tools overlap with each other. For this setup: keep native LSP + clangd for precise C++ navigation and post-edit diagnostics; add at most one persistent graph (codebase-memory-mcp or CodeGraph) for broad "who calls / what depends / outline" queries across C++, Lua and Python; consider Serena only if symbolic editing is wanted, accepting its clangd issues.

### Cited Findings
- Native LSP tool already offers definition, references, hover, document/workspace symbols, implementations and call hierarchy — [tools reference](https://code.claude.com/docs/en/tools-reference.md)
- Serena adds symbolic editing (replace symbol body, insert before/after symbol, rename, safe delete) and memories on top of LSP retrieval — [repo](https://github.com/oraios/serena)
- cclsp and mcp-language-server bridge LSP into MCP, which the native plugin `lspServers` mechanism now does in-process — [cclsp](https://github.com/ktnyt/cclsp), [plugins reference](https://code.claude.com/docs/en/plugins-reference)
- In the 936-run benchmark CodeGraph beat Serena on hard-task accuracy (0.93 vs 0.85) and cost ($0.036 vs $0.065) on a Python/TS repo — [DEV](https://dev.to/neko1313_4/how-much-does-context-cost-an-ai-coding-agent-grep-vs-graph-vs-lsp-measured-across-936-runs-33m8)
- codebase-memory-mcp: index out of tree, no telemetry, worktree-aware, built-in C++ type resolution — [README](https://github.com/DeusData/codebase-memory-mcp); CodeGraph: in-tree `.codegraph/`, telemetry opt-out, per-worktree index — [README](https://github.com/colbymchenry/codegraph)
- Vector-search tools (Claude Context, ck, Codanna embeddings) target vocabulary mismatch; Probe's authors argue "AI agents don't need embedding search" because the LLM writes precise queries — [Probe README](https://github.com/probelabs/probe); Claude Context requires an embedding API and Milvus/Zilliz unless run fully local — [README](https://github.com/zilliztech/claude-context)
- Serena maintainers have an open doc request on running Serena hooks alongside CodeGraph in Claude Code — [#2234](https://github.com/oraios/serena/issues/2234)

### Inferences
- Candidate top 3 for this setup (for the writer to confirm): (1) native LSP tool + clangd (already present; tune it rather than add a bridge); (2) codebase-memory-mcp (best fit on tree hygiene and worktrees, C++ type resolution, single binary, no telemetry; weaker evidence base — author preprint only); (3) CodeGraph (best evidence incl. one independent reproduction, very active; costs a `.codegraph/` per worktree and telemetry must be turned off). Serena is the runner-up if symbolic edits matter; Graphify is a fit if a single graph over code plus the Markdown RE specs is wanted, but its hooks and Windows C++ bug need care.
- Skip for this setup: GitNexus (non-commercial licence, rewrites CLAUDE.md, Windows DB bugs), Claude Context (cloud embeddings/vector DB), code-index-mcp (no real C++ parsing), cclsp / mcp-language-server (superseded by native LSP).

### Gaps
- No head-to-head comparison of codebase-memory-mcp vs CodeGraph exists.

## Q6. Abandoned (no commit in 12 months) or not on Windows

### Takeaway
Abandoned or archived: isaacphi/mcp-language-server, jonrad/lsp-mcp, Tritlo/lsp-mcp, eas4ai/code-graph-mcp, nuanced-py (archived), wrale/mcp-server-tree-sitter (archived), sourcegraph-public-snapshot (archived). Not native on Windows: Sourcebot (Docker), CodeGraphContext's default FalkorDB Lite backend.

### Cited Findings
- isaacphi/mcp-language-server — clangd/LSP-to-MCP bridge; last default-branch commit 2025-06-03 (repo pushed 2026-03-01), 1,622 stars, BSD-3-Clause: abandoned — [repo](https://github.com/isaacphi/mcp-language-server)
- jonrad/lsp-mcp — LSP-to-MCP, last commit 2025-03-31, 194 stars: abandoned — [repo](https://github.com/jonrad/lsp-mcp)
- Tritlo/lsp-mcp — LSP-to-MCP, last commit 2025-07-21, 128 stars: abandoned — [repo](https://github.com/Tritlo/lsp-mcp)
- entrepeneur4lyf/code-graph-mcp (now eas4ai/code-graph-mcp) — last commit 2025-07-26, 88 stars: abandoned — [repo](https://github.com/eas4ai/code-graph-mcp)
- nuanced-dev/nuanced (redirects to nuanced-py) — Python call graphs only, archived, last commit 2025-06-26 — [repo](https://github.com/nuanced-dev/nuanced-py)
- wrale/mcp-server-tree-sitter — archived, last commit 2026-05-21 — [repo](https://github.com/wrale/mcp-server-tree-sitter)
- sourcegraph/sourcegraph-public-snapshot — archived, last commit 2024-08-22 (Sourcegraph MCP lives on the paid hosted/self-hosted product) — [repo](https://github.com/sourcegraph/sourcegraph-public-snapshot)
- sourcebot-dev/sourcebot — Windows only through Docker / Docker Compose — [repo](https://github.com/sourcebot-dev/sourcebot)
- CodeGraphContext default FalkorDB Lite — "Unix-only (Linux/macOS/WSL)"; Windows must use KuzuDB/LadybugDB — [README](https://github.com/CodeGraphContext/CodeGraphContext)

### Inferences
- Borderline (alive but quiet): ktnyt/cclsp (last commit 2026-02-22), cased/kit (2026-03-03), pdavis68/RepoMapper (2025-12-08).

### Gaps
- None beyond those listed in earlier sections.
