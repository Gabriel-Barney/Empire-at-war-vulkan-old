# Deepening

How to deepen a cluster of shallow modules safely, given its dependencies. Uses the vocabulary in [SKILL.md](SKILL.md).

## Dependency categories

Classify each dependency of a candidate; the category decides how the deepened module is tested across its seam.

1. **In-process.** Pure computation, in-memory state, no I/O (most sim systems: movement, combat rules, AI evaluation). Always
   deepenable: merge the modules and test through the new interface directly. No adapter needed.
2. **Local-substitutable.** Dependencies with a local stand-in: the VFS over a fixture folder instead of the game's MEG archives,
   the headless host instead of a window, worker count 0 instead of the job pool. Deepenable when the stand-in exists; tests run
   with it. The seam is internal, so no port at the module's external interface.
3. **Owned but out of process.** Rare here (the launcher talking to `alamo_game.exe`). Define a port at the seam; the deep module
   owns the logic and the transport is an injected adapter; tests use an in-memory adapter.
4. **True external.** Things the project does not control: the Vulkan driver, the OS, Steam, the retail game data. The deepened
   module takes them through an injected port; tests use a stub adapter, and data-driven tests read `ALAMO_FOC_DATA` /
   `ALAMO_EAW_DATA` and skip when unset (a skip proves less).

## Seam discipline

- **One adapter means a hypothetical seam; two adapters mean a real one.** Don't add a port unless at least two adapters are
  justified (typically production + test). A single-adapter seam is just indirection.
- **Internal vs external seams.** Don't expose an internal seam through the interface just because a test uses it.
- **Sim seams are deterministic seams.** Nothing that crosses a sim seam may depend on wall-clock time, pointer order, thread count
  or an unordered container's iteration order (rule 3).

## Testing strategy: replace, don't layer

- Unit tests on the old shallow modules become waste once tests at the deepened module's interface exist; delete them in the same
  change.
- Write the new tests at the deepened interface, registered with `add_test` in the module's test exe; a long scenario gets one
  ctest entry per worker count (`alamo_add_worker_count_check`) and the `slow` label over about 5 s.
- Tests assert on observable outcomes through the interface, not internal state, so they survive internal refactors. A test that
  has to change when the implementation changes is testing past the interface.
- Goldens are the outer net: a deepening that is meant to keep behaviour moves no golden. A moved golden is a behaviour change to
  explain, never to regenerate away (`regen-goldens` skill).
