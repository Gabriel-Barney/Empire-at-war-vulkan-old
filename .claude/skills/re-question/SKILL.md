---
name: re-question
description: Get an answer about how retail StarWarsG.exe behaves by having the re-analyst agent read the binary (Ghidra through ReVa) and write the behaviour into docs/re/. Use when a task depends on original-game behaviour that docs/re/ and the game's XML / Lua do not settle - a formula, constant, order of steps, which branch a rule takes, what a Lua or XML field does in the exe - before guessing and before building a probe kit; also when a docs/re spec marks something as a guess that the binary can confirm.
argument-hint: <question>
---
# Asking the binary

Order of sources for "what does retail do": `docs/re/` and `extracted/` (the `research` skill), then the binary (this skill,
minutes), then a probe kit (`probe-kit`, a game run) for what only the running game shows: timings, random outcomes, what is on
screen.

Since 2026-11-15 (Gabriel) any worker may read decompiled output itself: REA (`tools\tc.cmd rea call ...`), `re_reference/`,
cdb on a TTD trace. Use this skill when you want a written, checked spec, or need ReVa (Ghidra's decompiler and cross-references),
which only `re-analyst` has. Either way, decompiled code is never copied into the repo: the engine is written in the port's own
words from the behaviour, and the behaviour goes into `docs/re/` in words.

## Steps

1. **Check it is not answered.** `Grep` `docs/re/` for the function, field or rule name. Done when you can name the spec section
   that is silent or marked as a guess.
2. **Write the question** so one reading answers it: the exact behaviour asked, the port's current guess and where it lives
   (`docs/re/<SPEC>.md` section or the engine file), anchors that find the code (a string the exe prints, an XML tag, a Lua
   function name, an RVA from `docs/re/`), and the spec file the answer goes into. Several related questions go in one request.
3. **Hand it over.**
   - You have the `Agent` tool (a main session): start `re-analyst` with the question. Ghidra locks its project, so run one
     `re-analyst` at a time; a second question waits or joins the first request.
   - You have no `Agent` tool (a lane or module agent started as a subagent): save the question as
     `docs/re/<TASK>_RE_QUESTIONS.md`, carry on with the tasks that do not depend on it, and name the file in your report so the
     coordinating session runs `re-analyst` on it. Meanwhile you may answer it yourself with REA or `re_reference/` and write
     the behaviour into the spec. Do not guess the answer in code.
4. **Read the answer in `docs/re/`**, not in the agent's reply. Done when the spec names the RVAs it rests on and separates what
   was read from what was inferred. An "inferred" line that your task depends on is still open: ask again with a narrower
   question, or settle it with a probe kit.
5. **Implement from the spec.** A difference from retail that you keep is a `[port]` entry (`docs/PHASE4.md`).

A failure to open the Ghidra project means another session holds it: wait, do not re-import. Tool locations and what was
checked: `docs/PROBES.md`, "RE and retail-tracing tools".
