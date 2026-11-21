# Session memory and handoff tools for Claude Code (as of 2026-11-19)

Scope: tools that carry context between sessions and between parallel agents. Each one is judged against this setup: Windows 11, Claude Code desktop and CLI, up to 3 lanes in git worktrees plus a coordinator, a large C++20 codebase, and existing auto-memory (MEMORY.md) + CLAUDE.md + a git-backed roadmap store + ReVa MCP.
Repo stats (stars, license, last commit) come from `gh api repos/...` run on 2026-11-19. "claim" = vendor-reported, "measured" = independent.

## Baseline: Claude Code's own memory, compaction and session features

### Takeaway
Claude Code already gives you per-repo auto memory that all worktrees share (200 lines / 25KB of MEMORY.md injected at start, topic files read on demand), per-subagent persistent memory, compaction that re-injects the root CLAUDE.md, and resume/fork of sessions. Any third-party tool has to add something beyond this: search over past transcripts, auto-capture of tool activity, or coordination between agents.

### Cited Findings
- Auto memory scope is "Per repository, shared across worktrees". It loads into every session, "first 200 lines or 25KB" — [Claude Code docs: memory](https://code.claude.com/docs/en/memory)
- Storage is `~/.claude/projects/<project>/memory/`. The `<project>` path comes from the git repository, "so all worktrees and subdirectories within the same repo share one auto memory directory". It is machine-local and not shared across machines or cloud environments. The location can be changed with `autoMemoryDirectory` — [Claude Code docs: memory](https://code.claude.com/docs/en/memory)
- Topic files (anything other than MEMORY.md) are not loaded at startup. Claude reads them on demand with its normal file tools. Claude Code warns when MEMORY.md nears the 200-line / 25KB limit — [Claude Code docs: memory](https://code.claude.com/docs/en/memory)
- The main conversation's auto memory is not loaded into subagents (forks excepted). Subagents get their own persistent memory through the `memory:` frontmatter field, with scopes `user` (`~/.claude/agent-memory/<agent>/`), `project` (`.claude/agent-memory/<agent>/`, "shareable via version control") and `local` (`.claude/agent-memory-local/<agent>/`). The first 200 lines / 25KB of that MEMORY.md are injected. The field does nothing when auto memory is off — [Claude Code docs: sub-agents](https://code.claude.com/docs/en/sub-agents)
- Non-fork subagents do load the full CLAUDE.md hierarchy (Explore and Plan skip it) — [Claude Code docs: sub-agents](https://code.claude.com/docs/en/sub-agents)
- After `/compact`, the project-root CLAUDE.md is re-read from disk and re-injected. Nested CLAUDE.md files and `paths:`-scoped rules reload on demand. Instructions given only in conversation can be lost — [Claude Code docs: memory](https://code.claude.com/docs/en/memory)
- What compaction keeps can be steered with a "Compact Instructions" section in CLAUDE.md or with `/compact <focus>`. MCP tool definitions are deferred by default, so only tool names and server instructions use context until a tool is called — [How Claude Code works](https://code.claude.com/docs/en/how-claude-code-works)
- Sessions are stored as JSONL under `~/.claude/projects/`. `--continue` / `--resume` reopen a session, and `--fork-session` / `/branch` copy it. The `/resume` picker shows sessions from the current worktree by default and can widen to other worktrees — [How Claude Code works](https://code.claude.com/docs/en/how-claude-code-works)
- Path-scoped rules in `.claude/rules/` load only when Claude touches matching files. CLAUDE.md files should stay under 200 lines. Block-level HTML comments are stripped before injection, so they cost no tokens — [Claude Code docs: memory](https://code.claude.com/docs/en/memory)
- A gitignored `CLAUDE.local.md` exists only in the worktree where it was created. The docs suggest importing a file from the home directory to share it across worktrees — [Claude Code docs: memory](https://code.claude.com/docs/en/memory)

### Inferences
- The user's existing MEMORY.md + topic files are exactly the built-in auto-memory design. Worktree lanes under `.claude/worktrees/` already share it because they are worktrees of one repo.
- A subagent with `memory: project` writes into the worktree's checkout (`.claude/agent-memory/...`). Each lane would then get its own copy, merged through git like code. `memory: user` or `local` avoids merge conflicts but is not versioned. Lane sessions started with `start_session` are full sessions, not subagents, so they get auto memory directly.
- Sessions in the desktop app expose a transcript-search tool (`search_session_transcripts`, seen in this session's tool list). No public doc for it was fetched, so treat it as unverified.

### Gaps
- No Anthropic-published figure for the token savings of auto memory or compaction was found.
- The `cleanupPeriodDays` default (transcript retention) was not captured. The memory page only links to the settings reference.

## claude-mem (thedotmack/claude-mem)

### Takeaway
The most popular Claude Code memory plugin. Hooks send every tool call to a local Bun worker, an observer LLM compresses that into "observations" in SQLite + Chroma, and the next session gets an index of them injected. It is powerful but heavy: a daemon, extra LLM calls, a history of Windows hook-blocking bugs, and it overlaps auto-memory.

### Cited Findings
- Stats: 118,419 stars, Apache-2.0, last commit 2026-11-19 (gh api) — [GitHub](https://github.com/thedotmack/claude-mem)
- Install: `/plugin marketplace add thedotmack/claude-mem` + `/plugin install claude-mem` or `npx claude-mem install`. `npm install -g` installs only the SDK, not the hooks or worker — [README](https://github.com/thedotmack/claude-mem)
- Architecture: lifecycle hooks pass each tool call to "a small local worker" (an HTTP service managed by Bun). An observer model (Claude, OpenRouter, Gemini and other providers) turns the activity into observations and session summaries, stored in SQLite at `~/.claude-mem/claude-mem.db` with FTS5 keyword search plus Chroma vector search — [README](https://github.com/thedotmack/claude-mem)
- Token mechanics (claim): search returns a compact index at "~50-100 tokens/result", and `get_observations` returns full details at "~500-1,000 tokens/result". SessionStart injects a compact, layered index. The default `CLAUDE_MEM_CONTEXT_OBSERVATIONS` is `50` observations injected per session — [README](https://github.com/thedotmack/claude-mem); [configuration doc](https://github.com/thedotmack/claude-mem/blob/main/docs/public/configuration.mdx)
- Optional Cloud Sync to cmem.ai and a hosted "CMEM Pro" memory. The README also promotes a third-party "CMEM" crypto token that the author endorses — [README](https://github.com/thedotmack/claude-mem)
- Windows: the hooks run a `claude-mem.exe` launcher placed in `%LOCALAPPDATA%\claude-mem\bin` and do not need Git Bash — [README](https://github.com/thedotmack/claude-mem)
- Windows bugs: on Windows 11, a port conflict left the UserPromptSubmit hook blocking prompts for about 15 minutes (#2996). After an update, every prompt was blocked until the stale PID file was deleted and the port changed (#2899). An open issue (2026-10-28) reports that the Windows "My Work" refresh fails every 10 minutes — [issue #2996](https://github.com/thedotmack/claude-mem/issues/2996); [issue #2899](https://github.com/thedotmack/claude-mem/issues/2899); [issues search "windows"](https://github.com/thedotmack/claude-mem/issues?q=windows)
- Worktrees: the code resolves linked worktrees and bare-repo worktrees to their parent project (`src/utils/worktree.ts`) and has a "WorktreeAdoption" service. A comment there records a bug where a git call froze the worker's health endpoint for up to 15 s and caused restart loops (#4741) — [worktree.ts](https://github.com/thedotmack/claude-mem/blob/main/src/utils/worktree.ts); [WorktreeAdoption.ts](https://github.com/thedotmack/claude-mem/blob/main/src/services/infrastructure/WorktreeAdoption.ts)

### Inferences
- Fit: it stores memory in one per-user DB keyed by repo, so all 3 lanes would share observations. It also injects up to 50 observations per session on top of MEMORY.md, which is a recurring token cost that partly duplicates auto memory.
- Every tool call costs a call to the observer LLM (your Claude subscription or API), so it can raise total token spend while lowering re-discovery.
- Risk: it runs a daemon, has a history of blocking prompts on Windows, the project changes very fast (v13.x), and there is cloud and crypto noise. Its distinct value over the current setup is automatic capture plus search of what earlier sessions read and did.

### Gaps
- No independent or vendor A/B figure for token savings on coding tasks was found.
- Exactly what "WorktreeAdoption" does on merge was not confirmed.

## Basic Memory (basicmachines-co/basic-memory)

### Takeaway
A local-first knowledge base of Markdown files with wiki-links, indexed in SQLite (full-text plus FastEmbed vectors). It works as an MCP server plus a Claude Code plugin with hooks and skills. It is the closest to the user's existing file-based memory, adding search and a graph. A paid cloud tier is optional.

### Cited Findings
- Stats: 4,635 stars, AGPL-3.0, last commit 2026-11-19 — [GitHub](https://github.com/basicmachines-co/basic-memory)
- Notes live "as Markdown files that both you and your AI can read, write". It is "Just files plus a local SQLite index. No servers required". Hybrid full-text plus vector ranking uses FastEmbed embeddings. SQLite is the default, Postgres optional — [README](https://github.com/basicmachines-co/basic-memory)
- Claude Code setup: `claude mcp add basic-memory -- uvx --prerelease=allow basic-memory mcp`. A separate Claude Code plugin adds hooks and skills that call the MCP server — [README](https://github.com/basicmachines-co/basic-memory)
- Windows: Claude Desktop is listed for macOS / Windows / Linux, and the test suite has a `windows` pytest marker. An open issue (2026-11-16) reports Windows Smart App Control blocking the unsigned launcher in v0.24.1 — [README](https://github.com/basicmachines-co/basic-memory); [issues search "windows"](https://github.com/basicmachines-co/basic-memory/issues?q=windows)
- A paid cloud version uses the same Markdown files. Some projects can be set to cloud through an API key — [README](https://github.com/basicmachines-co/basic-memory)
- Open feature request: "FTS-based session/conversation search for episodic recall" (2026-03-21), so session search is not built in — [issues](https://github.com/basicmachines-co/basic-memory/issues?q=windows)

### Inferences
- It overlaps heavily with MEMORY.md + topic files and with `docs/re/`. Its added value is semantic search and a link graph over a Markdown corpus. It could index the user's existing reverse-engineering Markdown specs.
- Parallel worktrees: one per-user project directory gives a shared store. Concurrent writes from 3 lanes go through one SQLite index; no conflict guarantees were found.
- The AGPL license only matters if it is redistributed; it is a tool, not linked into the engine.

### Gaps
- No token-savings measurement found.

## mem0 / OpenMemory MCP (mem0ai/mem0)

### Takeaway
A general-purpose memory layer for apps and agents (it extracts facts with an LLM and stores them in a vector store). It has the strongest benchmark marketing, all of it vendor claims and disputed by competitors. It needs an LLM and embeddings (OpenAI by default) or Docker for self-hosting. It is not coding-specific and is a poor fit here.

### Cited Findings
- Stats: 69,247 stars, Apache-2.0, last commit 2026-11-18 — [GitHub](https://github.com/mem0ai/mem0)
- Claim (README, new algorithm): LoCoMo 92.5 (old 71.4), LongMemEval 94.4 (old 67.8) at about 7K tokens per query, plus BEAM (1M) 64.1. The evaluation framework is open-sourced — [README](https://github.com/mem0ai/mem0); [mem0ai/memory-benchmarks](https://github.com/mem0ai/memory-benchmarks)
- Claim (paper): 26% relative improvement in LLM-as-judge score over OpenAI memory on LoCoMo, 91% lower p95 latency and more than 90% token savings compared with full-context — [arXiv 2504.19413](https://arxiv.org/abs/2504.19413)
- Dispute: Zep's blog argues that Mem0's paper misconfigured Zep and that a correct Zep setup beats Mem0 on LoCoMo — [Zep blog](https://blog.getzep.com/lies-damn-lies-statistics-is-mem0-really-sota-in-agent-memory/). Secondary sources give the figures as Zep 84%, Mem0's re-measurement 58.44%, and Zep's corrected 75.14%. These were not verified against primary sources — [braindetox summary](https://braindetox.kr/en/posts/memory_benchmark_scores_reliability_2026.html)
- Requirements: "Mem0 requires an LLM to function, with `gpt-5-mini` from OpenAI as the default". Self-hosting is `docker compose up` on localhost:3000 with auth on — [README](https://github.com/mem0ai/mem0)
- `mem0ai/openmemory` is now a different product: an "Open-source CLI & TUI to port AI coding sessions across Claude Code, Codex, and OpenCode". It has 108 stars, MIT, last commit 2026-07-29 — [GitHub](https://github.com/mem0ai/openmemory)

### Inferences
- It sends content to an LLM and embedding API (cloud by default), which raises a privacy and cost risk for the codebase. Docker on Windows adds setup weight. It gives no advantage over files for coding handoffs.

### Gaps
- Where the OpenMemory MCP server lives today (it used to be in the mem0 monorepo) and its Windows status were not confirmed.

## Graphiti / Zep (getzep/graphiti)

### Takeaway
A temporal knowledge-graph engine (facts with validity windows, hybrid retrieval). It needs a graph DB (Neo4j / FalkorDB) and an LLM with structured output. It is strong for evolving facts in conversational agents but overkill and heavy for a coding handoff.

### Cited Findings
- Stats: 32,946 stars, Apache-2.0, last commit 2026-11-19 — [GitHub](https://github.com/getzep/graphiti)
- Facts are entity-relationship-entity triplets "with temporal validity windows". Retrieval combines semantic embeddings, BM25 and graph traversal — [README](https://github.com/getzep/graphiti)
- It "defaults to OpenAI for LLM inference and embedding" (`OPENAI_API_KEY`). Anthropic, Gemini, Groq, Azure and OpenAI-compatible or local endpoints are supported. Backends are Neo4j (`bolt://localhost:7687`), FalkorDB, FalkorDBLite or Neptune — [README](https://github.com/getzep/graphiti)
- Claim (paper): Zep scores 94.8% vs MemGPT's 93.4% on DMR, and gains up to 18.5% accuracy with about 90% lower latency on LongMemEval — [arXiv 2501.13956](https://arxiv.org/abs/2501.13956)

### Inferences
- It needs a DB service plus LLM calls on every write. Windows would most likely run it through Docker or a local Neo4j install. It is not justified when the handoff state already lives in a git-backed JSON roadmap.

### Gaps
- No Windows-specific notes or a Claude Code plugin were found in the README excerpt (the repo does ship an MCP server folder, not checked).

## Letta (letta-ai/letta, letta-code, claude-subconscious)

### Takeaway
Letta (formerly MemGPT) is a stateful-agent platform. Its coding product, Letta Code, is a separate harness that replaces Claude Code rather than plugging into it. The Claude Code plugin "claude-subconscious" is a demo that needs a Letta cloud account. Letta's most useful contribution is evidence that plain files plus grep perform about as well as dedicated memory products.

### Cited Findings
- letta: 25,494 stars, Apache-2.0, last commit 2026-10-18 — [GitHub](https://github.com/letta-ai/letta)
- letta-code: 3,875 stars, Apache-2.0, last commit 2026-11-19. It is a "stateful agent harness" with git-tracked "MemFS" context and its own subagents. Letta Cloud is the default backend, with a local option. Automatic "dreaming" (memory consolidation) is disabled by default on native Windows. Supported wheels include Windows x64 — [GitHub README](https://github.com/letta-ai/letta-code)
- claude-subconscious: 2,917 stars, MIT, last commit 2026-09-25. After each response, a Stop hook relays the transcript to a Letta agent, which feeds memory blocks back. It needs a Letta account / API key, and its README calls it a demo not intended for production — [GitHub](https://github.com/letta-ai/claude-subconscious); [secondary write-up](https://aitoolly.com/ai-news/article/2026-03-27-letta-ai-introduces-claude-subconscious-a-background-agent-for-persistent-memory-in-claude-code)
- Claim (Letta, reported second-hand): an agent using only file tools (grep, search_files, open) scored 74.0% on LoCoMo, against 68.5% reported for Mem0's graph variant. Mem0's later 92.5% has not been re-compared — [secondary summary](https://signals.aktagon.com/articles/2026/02/benchmarking-ai-agent-memory-is-a-filesystem-all-you-need/)

### Inferences
- Letta Code is skip for this setup, because it would mean leaving Claude Code. claude-subconscious is skip too: it is a demo, sends transcripts to the Letta cloud, and duplicates auto memory. The filesystem result supports keeping the user's file-based memory.

### Gaps
- Letta's primary blog post on the filesystem benchmark was not fetched; the figure is second-hand.

## Cipher / ByteRover CLI (campfirein/byterover-cli)

### Takeaway
Cipher was renamed ByteRover CLI (`brv`), and the GitHub repo is now archived (last push 2026-06-25). It ships under the Elastic-2.0 license, with a cloud sync product and a shell installer only for macOS and Linux. Skip it: archived, not native Windows, and source-available rather than open source.

### Cited Findings
- `gh api repos/campfirein/cipher` redirects to `campfirein/byterover-cli`: 4,944 stars, `archived: true`, last commit 2026-06-25 — [GitHub](https://github.com/campfirein/byterover-cli)
- License badge Elastic 2.0. It runs as an "Interactive REPL CLI" that builds a context tree, with cloud sync (`brv vc push/pull`), MCP integration and 20 LLM providers — [README](https://github.com/campfirein/byterover-cli)
- Claim: LoCoMo 96.1%, LongMemEval-S 92.8% (LLM-as-judge) — [README](https://github.com/campfirein/byterover-cli); [arXiv 2604.01599](https://arxiv.org/abs/2604.01599)
- The shell installer lists supported platforms as macOS ARM64/x64 and Linux x64/ARM64 only — [README](https://github.com/campfirein/byterover-cli)

### Inferences
- The archive likely reflects a move to closed development or the hosted product (not confirmed).

### Gaps
- No archive notice or successor repo was found. Third-party pages still describe it as active.

## Memory Bank pattern (Cline-style)

### Takeaway
This is a prompt convention, not software: six Markdown files (projectbrief, productContext, activeContext, systemPatterns, techContext, progress) that the agent is told to read at the start of every task. The user's CLAUDE.md + MEMORY.md + roadmap store already cover it.

### Cited Findings
- "Memory Bank is a documentation methodology, not a built-in feature". The files are `projectbrief.md`, `productContext.md`, `activeContext.md`, `systemPatterns.md`, `techContext.md` and `progress.md`. A custom instruction says Cline "MUST read ALL memory bank files at the start of EVERY task". The page states no token cost — [Cline docs](https://docs.cline.bot/prompting/cline-memory-bank)

### Inferences
- Reading every file every task is the opposite of token-saving at scale. The progressive-disclosure index already in use (MEMORY.md plus on-demand topic files) is cheaper. Skip.

### Gaps
- None material.

## beads (steveyegge/beads, now gastownhall/beads)

### Takeaway
A dependency-aware issue tracker built for agents (`bd` CLI), stored in Dolt (a versioned SQL database). It primes about 1-2k tokens of "ready work" context at SessionStart and after compaction, and all worktrees share one workspace. It is the strongest overlap with the user's roadmap store, so it is a replacement candidate, not an addition.

### Cited Findings
- `steveyegge/beads` now resolves to `gastownhall/beads`: 28,848 stars, MIT, last commit 2026-11-19 — [GitHub](https://github.com/gastownhall/beads)
- It is a "Distributed graph issue tracker for AI agents, powered by Dolt" on macOS, Linux, Windows and FreeBSD. The default mode embeds Dolt in-process with a single writer (`.beads/embeddeddolt/`). Server mode (`bd init --server`, an external `dolt sql-server`) is for multiple concurrent writers. Sync uses `bd dolt push/pull` against `refs/dolt/data` on the git remote — [README](https://github.com/gastownhall/beads)
- Worktrees: "All worktrees in the same repository use the same beads workspace unless you override discovery with `BEADS_DIR`", and no separate sync branch is needed — [worktrees doc](https://github.com/gastownhall/beads/blob/main/docs/reference/worktrees.md)
- Claude Code: `bd setup claude` writes a SessionStart hook running `bd prime --hook-json`, and SessionStart also fires after compaction. "Session starts → `bd prime` injects ~1-2k tokens of context" (claim). The docs explain why they chose CLI + hooks over MCP. A plugin variant also exists — [Claude Code integration doc](https://github.com/gastownhall/beads/blob/main/docs/integrations/claude-code.md)
- Open Windows issues include: Dolt spawns allocating a console and failing `dolt init` with 0x800700E8 in Windows Terminal (2026-08-30), and directory-mounted Windows volumes breaking workspace and hook resolution (2026-11-06). The README links Windows antivirus false-positive guidance — [issues search "windows"](https://github.com/gastownhall/beads/issues?q=windows); [README](https://github.com/gastownhall/beads)

### Inferences
- The roadmap store already provides git-backed tasks, groups, claims and handoff notes for 3 lanes plus a coordinator. Adopting beads would mean migrating, plus the risk of the Dolt binary on Windows, so the gain is mainly the `bd prime` auto-injection, which a 20-line SessionStart hook calling `roadmap_store.py` could replicate.
- 3 lanes writing at once would need server mode (an extra daemon) or would serialize on the embedded single writer.

### Gaps
- No measured token or time savings found.

## Official MCP "memory" knowledge-graph server (modelcontextprotocol/servers, src/memory)

### Takeaway
A minimal reference server: entities, relations and observations stored in a local JSONL file, with tools to create, read and delete them. It is maintained and has a Windows recipe, but it has no search beyond names and observations, no auto-capture, and no repo awareness. It is a weaker version of what MEMORY.md already does.

### Cited Findings
- It offers "A basic implementation of persistent memory using a local knowledge graph". The npm package is `@modelcontextprotocol/server-memory`. Storage is `MEMORY_FILE_PATH` (JSONL, default `memory.jsonl` in the server directory) — [README](https://github.com/modelcontextprotocol/servers/tree/main/src/memory)
- Windows: launch through `cmd /c npx -y @modelcontextprotocol/server-memory` — [README](https://github.com/modelcontextprotocol/servers/tree/main/src/memory)
- The servers repo has 92,112 stars and its last commit was 2026-11-18. The last commit touching `src/memory` was 2026-11-18. The repo license reports NOASSERTION (gh api) — [GitHub](https://github.com/modelcontextprotocol/servers)

### Inferences
- Skip, already covered. Its JSON graph is not human-curated like topic files, and concurrent lanes appending to one JSONL risk lost writes.

### Gaps
- Concurrency behaviour with several writers is not documented in the excerpt read.

## Serena memories (oraios/serena)

### Takeaway
Serena is mainly a symbol-level code-navigation and editing MCP backed by language servers (relevant to the "re-reading big files" token sink). Its memory feature is a deliberately minimal set of Markdown files in `.serena/memories/`, committed with the code, with only the name list injected. That is the same design as the user's auto memory, so the memory part adds nothing. The retrieval part is covered by another researcher's scope.

### Cited Findings
- Stats: 31,540 stars, last commit 2026-11-19; the license reports NOASSERTION in gh api — [GitHub](https://github.com/oraios/serena)
- Project memories live in `.serena/memories/` and global ones in `~/.serena/memories/global/`. The agent receives the memory name list up front and reads entries by deliberate choice. The design rejects database-backed memory and "prefers references to search". The harness "does not inject memory content on the agent's behalf". Memory can be disabled — [Serena memories doc](https://github.com/oraios/serena/blob/main/docs/02-usage/045_memories.md)
- The README says symbolic tools are "much more token-efficient than typical alternatives" (claim, no figure) — [README](https://github.com/oraios/serena)

### Inferences
- Project memories committed per worktree would merge through git like code. Disable Serena memories if Serena is adopted for navigation, to avoid two memory systems.

### Gaps
- No measured token figure was found for Serena memory.

## episodic-memory (obra/episodic-memory)

### Takeaway
A Claude Code plugin that indexes past conversation transcripts and offers semantic search over them through MCP plus a "remembering-conversations" skill. It fills the one gap auto memory leaves: recalling why a decision was made in an earlier session without re-deriving it. It is light (local index, Claude Haiku summaries through your existing Claude Code auth).

### Cited Findings
- Stats: 506 stars, MIT, last commit 2026-09-10 — [GitHub](https://github.com/obra/episodic-memory)
- It provides "Semantic search for Claude Code, Codex, Cursor, opencode, and Oh My Pi (OMP) conversations". The install is `/plugin install episodic-memory@superpowers-marketplace`. It indexes conversations at session end and exposes MCP search and view tools — [README](https://github.com/obra/episodic-memory)
- Summaries use Claude Code authentication by default (model `haiku`, fallback `sonnet`). `EPISODIC_MEMORY_SKIP_SUMMARIES=1` disables them, and `EPISODIC_MEMORY_DISABLE_AUTO_SYNC=1` stops automatic indexing. Search queries go through a dispatched `search-conversations` agent — [README](https://github.com/obra/episodic-memory)

### Inferences
- It needs no daemon and writes nothing into the repo (it reads `~/.claude/projects` transcripts), so it fits parallel worktrees, which all write transcripts to the same place. It overlaps with the desktop app's own transcript search.

### Gaps
- Windows support is not stated in the README, and an issue search was not run for this repo. It uses sqlite-vec, whose Windows binaries usually work, but this is unverified.

## mcp_agent_mail (Dicklesworthstone), coordination between parallel agents

### Takeaway
This is not memory but the closest tool to "handoff between parallel agents": a mail-like MCP server with agent identities, threaded inboxes, and advisory file reservations enforced by a pre-commit guard, archived in git. The Python version is deprecated in favour of a Rust rewrite with a PowerShell installer.

### Cited Findings
- `mcp_agent_mail` (Python): 2,240 stars, last commit 2026-11-01, and its description says "Deprecated: superseded by mcp_agent_mail_rust" — [GitHub](https://github.com/Dicklesworthstone/mcp_agent_mail)
- `mcp_agent_mail_rust`: 265 stars, created 2026-02-05, last push 2026-11-19; the license reports NOASSERTION — [GitHub](https://github.com/Dicklesworthstone/mcp_agent_mail_rust)
- Features: 45 MCP tools and 25 resources, a git-backed archive with SQLite indexing, and "Advisory File Reservations" on file globs with a pre-commit guard. "Messages stored in a per-project archive, not in agent context windows". A Windows PowerShell one-liner installer (`install.ps1`) verifies signed SHA256SUMS — [README](https://github.com/Dicklesworthstone/mcp_agent_mail_rust)

### Inferences
- The user's lane design (one group per lane, claims in the roadmap store, coordinator relay) already covers file ownership and handoffs. Agent Mail would add direct lane-to-lane messaging and enforced file leases, at the cost of a server and 45 tool names (deferred by Claude Code's tool search, so the context cost is small). Worth a look only if lanes start colliding.

### Gaps
- Real-world Windows reliability is unknown, and the Rust rewrite is young (9 months).

## Other candidates (brief)

### Takeaway
None of the following beats the shortlist for this setup. Most are general memory platforms (cloud/LLM-dependent) or session managers without memory.

### Cited Findings
- mcp-memory-service (doobidoo): 2,136 stars, Apache-2.0, last commit 2026-11-19. Local SQLite with ONNX embeddings ("memory never leaves your infrastructure"), Claude Code hooks for automatic session capture, and scheduled "autonomous consolidation". Installed with `uv tool install "mcp-memory-service[sqlite]"`. A Cloudflare backend is optional — [GitHub](https://github.com/doobidoo/mcp-memory-service)
- supermemory: 31,674 stars, MIT, last commit 2026-11-18. A "Memory and context engine + app"; it says it can run fully locally — [GitHub](https://github.com/supermemoryai/supermemory)
- cognee: 33,415 stars, Apache-2.0, last commit 2026-11-18. An "open-source AI memory platform for agents" — [GitHub](https://github.com/topoteretes/cognee)
- claude-task-master: 28,405 stars, last commit 2026-04-23. An AI task-management system (overlaps the roadmap store) — [GitHub](https://github.com/eyaltoledano/claude-task-master)
- ruflo (formerly ruvnet/claude-flow): 82,102 stars, MIT, last commit 2026-11-19. A swarm "agent harness" that includes memory — [GitHub](https://github.com/ruvnet/ruflo)
- Session managers without memory: claude-squad (8,836 stars, AGPL-3.0, last commit 2026-11-13) and ccmanager (1,279 stars, MIT, last commit 2026-10-27) manage multiple agent terminals and worktrees — [claude-squad](https://github.com/smtg-ai/claude-squad); [ccmanager](https://github.com/kbwo/ccmanager)
- claude-context (zilliztech): 12,707 stars, MIT. A code-search MCP (vector index of the codebase); retrieval, not memory — [GitHub](https://github.com/zilliztech/claude-context)

### Inferences
- mcp-memory-service is the most local and Windows-friendly of the general memory servers, but it adds another auto-injection layer on top of MEMORY.md.
- claude-squad and ccmanager duplicate the desktop app's session and worktree management.

### Gaps
- Windows status and Claude Code integration for supermemory, cognee and ruflo were not checked.

## Abandoned or not usable on native Windows

### Takeaway
Two tools are out: cc-sessions is stale and ByteRover is archived. ByteRover's installer is also macOS/Linux only.

### Cited Findings
- cc-sessions (GWUDCAP): last commit 2025-10-17, more than 12 months ago, so abandoned. 1,538 stars, MIT. It was an opinionated hook framework (DAIC enforcement, task files, context agents) — [GitHub](https://github.com/GWUDCAP/cc-sessions)
- ByteRover CLI / Cipher: archived, last commit 2026-06-25. The shell installer supports macOS and Linux only — [GitHub](https://github.com/campfirein/byterover-cli)
- Letta Code native Windows: it runs, but automatic dreaming is off by default on Windows — [letta-code README](https://github.com/letta-ai/letta-code)

### Inferences
- None.

### Gaps
- None.

## Overlap with existing setup and shortlist for a writer

### Takeaway
Most of this category is already covered by the user's auto memory, CLAUDE.md, roadmap store and lane design. The genuinely additive options are (1) transcript recall, through episodic-memory or the desktop app's built-in transcript search; (2) automatic capture of tool activity with claude-mem, the most capable but heaviest and riskiest on Windows; and (3) better use of built-in features at no install cost: subagent `memory:`, "Compact Instructions", path-scoped rules, and a SessionStart hook that primes from `roadmap_store.py`, which is the same pattern as beads' `bd prime`. Graph and vector platforms (mem0, Graphiti, Letta, cognee, supermemory) are skips for a single-developer coding setup: their benchmarks are vendor claims on chat-memory tests (LoCoMo, LongMemEval), not coding A/Bs.

### Cited Findings
- The leading benchmarks are conversational recall (LoCoMo, LongMemEval), and the vendors' numbers conflict (mem0 92.5, ByteRover 96.1, Zep vs Mem0 dispute) — [mem0 README](https://github.com/mem0ai/mem0); [ByteRover README](https://github.com/campfirein/byterover-cli); [Zep blog](https://blog.getzep.com/lies-damn-lies-statistics-is-mem0-really-sota-in-agent-memory/)
- Letta reportedly found file tools alone reach 74.0% on LoCoMo — [secondary summary](https://signals.aktagon.com/articles/2026/02/benchmarking-ai-agent-memory-is-a-filesystem-all-you-need/)
- beads documents the SessionStart-after-compaction priming pattern at "~1-2k tokens" — [beads Claude Code doc](https://github.com/gastownhall/beads/blob/main/docs/integrations/claude-code.md)
- Serena's maintainers deliberately chose plain Markdown, name-list-only injection and no DB for memory — [Serena memories doc](https://github.com/oraios/serena/blob/main/docs/02-usage/045_memories.md)

### Inferences
- Suggested top 3 for this setup:
  1. Built-in features tuned, since everything is already installed: subagent `memory:` for `re-analyst`, `reviewer` and `lane`; Compact Instructions; path-scoped rules; a roadmap-priming SessionStart hook.
  2. episodic-memory for transcript recall: light, local, MIT, no repo writes. Windows is unverified.
  3. claude-mem only as a trial in one lane: biggest potential reduction in re-discovery, but it adds a daemon, observer-LLM cost and a history of Windows prompt-blocking bugs, so pilot it before rollout.
- Skip as already covered: Memory Bank, MCP memory server, Serena memories, beads (unless replacing the roadmap store), Basic Memory (unless semantic search over `docs/re` is wanted).
- No tool found addresses "re-reading the same big files in each lane" directly. That is a code-retrieval problem (Serena symbols, claude-context, LSP), outside memory tools.

### Gaps
- No independent, measured coding-agent A/B (tokens per task with vs without a memory tool) was found for any tool. Every efficiency figure here is a vendor claim or a chat-memory benchmark.
