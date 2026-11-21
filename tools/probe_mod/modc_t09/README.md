# modc t09: play the second mods in the port

Roadmap group `modc`, task t09 ("Gabriel: run the second mod in game and report what breaks"). t06 picked two installed
workshop mods that are not Thrawn's Revenge and checked them headless (`docs/MODS.md` §9, "Second mod"). This run is the
part only a person can do: play them in `alamo_game` and look at what the headless checks cannot see (rendering, UI,
text, sound, how the AI plays). Nothing here drives the game for you and nothing writes into the game or mod folders.

| Mod | Workshop id | What it is |
|---|---|---|
| EAW-Remake (Empire at War Remake) | 1770851727 | Total conversion by its own team: Rebel / Empire / Underworld + Hutts and minor factions, own MEGs and shaders |
| Star Wars: Age of Legends (New Jedi Order era) | 2858787558 | Era mod: New_Republic, Galactic_Empire, Vong and ~45 minor factions, loose XML, story files with their own root tag |

## Retail run (Claude with computer use, 2026-11-17)

Since 2026-11-14 Claude runs every probe kit in retail `StarWarsG.exe` (`docs/DECISIONS.md`); the port is never driven
(the port side was checked by the hidden smokes, `docs/MODS.md` §9). This run answers what the port's smokes found and the
docs do not settle. Per mod, start the original windowed, one at a time:

```
tools\probe_mod\modc_t09\run_t09.cmd retail 1770851727      EAW-Remake
tools\probe_mod\modc_t09\run_t09.cmd retail 2858787558      Age of Legends
tools\probe_mod\modc_t09\cleanup_t09.cmd                    afterwards (quit the game with Exit Game first): deletes out\
```

(`launch_foc.py` adds `WINDOWED SCREENWIDTH=1920 SCREENHEIGHT=1080`; computer use reaches the game as `StarWarsG.exe`.)
Take a screenshot at each **[shot]** and write the answers as `Rn: ...` lines.

**EAW-Remake 1770851727**
- R1 [shot] Main menu: is "Single Player" a normal button under the menu heading, and does a click open the
  single-player menu? (modc t34: the mod writes the button as `265,550,215,,22`; the port now reads it as 22 units high.)
- R2 [shot] Skirmish setup: the game-type list, its rows and which row is selected when the dialog opens.
- R3 Space skirmish on any space map, you vs one AI, buy a few units, Start. [shot] at the battle start and again after
  about 20 s: which of your units are on the map (start forces? a space station only?) and where the bought units are
  (on the map, arriving by hyperspace, or in the reinforcement pool behind "Call for Reinforcements")? Then Ctrl+Y
  (select all): [shot] what is selected? (modc t40: in the port's smoke Ctrl+Y at battle tick 57 selected nothing.)
- R4 Rough speed of that battle on a map with big asteroid fields (Geonosis): smooth / slow? (modc t33, known slow in the port.)

**Age of Legends 2858787558**
- A1 [shot] Main menu: background, title, buttons (any raw `TEXT_...` keys?).
- A2 [shot] Skirmish setup right after it opens: is there a game-type (Land / Space) list at all? Which map list is shown
  (land or space maps)? (The mod's `IDD_SKIRMISH_STAGING_DIALOG` has no `IDC_GAME_TYPE_LIST`.)
- A3 [shot] Slot 0's faction combo **as it opens** (no clicks): which faction is selected? Open the combo: [shot] the
  first rows, including any header-like entries such as the text of `TEXT_FACTION_HEADER_MAJOR_POWERS`. (modc t41: the
  port defaults slot 0 to `Faction_Header_Major_Powers`, which has nothing to buy.) If time allows, close the game,
  start it again and check A3 once more (is the choice remembered between launches?).
- A4 [shot] Start Game with that default faction: what does the buy screen list?

Results go to the lane-modc session (the coordinator relays the paths / screenshots); the lane files them on modc t40 /
t41 / t34 and in `docs/MODS.md` §9.

### Results (retail run 2026-11-17, coordinator session; screenshots were in `out\`, not kept)

**Kit error:** R3 and A4 said "Ctrl+Y (select all)". Ctrl+Y is the port's binding; retail's Select All is **Ctrl+A**
(UI.md hotkey table, Q6_T15_RETAIL_RUN row 6). The "Ctrl+Y selected nothing" readings say nothing about selection.

- R1 Remake: "Single Player" is a normal button like the other four; one click opens Galactic Conquest / Skirmish
  Battles / Back. Matches the port after t34.
- R2 Remake: game types Land, Space, Land Control (Land selected at open). Official tab: Land lists no maps; Space lists
  11 ("(4) [Re] Kuat Repair Yards" first). Custom tab empty for Space. Slots: player Empire / Team 1, AI Rebellion / Team 2.
- R3 Remake (Kuat Repair Yards): **no buy screen** (Start Game goes to the load screen), as SKIRMISH_SETUP §2.7 says.
  Start: own Space Station Level 1 and one TIE Fighter squadron (12); 0/150 population, 5,634 credits rising (about 24k
  after 3 min). In the first 3 minutes the station's build bar allowed only upgrades; ship icons (TIE 480, Arquitens
  1200, ...) stayed greyed. R4 not checked (no Geonosis space map in the Remake's list).
- A1 Age of Legends: about 90 s splash; title "STAR WARS / AGE OF LEGENDS"; the five main buttons, no raw TEXT_ keys;
  Single Player offers only "Galactic Conquest" and "Space Skirmish".
- A2: dialog "SPACE SKIRMISH SETUP", a "Game Type:" label with no list; the map list shows **land** maps ((4) Dagobah
  Rush first), plus a "Select Age of Legends Maps" button.
- A3: player New Republic, AI the "= MAJOR POWERS ===" header row (combo rows listed in MODS.md §9). The profile had a
  saved faction index 1; the rule is SKIRMISH_SETUP §2.8. Not checked between launches.
- A4: Start Game gives a **land** battle at Dagobah (no type list: Land), no buy screen, Era 1, 0/0 population, 6,000
  credits (not rising), no own units on the map or minimap, an empty reinforcement pool; quitting at 1:18 gives "You
  have lost the battle" with no losses on either side.

## How to start (port, Gabriel only)

From the repository root, after a normal `build_msvc.cmd` (or set `ALAMO_BIN` to a folder with `alamo_game.exe`):

```
tools\probe_mod\modc_t09\run_t09.cmd check  1770851727    load report only, no window (out\<time>-<id>-report.txt)
tools\probe_mod\modc_t09\run_t09.cmd port   1770851727    the port with the mod (log: out\<time>-<id>-alamo_game.txt)
tools\probe_mod\modc_t09\run_t09.cmd retail 1770851727    the original with the same mod (Steam must be running)
```

Same with `2858787558`. Instead of the command line you can also start `alamo_game` without a mod and use the main
menu's **Mods** button (`docs/MODS.md` §5.2): select the mod's row, wait for its report in the right pane, then
**Launch**. Both ways give the same command line (`STEAMMOD=<id>`).

The retail run is only for comparison: when something looks wrong in the port, check whether the original does the same
before reporting it.

## What to look at (each mod, about 20 minutes)

1. **Main menu** (2 min): the mod's menu background, fonts and text (no raw `TEXT_...` keys), music. The Mods dialog row
   says "(running)" for this mod. Known (modc t34): on the Remake the hidden smoke saw "Single Player" drawn as a heading
   with no button, and its click did nothing; on Age of Legends the skirmish setup's Land / Space list did not offer
   Space. Say what the original shows there, and whether Single Player / Space work when you click them.
2. **Space skirmish** (5 min): Skirmish > Space, any map of the mod (the Remake's own maps are on the Custom tab too),
   you vs one AI. Look at: the buy screen (only the mod's units, sensible costs), ship models and textures (missing =
   invisible or pink / green), engine glows and shields, weapons firing, the AI building and attacking within a few
   minutes, no freeze or crash.
3. **Land skirmish** (5 min): Skirmish > Land, any map. Look at: starting units, terrain, infantry / vehicle models and
   animations, build pads, the AI capturing pads and attacking.
4. **Galactic conquest** (8 min): Remake: "GC Mini Web Way" (any faction). Age of Legends: a "Balance Point" sandbox (New
   Republic or Vong). Look at: the galaxy map (planets, trade routes, icons), planet info, building and moving a fleet,
   the AI factions taking turns (planets changing owner after a few minutes at normal speed), one space battle (fight it
   or auto-resolve), the story / event pop-ups.

Known from the headless runs (`docs/MODS.md` §9), so not worth reporting again: Age of Legends' 95 story scripts are
32-bit bytecode (they do not run in the original either), two of its AI scripts start with a UTF-8 BOM (same), its
`Try_Weapon_Switch` / unknown filter Lua errors are in the mod's own scripts; the Remake's land factions have no
`Land_Skirmish_AI_Default_Forces`, so the port starts them with their three cheapest units ([port] fallback, the original
places nothing); Remake space battles on maps with very large asteroid fields run slowly in the port (about 26 ms a sim
tick headless, modc t33): say how slow it feels, but it is known.

## Where to report

Write what you saw in a reply to the modc coordinator session (or on the roadmap, group `modc`): per mod and per step
one line "OK" or what broke, with a screenshot for anything visual, and the `out\<time>-<id>-alamo_game.txt` log of a run
that crashed or misbehaved. For a crash, also say what you clicked last. Then delete `out\` (it holds only logs, the
effect cache and saves).
