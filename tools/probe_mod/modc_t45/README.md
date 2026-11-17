# modc_t45: Fall of the Republic progressive GC and EAW-Remake build-bar locks (retail)

Observation kit for modc **t45** and **t50**: no mod payload, no Lua probe. Retail `StarWarsG.exe` runs windowed with one
workshop mod; nothing is written into the game or mod folders. Select All in retail is **Ctrl+A** (not Ctrl+Y, the port's
key). About 25 minutes in all.

Start: `tools\probe_mod\modc_t45\run_t45.cmd retail <workshop id>` (launch_foc.py, windowed; Steam must run). Quit with
Exit Game, then `cleanup_t45.cmd` (it only deletes `out\`, where screenshots may be kept).

## F: Fall of the Republic 1976399102, progressive galactic conquest (t45)

The port's question: the campaign `CloneWars_Medium_Republic` (file `Data\XML\Conquests\Progressive\CampaignLoaders\
Campaigns_CloneWars_Medium.xml`) starts with **1 planet** in the port. Its story script `GCProgressiveStateMachine.lua`
(in the mod's `config.meg`) calls `EawXStateMachine.from_script("eawx-gc-states/FotRProgressiveStates")`, and the mod ships
no such file. Prediction: retail hits the same missing script; the campaign either does not appear in the menu, or starts
with only its starting planet(s) and no further planets ever appear.

- F1 [shot] Galactic Conquest menu: is there a progressive / "Clone Wars" campaign for the Republic (medium size)? Note its
  exact menu name and every campaign row on that page.
- F2 Start it as the Republic (defaults otherwise). [shot] the galaxy after the load: how many planets are on the map
  (count them, or read the planet list / galaxy overview)? Any error text or an empty map?
- F3 Let it run 2 minutes on normal speed (unpause if needed). [shot] again: did planets appear, did the AI move, did any
  story pop-up show?

## R: EAW-Remake 1770851727, space skirmish build bar (t50)

The port's question: at the start of a space skirmish (modc t09 R3: Kuat Repair Yards, you vs one AI, no buy screen) the
station's build bar allowed only upgrades for the first 3 minutes; ship buttons (TIE 480, Arquitens 1200, ...) were grey.
The ship types carry `Required_Star_Base_Level` 2 and `Required_Special_Structures` `E_Shipyard_1`; the station starts at
level 1; the retail tactical check (SKIRMISH_SETUP §3) reads only `Tactical_Build_Prerequisites`, which these ships do not
have. Prediction: the buttons stay grey until the station upgrade to level 2 (`ES_Level_Two_Starbase_Upgrade`) finishes.

- R1 Same setup as t09 R3 (Space, "(4) [Re] Kuat Repair Yards", Empire vs one AI). In battle, select the station
  (click it). [shot] the build bar.
- R2 Hover a grey ship button (TIE Fighter, then Arquitens) and wait for its tooltip. [shot] each tooltip: the text may name
  what is missing (station level, a structure, population, credits).
- R3 Queue the station upgrade (the level-2 upgrade button). [shot] while it builds and right after it finishes: are the
  ship buttons still grey? If yes, hover one again: [shot] the tooltip.
- R4 Note the population counter (used / cap) and credits in each shot.

Results go to the lane-modc session (the coordinator relays them); the lane files them on modc t45 / t50, in
`docs/MODS.md` §9 / §10.4 and in the spec the answer belongs to (`docs/re/SKIRMISH_SETUP.md` §3 for R).

## Results (retail run 2026-11-17, run by the coordinator; 18 screenshots in `out\retail-2026-11-17\`, not committed)

Both predictions were wrong.
- F1: the campaign is listed. The GC menu has a faction combo (default "Republic-Aligned Sector Forces") and sections
  ERA-PROGRESSIVE / REGIONAL CONFLICTS (empty) / HISTORIC CONFLICTS / INFINITIES. The Clone Wars - Medium says "Planets:
  95". Start Game is greyed until the faction is Galactic Republic.
- F2: the galaxy opens empty (pop 0/0, 15,002 credits, "Initializing Galactic Conquest"). A Lua "YEAR SELECTOR" popup
  offers 22 / 21 / 20 / 19 BBY. After 22 BBY the full galaxy appears within about 30 s (about 100 planets, pop 66/826,
  10,000 credits, income 8250) with no game end. The mod's loader (`eawx-plugins/ui/gc-loader`) calls
  `StoryUtil.LoadCampaign("<PLANET>_Era_<n>_<FACTION>")`, a `LOAD_CAMPAIGN` reward.
- F3: after 2 more minutes the "Story - Campaign Overview" objectives popup opens by itself; the era text reads "22 BBY
  MONTH 6 (ERA 2)".
- R1: station level 1. The build bar's upgrades are coloured; TIE 480, 1080 and Arquitens 1200 (top row) and 448 / 2200
  (bottom row) are grey.
- R2: the TIE and Arquitens tooltips are only the unit text plus a population number. No requirement is named.
- R4: ships are still grey at about 3.5 min without an upgrade. Pop 0/150; credits rise about 140/s.
- R3: the level-2 upgrade builds in about 1 min, and the station becomes the large ring. The ship buttons stay grey;
  clicking TIE queues nothing and takes no credits. The Level 3 upgrade (5000) is then offered; level 3 was not tried.
