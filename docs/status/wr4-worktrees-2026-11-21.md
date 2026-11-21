# Worktrees and local branches before the two-writer switch (wr4 t06, listed 2026-11-21)

Nothing has been removed. Gabriel confirms the removal list; unique work is bundled (`git bundle`) or landed first, then each worktree goes with `tools\clean_worktrees.cmd --apply --only <name>` (junctions unlinked first). Keep: the main checkout, `verifier`, `gate-dev`, `scenario-base` and the two writer checkouts.

## Worktrees (tools\clean_worktrees.cmd dry run)

```
worktree                            branch                                      merged  dirty  unpushed  age d  build GB  decision
Star wars empire at war port        dev                                         yes     2      0         0.1    -         keep: main checkout; uncommitted changes; active 0.0d ago (< --days)
agent-a06f5a21d42b828c0             worktree-agent-a06f5a21d42b828c0            no      no     6         19.3   -         keep: not merged into origin/dev or origin/main; unpushed commits
agent-a39fa9a7e59ff2966             worktree-agent-a39fa9a7e59ff2966            no      no     1         12.4   -         keep: not merged into origin/dev or origin/main; unpushed commits
agent-a70cf7ddffe9ea927             worktree-agent-a70cf7ddffe9ea927            no      no     5         19.2   -         keep: not merged into origin/dev or origin/main; unpushed commits
agent-a713baa85d6fc2a89             worktree-agent-a713baa85d6fc2a89            yes     2      0         15.3   -         keep: uncommitted changes
agent-a8f3cbad1267b9588             claude/a2k-laneRE2                          no      no     2         14.6   -         keep: not merged into origin/dev or origin/main; unpushed commits
agent-aa2907a3598153201             worktree-agent-aa2907a3598153201            yes     2      0         15.3   -         keep: uncommitted changes
agent-ac4ac50c8e3e1cfbf             worktree-agent-ac4ac50c8e3e1cfbf            no      no     7         19.3   -         keep: not merged into origin/dev or origin/main; unpushed commits
agent-ac678f16681b77376             worktree-agent-ac678f16681b77376            no      13     5         19.9   -         keep: not merged into origin/dev or origin/main; uncommitted changes; unpushed commits
agent-ad28a749c1193250b             worktree-agent-ad28a749c1193250b            no      no     1         20.1   -         keep: not merged into origin/dev or origin/main; unpushed commits
agent-ad2ca3d4b9c471c79             worktree-agent-ad2ca3d4b9c471c79            no      no     5         19.2   -         keep: not merged into origin/dev or origin/main; unpushed commits
agent-afc5ac066f374587a             worktree-agent-afc5ac066f374587a            yes     5      0         15.3   -         keep: uncommitted changes
b0173-mdu-destination               claude/b0173-mdu-destination                yes     3      0         6.4    -         keep: uncommitted changes
determined-mahavira-4aca2c          claude/project-codebase-audit-fa27da        no      4      3         19.8   -         keep: not merged into origin/dev or origin/main; uncommitted changes; unpushed commits
gate-dev                            (detached)                                  yes     no     0         0.1    7.21 (3)  keep: locked; active 0.1d ago (< --days)
mystifying-hopper-6e7216            claude/cpu-performance-improvements-30dc7e  yes     1      0         7.1    -         keep: uncommitted changes
project-completion-timeline-6c2bcc  claude/game-exe-packaging-60eb38            yes     5      0         7.4    -         keep: uncommitted changes
project-skills-subagents-54e236     claude/memory-performance-usage-3d4947      yes     1      0         7.1    -         keep: uncommitted changes
project-timeline-update-803c2f      claude/update-github-readme-5878a8          yes     1      0         3.3    -         keep: uncommitted changes
ref1-tasks-765eb6                   claude/wr2-landing                          no      1      2         0.0    4.35 (3)  keep: current worktree; not merged into origin/dev or origin/main; uncommitted changes; unpushed commits; active 0.0d ago (< --days)
scenario-base                       (detached)                                  yes     no     0         0.1    1.47 (1)  keep: locked; active 0.0d ago (< --days)
skill-usage-summary-0d7309          (detached)                                  yes     no     0         0.3    3.44 (2)  keep: active 0.1d ago (< --days)
star-wars-empire-icon-38cac8        claude/star-wars-empire-icon-38cac8         yes     1      0         7.4    -         keep: uncommitted changes
tst2-c-ed478b                       (detached)                                  yes     no     0         0.3    2.62 (2)  keep: active 0.1d ago (< --days)
verifier                            (detached)                                  no      no     1         0.0    4.07 (3)  keep: locked; not merged into origin/dev or origin/main; unpushed commits; active 0.0d ago (< --days)

25 worktrees, 0 removable (merged, clean, nothing unpushed, idle >= 2 d), their build trees 0.0 GB
DRY RUN: nothing to remove.
```

## Local branches

```
claude/a2k-laneRE2 unmerged (2 commits not on dev)
claude/a2m-diag-rebel-r1 unmerged (1 commits not on dev)
claude/a2m-diag-rebel-r2 unmerged (2 commits not on dev)
claude/a2m-diag-rebel-r3 unmerged (3 commits not on dev)
claude/a2m-diag-rebel-r3b unmerged (5 commits not on dev)
claude/a2m-diag-underworld-r2 unmerged (1 commits not on dev)
claude/aar-loop-repo-eval-68f8f8 merged
claude/abilities-tasks-0a5e8b merged
claude/adoring-khayyam-289cd5 merged
claude/agents-model-effort-settings-242301 merged
claude/agents-playtest-qa-demo-fog-890786 unmerged (5 commits not on dev)
claude/antialiasing-menu-options-92f71c unmerged (1 commits not on dev)
claude/aud1-tasks-38f4dc merged
claude/audio-project-audit-da0a64 merged
claude/b0173-mdu-destination merged
claude/b0173-mdu-wip unmerged (1 commits not on dev)
claude/backlog-parallel-agents-d5391d unmerged (1 commits not on dev)
claude/backlog-roadmap-s1-s2-c1bd36 merged
claude/backlogged-tasks-28db7b merged
claude/bk-track-d-a87356 unmerged (3 commits not on dev)
claude/bm4-parallel-agents-3d6411 unmerged (2 commits not on dev)
claude/bo1-build-speed-26b9cd merged
claude/bo2-test-exes-eb60cf merged
claude/bo3-header-diet-d303a6 unmerged (1 commits not on dev)
claude/build-optimization-strategies-a26a53 merged
claude/camera-control-rework-0ec18e unmerged (1 commits not on dev)
claude/cmb3-d74908 merged
claude/code-optimization-refactoring-ce225b unmerged (1 commits not on dev)
claude/code-refactoring-benefits-90802b merged
claude/codebase-architecture-refactor-913b09 merged
claude/codebase-quality-refactoring-77096b merged
claude/coordinator-worker-model-a13dc7 merged
claude/cpu-instructions-performance-286361 merged
claude/cpu-performance-improvements-30dc7e merged
claude/cpu-performance-research-e02852 unmerged (3 commits not on dev)
claude/current-project-tools-455738 merged
claude/default-workflow-auto-tools-3835c3 merged
claude/default-workflow-auto-tools-56f12f merged
claude/default-workflow-automation-notes-77767d merged
claude/dev-tools-research-16ea4d merged
claude/devcheck-tac2 merged
claude/distracted-bose-270463 merged
claude/empire-war-command-center-d24fa7 merged
claude/engine-tools-launcher-audit-49c25c merged
claude/first-person-ground-unit-fb32d4 unmerged (2 commits not on dev)
claude/foc-underworld-mdu-destination-6eb8bc merged
claude/fowf-t01-map-extents unmerged (1 commits not on dev)
claude/frame-time-worker-scaling-4ebbc2 merged
claude/gabriel-probe-automation-846cd2 merged
claude/gabriel-probe-automation-bc380e merged
claude/gai3-29b6ca merged
claude/gai3-lane2-6fa03a merged
claude/gallant-ellis-698645 merged
claude/game-exe-packaging-60eb38 merged
claude/game-refactor-assessment-3ee091 merged
claude/github-branch-cleanup-a8a370 merged
claude/github-projects-readme-274139 merged
claude/github-readme-audit-0bea44 merged
claude/github-readme-audit-967c6f merged
claude/graphify-tool-evaluation-5a87f7 merged
claude/groups-mcx4-mcx5-tasks-e2e448 merged
claude/groups-mcx7-mcx8-mcx9-6750f7 merged
claude/hbao-gtao-ssao-comparison-656071 merged
claude/header-diet-removal-decision-172b9a merged
claude/improve-codebase-architecture-model-278919 merged
claude/in-game-benchmark-foc-455bb1 merged
claude/lane-aud2-aaa9d9 merged
claude/lane-cmb-fee4ec merged
claude/lane-galab-ce5b5e merged
claude/lane-map1-3b4b81 merged
claude/lane-modc-627318 merged
claude/lane-mov-2d848f merged
claude/lane-r3g-080f71 merged
claude/lane-skm1-e1a531 merged
claude/lane-skm2-79ff9f merged
claude/lane-tst1-da9ec1 merged
claude/lane-ui2-568556 merged
claude/lane-ui4-2cdebc merged
claude/lane-ui5-034972 merged
claude/launcher-save-export unmerged (1 commits not on dev)
claude/map-tasks-roadmap-group-fed619 merged
claude/mcx0-173efc merged
claude/mcx1-a193b5 unmerged (1 commits not on dev)
claude/mcx2-6bc3e1 merged
claude/mcx3-6bc3e1 merged
claude/mcx4-gal-cf0bc3 merged
claude/mcx4-gal2-247b16 merged
claude/mcx4-pres-b5cef7 merged
claude/mcx4-tac-e5daf3 merged
claude/mcx4-tac2-42de6a unmerged (2 commits not on dev)
claude/mcx5-be2c66 merged
claude/mcx6-173efc merged
claude/mcx7-6a8db8 merged
claude/mcx8-a-a35a4d merged
claude/mcx8-corpus-5796ac merged
claude/memory-performance-usage-3d4947 merged
claude/mod-compatibility-system-0827f4 merged
claude/mod-compatibility-system-5b808a merged
claude/mod-compatibility-system-e08529 merged
claude/mod-system-audit-e8e879 merged
claude/mod-system-roadmap-cleanup-6f1850 merged
claude/mov2-ab3ff6 merged
claude/mov2-t08-wip merged
claude/movement-combat-tasks-0e1be1 merged
claude/movement-combat-tasks-2479dd merged
claude/new-test-build-a22e3e merged
claude/new-testbuild-package-415eb2 unmerged (1 commits not on dev)
claude/opt2-gal-perception-prototype unmerged (1 commits not on dev)
claude/opus-sonnet-workflow-3ce91d merged
claude/parallel-agent-workflow-b70f7f merged
claude/parallel-agents-a2k-3bcdda unmerged (4 commits not on dev)
claude/parallel-lanes-nested-sessions-b33191 merged
claude/performance-limitations-research-ef2606 merged
claude/playtest-video-qa-audit-ed3331 unmerged (1 commits not on dev)
claude/port-performance-comparison-da2fd2 merged
claude/probe-gabriel-automation-60f4b5 unmerged (18 commits not on dev)
claude/project-api-value-ffcd80 merged
claude/project-code-lines-d2d70f merged
claude/project-codebase-audit-fa27da unmerged (3 commits not on dev)
claude/project-disk-audit-b66e86 merged
claude/project-folder-audit-clean-bf332b merged
claude/project-loc-update-9e298b merged
claude/project-optimization-plan-dffd25 merged
claude/project-review-e2e-f17a06 unmerged (3 commits not on dev)
claude/project-skills-subagents-54e236 merged
claude/project-timeline-224b7b merged
claude/project-timeline-93f96b merged
claude/project-timeline-roadmap-29ccd5 unmerged (1 commits not on dev)
claude/project-timeline-status-023d0d merged
claude/project-timeline-update-803c2f merged
claude/project-timeline-workflow-57a64c merged
claude/project-toolchain-tools-list-4b5213 merged
claude/prompt-effectiveness-open-tasks-71c12f merged
claude/prompt-task-prioritization-ae2053 merged
claude/prototype-roadmap-page unmerged (1 commits not on dev)
claude/q2-land-skirmish-ai-3c5b96 merged
claude/q4d-skai-27a2ab merged
claude/q5-q6-modc-roadmap-00ec2f merged
claude/q6-c02c95 merged
claude/q6-probes-ac5272 merged
claude/quickest-roadmap-tasks-975998 merged
claude/r3-animation-followups-bb4d80 unmerged (19 commits not on dev)
claude/r3-animations-task-group-789284 unmerged (3 commits not on dev)
claude/rea-library-evaluation-e023c8 merged
claude/rea-reverse-engineering-eval-7f920c merged
claude/readme-side-goals-removal-835d09 merged
claude/reduce-dev-builds-79d5cb merged
claude/ref1-a-48ed99 merged
claude/ref1-b-b9e7e8 merged
claude/ref1-c-81d4d5 merged
claude/ref1-tasks-765eb6 merged
claude/ref2-a-6a8884 merged
claude/ref2-b-87fcc4 merged
claude/ref2-c-f61d46 merged
claude/ref2-tasks-cd9ad4 merged
claude/ref3-a-aad4d2 merged
claude/ref3-b-e02c97 merged
claude/ref3-c-ba65e0 merged
claude/ref3-tools-wide-interfaces-81041c merged
claude/ref3-tools-wide-interfaces-b3a032 merged
claude/ref3c-base merged
claude/ref4-a-372255 merged
claude/ref4-b-707d43 merged
claude/ref4-c-6c7b00 merged
claude/ref4-c-t03 merged
claude/ref4-save-state-frame-math-7a5274 merged
claude/ref4-save-state-frame-math-98dbb6 merged
claude/ref5-t07-facts merged
claude/ref5-t07-facts-exp unmerged (1 commits not on dev)
claude/ref5-t09 merged
claude/refactoring-benefits-4c6246 merged
claude/refactoring-code-health-impact-be66be merged
claude/refactoring-quality-improvements-3a79ec merged
claude/remove-multiplayer-buttons-9f7a03 merged
claude/reva-vs-rea-choice-11823f merged
claude/reverse-engineering-tools-c7f5d2 merged
claude/roadmap-audit-cleanup-0b3c61 merged
claude/roadmap-audit-restructure-08a8b0 merged
claude/roadmap-group-labels-16be00 merged
claude/roadmap-interactive-redesign-238d4c merged
claude/roadmap-most-important-run-04952e merged
claude/roadmap-refactor-alignment-6a9465 merged
claude/roadmap-restructure-2cb0d6 merged
claude/roadmap-task-priority-39cce2 merged
claude/roadmap-tasks-audit-1135af merged
claude/roadmap-tasks-audit-fb6ae7 merged
claude/run-group-roadmap-50cf9f merged
claude/rust-rewrite-feasibility-950099 merged
claude/sim-queue-4bd9e2 merged
claude/sim-system-redesign-201cea merged
claude/simp-ai-4f merged
claude/simp-ai-f731ef unmerged (7 commits not on dev)
claude/simp-core-067e57 merged
claude/simp-performance-projection-d9edbe merged
claude/simp-svc-e072e0 merged
claude/skai-a-2c5f0f merged
claude/skai-b-80f49f merged
claude/skai-lane2-f61bcd merged
claude/skill-usage-summary-0d7309 merged
claude/skills-default-workflow-abf9fe merged
claude/skills-sh-research-8459f7 merged
claude/skm2-devcheck merged
claude/sonnet-5-5-s3-backlog-b3addc merged
claude/sp4a-base merged
claude/sp6-devcheck merged
claude/star-wars-empire-icon-38cac8 merged
claude/sty1-a merged
claude/sty1-b merged
claude/sty1-c merged
claude/sty1-tasks-751989 merged
claude/sweet-curran-622bf0 merged
claude/tai1-b-9f22ba merged
claude/task-groups-roadmap-presets-150015 merged
claude/task-prioritization-prompt-89376c merged
claude/test-build-packaging-8c1983 unmerged (1 commits not on dev)
claude/token-optimization-research-d4f9c5 merged
claude/trace-parity-gaps-automation-de61a4 merged
claude/trn1-a8b95e merged
claude/tru1-a-831e47 merged
claude/tru1-a-dbce65 merged
claude/tru1-b-487a02 merged
claude/tru1-b-5faec2 merged
claude/tru1-c-9e92d9 merged
claude/tru1-c-cc16c7 merged
claude/tru1-tasks-26c221 merged
claude/tru1-tasks-completion-a9c602 merged
claude/tst2-a-6d3484 merged
claude/tst2-b-b31ff1 merged
claude/tst2-c-ed478b merged
claude/tst2-tasks-d94a9f merged
claude/tst2-tasks-necessity-fbe4af merged
claude/ums0-eaf2ec merged
claude/ums1-7f6bc1 merged
claude/ums1-mod-env-refactor-919619 merged
claude/ums1-mod-environment-refactor-7538f4 merged
claude/ums2-before merged
claude/ums3-7f6bc1 merged
claude/ums4-4dba08 merged
claude/ums4-base merged
claude/underworld-seeds-task-failure-9aa417 merged
claude/unified-mod-system-design-8df298 merged
claude/update-github-readme-5878a8 merged
claude/update-github-readme-c4bb8a unmerged (1 commits not on dev)
claude/update-github-readme-f20827 merged
claude/update-projects-readme-934235 merged
claude/vis1-802514 merged
claude/worker-subagent-skill-access-dce02c merged
claude/workflow-dev-speed-527c20 merged
claude/worktree-performance-comparison-30f896 merged
claude/worktree-planning-workflow-perf-dd1c69 merged
claude/wr1-goldens merged
claude/wr2-landing unmerged (2 commits not on dev)
dev merged
main merged
ref5-ai2 merged
simp-core-sp1-wip merged
simp-core-sp2-wip merged
worktree-agent-a06f5a21d42b828c0 unmerged (6 commits not on dev)
worktree-agent-a2dc3d2c35cef35ea merged
worktree-agent-a39fa9a7e59ff2966 unmerged (1 commits not on dev)
worktree-agent-a70cf7ddffe9ea927 unmerged (5 commits not on dev)
worktree-agent-a713baa85d6fc2a89 merged
worktree-agent-a77eda01deae4b90e merged
worktree-agent-a7872681f68a4f242 merged
worktree-agent-a85011d20bd60a4f5 merged
worktree-agent-a87de9713e63c333c merged
worktree-agent-aa2907a3598153201 merged
worktree-agent-ac4ac50c8e3e1cfbf unmerged (7 commits not on dev)
worktree-agent-ac678f16681b77376 unmerged (5 commits not on dev)
worktree-agent-ad28a749c1193250b unmerged (1 commits not on dev)
worktree-agent-ad2ca3d4b9c471c79 unmerged (5 commits not on dev)
worktree-agent-af05d3e0dca2163ba merged
worktree-agent-afa444f2a16fbe99f merged
worktree-agent-afc5ac066f374587a merged
```
