# Design it twice

When exploring alternative interfaces for a chosen deepening candidate, use parallel sub-agents. Based on "Design It Twice"
(Ousterhout): the first idea is unlikely to be the best. Uses the vocabulary in [SKILL.md](SKILL.md).

## 1. Frame the problem space

Before spawning sub-agents, write for the user:
- the constraints any new interface must satisfy (determinism, threading phase, the callers that must keep working, goldens that
  must not move);
- the dependencies it relies on and their category ([DEEPENING.md](DEEPENING.md));
- a rough code sketch that makes the constraints concrete (not a proposal).

Show this, then go straight to step 2; the user reads while the sub-agents work.

## 2. Spawn sub-agents

Spawn 3 or more sub-agents in parallel (`Agent` tool, `general-purpose` or `Plan`), each with a fresh context and a separate
technical brief: file paths, coupling details, dependency category, what sits behind the seam, the [SKILL.md](SKILL.md) glossary,
and the project terms from the `docs/` pages that cover the area (`docs/PHASE2.md` design notes, `docs/PHASE4.md` step map, the
`docs/re/` spec). Give each a different constraint:

- Agent 1: minimise the interface: 1 to 3 entry points, maximum leverage per entry point.
- Agent 2: maximise flexibility: many use cases and extension (mods load through data, never mod-specific code: `docs/MODS.md`).
- Agent 3: optimise for the most common caller: make the default case trivial.
- Agent 4 (if applicable): ports and adapters for the cross-seam dependencies.

Each returns: the interface (declarations plus invariants, ordering, threading, error modes), a usage example, what the
implementation hides, the dependency strategy and adapters, and the trade-offs.

If you are yourself a sub-agent (no `Agent` tool), draft the designs one after the other under the same four constraints, writing
each in full before starting the next.

## 3. Present and compare

Present the designs one at a time, then compare them in prose by **depth**, **locality** and **seam placement**. Then give your
own recommendation and why; propose a hybrid when parts combine well. Be opinionated: the user wants a strong read, not a menu.
Gabriel's decision goes to `docs/DECISIONS.md` when it is binding, or to the step's entry in `docs/PHASE4.md` when it is a port choice.
