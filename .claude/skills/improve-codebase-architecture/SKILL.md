---
name: improve-codebase-architecture
description: Scan engine/ or tools/ for deepening opportunities (shallow modules, leaky seams, code hard to test through its interface), present them as a visual HTML report, then work through the one the user picks. Use when asked to review or improve the codebase's architecture or structure, find refactoring opportunities, or when the same area keeps causing bugs, golden churn or long searches; also when diagnosing-bugs finds no seam for a regression test.
---

# Improve codebase architecture

Surface architectural friction and propose **deepening opportunities**: refactors that turn shallow modules into deep ones, for
testability and agent navigability. Adapted from `mattpocock/skills` (MIT, `.claude/skills/LICENSE-mattpocock-skills.txt`).

Built on the `codebase-design` skill: invoke it first for the vocabulary (module, interface, depth, seam, adapter, leverage,
locality) and the principles (deletion test, "the interface is the test surface", "one adapter = hypothetical seam, two = real").
Use those terms exactly; never drift into "component", "service", "API" or "boundary".

The project's own language and decisions take the place of a glossary and ADRs: domain terms come from `docs/PHASE2.md` (design
notes), `docs/PHASE4.md` (step map), `docs/re/` and `docs/MODS.md`; binding decisions are in `docs/DECISIONS.md`, and port
choices in `docs/PHASE4.md`. Do not re-litigate a decision recorded there.

**Current focus (CLAUDE.md rule 6).** Architecture work is tier 3 and paused unless it serves a tier-1 / tier-2 group's exit test.
So this skill proposes and records; it does not start a refactor on its own. A candidate the user wants done now becomes a task of
the open group it serves; every other candidate is filed as an S3 tier-3 backlog entry (step 3).

## 1. Explore

**Scope before you scan.** Deepening pays off by making future changes easier, so weight the parts that change most:
- If the user named a direction (a module, a subsystem, a pain point), take it and skip the rest of this list.
- Otherwise walk back the history (`git log --oneline -200 --stat`) for hot spots: files and modules that keep changing, golden
  regenerations, `fix` commits, `diagnosing-bugs` findings with no seam (backlog entries mentioning a missing seam). Let those pull
  your attention first; widen the net if changes are scattered.

Then spawn a sub-agent (`Explore` or `general-purpose`, fresh context) to walk the code and note friction, not to follow rigid
heuristics. If you are yourself a sub-agent, walk it yourself. Questions:
- Where does understanding one concept mean bouncing between many small modules?
- Where are modules **shallow**, with an interface nearly as complex as the implementation?
- Where were pure functions extracted just for testability while the bugs hide in how they are called (no **locality**)?
- Where do tightly coupled modules leak across their seams (shared mutable state, one module reaching into another's internals)?
- Which parts are untested or hard to test through their current interface?
- Project-specific: where does sim code mix with presentation, or a mod-specific name sit in engine code (`docs/MODS.md`), or a
  system read state that is outside the CRC?

Apply the **deletion test** to anything you suspect is shallow: would deleting it concentrate complexity, or just move it?

## 2. Present candidates as an HTML report

Write one self-contained HTML file to `%TEMP%\architecture-review-<yyyymmdd-hhmmss>.html` (nothing lands in the repo), open it with
`start "" "<path>"` and give the user the absolute path. Layout, diagrams and tone: [HTML-REPORT.md](HTML-REPORT.md).

One card per candidate:
- **Files**: the files / modules involved.
- **Problem**: why the current shape causes friction (cite the commits, bugs or backlog entries that show it).
- **Solution**: plain description of what would change.
- **Benefits**: in terms of locality and leverage, and how tests would improve.
- **Before / after diagram**.
- **Determinism and goldens**: which goldens must stay unchanged, whether sim code is touched.
- **Tier**: the tier-1 / tier-2 group it would serve, or "tier 3".
- **Recommendation strength**: `Strong`, `Worth exploring` or `Speculative`.

End with a **Top recommendation** section: which to do first and why. A candidate that contradicts `docs/DECISIONS.md` appears only
when the friction is real enough to reopen the decision, marked in its card ("contradicts DECISIONS.md, <row>, worth reopening
because ...").

Do not propose interfaces yet. After writing the file, ask the user which candidate to explore. In an unattended run (no user to
ask), stop here: file the candidates (step 3) and report.

## 3. Work through the chosen candidate

Walk the decision tree with the user in rounds (the `grill-with-docs` skill): constraints,
dependencies and their category (`codebase-design` DEEPENING.md), the shape of the deepened module, what sits behind the seam,
which tests survive, which goldens must stay put. To explore alternative interfaces, use `codebase-design` DESIGN-IT-TWICE.md.

As decisions land, record them where `docs/README.md` says:
- A new or sharpened domain term goes into the `docs/` page that owns the area (design notes in `docs/PHASE2.md`, the step in
  `docs/PHASE4.md`).
- The user rejects a candidate for a reason a future review would need: offer to record it in `docs/DECISIONS.md` so it is not
  suggested again. Skip passing reasons ("not now") and self-evident ones.
- The outcome goes on the roadmap: `python roadmap-store/roadmap_store.py backlog add "<candidate: problem, solution, files>"
  --by <session> --tier 3 --kind fix --from <group if any>` for tier-3 work (S3 is the default), or, when it serves a tier-1 /
  tier-2 group's exit test, `backlog add ... --sev S2 --to <group>` (or a task added to that group with `write`). Gabriel picks
  backlog entries (`backlog promote`).
