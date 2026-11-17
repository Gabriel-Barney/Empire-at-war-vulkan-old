---
name: triage
description: Triage roadmap backlog entries and new findings - file them into a category and box, set severity / tier / kind, check whether they are already done or were rejected before, verify the claim, and turn the ones Gabriel picks into agent-ready group tasks. Use when asked to triage, sort or clean up the backlog or the inbox, when lint reports sev-in-backlog or inbox entries, when Gabriel asks "what needs my attention" or "what's ready", or when a review, probe or test run produced findings to file.
argument-hint: [backlog id | "inbox" | free-text request]
---

# Triage

Move findings through the roadmap backlog's states (`docs/ROADMAP_PROTOCOL.md`, "Backlog and group exit tests"). Adapted from
`mattpocock/skills` (MIT, `.claude/skills/LICENSE-mattpocock-skills.txt`): there it triages GitHub issues; here the issue tracker
is `roadmap-store/` and every change goes through `python roadmap-store/roadmap_store.py` (never edit its data by hand). Sync first:
`python roadmap-store/roadmap_store.py sync`.

## States

| Triage state | Roadmap form |
|---|---|
| needs triage | open entry in category `inbox` (no rule matched) |
| triaged | open S3 entry in its category and box, with `kind` and (for parity) `tier` |
| needs info | open entry whose text ends with the open question; `kind re` / `probe` when the binary or the game must answer (`re-question`, `probe-kit`) |
| ready for an agent | a task of an open group with an exit test (`backlog promote <id> --to <group>`, or `--new` with `--exit`) |
| ready for Gabriel | `--g` (only Gabriel can do it: a judgement call, his machine, his look at the game) |
| won't fix | `backlog drop <id> --why "..."` |

Severity: `S1` crash, hang, save corruption or a tier-1 difference a player or modder sees; `S2` a tier-2 metric fails or a rule
differs; `S3` polish or a documented guess. **The backlog holds only S3**: an S1 / S2 finding is a task of the open group that owns
the work (`backlog add ... --sev S1|S2 --to <group>`; none fits or all are full: a new group with an exit test via `write`).
Current focus: tier 3 is paused, so a tier-3 finding is S3 at most, or dropped.

## Who decides what

- You may, without asking: file and re-file entries (`backlog move`, `backlog retag --apply`), set `sev` / `kind` / `tier`, add
  findings, move an S1 / S2 entry into its group (`backlog promote <id> --to <group>`: lint's `sev-in-backlog` asks for exactly
  this), and drop an entry that is already done (cite the commit or code) or a duplicate (cite the other id).
- Gabriel decides: promoting an S3 entry into work, dropping for any other reason, and anything reopening a `docs/DECISIONS.md`
  row. Recommend, then wait. In an unattended run, leave these as a list in your report.

## Show what needs attention

`backlog list --cat inbox` (untriaged), `roadmap_store.py lint` (`sev-in-backlog`, entries without a box), and `backlog list
--summary`. Present three buckets, oldest first: untriaged, S1 / S2 still in the backlog, and needs-info entries whose question has
since been answered (a new `docs/re/` spec or probe result). Counts plus one line each; let Gabriel pick.

## Triage one entry or finding

1. **Gather context.** Read the entry (`backlog list --full`), the group it came from (`roadmap-store/data/items/<group>.json`),
   its `ref`, and the code and `docs/` pages it names. Two checks:
   - **Already done?** Search the code and `git log` for the behaviour by concept, not just the entry's wording; say where you looked.
     Done: drop with the commit / file as the reason.
   - **Rejected before?** `docs/DECISIONS.md` (out-of-scope work is recorded there first) and dropped entries (`backlog list --all`).
     Surface any match: "this resembles <row / bNNNN>, rejected because ...".
2. **Recommend** category, box, severity, tier and kind, with reasons and a short summary of the relevant code. For anything Gabriel
   decides, wait.
3. **Verify the claim** before anything else is written about it. A port bug: reproduce it (`diagnosing-bugs` phase 1 loop). A
   retail-behaviour claim: check `docs/re/`, then `re-question`. Report confirmed (with the code path), not reproduced, or not enough
   detail (needs info).
4. **Sharpen if needed.** Unclear entries get one round of specific questions at a time (`grill-with-docs` for a bigger one) (to Gabriel, or to the `docs/re/` spec via
   `re-question`); vague "please clarify" questions don't count.
5. **Apply the outcome** with the matching `roadmap_store.py backlog` command. Promoting into a group: the task text is an agent
   brief ([AGENT-BRIEF.md](AGENT-BRIEF.md)); a new group gets an `exit` test that the brief's acceptance criteria add up to.
   Rejected for a reason a future triage would need: offer a `docs/DECISIONS.md` row so it is not re-filed.

## Quick override

When Gabriel says "promote b0412 to skai" or "drop b0300", trust it: say what you will run, run it, report. If he promotes without
a brief, ask whether he wants the task text rewritten as one.

## Stage 6 and Stage 9 groups

Stage 6 · Parity and mods (boxes MODC, Q4, Q5, Q6) and Stage 9 · Mod compatibility system (boxes MCX1-MCX6, since 2026-11-17)
keep their follow-ups: `backlog add ... --from <stage-6 or stage-9 group>` makes the finding
a task of that group instead of a backlog entry (tier 3 still goes to the backlog). Triage there means writing the task as a brief.
