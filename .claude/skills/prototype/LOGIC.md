# Logic prototype

Adapted from `mattpocock/skills` (MIT, `.claude/skills/LICENSE-mattpocock-skills.txt`).

One self-contained HTML file that lets anyone drive a state model by clicking buttons. Use it when the question is about **rules,
state transitions or data shape**: the kind of thing that looks fine on paper and feels wrong once pushed through real cases (a
retreat timer interacting with a bribe, the order of galactic turn steps, a build-queue rule, how a mod's data overrides resolve).
One file with nothing to install, so Gabriel can open it and feel the model. It speaks the game's language, not the code's.

Not for "what should this look like": that is [UI.md](UI.md).

## Process

### 1. State the question

Before writing code, write one paragraph at the top of the page (visible, not a comment): the state model and the question. Cite
the `docs/re/` spec or retail data the rules come from, and mark guesses as guesses. A prototype that answers the wrong question is
waste, so make the question checkable later.

### 2. Isolate the logic in a portable module

Put the logic in one `<script>` block written as a small, pure module: a reducer `(state, action) => state`, an explicit state
machine (when "which actions are legal now" is part of the question), pure functions over plain data, or a small class when the
logic really owns ongoing state. Pick the shape that fits the question, not the one easiest to wire to a page. No DOM, no
`document`, no button handlers inside it: the page calls in, nothing flows back.

It is a reference for the real code, not code to paste: the engine version is written in C++ through `tdd`, following the
determinism rules (rule 3 and the `determinism-check` skill), seeded `SimRng` instead of `Math.random`, and the prototype's walkthroughs as test cases.

### 3. Build the HTML file

One file, plain HTML / CSS / JS, everything inline, no framework, bundler or server. Labels are in game terms (units, planets,
credits, ticks), not code terms. Layout, top to bottom:

1. **Title and one line** on what the demo explores (the question).
2. **Current state**: the full relevant state as a readable panel of labelled fields, re-rendered after every click, with what just
   changed called out.
3. **Free-play buttons**: one per action, always available, in any order.
4. **Guided walkthroughs**: one tab per scenario: a short description (the situation and what to watch for) and the ordered
   buttons to press, each a real button that performs the step. Starting a walkthrough resets to a known state. Pick the awkward
   cases: the happy path, a tricky edge case, and an attempt at something that should be illegal; where retail behaviour is known,
   a scenario that reproduces it.

Restrained and clean: readable type, generous spacing, one accent colour, no animation.

### 4. Hand it over

Write it under `%TEMP%` or next to the module with `prototype` in the name, open it (`start "" "<path>"`) and give Gabriel the
absolute path. The useful moments are "wait, that shouldn't be possible" and "I assumed X": bugs in the idea. Add actions or
scenarios when asked.

### 5. Capture

When it has answered its question, capture the answer and the prototype as [SKILL.md](SKILL.md) rule 8 says: the validated rules go
into the C++ module through `tdd` (the walkthroughs become test cases), the HTML file goes to the `claude/prototype-<name>` branch.

## Anti-patterns

- **Tests in the prototype.** A prototype that needs tests is no longer a prototype (the C++ version gets them).
- **Wiring it to real data or saves.** In-memory state unless persistence is the question.
- **Generalising.** No "what if we later support X": one question.
- **Logic mixed into the page.** If the module touches the DOM it is not a clean reference any more.
- **A framework, bundler or server.** One file, double-click.
