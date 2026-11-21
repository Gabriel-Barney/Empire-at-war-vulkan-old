#!/usr/bin/env bash
# Human-in-the-loop reproduction loop (from mattpocock/skills, MIT: .claude/skills/LICENSE-mattpocock-skills.txt).
# Last resort for a bug only a person can trigger or see, e.g. something in the port's alamo_game.exe, which agents never drive
# (CLAUDE.md rule 7): Gabriel follows the prompts, the agent reads the captured values.
# Copy it to a scratch folder (never commit the copy), edit the steps, and ask Gabriel to run it in a terminal (Git Bash):
#   bash hitl-loop.sh
#
#   step "<instruction>"        show an instruction, wait for Enter
#   capture VAR "<question>"    show a question, read the answer into VAR
# At the end the captured values print as KEY=VALUE for the agent to parse. Capture observations only; never ask for passwords.

set -euo pipefail

step() {
  printf '\n>>> %s\n' "$1"
  read -r -p "    [Enter when done] " _
}

capture() {
  local var="$1" question="$2" answer
  printf '\n>>> %s\n' "$question"
  read -r -p "    > " answer
  printf -v "$var" '%s' "$answer"
}

# --- edit below ---------------------------------------------------------

step "Start alamo_game.exe from the test build and load the skirmish save named in the task."

capture REPRODUCED "Order the selected squad to retreat. Did it freeze in place? (y/n)"

capture LOG_LINE "Paste the last line of alamo_game_log.txt (or 'none'):"

# --- edit above ---------------------------------------------------------

printf '\n--- Captured ---\n'
printf 'REPRODUCED=%s\n' "$REPRODUCED"
printf 'LOG_LINE=%s\n' "$LOG_LINE"
