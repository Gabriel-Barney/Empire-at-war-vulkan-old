# UI prototype

Adapted from `mattpocock/skills` (MIT, `.claude/skills/LICENSE-mattpocock-skills.txt`); the web specifics there are replaced with
this project's two UIs: the in-game UI (`engine/ui`, drawn by the Vulkan renderer in `alamo_game.exe`) and the Command Center
launcher (`launcher/`, Rust / egui).

Generate **several radically different variants** of one screen, switchable at run time; Gabriel flips between them, picks one (or
takes parts of several), and the rest is thrown away. Not for logic or state questions: that is [LOGIC.md](LOGIC.md).

Screen parity first: an in-game screen that exists in retail is matched to retail (tier 1 visible states, `docs/DECISIONS.md`
"Parity tiers"), so variants are for port-only UI (the launcher, port menus, debug and mod tools) or for the parts retail leaves
open. Check `docs/DECISIONS.md` (camera / controls, graphics and display) before proposing a variant that contradicts it.

## Where the variants live

**A: inside the existing screen (strongly preferred).** The variants render on the real screen, with the real surroundings, data
and density; only the screen's drawing swaps. A variant shown alone in an empty window hides the problems a populated one exposes.
Something new that would live inside an existing screen (a panel in the options menu, a tab in the launcher) is still A.

**B: a new throwaway screen (last resort).** Only when there is genuinely no host. Name it with `prototype` and follow the module's
existing layout.

## Process

### 1. State the question and pick N

Default 3 variants, at most 5. Write the plan in one line at the top of the prototype file, e.g. "Three variants of the launcher's
mod list, switchable with `ALAMO_PROTOTYPE_VARIANT` or F9 / F10".

### 2. Make them structurally different

Each variant keeps the screen's purpose and data and uses the UI's own drawing code (the `engine/ui` widgets, or egui), with a
clear name (`VariantA_sidebar`, `VariantB_grid`, ...). Variants differ in layout, information order and the main affordance, not
colour or wording; two that came out similar get redone with an explicit "no grid" style constraint. Lay them out for Gabriel's
3840x2160 screen at 150 % scaling, and check them at 1920x1080 as well.

### 3. Wire the switch

- **In-game:** a dev-only command-line flag (`--ui-variant <A|B|C>`) read by the screen, plus two keys that cycle variants while
  it is open. Never compiled into a release build: behind the existing debug / dev option, or an `#if` the release build leaves out.
  Not reachable from sim code.
- **Launcher:** an environment variable `ALAMO_PROTOTYPE_VARIANT` plus a small floating bar at the bottom centre (previous arrow,
  current variant name, next arrow; also the left / right keys when no text field has focus), visually distinct from the design
  being judged, compiled only in debug builds (`cfg(debug_assertions)`).

Keep all data loading above the switch; only the drawing differs per variant.

### 4. Hand it over

Give Gabriel the one command that starts it (`launcher\cargo.cmd run` with the variable set, or the test-build command with
`--ui-variant`) and the keys. Agents don't run `alamo_game.exe` to look at it (CLAUDE.md rule 7); the launcher you may build and
run. The useful feedback is usually "the header from B with the list from C": that is the design.

### 5. Capture and clean up

Write down the answer (which variant and why), then follow [SKILL.md](SKILL.md) rule 8: rebuild the winner properly in the real
screen (with tests where the UI has them, and zero warnings), remove the switch and the losing variants from the code that lands on
`dev`, and keep the full set on the `claude/prototype-<name>` branch.

## Anti-patterns

- **Variants that differ only in colour or text.** That is a tweak; real variants disagree about structure.
- **Sharing too much between variants.** A shared header is fine; a shared layout defeats the point.
- **Variants that change state.** Read-only is fine; a variant that must act points at a stub, not real saves or settings.
- **Landing the prototype.** Prototype code skipped tests and error handling; rewrite the winner properly.
