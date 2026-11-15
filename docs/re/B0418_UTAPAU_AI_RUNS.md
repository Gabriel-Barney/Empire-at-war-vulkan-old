# b0418: Empire and Underworld AI on Utapau, port runs and the retail kit (2026-11-14)

Backlog b0418 asked for retail's length and winner of an Empire vs Underworld AI land skirmish on `_MP_LAND_UTAPAU`, the
configuration of the port test `alamo_headless_skirmish_ai_foc_land_empire_underworld_seed*`. This page records why that
exact measurement does not exist, what the port does instead, and the retail runs that can be compared. Retail findings go
in section 5 after the runs.

## 1. AI vs AI has no retail counterpart

A FoC skirmish always gives the local player a slot and has no spectator slot (`tools/probe_mod/q2_traces/README.md`,
"AI vs AI is not possible in a FoC skirmish"). The port's two-AI test therefore cannot be checked against retail directly.
The comparable configuration is one AI against an idle human: the human sends no commands and its units only react
automatically. The port has the same mode (`--skirmish-ai foc-land --human H --ai A`).

## 2. The port's two-AI runs (dev 522bef97, MSVC, 4 workers, 70000 ticks)

Seeds 1-10: the Underworld wins 8 (frames 26830-51427); seeds 1 and 2 are still fighting at 70000; the Empire never wins.

Why the Empire is weak, seed 5 (AI log, `--ai-log`):
1. Its two starting Stormtrooper squads die in its first Clear_Infantry plan (371-4115).
2. `Tactical_Multiplayer_Build_Infantry_Emergency` (task force `Infantry | LandHero = 1`, a build plan) buys
   `Boba_Fett_Team_Land_MP` at 4088.
3. Fett's force (500) is above the emergency equation's limit (`200 > Non_Structure_Force_Unnormalized`), so no further
   emergency build runs.
4. `BobaFettFlameInfantry` takes Fett at 4809 and marks itself not removable; it holds him until a Burn_Units_Land purge at
   24854.
5. `Secure_Contestable` (task force `Infantry | LandHero = 1`, not a build plan, so it does not buy) has no free unit. No
   mineral-processor pad is captured, `Number_Resource_Structures_Land` stays 0, so `Is_Initial_Skirmish_State_Established`
   stays 0, and that blocks `Tactical_Multiplayer_Should_Build_Units_Generic_Land`.
6. The Empire's credits grow from 5800 to 11065 (frames 4201-23401) while it starts no plan between 4809 and 19487.

Every step follows the shipped XML / Lua; whether retail's plan solver also picks Fett in step 2 (rather than a
Stormtrooper squad) is the open question. It decides whether the stall is a port difference (tier 2).

## 3. The port against an idle human (dev 522bef97, MSVC, 4 workers, 36000 ticks, seeds 1-3)

| | Empire AI vs idle Underworld | Underworld AI vs idle Empire |
|---|---|---|
| Winner / end frame | Empire 3 of 3: 27604, 24879, 26499 | Underworld 2 of 3: 35199, 26887; seed 2 still fighting at 36000 |
| First mineral processor ordered | only seed 2, at 11615 | 1454 in all three (second at 8153-8549) |
| First purchases | `EL_Increased_Mobility_Upgrade` and `EC_Level_Two_Tech_Upgrade` at about 4805; Dark Troopers, `Imperial_Anti_Aircraft_Company` (and in seed 1 Boba Fett) at about 7045 | `Bossk_Team` at 365, `UC_Level_Two_Tech_Upgrade` at 1820, `UL_Bounty_Upgrade` at 3299, skiffs / MDUs / pod walkers at about 7028 |
| Lua errors | 1 in seeds 1 and 3 (`BOBAFETTFLAMEINFANTRY ... Move_To -- Target is already dead`, see section 6) | 0 |

Retail's Rebel AI on the same map built its first Mineral Processor at 1909 (`SKAI_T06_LAND_RUNS.md` §6), so a first
refinery at 11615 or never, as in the port's Empire AI runs, would be a large difference.

## 4. The retail kit

`tools/probe_mod/q2_traces/`, the skai t02 land trace with the plan hooks extended for this question (`make_plan_hooks.py`
now also hooks `BobaFettFlameInfantry` and `AI_Plan_ExpansionGeneric_BuildMDUItem`). Two runs, about 22 min each, by-hand
menu clicks, hands off afterwards:

| Run | Clicks (Single Player - Skirmish Battles - Land - Utapau, Advanced... Default) |
|---|---|
| `run_skirmish_land_empire_ai.cmd` | you = Underworld (slot 1), slot 2 = Empire, Medium AI |
| `run_skirmish_land_underworld_ai.cmd` | you = Empire (slot 1), slot 2 = Underworld, Medium AI |

Read each run with `analysis\land_attack.py out\<run>`; the plan records (`PSTART` / `PDONE` / `PFAIL` with task-force
types) give the AI's purchases and plans. Compare against section 3:
- the first Mineral Processor frame of each AI (port: Empire 11615 or never, Underworld 1454);
- what the Empire's first Build_Infantry_Emergency buys, if it runs, and whether Flame_Infantry then holds Fett;
- the winner and the end frame within 36000 ticks.

## 5. Retail results (2026-11-14, driven by Claude)

Two runs of the q2 land kit, FoC 1.121, Utapau Heights (`_MP_LAND_UTAPAU`), Advanced Default, 36000 ticks each, both ended by
the kit at the tick limit (exit 0, mod removed, profile restored): `out\20261114-210208-skirmish-vanilla-lua` (Empire AI, human
Underworld idle) and `out\20261114-212910-skirmish-vanilla-lua` (Underworld AI, human Empire idle). Frames are `lframe` × 30
from the `TBUILT` / `TKILL` records.

| | Retail Empire AI | Port Empire AI (§3) | Retail Underworld AI | Port Underworld AI (§3) |
|---|---|---|---|---|
| Battle end | **not within 36000**; the AI killed the idle Underworld vehicle factory 23255, droid works 25154, barracks 26153, then no more base kills | Empire wins at 24879-27604 (3 of 3) | **not within 36000**; first base kill 24447 (comm array), then factories, shield, power, barracks, research 30835-33870 | Underworld wins at 26887 and 35199; 1 of 3 still fighting at 36000 |
| First mineral processor | built 2565 (4 more by 13305, plus a mining facility 12792) | ordered 11615 in 1 of 3, never in 2 of 3 | built 2451 (4 more by 10417) | ordered 1454 |
| Tech 2 / tech 3 | 7674 / 17617 | ordered at about 4805 | 3510 / 20242 | ordered 1820 |
| Hero | Boba Fett team 2875 (Flame_Infantry from 121 s, one start; Mara Jade in Land_Heroes) | Boba Fett with the units at about 7045 in seed 1 | Bossk team 597 (again 14550); Hutt palace 4878 | Bossk team ordered 365 |

Retail opening of the Empire AI (plan records): at 12 s it starts **two** Secure_Contestable plans, one per starting
Stormtrooper team, one on the `SKIRMISH_OUTPOST_PAD` and one on a `SKIRMISH_MINERAL_PROCESSOR_PAD` (that one exits at 85.8 s,
when the processor is done), and a third on a Reinforcement_Point at 72.8 s; Land_Scout_Area starts only at 96 s, and the
first Build_Econ_Structure at 60 s. The port's Empire AI at frame 371 (12.4 s) starts **one** Secure_Contestable (the outpost
pad, object 757) and two Land_Scout_Area plans, which take the other starting squads; its next Secure_Contestable is at
18115. So the port's late or missing refinery is a tier-2 difference in the opening plan choice (Secure_Contestable vs
Land_Scout_Area for the second team), not the Boba Fett trap of §2: retail buys Fett too (2875), but by then it already has
its refinery.

The emergency infantry build (`Tactical_Multiplayer_Build_Infantry_Emergency`) did not run in either retail run; Boba Fett came
from another purchase path. Retail's AI does not finish an idle base within 36000 ticks with either faction, while the port's
does in 5 of 6 runs.

## 6. b0419: `Move_To -- Target is already dead` in BobaFettFlameInfantry (no port difference)

`MoveUntilDone` tests `TestValid(target)` in its loop condition, calls `PumpEvents()` (which yields to the engine:
`coroutine.yield` in `PGBASE.LUA`), then, once its timeout has passed, calls `fett.Move_To(target)` again without testing
the target. Retail `GameObjectWrapper.Is_Valid` tests only that the wrapper's object pointer (+0x60) is non-null
(`OBJECT_RUNTIME.md`), and `Move_Internal` raises "Target is already dead." for a GameObjectWrapper argument whose +0x60 is
null (`LUA_API.md`, Move_To / Attack_Move / Guard_Target). Both read the same field, so a target that dies during the yield
raises the error in retail too. The port tests both with the same predicate (`object_alive`). The error is the shipped
script's own behaviour, and how often it happens follows from the battle, not from a separate rule.
