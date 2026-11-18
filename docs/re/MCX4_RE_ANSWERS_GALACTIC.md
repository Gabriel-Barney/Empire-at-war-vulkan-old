# mcx4: RE answers, galactic share (Q1, Q2.1-Q2.5, Q5.1, Q7.1-Q7.6)

Answers to `MCX4_RE_QUESTIONS.md` for lane mcx4-gal (roadmap group `mcx4`, box MCX2, tasks t01, t02, t05, t07). The
tactical share (Q2.1 starbase / groundbase levels, Q3, Q4, Q5.2-Q5.3, Q6) is in `MCX4_RE_ANSWERS_TACTICAL.md`. Retail
`StarWarsG.exe` FoC x64 (Steam), Ghidra 12.1.4 through ReVa, 2026-11-18. Addresses are RVAs (image base 0x140000000).
Behaviour only, no decompiled code. **[H]** read and cross-checked, **[M]** read, one link inferred, **[L]** inferred.
"AI" in the reader column means AI planning / perception code; "rules" means game-rule code that runs for every player
(the AI may call it too); "UI" means front-end or HUD code.

## Method (how "no reader" was established)

- **Every memory operand in the image.** A capstone linear sweep of all of `.text` (not only the `.pdata` functions, so
  leaf getters without unwind data are included) indexed every instruction by displacement and by RIP-relative target.
  The sweep found the faction getters 0x33e440 / 0x33e470 / 0x33e4a0, which have no `.pdata` entry.
- **GameConstants** is one global object at 0x140b15800. No data word anywhere in the image holds its address (64-bit or
  32-bit search), so a reader either uses a RIP-relative operand into the field's bytes, or `lea`s the object (or an
  interior address) and calls a GameConstants method. The `lea` sites call only methods in 0x2c4370..0x2cb520. For each
  GC field below, the RIP-relative targets inside the field's bytes and the displacement hits inside those methods were
  listed. The methods that touch the fields are ctor 0x2c4370, defaults 0x2c91c0, dtor 0x2c5b30 and the post-load
  resolver 0x2c79d0. None of them reads a field for game use.
- **Faction / campaign / type fields** were found by displacement, checking the base register of each hit in context.
  For the faction, the ctor is 0x33a580 and the dtor 0x33c342. For the campaign, the ctor is 0x4ae110, the dtor
  0x4ae7a0, the copy 0x478447 and the post-load resolver 0x479400.
- The tag strings themselves are referenced only by the field tables (`docs/re/ghidra/strings_xrefs.csv`: no code xref
  for any tag of this batch) [H].

## Summary

| Tag | Reader (RVA) or retail-unused | Effect | Kind | Conf |
|---|---|---|---|---|
| Campaign `Special_Case_Production` | 0x3fae10 (load) → planet list read by the build gate 0x2804d0 step 24 | adds `Build_Requires_Initial_Placement` types to the planets they may be built on; the faction column is ignored | rules | H |
| GC `MaximumSpecialStructures` (+0xFC) | 0x65e540 only | divisor of the AI token `MaxStructureSlots` (normalised); caps nothing | AI | H |
| GC `MaximumSpecialStructuresLand` (+0x100) | **retail-unused** | none (default 3) | | H |
| GC `MaximumSpecialStructuresSpace` (+0x104) | **retail-unused** | none (default 3) | | H |
| GC `Progressive_Taxation` (+0x150) | **retail-unused** | none | | H |
| GC `Income_Redistribution` (+0x178) | **retail-unused** | none | | H |
| GC `Skirmish_Buy_Credits` (+0x1184C) | 0xeddd0 (buy-units dialog open) | the pre-game buy budget, only in the "no game mode" branch, which no single-player opener reaches | UI | H (M unreachable) |
| GC `First_Strike_Extra_Damage_Percent` (+0x120A8) | **retail-unused** | none (default 1.0) | | H |
| GC `First_Strike_Particle` (+0x12098) | resolved post-load (0x2c79d0), then **never read** | none | | H |
| GC `Random_Story_*` (11 tags, +0x600..+0x6E0) | init 0x4c0420, builder 0x4bfdc0 | the RANDOM_STORY story reward only; no shipped FoC file uses that reward | rules (story) | H |
| Type `Cheap_Bribe_Cost` / `Expensive_Bribe_Cost` (+0x2350 / +0x2354) | 0x4313e0 (via MoveThroughObjects 0x3c4460), 0x434720, 0x3cb080 | per-unit toll a corruption faction pays to route a fleet past planets it does not hold in orbit | rules + UI | H |
| Faction `Credits_Accumulation_Factor` (+0x3A8) | **retail-unused** | none | | M |
| Faction `Corruption_Tactical_Production_Price_Multipliers` (+0x3020) | 0x33e470 ← 0x496d80 | × tactical unit price in campaign battles, **only while a price modifier is active** | rules (+AI callers) | H |
| Faction `Corruption_Tactical_Production_Time_Multipliers` (+0x3008) | 0x33e4a0 ← 0x496f00 | × tactical unit build time in campaign battles | rules | H |
| Faction `Corruption_Tactical_Build_Pad_Price_Multipliers` (+0x3050) | 0x33e440 ← 0x3744e0 | × pad build price in campaign battles at a corrupted planet (entry 1 only) | rules (+AI callers) | H |
| Faction `Corruption_Tactical_Build_Pad_Time_Multipliers` (+0x3038) | **retail-unused** | none | | H |
| Campaign `Is_Listed` (+0xC9) | 0x478f90, 0x479750 | hides the campaign from the skirmish / multiplayer / campaign lists and the default pick; the tutorial list ignores it | UI | H (M which screen) |
| Campaign `Autoresolve_Exclusion_Locations` (+0x348) | 0x28dbe0 → story manager list, read by 0x4471b0 | no auto-resolve for the human at listed (planet, land/space) pairs, story campaigns only, until the local player wins there | rules + UI | H |
| Campaign `Corruption_Level_Override` (+0x1D0) | 0x28dbe0 | at campaign start, sets the planet corrupted (Racketeering) when the value is ≥ 1 and runs the corruption-complete effects | rules | H |
| Faction `Can_Win_By_Destroying_Super_Weapon` (+0x20A9) | **retail-unused** | none | | H |
| Campaign `Is_Autoresolve_Allowed` (+0x378) | 0x28dbe0 → story manager +0x41, read by 0x4472a0 | `False` = every human-involved battle must be fought; AI-vs-AI still auto-resolves | rules + UI | H |

---

## Q1 Special_Case_Production

**Parse** [H]. Campaign field table, code 0x37 (the same list kind as `Starting_Forces`), member +0x1A0. It is a
DynamicVector: data +0x1A8, count +0x1B0, entries of 0x70 bytes. The value is `Faction, Planet, Type`, for example
`Empire, Atzerri, Ground_Hutt_Palace_E`. One element is one entry, and repeated elements append (MCX1 Q1). The entry
layout is the `Starting_Forces` one:
- owner name string at +0x40;
- planet type at +0x60 and object type at +0x68, both resolved after load by the campaign manager 0x479400 [M for the
  resolver, H for the fields the reader uses].

**Reader at load** [H]. 0x3fae10 runs per planet in L8 pass 2 (0x292050, `A2K_T07_STORY_LOAD_RE.md` §1 step 6). It runs
only when the planet has the production behaviour (slot 6), and **before** the starting-force loop. For each entry, in
file order:
1. The entry's planet type must equal this planet's type.
2. The object type must have `Build_Requires_Initial_Placement` (type +0xC3).
3. The type must not already be in the planet's list (0x4b83d0).
4. Then the type is appended to the planet-data list at PD +0x108 (data +0x110, count +0x118).

The owner name (+0x40) is **never read** here, so the faction column has no effect. Entries whose type lacks the flag
are dropped. No object is created and no story event is raised.

**Reader in play** [H]. The galactic can-build gate 0x2804d0, final step 24 (`STRATEGIC_GAPS.md` §1.3). It passes
only if at least one of these holds:
- the planet has no planet data;
- T lacks `Build_Requires_Initial_Placement`;
- T is in that planet's PD +0x110 list (0x4b83d0).

So a flagged type is buildable **only on the planets the campaign lists it for**. Which faction may build it still
comes from the type's `Affiliation` (gate step 2): FoC ships `_E` / `_R` variants per faction. The gate is shared by
the human production path (enqueue 0x3fff70) and the AI planners (0x664210, 0x6639f0, the token `CanAdvanceTech`), so
the list is game rules that the AI consults.

**Other readers** [H]:
- 0x4b83d0 has exactly two callers (0x2804d0 and 0x3fae10);
- the planet-data save 0x4bbef0 writes the list and the load chunk 0x4b92e0 reads it back, so it persists in saves;
- the ctor 0x4b5700 and the dtor 0x4b62b0 initialise and free it;
- no other code reads PD +0x110 / +0x118.

No price or build-time change is attached.

**Data** [H]. Base FoC flags exactly five types: `Ground_Cantina_E/_R/_U` and `Ground_Hutt_Palace_E/_R`. The campaign
files list only the `_E` / `_R` cantinas and palaces, so `Ground_Cantina_U` is never buildable through this path.

**Port.** Build the per-planet list at campaign start from the entries whose type has the flag (ignore the faction
column, dedupe), and save it with the planet. Then let gate step 24 pass when T is in the planet's list. Today the port
does not model the list: `strategic.cpp` step 24 fails every flagged type (a recorded [port]), so FoC's cantinas and Hutt
palaces are never buildable in the port.

## Q2.1 MaximumSpecialStructures

Every RIP-relative operand into GC +0xF0..+0x10F was listed [H]:
- `MaximumPoliticalControl` (+0xF0): 0x39e9ba, 0x3f5470, 0x3f8483, 0x65f000;
- `MaximumStarbaseLevel` (+0xF4) and `MaximumGroundbaseLevel` (+0xF8): these belong to the tactical file;
- **+0xFC: one read only, 0x65e564 in 0x65e540**, the AI perception token `MaxStructureSlots` (normalised). It divides
  the planet's ground-structure slot maximum (PD +0x230, from type +0x92C) by GC +0xFC and fails when GC +0xFC = 0
  (`AI_GALACTIC_TOKENS.md`, row MaxStructureSlots).

No build check, upgrade check or HUD reads it, and no GC method reads it. The defaults function 0x2c91c0 writes 1;
vanilla XML sets 5. So it **caps nothing**: the real slot caps are per planet, `Special_Structures_Land` (type +0x92C →
PD +0x230) and the orbital maximum (PD +0x234, 0x372760), tested by gate step 18. Kind: **AI** (token only). [H]

### Q2.1 MaximumSpecialStructuresLand

GC +0x100: **retail-unused** [H]. No RIP-relative operand targets +0x100..+0x103. The only GC-method access is the
default write of 3 in 0x2c91c0.

### Q2.1 MaximumSpecialStructuresSpace

GC +0x104: **retail-unused** [H]. Same evidence: no RIP-relative operand targets +0x104..+0x107, and 0x2c91c0 writes the
default 3. +0x108 (`MaximumFleetMovementDistance`) is read at 13 sites, so the scan does see this region.

## Q2.2 Progressive_Taxation

GC +0x150 (code 0x2F, a numeric list; vanilla `1,0, 3,.1, 20,.5, ...`): **retail-unused** [H]. Evidence:
- no RIP-relative operand targets any byte of +0x150..+0x177, and no `lea` takes an address in that range. The other
  code-0x2F list, `Diminishing_Firepower` +0x117F0, is read through exactly such a `lea` (0x3a9f7a), and so is
  `Political_Income_Curve` +0x128 (0x3f5470, 0x3f8483);
- inside the GC methods only the ctor 0x2c4370, the defaults 0x2c91c0 (clears the list) and the dtor 0x2c5b30 touch
  +0x150..+0x170.

This confirms the [M] in `GALACTIC_ECONOMY.md` §1.5 ("Taxes") for the whole binary, not only the income path.

## Q2.2 Income_Redistribution

GC +0x178 (float): **retail-unused** [H]. No RIP-relative operand targets +0x178..+0x17B. The nearest read is +0x17C
`Credit_Cap_Per_Planet` at 0x288fe5. The only GC-method access is the default write of 0 in 0x2c91c0.

## Q2.3 Skirmish_Buy_Credits

GC +0x1184C (int, vanilla 2000). One reader: 0xee1fc in the buy-units dialog open 0xeddd0, which copies it into the
start budget 0x140a62214 and the current budget 0x140a62218. The defaults function writes 2000 [H].

This is **one budget, for the local player only**. It is used only by the dialog's "no game mode" (pre-game) branch:
Buy spends it, Sell refunds, Sell All resets to the start budget, Next starts the map. No opener runs before a game mode
exists, so in single player the value is never seen. The only openers are tactical-skirmish command-bar callbacks, and
those use the player's credits instead. The multiplayer lobby path is out of scope. Full detail:
`SKIRMISH_SETUP.md` §2.7 [H for the code, M that the branch is unreachable]. Kind: **UI**.

**Port:** nothing to honour in single player.

## Q2.4 First_Strike_Extra_Damage_Percent

GC +0x120A8 (float, vanilla 1.5): **retail-unused** [H].
- No RIP-relative operand targets +0x120A8..+0x120AB.
- The neighbours are read: +0x12090 `Sensor_Jamming_Time` (0x380bf0), +0x12094 `Stealth_Detection_Time` (0x38d730,
  0x39b140) and +0x120AC `Garrisoned_Max_Attack_Distance_Multiplier` (0x3749d0, 0x3857f4).
- The only access is the default write of 1.0 in 0x2c91c0.
- There is no "first strike" rule anywhere: no damage path reads it, and no `lea` points into +0x12098..+0x120AF.

### Q2.4 First_Strike_Particle

GC +0x12098 (code 0x1D, a particle type reference, vanilla `First_Strike_Particle_Effect`). The post-load resolver
0x2c79d0 resolves it (0x4c4ec0), and the ctor / dtor construct and free it. Then **nothing reads it**: no RIP-relative
operand, no `lea` outside the GC methods. **Retail-unused** [H].

## Q2.5 Random_Story_Triggers, Random_Story_Rewards, Random_Story_Max_Triggers, Random_Story_*_Construction / _Destroy / _Reward_*_Buildable / _Reward_*_Unit

The rules are in `STORY_REWARDS_GALACTIC.md` §1.15 [H structure], and the port has them (mcx6, `story_random_story.cpp`).
New in this read:
- **Readers** [H]. Only two functions read the eleven GC fields:
  - the generator init 0x4c0420 reads +0x600 `Random_Story_Triggers`, +0x680 `Random_Story_Rewards` and the eight side
    lists (+0x620 .. +0x6E0);
  - the builder 0x4bfdc0 reads +0x618 `Random_Story_Max_Triggers`.
- **When the generator is set up** [H]. 0x4c0420 runs from the galactic mode init 0x4b21f0, in every galactic game. It
  binds the trigger and reward lists, and picks the side lists by the **local player's faction**:
  - Rebel uses the `Rebel` lists, the Mon Mothma portrait and the `RC0..._MON...` speech names;
  - **every other faction, Underworld included, uses the `Empire` lists** and the Tarkin dialog strings
    (`EC0..._TAR...`, `TEXT_STORY_TRANSMISSION_18`).
  - It also allocates one "used" byte per entry of the buildable reward list.
- **What starts a story** [H]. The builder 0x4bfdc0 has one call site, 0x4584a4, in the story reward handler 0x456970:
  the `RANDOM_STORY` reward (0x3F). Nothing else builds a random plot: no Lua function, no timer, no mode.
- **Live in FoC?** **No shipped FoC file uses `RANDOM_STORY`.** A search of the extracted FoC text finds `Random_Story`
  only in `GameConstants.xml` (the eleven tags). So the system is wired but dormant in vanilla; only a mod's story
  reward can start it [H].
- **RNG** [H]. Every draw uses the free (unsynced) RNG 0x140a13e20 through Get_Int 0x1ffb40:
  - the trigger count n = one draw in [1, `Random_Story_Max_Triggers`];
  - then, per trigger i = 0..n−1, the **first draw** picks the trigger type uniformly, an index in [0, list size − 1]
    of `Random_Story_Triggers` (0x4bec90);
  - the parameter draws per type follow, listed in `STORY_REWARDS_GALACTIC.md` §1.15 [M];
  - a pick that cannot be built recurses into 0x4bec90 for a new pick (the self-call at 0x4bf795) [M];
  - the final event draws the reward (0x4be420).
- **Max triggers** [H]. Only the inclusive upper bound of n (vanilla 3). It does not cap the stories per game: each
  `RANDOM_STORY` reward builds a new `Random_Plot_<k>`.

Kind: **rules** (story). Port impact: none beyond §1.15; the port's own stream is a recorded [port] choice.

## Q5.1 Cheap_Bribe_Cost / Expensive_Bribe_Cost

**Parse** [H]. GameObjectType ints at +0x2350 (`Cheap_Bribe_Cost`) and +0x2354 (`Expensive_Bribe_Cost`). The type ctor
0x367250 sets both to **1**, and the type copy 0x36c000 copies them. Only three functions read them, and all three use
the same rule:
- the toll counts a planet when its **orbit controller** (0x3f9270) is not the moving fleet's owner. 0x3f9270 returns
  the starbase owner when the starbase level is > 0; else the owner of the first eligible non-hidden fleet in orbit;
  else the first hidden one; **else none**, and "none" also counts as not the mover;
- unit cost = `Cheap_Bribe_Cost` when the **planet owner's** faction (planet object +0x58) has `Easily_Bribed`
  (faction +0x10C), else `Expensive_Bribe_Cost`. The type is the unit type (0x3973b0) of each unit of the fleet,
  expanded through fleets and transports (0x3965f0);
- toll = the int sum over the units. For an AI-owned mover it is then scaled: `(int)((float)sum × M)`, truncated, with
  M = the difficulty adjustment's `Bribe_Cost_Multiplier` (+0x4C, 0x4b0500; vanilla 0.25 in all three adjustments).
  That closes the [L] "not traced" row in `AI_GALACTIC.md` (Difficulty table).

The three readers:
1. **Human route charge** [H], 0x4313e0 (per planet), called by `MoveThroughObjectsEventClass` execute 0x3c4460, the
   galactic multi-hop move command (`COMMANDS.md` id 10).
   - Applies only when the mover's faction has `Benefits_From_Corruption` (+0x10B), which in vanilla is Underworld.
   - For every **intermediate** waypoint (path entries 1 .. count−2, so neither the first nor the last), the planet's
     toll is added to the total and to a per-player tally keyed by the planet's owner.
   - If the total > the mover's credits (float compare), a refusal message is posted (0x2d5290, text 0x140b301c8) and
     the move is dropped.
   - Otherwise the mover is charged the total and each planet owner receives its tally (0x27f370, notify 0, as in
     `A2K_T10_CREDIT_NOTIFY_RE.md`, where these were "raid fleet move [M]").
   - Kind: **rules**.
2. **AI per-hop payment** [H], 0x434720, called from the fleet locomotor 0x56a1e0 when a moving fleet reaches a blocked
   waypoint.
   - Only for fleets owned by AI players (0x2814d0 true, player +0x62 clear) whose fleet data (object +0xC0) has byte
     +0x96 set [L for that flag's meaning].
   - Requires a corrupted planet (0x3a0910) and a mover faction with `Benefits_From_Corruption`.
   - If the toll ≤ the owner's credits, the owner pays it (Add_Credits −toll, notify 1; no payee) and the fleet may pass;
     otherwise the move stops there.
   - Kind: **rules** code that only AI-owned fleets reach (not AI planning).
3. **Route preview** [H], 0x3cb080, from the galaxy-map UI 0x3e1500. It sums the same per-unit costs over the
   intermediate planets of the planned route, but it counts only planets whose orbit controller exists and is not the
   local player, that are corrupted (PD +0x2F4 ≠ −1), and only when the local faction has `Benefits_From_Corruption`.
   It applies no AI factor. So the preview can show less than 0x3c4460 later charges (planets with no orbit controller,
   uncorrupted planets). Kind: **UI**.

**No tactical reader** [H]. There is no +0x2350 / +0x2354 access outside the five sites above (ctor, copy and the three
readers), so the tactical bribe ability `Tactical_Bribe_Ability` (0x5b07c0) does not use these fields. Its own price
source was not traced here.

## Q7.1 Credits_Accumulation_Factor

Faction float +0x3A8 (Rebel 0.95, Empire 0.85, others 1.0): **retail-unused** [M].
- **The faction class itself never reads it** [H]. The faction methods (0x330000..0x342000) access +0x3A8 once: the ctor
  0x33a580 writes 1.0 there with one 8-byte store that also zeroes `Maintenance_Cost` +0x3AC. There is no getter.
- **No faction copy reads it** [H]. No faction copy exists: the faction-only field +0x20A9 is touched by the ctor alone.
  No 16-byte (SIMD) load starts in +0x398..+0x3A8 on any non-stack base. The 8-byte loads at +0x3A8 are pointer fields
  of other classes (list owners such as 0x30770, 0x3b7b60, 0x3baaf0) [M for the attribution].
- **Every other float read at displacement 0x3A8 belongs to another class** [M for the attribution]. All nine sites
  were checked:
  - 0x1e8970, 0x1e99e0: a runtime class whose +0x3A8 is written at run time by 0x1e7b30;
  - 0x34ffae, 0x350360, 0x350df0: GUI components, through +0x80;
  - 0x4d6fd0, 0x4d9300: written at run time by 0x4d6ba0;
  - 0x622b80: the object type, through object +0x298.
- **The income path never reads it** [H]. `GALACTIC_ECONOMY.md` §1.3 already showed this; it is now checked for every
  instruction in the image.

## Q7.2 Corruption_Tactical_Production_Price_Multipliers

**Shape** [H]. Code 0x13, a float list: data +0x3028, count +0x3030. The getter is 0x33e470(faction, i): it returns
entry i, and 1.0 (constant 0x1407ffaf8) when i is out of range. The index is **0 = battle planet not corrupted, 1 =
corrupted**; vanilla is `1.0, 1.0` for every faction, so there is no effect in base FoC.

**Reader** [H]: the tactical production price 0x496d80(build manager, type). The build manager belongs to the player
whose id is at manager +0x30.
1. base = (float) 0x3743f0(T): `Tactical_Build_Cost_Multiplayer` in skirmish, else `Tactical_Build_Cost_Campaign`,
   falling back to Multiplayer when < 1.
2. Look T up in the manager's price-modifier map (+0x78).
   - **Not found** → return base. **No corruption factor.**
   - Found but empty (0x282450) → the entry is erased, return base.
   - Found and non-empty → r = Σ over modifier sources of the largest value of that source (0x33fb70 with
     GreaterThan / Plus). Then m = 1.0, and when a parent galactic mode exists (0x28aa00), i.e. a campaign battle:
     c = the battle planet (0x28a890) is corrupted (0x3a0910), and m = getter(owner faction, c). Return
     **(1 − r) × base × m**, float32, multiplied in that order.

So the price multiplier applies **only while the player has an active tactical price reduction for that type**, for
example `Reduce_Production_Price_Ability`. That is a retail quirk to keep. `Benefits_From_Corruption` is not checked.

**Callers of 0x496d80** [H]:
- the tactical enqueue 0x496a20 and 0x497140 (rules);
- 0x2e0c20 and 0x309af0 (HUD tooltip / button state [M]);
- the AI TacticalBuild score 0x727d30 and commit 0x728000 (AI).

## Q7.2 Corruption_Tactical_Production_Time_Multipliers

**Shape** [H]: as above. Data +0x3010, count +0x3018; getter 0x33e4a0; same 0 / 1 index; vanilla `1.0, 1.0`.

**Reader** [H]: the tactical production time 0x496f00(build manager, type).
1. If the player's flag +0x3C0 is set, return 5.0.
2. Else base = `Tactical_Build_Time_Seconds` (type +0xA00, as float) × GC `Tactical_Build_Time_Multiplier` (+0x11734).
3. For an AI player in space or land mode, × the difficulty's space (+0x2C) or land (+0x30) build-time factor.
4. f = 1 − Σ (the same fold over the time-modifier map +0x38) if a non-empty entry exists, else 1.0.
5. m = 1.0, or, when a parent galactic mode exists, the getter at index c (battle planet corrupted). Here m applies
   **unconditionally**, unlike the price.
6. Return **f × base × m**.

Callers: 0x496a20 and 0x497140 (rules). Kind: **rules**.

## Q7.2 Corruption_Tactical_Build_Pad_Price_Multipliers

**Shape** [H]: data +0x3058, count +0x3060; getter 0x33e440.

**Reader** [H]: the pad price 0x3744e0(type, player). This is the price `BKG1_RE_ANSWERS.md` b0138 marked [M: corruption].
- In skirmish: `Tactical_Build_Cost_Multiplayer`, with no factor.
- Otherwise, cost = `Tactical_Build_Cost_Campaign`, or Multiplayer when < 1.
- If there is a battle planet and it is corrupted, and the player has a faction: m = getter(faction, **1**). Otherwise
  m = 1.0, and **entry 0 is never read**.
- Result: (int)((float)cost × m), truncated.

**Callers:**
- the human pad build 0x42edf0 → 0x42dbd0 (rules);
- 0x2fee40 and 0x309af0 (HUD [M]);
- the AI TacticalBuild 0x727d30 / 0x728000 (AI).

## Q7.2 Corruption_Tactical_Build_Pad_Time_Multipliers

Faction +0x3038 (data +0x3040, count +0x3048): **retail-unused** [H]. The ctor 0x33a580 and the dtor 0x33c342 construct
and free it. **No getter exists**: the three tactical getters cover +0x3008, +0x3020 and +0x3050 only. No instruction
anywhere uses displacement +0x3040 or +0x3048 on a non-stack base; the only hits are stack locals of 0x5b440b. The pad
build time has no corruption factor.

## Q7.3 Is_Listed

**Parse** [H]. Campaign bool +0xC9. The ctor 0x4ae110 sets it to **true** (one word store of 0x0101, which also sets
`Is_Quickmatch_Allowed` +0xCA); the copy 0x478447 copies it.

**Readers** [H], both in the CampaignDataManager (global 0x140b31160):
- 0x478f90(manager, out list, mp, listed_only) collects every campaign with `Is_Multiplayer` (+0xC8) == mp and, when
  listed_only is set, `Is_Listed`. For single player (mp = 0) it keeps one campaign per `Campaign_Set` (+0x208 [M: the
  set's CRC]).
- 0x479750(manager, listed_only, mp_only) returns the first campaign that passes the same filters, else the map's first
  campaign.

**Callers and their arguments** (read from the call sites) [H]:

| Caller | What | listed_only | mp |
|---|---|---|---|
| 0xfb950 | campaign / map list builder of the setup screens (callers 0xf1230, 0xf2690, 0x943c0, 0xb4b10, 0xb6410, 0x2bf10 [M for which screen is which]); it also skips `Tutorial` (+0x260) campaigns | 1 | its argument |
| 0xfb950 → 0x479750 | default campaign when none is selected (0x479210 null) | 1 | 1 |
| 0xc7ca0 | multiplayer lobby list | 1 | 1 |
| 0x70f70 | Steam lobby `Set_Map_Name` lookup | 1 | 1 |
| 0xe4300 | a list filled into a control named `IDC_STAGING_MAP_LIST` (callers 0xc6590, 0xea3a0) that keeps only campaigns with `Tutorial` (+0x260) set, with checked-box icons [M: the tutorial chooser] | **0** | 0 |

So `Is_Listed = false` hides a campaign from the skirmish, multiplayer and campaign lists and from the default pick. The
tutorial list does not filter on it, which fits the eight FoC tutorials (all `Is_Listed false`) still being offered [M]. Nothing
else reads +0xC9: the other byte reads at displacement 0xC9 are other classes. Story campaigns (`Full_Story_Campaign_*`,
also unlisted) are started by name from their own menu entries [M]. Kind: **UI**.

## Q7.4 Autoresolve_Exclusion_Locations

**Parse** [H]. Campaign +0x348, code 0x2A (a string list: data +0x350, count +0x358, 0x20-byte strings). Read as
**pairs** `planet, land|space`, for example `Fondor, land, Geonosis, land, Jabiim, space, Jabiim, land`
(`Campaigns_Alpha.xml`).

**Reader at load** [H]. 0x28dbe0, L14 (`A2I_T01_FRAME_ORDER.md` §2): 0x28d690 copies the strings into the galactic story
manager (0x140b30350) list +0x268 (data +0x270, count +0x278), replacing what was there. The story manager save /
load 0x447310 writes and reads that list [M].

**Test** [H]. 0x4471b0(story manager, planet, is_land) walks the pairs and is true when the name matches the planet
type's name (type +0xF8, `_stricmp`) and the kind matches "land" (is_land) or "space" (`_stricmp`). Its callers:
- 0x3ca480, the battle-pending banner. The test runs **only when `Is_Story_Campaign` is true** (campaign +0x37A, copied
  at L14 to 0x140b303c2). Land is tested when the pending conflict is a land one (0x2b0310), space when it is a space
  one (0x2b0470, ORed with 0x2affb0) [M for those three meanings]. A hit selects the one-button shell (`Begin` only,
  auto-resolve hidden; `GALACTIC_DIALOGS.md` §1.4).
- 0x30d710 (land) and 0x30e2f0 (space), the tactical HUD retreat / resolve handlers. The same story-campaign gate and
  the same kind choice apply; on a hit (or a story lock, or in MP or skirmish) they skip their normal callback (+0x28A0)
  and take the `retreat_clicked` branch [L for what the UI then shows].

**Removal** [H]. 0x448cb0 deletes the matching pair. It is called when **the local player wins** a battle there, in
single player only:
- the land "won" story event 0x32f4d0 (is_land 1);
- the space one 0x32f570 (is_land 0).

So each excluded battle must be fought until the human wins it once.

**Not read by the auto-resolve services** [H]. 0x4471b0 has only the three callers above, so AI-vs-AI battles and
overrun auto-resolve ignore the list.

Kind: **rules** (story manager) plus **UI** (the dialog and HUD that consult it). Vanilla uses it only in the EaW story
campaign file `Campaigns_Alpha.xml`.

## Q7.4 Corruption_Level_Override

**Parse** [H]. Campaign +0x1D0, code 0x2B: a list of `name, int` pairs, entries of 0x28 bytes (name string +0x0, int
+0x20), data +0x1D8, count +0x1E0. Vanilla example: `Endor, 1` in `Campaigns_Underworld_Exec_Demo.xml`.

**Reader** [H]. 0x28dbe0 at L14, right after the flags and the exclusion copy, when a galactic mode exists. For each
entry in order:
1. Find the type by name (0x407c00) and its first object in the galactic mode (0x2a87d0). Skip the entry when there is
   no such object, no planet behaviour (slot 3) or no planet data.
2. type := **3 (Racketeering) if the int ≥ 1, else −1**. Only ≥ 1 versus < 1 matters, so `1` and `5` do the same.
3. PD +0x2F4 := type, written at once: no gain timer, no flags.
4. 0x3ff230 rebuilds the corruption visuals (`FIXPASS_GALACTIC.md`).
5. 0x3f9690(planet, new type, −1, planet owner) runs the **corruption-complete** effects, the same routine as a
   finished corruption (0x3fc5b0):
   - per player with a faction, the faction's per-type record (0x33e310) is applied for that planet (0x27f9a0);
   - the planet owner gets the planet type's success credit bonus for type 3 (0x3716b0 > 0 → Add_Credits, notify 0),
     the planet type's per-type record (0x3715d0 → 0x27f9a0) and its per-type object type (0x371610 → 0x27f860, only
     when that type's cost is > 0);
   - then 0x37cff0 and the notifier 0x3f98a0.

   The calls are read [H]; what the records grant (abilities, unlocks) was not traced [M].

The owner is the planet owner after L8 (planet object +0x58). With type −1 the effects are skipped: only the −1 / −1
notifications run [M]. Kind: **rules**.

**Port:** apply at campaign start after the starting forces: set the planet corrupted (Racketeering) without a timer,
then run the same completion effects as a finished corruption, credit bonus to the owner included.

## Q7.5 Can_Win_By_Destroying_Super_Weapon

**Parse** [H]. Faction bool +0x20A9, next to `Is_Playable` +0x20A8; vanilla `Yes` on Rebel and the expansion Rebel.

**Verdict: retail-unused** [H]. The only instruction with displacement 0x20A9 is the ctor store in 0x33a580. No 2-, 4-
or 8-byte load covers +0x20A9: the widest faction reads near it are 0x33e6c0 at +0x2098 / +0x20A0. The flag decides
neither a tactical battle nor the campaign.

The super-weapon outcomes that do exist come from other code, not from this flag [M]:
- the Death Star planet kill (`TSW_PLANET_DESTROY.md`);
- the game-over reasons `Galactic_Super_Weapon_Destruction`, `..._Destroys_Last_Enemy_Planet` and
  `..._Kills_Enemy_Leader` (enum `VictoryType`, converter 0x341ff0).

## Q7.6 Is_Autoresolve_Allowed

**Parse** [H]. Campaign bool +0x378. The ctor 0x4ae110 sets it to **true** (one word store with `Show_Completed_Tab`
+0x379); the copy 0x478447 copies it. Vanilla sets `True` in the three story campaigns.

**Reader** [H]. 0x28dbe0 at L14 stores story manager +0x41 (0x140b30391) := **(value == false)**. The same block copies
+0x379 to 0x140b305e8 and +0x37A to 0x140b303c2. Other writers of +0x41:
- 0x28f590, the linked-battle setup, sets it to 1 at two sites;
- the pre-shutdown hooks 0x3b7aa0 and 0x4d7540 clear it;
- the story manager save / load 0x447310 keeps it [M].

`BATTLE_TRANSITIONS.md` §2.9.9 item 3 ("no reader found") missed the this-pointer read: 0x4472a0 below.

**Effect** [H]. 0x4472a0(story manager, planet), the auto-resolve lock, is true when +0x41 is set **or** the planet is
in the `RESTRICT_AUTORESOLVE` list (+0x300). So `False` locks **every** planet. The lock's callers:
- the battle-pending banner 0x3ca480: the one-button shell, so the human cannot auto-resolve;
- the HUD handlers 0x30d710 / 0x30e2f0, as in Q7.4;
- the conflict services 0x2b92e0 (space) and 0x2b76a0 (land):
  - single player: the lock only **suppresses the overrun check**. A battle with no human (AI vs AI) is still
    auto-resolved at once, because the "not human-involved" branch does not test the lock (0x2b92e0, state 4). The land
    service 0x2b76a0 follows the same pattern (`BATTLE_TRANSITIONS.md` §1.6) [M for land];
  - multiplayer: the lock also feeds the lobby-option logic; that branch was not traced (out of scope).

**Interaction with `Autoresolve_Exclusion_Locations`** [H]:

| | `Is_Autoresolve_Allowed = false` | `Autoresolve_Exclusion_Locations` |
|---|---|---|
| scope | every planet, land and space, any campaign | only the listed (planet, land/space) pairs, and only when `Is_Story_Campaign` |
| lifetime | the whole campaign; also forced on by a linked battle | until the human wins that battle once (0x448cb0) |
| AI-vs-AI battles | still auto-resolved | still auto-resolved |
| overrun auto-resolve | suppressed | not affected (not tested by the services) |

The UI consequence for the human is the same: no auto-resolve choice. Kind: **rules** (story manager, conflict services)
plus **UI**.

---

## Follow-ups for other specs (not edited here)

- `A2K_T07_STORY_LOAD_RE.md` §1 step 6: the second list is `Special_Case_Production` (Q1), so "list meaning L" becomes
  [H]. `STRATEGIC_GAPS.md` §1.3 step 24: the PD +0x110 list is filled from it at L8.
- `STORY_REWARDS_GALACTIC.md` §0 table, row +0x41: "not set by a reward found" is right. The setters are the campaign
  load (`Is_Autoresolve_Allowed`) and the linked-battle setup 0x28f590 (Q7.6). `BATTLE_TRANSITIONS.md` §2.9.9 item 3 is
  answered by Q7.6.
- `AI_GALACTIC.md`, Difficulty table: `Bribe_Cost_Multiplier` +0x4C is read by 0x4313e0 and 0x434720 (Q5.1).
- `A2K_T10_CREDIT_NOTIFY_RE.md`: 0x3c4460 is the `MoveThroughObjects` route toll (mover −, planet owners +), not a
  "raid fleet move". 0x434720 is the AI per-hop toll, with no payee.
- `GALACTIC_ECONOMY.md` §1.3 and §1.5 ("Taxes"): `Credits_Accumulation_Factor`, `Progressive_Taxation` and
  `Income_Redistribution` are retail-unused in the whole binary (Q2.2, Q7.1).
