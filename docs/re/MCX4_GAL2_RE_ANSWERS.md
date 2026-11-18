# mcx4-gal2: RE answers, galactic leftovers (Q1-Q6)

Answers to `MCX4_GAL2_RE_QUESTIONS.md` for lane mcx4-gal2 (roadmap groups `mcx4` and `mcx3`, Stage 9). Retail
`StarWarsG.exe` FoC x64 (Steam), Ghidra 12.1.4 through ReVa plus REA, 2026-11-18. Addresses are RVAs (image base
0x140000000). Behaviour only, no decompiled code. **[H]** read and cross-checked, **[M]** read with one link inferred,
**[L]** inferred. "AI" = AI planning or perception; "rules" = game-rule code that runs for every player; "UI" =
front-end, HUD or presentation. GC = GameConstants (object 0x140b15800). GRC = `GALACTIC_REVEAL_CORRUPTION.md`.

## Method

As in `MCX4_RE_ANSWERS_GALACTIC.md` ("Method"). For this batch:
- **GameConstants +0x10C..+0x10F**: a byte-level superset scan of every executable section listed each ModRM byte
  with RIP-relative addressing whose target falls in the field's bytes (for 0, 1, 2 or 4 immediate bytes after the
  displacement). Each candidate was then checked in Ghidra. The GC methods (ctor 0x2c4370, defaults 0x2c91c0, dtor
  0x2c5b30, resolver 0x2c79d0) are covered by the earlier method.
- **Object byte +0x39F** (Q3): REA `search_bytes` for the displacement bytes, then every instruction around each of the
  39 hits was disassembled (REA `read_function_instructions`). Each GameObject read was then read in context.
- **Player +0x368** (Q3): masked byte searches for `cmp` / `mov` / `lea` / `inc` with displacement 0x368 and any
  register, then each hit was read in context.
- **Object byte +0x3A0 bit 0x04** (Q4): a masked search for `or byte [reg+0x3A0], 4`.
- Shipped data: `extracted/foc_data_text.zip` and `eaw_data_text.zip`, searched in full.

## Summary

| Question | Answer | Kind | Conf |
|---|---|---|---|
| Q1 `TradeRouteMovementFactor` (GC +0x10C) | only reader 0x662b30: the edge length of the AI connectivity graph; feeds the four tokens `FriendlyInternalConnectivity`, `FriendlyExternalConnectivity`, `EnemyInternalConnectivity`, `EnemyExternalConnectivity`, which **no shipped equation uses** | AI | H |
| Q2 `Corruption_Level_Override` order | load L14 (0x28dbe0) first, then the planet's first political update 0x3fb040 in the tick-1 planet service; neither tests the current type; the type-3 credit bonus is paid twice, the upgrade and unlock adds are idempotent | rules | H (M load-time services) |
| Q3 `Hyperspace_Fleet_Reveal_Range` / `Bribery_Fleet_Reveal_Range` | the vote 0x433090 sets only "visible to the local player" (object +0x39F, render show / hide) and the hyperspace SFX; **presentation only**; type-0 skip is a "≠ 0" test; Bribery planets of any owner count | UI | H |
| Q4 hero death in an auto-resolve | combatant vf+0x120 → Take_Damage 0x3a9e30 → 0x3ab890 → Die 0x39bdb0, whose galactic branch calls 0x48eb10 directly (respawn scheduled, default time); **not** 0x3a01f0, so no STORY_DEFEAT_HERO. A hero ship lost in a fought battle is removed by 0x2b5360 at battle end through 0x3a01f0 | rules | H (M two links) |
| Q5 `Indigenous_Spawn_Destruction_Reward` (GC +0x11E34) | only via the story reward SET_SANDBOX_OBJECTIVES of the sandbox plots, in the human's campaign **land** battles; one `CHECK_DESTROYED` + `STORY_TRIGGER` pair per hostile spawner **type**; 500 credits to the **local player**, once per type | rules (story) | H (M treasury) |
| Q6 MEG directory listing | find 0x217e80 / next 0x218e40 walk each MEG's **record table in stored order** and take each name through the record's name-index field; no CRC check, so a listed name can be unreadable | rules (VFS) | H |

---

## Q1 TradeRouteMovementFactor

**Verdict** [H]. GC +0x10C (0x140b1590c) has one reader, 0x662b30. It is the edge length of the AI connectivity graph,
whose only consumers are four perception tokens. No shipped perceptual equation uses those tokens, in FoC or in EaW. They appear only in
`Data/XML/Enum/PerceptionTokenType.xml` and in a commented-out line of EaW `OffensiveGalaticEquations.xml`. So the
constant changes nothing in vanilla play; it matters only for a mod equation that uses one of the four tokens.
Kind: **AI**.

**Reader scan** [H]. The superset scan found one real operand in the field: the divisor at 0x140662c39 inside
0x662b30. The other 13 candidates were the reads of `MaximumFleetMovementDistance` (+0x108) seen with the wrong
immediate width; Ghidra's own reference list of 0x140b1590c agrees.

### Q1 Edge length 0x662b30(evaluator E, planet a, planet b, player P) [H]

1. a = b → 0.
2. Each planet's political-control record is tested by 0x4ad9f0. The record is the planet behaviour's vf+0x98 record:
   control as a float and the controlling player id. A planet passes when control > 0 and P's relation table
   (+0x370) entry for that id is 0, that is P itself or an ally; id −1 fails. The test applies to a, and also to b
   when E's internal flag (+0x84) is set. A failure gives −1.
3. d = 3-D distance between the planet objects' positions (+0x78 / +0x7C / +0x80), float with sqrt.
4. If a and b are linked by a trade route (0x3f7b30), the result is d / `TradeRouteMovementFactor` (double), with no
   length limit.
5. Otherwise the result is d when d ≤ `MaximumFleetMovementDistance` (inclusive), else −1 (no edge).

So with vanilla `0.5` a trade-route hop counts as **twice** its straight-line length. `HyperGameConstants.xml` and
`AITestGameConstants.xml` set `1.0`, the plain length.

### Q1 The connectivity objects and their recompute [H]

- 0x6625e0(player, internal flag) builds a 0x88-byte object holding the player id (+0x80), the flag (+0x84), a map
  from planet id to matrix index and a result map from planet id to value (+0x40).
- The galactic perception system's vf6 0x4e2060 creates the objects, but only in the first perception system (ref
  count 0x140b31a98). It makes two per player, in PlayerList order: internal (list 0x140b31aa0) and external (list
  0x140b31ab8). 0x4e3e30 builds the same lists [M: the load path]. 0x4e4520 frees them with the last system.
- Recompute 0x662c90(E):
  1. Planets: every GOM category-3 object (0x2a9ff0), filtered by 0x662710. A planet is kept when it has at least one
     edge, of length ≥ 0 in either direction, with another planet of the list. The GOM order is kept.
  2. An n × n matrix of 0x662b30(E, i, j, P). −1 (none) and 0 (i = j) are "no edge". With the internal flag the lower
     triangle reuses the value of (j, i).
  3. 0x662140 computes all-pairs shortest paths (Floyd–Warshall, k outermost) on a copy, with a predecessor matrix.
     An entry is replaced when it is still −1 or the new sum is strictly shorter, so ties keep the first path.
  4. For every ordered pair (u, v) with u ≠ v and a path between them (internal objects: only u < v), the walk goes
     back from v to u through the predecessors and adds 1 to every planet strictly between them.
  5. value(planet) = its count / the largest count (0 when the largest is 0). It is stored under the planet data's id
     (PD +0x354). A planet removed in step 1 has no value.
- When: once at the creation in 0x4e2060, and for every player on **every derived-owner change of any planet**. The
  planet service 0x3fe1b0 calls 0x4e4670 when 0x3f8820 differs from PD +0x1A0; that call also reruns the island
  analysis 0x4e4850. The recompute does not run each frame.

Meaning: an **internal** value says how often the planet lies on shortest routes between two planets held by P or its
allies, so it marks a choke point of P's own territory. An **external** value counts shortest routes that start from
P's planets and go anywhere. Those graphs are directed: only edges from held planets exist.

### Q1 The four tokens (planet evaluator 0x65f7b0, visible leaves, cache life 15 s) [H]

| Token | Leaf | Value |
|---|---|---|
| `FriendlyInternalConnectivity` | 0x65c160 | Self's internal value of the planet; 0.0 when it has none |
| `FriendlyExternalConnectivity` | 0x65c120 | Self's external value; 0.0 when none |
| `EnemyInternalConnectivity` | 0x65c040 | the largest internal value of the planet over every player that is not friendly to Self (relation ≠ 0, so neutrals too); players without a value are skipped; the maximum starts from the caller's initial output, 0.0 at the known sites [M] |
| `EnemyExternalConnectivity` | 0x65bf70 | the same with external values |

The getter is 0x4e1dc0, which looks up the player's object, then 0x662a60, the hash lookup by PD +0x354. Self is the
evaluator context's bound player (context +0x58 → +0x50) [M]. No leaf fails. The token ids come from the
registration 0x5b43d0; the names are the strings at 0x1408a4bd0..0x1408a4c30.

**Port.** `engine/ai` needs these only for mods. Build the per-player internal and external graphs as above. Use edge
length d / `TradeRouteMovementFactor` for linked pairs and d for unlinked pairs within the movement distance. Count
the shortest-path transit, normalise by the maximum and recompute on every planet ownership change. Vanilla can leave
the tokens at 0.

## Q2 Corruption_Level_Override order

**Order at campaign start** [H]:
1. **Load, L14 of 0x28dbe0** (load thread, before tick 1; `A2I_T01_FRAME_ORDER.md` §2). The override loop (call
   site 0x28e77f) writes PD +0x2F4 and calls 0x3f9690(new 3, old −1). P is the owner held in planet +0x58, as set at
   L8.
2. **Tick 1, T5**: the planet behaviour service 0x3fe1b0 (vf6) runs for each planet. Its call to 0x3fb040 at
   0x3fe530 is the planet's **first political update**, because PD +0x1C9 is still 0. [M: no planet service runs during
   the load, since no tick runs, A2I_T01 §2.] Earlier in the same service, the corruption completion 0x3fc5b0 does
   nothing for an overridden planet: the override sets no gain flag and no timer.

The other callers of 0x3fb040 are `Change_Owner` 0x3f7280 (story / Lua), 0x3f8060 (story 0x45eae0 and 0x61b790 /
0x61b9f0) and the revolt branch of 0x3f98a0. They do not run at a normal campaign start. The notifier 0x3f98a0 that
ends 0x3f9690 reaches 0x3fb040 only for a revolt (old type 4, new −1), so the override does not trigger it.
Exception [M]: a story reward fired during the load can run that planet's first update at load time, before L14. One
case is a CHANGE_OWNER reward of an L10 starting-tech event (`A2I_T01` §3), which calls 0x3f7280 → 0x3fb040. The two
writes then come in the other order, with the same effects.

**Neither path tests the current type** [H]:
- the override writes PD +0x2F4 := 3 (value ≥ 1) or −1 (value < 1) at once;
- the first update (the block after 0x3fbda0, reached once per planet) checks whether the owner held in planet +0x58
  has `Benefits_From_Corruption`. If so it writes PD +0x2F4 := 3 **whatever the current value**, rebuilds the visuals
  (0x3ff230) and inlines the type-3 completion effects with P = that owner (`MCX4_T09_RE_ANSWERS.md` Q1).
- 0x3fb040's owner-change branch is the one that does test the type: it starts a gain only when PD +0x2E2 is clear
  and +0x2F4 = −1, and a removal only when +0x2F4 ≠ −1. The first call skips that branch, because the owner was
  written before the snapshot (`Q6_T24_TR_GCMENU.md` §3).

**A planet that gets both** (Underworld-owned, override ≥ 1) [H]:

| Effect | Load (0x3f9690) | Tick 1 (0x3fb040) | Net |
|---|---|---|---|
| each player's faction record `Automatic_Planetary_Corruption_Upgrade_Type`[3], via 0x27f9a0 | added | added | **once**: 0x27f9a0 does not add an identical (planet id, type) pair |
| type-3 success credit bonus (0x3716b0 > 0 → Add_Credits 0x27f370, notify 0) | paid | paid | **twice** |
| planet-specific tactical upgrade (0x3715d0), via 0x27f9a0 | added | added | once (same dedup) |
| unlock unit (0x371610, cost > 0), via 0x27f860 | added | added | once: 0x27f860 returns when the type is already listed |
| 0x37cff0: each ability of the planet type gets vf+0xC8(planet, old −1, new 3) | yes | yes | twice [M: what the abilities do with it was not traced] |
| notifier 0x3f98a0 (old −1, new 3) | yes | yes | nothing (acts only for old 4 → new −1) |
| visuals 0x3ff230 | yes | yes | rebuilt twice, same result [M] |

FoC data: no planet has a credit bonus on a Racketeering slot (`MCX4_T09_RE_ANSWERS.md` Q1), so the doubled bonus is
0 + 0 in vanilla.

Corollaries [H]:
- An override value < 1 on a planet whose owner has `Benefits_From_Corruption` does not keep it clean: tick 1 makes
  it type 3 anyway.
- An override on a planet of a non-corruption owner gets only the load effects (already `MCX4_RE_ANSWERS_GALACTIC.md`
  Q7.4).

**Port.** Run the override at load. At the first political update of each planet in tick 1, apply the
`Benefits_From_Corruption` rule with no type test. Pay the credit bonus on each run and keep the upgrade and unlock adds
idempotent.

## Q3 Hyperspace_Fleet_Reveal_Range

`Hyperspace_Fleet_Reveal_Range` (type +0x64C) and `Bribery_Fleet_Reveal_Range` (GC +0x11F7C) are read only by the
fleet visibility vote 0x433090 (FleetBehaviorClass vtable 0x86a958, slot 0xA0).

**Verdict: presentation only** [H for the reader chain, M for the labels of two readers]. The vote decides two things:
- whether the local player's client shows a fleet it does not own: the object byte +0x39F "visible to the local
  player", the render object shown or hidden, and through them the galaxy-map icon, the planet tooltip count, the
  radar and the cinematic camera;
- whether the `Galactic_Fleet_Hyperspace` SFX plays when that fleet departs.

No AI, rule, conflict or story code reads the vote or the byte. AI perception uses its own fogged leaves (with
`AIUsesFogOfWarGalactic` False the AI sees everything anyway, `AI_GALACTIC_TOKENS.md` §0). A hidden fleet has no shown
render object, so the human cannot see or click it on the map [M]; no rule tests that.

### Q3 The helpers [H]

- **Player +0x368**: an int saved with the player (Save 0x286760 chunk 8, Load 0x284fe0 case 8). The player ctor
  0x27df90 sets it to 0 and the copy 0x27e4a0 copies it. The byte searches found no other store. Two readers compare
  it with 1: the vote, and the galaxy-map planet panel 0x3e3a20, which then treats the planet as fully known [M]. It
  is a "sees everything" player flag that only a loaded save can set [M]. In a fresh game it is 0 and the early-out
  never fires.
- **0x4329c0(behaviour, fleet)**: the planet object at locomotor path index + 1. The locomotor is fleet +0xA8, with
  path ids at +0xB8 and the index at +0x80. Since the hop runs path[index] → path[index + 1] (`BATTLE_TRANSITIONS.md`
  §3.3a), this is the current hop's destination [M]. It returns null when the fleet has no path.
- **0x375270(type)**: true when the type's `CategoryMask` has `Super`. FoC: only `Death_Star`.
- **0x3a9760(object)**: "hidden". True when flag +0x3A0 bit 1 is set. For a fleet it is true when every member is
  hidden (0x4357d0, non-empty fleets only), for a transport 0x4f4610 decides, otherwise the component +0x100's byte
  +0x98.
- **0x2814d0(player)**: the player has an AI system (+0x360).
- **0x397450(object)**: corruption type PD +0x2F4; −1 when the object has no planet data.
- **0x2aa200(GOM, id)**: the GOM revealer list (+0x218; objects whose type has +0x64C > 0, registered at creation),
  filtered to owner = id.

### Q3 The vote 0x433090(fleet behaviour, player Q) [H]

L = the local player, F = the fleet, O = F's owner (F +0x58).
1. Q ≠ L → **true** (the vote is only meaningful for L).
2. F has no fleet data pack (+0xC0) or sits inside a container (+0x2B0) → **false**.
3. O = L, or L +0x368 = 1 → **true**.
4. If F is not hidden, the hop destination N (0x4329c0) exists and N's owner is L → **true**.
5. Any content of F (0x3965f0, nested) of a `Super` type → **true**.
6. F hidden → **false**.
7. The current mode is not galactic (mode vf+0xE0 ≠ 0; GalacticModeClass returns 0 at 0x17d070) → **false**.
8. The candidates, in this order: L's revealers (0x2aa200), then, only when a player with a `Benefits_From_Corruption`
   faction exists (0x2948d0), every planet (GOM category 3) whose corruption type is 5 (Bribery).
9. For each candidate C whose corruption type (0x397450) **is not 0**:
   - r = `Bribery_Fleet_Reveal_Range` when C's type is 5, else C's type +0x64C;
   - if the squared 3-D distance between F and C is < r² (float, strict):
     - when O exists and has an AI system: **true** if the fleet data's destination planet (+0x50) has the reveal
       byte PD +0x2C9 = 1; otherwise go on to the next candidate;
     - otherwise (no owner player, or an owner without AI): **true** if the path planet at the index or at index − 1
       (when the index is > 0) has PD +0x2C9 set; otherwise go on.
10. **false**.

### Q3 Who calls the vote and who reads the result [H]

- **0x3a51e0** (GameObject visibility refresh) acts only when its player is L and global 0x140b1574e is clear. Every
  behaviour of the object votes through vf+0xA0. If all vote true, the object byte +0x39F := 1 and the render object
  is shown (0x38c730); if any votes false, +0x39F := 0 and the render object is hidden (0x3a5840). Then every behaviour
  gets vf+0xA8. It has 45 call sites in 26 functions. Fleet create / merge / arrive / leave: 0x4309a0, 0x430d90,
  0x430f30, 0x4310d0, 0x4338d0, 0x434940, 0x434d30, 0x435020, 0x4352f0, 0x4358c0, 0x42dbd0, 0x42df80, 0x42e6d0,
  0x42ecd0. Transports: 0x4f3430, 0x4f40c0. Political update 0x3fb040, campaign start 0x28dbe0, conflicts 0x2b76a0
  and 0x2b92e0, 0x2b5a30, the mode switch 0x2b59b0, story rewards 0x456970, spies 0x70f510 and 0x70fb90, and 0x4b2600.
- **0x388b60**, called from object creation 0x29f810: the same vote at creation.
- **0x39f380 / 0x39f580** (galactic move orders) play `Galactic_Fleet_Hyperspace` when L owns the fleet, the vote is
  true, or L has the tutorial flag +0x3C0.
- **Readers of +0x39F**: the whole-image displacement scan found 39 byte hits, and every GameObject read among them is
  listed here.
  - render object build and update: 0x3a4820, 0x3a8ff0, 0x3a0930; show / hide setters 0x39f280 / 0x39d6a0;
  - planet tooltip text (0x39e920 ← 0x39d8f0, `" - %d%%"`, `TEXT_FLAGSHIP`): counts the visible fleets in orbit;
  - the fleet's destination marker 0x435c30;
  - the radar / minimap pass 0x35b4d0 (← 0x365a80);
  - the "interesting object" cinematic camera 0x2cd0d0 / 0x520df0;
  - planet behaviour 0x3fd560 [M: per-player presentation];
  - a GOM thinning pass driven by an unsynchronised LCG, 0x2a7190 [M];
  - the tactical projectile step 0x4ce6a0;
  - the object ctor 0x3989a0 (default 1), the copy 0x472560, save / load 0x39ce40 and 0x3a5ad0.

  None of them is AI, selection rule, combat, conflict, economy or story code.

### Q3 Bribery_Fleet_Reveal_Range

**Why type 0 is skipped** [H]. The candidate filter is "corruption type ≠ 0", not "≠ −1"; the code has no other
reason [L: it reads like a slip for −1]. The effect:
- a non-planet revealer (a hero) has no planet data, gives −1 and is kept;
- an uncorrupted revealer planet gives −1 and is kept;
- a revealer planet corrupted with **Intimidation** (type 0) is **skipped**. FoC: Bothawui (range 150) has
  `Corruption_1_Type` = `Corruption_Intimidation`, so an Intimidation-corrupted Bothawui stops revealing.

**Which planets the Bribery range covers** [H]:
- every planet whose corruption type is 5, **whoever owns it and whoever corrupted it**: the corruption state keeps no
  corrupter;
- the list is built only when some player's faction has `Benefits_From_Corruption`. 0x2948d0 returns the first such
  player, which need not be the local player.

So with an Underworld AI in the game, its Bribery planets also reveal passing fleets to a Rebel or Empire human [M
that this shows in play]. A revealer planet that is itself under Bribery uses the Bribery range instead of its own.

FoC values: `Bribery_Fleet_Reveal_Range` 75.0; `Hyperspace_Fleet_Reveal_Range` on Bothawui 150, `Darth_Vader` and
`Darth_Vader_Expansion` 75 (two `SpecialStructures` entries set 0.0).

**Port.** Put the vote in the presentation layer (the client's per-local-player fleet visibility and the hyperspace
SFX). Keep it out of sim state and out of the CRC.

## Q4 hero respawn after auto-resolve

### Q4 Units killed by a galactic auto-resolve

**The kill call** [H]. The resolution's kill paths call the combatant's vf+0x120 with arguments (1, damage
1.0e18 (float bits 0x5d5e0b6b), killer combatant). The killer is the first combatant of the other side
(`A2M_R2_RE_B.md` §2.1). The three paths are the survival roll 0x60a140, the space transport losses 0x60aac0 and the
early withdrawal 0x60cdb0. vf+0x120 differs by combatant class:

| Combatant (vtable) | vf+0x120 | Effect |
|---|---|---|
| `CombatantBehaviorClass` (0x8b21d0), a unit's own | 0x638e70 | Take_Damage 0x3a9e30(the unit, 1, 1e18, killer object) |
| `SquadronCombatantClass` (0x8bf898) | 0x6cbd70 | the same for every member of the squadron fleet (0x432730 / 0x432710), then for the combatant's object |
| `CompanyCombatantClass` (0x8bfa70) | 0x6cc150 | the same for every content of the transport (0x4f3ba0 / 0x4f3b80), then for the transport |
| `BaseCombatantClass` (0x8bf6c0) | 0x6cb930 | not a kill: lowers the base level (0x3fe750 / 0x4bb1a0) |

Take_Damage 0x3a9e30 reaches the death 0x3ab890 when health runs out (call site 0x3ab184), and 0x3ab890 calls Die
0x39bdb0 [H calls; M that the 1e18 hit always gets there, e.g. for invulnerable objects].

**What Die does for a hero** [H]. The galactic conflict services 0x2b92e0 / 0x2b76a0 run the auto-resolve while the
galactic mode is current, so Die's mode test (vf+0xF8 = 1 in GalacticModeClass) passes. For `Is_Named_Hero` or
`Is_Generic_Hero`, Die then calls **0x48eb10(D, planet none, time 0, skip 0)** before it removes the object (0x391310).
- Planet: D's planet (0x3963f0 [M]), which is the conflict planet for a unit in a fleet in orbit or on the surface [M].
- Time 0 → `Default_Hero_Respawn_Time` (GC +0x11F0).
- skip 0 → the parent tests of `MCX4_RE_ANSWERS_TACTICAL.md` §13 apply.

So **yes, a named hero killed by an auto-resolve schedules its respawn** at that planet with the default time.

**But it does not go through 0x3a01f0** [H]. The callers of 0x3a01f0 are 0x2b5360, 0x379df0, 0x431860, 0x4f3a20,
0x6f8c20 and 0x702790; Die reaches it only through 0x4f3a20, for the hero contents of a dying TRANSPORT. A hero unit
killed directly by the auto-resolve therefore skips 0x3a01f0's other steps:
- `STORY_DEFEAT_HERO` (0x326f80, whose only caller is 0x3a01f0);
- the "Hero %s has been defeated by %s!" log;
- the faction-leader victory tests 0x346460 types 5 and 8, which apply when the hero's type is its faction's
  type +0x2AC8. [M] Whether another test covers this was not traced; 0x4b2ef0 is one of the other callers of 0x346460.

Die writes the final-blow byte +0x330 itself (the killer's owner).

**Carried heroes** [M]. `CompanyCombatantClass` kills a transport's contents first, then the transport. Each content
hero dies through Die → 0x48eb10 with the transport as parent. While another content is still active the respawn is
refused, unless `Respawn_Whole_Team_When_Killed` is set; so the last member to die schedules the team. The transport's
own death then passes its remaining hero contents to 0x3a01f0 (0x4f3a20), and normally none remain. In space,
transports carrying a named hero always survive the auto-resolve (`BATTLE_TRANSITIONS.md` §4.4 step 4).

**Auto-resolve pressed in a tactical battle** (`IDC_AUTORESOLVE`) [M]: the kills happen in the tactical mode, where
vf+0xF8 = 0. Nothing is scheduled at the death, and the fought-battle path below applies.

### Q4 A hero ship lost in a fought space battle whose fleet survives

[H unless tagged]
1. The tactical death: Die in SpaceModeClass (vf+0xF8 = 0) schedules nothing.
2. Parent-mode death marking 0x2b2cc0 (`OBJECT_RUNTIME.md` §2.5): the galactic counterpart with the same type gets
   +0x3A0 |= 0x04, and the final-blow byte is copied [M: a hero ship that is a direct fleet member takes the same-type
   branch]. 0x2b2cc0 and 0x2a6840 (from the battle-end tail 0x3639d0, which marks a side's units) are the only writers
   of that bit.
3. Battle end, 0x28bf10 kind 2 (space). After the orbit split 0x2ad100 it calls **0x2b5360** over the galactic GOM.
   This is the same pass as the land end, so §13's "land battle end" label is too narrow. The pass removes every object
   whose flags & 6 = 4:
   - objects that are neither fleets nor transports first: a hero (0x39a600, named or generic) goes through
     **0x3a01f0(hero, none)**. That raises `STORY_DEFEAT_HERO` and the log, calls 0x48eb10(hero, the conflict planet,
     `Default_Hero_Respawn_Time`, skip 0) and runs the leader tests. Then the story DESTROY notification 0x327070 is
     raised and 0x391310 removes the object from its fleet;
   - then marked fleets and transports, the same way;
   - last, objects with +0x3A6 set are removed.
4. 0x2b4660 (for each side's first fleet) then refills partial fighter squadrons. It moves members between partial
   squadrons of the same owner and squadron type. A squadron whose alive fraction is ≥ `Squadron_Refill_Threshold`
   (GC +0x1171C) gets its missing members recreated (0x29f810); one below the threshold is destroyed (0x431860, which
   sends named-hero contents through 0x3a01f0).

So the surviving fleet loses the hero ship at battle end, and **the death reaches 0x3a01f0**.

**Port.** Auto-resolve deaths take Die's direct schedule. Do not raise `STORY_DEFEAT_HERO` for them, and do not run
`hero_defeated`'s extras. Fought-battle losses are removed at battle end through `hero_defeated` with the conflict
planet. The respawn itself (planet, time, parent tests) is the same in both.

## Q5 Indigenous_Spawn_Destruction_Reward

**When 0x32bd10 runs** [H]:
- 0x32bd10 has one caller, the story reward switch 0x456970 case 0x65, `SET_SANDBOX_OBJECTIVES`. That case runs it on
  the reward's plot's story manager (event +0x228 → plot +0x630).
- Shipped FoC uses that reward only in the three sandbox plots `SANDBOX_STANDARD.XML`, `SANDBOX_LAND_DESTROY.XML` and
  `SANDBOX_LAND_CAPTURE.XML`. In each, the event `Sandbox_Set_Sandbox_Objectives` fires it at `STORY_ELAPSED` 2 s.
- Those plots are loaded by 0x329530, and 0x329530 is called only from the battle start 0x28fda0. That is the
  galactic-to-tactical transition, used by the conflict services 0x2b92e0 / 0x2b76a0, the linked-battle start 0x28f590
  and the dialog / load paths 0x84ab0, 0x92940 and 0xa9840.
- 0x28fda0 makes the call only when all three hold:
  - the new tactical mode's story flag +0x2CF is set. Global 0x140b30544 sets it: `LINK_TACTICAL` clears that global,
    and 0x28fda0 sets it back to 1 after each start;
  - the local player is not a tutorial player (+0x3C0);
  - the parent conflict state (GOM +0x618) is not 9.
- The plot choice:
  - local player defending, or any space battle → `sandbox_standard.xml`;
  - local player attacking in a land battle → `sandbox_land_capture.xml` when the battle object 0x140b30570 has
    CAPTURE_POINT (0x3D), else `sandbox_land_destroy.xml`.
- The plot is owned by the **local player** (plot +0x638 := 0x294a40) and becomes the story manager's sub-plot (+0xD8).

So the builder runs in the human's **campaign / galactic-conquest tactical battles**. It does not run in skirmish
(0x28fda0 is not on that path [M]), in the story linked battle of conflict state 9, or for a tutorial player.

**Land only** [H]. When the mode is space (mode vf+0xE0 = 2), 0x32bd10 clears the flag that guards both the
planetary-credit-bonus objective and the spawner objectives.

**Per spawner type** [H]. 0x5315e0 reads the global indigenous-spawner list (0x140a43690, count 0x140a43698) and keeps
the **first object of each type**. For each kept spawner S of type T:
1. Text: S's type's `Enemy_Spawn_Text` (list +0x19E8, getter 0x371a90) when S is not friendly to the local player
   (0x39a2c0), else `Friendly_Spawn_Text` (+0x19D0, 0x371b20). An empty text ends this spawner; the getter logs
   "does not have enemy / friendly spawn text defined".
2. 0x326460 adds the text as a story-manager objective, with a flag set when S is not hostile [M: the flag's meaning].
3. Only when S is **hostile** to the local player (0x39a4e0, relation 1), the builder adds two events to the sub-plot:
   - **`<T>_00`**: `CHECK_DESTROYED` (0x2E), active at once. Param1 is the local player's faction name, Param2
     `DESTROY_ALL_INDIGENOUS_SPAWNERS` (keyword 4), Param3 T's name. Reward `OBJECTIVE_COMPLETE` (0x55) with the
     objective text. It is polled every T9 and fires when no live object of type T is left in the spawner list
     (`SCRIPT_GAPS.md` §4.8), whoever destroyed them.
   - **`<T>_01`**: `STORY_TRIGGER` (0x22) with prereq `<T>_00`. Reward `CREDITS` (4), whose `Reward_Param1` is
     `Indigenous_Spawn_Destruction_Reward` formatted with `%d` (FoC "500"). It fires in the same call as `<T>_00`
     (`STORY.md` §1.2).
4. After the loop, when at least one pair was made, 0x530180 rebuilds the sub-plot's type buckets and prereq links,
   so the new events are dispatched.

**Who is paid, how often, which credits**:
- `CREDITS` (0x456970 case 4) pays atof(param) to the **plot's player**, which here is the **local player**: Add_Credits
  0x27f370 with notify 1, plus GUI flashes 6 and 0xF2 [H]. The player that destroyed the spawners and the other humans
  get nothing.
- **Once per spawner type** per battle [H]: the events are named by type and the list holds one entry per type. Two
  spawner types pay twice.
- The amount goes to player +0x70, the player's single credit field, which the galactic economy uses too. In a campaign
  battle that is the galactic treasury [M: no per-battle copy of +0x70 was found]. In a tactical mode Add_Credits clamps
  to the credit cap +0x74, unless the cap is negative or the `SKIRMISH_RULES` flag 0xb155c5 is set
  (`STORY_REWARDS_GALACTIC.md` §2.5).

Separately, the same land block adds a `TEXT_TACTICAL_PLANETARY_CREDIT_BONUS` objective (formatted with the planet
type's int +0x7D0). It is added when the battle planet 0x140b30428 is not the local player's and 0x2825b0 is false
[L: what 0x2825b0 tests]. That objective is text only and is not the spawner reward.

**Port.** In the sandbox objectives of a campaign land battle, for each hostile spawner type create the
`CHECK_DESTROYED` and `STORY_TRIGGER` pair and pay `Indigenous_Spawn_Destruction_Reward` to the local player once
when the last spawner of that type dies. Do nothing in space, skirmish, linked battles of state 9 or tutorials.

## Q6 MEG directory listing

**Functions** [H]. The MegaFileManager find API is find-first 0x217e80, find-next 0x218e40 and close 0x2199d0, with
WIN32_FIND_DATAA output (`UI.md` §9.2). Its users include:
- the map table 0x47cb40, scanning `*.TED`;
- the AI plan scan 0x4e7e30 and the evaluator scan 0x5e8220, scanning `*.lua`;
- the shader preload 0x18a880.

**How a MEG is enumerated** [H]:
1. Loose files come first, through `FindFirstFileA` / `FindNextFileA`. Every returned path is remembered in a
   per-search set.
2. Then each mounted archive is walked in list order. Archives flagged +0x140 and archives with no records are skipped.
3. Within an archive the walk is i = 0 .. record count − 1, the count at reader +0xC8 that `bsearch` also uses. Entry
   i's name is taken from **name table[(16-bit) record i field +0x10]** (0x240470): the record's name-index field,
   read as a ushort.
4. The name is normalised by 0x23f240: trim (0x2005e0), upper case (0x200ba0), `/` → `\`, then split at the last `\`
   into directory and file. The pattern is normalised the same way.
5. If the entry's directory lies deeper than the pattern's directory (0x21bd60), its first sub-directory below the
   pattern directory is returned as a **directory** entry (attributes 0x10). That happens when the sub-directory
   matches the wildcard and was not returned before.
6. If the directory equals the pattern's directory and the full normalised path is not in the set yet, the file part
   is matched against the wildcard (0x21b8c0, `*` and `?`). On a match the file part is returned in upper case
   (attributes 0) and remembered. The search state (archive, index) is kept for 0x218e40.

The record's CRC, its index field (+4), the table's sort order and `bsearch` play no part. So the listing comes from
the **stored name table, reached through the record table**, and is **not filtered through the CRC lookup**. A listed
name can be unreadable.

| Defect | Listing (0x217e80 / 0x218e40) | Reading the listed name (Open_File → 0x23f880, `MCX3_MEG_LOOKUP.md`) |
|---|---|---|
| records not sorted by CRC | listed | may be missed by `bsearch` → a lower layer's copy, or not found |
| stored CRC ≠ CRC of the stored name | listed | not found under that name, or another record's data when some record carries the name's CRC |
| two records with one CRC, different names | both listed | the name whose CRC that is reads whichever record `bsearch` stops at, so possibly the other record's data |
| two records with the same name | listed once (the per-search set) | as above |
| name stored with `/` or in lower case | listed as upper case with `\` (the directory match works) | found only when the stored CRC is the CRC of the normalised upper-case `\` form, because the read hashes the normalised request |
| name index ≥ name count | out-of-range read, undefined [M: no bound check in 0x240470] | — |
| more than 65 535 names | indices alias (16-bit read) | — |
| a name no record refers to | never listed | — |

**Port.** Directory listings should walk each archive's records in stored order and take names through the name index,
normalised (upper case, `\`). Deduplicate per search by normalised path and do not drop entries that fail the CRC
lookup. The port's `meg_*` findings already count those entries as unreadable (`MCX3_MEG_LOOKUP.md` §2).

---

## Follow-ups for other specs (not edited here)

- GRC §1.6 and open item 8 (Q3):
  - +0xC0 is the fleet data pack (every fleet), not a "moving" flag, so the vote also runs for fleets in orbit;
  - a `Super` content makes the fleet visible, rather than being required to be absent;
  - the AI test is on the **fleet's** owner;
  - the revealed-planet test is the fleet's destination (fleet data +0x50) for an AI owner, and the path planet at
    index or index − 1 otherwise;
  - the result is presentation only.
- `TRU1_C_RE.md` §1.5 (Q5): `<name>_00`'s reward text is the spawner's `Enemy_Spawn_Text`, not
  `TEXT_TACTICAL_PLANETARY_CREDIT_BONUS` (that is a separate objective); the pair is land-only, one per spawner type,
  paid to the local player.
- `STORY_REWARDS_GALACTIC.md` §2.7: the spawner part of the 0x32bd10 body is decoded here (Q5); the `Sandbox_Objective_1..4`
  part is still open.
- `OBJECT_RUNTIME.md` §2.5 "when flagged galactic objects are destroyed": at battle end, 0x28bf10 → 0x2b5360, land and
  space (Q4).
- `MCX4_RE_ANSWERS_TACTICAL.md` §13 (Q4):
  - 0x2b5360 is also the space battle-end pass;
  - auto-resolve deaths take Die's direct 0x48eb10 call, without 0x3a01f0.
- `A2M_R2_RE_B.md` §2.1: the vf+0x120 bodies are now named; the [M] link "vf+0x120 reaches 0x3ab890" becomes [H] via
  0x3a9e30 (Q4).
- `AI_GALACTIC_TOKENS.md` §1, the row "DistanceToNearestFriendly, connectivity, …": the four connectivity tokens are
  decoded here (Q1). `BKG3_RE_ANSWERS.md` row (c): add the token chain.
- `UI.md` §9.2: MEG entries are walked in record order through the 16-bit name index, and sub-directories are returned
  as directory entries (Q6).
