---
name: run-group
description: The default workflow for a request to complete roadmap work - "complete all tasks under AUD1", "do sty1", "finish skai", "work the AUDIO box", "clear stage 6", "pick up the next ready groups". This session becomes the coordinator - it resolves the groups, checks prerequisites, claims and the tier focus, splits the work into up to three parallel lanes, starts a lane agent per lane in its own worktree, runs the steps lanes cannot (re-analyst, reviews, questions for Gabriel), and reports; each lane follows the full lane loop with every skill and tool.
argument-hint: <group id | box id | stage | "next"> [more ids]
---

# Run a group: coordinator and lanes (the default workflow)

A request to complete roadmap work goes through this skill, whatever its wording. **This session is the coordinator**: it plans,
starts lanes, serves them and reports; it does not implement tasks itself. **Each lane is a `lane` agent** working one group (or
one share of a group's tasks) in its own worktree, following `.claude/agents/lane.md` step by step with every skill and tool
(`docs/TOOLS.md`). Lanes run in the background and in parallel.

One limit shapes the split of work: a lane is a subagent, and **subagents cannot start agents**. So the steps that need the
`Agent` tool are done by the coordinator on a lane's request: ReVa through `re-analyst`, the `/code-review` and `spec-review`
reviews, the `reviewer` read, and questions for Gabriel. Everything else (building, testing, every other skill, REA, cdb, Frida,
`re_reference/`, probes, roadmap writes, landing) the lane does itself. ("Coordinator protocol" in `lane.md` is the lane side.)

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

Show the plan (lanes, queues, shares, what waits on what) in a few lines, then start.

## 4. Start the lanes

For each lane:
1. **Worktree**: `tools\new_worktree.cmd <lane-name>` (branch `claude/<lane-name>` from `origin/dev`, junctions to `toolchain`,
   `Data`, `extracted`, `roadmap-store`, roadmap synced). Lane name: the group id, plus `-a`, `-b` for shares (`sty1-a`).
2. **Claim**: `roadmap_store.py claim <group> --by <lane-name>` (shares of one group: claim once, by the coordinator's name).
3. **Spawn** a `lane` agent in the background (`Agent`, `subagent_type: "lane"`, `run_in_background: true`) with a brief that
   names: the worktree's absolute path (it works only there, with absolute paths, every shell command starting with a `cd` into
   it), the group and the task ids of its share, the exit test, the modules it owns and the ones it must not touch, the lane
   queue after it, and "follow `.claude/agents/lane.md` including its Coordinator protocol".
4. **Model and effort from the group's `preset`** (Gabriel, 2026-11-15): the preset picks the model, and effort follows the preset
   but never above the lane's own `high` (`lane.md` frontmatter: Opus / high; `xhigh` costs too much on long lane runs). So:

   | Preset | Lane runs on | `Agent` call |
   |---|---|---|
   | `xhigh`, `high`, `fable`, none | Opus, high | nothing extra (the lane default) |
   | `sonnet` | Sonnet, high | `model: "sonnet"` |
   | `tools` | Sonnet, medium | `model: "sonnet"`, `effort: "medium"` |
   | `gab` | Opus, medium; Gabriel's parts come back as `REQUEST gabriel` | `effort: "medium"` |
   | `re2ui` | RE share on Opus, high; UI share as its own lane on Sonnet, high (one Opus lane when it cannot be split) | UI lane: `model: "sonnet"` |

   This skill is the explicit instruction the `Agent` tool needs before `model` / `effort` are passed (an exception, for lanes, to
   `DECISIONS.md` "Agent model and effort"). A lane agent keeps one model for its life, so a queued group whose preset maps to a
   different row gets a fresh lane agent (same worktree is fine) once the group before it has landed.

Then wait for lanes to report; don't poll. Up to three lanes run at once; when one finishes its queue and work remains, start the
next lane.

## 5. Serve the lanes

A lane stops with a **request** (format in `lane.md`) when it reaches a step that needs the `Agent` tool or Gabriel. Serve it, then
resume that lane with `SendMessage` (to its agent id) carrying the result:
- **`re-analyst`**: start it with the lane's questions file (`<lane worktree>\docs\re\<TASK>_RE_QUESTIONS.md`) and tell it to
  write its answer into the spec files **in that worktree** (absolute paths), so the lane reads it and lands it with its work.
  One at a time (Ghidra lock): queue questions from several lanes and run them in turn.
- **Review** (the lane has committed its work): in the lane's worktree run `/code-review high` on its branch against
  `origin/dev`, `spec-review` with the group and its share of tasks, and for sim code the `reviewer` agent with the changed files.
  Send back the findings ranked; the lane fixes, files or rejects each (`finish-group` step 2).
- **A question for Gabriel** (a decision, a `g` task, a UAC prompt, a look at `alamo_game.exe`): ask him, send the answer back; if
  he is away, tell the lane to continue with what does not depend on it.
- **A blocker between lanes** (one lane needs another's work on `dev` first): hold the waiting lane until the other has landed,
  then resume it (it merges `origin/dev` on landing anyway).

Keep each lane's results in your own context short: the request, the answer, the commit.

## 6. Close and report

When a lane reports it landed: check its `dev` commit exists (`git log origin/dev`), that its tasks are `done` on the roadmap and the
claim released. For a group split into shares, once every share has landed: run the exit test once more on `dev` and close the
group (`finish-group` step 3: group and box done, or `close-group`). Clean up merged worktrees (`tools\clean_worktrees.cmd`).

Report per group: tasks closed and open (why), the exit test result, the `dev` commits, goldens changed, skips, anything Gabriel
must do, and the lanes' friction lines for `retro`.

## Running without lanes

For one small group (a couple of tasks in one module), or when Gabriel asks for it to be done here, the coordinator may run the
lane loop itself in this session (it keeps the `Agent` tool, so no requests are needed). When Gabriel asks for **sessions**
instead of agents, start one session per lane with `start_session` (each is a full session with the `Agent` tool, prompt
`/run-group <ids>` for its queue, worktree of its own) and coordinate them from here. Sessions don't read agent files, so apply
the step-4 table yourself: the session's `model` when starting it, and its effort with `set_session_effort`.
