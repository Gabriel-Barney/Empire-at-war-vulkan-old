# STY1 lane C: questions for the re-analyst

Answer each in words in `docs/re/STY1_GENERIC_AUTOSAVE.md` (behaviour only; no decompiled code). Mark [H]/[M]/[L] confidence.

## Q1. STORY_GENERIC raisers (function 0x32edd0, "notify by name")
List every caller of 0x32edd0 (the string notification helper) that passes a literal name, with: the literal, the UI/sim action
that triggers it (button, drag start, dialog close, zoom end, hyperspace start, hero neutralised, bombard start, super laser, ...),
whether it fires on press or release / only for the local human player, and the mode (galactic / tactical / both).
Names we need: click, right_click, start_fleet_drag, start_land_drag, drag_select, drag_land_to_space, land_to_space,
Continue_Tutorial, CLOSE_STORY_DIALOG, retreat_clicked, retreat_complete, corruption_dialog, open_black_market_menu,
open_galactic_sabotage_menu, slice, slice_dialog, slice_slot, merge_fleets, zoomed_in, zoomed_out, start_hyperspace,
hero_neutralized, activate_deployed_ability, start_bombard, super_laser_fired. For each: exact trigger condition.

## Q2. Mission-start autosave
When does retail write AutoSave.rec (strings TEXT_GUI_AUTO_SAVE, TEXT_LOAD_AUTOSAVE)? Which campaigns / modes (story
campaigns only? skirmish? every galactic turn?), at which moment (after the mission's first tick / on mode start / on
story-mission start), the file name and folder, overwrite rule, and what Retry (TEXT_LOAD_AUTOSAVE dialog after a
mission-lost / game-over) loads. Also: the Load list row for it (label text key, ordering, whether it can be deleted).
