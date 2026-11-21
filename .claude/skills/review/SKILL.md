---
name: review
description: One risk-scaled review pass of a branch diff before it is submitted - picks the depth from what changed (built-in code review at medium or high, spec review against the plan and its predicted behaviour impact, the determinism reviewer for threading code, simplify on new code) and handles every finding. Use as step 4 of the work skill, before any landing submission, or when asked to review a change at the right depth (wr5 t04).
argument-hint: [<group-id>]
---
Review this checkout's diff against `origin/dev` once, at the depth the diff calls for. Commit first (a WIP commit is fine).

1. **Classify the diff** (`git diff --stat origin/dev...HEAD`). Done when each line below is answered yes / no:
   - docs, skills or roadmap only (no code)?
   - sim code (engine/ except render, audio, video, ui)?
   - threading, job-system or parallel-loop code (`engine/jobs`, `parallel_for`, atomics, mutexes, per-worker outputs)?
   - new code (new files or new functions of more than a few lines)?
2. **Run the passes** that apply, in parallel where they are independent:
   | Diff | Passes |
   |---|---|
   | docs / skills only | none by agent: read it once against `writing-for-agents` |
   | non-sim code | `/code-review medium`; `spec-review` against the group's plan and tasks |
   | sim code | `/code-review high`; `spec-review`, which also checks that the computed behaviour impact (`tools/goldens/behaviour_check.py range origin/dev HEAD`) matches the plan's prediction |
   | threading code | add the `reviewer` agent with the changed files (determinism and thread safety; `determinism-check` lists the rules) |
   | new code | add `/simplify` on the new code |
   `max` and `ultra` review levels only when Gabriel asks.
3. **Handle every finding.** Fix confirmed correctness findings and rebuild what they touched; a finding that does not block the
   exit test is filed by severity (S1 / S2: a task of the owning group, `backlog add ... --sev S1|S2 --to <group>`; S3:
   `backlog add ... --from <group>`); a finding judged wrong is reported as rejected with the reason. Done when: the report
   lists the passes run and why, and the counts fixed / filed / rejected.
