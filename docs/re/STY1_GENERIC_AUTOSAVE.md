# STORY_GENERIC raisers and the autosave (StarWarsG.exe FoC x64) - behaviour spec

Answers `STY1_RE_QUESTIONS.md` (STY1 lane C). Written 2026-11-15 by the re-analyst from the decompile export
(`re_reference/ghidra`, searched as text) plus the on-disk save folder of the test machine. Prose and tables only.
Tags: **[H]** read in the decompile, **[M]** inferred from structure or call sites, **[L]** guess. RVAs are image-relative.
ReVa did not connect and REA timed out twice in Ghidra's startup, so nothing here comes from a live Ghidra session; two
instruction byte reads were done directly from the exe file (§1.1).

Builds on: STORY.md (event types, generic matching), HUD_GAPS §9.6 (HUD mouse codes), UI.md §7, MAIN_LOOP §2.5,
BATTLE_TRANSITIONS (game types), BKC1_RE_ANSWERS (retry-dialog flag).

---

## 1. Q1 - STORY_GENERIC raisers (0x32edd0)

### 1.1 The mechanism [H]

- 0x32edd0(dispatcher, name) copies the name (up to 255 characters; a null name does nothing), writes the debug line
  "TUTORIAL - Generic string based trigger - %s", and calls the dispatcher's notify 0x32e5f0 with type **0x21
  (STORY_GENERIC)** and the name as its string parameter. 0x32ed60 is the same with the fixed name
  `Continue_Tutorial`. (GUI clicks use 0x32e9b0 with type 0x1d, CLICK_GUI, and the line "GUI item clicked on %s"; they
  are a different event type and are not covered here.)
- **One dispatcher.** The call sites show `mode + 0x14` or `mode + 0xa0` in the decompile, but this is only the
  decompiler's typing of the "current mode" global (0x140b15418) as a pointer to 8-byte cells or as an integer. Two
  sampled sites (0x30d5d0 "+0x14" and 0x340750 "+0xa0") both encode `add rcx, 0xA0`, so the dispatcher is the member
  at **byte offset 0xA0 of the current game mode** for all of them [H for those two, M for the rest]. There is no
  separate galactic and tactical dispatcher; the mode that is current when the code runs is the one used.
- **Delivery (0x32e5f0)** [H]: nothing happens while the story manager does not exist or while a global "no story"
  byte (0x140b155c4) is set. If the dispatcher belongs to the current mode, and the story manager's enable byte is
  set or the game is multiplayer-type (game type 1, 2, 4) or 0x140b1574e is set, the event is handed at once to every
  listener of type 0x21 that is active (+0x4d), not finished (+0x4c clear), not disabled (+0x4f clear), passes the
  campaign-type gate (0x140b153f8 is 0 or 3, or the listener flag +0x4e), and is bound to this dispatcher's owner.
  If the dispatcher is **not** the current mode's (a stale mode), the event is not dropped: it is queued (a copy with
  the string) and the log says "STORY DELAYED EVENT - This event is being sent to an inactive mode".
- **Matching** is the generic String-family rule of STORY.md §5: each non-empty event parameter is compared with the
  raised name, **case-insensitive equality**, no count. So the XML `CLOSE_STORY_DIALOG` matches the raised
  `close_story_dialog`. [H for rule, M that the literal comparison is case-insensitive here: it is the documented
  generic rule.]
- **Who it fires for.** Nothing in 0x32edd0 or 0x32e5f0 checks the player. Whether an event fires only for the local
  human depends on its raiser; the table says which raisers test the local player. Almost all UI raisers run in the
  local client's input / dialog code and so are naturally local-only; sim-side raisers (hero neutralised, start
  bombard, super laser, activate ability, merge) run on every machine for every player. [H]

### 1.2 Names asked for

HUD mouse codes (HUD_GAPS §9.6): 0 left press, 1 left release, 2 right press, 3 right release, 4 / 5 move. Mode types
from vf+0xe0: 0 galactic, 1 land, 2 space (consistent with the checks at 0x30d5d0 / 0x30e1b0 below).

| Name | Raiser (RVA) | Trigger | Press / release, who | Mode |
|---|---|---|---|---|
| `click` | HUD mouse handler 0x2f6c30 | **First thing in code 0 (left press) and in code 2 (right press)**, before any hit test, whether or not something is under the cursor, and before the bombing-run / drag logic. Nothing is raised for the releases (codes 1, 3) | press only; local human's input | both (shared HUD handler) |
| `right_click` | - | **No such literal exists in the exe.** A right press raises `click` (code 2). A plot that listens for `right_click` can never fire [H] | - | - |
| `start_fleet_drag` | 0x2f6c30 code 0 | After the click was not consumed by a HUD component, the HUD's hover / drag component (+0x2160) is set and its type code is 0x1f2 (a fleet stack pad): raised, then the drag begins | press; local | galactic |
| `start_land_drag` | 0x2f6c30 code 0 | Same, component type 0x202 (a ground-unit stack pad) and the unit pad's owner test (0x39a230 against the local player) passes: raised, then the drag begins. A pad that fails the test raises nothing | press; local | galactic |
| `drag_select` | 0x3a310 (box selection) | After the selection box has been resolved: raised only if the new selection list holds **at least one object**; an empty box raises nothing. Raised before the selection event is queued | on box release; local | tactical (space and land) [M] |
| `drag_land_to_space` | 0x3cf7d0 and 0x3d0790 | A ground-unit stack was dropped on a fleet (space slot) pane and the transfer events were queued (the unit list is non-empty after the launch helper 0x3cfe70). Also raised when the drop target pane holds a fleet and units are launched into it | on drop; local | galactic |
| `land_to_space` | 0x3d51f0 | Entry of the "launch land units into a fleet" routine, **raised first, unconditionally**, whatever it then does (new fleet or merge). A second event `merge_fleets` follows when the target slot already holds a fleet | on the drop result | galactic |
| `merge_fleets` | 0x3d02e0, 0x3d4c80, 0x3d51f0, and 0x56a1e0 | After the fleet-management event that merges fleets (type 3 in the event builder 0x5afb60) was queued: a drop on an occupied pane (0x3d02e0), the merge helper (0x3d4c80), land-to-space with an occupied target (0x3d51f0), and the sim-side routine 0x56a1e0 when a unit's move finds an existing own fleet to join at the same planet slot | UI sites: on drop, local; 0x56a1e0 runs in the sim for any player [M] | galactic |
| `Continue_Tutorial` | 0x1243f0 via 0x32ed60 (no name argument) | The "continue" button (control id starting `IDC_CONTINUE`) of the story / tutorial dialog was clicked: the dialog's close helper 0x1243b0 runs, then the event is raised | button release; local | both (dialog layer) |
| `close_story_dialog` (XML `CLOSE_STORY_DIALOG`) | 0x105190 and 0x107900 | Two dialog closers (the story text dialog and the "mission objective / tutorial" dialog), each only if its dialog is currently open: dialog torn down, then raised. 0x107900 additionally resumes the game (un-pause the galactic clock when no pause is held) before raising | when the dialog closes (any cause); local UI | both |
| `retreat_clicked` | 0x30d5d0, 0x30d710 (land, mode type 1), 0x30e1b0, 0x30e2f0 (space, mode type 2) | The Retreat button callback, **only when the current mode exists and is the matching tactical type** (1 = land, 2 = space). Raised before the retreat is requested (the retreat itself needs the mode's retreat coordinator to accept it with the local player id). 0x30d710 / 0x30e2f0 are the button's other path (a retreat is requested again while an auto-confirm check passes) and raise it the same way | press of the button; local | tactical |
| `retreat_complete` | 0x340750 (retreat coordinator step) | The coordinator is in its **state 3 (completed)** and has a valid player id: raised, then the coordinator pushes the next mode transition. Runs for the retreating player whoever it is, from the sim | sim state | tactical |
| `corruption_dialog` | 0x52b160 | Opening the galactic corruption-choices dialog: the corruption options for the target planet were computed (list non-empty), the acting player is the local player **and** is a human (player +0x62), then the dialog is built and the event raised | on open; local human | galactic |
| `close_corruption_dialog` | 0x3ceab0 | The corruption dialog is closed only when the dialog gadget (control 0x381) reports it is open; closed, state reset, then raised (also called by `choose_corruption_option`) | on close | galactic |
| `open_black_market_menu` | 0x3c6680 | The black-market window was built (slot text `TEXT_BLACK_MARKET_SLOTS` set) and is being shown; raised just before the window's show helper 0x3e5aa0 | on open; local | galactic |
| `open_galactic_sabotage_menu` | 0x3c82f0 | The sabotage window was built and is being shown; raised just before the show helper 0x3e6ff0 | on open; local | galactic |
| `slice` | 0xf5d10 and 0x3e4450 | A slice (hacker) option was picked: 0xf5d10 for the old list-box dialog (`IDC_SLICE_LISTBOX`) after the cost check against the local player's credits passed and the slice event was queued (the unit `Unit_Galactic_Slicer_Tani` is announced as the actor), 0x3e4450 for the slot-bar index form. Not raised when the option is unaffordable (the negative GUI sound plays instead) | on the choice; local | galactic |
| `slice_dialog` | 0x6187a0 | Opening the slice dialog: raised only when the acting player is the local player and a human (+0x62); other cases take the AI path with no dialog | on open | galactic |
| `slice_slot` | 0x3d2900 (unit / ability pad drop handler) | A unit / ability pad was dropped onto a slot pad: the event name is chosen by the target pad's group index: **`slice_slot` when the target index is 1, otherwise `ability_slot`**; raised before the action is dispatched | on drop; local | galactic |
| `zoomed_in` / `zoomed_out` | 0x3c3bf0 (galactic camera zoom) | Raised **once, when the zoom animation reaches its end (progress exactly 1.0)**. If the pre-state was "zoomed in on a planet" (+0x44 = 2) the camera returns to the map and `zoomed_out` is raised; otherwise the camera is set to the planet view and `zoomed_in` is raised. Not raised for a zoom that is interrupted or starts mid-way | on animation end | galactic |
| `start_hyperspace` | 0x41f70 (tactical command handler, space) | A hyperspace order was accepted in the tactical command switch (the else branch after the order flag +0xe4 is set and the target check passes). The recipient dispatcher comes from the game-mode manager's current mode rather than the global mode pointer, but it is the same object [M] | on the order; local | space tactical |
| `hero_neutralized` | 0x616740 | In the galactic hero-neutralise routine (bounty-hunter / sabotage kill of a hero, A2K_T10): after the hero is removed and the story manager is told (0x3284c0), and before the "neutralised" message is shown to the local player. **Runs for any player's hero**, no local check before raising | on the sim event | galactic |
| `activate_deployed_ability` | 0x4b4d70 (SpecialAbilityEvent execute), case 1 | Executing a special-ability event of kind 1 whose actor and target both resolve, **when the actor is not in the "no ability" state (+0x337)**: raised, then the ability starts. The event is dropped silently before that if the galactic pause or a queued conflict blocks it, so no event in those cases | on execution; every player (not local only) | tactical [M] |
| `start_bombard` | 0x528140 (bombard manager start) | The orbital bombardment was accepted (not disabled, point not fogged for the player, cooldown fine), its record was filled in and queued with its start frame; raised last | on start; every player | land tactical |
| `super_laser_fired` | 0x714fa0 (Super_Laser ability apply) | The ability's apply step ran: the fire frame (+0x304) is now + `Fire_Delay` and the "fired" byte is cleared; raised after the effect or an event on the beam target was set up. **Raised when the laser is armed and the apply runs, not when the beam hits** | on apply; every player | space tactical |

Extra literals found in the same set (not asked for, in case the port needs them): `tooltip` (0x2e32f0, galactic
planet tooltip cleared), `fleet_tooltip` (0x2e2910 / 0x2e0c20), `land_tooltip` (0x2e0c20), `battle_end_closed`
(0x884c0: raised after the tactical battle-end dialog is dismissed), `end_setup` (0x3bfba0: setup-phase finished),
`drag_unit` (0x3d0790, 0x3d6c50: a unit pad drag completed by the local player), `next_planet` / `previous_planet`
(0x3d6610, 0x3d6730, 0x3dcf20, 0x3dd020: raised only when the cycle around the local player's planets found
**no** other planet, so the key did nothing), `choose_corruption_option` (0x3de390), `increase_corruption` (0x3f6810) /
`decrease_corruption` (0x3f6770; sim side, raised when the corruption change was applied).

### 1.3 Confidence notes

- The literals and which routine contains each are [H]. "Press / release" for HUD names is [H] (the mouse codes are
  documented in HUD_GAPS and the raise is the first statement of the case). The routine names given in the Trigger
  column are descriptions of what each routine does, taken from its calls and string uses; they are not retail symbol
  names [M].
- `slice_slot` / `ability_slot`: raised in 0x3d2900 [H].
- `drag_select` mode, `start_hyperspace` mode and `activate_deployed_ability` mode are [M] (from the neighbouring code,
  not from a mode check at the raise).

---

## 2. Q2 - the autosave

### 2.0 Correction to the question [H]

There are **two different things** named "AutoSave", and the Retry dialog uses the first:

1. **The save game `[AutoSave]`**: a normal save file whose *name* is `[AutoSave]`, written by the same routine as
   user saves. This is what "Load autosave" / Retry / "Resume game" load.
2. **`Recordings\AutoSave.rec`**: the *replay recording* of the running skirmish / multiplayer game (the command stream),
   which the "Save replay" dialog copies to a user-named `.rec` file. It has nothing to do with the Retry dialog or the
   story campaigns. It is only set up at skirmish / multiplayer game start (0xeed80 for a local skirmish, 0xc9e10 for
   a multiplayer-type game, both called from 0x8df00) and is small (350 bytes on the test machine, a header only until
   commands are appended).

### 2.1 Files and folder [H]

- Saves live in the per-user game folder `Saved Games\Petroglyph\Empire At War - Forces of Corruption\Save`. A save is
  `<name>.PetroglyphFoC64Save`; the autosave is `[AutoSave].PetroglyphFoC64Save` (the quick save is
  `[QuickSave].PetroglyphFoC64Save`). The test machine holds `[AutoSave].PetroglyphFoC64Save` (about 66 MB) beside
  user saves. The folder is created if missing (0x57560).
- Replays are in `...\Recordings`; `AutoSave.rec` is the live recording, a user "Save replay" copies it (CopyFile,
  overwrite allowed) to `Recordings\<name>.rec`, with messages `TEXT_SAVED_OK` / `TEXT_SAVE_FAILED`. The replay-save
  name box may not be the word `AutoSave` (0xe1d30).

### 2.2 When the save game `[AutoSave]` is written (0x55690) [H]

The routine is **skipped in multiplayer-type games** (game type 1, 2, 4; campaign is 0 and local skirmish is 3, so
**campaign and skirmish both autosave**). It makes sure the folder exists, then writes the full game state (the same
chunk set as a user save: player list, game-mode manager, story, scoring, and the modes) to the fixed name
`[AutoSave]`, **overwriting** the previous file; there is only ever one, no rotation, no backup.

It has three callers:

1. **Start of every new load, on the loading thread (0x92940, `LoadingThreadClass` run)** [H for order]: when the thread
   finishes its preparation and the job is **not a load of a saved game** (job flag +0x38 clear) and the save manager's
   "loading a save" byte (0x140a10b30) is clear, the autosave is written **before** the new mode is started
   (0x28fda0). So it captures the state that exists when a new load begins, not the first tick of the new mission:
   - Galactic to tactical battle (the BattleLoadDialog path, "Every long load" in MAIN_LOOP §2.5): the galactic state
     just before the battle (with the battle pending). This is the state Retry returns to, so a lost story
     battle retries from the galactic map before the battle, not from tactical frame 0 [M: the loaded state's exact
     pending-battle contents were not traced].
   - New campaign, new skirmish: the thread runs the same code; whether anything meaningful exists to save with no
     game running yet is not shown [L].
   - It is **skipped once** when the global byte 0x140b313e8 is set; the invade-event execute (0x48ff50) sets it when
     the event's flag byte (+0x24c) is 1 (and calls an extra callback, 0x140b313f0). The byte is cleared by the
     thread right after the check (and by 0x28e890 when a name is passed) [H for the clear, M for what the event flag
     means: it marks an invasion whose battle must not overwrite the autosave].
2. **Quit-to-menu confirmation** (0xa3cb0, the "Are you sure you want to quit?" Yes callback, code 1): not multiplayer
   and the current mode is **galactic (type 0)** -> autosave, then leave to the main menu. In a tactical mode it does not
   save. [H]
3. A second helper 0x57850 (reached through a data pointer, no direct caller) that first stores the screenshot thumbnail
   (0x7fa80) and then autosaves; the pointer holder was not identified [L on who calls it, M that it is the in-game
   "exit game" path].

It does **not** run every galactic turn, on a timer, or at the story-mission's first tick. [H: no other caller of
0x55690 exists in the callgraph.]

### 2.3 Loading it

- `Load autosave` = 0x55b60: not for multiplayer; refresh the save list (0x53c10), then load the save named
  `[AutoSave]` through the normal load routine (0x52d10). Nothing else is chosen: Retry, the main menu's
  `IDC_MAIN_MENU_RESUME_GAME` button (0xc6590) and the retry dialog all call it.
- **Retry dialog** (`IDD_RETRY_DIALOG`, 0xdca70 opens it; resource: title `TEXT_MISSION_FAILED`, default button
  `TEXT_LOAD_AUTOSAVE` = `IDC_RETRY_BUTTON`, button `TEXT_BUTTON_QUIT_GAME` = `IDC_QUIT_BUTTON`). Its handler is 0xdcc40:
  the retry button closes the dialog and calls 0x55b60; Quit opens the confirm box (`TEXT_QUIT_SURE`, yes = quit to
  menu with **no** autosave: 0xdcf50). `IDC_DIALOG_CLOSE_BUTTON` does nothing.
- The dialog is opened by the registered callback (0x140b30718 = 0xdca70, registered at start-up; its companion
  0x140b30720 is a veto that returns "do not show" when true) from two places: the story reward type **0x45**
  (log "Activating retry dialog box", from XML as the retry-dialog activate reward) and the victory monitor's
  countdown step (0x344670) when the countdown reaches 0 while monitor flag +0x79 is set and the dialog was not yet
  shown (+0x7a). Reward type 0x46 (`USE_RETRY_DIALOG`, "Setting retry dialog box") sets monitor byte **+0x78**. Why
  +0x78 and +0x79 are different bytes, and which code sets +0x79 on the galactic path, is not shown (BKC1 also found no
  writer) [L]. USE_RETRY_DIALOG is used by Underworld story missions (e.g. A01 M01, BATTLE_TRANSITIONS).

### 2.4 The load / save list row [H]

- **Label.** The list shows the file name, except that the name `[AutoSave]` is replaced by the text of
  `TEXT_GUI_AUTO_SAVE` and `[QuickSave]` by `TEXT_GUI_QUICK_SAVE`. Same in the Save dialog list (0xdd6d0) and the Load
  list (0xbdbb0). The date column is the file's saved date and time like any other row.
- **Ordering** (key function 0x54cc0, lower comes first): `[AutoSave]` key 0 (first), `[QuickSave]` key 1 (second),
  every other save by saved time descending (newest first).
- **Saving over it.** In the Save dialog (0xdfcd0), typing the name `[AutoSave]` or `[QuickSave]` is rejected: the
  name box is cleared and nothing is saved. A user cannot create or overwrite them by name.
- **Deleting.** The delete handler (0xde420) has no special case for the reserved names (no reference to them), so
  the autosave row can be deleted like any other save with the usual `TEXT_DELETE_CONFIRM` [M: absence of a guard
  read in the code, not seen in the running game].
- "Does an autosave exist" is tested by 0x56360 (scans the list for the name `[AutoSave]`); the main-menu Resume
  button state follows it [M].

---

## 3. What the port needs (summary)

1. One generic dispatcher; events are queued if raised for an inactive mode; matching is case-insensitive equality.
2. `right_click` does not exist; right press raises `click`. `click` fires on **press** of left and right.
3. The raisers tied to the local human are the UI ones; hero_neutralized, start_bombard, super_laser_fired,
   activate_deployed_ability, retreat_complete, increase / decrease_corruption, and the 0x56a1e0 `merge_fleets` fire for
   any player.
4. Autosave = the save named `[AutoSave]`, overwritten, written (a) at every non-save load start before the new mode
   begins (skipped for multiplayer, skipped once for flagged invade events), (b) when quitting to the menu from the
   galactic mode. Retry / Resume load it. `AutoSave.rec` is the skirmish replay file and unrelated.

## 4. Open items

- The exact meaning of invade-event flag +0x24c.
- What the autosave contains when a campaign starts from the menu (nothing to save).
- Who holds the pointer to 0x57850.
- The veto callback behind the retry dialog (0x140b30720 = routine at 0x14004fe70) was not read.
- A probe could settle the timing questions: delete `[AutoSave].PetroglyphFoC64Save`, start a battle from a story
  campaign and watch the file appear before the battle loads.
