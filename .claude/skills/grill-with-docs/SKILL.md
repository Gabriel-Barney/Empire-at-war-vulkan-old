---
name: grill-with-docs
description: Interview the user relentlessly about a plan, design or decision in rounds until every branch is settled, sharpening the project's terms and recording decisions in docs/DECISIONS.md, docs/PHASE4.md and the glossary as they land. Use when Gabriel wants to stress-test a plan or idea, says "grill me" or "poke holes in this", before to-tickets turns a vague plan into roadmap work, or when improve-codebase-architecture / triage / codebase-design need the decisions walked with him.
argument-hint: [the plan, design or question]
---

# Grill with docs

Interview the user until you reach a shared understanding, and write down terms and decisions the moment they settle. Adapted from
`mattpocock/skills` `grill-with-docs`, which combines its `grilling` and `domain-modeling` skills; both are folded in here (MIT,
`.claude/skills/LICENSE-mattpocock-skills.txt`). Where things are written: [DOCS.md](DOCS.md).

Needs a person to answer. In an unattended run, write the first round of questions (with your recommended answers) to your report
or a scratch file, and stop; never answer them yourself.

## The design tree

Map the plan as a **design tree**: every decision branches into the decisions that hang off it. Work it in **rounds**. The
**frontier** is every decision whose prerequisites are already settled: what you can ask now without guessing at answers you have
not heard. Ask the whole frontier in one round, numbered, each with your recommended answer, then wait.

```
❓ **Q1** - **<question title>**: <question, with the choices when there are some>

➡️ <your recommended answer>

---

❓ **Q2** - ...
```

Word each question so "yes" accepts your recommendation. When the frontier has four questions or fewer, `AskUserQuestion` (when
available) can carry the round, recommendation first.

Each round reshapes the tree: settled decisions push the frontier out and unblock questions that depended on them. A question whose
answer depends on another question still open belongs to a later round.

**Facts are your job, decisions are the user's.** When a question needs a fact (what the code does, what retail does, what a
`docs/re/` spec says, what a test shows), find it yourself: read, run, or dispatch a sub-agent with a fresh context; for retail
behaviour that `docs/re/` does not settle, `re-question`. Don't block the whole round on it: only the questions downstream of a
running lookup wait. Never ask the user something you could look up; always put the decisions to them.

The session is done when the frontier is empty: every branch visited, nothing silently assumed. Don't act on the plan until the
user confirms the shared understanding. Then `to-tickets` can turn it into roadmap work.

## Sharpen the language as you go

- **Challenge against the existing terms.** When the user's word conflicts with how the docs or code use it, say so at once: "the
  glossary / `docs/re/` uses 'retreat' for X, you seem to mean Y; which is it?"
- **Sharpen fuzzy words.** Propose one precise term for an overloaded one: "'player' here: the faction slot or the human at the
  keyboard?"
- **Stress-test with concrete scenarios.** Invent cases that probe the edges between concepts (a unit bribed mid-retreat, a mod that
  renames a faction, worker count 0 vs 16).
- **Cross-check the code.** When the user says how something works, check the code agrees, and surface a contradiction: "the port
  cancels the whole build queue, but you said partial cancel is possible".
- **Write terms down when they settle**, not in a batch at the end ([DOCS.md](DOCS.md)).

## Record decisions sparingly

Offer to record a decision only when all three hold:
1. **Hard to reverse**: changing your mind later costs something real.
2. **Surprising without context**: a future reader would wonder "why did they do it this way?"
3. **A real trade-off**: there were genuine alternatives and one was picked for reasons.

Otherwise skip it. Binding decisions are Gabriel's: write them with his name and the date ([DOCS.md](DOCS.md)).
