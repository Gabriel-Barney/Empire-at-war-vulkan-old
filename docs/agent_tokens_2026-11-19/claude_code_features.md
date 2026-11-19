# Claude Code built-in token levers and measurement tools (as of 2026-11-19)

Scope: Claude Code's own features and settings that cut tokens or speed up work, plus the tools that measure tokens per task. Version numbers are the `v2.1.x` releases the official docs name. All docs pages were fetched on 2026-11-19. They describe the current release, and several features need versions from v2.1.2xx to v2.1.3xx.

## 1. Prompt caching in Claude Code (TTL, invalidation, read/write cost)

### Takeaway
Caching is automatic and works on the prefix. On Opus 5.5 a cache read costs 0.05x the base input price, a 5-minute write costs 1.25x and a 1-hour write costs 2x. On a subscription within plan usage, the main conversation gets a 1-hour TTL and subagents get 5 minutes. A cache miss happens when you switch model, toggle fast mode, change the tool-definition set (when tool search is off), compact, or upgrade. Editing CLAUDE.md mid-session does NOT break the cache. On Opus 5.5, changing effort does not break it either.

### Cited Findings
- Requests are ordered in layers so stable content comes first: system prompt (core instructions and tool definitions), then project context (CLAUDE.md, auto memory, unscoped rules), then the conversation. The match is an exact prefix match: "There is no per-file or per-segment caching." — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)
- API pricing multipliers relative to base input: 5-minute cache write **1.25x**, 1-hour cache write **2x**, cache read **0.1x** by default, **0.05x on Claude Opus 5.5 and Sonnet 5.5**, 0.025x on Fable/Mythos 5.1–5.2. Example: Opus 5 has base input $5/MTok, 5m write $6.25, 1h write $10, read $0.50. — [API docs: prompt caching](https://platform.claude.com/docs/en/build-with-claude/prompt-caching)
- Minimum cacheable prefix is 512 tokens on Opus 5.5 / Sonnet 5.5 / Haiku 5.5, and 4,096 on Opus 4.5/4.6 and Haiku 4.5. If both `cache_creation_input_tokens` and `cache_read_input_tokens` are 0, nothing was cached. — [API docs: prompt caching](https://platform.claude.com/docs/en/build-with-claude/prompt-caching)
- API hierarchy is tools → system → messages, and a change invalidates that level and everything after it. Up to 4 breakpoints are allowed, with a 20-block lookback per breakpoint. — [API docs: prompt caching](https://platform.claude.com/docs/en/build-with-claude/prompt-caching)
- **Default TTL per bucket:**
  - Main conversation on a Claude subscription within plan usage: **1 hour**.
  - Main conversation on usage credits, an API key or a cloud provider: **5 minutes**.
  - "Everything else" (subagents, workflows, teammates, forks, compaction, titles): **5 minutes**, except a few server-controlled helper requests.
  - When a subscription goes over its limit onto usage credits, the main conversation drops to 5m. — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)
- **TTL controls (v2.1.242+):**
  - Main conversation: setting `promptCacheTtl` or env `CLAUDE_CODE_PROMPT_CACHE_TTL`.
  - Everything else: setting `subagentPromptCacheTtl` or env `CLAUDE_CODE_SUBAGENT_PROMPT_CACHE_TTL`.
  - Both take `5m` or `1h`.
  - Per-agent frontmatter `experimental.cacheTtl` (v2.1.248+).
  - `ENABLE_PROMPT_CACHING_1H=1` sets 1 hour for both buckets. `FORCE_PROMPT_CACHING_5M=1` forces 5 minutes and wins over everything else.
  - To check which TTL a write used, run `claude -p "hello" --output-format json` and read `usage.cache_creation.ephemeral_1h_input_tokens` / `ephemeral_5m_input_tokens`. — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)
- **Actions that invalidate the cache:** switching model; changing effort (except with an API key or subscription on Fable 5.1+, **Opus 5.5**, Sonnet 5.5 and Haiku 5.5, where the cache is kept; before v2.1.260 Fable 5.1 broke it); turning fast mode on or off (fast and standard have separate caches); connecting or removing an MCP server, but only when tools load upfront; enabling or disabling a plugin that provides MCP servers; denying a whole tool when tool search is off; compaction; dropping old images; upgrading Claude Code. A skill or command whose frontmatter names a `model` is a model switch for that turn. `opusplan` toggles are model switches. — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)
- **Actions that keep the cache:** editing repo files; **editing CLAUDE.md mid-session** (it is read at session start and held in memory, and reloads only on `/clear`, `/compact` or a restart); changing permission mode; changing output style (applies at the next message since v2.1.251); invoking skills or commands (appended as user messages); `/recap`; `/rewind`; spawning a subagent. A plugin's skills, commands, agents, hooks, monitors and themes never invalidate the cache. — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)
- **With tool search on (the default), MCP changes don't break the cache:** "Claude Code keeps the tool list from the conversation's first request for the whole conversation, so a server connecting or disconnecting mid-session doesn't disturb anything already cached." — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)
- **Compaction cost:** the summary request reuses the parent prefix. With a warm cache, a mid-session `/compact` "costs a fraction of what the context size suggests". On a cold cache (after a break longer than the TTL) it reprocesses the history uncached. Run `/compact` at natural breaks. `/rewind` returns to an already-cached prefix. Idle compaction runs on the 1h TTL; turn it off with `idleCompaction: false` (v2.1.290+). — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)
- **Cache scope** is effectively one machine and one directory. The system prompt embeds the auto-memory paths, and the conversation opens with the cwd, platform and OS. "Two sessions in different directories therefore build different prefixes and miss each other's cache." Sequential sessions share a prefix only when the startup git-status snapshot matches. — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)
- **Subagents and forks:** a subagent's first request does not read the parent cache, because its prefix differs. "A fork, by contrast, inherits the parent's system prompt, tools, and conversation history exactly, so its first request reads the parent's cache." Resumed subagents and workflow fan-outs also reuse a cached prefix. In a fan-out, all agents except the first are held for up to 5 s by default. — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)
- **Design rationale (Anthropic, Thariq Shihipar, 2026-04-30):**
  - The team runs alerts on cache hit rate and "declare[s] SEVs if they're too low", and says "A few percentage points of cache miss rate can dramatically affect cost and latency."
  - Tools are sent as `defer_loading: true` stubs.
  - Plan mode is implemented as tools (EnterPlanMode / ExitPlanMode) so the tool set never changes.
  - Compaction is a cache-safe fork whose summary is "about 20k tokens".
  - Example: 100k tokens into an Opus conversation, switching to Haiku for an easy question is *more* expensive than letting Opus answer, because the cache has to be rebuilt. — [Lessons from building Claude Code: Prompt caching is everything](https://claude.dev/blog/lessons-from-building-claude-code-prompt-caching-is-everything/) (claude.com URL redirects here)

### Inferences
- Up to 3 lane sessions run in separate worktrees, plus a coordinator. They have different cwds, so they **cannot share cache**: each lane pays its own cold-start prefix write. Parallel sessions in the *same* directory could share it.
- On a subscription the main session already gets the 1h TTL. Subagents (re-analyst, reviewer, Explore) get 5m by default. Long Ghidra / build waits inside a subagent may therefore cause cold re-reads. Setting `subagentPromptCacheTtl: 1h` is worth trialling on the API, but costs 2x on writes.
- On Opus 5.5, `/effort` changes are cache-safe, so lowering effort per task is a free lever. Model switches mid-task are not.
- MEMORY.md and auto-memory paths sit in the system prompt / project-context layer. Edits reload only after `/clear` or `/compact`.

### Gaps
- No published Claude Code cache hit-rate figure (the blog explicitly gives none).

## 2. MCP tool search / deferred tool loading and MCP context cost

### Takeaway
Tool search is on by default for supported models. Only tool names and server instructions load at start, and full schemas load on demand. Anthropic measured an 85% cut in tool-definition tokens (~77K to ~8.7K) on a five-server setup. MCP output is capped at 25,000 tokens by default and spills to a file past that limit or past 50,000 characters.

### Cited Findings
- "Only tool names and server instructions load at session start, so adding more MCP servers has minimal impact on your context window." There is no fixed per-server tool cap. — [Claude Code docs: MCP](https://code.claude.com/docs/en/mcp)
- **`ENABLE_TOOL_SEARCH` values:**
  - unset / `true`: deferred.
  - `auto`: tools load upfront while their definitions total under 10% of the context window, and search turns on at 10% or more.
  - `auto:N`: custom percentage.
  - `false`: everything upfront.
  - Supported on Claude Opus 4 / Sonnet 4 / Haiku 4.5 and later. Not on Microsoft Foundry Azure-hosted deployments. — [Claude Code docs: MCP](https://code.claude.com/docs/en/mcp)
- **Per-server `alwaysLoad`:** `true` loads a server's tools upfront. `"alwaysLoad": false` keeps even author-marked tools deferred (v2.1.287+). — [Claude Code docs: MCP](https://code.claude.com/docs/en/mcp)
- **`/context`** labels tools `(deferred)` or `(loaded on-demand)`. The costs page says: "Run `/context` to see what's consuming space." — [Claude Code docs: MCP](https://code.claude.com/docs/en/mcp); [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
- **Description caps:** server instructions and upfront tool descriptions are cut at 4,096 chars. A tool loaded via tool search is cut at 16,384 chars (env `CLAUDE_CODE_MAX_MCP_DESCRIPTION_LENGTH`, v2.1.280+; before v2.1.296 the cap was 2,048). — [Claude Code docs: MCP](https://code.claude.com/docs/en/mcp); [env vars](https://code.claude.com/docs/en/env-vars)
- **MCP output limits:**
  - The transcript viewer warns above **10,000 tokens**.
  - `MAX_MCP_OUTPUT_TOKENS` defaults to **25,000**.
  - Over the limit, a successful text result is saved to a file in the session's `tool-results` dir under `~/.claude/projects/` and replaced by its path.
  - Text results over **50,000 characters** also go to a file, regardless of the token setting.
  - Servers can raise their own limit per tool with `_meta["anthropic/maxResultSizeChars"]`, up to a 500,000-character ceiling.
  - Error results over ~11,000 chars are cut to their first 5,000 + last 5,000 chars. — [Claude Code docs: MCP](https://code.claude.com/docs/en/mcp)
- The costs page advises: "Prefer CLI tools when available" (`gh`, etc.), because they "don't add any per-tool listing"; also disable unused servers via `/mcp`. — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
- **Subagent-scoped MCP:** define a server inline in a subagent's `mcpServers` "to keep an MCP server out of the main conversation entirely and avoid its tool descriptions consuming context there". — [Claude Code docs: sub-agents](https://code.claude.com/docs/en/sub-agents)
- **Anthropic measured numbers (2025-11-24):**
  - Tool-definition sizes: GitHub 35 tools ~26K, Slack 11 tools ~21K, Sentry ~3K, Grafana ~3K, Splunk ~2K, Jira ~17K. Definitions reached 134K tokens internally before optimisation.
  - Tool Search Tool: ~77K → ~8.7K tokens, "an 85% reduction".
  - MCP eval accuracy: Opus 4 49% → 74%, Opus 4.5 79.5% → 88.1%.
  - Programmatic Tool Calling: 43,588 → 27,297 tokens (37%).
  - Tool use examples: accuracy 72% → 90%. — [Anthropic engineering: Advanced tool use](https://www.anthropic.com/engineering/advanced-tool-use)
- **Code execution with MCP (2025-11-04):** presenting MCP servers as code APIs and filtering results in the sandbox cut one example from "150,000 tokens to 2,000 tokens" (98.7%). This is the article's own single example, not a benchmark. Caveat: "infrastructure requirements add operational overhead and security considerations". — [Anthropic engineering: Code execution with MCP](https://www.anthropic.com/engineering/code-execution-with-mcp)

### Inferences
- With tool search on by default, the user's MCP servers (ReVa, browser, docs) mostly cost names plus server instructions in each session. The bigger risk is large MCP *outputs*, such as Ghidra decompiles. Keeping ReVa inline in the `re-analyst` agent's `mcpServers` (it is already only on that agent, per CLAUDE.md) keeps even its names out of other sessions.
- Lowering `MAX_MCP_OUTPUT_TOKENS` (e.g. to 10,000) forces large decompiles to file plus path, so the agent greps the file instead of ingesting it. Measure the effect on task success.

### Gaps
- The docs give no exact per-tool-name token cost for deferred stubs.
- I did not fetch a `/context` output sample.

## 3. Skills progressive disclosure vs CLAUDE.md; CLAUDE.md size guidance

### Takeaway
Only skill descriptions are always in context, each capped at 1,536 chars. A skill's body loads once on invocation and stays. CLAUDE.md is always loaded, and the docs say to keep it under 200 lines and move procedures into skills. `/usage`, `/doctor` and `/skill-doctor` flag oversized always-loaded content.

### Cited Findings
- "skill descriptions are loaded into context so Claude knows what's available, but full skill content only loads when invoked." Rendered SKILL.md "enters the conversation as a single message and stays there across later turns". Re-invoking an unchanged skill adds only an "already loaded" note. — [Claude Code docs: skills](https://code.claude.com/docs/en/skills)
- Combined `description` + `when_to_use` is truncated at **1,536 characters** in the listing. — [Claude Code docs: skills](https://code.claude.com/docs/en/skills)
- `disable-model-invocation: true` removes the description from context entirely (and blocks subagent preload). — [Claude Code docs: skills](https://code.claude.com/docs/en/skills)
- After compaction, the most recent invocation of each skill is re-attached, keeping the first 5,000 tokens of each, within a combined 25,000-token budget. — [Claude Code docs: skills](https://code.claude.com/docs/en/skills)
- Keep SKILL.md under 500 lines; "every line is a recurring token cost." `context: fork` runs a skill in a subagent (`background` defaults to true). — [Claude Code docs: skills](https://code.claude.com/docs/en/skills)
- `/skill-doctor` (v2.1.252+) "reports each skill's context cost and usage, and flags skills that were listed but never invoked". `/doctor` can migrate always-loaded CLAUDE.md guidance into skills and nested CLAUDE.md files. — [Claude Code docs: skills](https://code.claude.com/docs/en/skills)
- "Skills load on-demand only when invoked, so moving specialized instructions into skills keeps your base context smaller. Aim to keep CLAUDE.md under 200 lines by including only essentials." — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
- `/usage` on paid plans shows recommendation rows such as "Trim ~/code/api/CLAUDE.md · 49k tokens per request". Since v2.1.305 the `f` key applies the fixable ones. — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
- Nested CLAUDE.md files and rules with `paths:` frontmatter load on demand, later in the session. — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)
- Subagent descriptions also cost context. A startup warning fires when non-built-in agent descriptions exceed 15,000 tokens. — [Claude Code docs: sub-agents](https://code.claude.com/docs/en/sub-agents)

### Inferences
- The project CLAUDE.md is far over 200 lines' worth of dense prose (long Rules and Parallel work sections). It is the most obvious always-loaded sink and is re-sent (as cache reads) on every request of every lane and subagent. The exception is Explore / Plan, which skip CLAUDE.md.
- With ~20 skills, the listing costs up to ~20 × 1,536 chars. Long descriptions in this repo may be truncated anyway.

### Gaps
- The docs give no total skill-listing budget (the `SLASH_COMMAND_TOOL_CHAR_BUDGET`-style variable was not in the fetched portion).

## 4. Subagents, Explore, forks, background agents, model/effort per agent

### Takeaway
A subagent keeps verbose work out of the main context, but sends its own requests, does not reuse the parent cache (forks do), and gets a 5m TTL. A model can be set per agent or forced for all. The current docs say Explore defaults to the parent model (or Opus), not Haiku. Explore and Plan skip CLAUDE.md, and custom agents can opt out with `omitClaudeMd`.

### Cited Findings
- Use a subagent when a side task would "flood your main conversation with search results, logs, or file contents"; only the summary returns. Subagents "count toward the same usage limits" and send their own requests. — [Claude Code docs: sub-agents](https://code.claude.com/docs/en/sub-agents)
- **Built-ins:**
  - **Explore:** read-only; default model is the parent's, or the Opus alias on subscription / Console / gateway setups. It "Skips CLAUDE.md and the git status snapshot 'to keep research fast and inexpensive'". To run it on Haiku, define your own agent named `Explore` with `model: haiku`.
  - **Plan:** inherits the parent model and skips CLAUDE.md too.
  - **claude-code-guide:** Haiku. — [Claude Code docs: sub-agents](https://code.claude.com/docs/en/sub-agents)
  - **Conflict:** the April 2026 Anthropic blog says "Claude Code's Explore agents, which use Haiku" — [prompt caching blog](https://claude.dev/blog/lessons-from-building-claude-code-prompt-caching-is-everything/). The current docs say parent/Opus. Treat the docs (Nov 2026) as current.
- **Model resolution:** per-invocation `model` → definition `model` → `CLAUDE_CODE_SUBAGENT_MODEL` → parent's model. `CLAUDE_CODE_SUBAGENT_MODEL_FORCE=1` (v2.1.257+) forces it on all agents except forks and inherit-skills. — [Claude Code docs: sub-agents](https://code.claude.com/docs/en/sub-agents)
- **Per-agent `effort`** (`low`…`max`); per-invocation effort (v2.1.292+); `CLAUDE_CODE_EFFORT_LEVEL` overrides both. — [Claude Code docs: sub-agents](https://code.claude.com/docs/en/sub-agents)
- **Startup load of a non-fork subagent:** its own system prompt, the task message, the full CLAUDE.md hierarchy (except Explore/Plan), git status, preloaded `skills:` (full content, up to 32), and a sibling roster. `omitClaudeMd: true` (v2.1.271+) skips CLAUDE.md. Persistent `memory` loads the first 200 lines / 25 KB of MEMORY.md. — [Claude Code docs: sub-agents](https://code.claude.com/docs/en/sub-agents)
- **Other fields:** `maxTurns`; `autoCompactWindow` (min 100,000, v2.1.296+); `tools` / `disallowedTools` (narrows the tool descriptions loaded); concurrency `CLAUDE_CODE_MAX_CONCURRENT_SUBAGENTS` default 20; nesting depth default 3. — [Claude Code docs: sub-agents](https://code.claude.com/docs/en/sub-agents)
- **Forks** inherit the parent conversation and tool pool. Fork mode is on by default in interactive sessions (`CLAUDE_CODE_FORK_SUBAGENT=0` turns it off). A fork's first request reads the parent's cache. — [Claude Code docs: sub-agents](https://code.claude.com/docs/en/sub-agents); [prompt caching](https://code.claude.com/docs/en/prompt-caching)
- `/btw` answers a side question with no tools and adds nothing to history. — [Claude Code docs: sub-agents](https://code.claude.com/docs/en/sub-agents)
- **Agent teams** use "approximately 7x more tokens than standard sessions when teammates run in plan mode". The docs recommend Sonnet teammates and small teams. — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)

### Inferences
- The `lane`, `reviewer` and `integrator` agents preload skills via `skills:`, and each preload is full content at startup. Trimming preloads or adding `omitClaudeMd: true` for narrow agents (reviewer, re-analyst) is a measurable lever.
- A fork is cheaper than a fresh general-purpose agent for work that needs the parent's context, because it hits the warm cache. A fresh agent is cheaper when the parent context is huge and irrelevant.

### Gaps
- The docs give no published token comparison of subagent vs inline work.

## 5. Hooks and settings that cut tokens (output caps, compaction, effort, thinking)

### Takeaway
PreToolUse `updatedInput` can rewrite Bash commands (e.g. append `| tail`), and PostToolUse `updatedToolOutput` can replace or trim the output of built-in and MCP tools. Bash output inline is capped at ~30k chars by default, with overflow to a file plus a 2k preview. Hook-injected text is capped at 10k chars. The remaining levers are effort, auto-compact thresholds and `/compact` instructions.

### Cited Findings
- The official cost page gives an example PreToolUse hook that rewrites test commands to `... 2>&1 | tail -n 100`. A hook that greps `ERROR` out of a 10,000-line log reduces context "from tens of thousands of tokens to hundreds". — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
- **`updatedInput`** (PreToolUse, in `hookSpecificOutput`) "Replaces the entire input object". Permission rules are evaluated against the rewritten input. In auto mode, a changed input with no `permissionDecision` can be denied. — [Claude Code docs: hooks](https://code.claude.com/docs/en/hooks)
- **`updatedToolOutput`** (PostToolUse) "Replaces the tool's output with the provided value before it is sent to Claude", for built-in and MCP tools. For built-in tools the value must match the output schema (Bash: `stdout`, `stderr`, `interrupted`, `isImage`), or it is ignored. `updatedMCPToolOutput` is MCP-only. OTel still records the original output. `PostToolUseFailure` can't rewrite. — [Claude Code docs: hooks](https://code.claude.com/docs/en/hooks)
- **Hook caps:** `additionalContext`, `systemMessage` and plain stdout are each capped at **10,000 characters**. Above that, output is saved to a file and Claude gets the path plus a 2,000-char preview. PreCompact stdout becomes instructions for the compaction summary. — [Claude Code docs: hooks](https://code.claude.com/docs/en/hooks)
- **Bash output:**
  - A valid result comes back inline up to ~30,000 chars. Beyond that: a file path plus a preview of the first 2,000 chars.
  - A failure is inline up to ~10,000 chars as a head+tail excerpt.
  - `BASH_MAX_OUTPUT_LENGTH` sets the read-back window (default 30,000, max 150,000) but does not raise the inline ceiling.
  - Setting `bashOutputMaxChars` (up to 128,000, v2.1.261+) sizes both the inline ceiling and the read-back window. — [Claude Code docs: tools reference](https://code.claude.com/docs/en/tools-reference); [env vars](https://code.claude.com/docs/en/env-vars)
- **Size and compaction variables:**
  - `CLAUDE_CODE_FILE_READ_MAX_OUTPUT_TOKENS` default 25,000.
  - `CLAUDE_CODE_MAX_OUTPUT_TOKENS` varies by model.
  - `CLAUDE_AUTOCOMPACT_PCT_OVERRIDE` (1–100, can only lower the threshold).
  - `CLAUDE_CODE_AUTO_COMPACT_WINDOW` (min 100,000).
  - `CLAUDE_CODE_IDLE_COMPACT_MIN_TOKENS` default 200,000 (v2.1.290+).
  - `CLAUDE_CODE_DISABLE_1M_CONTEXT`.
  - `CLAUDE_CODE_EFFORT_LEVEL` (`low|medium|high|xhigh|max`).
  - `MAX_THINKING_TOKENS` (fixed-budget models only).
  - `CLAUDE_CODE_DISABLE_FILE_MENTIONS` (v2.1.299+). — [Claude Code docs: env vars](https://code.claude.com/docs/en/env-vars)
- **Thinking and effort:** thinking tokens are billed as output, and "the default budget can be tens of thousands of tokens per request". Thinking can't be turned off on Opus 5.5 / Sonnet 5.5 / Haiku 5.5 / Fable, so use `/effort` instead. — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
- **`/compact` with focus text** (e.g. `/compact Focus on code samples and API usage`) and a `# Compact instructions` section in CLAUDE.md steer the summary. `/clear` "costs nothing". `/rename` + `/resume` let you return to a session. — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
- **Background token sinks:** session recap, idle compaction, prompt suggestions (mostly cache reads), `/loop` scheduled tasks, cross-session messages (`crossSessionInbound: hold`), and goal check-ins (`CLAUDE_CODE_GOAL_CHECKIN_MINUTES=0`). Each sends the full context. — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
- **Fast mode** has a separate cache. The first toggle re-reads the whole conversation uncached at fast-mode rates, so turn it on at session start. — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)
- **Output style** changes are cache-safe. — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)

### Inferences
- For the user's build scripts, a PreToolUse hook that rewrites `build_*.cmd` calls to print only `tools\lint\build_summary.py` output is the most direct lever. So is a PostToolUse `updatedToolOutput` that trims Bash stdout. The existing PostToolUse lint hook's own `additionalContext` also counts toward context (10k cap).
- Default Bash inline is ~30k chars (~7–8k tokens) per big log. Lowering `bashOutputMaxChars` forces file-plus-preview for long logs.

### Gaps
- No version number was found for when `updatedToolOutput` was introduced.
- No independent measurements of hook-based trimming.

## 6. LSP tool / code-intelligence plugins vs grep

### Takeaway
The LSP tool (active once a code-intelligence plugin such as `clangd-lsp` is installed) offers definition, references, hover, symbols, implementations, call hierarchy and post-edit diagnostics. Anthropic presents it as cutting file reads. Independent measurements are mixed: CircleCI saw −31 to −33% tool-output tokens on a reference task, while a June 2026 arXiv study found LSP often *costs* tokens for localization.

### Cited Findings
- **LSP operations:** jump to definition, find references, type info at a position, file symbols, workspace symbol search, implementations, call hierarchies. Type errors are reported automatically after each edit. The tool is read-only, inactive in cloud sessions, and the binary must be installed and on PATH. — [Claude Code docs: tools reference](https://code.claude.com/docs/en/tools-reference); [code intelligence plugins](https://code.claude.com/docs/en/plugins/code-intelligence)
- Official C/C++ plugin: `clangd-lsp` (binary `clangd`). Check PATH in PowerShell with `Get-Command`. After edits, a diagnostics line reads `Found N new diagnostic issues in M files`. — [Claude Code docs: code intelligence](https://code.claude.com/docs/en/plugins/code-intelligence)
- **Vendor claim:** "A single 'go to definition' call replaces what might otherwise be a grep followed by reading multiple candidate files." — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
- **CircleCI (2026-06-24)**, vuejs/core (~149K LOC TS), find-all-references on 3 symbols, Opus 4.8 and Sonnet 4.6, with CLAUDE.md telling Claude to prefer LSP:
  - Opus totals: cost $1.26 → $1.22 (−3%), tool-output tokens 18,673 → 12,916 (−31%), time +2%.
  - Sonnet totals: $0.77 → $0.66 (−14%), tokens 12,597 → 8,480 (−33%), time −34%.
  - LSP cost more on 2 of 3 Opus tasks.
  - Grep on Sonnet missed references (9/11 and 249/260). The number of runs is not stated. — [CircleCI blog](https://circleci.com/blog/claude-code-lsp/)
- **arXiv 2608.13568** (Pengcheng Xu, 2026-06-29), Opus 4.8 / Sonnet 4.6 / Haiku 4.5, Python and TS, tokens-to-success:
  - Localization: LSP cost "+6% to +118%", and the models used it only 0–6% of the time.
  - Reference completeness: LSP buys precision, and saves tokens only for Haiku.
  - Renames: grep was better.
  - The conclusion is "conditional and usually negative". This is a single-author preprint and I read only the abstract. — [arXiv](https://arxiv.org/abs/2608.13568)
- A DEV post by the agent-lsp author claims 5–34x fewer input bytes. It is self-interested and counts input only. — [DEV Community](https://dev.to/daynablackwell/we-measured-it-lsp-saves-ai-agents-5-34x-tokens-vs-grep-427) (not fetched; via search)

### Inferences
- For a large C++ tree, LSP is most likely to help on find-references of heavily overloaded names. The post-edit diagnostics may save build iterations, which is a bigger saving than the navigation itself. It needs a per-task trial, and an instruction in CLAUDE.md to prefer the tool.

### Gaps
- No C++ / clangd-specific measurement found.

## 7. Measurement: /usage, /context, OTel, statusline, ccusage and similar

### Takeaway
For tokens per task:
- In-app, `/usage` (per session: input, output, cache read, cache write per model, plus a prompt-cache hit line since v2.1.251) and `claude -p --output-format json`.
- For time series, OTel (`claude_code.token.usage` with `type`=input/output/cacheRead/cacheCreation, plus per-request `claude_code.api_request` events carrying `query_source`, agent and skill names).
- Offline, ccusage reads the JSONL transcripts under `~/.claude/projects`.

### Cited Findings
- **`/usage` Session block** (the former `/cost` content now lives here): total cost, API and wall duration, and "Usage by model: … 1.2k input, 5.3k output, 940.0k cache read, 50.0k cache write". It resets on `/clear` since v2.1.211.
  - `Prompt cache (main)` line (v2.1.251+): request count, % of input from cache, misses, expected rebuilds, warm/cold plus TTL. Likely cause of the last miss since v2.1.260. It covers the main conversation only, not subagents. — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
  - The plan breakdown in `/usage` attributes recent usage to skills, subagents, plugins and MCP servers (accurate since v2.1.222), and lists the heaviest loops. It toggles between 24 h and 7 d and is computed from local session history. — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
- **Status line** scripts can read `current_usage` (`cache_creation_input_tokens`, `cache_read_input_tokens`) and a `prompt_cache` object. — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)
- **The spinner token count** shows live output tokens for the current turn only. — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
- **`/insights`** writes an HTML report on work patterns to `~/.claude/usage-data/report.html` (it covers behaviour, not tokens). — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
- **OpenTelemetry:**
  - Enable with `CLAUDE_CODE_ENABLE_TELEMETRY=1`.
  - `OTEL_METRICS_EXPORTER=otlp|prometheus|console|none`; `OTEL_LOGS_EXPORTER=otlp|console|none`; `OTEL_METRIC_EXPORT_INTERVAL` default 60000 ms; `OTEL_LOGS_EXPORT_INTERVAL` 5000 ms.
  - Metric `claude_code.token.usage` with attribute `type` = `input` / `output` / `cacheRead` / `cacheCreation`, plus `model`, `query_source` (`main` / `subagent` / `auxiliary` …), `effort`, `agent.name`, `skill.name`, `plugin.name`, `mcp_server.name`, `mcp_tool.name`, and `session.id` by default. `claude_code.cost.usage` takes the same attributes.
  - Event `claude_code.api_request` fields: `input_tokens`, `output_tokens`, `cache_read_tokens`, `cache_creation_tokens`, `cache_creation_5m_tokens` / `_1h_tokens` (v2.1.300+), `cost_usd`, `duration_ms`, `query_source`, `agent.name`, `skill.name`.
  - The `console` exporter prints locally. Windows notes: `otelHeadersHelper` runs through the shell; `host.id` comes from MachineGuid when `OTEL_RESOURCE_INCLUDE_HOST=1`.
  - The env var is ignored in project/local settings, so set it in user settings or the shell. — [Claude Code docs: monitoring usage](https://code.claude.com/docs/en/monitoring-usage); [env vars](https://code.claude.com/docs/en/env-vars)
- **Headless per-run usage:** `claude -p "…" --output-format json` returns a `usage` object including `cache_creation.ephemeral_1h_input_tokens` and `ephemeral_5m_input_tokens`. — [Claude Code docs: prompt caching](https://code.claude.com/docs/en/prompt-caching)
- **Org-level reporting:** Console Cost page, Claude Code Analytics API, Enterprise Analytics API. The average enterprise cost is "around $13 per developer per active day and $150-250 per developer per month", and "below $30 per active day for 90% of users". — [Claude Code docs: costs](https://code.claude.com/docs/en/costs)
- **ccusage** (`ccusage/ccusage`, formerly ryoppippi/ccusage):
  - 19,520 stars, license NOASSERTION per the GitHub API, last commit 2026-11-19, latest release v20.0.51 (2026-11-14).
  - Reports: daily, weekly, monthly, session, `blocks` (5-hour windows), `statusline`.
  - Tracks cache creation and cache read separately. `--json`, `--offline`, `--breakdown` (per model), `--instances` / `--project` (per project).
  - Reads `~/.config/claude/projects/` and `~/.claude/projects/`, overridable with `CLAUDE_CONFIG_DIR` (comma-separated list allowed).
  - Also supports Codex, OpenCode, Gemini CLI and others. Run with `npx ccusage@latest` or `bunx ccusage`.
  - The README does not mention Windows explicitly. — [GitHub ccusage](https://github.com/ccusage/ccusage); `gh api repos/ryoppippi/ccusage` (redirects to ccusage/ccusage); [ccusage Claude guide](https://github.com/ccusage/ccusage/blob/main/docs/guide/claude/index.md)
- **Claude-Code-Usage-Monitor** (Maciek-roboblog): 8,810 stars, MIT, real-time monitor "with predictions and warnings". Last commit 2026-06-27 (v4.0.0), so less active. — `gh api repos/Maciek-roboblog/Claude-Code-Usage-Monitor`; [GitHub](https://github.com/Maciek-roboblog/Claude-Code-Usage-Monitor)
- Large tool results that spill to disk land in the session's `tool-results` dir under `~/.claude/projects/`, alongside the JSONL transcripts that ccusage reads. — [Claude Code docs: MCP](https://code.claude.com/docs/en/mcp)

### Inferences
- **Recommended per-task measurement for the 30-minute trial:**
  1. Run each task as a fresh session (or after `/clear`), so the `/usage` Session block equals that task. Record the input / output / cache read / cache write per model, plus the `Prompt cache (main)` hit %. Subagent tokens are not in that line, so also use ccusage `session --json` or OTel `query_source=subagent`.
  2. For repeatable A/B runs, use `claude -p "<task>" --output-format json` and read `usage`. The fork / subagent split is not in that object.
  3. Use `ccusage session --json` afterwards to cross-check per-session totals from the JSONL.
  4. Set `CLAUDE_CODE_ENABLE_TELEMETRY=1` with `OTEL_METRICS_EXPORTER=console` (or a local OTLP collector) to get per-request `api_request` events tagged by agent and skill.
- **Windows:** ccusage runs under Node / Bun. On Windows `~` resolves to `C:\Users\<user>`. Unverified on this machine, so test `npx ccusage@latest session` first.
- **The desktop app** exposes a `get_usage` session-management tool in this environment (seen in this session's deferred tool list). It is undocumented in the fetched pages, so verify before relying on it.

### Gaps
- I did not fetch the `/context` docs page, so the exact breakdown categories `/context` prints are unverified here.
- ccusage's Windows support and its handling of desktop-app ("Code tab") sessions are unverified.
- The license for ccusage shows as NOASSERTION in the API. Check the LICENSE file before recommending it for org use.
