---
name: run-group
description: The default workflow for a request to complete roadmap work - "complete all tasks under AUD1", "do sty1", "finish skai", "work the AUDIO box", "clear stage 6", "pick up the next ready groups". This session becomes the coordinator - it resolves the groups, checks prerequisites, claims and the tier focus, splits the work into up to three parallel lanes, asks every approval and open decision up front so Gabriel can walk away, starts each lane as its own session (auto mode) nested under this one with its own worktree, serves what lanes cannot do alone (the Ghidra turn, retail probe runs, questions for Gabriel, waits between lanes), and reports; each lane follows the full lane loop with every skill and tool.
argument-hint: <group id | box id | stage | "next"> [more ids]
---

# Run a group: coordinator and lanes (the default workflow)

A request to complete roadmap work goes through this skill, whatever its wording. **This session is the coordinator**: it plans,
starts lanes, serves them and reports; it does not implement tasks itself. **Each lane is its own session** (Gabriel,
2026-11-15), started by the coordinator with `start_session`, so it shows nested under the coordinator's session, and **with its
own worktree** made by the app (`use_worktree: true`). It works one group (or one share of a group's tasks) following
`.claude/agents/lane.md` step by step with every skill and tool (`docs/TOOLS.md`). Lanes run in parallel.

Why sessions and not background `lane` agents: a subagent runs inside the coordinator's session, which is bound to the
coordinator's worktree, so its writes into another worktree hit the worktree write block; and a subagent cannot start agents.
A lane session is bound to its own worktree and has the `Agent` tool, so it runs `re-analyst`, `/code-review`, `spec-review` and
`reviewer` itself. What stays with the coordinator: the up-front round (step 3a), the Ghidra turn (one ReVa session at a time
across lanes), retail probe runs (the computer-use grant is per session), questions for Gabriel, and holding a lane that waits
for another. ("Coordinator protocol" in `lane.md` is the lane side.)

## 1. Resolve what was asked

`python roadmap-store/roadmap_store.py sync`, then match each id case-insensitively:
- **A group** (`roadmap-store/data/items/<id>.json`, e.g. `sty1`).
- **A box** (`nodes/<ID>.json`, e.g. `AUDIO`): its groups that are not `done`.
- **A stage** or **"next"**: `roadmap_store.py batch 3` (ready groups whose `touches` do not overlap).

For each group show: title, open tasks (tier, `g`, the modules each touches), `exit` test, `after` and whether it is done,
`claimed_by`.

## 2. Check before starting

- **`after` not done**: the group waits behind its prerequisite (same lane queue), or, if the prerequisite is not in this run,
  it is left out and named in the report.
- **Claimed by another session** (`roadmap_store.py claims`): left out and named.
- **Current focus** (CLAUDE.md rule 6): tier-3 tasks are skipped. Crashes, hangs and save corruption are always fixed.
- **No `exit` test**: write one now (`update items/<id> {"exit": "..."}`).
- **Gabriel-only tasks** (`g`) stay open; the rest are worked; they are listed in the report.
- **Vague tasks**: sharpen them before lanes start (`to-tickets`; `grill-with-docs` while Gabriel is here to answer).

## 3. Plan the lanes (at most three at once)

- **Several groups**: one lane per group. Groups linked by `after`, or touching the same modules, go into the **same lane queue**
  in order; independent groups go in separate lanes.
- **One group**: split its tasks over two or three lanes **only** when they fall into independent shares that touch different
  files and need nothing from each other (a share per module is typical). Otherwise one lane takes the whole group. Shares of one
  group land separately; the coordinator closes the group (step 6).
- Never two lanes on the same files, goldens included (CLAUDE.md "Parallel work"); when two lanes would both move the same goldens,
  queue them in one lane.

Show the plan (lanes, queues, shares, what waits on what) in a few lines, then hold the up-front round.

## 3a. Up-front round: every approval now, then Gabriel walks away

Gabriel points the coordinator at work, answers once, and leaves (Gabriel, 2026-11-15). So everything that would stop a lane
later is asked **before the first lane starts**, in as few cards as possible; done when each item below is settled or skipped:
1. **Permission mode.** `get_session` on this session: not `auto` → `set_session_permission_mode` `self` `auto` (one card).
   Lanes then start in `auto` (step 4); in `default` every unlisted command of every lane waits for a click.
2. **Retail probes.** Any task that needs a probe-kit run: `request_access` for `StarWarsG.exe` now. The grant belongs to this
   session, so lanes hand probe runs to the coordinator (`REQUEST probe`, step 5) instead of asking for their own.
3. **Decisions.** Read every task in the run and collect what the docs do not settle: `g` tasks Gabriel can answer now, vague
   tasks, a third-party dependency, a tier-1 rule with two readings, UAC needs (TTD recording, a toolchain install). Ask them in
   one `AskUserQuestion` round (up to 4 per card, recommended answer first) and write each answer into the brief of the lane that
   needs it (and into `docs/DECISIONS.md` / `docs/PHASE4.md` when it is a decision). Unanswered ones become held questions.
4. **Say what is left.** One line: "you can leave now; I stop only for <held questions, `g` tasks, anything that needs a click>".

After the round, a run never waits on Gabriel for something it can decide: a choice the rules or docs settle (tier focus, S3 to
the backlog, golden drift to `regen-goldens`, a reversible default) is taken and listed in the report as `default taken:`; a task
that truly needs him stays `todo`, its question goes on the held list, and its lane goes on with its other tasks. Command
spellings that run without a prompt: `docs/WORKFLOW_TIPS.md`, "Unattended runs".

## 4. Start the lanes

For each lane:
1. **Name**: the group id, plus `-a`, `-b` for shares (`sty1-a`).
2. **Claim**: `roadmap_store.py claim <group> --by <lane-name>` (shares of one group: claim once, by the coordinator's name).
3. **Start the lane session** (`start_session`): `initiation: "user_asked"` (the request for the group work is the ask),
   `context: "fresh"`, `use_worktree: true`, `permission_mode: "auto"`, `name` and `branch_name` the lane name,
   `wake_when_done: true`, and the up-front answers that concern it in the `prompt`. Do not create the
   worktree with `tools\new_worktree.cmd` first: the app makes it, and the session is then bound to it. The `prompt` names: "you
   are lane <name>, started by the run-group coordinator; read and follow `.claude/agents/lane.md` including its Coordinator
   protocol" (sessions do not load agent files), the group and the task ids of its share, the exit test, the modules it owns and
   the ones it must not touch, the lane queue after it. `background`: the run's plan in a few lines (the other lanes and what they
   own). Right before the call, a bold title line and one sentence; several lanes at once: `mark_chapter` first.
4. **Model and effort from the group's `preset`** (Gabriel, 2026-11-15): the preset picks the model, and effort follows the preset
   but never above the lane's own `high` (`lane.md` frontmatter: Opus / high; `xhigh` costs too much on long lane runs). So:

   | Preset | Lane runs on | `start_session` fields |
   |---|---|---|
   | `xhigh`, `high`, `fable`, none | Opus, high | `effort: "high"` (model inherited) |
   | `sonnet` | Sonnet, high | `model: "claude-sonnet-5-5"`, `effort: "high"` |
   | `tools` | Sonnet, medium | `model: "claude-sonnet-5-5"`, `effort: "medium"` |
   | `gab` | Opus, medium; Gabriel's parts come back as `REQUEST gabriel` | `effort: "medium"` |
   | `re2ui` | RE share on Opus, high; UI share as its own lane on Sonnet, high (one Opus lane when it cannot be split) | UI lane: `model: "claude-sonnet-5-5"`, `effort: "high"` |

   A session's model and effort can only be the coordinator's or lower, so the coordinator runs on Opus at `high` or above. A
   queued group whose preset maps to a different row gets a fresh lane session once the group before it has landed (or
   `set_session_model` / `set_session_effort` on the idle lane).

Then wait for lanes to report; don't poll: when a lane ends a turn the app continues the coordinator with a note quoting the start
of the lane's last message. Up to three lanes run at once; when one finishes its queue and work remains, start the next lane.

## 5. Serve the lanes

A lane ends a turn with a **request** (format in `lane.md`; the block comes first in its message, since the note shows only its
start) when it needs something only the coordinator can give. Serve it, then resume that lane with `SendMessage` to its
`local_…` session id carrying the result:
- **`ghidra`**: the lane wants to start `re-analyst` (ReVa). One at a time across lanes (Ghidra lock): answer "go" to one lane,
  queue the others, and give the next lane its "go" once the previous one reports `ghidra done`. REA needs no turn.
- **`probe`**: the lane has a probe kit ready (`tools/probe_mod/<kit>/`, committed on its branch). Run it with this session's
  computer-use grant from the up-front round (one retail game at a time, like the Ghidra turn), then send the lane the result
  files' paths. No grant (Gabriel declined or is away): the probe task stays `todo` and goes on the held list.
- **A question for Gabriel** (a decision, a `g` task, a UAC prompt, a look at `alamo_game.exe`): settled up front → answer from
  the round; a default the rules give → send it and list it under `default taken:`; otherwise ask him, and while he is away put it
  on the held list and tell the lane to continue with what does not depend on it. Several held questions go out as one card.
- **A blocker between lanes** (one lane needs another's work on `dev` first): hold the waiting lane until the other has landed,
  then resume it (it merges `origin/dev` on landing anyway).

Keep each lane's results in your own context short: the request, the answer, the commit.

## 6. Close and report

When a lane reports it landed: check its `dev` commit exists (`git log origin/dev`), that its tasks are `done` on the roadmap and the
claim released. For a group split into shares, once every share has landed: run the exit test once more on `dev` and close the
group (`finish-group` step 3: group and box done, or `close-group`). Remove each landed lane's worktree from the coordinator's
checkout once the lane is idle (`tools\clean_worktrees.cmd --apply --only <worktree name>`, which unlinks the junctions first),
then offer Gabriel to archive the lane sessions, all in one ask at the end of the run (archive only on his yes).

Report per group: tasks closed and open (why), the exit test result, the `dev` commits, goldens changed, skips, every
`default taken:` line, the held questions, anything Gabriel must do, and the lanes' friction lines for `retro` (a prompt that
kept coming is a missing allow rule: name its exact command).

## Running without lanes

For one small group (a couple of tasks in one module), or when Gabriel asks for it to be done here, the coordinator may run the
lane loop itself in this session (it keeps the `Agent` tool, so no requests are needed). Background `lane` agents (`Agent`,
`subagent_type: "lane"`) are used only when Gabriel asks for agents instead of sessions; they hit the worktree write block when
they edit a worktree other than the coordinator's, and need the coordinator to run `re-analyst` and the reviews for them.
