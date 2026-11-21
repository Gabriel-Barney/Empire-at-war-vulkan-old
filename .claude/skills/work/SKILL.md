---
name: work
description: Work a roadmap group start to finish in this checkout - resolve it, check prerequisites and the tier focus, plan, build, review, update the roadmap and submit to the landing queue. Use for any request to do roadmap work ("do aud3", "work the AUDIO box", "pick up the next group"), in the sim or non-sim writer checkout. Replaces the lane loop (start-group, lane.md, finish-group) for the two-writer workflow (wr4 t03).
argument-hint: <group-id | box id>
---
Work roadmap group `$ARGUMENTS` here, in this session's own checkout. Read-only help runs as subagents (`re-analyst` through
`re-question`, `reviewer`, `research`, `Explore`); retail probe runs stay with this session (it holds the computer-use grant).
Each step ends in the state named after "Done when".

1. **Resolve.** `python roadmap-store\roadmap_store.py sync`, then read `roadmap-store\data\items\<id>.json` (a box id: its
   open groups in order). Show title, `writer`, exit test, open tasks with `tier`, `g` and `sev`, and `after`.
   Done when: every `after` group is `done`, the group is open, its `writer` matches this checkout's `.alamo_writer` (a sim group
   in the non-sim checkout: stop and say so; the path guard would block its edits anyway), and tier-3 tasks are set aside under
   the current focus (CLAUDE.md rule 6). Gabriel-only (`g`) tasks stay open and are listed at the end.
2. **Plan.** Run the `plan` skill for the group. Done when: the plan is on the group (`plan` field) and approved by Gabriel, or
   approved in an earlier batch (`plan.approved`); every binary question and probe the plan needs has its answer in `docs/re/`.
   Set the session goal from the exit test (`/goal <exit test> and roadmap_store.py lint is clean`).
3. **Build it.** Test first where the plan names tests (`tdd`); an unexplained failure goes through `diagnosing-bugs`. The
   edit hook compiles each edited file on MSVC: fix what it reports before the next edit. Iterate with
   `build_agent.cmd msvc all changed` (affected tests only). Done when: `build_agent.cmd msvc all` (the landing tier)
   passes with 0 warnings and the summary's `skips:` line is read. Clang and the sanitizers run nightly only
   (`docs/DECISIONS.md`, "Clang runs nightly only"); run them by hand only to fix a nightly failure (`ngt1`). Golden drift is not fixed by hand: if the behaviour change is intended, declare it (step 5).
4. **Review.** Run the `review` skill on the diff against `origin/dev`. Done when: each finding is fixed, filed by severity, or
   reported as rejected, and the landing diff's behaviour impact matches the plan's prediction (or the difference is explained).
5. **Commit.** Commit with a message that lists the change, build and test results per compiler, skips and tasks closed. Every
   commit that touches sim code ends with a `Behaviour:` trailer: `none`, or the CRC system columns it changes
   (`docs/status/golden-checkpoints.md`, "Computed results"). Then the `Co-Authored-By:` line.
6. **Roadmap.** `phase-status` skill: finished tasks `done`; no `todo` left means the group is `done` (and its box when it was
   the last open group); exit test passed with tasks left means `close-group`; findings outside the group filed as the rules say
   (S1 / S2 as tasks of the owning group, S3 to the backlog; Stage 6 and 9 keep follow-ups as tasks). Done when:
   `roadmap_store.py lint` shows nothing new for this group.
7. **Land.** `python -I tools/gate/land_queue.py submit --note "<group> <what>"`, then go on with other work. Done when:
   `land_queue.py status <id>` says `landed` (rejected: read its summary file, fix, commit, submit again). Then
   `python -I tools/gate/writer_setup.py sync .` puts this checkout on dev's tip.
8. **Report.** Tasks closed and open (why), the exit test result, the landed commit, goldens or behaviour declared, skips, the
   `g` tasks and questions for Gabriel, and one friction line (runs wasted and what would have prevented them, or "none").
