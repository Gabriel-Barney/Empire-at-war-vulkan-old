---
name: plan
description: Write the one-page plan of a roadmap group before any code - behaviour source, seam, tests first, predicted behaviour impact, check tier, out of scope - with every binary question and retail probe answered first, and get it approved (alone or in a batch). Use as step 2 of the work skill, when asked to plan a group or several groups, or when Gabriel wants to approve plans and walk away (wr5 t01-t03).
argument-hint: <group-id> [<group-id> ...]
---
Plan roadmap group(s) `$ARGUMENTS`. A plan is at most one page; it is stored on the group (`items/<id>` field `plan`) and
approved before implementation starts. This skill drives the existing ones; it does not repeat them.

1. **Draft in plan mode** (the Plan agent may draft). Fill the format below from the group's tasks, exit test and the
   `docs/re/` pages and game data they rest on. Done when: every field has content or "none".
2. **Questions first** (wr5 t02). For each open question in "Behaviour source": what the binary answers goes to `re-question`
   (the `re-analyst` writes `docs/re/`); what only the running game shows gets a `probe-kit` that this session runs on retail;
   a technique or format question goes to `research`. Done when: every answer is written in `docs/re/` (or the research
   report) and cited in the plan, or the question is a named assumption with its `[port]` choice recorded in `docs/PHASE4.md`.
   No binary question or probe run is left for after approval; an exception found later is listed in the group's log with why
   it could not be foreseen.
3. **Shape it** with the skills that apply, and name in "Skills used" each one used and why the others were not needed:
   `codebase-design` for where the seam goes; `tdd` for the tests to write first and where their expected values come from;
   `prototype` when a rule, state model or screen has to be felt out; `grill-with-docs` for decisions only Gabriel can make
   (recorded as that skill does); `to-tickets` when the work is too big for one group (split into groups with exit tests);
   `triage` when the plan starts from backlog entries.
4. **Store and ask.** Write the plan to the group: `update items/<id> {"plan": {"text": "<the plan>", "by": "<session>",
   "at": "<date>"}}` through `roadmap_store.py write`. Show it to Gabriel. Batch (wr5 t03): several groups' plans are shown
   together; Gabriel approves, changes or rejects each in one sitting, then walks away; decisions he makes are recorded as
   `grill-with-docs` does. Done when: each approved plan carries `"approved": "<date> Gabriel"`. Approved plans are worked in
   order (the `work` skill) without further questions; a plan that proves wrong during the work stops that group only, and
   the report says what differed.

## Plan format

```
Group <id> - <title>                                     writer: sim | non-sim
Behaviour source: docs/re pages, game data files, retail runs it rests on; open questions -> answered where / assumption
Seam: the module, interface or function where the change goes, and why there
Tests first: each test, what it asserts, where its expected value comes from (docs/re, retail probe, worked example)
Predicted behaviour impact: scenarios and CRC system columns that should move (the Behaviour: trailer), or "none"
Check tier: landing (default) | landing + clang | + ASan | + nightly slow tests named
Out of scope: what this group will not do (and where it goes instead)
Skills used: which planning skills ran and why the others were not needed
```
