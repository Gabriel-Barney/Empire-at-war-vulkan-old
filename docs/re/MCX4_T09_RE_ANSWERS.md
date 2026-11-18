# mcx4 t09: RE answers, the galactic long tail of the honour gaps (Q1-Q11)

Answers to `MCX4_T09_RE_QUESTIONS.md` for lane mcx4-gal (roadmap group `mcx4`, task t09, box MCX2). Q9-Q11 were handed over
by lane mcx4-tac. Retail `StarWarsG.exe` FoC x64 (Steam), Ghidra 12.1.4 through ReVa plus REA, 2026-11-18. Addresses are
RVAs (image base 0x140000000). Behaviour only, no decompiled code. **[H]** read and cross-checked, **[M]** read with one
link inferred, **[L]** inferred. "Rules" = game-rule code that runs for every player; "AI" = AI planning or perception;
"UI" = front-end, HUD or presentation. GRC = `GALACTIC_REVEAL_CORRUPTION.md`.

## Method

As in `MCX4_RE_ANSWERS_GALACTIC.md` ("Method"). For this batch a fresh capstone linear sweep of the whole `.text`
(2 189 191 instructions) indexed every memory operand by displacement (with its base register) and every RIP-relative
target. A "no reader" verdict below means:
- for a GameConstants field (object at 0x140b15800): no RIP-relative operand targets any byte of the field outside the
  GC methods (ctor 0x2c4370, defaults 0x2c91c0 and its chunk 0x2c92d6, dtor 0x2c5b30, resolver 0x2c79d0);
- for a type, ability or object field: every instruction with that displacement was checked by its base register in
  context, and only the ctor, copy, dtor or post-load fix-up of the owning class touch the field.

The tag strings have no code xref (`docs/re/ghidra/strings_xrefs.csv`); only the field tables reference them [H].

**Field-type note** [H]. Field-table code 0x8 is a **float** parse even when `xml_field_tables.md` shows the sample as
"int": `Corruption_N_Success_Credit_Bonus` is loaded with `movss` (0x3716b0), `Damage` with `cvttss2si` (0x4b8114) and
`AI_Combat_Power` with `mulss`. Code 0x6 is an int.

## Summary

| Tag | Reader (RVA) or retail-unused | Effect | Kind | Conf |
|---|---|---|---|---|
| Type `Corruption_1/2/3_Success_Credit_Bonus` (+0x25E0 / +0x2780 / +0x2920, float) | 0x3716b0 ← 0x3f9690 (corruption complete, campaign-start override) and 0x3fb040 (first owner, type 3) | credits to the corrupting player on a completed corruption of the slot's type | rules | H |
| GC `MaximumPoliticalControl` (+0xF0) | 0x3f83b0 + 0x65f000 (AI tokens), 0x39e920 (UI pips); 0x3f5470 only in a dead branch | AI potential income and token scale, the number of control pips; **not** the income x-scale and **not** the control clamp (literal 5) | AI + UI | H |
| GC `AutomaticAutoResolve` (+0x2F1) | **retail-unused** | none | | H |
| GC `AlwaysBypassAutoResolve` (+0x2F0) | 0x3ca480 only | `Yes` removes the auto-resolve button from every battle-pending dialog | UI (rules effect for the human) | H |
| Type `SpaceAutoResolveHitRate` (+0xDF8, int) | 0x4b80c0 ← vf3 0x608080: copied into the auto-resolve object, **never read** | none | | M |
| Type `SpaceAutoResolveStunRate` (+0xDF4, int) | 0x4b8120 ← vf3 0x608080: copied, **never read** | none | | M |
| Type `Prevents_Story_Campaign_Autoresolve` (+0xA1) | 0x430d90 / 0x430f30 (fleet flag), 0x2affb0 ← 0x3ca480, 0x30d710, 0x30e2f0 | story campaigns: no auto-resolve for the human in a **space** battle with such a unit; AI-vs-AI unaffected | rules + UI | H |
| GC `Activated_Destroy_Planet/_Neutralize_Hero/_Remove_Corruption/_Siphon_Credits/_Slice/_System_Spy_Ability_Names` | 0x2e5xxx helpers ← 0x3d2020 ← 0x3e8890, 0x3eb200; also 0x3e1790, 0x3ec380 | which galaxy-map ability button (kind, label) a selected hero shows over a planet; removal-cost tooltip | UI | H |
| Type `Build_Countdown_Timer` (+0xC7) | 0x401e50 (speech), 0x3d2b0 (HUD text) | countdown speech and on-screen timer only; no build rule | UI | H |
| Type `Build_Countdown_Text_ID` (+0x8D8 → +0x8F8) / `_RGBA` (+0x918) | 0x3782a0 (resolve), 0x3d2b0 | the timer's label and colour | UI | H |
| Type `Build_Speech_Countdowns` (+0x15C8) | 0x401e50 | prefix of the countdown speech events | UI | H |
| Type `Build_Speech_Underway` / `_Completed` / `_Stopped` (+0x1550 / +0x1578 / +0x15A0) | **retail-unused** | none | | H |
| Fleet data byte +0x96 (`Bribe_Cost_Multiplier` second reader) | set by AI 0x6aedc0, read by 0x56a1e0 → 0x434720, cleared 0x56a76a | AI-only per-hop toll; a human never reaches it | rules (AI-only path) | H |
| Ability `Unit_Strength_Category` (+0x48) / `Unit_Strength_Factor` (+0x50, float) | 0x48d320 ← 0x374010 ← 0x60d2a0 → 0x60ad90 | auto-resolve fire bonus by the firing unit's category; **inert in shipped data** (factor never set, default 0) | rules | H |
| `Income_Stream_Ability` `Split_Income_With_Allies` (+0x184), `Split_Favors_Owner` (+0x185), `Full_Amount_To_Everyone` (+0x186), `Owner_Income_Percentage` (+0x180) | 0x703d00 (validate), 0x703990 / 0x7038b0 (create), 0x694ba0 / 0x694e90 (pay) | recipients and share rules of a skirmish income stream | rules (skirmish) | H |
| `Income_Stream_Mod_Ability` `Interval_Multiplier` (+0x180), `Affects_All_Allied_Sources` (+0x198) | 0x704880 (validate), 0x703fe0 / 0x7046e0 (apply / remove), 0x694b30 | interval modifier entry (m − 1), stacking per category; whose streams are modified | rules (skirmish) | H |
| `Galactic_Stealth_Ability` `Evade_Detection_Chance` (+0x178; not a `Siphon_Credits_Ability` field) | 0x6ff260 → 0x3a86d0 (store), 0x3a0b70 ← DetectHero 0x6f6e30 | one synced draw after a successful detection roll; r < chance cancels the detection; FoC value 0.0 | rules | H |

---

## Q1 Corruption_N_Success_Credit_Bonus

Covers `Corruption_1_Success_Credit_Bonus`, `Corruption_2_Success_Credit_Bonus` and `Corruption_3_Success_Credit_Bonus`.

**Parse** [H]. GameObjectType (planet type) floats at +0x25E0, +0x2780 and +0x2920. Each sits in the same slot as
`Corruption_N_Type` (+0x2464 / +0x2608 / +0x27A8, CorruptionTypeEnum: Intimidation 0, Piracy 1, Kidnapping 2,
Racketeering 3, Corrupt_Militia 4, Bribery 5, Black_Market 6, Bonded_Citizens 7).

**How N is chosen** [H]. The getter 0x3716b0(planet type, corruption type c) returns 0.0 when c = −1. Otherwise it tests
slot 1, then 2, then 3, and returns the bonus of the **first slot whose `Corruption_N_Type` equals c**. No match gives
0.0. So N is the slot of the planet's choice list that holds the applied type; a type listed twice pays slot 1's value.

**Both sites pay it** [H]. 0x3716b0 has two call sites, 0x3f9792 in 0x3f9690 and 0x3fbec6 in 0x3fb040. Both compare the
float with 0.0 and, when it is > 0, call Add_Credits 0x27f370(player, bonus, notify 0). There is no int conversion: the
credits are the float as loaded.

### Q1 Normal completion: 0x3f9690 (renamed `Planet_Corruption_Complete_Effects`)

Arguments: planet behaviour, planet, new type, old type, player P. It does nothing when the planet has no planet data.
Steps, in this order [H]:
1. **Every player with a faction**, in PlayerList index order:
   - when new ≠ −1 and the faction's `Automatic_Planetary_Corruption_Upgrade_Type` entry for the new type exists
     (0x33e310: index < 8 and < the list count; faction list at +0x3068, data +0x3070, count +0x3078, type pointer at
     entry +8), the pair (planet id, upgrade type) is added to that player's planet-specific upgrade list (0x27f9a0:
     player +0x338 planet ids, +0x350 types; an identical pair is not added twice);
   - when old ≠ −1, the pair for the old type's faction entry is removed (0x2861f0).
2. **P only** (skipped when P is null):
   - new ≠ −1: the credit bonus above; then the planet type's `Corruption_N_Success_Planet_Specific_Tactical_Upgrade`
     for the new type (0x3715d0, resolved type at +0x25F0 / +0x2790 / +0x2930, same slot rule) is added to P's list;
   - old ≠ −1: the old type's planet-specific upgrade is removed from P's list;
   - new ≠ −1: `Corruption_N_Success_Unlock_Unit_Type` (0x371610) is unlocked for P (0x27f860), only when its planet cost
     0x3711c0 is > 0 (`REMAINING_GAPS.md` §1.5 item 5);
   - old ≠ −1: the old type's unlock is removed (0x286150).
3. The planet's component +0x100 (0x37cff0) and the notifier 0x3f98a0 run with (old, new).

**Who is P on a normal completion** [H]. The completion 0x3fc5b0 (planet service 0x3fe1b0, GRC §4.4) passes
P = 0x2948d0 (renamed `PlayerList_First_Corruption_Player`): the **first player in the player manager's list (array
+0x20, count +0x28) whose faction has `Benefits_From_Corruption`** (+0x10B). It is **not** the planet owner. New = PD
+0x2F4 after the completion; old = PD +0x2F4 before it. A gain starts only on an uncorrupted planet (GRC §4.3), so old is
−1 for a gain. A removal has new = −1 and pays nothing. [M]: in single player P is the Underworld player when one
exists, and no player otherwise (then step 2 is skipped).

**When** [H]. On the first galactic frame at which the planet service's frame argument is ≥ the end frame PD +0x2EC
(GRC §4.4). Gain durations (GRC §4.3): the corruption action 5.0 s, corruption mission success GMM +0x1FC (5.0 s),
`Corrupt_Planet_Ability` +0x17C, and an ownership change to a `Benefits_From_Corruption` owner 10.0 s (always type 3).
So a capture by the Underworld pays the type-3 bonus 10 s after the capture, to P, not at the capture.

**Campaign start** [H]. `Corruption_Level_Override` (0x28dbe0 at 0x28e77f) calls 0x3f9690 with P = the **planet owner**
(`MCX4_RE_ANSWERS_GALACTIC.md` Q7.4).

### Q1 First owner set: 0x3fb040 (site 0x3fbeda)

This is the path `A2K_T10_CREDIT_NOTIFY_RE.md` lists [H]. It runs only on the planet's **first** political update (PD
+0x1C9 still 0, set at once), which normally happens at campaign start. When the owner's faction has
`Benefits_From_Corruption`:
- PD +0x2F4 := 3 at once (no transition) and the visuals are rebuilt (0x3ff230);
- then the steps of 0x3f9690 are inlined for type 3, old type none, with P = the **planet owner**: faction upgrade for
  every player, credit bonus (float, notify 0), planet-specific upgrade, unlock, 0x37cff0, 0x3f98a0.

### Q1 What the per-type records grant

- **Faction record** [H for the list, M for the battle use]: `Automatic_Planetary_Corruption_Upgrade_Type` (faction
  +0x3068, eight names indexed by corruption type). FoC: Rebel and Empire list `Generic_<Type>_Corruption_Upgrade`
  (DUMMY_UPGRADE objects that only show a corruption icon); the Underworld lists the same except index 3,
  `Underworld_Racketeering_Corruption_Upgrade`, which carries the bounty ability (`A2M_R2_UW_BOUNTY_TRACE.md`). Every
  player gets **its own faction's** entry for that planet. The per-player list is read by the battle setup 0x2a1eb0
  (0x281620 / 0x281760): each listed upgrade that is affiliated and has behaviour 0x65 is spawned for that player in a
  battle at that planet (`REMAINING_GAPS.md` §3.2 step 5).
- **Planet record** [H]: `Corruption_N_Success_Planet_Specific_Tactical_Upgrade`, same list, P only. No FoC planet sets
  it.

**Data** [H]. FoC uses only `Corruption_1_Success_Credit_Bonus`, nine planets, all with `Corruption_1_Type` =
Kidnapping: Bestine 10000, Byss 9500, Carida 8000, Kuat 10000, Muunilinst 8500, NalHutta 8000, NalHutta_Story 8000,
Thyferra 7500, Utapau 9000. No planet has a bonus on a Racketeering slot, so the type-3 paths (capture, first owner,
override) pay 0 in base FoC.

**Port.** At the corruption-complete moment pay the bonus of the first slot whose type matches to the first
`Benefits_From_Corruption` player (planet owner for the campaign-start override and the first-owner instant path), as a
float with notify 0.

## Q2 MaximumPoliticalControl

GC +0xF0 (int, vanilla 5). The four RIP-relative reads (0x39ea26, 0x3f5500, 0x3f8483, 0x65f085) were each read [H]:
- **0x3f5470, income political term — dead branch.** Its x argument is `MaximumPoliticalControl` (int converted to
  float) only when its fourth argument is 1; otherwise it is the planet's control PD +0x68. Its only caller,
  Planet_Income 0x3f68b0 (`GALACTIC_ECONOMY.md` §1.4), passes 0. So the income curve always uses the planet's real
  control [H].
- **0x3f83b0, potential income (AI)**: the political term at full control, curve(float(GC +0xF0)). Callers 0x65cb60,
  0x6698f0, 0x669ca0: the AI income tokens (`A2I_TOKENS_RE.md`) [H].
- **0x65f000, AI token `TargetPoliticalControl`**: (int)|control| / GC +0xF0 (`AI_GALACTIC_TOKENS.md`) [H].
- **0x39e920, UI.** From 0x39d8f0. It loops i from 0 to `MaximumPoliticalControl` − 1 and appends one control pip per
  step (the faction icon at faction +0x320) to a planet label string. Pips past the shown control value (0x3f8620) are
  drawn at half colour, and a faction with flag +0x108 draws a single pip [H for the loop, M for "planet label"].

**Political-control update** [H]. The target-control function 0x3f6af0 uses a literal 5 as its upper bound and has no
read of GC +0xF0 (none of the four sites is in it). So the port's hard-coded 5 (`GALACTIC_ECONOMY.md` §4.1) matches
retail; changing `MaximumPoliticalControl` changes only the AI tokens and the pip count. Kind: **AI + UI**.

## Q3 AutomaticAutoResolve

GC bool +0x2F1: **retail-unused** [H].
- No RIP-relative operand targets 0x140b15af1. The neighbouring reads are 1-byte reads of +0x2EF (0x3c05e3), +0x2F0
  (0x3ca60b) and +0x2F2 (0x86d8c), so no wider read covers +0x2F1.
- The only access is the default word store at +0x2F0 in the defaults function (0x2c9588, chunk 0x2c92d6 of 0x2c91c0),
  which writes both flags.
- Vanilla: `No` in `GAMECONSTANTS.XML`; `true` only in `AITESTGAMECONSTANTS.XML` and `HYPERGAMECONSTANTS.XML`, which have
  no effect.

## Q3 AlwaysBypassAutoResolve

GC bool +0x2F0. **One reader**, 0x3ca60b in the battle-pending banner 0x3ca480 (callers 0x28c6c0, 0x28c7e0, 0x291330)
[H]. The banner selects the **one-button shell** (`Begin` only, no auto-resolve; `GALACTIC_DIALOGS.md` §1.4) when any of
these holds:
- `AlwaysBypassAutoResolve` is true;
- the linked-battle flag 0x140b30545 is set;
- in a story campaign (`Is_Story_Campaign`, 0x140b303c2): a space conflict with a `Prevents_Story_Campaign_Autoresolve`
  unit (Q5) or an excluded space location, or a land conflict at an excluded land location
  (`Autoresolve_Exclusion_Locations`);
- the local player's flag +0x3C0 is set;
- the auto-resolve lock 0x4472a0 (`Is_Autoresolve_Allowed` false, `RESTRICT_AUTORESOLVE`).

So `Yes` means the human must fight every pending battle from the galactic dialog, in any campaign or mode that shows the
banner [H]. It is not read by the conflict services 0x2b92e0 / 0x2b76a0 (AI-vs-AI and overrun auto-resolve unchanged)
nor by the tactical HUD handlers 0x30d710 / 0x30e2f0, so the in-battle auto-resolve button is not affected [H for the
missing reads]. Vanilla `No`. Kind: **UI** with a rules consequence for the human. It neither forces auto-resolve nor is
MP or debug only.

## Q4 SpaceAutoResolveHitRate

Type int +0xDF8 (code 0x6). Readers [H]:
- type ctor 0x367250 (8-byte zero store over +0xDF4..+0xDFB) and copy 0x36c000;
- 0x4b80c0 (renamed `PlanetData_First_Structure_AutoResolve_HitRate`): walks the planet data's structure list (PD +0x158,
  count +0x160) and returns the first non-zero `SpaceAutoResolveHitRate` of a structure type, with that type's `Damage`
  (float +0xDEC, truncated) as a second result; 0 if none.

Its only caller is the auto-resolve Add_Object vf3 0x608080, in the branch for a planet added to a **space** auto-resolve
(object byte +0x3D, set to 1 by the space init vf1 0x60d040 and 0 by the land init vf2 0x60cf30). It keeps the smallest
positive value seen (or any value while the slot is 0) in auto-resolve +0x28 and +0x30, and the damage in +0x2C [H].

**Then nothing reads them** [M]. Inside the AutoResolve class (vtable 0x1408755b8, 30 slots, and the helpers in
0x608000..0x60e400) the words +0x20..+0x30 are only written: by vf3 and by the two inits. No getter returns them (all
small virtuals were read: 0x60c6c0..0x60c6f0, 0x60c650, 0x60dd50, 0x60dd70, 0x60c600, 0x60c560, 0x60c5b0, 0x60c8b0,
0x60c900, 0x60c660, 0x562060, 0x491370, 0x491390). The resolution math (`BATTLE_TRANSITIONS.md` §4.4) does not use them.
[M] only because an outside reader would need the object pointer and no class method hands these fields out.

So **in effect retail-unused**: no hit-rate rule exists in auto-resolve. FoC data: 3 on `Ground_Empire_Hypervelocity_Gun`
and `Space_Mode_Ground_Empire_Hypervelocity_Gun`. Port: nothing to honour.

## Q4 SpaceAutoResolveStunRate

Type int +0xDF4. Same pattern [H for the writes, M for no reader]: 0x4b8120 (renamed
`PlanetData_First_Structure_AutoResolve_StunRate`) returns the first non-zero value over PD +0x158. vf3 0x608080 keeps the
smallest positive one in auto-resolve +0x20 and +0x24. Nothing reads these words (evidence as for
`SpaceAutoResolveHitRate`). FoC data: 3 on `Ground_Ion_Cannon` and `Space_Mode_Ground_Rebel_Ion_Cannon`. **In effect
retail-unused.**

## Q5 Prevents_Story_Campaign_Autoresolve

Type bool +0xA1. The byte reads at displacement 0xA1 are 10 in all. 0x3a9adc and 0x5d5d4a (three) read a struct with
fields at +0x84 / +0x94, so they are another class [M]. The rest are the readers below [H].

**Fleet flag** [H]. The galactic fleet add-member functions 0x430d90 and 0x430f30 set fleet data (fleet +0xC0) byte
**+0x95 := 1** when the joining unit's type has the flag. No byte store ever clears +0x95 (those two are its only byte
writers), so the flag stays on the fleet after the unit leaves [M: a wider clear in a ctor or on load was not checked].

**Test** 0x2affb0 (renamed `Conflict_Has_Prevents_Story_Autoresolve_Unit`) [H]. It collects both sides' objects of the
pending conflict (0x2ad100) and is true when any of them is:
- a non-fleet object whose type has the flag;
- a fleet whose data +0x95 is set;
- or a fleet one of whose expanded members (0x3965f0) has the flag or +0x95 set.

**Callers and the rule** [H]. 0x3ca480 (battle-pending banner) and the tactical HUD handlers 0x30d710 / 0x30e2f0. Each
calls the test only when `Is_Story_Campaign` (0x140b303c2) is true **and** the pending conflict is a **space** conflict
(0x2b0470), ORed with the space exclusion test (0x4471b0, is_land 0). Land conflicts never test it. A hit selects the
one-button shell (banner) or the alternate HUD branch, exactly like an exclusion hit
(`MCX4_RE_ANSWERS_GALACTIC.md` Q7.4).

**Not used by** the conflict services 0x2b92e0 / 0x2b76a0 [H]: AI-vs-AI battles still auto-resolve and overrun is not
suppressed. The flag does not persist "until won": it holds while such a unit (or a fleet it once joined) is in the
battle.

**Interaction with the lock 0x4472a0** [H]. Independent OR terms of the same banner test (Q3). 0x4472a0
(`Is_Autoresolve_Allowed` false / `RESTRICT_AUTORESOLVE`) applies to every campaign, land and space, and also suppresses
overrun. The type flag applies only in story campaigns, only to space, and only to the human's choice.

FoC data: `Death_Star`, `Accuser_Star_Destroyer`, `Moff_Falast_Star_Destroyer`, `Admonitor_Star_Destroyer`.

## Q6 Activated_*_Ability_Names

GC string lists (code 0x2A: data at list +8, count at +0x10, 0x20-byte strings). Each list has exactly one helper that
reads it [H]:

| Tag | GC offset | helper | kind id / label |
|---|---|---|---|
| `Activated_Slice_Ability_Names` | +0xFD8 | 0x2e6b20 | 1 `TEXT_ABILITY_SLOT_SLICE` |
| `Activated_Black_Market_Ability_Names` | +0xFF0 | 0x2e5740 | 9 |
| `Activated_Sabotage_Ability_Names` | +0x1008 | 0x2e6770 (also 0x3e6ff0) | 2 |
| `Activated_Neutralize_Hero_Ability_Names` | +0x1020 | 0x2e62a0 | 4 `TEXT_ABILITY_SLOT_NEUTRALIZE_HERO` |
| `Activated_Siphon_Credits_Ability_Names` | +0x1038 | 0x2e68c0 | 5 `TEXT_ABILITY_SLOT_SIPHON_CREDITS` |
| `Activated_System_Spy_Ability_Names` | +0x1050 | 0x2e6d80 | 3 `TEXT_ABILITY_SLOT_SPY` |
| `Activated_Destroy_Planet_Ability_Names` | +0x1068 | 0x2e5c20 | 6 `TEXT_TOOLTIP_ABILITY_DESTROY_PLANET_NAME` |
| `Activated_Corrupt_Planet_Ability_Names` | +0x1080 | 0x2e59b0 | 7 |
| `Activated_Remove_Corruption_Ability_Names` | +0x1098 | 0x2e6500 (also 0x3e1790) | 8 `TEXT_ABILITY_SLOT_REMOVE_CORRUPTION` |
| `Activated_Hack_Super_Weapon_Ability_Names` | +0x10B0 | 0x2e6110 | 10 |

**Helper** [H for the walk, M for "usable"]. A helper is true when the unit has an ability with one of the listed names
that applies to the planet (0x2e6fe0; it recurses into a single-unit fleet and contained units, then asks the unit's
ability manager +0x100 through 0x37c430). A fleet of more than one unit never matches.

**Users, all UI** [H]:
- 0x3d2020 (renamed `GalaxyUI_Classify_Activated_Ability`) picks, from the selection, the object with the lowest type
  +0x1660. Unless the planet is story-blocked (0x4470b0) or 0x326ab0 refuses, it tests the kinds in the order 6, 1, 9,
  2, 3, 4, 5, 7, 8, 10 and returns the first kind with its label (−1 for none).
- Its callers 0x3e8890 (a hero selected alone: shows or hides GUI component 0x5f with the kind, the unit, the planet and
  the label) and 0x3eb200 (the story-forced steal slots, `REMAINING_GAPS.md` §1.5, plus the drag hint) are galaxy-map UI.
- 0x3e1790 (from 0x30c3b0): the corruption-removal cost tooltip `TEXT_CORRUPTION_REMOVAL_DISPLAY_TEXT`, which finds the
  unit's ability by a `Activated_Remove_Corruption_Ability_Names` name and shows 0x5b1730(ability, planet).
- 0x3ec380 calls the siphon helper for a GUI component; 0x3e6ff0 reads the sabotage list in galaxy-GUI code [M for what
  it shows].

No rules or AI code reads any of these lists. The ability that fires is the unit's own ability object; the names only
choose the button and its text.

### Q6 Activated_Destroy_Planet_Ability_Names

UI only: kind 6, tested first (FoC `Death_Star_Planet_Destruction`) [H].

### Q6 Activated_Neutralize_Hero_Ability_Names

UI only: kind 4 [H].

### Q6 Activated_Remove_Corruption_Ability_Names

UI only: kind 8 and the removal-cost tooltip 0x3e1790 [H].

### Q6 Activated_Siphon_Credits_Ability_Names

UI only: kind 5 and 0x3ec380 [H].

### Q6 Activated_Slice_Ability_Names

UI only: kind 1 [H].

### Q6 Activated_System_Spy_Ability_Names

UI only: kind 3 [H].

## Q7 Build_Countdown_Timer

Type bool +0xC7. Byte reads at 0xC7 on a type pointer [H]:
- **0x401e50** (renamed `Production_Build_Countdown_Speech`; caller 0x402840, the production update [M]). For a queue
  entry whose type has the flag and a non-empty `Build_Speech_Countdowns`, when the remaining time changes [M: entry
  +0x1C previous, +0x14 current]:
  - remaining ≥ 60 s: when (int)remaining / 60 + 1 changes, it builds the event name prefix + that number + `_Minutes`;
  - 10 to 60 s: when (int)remaining / 10 changes, prefix + ((int)remaining + 1) + `_Seconds`;
  - below 10 s: every whole second, prefix + ((int)remaining + 1) + `_Seconds`.
  - (int) is truncation. If the speech event exists (0x320f80) it is played (0x320a50), and with subtitles on
    (0x140b31410) its first text line is shown in white (0x2dd630).
  - No owner test is visible, so every player hears it [M].
- **0x3d2b0** (renamed `HUD_Draw_Build_Countdown_Timers`). For every planet (GOM query, behaviour 6), for the first two
  production entries (0x401960) whose type has the flag, it draws `<Build_Countdown_Text_ID text>` then
  (r + 1) / 60, ":", (r + 1) mod 60, with r = (int)remaining. The colour is `Build_Countdown_Text_RGBA`. It has no owner
  filter [M].
- Of the other byte reads at 0xC7, 0x3d41e is this HUD; 0x2d6bdb, 0x2d7b52, 0x522407 and 0x522842 are other classes
  [M].

**Verdict: presentation only** [H]. Neither function changes the queue, the time or the cost; the build completes on the
normal production timer. FoC: `Death_Star` only.

### Q7 Build_Countdown_Text_ID

Type string +0x8D8. The type fix-up 0x3782a0 resolves it (0x1fa1a0) into the text handle at +0x8F8 when the string is
non-empty. The only reader of +0x8F8 is the HUD 0x3d2b0 (the timer label). Other hits are the ctor, copy, dtor and other
classes [H]. UI. FoC `TEXT_DEATH_STAR_COUNTDOWN_TIMER`.

### Q7 Build_Countdown_Text_RGBA

Type colour, 16 bytes at +0x918 (the copy uses one 16-byte move). Read by the HUD 0x3d2b0 (0x1f9320) as the timer text
colour [H]. UI. FoC `64, 128, 255, 255`.

### Q7 Build_Speech_Countdowns

Type string +0x15C8. Read only by 0x401e50, as the prefix of the countdown events (FoC `Speech_Death_Star_Build_`) [H].
UI (audio).

### Q7 Build_Speech_Underway

Type string +0x1550: **retail-unused** [H]. Only the ctor 0x367250, dtor 0x36a070 and copy 0x36c000 address it; the
other +0x1550 hit is the GC defaults chunk 0x2c92d6.

### Q7 Build_Speech_Completed

Type string +0x1578: **retail-unused** [H]. Same evidence (ctor, dtor, copy; +0x1568 / +0x1578 hits otherwise only in
0x2c92d6).

### Q7 Build_Speech_Stopped

Type string +0x15A0: **retail-unused** [H]. Ctor, dtor and copy only; 0x31b590 (r11, a stride-0x18 array) and 0x5b440b
(stack frame) are other classes.

## Q8 Bribe_Cost_Multiplier (fleet data +0x96)

The AI per-hop payment 0x434720 (`MCX4_RE_ANSWERS_GALACTIC.md` Q5.1). Fleet data = fleet object +0xC0. Every byte
access at +0x96 was read [H]:
- **Set** by 0x6aedc0, renamed `MovementBlockStatus_Get_Next_Hop` from its own log strings. This is the AI fleet-movement
  plan state. It plans a path from the fleet's planet to the goal (0x3f0010) and takes the next hop. When that hop has an
  orbit controller (0x3f9270) that is not the fleet's owner, and the hop is not the destination:
  - if the owner's faction has `Benefits_From_Corruption`, it sets fleet data **+0x96 := 1** and returns the hop;
  - otherwise, when the block mode (+0x58) is < 2, the plan is blocked.
- **Read** by the fleet locomotor 0x56a1e0 (0x56a6e2). It takes the toll branch (0x434720) only when the owner is
  AI-controlled (0x2814d0), the player's byte +0x62 is clear and +0x96 is set.
- **Cleared** by the same locomotor at 0x56a76a when the move goes on [M for the exact branch], and by the fleet-data
  init at 0x552935.

**Does the human reach it?** No [H]. Only the AI plan sets the flag, and the locomotor also requires an AI-controlled
owner. A human Underworld player's multi-hop move goes through `MoveThroughObjects` 0x3c4460 → 0x4313e0, which charges the
whole route up front and pays the planet owners. The AI toll is ×`Bribe_Cost_Multiplier` and has no payee.
- Kind: rules code reached only by AI fleets.
- [M]: no other byte writer exists; wider stores and save/load of +0x96 were not checked.

## Q9 Unit_Strength_Category

Ability base field +0x48 (64-bit category mask, code 0x56), returned with `Unit_Strength_Factor` by the base vf7
0x48d320. The base ctor 0x48c780 stores 0 into +0x48 and 0.0 into +0x50. None of the 91 derived ctors that call it
writes +0x48..+0x50 (all 91 bodies scanned) [H].

**Per side, the bonus table** [H]. The exchange of fire 0x60d2a0 runs once per firing side A against side B. At each call
it **clears a static 32-float table (0x140b39810) to 0**, which settles the [M] in `TACTICAL_GAPS.md` §4.7. For each
combatant of side A it takes the combatant's representative type (0x60c710). If that type has abilities (0x374da0:
`Abilities` +0x1A98 non-empty), 0x374010 (renamed `Type_Ability_Unit_Strength_Bonuses`) lists one (bit, factor) pair per
set bit of the low 32 bits of each ability's mask, but **only for abilities with mask ≠ 0 and factor ≠ 0.0**. For each
pair, table[bit] := factor − 1 when that value is greater than the entry. So entries only rise from 0, and a factor ≤ 1
has no effect. The table is per side, built from that side's own combatants.

**Use** [H, disassembly 0x60adb1..0x60aedb]. In the apply step 0x60ad90 (renamed `AutoResolve_Apply_Fire`), for each
firing unit (members, fleet unit types and the combatant itself; `BATTLE_TRANSITIONS.md` §4.4 step 1) with power p and
type t:
1. Walk the set bits of **the firing type's own `CategoryMask`** (+0x1648), lowest bit first, with a running value b
   that starts at 0.0. For each bit: if table[bit] > b, then b := table[bit] + 1.0. The compare is against b, which
   already includes the +1, so a later bit replaces b only when its entry exceeds the first entry + 1 (retail quirk).
2. If b ≠ 0, p := b × p (float32).
3. Then the step goes on as in §4.4. With target entry j < 1, the matching total −= p (bonus included). Otherwise
   d = c_j × (second factor) × p, a = min(d, R_j), R_j −= a, and p_left = (1 − a / d) × **p before the bonus**. The total
   entry −= a + p_left.

So the bonus raises the firing side's damage by the **firer's** category, not the target's (the [M] "target's category
bits" in §4.4 is wrong). It is never 1 + b summed over abilities.

**Shipped data** [H]. `Unit_Strength_Factor` is set in **no** EaW or FoC XML file. The FoC hero abilities that set a
`Unit_Strength_Category` (13 per `TACTICAL_GAPS.md` §4.7) therefore keep factor 0.0, 0x374010 skips them, the table
stays 0 and the multiplier is 1 in every vanilla battle. Kind: **rules** (auto-resolve), inert unless a mod sets the factor.

### Q9 Unit_Strength_Factor

Ability base float +0x50, default 0.0; read only through vf7 0x48d320 into 0x374010 as described under
`Unit_Strength_Category` [H]. Rule for mods: entry = max(0, factor − 1) per category bit and side, applied as above.

## Q10 Split_Income_With_Allies

`Income_Stream_Ability` bool +0x184. **Where the stream pays** [H, `SKIRMISH_SETUP.md` §5]: only in tactical skirmish.
Creation vf30 0x703990 needs the mode's income manager (mode vf +0x140), which exists only in skirmish modes, so a stream
never pays in a galactic campaign or a campaign battle [M for the galactic mode returning none].

**Create** [H]. 0x703990 creates the stream (0x537840: source type = object type, source id, owner = object owner). It
then sets:
- the base value (`Base_Income_Value` +0x178) and the base interval (`Base_Interval_In_Secs` +0x17C, via 0x6953c0);
- the mode +0x84: 1 when `Split_Favors_Owner`, with the owner share from `Owner_Income_Percentage` (kept only when in
  [0, 1], 0x695420); 2 when `Full_Amount_To_Everyone`; 0 otherwise.

**Recipients** (0x7038b0, renamed `IncomeStreamAbility_Add_Recipients`) [H]:
- without `Split_Income_With_Allies`, the owner only;
- with it, every player in PlayerList order that is allied with the owner (0x2823c0) and whose faction is `Is_Playable`
  (+0x20A8). [M] that 0x2823c0 counts the owner as its own ally.
- Recipients are a set keyed by player id, paid in ascending id order. The ability virtual 0x703ae0 (vtable 0x1408c8da0)
  rebuilds the recipients of the object's streams [M: on an owner change].

**Per-frame amount** [H, 0x6951a0]. pay = (((M + 1) × base + A) / I) / FPS, float32, in that order:
- M and A are the folds of the multiplier and additive modifier maps;
- I is the effective interval (0x694b30, below).

**Equal split (mode 0)** [H, 0x694ba0]. Every recipient gets pay / N, where N is the recipient count. A recipient whose
player no longer exists is removed while N stays as counted, so its share is lost for that frame. Each payment goes to
Add_Credits (notify 0), the player's income stat +0x7C and the stream's per-player tally +0xC0. No rounding: the shares
are float32.

### Q10 Full_Amount_To_Everyone

Bool +0x186 → mode 2: **every recipient gets the whole pay** [H]. The validator 0x703d00 clears it together with
`Split_Favors_Owner` when both are set, with a warning [H]. Without `Split_Income_With_Allies` there is a single recipient,
so it changes nothing.

### Q10 Split_Favors_Owner

Bool +0x185 → mode 1 (0x694e90, renamed `IncomeStream_Distribute_Favor_Owner`) [H]:
- owner share s = clamp(pay × `Owner_Income_Percentage`, 0, pay);
- **every other recipient gets (pay − s) / N, with N = all recipients including the owner**; the owner gets s only. So
  (pay − s) / N per frame is never paid out (retail quirk to keep);
- shares ≤ 0 are skipped.

Validator 0x703d00 [H]: with `Split_Favors_Owner` but **without** `Split_Income_With_Allies` it warns (the message wrongly
says "true") and clears both `Split_Favors_Owner` and `Owner_Income_Percentage`. The stream then pays its single
recipient in full.

### Q10 Owner_Income_Percentage

Float +0x180. It is used only in mode 1 [H]. With split on, the validator 0x703d00 clamps a value < 0 to 0 and a value
≥ 1 to 0.99, with an error. 0x695420 copies it to the stream only when it is in [0, 1].

### Q10 Interval_Multiplier

`Income_Stream_Mod_Ability` float +0x180. **Validator** 0x704880 [H]: a value ≤ 0 is an error and becomes **1.0** (not
0). Values above 1 are kept.

**Apply** (0x703fe0, renamed `IncomeStreamModAbility_Apply`) [H].
- Streams = those of the income manager whose source type is `Target_Stream_Source` (resolved at +0x190) and whose
  recipients include the mod object's owner (0x537a00 / 0x6950a0).
- Filter: `Affects_All_Allied_Sources` below.
- For each such stream, entries keyed by the mod's source object, added once per source within the stream's
  `Stacking_Category` (+0x184) bucket:
  - `Income_Multiplier` ≠ 1 → value m − 1 in the multiplier map (stream +0x18);
  - `Income_Additive_Value` ≠ 0 → value a in the additive map (+0x38);
  - `Interval_Multiplier` ≠ 1 → value im − 1 in the interval map (+0x60).
- Entries never expire (the expiry field holds all ones). The removal 0x7046e0 deletes this source's entries from the
  three maps.
- vf3 0x7045e0: service interval (int)(FPS × 5.0 + 0.5), or 0 with `Reverse_Application_Logic` (`TACTICAL_GAPS.md` §1.4).

**Fold** (0x33fb70) [H]. Within each stacking-category bucket the **largest** value is taken; the bucket maxima are then
summed in map order. The fold's own optional min / max clamps are not set for streams [M].

**Effective interval** (0x694b30, renamed `IncomeStream_Effective_Interval`) [H]. I = max(1.0, (1 + fold(interval map))
× base interval), in seconds. So 0.5 halves the interval (doubles the income rate) and 2.0 doubles it. The floor is 1 s.
Two mods in the same category do not stack (the larger m − 1 wins, so for interval cuts the smaller cut). I also sets
the stats period: next stats frame = now + round(FPS × I).

### Q10 Affects_All_Allied_Sources

`Income_Stream_Mod_Ability` bool +0x198 [H]. In apply (0x703fe0) and remove (0x7046e0), a found stream is modified only
when this flag is set **or** the stream's owner (+0xC) is the mod object's owner. So:
- without it, a mod changes only its owner's own streams of the target source type;
- with it, it also changes allies' streams of that type, but only those that list the mod owner as a recipient (allies
  who split that income with the mod owner).

## Q11 Evade_Detection_Chance

**Owning class** [H]. `Evade_Detection_Chance` is a **`Galactic_Stealth_Ability`** field (+0x178), not a
`Siphon_Credits_Ability` one. The `xml_field_tables.md` segment headed `Siphon_Credits_Ability` mixes several classes'
fields at +0x178..+0x180. In `Siphon_Credits_Ability`, +0x178 is `Min_Siphon_Percentage`. All 25 FoC uses are inside
`Galactic_Stealth_Ability` blocks, all 0.0.

The RVAs handed over in the question are other classes [H]:
- 0x70bc30: validator, "Time_Reduction_Percentage must be < 1.0";
- 0x70c260: validator, "Price_Reduction_Percentage must be < 1.0";
- 0x70e2b0: validator of a Political_Change ability;
- 0x70be20: an activation that writes the negated +0x178 into a modifier list of the target (+0x270).

**Store** [H]. Stealth activation vf30 0x6ff260 sets the object's stealth byte (0x3a92d0) and stores the chance through
0x3a86d0: clamped to [0, 1], written to the object's stealth component (object +0x100) +0x88. Deactivation 0x6ff330
writes 0 and clears the stealth byte.

**Roll** [H]. `DetectHeroAbility` service vf4 0x6f6e30 (`STRATEGIC_GAPS.md` §2.2). Candidates = stealthed objects in the
detector planet's orbit, then on its surface, in list order; the loop stops when the detector is flagged (+0x3A0 & 0x42).
Per candidate:
1. Skip an allied candidate, and skip when the detector is a generic hero (+0x14) and the candidate a named hero (+0x13).
2. A named detector and a generic candidate: **detected with no draw and no evade roll**.
3. Otherwise draw r1 = Get_Float01 0x5324d0 on the **synced LCG 0x140a13e24**. If r1 < `Detection_Chance` (+0x17C), call
   0x3a0b70 (renamed `Object_Evade_Detection_Roll`):
   - when the candidate type, the detector type and the candidate's stealth component exist, draw **r2** (same stream).
     If r2 < the candidate's `Evade_Detection_Chance`, the detection is cancelled and only the log line "has narrowly
     evaded detection" is written;
   - otherwise (or with no component, no draw) the candidate is detected.
4. Detected: 0x3a0130 (debug log "%s has been discovered by %s!" when the candidate is still stealthed) and 0x3a0110 (the
   detector's ability manager is notified, 0x37cdc0) [M for what that notification changes].

Draws per candidate: 0 (named detector against generic), 1 (r1 ≥ chance), or 2 (r1 < chance with a stealth component).
**r2 is drawn even when the evade chance is 0.0**, as on every FoC hero, so it shifts the synced stream.

**Siphon detection** [H]. With `Detect_Credit_Siphon_Effects` (+0x180), after the candidate loop: when the planet has a
siphon (PD +0x200 ≠ −1) and is held by the detector's owner or an ally, one more draw r < `Detection_Chance` discovers it.
There is **no evade roll** for the siphon. 0x3facc0 then writes "A credit siphon effect on %s has been discovered" and
sets PD +0x210 := 1, but only when the checked player (0x3956c0) is the local player [M: UI flag]; 0x3a00f0 notifies the
detector. Nothing ends the siphon, reveals the siphoning hero or refunds credits here [M].

**Port**: keep the r2 draw on the synced stream whenever r1 < `Detection_Chance` and the candidate has a stealth
component.

---

## Follow-ups for other specs (not edited here)

- `BATTLE_TRANSITIONS.md` §4.4 step 1: the bonus is looked up by the **firing** unit's `CategoryMask`, with the running
  compare quirk; the table is cleared per firing side; shipped data never sets `Unit_Strength_Factor`, so the bonus is 1
  (Q9). `TACTICAL_GAPS.md` §4.7: "initial value assumed 0 [M]" is now [H], and the 13 FoC abilities set the category
  only.
- `BATTLE_TRANSITIONS.md` §4.5: `Prevents_Story_Campaign_Autoresolve` wiring is [H] (story campaign, space only, sticky
  fleet flag +0x95, banner and HUD only) (Q5); add `AlwaysBypassAutoResolve` (banner only) (Q3).
- GRC §4.4 step 5 and `REMAINING_GAPS.md` §1.5 item 5: the corrupting player passed to 0x3f9690 is the first
  `Benefits_From_Corruption` player (0x2948d0) (Q1). `A2K_T10_CREDIT_NOTIFY_RE.md`: site 0x3fbeda is the first-owner
  instant type-3 path.
- `xml_field_tables.md`: code 0x8 is a float parse (the "int" samples mislead), and the `Siphon_Credits_Ability` segment
  mixes classes (`Evade_Detection_Chance` belongs to `Galactic_Stealth_Ability`).
- `GALACTIC_ECONOMY.md` §4.1: the hard-coded 5 is [H] (literal in 0x3f6af0, no GC read); §1.4: 0x3f5470's
  `MaximumPoliticalControl` branch is dead (Q2).
- `SKIRMISH_SETUP.md` §5 and §9 ("how an Income_Stream_Mod_Ability binds to its target stream [M]"): answered by Q10
  (source type + recipient test, `Affects_All_Allied_Sources`, per-category max, 1 s interval floor, favour-owner share
  quirk).
- `STRATEGIC_GAPS.md` §2.2: the draw count per candidate, the evade draw even at chance 0, and that a named detector
  against a generic candidate draws nothing (Q11).
- `MCX4_RE_ANSWERS_GALACTIC.md` Q5.1: the "[L for that flag's meaning]" on fleet data +0x96 is now [H] (Q8);
  `AI_GALACTIC.md` can name 0x6aedc0 `MovementBlockStatus::Get_Next_Hop`.
- Tag ledger: `AutomaticAutoResolve`, `Build_Speech_Underway`, `Build_Speech_Completed` and `Build_Speech_Stopped` are
  retail-unused [H]; `SpaceAutoResolveHitRate` and `SpaceAutoResolveStunRate` are written but never read [M].

Ghidra (shared project, checked in with keepCheckedOut): 25 functions renamed and "mcx4 t09 Q<n>" comments at the key
sites.
