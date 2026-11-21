---
name: codebase-design
description: Shared vocabulary and principles for designing deep modules (module, interface, depth, seam, adapter, leverage, locality). Use when designing or changing a module's interface in engine/ or tools/, deciding where a seam goes, making code easier to test or for agents to navigate, when a regression test has no fitting seam, or when another skill (improve-codebase-architecture, spec-review) needs the vocabulary.
---

# Codebase design

Design **deep modules**: a lot of behaviour behind a small interface, placed at a clean seam, testable through that interface.
The aim is leverage for callers, locality for maintainers and testability for everyone. Adapted from `mattpocock/skills`
(MIT, `.claude/skills/LICENSE-mattpocock-skills.txt`) to this C++20 engine.

Project limits that come first:
- **Determinism (CLAUDE.md rule 3).** A restructure of sim code must keep every per-tick CRC bit-identical unless the change is
  meant to alter behaviour; check with the `determinism-check` skill and the goldens. "Same behaviour, new shape" means no golden moves.
- **Current focus (CLAUDE.md rule 6).** A refactor for its own sake is tier-3 work and paused: do it only when it serves a tier-1 /
  tier-2 group's exit test (a seam a regression test needs, a bug the shape keeps causing); otherwise file it as an S3 tier-3
  backlog entry.
- `engine/core` public headers and the top-level CMake change only through the integrating agent.

## Glossary

Use these terms exactly; don't substitute "component", "service", "API" or "boundary".

- **Module**: anything with an interface and an implementation, at any scale: a function, class, `engine/<module>` library or a
  slice across several. _Avoid_: unit, component, service.
- **Interface**: everything a caller must know to use the module correctly: the declarations, and also invariants, ordering
  constraints, threading rules (which job phase may call it), error modes, required configuration, determinism guarantees and
  performance characteristics. _Avoid_: API, signature (they name only the type-level surface).
- **Implementation**: the code inside a module. Distinct from **adapter**: a small adapter can have a large implementation, and a
  large adapter a small one. Say "adapter" when the seam is the topic, "implementation" otherwise.
- **Depth**: leverage at the interface: how much behaviour a caller (or test) can exercise per unit of interface they must learn.
  **Deep**: much behaviour behind a small interface. **Shallow**: the interface is nearly as complex as the implementation.
- **Seam** (Michael Feathers): a place where behaviour can be altered without editing in that place; the location where a module's
  interface lives. Where the seam goes is its own decision. _Avoid_: boundary.
- **Adapter**: a concrete thing that satisfies an interface at a seam (its role, not its contents).
- **Leverage**: what callers get from depth: more capability per unit of interface; one implementation pays back across N call
  sites and M tests.
- **Locality**: what maintainers get from depth: change, bugs, knowledge and verification concentrate in one place.

## Deep vs shallow

```
Deep module                          Shallow module (avoid)
┌─────────────────────┐              ┌─────────────────────────────────┐
│   Small interface   │              │        Large interface          │
├─────────────────────┤              ├─────────────────────────────────┤
│                     │              │  Thin implementation            │
│ Deep implementation │              └─────────────────────────────────┘
│                     │
└─────────────────────┘
```

When designing an interface ask: can I reduce the number of functions, simplify the parameters, hide more inside?

## Principles

- **Depth is a property of the interface, not the implementation.** A deep module may be built from small parts internally; they
  just aren't part of its interface. It can have **internal seams** (private, used by its own tests) as well as the external seam.
- **The deletion test.** Imagine deleting the module. If complexity vanishes, it was a pass-through. If complexity reappears across
  N callers, it was earning its keep.
- **The interface is the test surface.** Callers and tests cross the same seam. Wanting to test *past* the interface means the
  module is probably the wrong shape.
- **One adapter means a hypothetical seam; two adapters mean a real one.** Don't add a seam unless something varies across it
  (here, typically: real game data vs a test fixture, Vulkan vs a headless stub, worker count 0 vs N).

## Designing for testability

1. **Accept dependencies, don't create them.**
   ```cpp
   // Testable: the caller passes the file system and the data it needs
   LoadReport load_units(const vfs::FileSystem& fs, std::span<const std::string> files);
   // Hard to test: reaches for a global
   LoadReport load_units() { auto& fs = vfs::global(); /* ... */ }
   ```
2. **Return results, don't produce side effects.**
   ```cpp
   DamageResult compute_damage(const Attack& a, const Target& t);  // testable
   void apply_damage(Attack& a, Target& t);                          // harder: the effect is the result
   ```
   In sim code this also keeps parallel jobs writing per-index outputs that a later step reduces in index order (rule 3).
3. **Small surface area.** Fewer functions mean fewer tests; fewer parameters mean simpler setup.

## Relationships

A module has exactly one interface. Depth is measured against that interface. A seam is where the interface lives. An adapter
sits at a seam and satisfies the interface. Depth produces leverage for callers and locality for maintainers.

## Rejected framings

- **Depth as a ratio of implementation lines to interface lines**: rewards padding. Use depth as leverage.
- **"Interface" as only a class's public functions or a header**: too narrow; it includes every fact a caller must know.
- **"Boundary"**: overloaded. Say seam or interface.

## Going deeper

- Deepening a cluster given its dependencies: [DEEPENING.md](DEEPENING.md).
- Exploring several radically different interfaces with parallel sub-agents: [DESIGN-IT-TWICE.md](DESIGN-IT-TWICE.md).
