# wr2 t04: make the verifier the only way onto dev (settings edit for Gabriel)

**Applied 2026-11-21** at Gabriel's request (commit "wr2 t04"); `land_on_dev.cmd` now refuses unless `ALAMO_LAND_DIRECT=1`. Kept as the record of the change.

Agents may not widen or change their own permission rules, so this file prepares the edit and Gabriel applies it to
`.claude/settings.json` (`permissions`). Apply it once the verifier task is registered and has landed a few entries
(`python -I tools/gate/land_queue.py status`).

Add to `permissions.deny` (a session's direct push to dev is refused; the verifier runs outside any session, as the
AlamoVerifier task, so its own push is not affected):

```json
"Bash(git push * HEAD:refs/heads/dev*)",
"Bash(git push * HEAD:dev*)",
"Bash(git push origin dev*)",
"PowerShell(git push * HEAD:refs/heads/dev*)",
"PowerShell(git push * HEAD:dev*)",
"PowerShell(git push origin dev*)"
```

Add to `permissions.allow` (submit runs unprompted in auto mode):

```json
"Bash(python -I tools/gate/land_queue.py *)",
"PowerShell(python -I tools/gate/land_queue.py *)",
"PowerShell(python -I tools\\gate\\land_queue.py *)"
```

Remove from `permissions.allow` the four `land_on_dev.cmd` spellings (`PowerShell(cmd /c tools\\land_on_dev.cmd)`,
`PowerShell(cmd /c "tools\\land_on_dev.cmd")`, `PowerShell(.\\tools\\land_on_dev.cmd)`,
`PowerShell(& .\\tools\\land_on_dev.cmd)`).

The same change set retires `tools\land_on_dev.cmd` (wr2 t07 makes it refuse with a pointer to the submit command once
this is applied). `main` stays Gabriel's release (`gh pr create --base main --head dev`), unchanged.

Check after applying (acceptance): a session's `git push origin HEAD:refs/heads/dev` is refused; `python -I
tools/gate/land_queue.py submit` runs without a prompt in auto mode.
