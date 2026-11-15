---
name: prototype
description: Build throwaway code that answers one design question - a logic / state-model demo Gabriel can click through, or several radically different UI variants of an in-game screen or the Command Center launcher to compare - then fold the validated decision into real code and keep the prototype off dev. Use when a rule, state machine or data shape needs to be felt out before writing engine code, when asked what a screen or panel should look like, or when the user says prototype, mock up, try a few options or sanity-check this.
argument-hint: [the question to answer]
---

# Prototype

A prototype is **throwaway code that answers a question**; the question decides the shape. Adapted from `mattpocock/skills`
(MIT, `.claude/skills/LICENSE-mattpocock-skills.txt`) to this engine.

## Pick a branch

From the prompt, the surrounding code, or by asking when the user is around:
- **"Does this logic / state model feel right?"** (a rule, a state machine, a data shape) → [LOGIC.md](LOGIC.md): one shareable
  HTML file with free-play buttons and guided walkthroughs that pushes the model through cases hard to reason about on paper.
- **"What should this look like?"** (an in-game screen, HUD panel, menu, or the Command Center launcher) → [UI.md](UI.md): several
  structurally different variants in the real UI, switchable at run time.

Getting this wrong wastes the prototype. Genuinely ambiguous and nobody to ask: pick the branch that matches the surrounding code
(sim or data module → logic; `engine/ui`, `engine/render` overlay or `launcher/` → UI) and state the assumption at the top of it.

## Rules for both

1. **Throwaway from the start, and marked.** Put it next to what it prototypes so the context is obvious, with `prototype` in the
   file or folder name; follow the module's existing layout, don't invent a new top-level folder.
2. **Trivial to run.** A logic demo is one HTML file opened by double-click. A UI prototype starts with one command from the
   project's own scripts (a build script plus a flag, `launcher\cargo.cmd run`), written at the top of the prototype.
3. **No persistence.** State lives in memory; nothing writes saves, `mod_runs\`, the fx cache or the roadmap. If persistence is the
   question, use a scratch folder named `PROTOTYPE-wipe-me` under `%TEMP%`.
4. **Skip the polish.** No tests, no error handling beyond what keeps it runnable, no abstractions. Learn fast.
5. **Surface the state.** After every action (logic) or variant switch (UI), show the full relevant state so changes are visible.
6. **Keep it off `dev`.** No prototype code lands on `dev`, and nothing in it is reachable from sim code (rule 3: it is not
   deterministic and not in the CRC).
7. **Agents never drive `alamo_game.exe`** (CLAUDE.md rule 7). Gabriel looks at in-game variants; you hand him the command.
8. **Capture it when done.** Write down the answer (the question and the verdict) in the roadmap task or log line and, when it is a
   binding call, `docs/DECISIONS.md` (or the step's entry in `docs/PHASE4.md` for a port choice). Fold the validated decision into
   real code through the normal path (`tdd`, tests, both compilers for sim code). Keep the prototype itself as a primary source on a
   local branch `claude/prototype-<name>` (commit it there, don't push it; origin keeps only `dev` and `main`), and name that branch
   in the roadmap task or log line.
