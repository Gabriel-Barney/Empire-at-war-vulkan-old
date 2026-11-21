---
name: tdd
description: Test-driven development in this engine - red then green, one vertical slice at a time, tests at agreed seams through the module's interface, expected values from an independent source (a docs/re spec, a retail probe, a worked example). Use when building a feature or fixing a bug test-first, when the user mentions red-green-refactor or TDD, when a task's acceptance is a new test, or when writing the regression test in diagnosing-bugs phase 5.
---

# Test-driven development

TDD is the red → green loop. This skill is what makes the loop produce tests worth keeping: what a good test is, where tests go,
the anti-patterns and the rules of the loop. Consult every section before and during the loop, not after. Adapted from
`mattpocock/skills` (MIT, `.claude/skills/LICENSE-mattpocock-skills.txt`) to this C++20 engine.

Test names and vocabulary follow the project's own terms (`docs/PHASE4.md` step map, `docs/re/` specs, XML / Lua names); respect
`docs/DECISIONS.md` in the area you touch.

## What a good test is

Tests check behaviour through the module's interface, not implementation details. The code can change entirely; the test should
not. A good test reads like a specification ("a bribed unit switches owner and keeps its health") and survives refactors because
it does not care about internal structure. Examples: [tests.md](tests.md). Stand-ins and fakes: [mocking.md](mocking.md).

## Seams: where tests go

A **seam** is the interface you test at, where behaviour is observed without reaching inside. Tests live at seams, never against
internals.

**Test only at agreed seams.** Before writing a test, write down the seams under test, each with one line on what it catches and
what it misses. With a user present, confirm them ("what is the interface, and which seams should we test?"). In a lane the
group's tasks and `exit` test are the agreement: state the seams you chose in your plan and the commit message. You cannot test
everything; agreeing the seams first puts the effort on critical paths and hard logic, not every edge case.

When the interface itself is in question (how deep the module is, where the seam belongs), use the `codebase-design` skill's
vocabulary; it is a reference, not a session to run.

## Anti-patterns

- **Implementation-coupled**: tests a private function, reaches into internal state, or verifies through a side channel (reading a
  save file's bytes instead of loading it through the interface). The tell: the test breaks on a refactor while behaviour is unchanged.
- **Tautological**: the expected value is recomputed the way the code computes it (`CHECK(damage(a, t) == a.power * t.armor_factor)`),
  so it can never disagree with the code. Expected values come from an independent source: a known-good literal, a worked example,
  the `docs/re/` spec, a retail probe result. For parity work that source is retail, by tier: tier 1 exact values, tier 2 a
  distribution over many seeds (the parity harness), never the port's own output.
- **Diff as oracle for new behaviour**: declaring a moved scenario and calling it the test. The behaviour check catches change;
  it does not say the behaviour is right. New behaviour needs a test with an independent expected value (`behaviour-check`).
- **Horizontal slicing**: writing all tests first, then all code. Bulk tests check imagined behaviour and lock in structure before
  you understand it. Work in **vertical slices**: one test, one implementation, repeat; each test a **tracer bullet** that responds
  to what the last cycle taught you.

## Rules of the loop

- **Red before green.** Write the failing test, run it and see it fail for the right reason, then write only enough code to pass.
  No anticipating later tests, no speculative features.
- **One slice at a time.** One seam, one test, one minimal implementation per cycle.
- **Fast loop.** Build and run only what you changed: `& .\build_agent.cmd --% msvc wt "<modules>" changed`, or a test regex as the
  fourth argument. The whole fast tier runs once before landing.
- **Project test rules (`docs/BUILD.md`).** The test lives in the module's test exe registered with `add_test`; no two ctest
  entries share a fixed `%TEMP%` path; label it `slow` over about 5 s; a long determinism run gets one entry per worker count
  (`alamo_add_worker_count_check`); data-driven tests skip with exit 77 when `ALAMO_FOC_DATA` / `ALAMO_EAW_DATA` is unset (a skip
  proves less). Sim tests use a fixed seed and never read wall-clock time.
- **Refactoring is not part of the loop.** It belongs to review (`spec-review`, `/code-review`), not the red → green cycle.
