---
name: upkeep
description: The weekly upkeep run - workflow statistics of the last 7 days, a retro of the runs that wasted time, and an architecture scan of the areas with the most failed builds or test churn, ending in at most three proposed changes for Gabriel to pick. Use when the weekly scheduled upkeep session starts, or when Gabriel asks for the weekly review (wr5 t05). It changes no code.
---
Weekly upkeep. Read and report only: no code, docs or roadmap edits except the report and the picked items.

1. **Numbers.** `python -I tools/analysis/workflow_stats.py --from <7 days ago> --markdown`, beside the baseline in
   `docs/PERFORMANCE.md` ("Workflow baseline"). Also `python -I tools/lint/flaky_retry.py --report`,
   `python -I tools/lint/test_tiers.py check`, and the nightly lines of `logs\gate_history.txt`. Done when: builds per day,
   failed share by kind, test hours, landing-tier wall time, flaky tests and nightly failures are in a table.
2. **Retro.** From the failed build records of the week (`roadmap-store\data\builds\`, `logs\build_records\`) and the queue
   summaries (`.cache\land-queue\*.summary.txt`), find the three costliest wasted runs (repeat failures, rejected landings,
   golden drift, compile errors the edit hook should have caught). Done when: each has its cause in one line.
3. **Scan.** Run `improve-codebase-architecture` on the one or two modules with the most failed builds or test churn this week
   (from step 1). Done when: its candidates are listed, report only.
4. **Propose.** At most three changes (a deepening, a test to split or move, a rule or instruction to drop), each with the
   evidence from steps 1-3 and its expected effect. Write the report to `docs/status/upkeep-<date>.md` and ask Gabriel to pick.
   Done when: the picked items are on the roadmap (`backlog add` S3 or a group task, as `triage` files them) and the report
   names them.
