# Fix your clangd before buying graphs

*Dated report, 2026-11-19; history, not kept current. Per-category research notes with sources: [`agent_tokens_2026-11-19/`](agent_tokens_2026-11-19/). Pick 1 landed on dev 6c420b8c.*

The best token saver for this setup is one you already have: the `eaw-clangd` plugin that gives Claude Code its LSP tool. Right now it is half-blind in fresh lane worktrees. A spot check in this worktree (2026-11-19, no build tree yet) ran `findReferences` on `TacticalBattle::issue_orders`. It returned only the declaration, while Grep finds the real call at `engine/apps/sim/src/battle_ai.cpp:3364`. The reason is that `tools/lsp/clangd_launch.py` finds no `compile_commands.json` until the lane has built once. So rank #1 is making clangd work in every worktree and telling agents to use it before Grep and Read. It is the only option that understands templates and macros, it adds no new dependency, and its one independent measurement shows **−31% to −33% tool-output tokens** on a find-references task ([CircleCI](https://circleci.com/blog/claude-code-lsp/)). Rank #2 is a **single trial lane of codebase-memory-mcp**. It is the graph tool that fits this tree best: index kept outside the repo, aware of worktrees, offline, and it adds C++ type resolution on top of tree-sitter. Its savings figure, though, is the author's own claim. Rank #3 is a **context diet using built-in settings**, because `CLAUDE.md` (21.9 KB, about 5.5k tokens) and `lane.md` (18.4 KB, about 4.6k tokens) load into every lane and most subagents. Log trimmers such as RTK, memory daemons such as claude-mem, and Graphify (your reference point) do not make the top three. Your build scripts already turn logs into summaries, auto-memory plus the roadmap store already cover handoff, and each of these tools has an open Windows or C++ bug that hits this machine. One caveat applies throughout: no tool in any category has a published measurement on a C++ codebase, so the trial below is what settles the question.

## Trial result (2026-11-19, after pick 1 landed on dev 6c420b8c)

Headless `claude -p` needs a separate CLI login, so the trial ran as 12 Opus subagents in one session instead. The arm was set in
the prompt: A "no LSP; Grep, Glob, Read only", B "LSP first". Both arms had the same tools and prompt prefix, and all 12 ran at
once. Tokens come from each subagent's transcript, counting each API message once. "Fresh" means input + cache write + output.

| Task | Arm | Correct | Median fresh tokens | Median API calls | Tool calls (median) |
|---|---|---|---|---|---|
| T1: callers of `TacticalBattle::issue_orders` (truth: `battle_ai.cpp:3364`, `tactical_battle_services.cpp:180`) | A grep | 3/3 | 74,726 | 4 | 5 |
| | B LSP | 3/3 | 74,537 (−0.3%) | 4 | 6 |
| T2: type of `tick_` at `tactical_battle_services.cpp:180` (truth: nested `TacticalBattle::TickContext`, `tactical_battle.hpp:2959`, 5 fields; a second `TickContext` exists in `jobs/tick.hpp:152`) | A grep | 3/3 | 75,245 | 4 | 5 |
| | B LSP | 3/3 | 74,791 (−0.6%) | 6 | 6 |

What it shows:

- **No token difference on lookups grep can already answer.** Both arms answered everything correctly, and the medians are within 1%. Grep did not fall for the duplicate `TickContext`. Under the decision rule below (≥15% fewer fresh tokens, or better recall), the "LSP first" steering does not earn its keep on tasks like these.
- **LSP adds overhead in subagents.** The LSP tool is deferred there, so every B run spent a `ToolSearch` call loading it first. One B run's go-to-definition landed on `issue_orders` instead of `tick_` and needed a Grep anyway. On T2, B used more total context (median 425k vs 294k, counting cache reads).
- **The fixed prefix is the real cost.** Each subagent's first call wrote about **65,000 tokens** of system prompt, tool list, `CLAUDE.md` and skill list, with no cache read, even though 12 identical prefixes started at the same moment. The work itself added only about 9–10k tokens. So about 87% of a small lookup's fresh tokens is the prefix. That is direct evidence for pick 3 (context diet), and for doing short lookups in the main session instead of a subagent.
- **What the trial does not cover:** symbols grep can't pin down (overloads, virtual overrides, a name defined in several namespaces), call hierarchies more than one level deep, and post-edit diagnostics that save a build round trip. Those are where clangd should still win. The fix that gives every worktree a compile database stays useful for diagnostics either way.

## Twenty-four candidates, judged against this machine

Tags in the evidence column: **M** = measured by a third party, **C** = vendor or author claim, **D** = Anthropic documentation, **–** = no figure. "Have" means it is already in your setup.

| Tool / lever | What it does | Savings evidence | C++ (templated, MSVC) | Windows | Hooks into Claude Code via | Maturity | Tree / network risk | Verdict |
|---|---|---|---|---|---|---|---|---|
| **clangd LSP tool (eaw-clangd) — have** | Definition, references, call hierarchy, file symbols, diagnostics after each edit | M: −31/−33% tool-output tokens, total cost −3% (Opus) / −14% (Sonnet) ([CircleCI](https://circleci.com/blog/claude-code-lsp/)); one preprint finds it "usually negative" for localization ([arXiv](https://arxiv.org/abs/2608.13568)) | Best available: real compiler front end, but needs a compile DB | Yes | Plugin `lspServers` | Built in | Index cache; no network | **#1: fix and steer** |
| codebase-memory-mcp | Persistent code graph: symbols, BM25 and semantic search, callers, outlines, test impact | C: ~10x fewer tokens per question, quality 0.83 vs 0.92 for grep ([arXiv preprint](https://arxiv.org/html/2603.27277v1)) | Tree-sitter plus built-in template / namespace / `auto` resolution | Yes (zip, unsigned) | MCP | 48.9k★, MIT, commit 2026-11-19 | Index in `~/.cache`; offline; no telemetry | **#2: trial in one lane** |
| CodeGraph | Pre-indexed graph, `codegraph_explore` | M: −55% tool calls, −23% tokens, **+7% cost** ([Guo](https://dev.to/harrisonsec/i-tested-codegraph-on-hono-the-tool-call-savings-reproduce-the-cost-savings-dont-389p)); competitor's 936 runs: 2–12x cheaper than grep ([Rybalchenko](https://dev.to/neko1313_4/how-much-does-context-cost-an-ai-coding-agent-grep-vs-graph-vs-lsp-measured-across-936-runs-33m8)) | Plain tree-sitter | Yes, but memory-exhaustion loop crashes other apps ([#3137](https://github.com/colbymchenry/codegraph/issues/3137)) | MCP | 78.1k★, MIT, 2026-11-19 | `.codegraph/` in each worktree; telemetry is opt-out | Runner-up to #2 |
| Serena | LSP-backed symbol retrieval and symbol edits, plus memories | M: same 936-run study, 4x cheaper than grep on simple tasks ([Rybalchenko](https://dev.to/neko1313_4/how-much-does-context-cost-an-ai-coding-agent-grep-vs-graph-vs-lsp-measured-across-936-runs-33m8)) | clangd; compile DB required at repo root ([guide](https://github.com/oraios/serena/blob/main/docs/03-special-guides/cpp_setup.md)) | Smart App Control blocks it ([#2196](https://github.com/oraios/serena/issues/2196)) | MCP | 31.5k★, GPL-3.0, 2026-11-19 | `.serena/` in tree; open clangd bugs | Skip: duplicates #1 |
| Graphify (reference) | Tree-sitter knowledge graph over code and docs; skill + hooks | C: 82% vs 71% key-fact coverage at ~140K tokens/query, a quality gain rather than a token cut ([BENCHMARKS](https://github.com/Graphify-Labs/graphify/blob/main/BENCHMARKS.md)) | Tree-sitter; angle-bracket includes unresolved ([#5218](https://github.com/Graphify-Labs/graphify/issues/5218)) | C++ breaks when repo and cache are on different drives ([#5145](https://github.com/Graphify-Labs/graphify/issues/5145)), which is exactly E: and C: here | Skill, PreToolUse hook, optional MCP | 150.6k★, Apache-2.0, 2026-11-19 | `graphify-out/` per worktree; strict mode denies Read/Grep | Skip for now |
| code-graph-rag | Graph built with tree-sitter plus libclang | – (benchmark still an open task) | Hybrid libclang with compile DB | Py 3.12+ | CLI / MCP | 5.4k★, MIT, 2026-11-19 | `~/.cgr`; LLM key unverified | Watch |
| CodeGraphContext | Graph DB with optional scip-clang | C: −62% response tokens | scip-clang (Windows unverified) | Kuzu backend only; setup issues ([#691](https://github.com/CodeGraphContext/CodeGraphContext/issues/691)) | MCP | 4.4k★, MIT | 4 GiB buffer pool | Skip |
| GitNexus | Graph engine plus web UI | – | Tree-sitter | DB crashes ([#3706](https://github.com/abhigyanpatwari/GitNexus/issues/3706)) | MCP + hooks | PolyForm **Noncommercial** | Rewrites `CLAUDE.md` / `AGENTS.md` ([README](https://github.com/abhigyanpatwari/GitNexus)) | Skip |
| Claude Context | Vector code search | C: −40% | Chunking only | Path-casing bug ([#233](https://github.com/zilliztech/claude-context/issues/233)) | MCP | 12.7k★, MIT | **Sends code to an embedding API and Milvus/Zilliz** | Skip |
| Probe / ast-grep MCP | Zero-index AST grep with token budget / structural search | C: ast-grep text output −75% | Tree-sitter | Yes | MCP | 740★ / 499★ | None | Optional |
| RTK | Rewrites Bash commands to compact output | C: 60–90% less output, but only **20.2% total cost** in its own pipeline test ([README](https://github.com/rtk-ai/rtk)) | ctest handler hides assertions behind crash backtraces ([#5119](https://github.com/rtk-ai/rtk/issues/5119)); green summary on non-zero exit ([#4421](https://github.com/rtk-ai/rtk/issues/4421)) | Native, **Bash tool only** (PowerShell routing is an open PR [#3301](https://github.com/rtk-ai/rtk/issues/3301)) | PreToolUse hook | 87.2k★, Apache/BUSL | Telemetry off by default | Skip: builds already summarized |
| context-mode | Keeps tool output in a searchable SQLite index | C: 99.4% of bytes kept out of context (bytes, not cost) ([README](https://github.com/mksglu/context-mode)) | n/a | Needs Git Bash and Node ≥ 24.15 | Plugin (hooks + MCP) | 34.3k★, ELv2 | `ctx_execute` runs arbitrary code | Skip |
| squeez / tokf / chop | Bash output compressors | C: 50–95% | No MSVC parser | squeez Git Bash only; tokf has no Windows binary | Hooks | ≤ 237★ | tokf masks exit codes to 0 | Skip |
| In-house `updatedToolOutput` hook | Trim ad-hoc ctest/ninja/git output in **both** shells using `build_summary.py` parsing | D: grepping errors out of a 10k-line log goes "from tens of thousands of tokens to hundreds" ([costs](https://code.claude.com/docs/en/costs)) | Exact (your own parser) | Yes | Hook | Yours | None | Build only if the trial shows ad-hoc output is a sink |
| repomix `--compress` / Aider repo map | Signature-only pack / ranked symbol map | – | Tree-sitter misparses macros and templates ([#293](https://github.com/tree-sitter/tree-sitter-cpp/issues/293)) | Yes | CLI | 29.3k★ / Aider idle since 2026-05 | Output file | Skip: clangd `documentSymbol` does this |
| **Auto memory + roadmap store — have** | MEMORY.md shared across worktrees; git-backed handoff | – | n/a | Yes | Built in | Built in | None | Keep |
| episodic-memory | Semantic search over past transcripts | – | n/a | Unverified | Plugin (MCP + skill) | 506★, MIT, 2026-09 | Haiku summaries via your auth | Optional |
| claude-mem | Auto-captures tool activity through an observer LLM | – (claims ~50–100 tokens per index hit) | n/a | Hooks blocked prompts for ~15 min ([#2996](https://github.com/thedotmack/claude-mem/issues/2996)) | Plugin + Bun daemon | 118.4k★, Apache-2.0 | Daemon; extra LLM calls; cloud upsell | Skip |
| beads | Agent issue tracker on Dolt, primes ~1–2k tokens at start | C | n/a | Dolt console bugs | Hook / CLI | 28.8k★, MIT | Replaces your roadmap store | Skip; copy its priming idea |
| Basic Memory / mem0 / Graphiti / Letta | General memory platforms | C on chat benchmarks only | n/a | Mixed | MCP | Active | LLM / embedding APIs, Docker | Skip |
| **Context diet (built-in)** | `omitClaudeMd`, smaller skill preloads, procedures moved to skills / path rules, `MAX_MCP_OUTPUT_TOKENS` | D: `/usage` shows per-request CLAUDE.md cost; "every line is a recurring token cost" ([skills](https://code.claude.com/docs/en/skills)) | n/a | Yes | Settings / frontmatter | Built in | None | **#3** |
| Tool search (deferred MCP) — have | Only tool names load at start | D (Anthropic-measured): ~77K → 8.7K tokens, −85% ([Anthropic](https://www.anthropic.com/engineering/advanced-tool-use)) | n/a | Yes | Default on | Built in | None | Already active |
| Prompt-cache settings | TTL per bucket, cache-safe habits | D: reads cost 0.05x on Opus 5.5 ([API docs](https://platform.claude.com/docs/en/build-with-claude/prompt-caching)) | n/a | Yes | Settings | Built in | None | Habits, not a tool |
| ccusage / `/usage` / OTel | Measure tokens per session | n/a | n/a | ccusage on Windows unverified | CLI / built in | ccusage 19.5k★ | Local | Use for the trial |

## Pick 1: make clangd see the whole tree, then make agents use it

**Why it wins.** Your biggest uncovered sinks are re-reading big files and grepping across the tree. `tactical_battle.hpp` alone is 3,008 lines. Claude Code's LSP tool already answers the questions those reads are trying to answer: "List symbols in a file", "Find all references", "Trace call hierarchies", plus diagnostics after each edit ([tools reference](https://code.claude.com/docs/en/tools-reference.md)). clangd is the only engine in this survey that understands C++20 templates and MSVC macros, because it reads the real compile commands. Tree-sitter indexes are blind to macros: one preprint measured a tree-sitter graph at **0.58 vs 1.00** quality against grep on macro-heavy C ([arXiv](https://arxiv.org/html/2603.27277v1)).

The evidence is honest but mixed. CircleCI cut tool-output tokens by about a third and found that grep missed references (9 of 11, 249 of 260). LSP still cost more on 2 of 3 Opus tasks ([CircleCI](https://circleci.com/blog/claude-code-lsp/)). A June 2026 preprint found models used LSP on only 0–6% of localization steps unless pushed ([arXiv](https://arxiv.org/abs/2608.13568)). That is why this pick has two parts. Part one is the compile database, which is the bug the spot check exposed. Part two is an explicit instruction to use the tool, which CircleCI's setup relied on.

There is a second benefit the benchmarks do not count. The "Found N new diagnostic issues" line after each edit ([code intelligence](https://code.claude.com/docs/en/plugins/code-intelligence.md)) can catch a compile error before a build round trip, and on this project a round trip costs minutes.

**Setup on Windows (about 15 minutes, all in-repo):**

1. **Confirm the plugin loads in a live session.** Run `/plugin` and check that `eaw-clangd@eaw-local` is enabled. Then ask for a `documentSymbol` on any header. Your memory still lists "plugin load unverified". This session did get the LSP tool, so it loads in at least one worktree.
2. **Give every worktree a compile database without a full build.** Add a configure-only mode to `build_agent.cmd`, for example `ALAMO_CONFIGURE_ONLY=1`, that runs its existing `cmake -S . -B %B% -G Ninja ...` line for `all` modules and stops before Ninja. Do not hand-run cmake (your "never hand-configure build trees" rule). Prefer the clang tree (`build-wt-clang`), because clangd reads clang flags natively, and `clangd_launch.py` already lists that tree first. Call it from `start-group`, or from the existing SessionStart hook when `build-wt-*\compile_commands.json` is missing.
3. **Let the index finish before you trust "no references".** Serena hit exactly this with clangd: references come back empty while the background index is still running ([#2191](https://github.com/oraios/serena/issues/2191)). Claude Code's own docs warn about high memory use while the server indexes ([code intelligence](https://code.claude.com/docs/en/plugins/code-intelligence.md)). With 3 lanes you get 3 clangd indexes, so watch RAM during a build. Check that clangd's on-disk index cache stays out of git.
4. **Steer the agents.** Add one line to `CLAUDE.md` and to `lane.md`: "C++ symbol questions (where defined, who calls, what is in this header) use the LSP tool first (`workspaceSymbol`, `findReferences`, `incomingCalls`, `documentSymbol`); read a file range only after that; Grep for strings, Lua, XML and docs." The single line is enough, so keep it short.

**30-minute trial: tokens per task, LSP off vs on**

Run the trial headless so each run is a fresh session with its own `usage` object. Pin the model with `--model opus` and the effort with `CLAUDE_CODE_EFFORT_LEVEL=high` for both arms. Arm A disables the plugin through a settings file that sets `"enabledPlugins": {"eaw-clangd@eaw-local": false}` (pass it with `--settings`, or toggle it in `/plugin`). Arm B uses the normal settings plus the steering line.

Use two fixed tasks with known answers:

- **T1:** "Find every caller of `TacticalBattle::issue_orders` in engine/ and list file:line." Ground truth comes from Grep.
- **T2:** "List the public member functions of `TacticalBattle` in `engine/apps/sim/include/alamo/sim/tactical_battle.hpp` with a one-line purpose each." This is the big-file re-read case.

If you have 15 more minutes, add **T3**: on a scratch branch, rename one member in a fast-test module and ask "make `build_agent.cmd clang wt sim` pass". This measures whether diagnostics save a build loop.

Order the runs A, B, A, B, and so on, with **one discarded warm-up per arm**. The two arms have different tool sets, so they build separate cache prefixes, and the first run of each pays the cache write ([prompt caching](https://code.claude.com/docs/en/prompt-caching)). Then do **3 scored repeats per task per arm**: 12 runs of about 1–2 minutes each. Collect results with:

```powershell
$r = claude -p $task --model opus --output-format json | ConvertFrom-Json
$u = $r.usage
"{0},{1},{2},{3},{4},{5},{6}" -f $arm,$u.input_tokens,$u.output_tokens,$u.cache_creation_input_tokens,$u.cache_read_input_tokens,$r.num_turns,$r.total_cost_usd
```

Report three numbers per run:

- **Fresh tokens:** input + cache write + output. This is what grows when an agent reads more.
- **Cache reads:** these are cheap at 0.05x on Opus 5.5 ([API docs](https://platform.claude.com/docs/en/build-with-claude/prompt-caching)), but they still fill the context window.
- **Turns.**

Score each answer against the Grep ground truth, because a cheaper answer that misses callers is a loss. Check the session JSONL under `~/.claude/projects/` to confirm that arm B really called `LSP`. Cross-check the totals with `npx ccusage@latest session --json` (it reads the same JSONL files; Windows use is unverified) ([ccusage](https://github.com/ccusage/ccusage)). For per-subagent attribution, set `CLAUDE_CODE_ENABLE_TELEMETRY=1` with `OTEL_METRICS_EXPORTER=console` in user settings and read `claude_code.token.usage` by `query_source` ([monitoring](https://code.claude.com/docs/en/monitoring-usage)). In interactive sessions, the `/usage` Session block gives the same split, but its cache line covers only the main conversation ([costs](https://code.claude.com/docs/en/costs)).

**Decision rule.** Keep the change if arm B's median fresh tokens are at least 15% lower with equal or better recall. Keep it at equal tokens if recall improves. Three repeats are a coarse sample: if the medians land within 10% of each other, run three more before deciding.

## Pick 2: codebase-memory-mcp in one lane, only after pick 1 is measured

clangd stops at C++. A graph tool covers what clangd cannot: Lua and Python files, callers of callers across modules, and a persistent index that survives between sessions.

**codebase-memory-mcp** fits this machine best of the graph tools:

- It is a single native binary with "164 vendored tree-sitter grammars".
- Its C++ layer "handles templates, namespaces, `auto` inference, and method resolution via class hierarchy".
- It stores the index under `~/.cache/codebase-memory-mcp/` instead of in your tree.
- It indexes each linked worktree as its own project.
- It collects no telemetry and needs no API key.

([README](https://github.com/DeusData/codebase-memory-mcp))

Its weak point is evidence. The only figure is the author's own preprint: about **10x fewer tokens** per question, but **lower answer quality (0.83 vs 0.92)** ([arXiv](https://arxiv.org/html/2603.27277v1)). Two risks are specific to this machine. The binary is unsigned, so SmartScreen will warn. And there is an open bug where the session root became `C:\Windows\system32` ([#1183](https://github.com/DeusData/codebase-memory-mcp/issues/1183)).

CodeGraph has better evidence. An independent reproduction got **−55% tool calls and −23% tokens, with cost +7%** ([Guo](https://dev.to/harrisonsec/i-tested-codegraph-on-hono-the-tool-call-savings-reproduce-the-cost-savings-dont-389p)). Three things lose it the pick here:

- It writes `.codegraph/` into every worktree.
- Its telemetry has to be switched off.
- It has an open Windows bug where a watchdog respawn loop under memory exhaustion "crashes unrelated apps" ([#3137](https://github.com/colbymchenry/codegraph/issues/3137)). That is unacceptable next to MSVC builds and 3 lanes.

If codebase-memory-mcp's answers prove wrong on templated code, CodeGraph is the fallback to try.

Graphify, your reference point, is very active. But its C++ declaration/definition matching fails when repo and cache sit on different Windows drives ([#5145](https://github.com/Graphify-Labs/graphify/issues/5145)), and that is this machine's E:/C: layout. Its strict hook also denies Read and Grep ([README](https://github.com/Graphify-Labs/graphify)), which would fight your skills. Revisit it once #5145 closes.

To run the trial, register the server for one lane only and reuse the pick-1 harness with a third arm. Treat any wrong caller list as disqualifying, because a lane that edits on false call graphs costs more than any token saving.

## Pick 3: put the always-loaded context on a diet

Every lane loads `CLAUDE.md` (21.9 KB, about 5.5k tokens, 144 long lines). Lanes also load `lane.md` (18.4 KB) and its preloaded skills. Every non-Explore subagent loads the full CLAUDE.md hierarchy too ([sub-agents](https://code.claude.com/docs/en/sub-agents)). Caching softens this, but only partly:

- Subagents use a **5-minute TTL** and do not read the parent's cache.
- Lanes in different worktrees "build different prefixes and miss each other's cache" ([prompt caching](https://code.claude.com/docs/en/prompt-caching)).

So each lane and each subagent pays a cold write of this prefix at 1.25x.

The built-in levers:

- **`omitClaudeMd: true`** on `re-analyst`, which needs the RE rules, not the landing rules. Re-test `reviewer` before giving it the same, because it checks CLAUDE.md rules.
- **Trim `skills:` preloads.** Each preload is loaded in full at startup.
- **Move procedural sections into skills or path-scoped rules.** Candidates are the long Parallel work and Rule 6 roadmap passages. Those load only on demand ([memory](https://code.claude.com/docs/en/memory)).
- **Set `MAX_MCP_OUTPUT_TOKENS` around 10,000 for the ReVa agent.** Large decompiles then spill to a file that the agent can grep instead of reading in full ([MCP](https://code.claude.com/docs/en/mcp)).

Measure before and after with the `/usage` recommendation rows, which name per-request costs such as "Trim … CLAUDE.md · Nk tokens per request", and with `/skill-doctor`, which reports each skill's context cost ([costs](https://code.claude.com/docs/en/costs), [skills](https://code.claude.com/docs/en/skills)).

Pair the diet with cache-safe habits:

- Don't switch models mid-task. On a warm 100k-token context, switching to Haiku costs more than letting Opus answer ([Anthropic](https://claude.dev/blog/lessons-from-building-claude-code-prompt-caching-is-everything/)).
- Effort changes are cache-safe on Opus 5.5.
- Toggle fast mode only at session start.
- Run `/compact` at natural breaks while the cache is still warm ([prompt caching](https://code.claude.com/docs/en/prompt-caching)).

Two more items are worth adding:

- **A 20-line SessionStart hook** that prints the claimed group's open tasks from `roadmap_store.py`. This copies beads' "~1–2k tokens" priming without adopting beads ([beads](https://github.com/gastownhall/beads/blob/main/docs/integrations/claude-code.md)).
- **ctest's native `--test-output-size-failed` with `--test-output-truncation middle`** for direct ctest runs ([ctest](https://cmake.org/cmake/help/latest/manual/ctest.1.html)). It bounds failure output without RTK's habit of dropping assertions.

## Excluded tools

These were dropped as abandoned (no commit in 12 months), archived, or not native on Windows:

- **isaacphi/mcp-language-server:** last commit 2025-06-03; superseded by the native LSP tool ([repo](https://github.com/isaacphi/mcp-language-server)).
- **jonrad/lsp-mcp and Tritlo/lsp-mcp:** last commits 2025-03 and 2025-07 ([repo](https://github.com/jonrad/lsp-mcp), [repo](https://github.com/Tritlo/lsp-mcp)).
- **eas4ai/code-graph-mcp:** last commit 2025-07-26 ([repo](https://github.com/eas4ai/code-graph-mcp)).
- **nuanced-py:** archived, Python call graphs only ([repo](https://github.com/nuanced-dev/nuanced-py)).
- **wrale/mcp-server-tree-sitter:** archived ([repo](https://github.com/wrale/mcp-server-tree-sitter)).
- **Sourcegraph public snapshot:** archived 2024; its MCP server needs a paid Enterprise plan ([docs](https://sourcegraph.com/docs/api/mcp)).
- **Sourcebot:** Windows only through Docker ([repo](https://github.com/sourcebot-dev/sourcebot)).
- **files-to-prompt:** last commit 2025-02 ([repo](https://github.com/simonw/files-to-prompt)).
- **codebase-digest:** last commit 2024-10 ([repo](https://github.com/kamilstanuch/codebase-digest)).
- **gitingest:** no default-branch commit since 2025-08 ([repo](https://github.com/coderamp-labs/gitingest)).
- **llm-code-highlighter:** last push 2024-03 ([repo](https://github.com/restlessronin/llm-code-highlighter)).
- **cc-sessions:** last commit 2025-10-17 ([repo](https://github.com/GWUDCAP/cc-sessions)).
- **ByteRover / Cipher:** archived; installer is macOS/Linux only ([repo](https://github.com/campfirein/byterover-cli)).
- **tokf:** no Windows release binary ([releases](https://github.com/mpecan/tokf/releases)).
- **squeez:** Git Bash only; PowerShell unsupported ([repo](https://github.com/claudioemmanuel/squeez)).
- **CodeGraphContext's default FalkorDB Lite backend:** Unix only ([repo](https://github.com/CodeGraphContext/CodeGraphContext)).
- **LLMLingua:** no Claude Code integration, and its own samples corrupt identifiers and numbers ([repo](https://github.com/microsoft/LLMLingua)).

Alive but quiet, and not needed here: **cclsp** (last commit 2026-02; also superseded by native LSP), **Kit** (2026-03), **RepoMapper** (2025-12).

## Gaps and uncertainties

- **No C++ benchmark exists for any tool in this survey.** Every savings figure comes from Python, TypeScript or C repos, and most are vendor numbers. The two independent studies of graph tools agree that **tool-call reductions reproduce more reliably than cost reductions**.
- **The LSP evidence is split.** CircleCI's result is positive; the arXiv preprint's is negative, and only its abstract was read.
- **The clangd spot check was a single query** in a worktree with no build tree. It showed the missing-compile-DB failure, not how clangd behaves once fully indexed on MSVC-flavoured flags.
- **codebase-memory-mcp's accuracy on MSVC code is unknown.** Neither its C++ resolver nor any other tool documents handling of `__declspec`, `#pragma` or C++20 concepts.
- **Several pieces are unverified:**
  - whether ccusage reads desktop-app Code-tab sessions on Windows;
  - whether episodic-memory works on Windows;
  - the desktop app's undocumented transcript search and `get_usage` tools, which overlap episodic-memory and ccusage.

## Conclusion

The pattern across all four research tracks is that this project has already built the expensive parts: summarized builds, file-based memory, a git-backed handoff store and an LSP plugin. Its remaining token waste comes from gaps in that plumbing more than from missing products. The highest-leverage fix is therefore a configure step, so that clangd sees the whole tree in every worktree, plus one steering line. Installing a fourth system on top is not.

Graph tools are the one category that could add something clangd cannot. They earn a slot only by passing a recall-scored A/B on this tree, and they should not be adopted on the strength of star counts or vendor percentages. The same harness that measures pick 1 will answer that question in an afternoon.
