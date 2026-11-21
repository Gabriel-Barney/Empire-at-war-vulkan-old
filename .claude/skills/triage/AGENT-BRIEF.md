# Writing agent briefs

Adapted from `mattpocock/skills` (MIT, `.claude/skills/LICENSE-mattpocock-skills.txt`). Here a brief is the text of a roadmap
task (and, for a new group, its `why` and `exit`) that a lane will work from, possibly weeks later. The original finding is
context; the brief is the contract.

## Principles

**Durable over precise.** The code will move before a lane picks the task up.
- Do name types, functions, XML tags, Lua calls, CRC columns and `docs/re/` sections the lane should look for.
- Don't rely on line numbers or on the current file layout staying the same. A file path is fine as a starting hint
  ("today in `engine/tactical/...`"), never as the definition of the work.

**Behaviour, not procedure.** Say what the game or tool should do, not how to edit it.
- Good: "A retreating fleet keeps its hyperspace timer when the tactical battle is saved and loaded (save round trip, galactic CRC
  unchanged)."
- Bad: "Add a field to the struct in retreat.cpp line 210."

**Testable acceptance.** Each criterion can be checked on its own: a ctest entry, a parity-harness metric with its tolerance, a
golden that must or must not move, a `docs/re/` fact matched. "Works correctly" is not a criterion. For parity work say the tier:
tier 1 exact, tier 2 statistical (name the metric and seeds).

**Explicit scope.** Say what is out of scope so the lane does not gold-plate, and which goldens are expected to change.

## Task text (one or two sentences)

`<behaviour that should hold> (<acceptance: test / metric / golden>; out of scope: <x>)`, plus `--ref <docs path>` when a
longer brief or spec exists.

Example: "Bribe refusal follows retail: a unit already bribed or a hero refuses, with the refusal SFX (tier 1; check: s78m_bribe
probe refusal cases match `docs/re/S78M_T06_BRIBE_RUN.md`; out of scope: bribe cost scaling)."

## Longer brief (new group `why`, or a `docs/status/` page cited by `--ref`)

```markdown
**Category:** bug / enhancement / parity tier N
**Summary:** one line
**Current behaviour:** what happens now (for a bug, the broken behaviour)
**Desired behaviour:** what should happen, including edge cases and error conditions
**Key interfaces:** types, functions, data fields, Lua / XML names that change and why
**Acceptance criteria:** - [ ] each independently checkable (test, metric, golden, docs/re fact)
**Out of scope:** adjacent things not to touch; goldens expected to move, or "none"
```

## A bad brief

"Fix the bribe thing in tactical; see the function around line 150." No behaviour, no acceptance, no scope, a line number that
will be stale.
