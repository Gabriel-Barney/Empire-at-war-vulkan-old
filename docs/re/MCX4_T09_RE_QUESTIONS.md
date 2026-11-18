# mcx4 t09 RE questions: the galactic long tail of the honour gaps

Roadmap group `mcx4` task t09 (box MCX2); rows: `python -I tools/lint/check_tag_ledger.py --gaps --group galactic`.
Answers go to `docs/re/MCX4_T09_RE_ANSWERS.md`, one greppable section per tag (`## Q3 AutomaticAutoResolve`), behaviour in
words with [H] / [M] / [L] and RVAs, no decompiled code. For every tag: the parse (class, offset, value shape), **every
reader** (RVA, when it runs, what it changes, kind: rules / AI / UI), or **retail-unused** with the cross-reference
evidence (the ledger then marks the row retail-unused citing the section). The method of
`MCX4_RE_ANSWERS_GALACTIC.md` ("Method") applies.

Already settled (not asked again): `Fleet_Maintenance_Update_Delay_Seconds` (STRATEGIC_GAPS §2.3, retail-unused),
`Hyperspace_Fleet_Reveal_Range` / `Bribery_Fleet_Reveal_Range` (GALACTIC_REVEAL_CORRUPTION §1.6, the local fleet
visibility vote 0x433090), `Indigenous_Spawn_Destruction_Reward` (TRU1_C_RE §1.5, tactical sandbox objectives),
`Battle_Pending_Timeout_Seconds` (GALACTIC_DIALOGS §1.1, MP banner countdown only), `MaximumPoliticalControl` (AI tokens;
MCX4_RE_ANSWERS_GALACTIC Q2.1 lists its four read sites 0x39e9ba, 0x3f5470, 0x3f8483, 0x65f000).

## Q1 Corruption_N_Success_Credit_Bonus (planet type, N = 1..3; base FoC 9 uses)
Planet type +0x25e0 / +0x2780 / +0x2920 via 0x3716b0. `A2K_T10_CREDIT_NOTIFY_RE.md` lists one credit site in 0x3fb040
(site 0x3fbeda, notify 0); `MCX4_RE_ANSWERS_GALACTIC.md` Q7.4 says 0x3f9690 (the corruption-complete effects, also run by
the normal completion 0x3fc5b0) pays it to its player argument. Which is it (or both)? Who receives it on a normal
corruption completion (the corrupting player or the planet owner), how is N chosen from the corruption type (the
planet's choice list `Corruption_N_Type`?), and when exactly (completion frame, ownership change)? Also the faction
per-type record 0x33e310 and planet-type per-type record 0x3715d0 that 0x3f9690 applies through 0x27f9a0: what do they
grant?

## Q2 MaximumPoliticalControl (GameConstants +0xF0)
Of the four read sites, 0x3f5470 and 0x3f8483 sit next to `Political_Income_Curve` reads. Is either on the galactic
income path or the political-control update (a clamp of planet control, the income curve's x scale)? The port's income
uses a hard-coded 5 (`GALACTIC_ECONOMY.md` near §1.4, "Note the hard-coded 5") [M].

## Q3 AutomaticAutoResolve, AlwaysBypassAutoResolve (GameConstants; base FoC 3 / 1 uses)
Readers and effect: does either force or skip auto-resolve for human battles (single player), or are they MP / debug
only?

## Q4 SpaceAutoResolveHitRate, SpaceAutoResolveStunRate (GameObjectType; base FoC 2 uses each)
Reader in the auto-resolve strength or survival math (`BATTLE_TRANSITIONS.md` §4.4)? Formula if read.

## Q5 Prevents_Story_Campaign_Autoresolve (GameObjectType; 4 uses)
`BATTLE_TRANSITIONS.md` §4.5 names it [M for the wiring]: reader, and the rule (a unit of that type at the battle
planet locks auto-resolve for the human? only in an `Is_Story_Campaign` campaign?). Interaction with the lock 0x4472a0.

## Q6 Activated_Destroy_Planet / _Neutralize_Hero / _Remove_Corruption / _Siphon_Credits / _Slice / _System_Spy_Ability_Names (GameConstants)
Readers and effect (the ability names that count as "activated" for a HUD state, the AI, or the galactic ability
command?).

## Q7 Build_Countdown_Timer (GameObjectType, with Build_Countdown_Text_ID / _RGBA and the Build_Speech_* tags)
Reader: is it a rule (a build that completes after a countdown) or presentation only?

## Q8 Difficulty_Adjustment Bribe_Cost_Multiplier, second reader
`MCX4_RE_ANSWERS_GALACTIC.md` Q5.1 found 0x4313e0 and 0x434720. The AI per-hop payment 0x434720 (fleet locomotor
0x56a1e0): what is fleet data byte +0x96 (set by whom), and does the human ever reach it?

## t06 rows handed over by lane mcx4-tac (galactic side; RVAs from `mcx4-tac` notes)

## Q9 Ability base Unit_Strength_Category (+0x48) / Unit_Strength_Factor (+0x50)
`TACTICAL_GAPS.md` §4.7 [H]: in the auto-resolve exchange of fire 0x60d2a0, per combatant whose type has an ability
(0x374da0), each set category bit raises a 32-float table entry to max(entry, factor − 1) (initial value assumed 0 [M]).
How does that table enter the side's strength or the damage dealt (which combatants, multiplied as 1 + b?), and is the
table per side or per battle? Port: `engine/galactic/src/auto_resolve.cpp` (bonus 1).

## Q10 Income_Stream_Ability Split_Income_With_Allies (+0x184), Full_Amount_To_Everyone, Split_Favors_Owner, Owner_Income_Percentage; Income_Stream_Mod_Ability Interval_Multiplier (+0x180), Affects_All_Allied_Sources (+0x198)
Methods around 0x702790 / 0x702f20 / 0x7038b0 / 0x703d00 / 0x7046e0 / 0x704880. Where does the stream pay (tactical
skirmish only, or galactic too), how is the amount split among allies (equal shares, rounding, the owner's share), and
how does the mod change other streams' interval (which streams, `Interval_Multiplier` outside (0, 1] read as 0)?

## Q11 Siphon_Credits_Ability Evade_Detection_Chance (+0x178)
Methods 0x70bc30 / 0x70be20 / 0x70c260 / 0x70e2b0: when the detection roll runs, which random stream and how many draws,
and what detection does (ends the siphon, reveals the hero, refunds?).
