# Where grilling results go

Adapted from `mattpocock/skills` `domain-modeling` (GLOSSARY-FORMAT.md, ADR-FORMAT.md; MIT,
`.claude/skills/LICENSE-mattpocock-skills.txt`). This project has one place per kind of fact (`docs/README.md`); upstream's
`GLOSSARY.md` and `docs/adr/` map onto it like this:

| Kind | Where | Notes |
|---|---|---|
| Binding decision (scope, platform, parity, dependencies, controls, graphics, process) | `docs/DECISIONS.md` | A dated section or table row with Gabriel's name; record why, and the rejected options when the rejection is not obvious |
| Port choice where the spec was silent | the step's entry in `docs/PHASE4.md` | Moves to `docs/PHASE4_HISTORY.md` when the step is finished |
| What retail does | the matching `docs/re/` spec | Never from decompiled output (clean-room) |
| Mod-loading rule | `docs/MODS.md` | |
| A project term | `docs/GLOSSARY.md` | Created on the first term; add its row to `docs/README.md` in the same change |
| Work that follows | the roadmap, via `to-tickets` or `triage` | Status lives only there |

## Glossary format (`docs/GLOSSARY.md`)

```md
# Glossary

Terms specific to this port and the game it re-implements; general programming terms do not belong.

## <Area, e.g. Galactic, Tactical, Mods, Determinism>

**Retreat**:
A tactical battle ending where one side leaves through hyperspace or reinforcement points, keeping its surviving units.
_Avoid_: withdraw, flee
```

Rules:
- **Be opinionated.** One word per concept; list the others under `_Avoid_`.
- **Tight definitions.** One or two sentences: what it IS, not how the code does it. No implementation details; the glossary is not
  a spec or a scratch pad.
- **Project terms only.** Game and port concepts (retreat, bribe, hardpoint, tick, CRC checkpoint, worker count), not general ones.
  Retail's own names (XML tags, Lua functions) are quoted as they are and may be the canonical term.
- **Group under subheadings** when clusters appear.

## Decision format (`docs/DECISIONS.md`)

Follow the file's existing style: a `## <title> (Gabriel, <date>)` section with bullets, or a row in the matching table. One to
three sentences can be enough: the context, what was decided, why. Add the considered options only when they are worth
remembering, and the consequences only when they are not obvious.

What qualifies: architectural shape (the job system, the sim / presentation split), deliberate deviations from the obvious path,
dependency choices with lock-in, scope calls (the explicit no-s are as valuable as the yes-s), constraints not visible in the code,
and rejected alternatives that someone would otherwise suggest again.
