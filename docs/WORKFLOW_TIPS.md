# Workflow tips: Claude Code features that speed up lanes

Adopted 2026-11-13 (Gabriel) from a review of Claude Code features against this project. Features the project already uses
(`CLAUDE.md`, skills, subagents, `/code-review` in `finish-group`, the clangd plugin) are not repeated here. Agents follow the
short version in `.claude/agents/lane.md` ("Speed habits"); this page is the reference for Gabriel and for new sessions.

| Feature | Use here | Where it is wired |
|---|---|---|
| `/loop` | Unattended "build, read summary, fix, rebuild" until green: `/loop /green-fast-tier <modules>`. Self-paced (no interval): a build takes minutes. Never for CI or PR polling (the app's Auto-fix does that). | `.claude/skills/green-fast-tier/` |
| `/goal` | Gives a lane a checkable stop condition: the group's `exit` test plus a clean `roadmap_store.py lint`. Clear it once the exit test passed. | `start-group` step 6 |
| `/context`, `/compact` | Docs and logs are large. Check `/context` at lane start and between tasks; compact above half full, keeping group id, claim, exit test, open items and the last build summary. | `start-group` step 6, `finish-group` step 8, `lane.md` |
| `/btw` | A side question (what does this flag do, where is X) without adding to the lane's context or interrupting it. | `lane.md` |
| Shell mode (`!`) | Fixed commands with no model round trip: `! python roadmap-store/roadmap_store.py claims`, `! python -I tools/lint/test_slot.py --status`, `! git status`. | this page |
| `/teleport`, Remote Control | Watch or steer a running lane from a phone or another machine while a long build or gate runs; at most 3 lane sessions run at once (2026-11-13). `/teleport` pulls a cloud session into the local checkout. Remote Control is a per-session setting in the app. | this page |

## Unattended runs: fewer approvals (Gabriel, 2026-11-15)

Goal: a `run-group` run goes from claim to landing with Gabriel answering only real decisions. Three layers decide whether a
tool call asks; each has its own fix.

| Layer | What it does | Setting |
|---|---|---|
| Permission mode | `default` asks for every command no rule covers; `auto` lets a classifier approve routine work and asks only for risky actions | New sessions start in `default` unless `permissions.defaultMode` says otherwise. One-time setup: `"defaultMode": "auto"` under `permissions` in `~/.claude/settings.json` (every project) or this repo's `.claude/settings.json`, or pick Auto in the app's mode menu per session. Sessions started with `start_session` inherit the starter's mode, so a coordinator in `auto` gives its lanes `auto` |
| Allow / ask / deny rules | `allow` runs without a prompt or a classifier check in every mode; `ask` always prompts; `deny` never runs | `.claude/settings.json`. A rule matches only the spelling written there (shell, slashes, `&`, `cmd /c` vs `cmd //c`), so agents use the spellings below. Edits to this file are Gabriel's (the auto-mode classifier blocks agents from widening their own permissions) |
| App actions | Archiving sessions, raising a session's permission mode, computer-use app access, UAC prompts | Not settable. They come as a card or OS prompt every time; agents batch them (one archive ask at the end of a run, computer-use access once per session) |

Spellings the allowlist covers (Bash tool / PowerShell tool):
- Build: `cmd //c build_agent.cmd ...` / `& .\build_agent.cmd --% msvc wt "<modules>"` (same for `build_module`, `build_msvc`,
  `build_clang`, `build_asan`, `build_gate`, `build_launcher`).
- Project tools: `python -I tools/<dir>/<script>.py ...` / the same with `tools\`; `.cmd` helpers as `cmd //c tools\<name>.cmd ...`
  / `& .\tools\<name>.cmd ...` (`regen_goldens`, `pr_check`, `clean_worktrees`, `tc`, probe-kit scripts).
- Roadmap: `python roadmap-store/roadmap_store.py <subcommand> ...` (every subcommand).
- Git and GitHub, read and local steps: `status`, `log`, `diff`, `show`, `fetch`, `add`, `commit`, `merge origin/dev`, `rev-parse`,
  `ls-files`, `blame`, `checkout -b`; `gh pr view|list|checks|diff`, `gh run list|view`. Landing goes through `python -I tools/gate/land_queue.py submit`; a direct push to `dev` is denied.
- Test executables in the checkout's own `build-*` trees (`./build-wt-msvc/...` / `& .\build-wt-msvc\...`).

Shapes that miss every rule, and what to use instead:
- A leading `cd <dir>;` or `cd <dir> &&`: the session already runs in its worktree; give paths relative to it.
- A pipe or chain whose every part is not allowed (`... | Select-Object`, `a && b` with an unlisted `b`): one command per call,
  or let the project script do the filtering (`build_summary.py` instead of grepping a log).
- `taskkill`, `Stop-Process`, `rm -rf`, `git push --delete`, `git branch -D|-f`, bare `git stash`: `stop_tree_procs.py`,
  `clean_worktrees.cmd --apply --only <name>`, `delete_merged_branch.py`, a WIP commit (`docs/BUILD.md`, "Pre-approved commands").
- Inline `python - <<EOF` scripts and writes outside the worktree and scratchpad: a project script, or a script file in the
  scratchpad; these still go to the classifier in `auto` mode and ask in `default` mode.

Still asks on purpose (`ask` / `deny` in `.claude/settings.json`): `tc.cmd frida*` / `TTD*` and `tools\re\ttd_record.cmd`
(attach to or record processes; UAC), `tc.cmd cmake*` / `ninja*` / `cargo*` (builds go through the build scripts), pushes to `main` and `gh pr merge` (`main` is Gabriel's release), force pushes and bare
`git stash` (never allowed).

A prompt that keeps coming back: name it in the lane's friction line; `/retro` (or the built-in `fewer-permission-prompts` skill,
which mines transcripts for read-only commands) turns it into a rule for Gabriel to approve.

## Rules for the loop skill

- It never regenerates goldens. A run that fails only on golden drift stops and asks (`regen-goldens` skill is the way to change them).
- It changes the code between runs: an identical failing rerun is refused (exit 3). `ALAMO_RERUN=1` only for a flaky test or a changed
  environment, and the report says so.
- It has an iteration cap (default 6) and stops when the same test fails three iterations running.
- Findings outside the group go to the backlog, not into the loop's fixes.

## Not adopted

`/voice` (no speed gain here), third-party workflow plugins (2026-11-13: they repeat the lane loop), `/init` (`CLAUDE.md` exists).
