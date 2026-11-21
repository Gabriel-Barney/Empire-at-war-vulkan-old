# mcx4-gal2 RE questions: galactic leftovers of mcx4 (t02, t05, t09, t21) and mcx3 t12

Lane mcx4-gal2 (roadmap groups `mcx4` and `mcx3`, Stage 9). Answers go to `docs/re/MCX4_GAL2_RE_ANSWERS.md`, one
greppable section per question (`## Q1 TradeRouteMovementFactor`), behaviour in words with [H] / [M] / [L] and RVAs, no
decompiled code. The method of `MCX4_RE_ANSWERS_GALACTIC.md` ("Method") applies. Retail FoC x64 (Steam).

Already settled, not asked again: `MCX4_T09_RE_ANSWERS.md` Q1-Q11 (Corruption bonus, auto-resolve flags,
Activated_*_Ability_Names UI-only, Build_Countdown_Timer UI-only, Unit_Strength_*, Income_Stream_*, Evade_Detection_Chance);
`MCX4_RE_ANSWERS_TACTICAL.md` §9 / §13 (hero respawn scheduler 0x48eb10 and its battle-end callers).

## Q1 TradeRouteMovementFactor (GameConstants +0x10C, vanilla 0.5)

`BKG3_RE_ANSWERS.md` row "(c) AI planet-evaluator distance 0x662b30": linked planets → distance / GC +0x10C; unlinked →
the plain distance when ≤ `MaximumFleetMovementDistance`, else −1. Callers 0x662710 and 0x662c90. Which AI evaluators or
perception tokens (by name, `AI_GALACTIC_TOKENS.md` / `docs/re/A2I_TOKENS_RE.md`) call 0x662710 / 0x662c90, with what
arguments (source planet, target planet, which player), and how the result enters the token value (divisor, filter, a
min over planets)? Is any other GC +0x10C reader present (whole-image RIP-relative scan of +0x10C..+0x10F)?

## Q2 Corruption_Level_Override vs the first political update (t21)

`MCX4_RE_ANSWERS_GALACTIC.md` Q7.4: 0x28dbe0 (L14) writes corruption type 3 and calls 0x3f9690 with old type none, P =
planet owner, every time. `MCX4_T09_RE_ANSWERS.md` Q1: the first political update 0x3fb040 (PD +0x1C9 still 0) of a planet
whose owner has `Benefits_From_Corruption` also writes type 3 and inlines the type-3 effects with P = owner. At campaign
start, which of the two runs first for one planet (name the callers and the frame / load pass), and on a planet that gets
both (an override on an Underworld-owned planet), are the type-3 credit bonus and the faction / planet-specific upgrade
adds applied twice? Does either path test the current type (already 3) before applying?

## Q3 Hyperspace_Fleet_Reveal_Range / Bribery_Fleet_Reveal_Range: the fleet visibility vote 0x433090

`GALACTIC_REVEAL_CORRUPTION.md` §1.6 and open item 8. Needed to port the vote:
1. Player +0x368 (the "== 1" early-out), 0x4329c0 (owner equals the local player?) and 0x375270 (the content test):
   what each is.
2. Who calls 0x433090 and who reads its result: only rendering / the galaxy-map fleet icon and the fleet list, or also
   rules or AI code (selection, targeting, AI perception of enemy fleets, story events)?
3. Why corruption type 0 planets are skipped as revealers, and whether the Bribery range applies only to planets of
   corruption type 5 (Bribery) owned by anyone, or only those corrupted by the local player.

## Q4 Battle deaths of heroes after an auto-resolve

`MCX4_RE_ANSWERS_TACTICAL.md` §13 lists the land / space battle-end write-backs (0x28bf10 → 0x2b5360 / 0x2b4660 →
0x431860) as the callers of "hero defeated" 0x3a01f0 → 0x48eb10. For an **auto-resolved** conflict (AutoResolve
apply step, `BATTLE_TRANSITIONS.md` §4.4 / §4.5): do units killed by the auto-resolve go through the same 0x3a01f0 path
(so a named hero killed in an auto-resolve schedules a respawn with `Default_Hero_Respawn_Time` at the conflict planet)?
Name the kill call. Also: a hero ship killed in a space battle whose fleet survives (the fleet is not destroyed): which
call removes it and does it reach 0x3a01f0?

## Q5 Indigenous_Spawn_Destruction_Reward (GameConstants +0x11E34, vanilla 500)

`TRU1_C_RE.md` §1.5: read only by the tactical sandbox objectives builder 0x32bd10. When does 0x32bd10 run (which
modes: skirmish land maps, campaign land battles, the story sandbox linked battle of conflict state 9)? For each spawner
type on the map: the two events' exact names, the `_01` reward type 4 (credits) target player (the local player, the
player that destroys the spawners, every human?), whether the reward is paid once per spawner type or once in all, and
whether the reward goes to tactical credits or galactic credits.

## Q6 MEG directory listing (mcx3 t12)

`MCX3_MEG_LOOKUP.md` §1: reads go through the CRC `bsearch`. Directory scans (map lists, AI plan / script folders,
`*.ted` / `*.lua` enumeration, the file-find APIs over mounted MEGs): does retail enumerate a MEG's entries by the
**stored name table** (so a listed name may not be readable on a malformed MEG), or does it filter the list through
the CRC lookup? Name the enumeration function(s) and what they return for a duplicate CRC or a name stored with `/`.
