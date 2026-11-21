---
name: spec-review
description: Two-axis review of a branch diff - Standards (does it follow CLAUDE.md, docs/BUILD.md, docs/DECISIONS.md and the determinism rules?) and Spec (does it do what its roadmap group's tasks, exit test and docs/re spec asked, no more and no less?), run as two separate read-only reviews and reported side by side. Use in finish-group next to /code-review, before landing any branch, when asked to "review against the spec", "check the branch did what the task said", or "review since <commit>".
argument-hint: [fixed point, default origin/dev] [group id or spec path]
---

# Spec review

Two-axis review of `HEAD` against a fixed point. Adapted from `mattpocock/skills` `code-review` (MIT,
`.claude/skills/LICENSE-mattpocock-skills.txt`) and renamed so the built-in `/code-review` (correctness bugs) keeps its name; the
two complement each other and `finish-group` runs both.

- **Standards**: does the code follow this repo's documented rules?
- **Spec**: does the code do what the originating task asked?

Each axis runs as its own read-only review with a fresh context, so one cannot mask the other.

## 1. Pin the fixed point

Default `origin/dev` (after `git fetch origin`); otherwise what the user named (commit, branch, tag). Run `git rev-parse <fixed>`,
then `git diff --stat <fixed>...HEAD` (three dots: against the merge base) and `git log --oneline <fixed>..HEAD`. A bad ref or an
empty diff stops here.

## 2. Find the spec

In this order:
1. The roadmap group: named by the user, in commit messages, or claimed by this session (`roadmap_store.py claims`). Read
   `roadmap-store/data/items/<id>.json`: the tasks this branch closes, the group's `exit` test, `why`.
2. Backlog entries or tasks cited in commit messages (`bNNNN`, `tNN`), with their `ref`.
3. The `docs/re/` spec, `docs/PHASE4.md` step or `docs/MODS.md` section those tasks cite (the behaviour to match, by parity tier).
4. Nothing found: ask. If there is none, skip the Spec axis and say so.

## 3. Standards sources

Always: `CLAUDE.md` (rules 1-7), `docs/BUILD.md` (test rules: parallel-safe fixtures, `slow` label, worker-count entries, LF
endings), `docs/DECISIONS.md` (scope, parity tiers, approved dependencies), the `determinism-check` checklist for sim code (everything
in `engine/` except render, audio, video, UI), no copied decompiled code anywhere in the diff (pasted pseudo-code, line-by-line transliteration), and `docs/MODS.md` (no mod-specific
names in engine code). Skip what tooling already enforces: warnings (`/W4`, clang flags), `.clang-tidy`, the mod-name and
string-literal lints.

On top, the **smell baseline** (Fowler, _Refactoring_ ch. 3). The repo overrides it, and each smell is a judgement call, never a
hard violation:
- **Mysterious name**: a name that doesn't say what it does or holds. Rename; no honest name means the design is murky.
- **Duplicated code**: the same logic shape in more than one hunk or file. Extract and share.
- **Feature envy**: a function reaching into another type's data more than its own. Move it to that data.
- **Data clumps**: the same few fields or parameters travelling together. Bundle them into a type.
- **Primitive obsession**: a raw int / string standing for a domain concept. Give it a small type.
- **Repeated switches**: the same switch / if-cascade on the same type in several places. One table or polymorphism.
- **Shotgun surgery**: one logical change scattered across many files. Gather what changes together.
- **Divergent change**: one file edited for several unrelated reasons. Split it.
- **Speculative generality**: hooks or parameters the spec doesn't need. Delete them.
- **Message chains**: long `a.b().c().d()` walks. Hide the walk behind one function.
- **Middle man**: a type or function that only delegates. Call the real target.
- **Refused bequest**: a subclass ignoring most of what it inherits. Use composition.

## 4. Run both axes

Spawn two `reviewer` agents in parallel (read-only, fresh context), each given the fixed point, the diff and log commands, and:

- **Standards**: the source list from step 3 and the smell baseline pasted in full. Brief: "Report per file / hunk (a) each place
  the diff breaks a documented rule, citing file and rule; (b) each baseline smell, named, with the hunk quoted. Separate hard
  violations from judgement calls; a documented rule overrides the baseline. Skip what tooling enforces. Under 400 words."
- **Spec**: the group / task / spec text from step 2. Brief: "Report (a) what the tasks or exit test asked for that is missing or
  partial; (b) behaviour in the diff nobody asked for (scope creep, including goldens moved without a task saying so); (c) things
  that look implemented but wrong against the spec or its parity tier. Quote the spec line for each. Under 400 words."

If you are yourself a sub-agent (no `Agent` tool), do the two axes one after the other, writing the Standards report in full before
reading for the Spec one.

## 5. Report

Show the two reports under `## Standards` and `## Spec`, lightly cleaned, not merged or re-ranked. End with one line: the number
of findings per axis and the worst one within each. Then handle findings as `finish-group` step 2 says: fix confirmed ones on the
branch, file the rest by severity (S1 / S2 as group tasks, S3 to the backlog), and report any you judge wrong.
