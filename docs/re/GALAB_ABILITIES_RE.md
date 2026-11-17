# GALAB: galactic hero and planet abilities (retail behaviour)

Roadmap group `galab` (box Q2F). Behaviour of the FoC galactic abilities that the port did not run before:

| § | Ability class | Port code |
|---|---|---|
| 1 | Remove_Corruption_Ability | `engine/galactic/src/galab_abilities.cpp` |
| 2 | Planet_Destruction_Ability (Death Star) | same file |
| 3 | Siphon_Credits_Ability | same file |
| 4 | Galaxy_Wide_Upgrade_Ability (planet bonuses), and its tactical spawn | same file; `engine/apps/sim/src/tactical_battle_hangar.cpp` |
| 5 | Reduce_Technology_Price_Ability (The Maw) | same file |
| 6 | Political_Transition_Bonus_Ability and Political_Control_Protection_Ability (Tarkin) | same file; the ramp in `campaign.cpp` |
| 7 | Neutralize_Hero_Ability, Hero_Protection_Ability | same file |
| 8 | `Variable_Self.HomePlanet.*` in AI equations | `engine/ai/src/galactic_ai_tokens.cpp` |

Sources: the retail export (`re_reference/ghidra/StarWarsG_decompiled.zip`), REA disassembly, the vtable names in
`docs/re/ghidra/functions.csv` and the FoC XML. Written in words; the addresses are RVAs of `StarWarsG.exe`.

Confidence: [H] read directly, [M] inferred from strong evidence, [L] guess or unread. [port] marks a choice of the
port.

Shared terms:
- **Ground_Activated walk.** The P4 hero trigger (`docs/re/P4_SIM_SYSTEMS.md` §6.1). It runs over a type's
  `<Abilities>` in list order. GALAB entries take part in that walk in their list position.
- **vf5 / vf30 / vf31 / vf21 / vf29** are the ability class's accept, apply, remove, owner-change and post-load slots.
- **Own-planet test 0x48e0c0.** The hero's player owns the planet.
- **Allied 0x2823c0.** Self, or relation Ally.

## 1. Remove_Corruption_Ability [H]

- **Defaults** (ctor 0x5b1310):
  - Corruption_Removal_Cost_Multiplier (+0x17C) = 1.0.
  - Corruption_Removal_Time_In_Secs (+0x180) = 10.
- **Post-load** (0x5b17f0):
  - A negative multiplier becomes 1.0. 0 is kept, so the removal is free.
  - A time of 0 or less becomes 10.
- **Accept** (vf5 0x5b1770). All of:
  - the own-planet test;
  - no corruption transition running on the planet;
  - the planet has a current corruption type.
- **Apply** (vf30 0x5b13c0):
  1. Cost = the planet type's Planet_Corruption_Removal_Base_Cost (T +0x804, read as a float) × the multiplier, never
     below 0.
  2. Refused, with the local player's error sound, in either case:
     - the cost is above 0 and the player has fewer credits than the cost;
     - the removal wrapper refuses.
  3. Otherwise the cost is charged with Add_Credits (no AI notify) and the removal transition starts. It takes
     Corruption_Removal_Time_In_Secs (`docs/re/GALACTIC_REVEAL_CORRUPTION.md` §4).
  4. Story LAND_ON / DEPLOY.

## 2. Planet_Destruction_Ability, the Death Star [H]

- **Style.** The ctor (0x7075d0) sets Ground_Activated.
- **Accept** (vf5 0x7079c0). The planet is not destroyed, and either:
  - the planet has no owner (−1); or
  - its owner is not allied with the shooter.
- **Apply** (vf30 0x707670):
  1. **Contested space** (when the shooter's object flag +0x3A2 bit 0x40 is set; the Lua `Set_Check_Contested_Space`).
     A non-hidden fleet in the planet's orbit that belongs to neither the shooter nor an ally refuses the shot, with
     the error sound.
  2. **Victory claim.** The shooter's player may have victory condition 7, and the victim's faction leader may be on
     the planet's surface. When both hold, the shooter claims victory 7 with countdown 30.
  3. The planet is destroyed through 0x3f76c0 (flag 1). Everything on it dies and the planet becomes a destroyed planet
     (`destroyed_credit_value`).
- No story LAND_ON / DEPLOY is sent by this class.
- [port] The contest check always runs. The Lua flag is not bridged.

## 3. Siphon_Credits_Ability [H]

- **Post-load** (0x70e9e0):
  - Min_Siphon_Percentage (+0x178) and Max_Siphon_Percentage (+0x17C) are clamped into 0..1, with an error.
  - Ground_Activated only.
  - Siphon_Duration_In_Cycles (+0x180) below 1 becomes 1.
- **Accept** (vf5 0x70e8c0). The planet's owner is not allied with the hero's player and is a playable faction.
- **Apply** (vf30 0x70e440), in order:
  1. The accept again.
  2. **Faction pairing** (0x48d900). Rebel, Empire and Underworld each pair with the other two.
  3. A **generic** hero is refused when an object whose type has Prevents_Minor_Credit_Siphons (T +0x63) is at the
     planet (0x4b8410).
  4. A planet that is **already siphoned** (planet data +0x200 set) is refused.

     Steps 2–4 refuse with the error sound.
  5. **Vigilance.** A Ground_Activated generic hero on a Planet_Vigilance planet is caught:
     - a respawn at the default time is scheduled (0x48eb10 with 0 s);
     - the hero is killed (0x391310);
     - nothing else happens, and there is no error sound.
  6. **Fraction.** One synced draw Get_Float(min, max) (0x532480; the bounds are swapped when needed).
  7. **Siphon record** (0x3f6520). The planet data gets:
     - +0x200 = the player;
     - +0x204 = the fraction;
     - +0x208 = now + fiscal cycle frames × cycles;
     - +0x218 = the hero's type;
     - +0x220 = the type of the hero's container (0x3973b0).

     The planet's income loses the fraction to the siphoner until the end frame. The record is cleared (0x3ff1f0) when
     it runs out, on an owner change, or by Neutralize_Hero (§7).
- [port] The original also needs the hero inside a container slot. The port's heroes at a planet always qualify.
- [M] The two lists 0x4b8410 reads are taken as the surface units and the orbiting fleets' contents.

## 4. Galaxy_Wide_Upgrade_Ability: planet owner bonuses [H]

- **Holder.** The planet type itself holds the ability (PLANETS.XML; for example Bespin holds
  Bespin_Krayt_Damage_Upgrade).
- **Post-load.** Galactic_Automatic only; anything else is forced to it with an error, and Causes_Despawn is cleared.
- **Apply** (vf30 0x6ff530). The ability applies to the planet's owner when:
  - the planet is not destroyed; and
  - its corruption type is not 0 (Intimidation). No corruption (−1) passes.

  Applying appends Space_Upgrade_Type (+0x180) to the player's space list (player +0x300), and Land_Upgrade_Type
  (+0x190) to the land list (+0x318).
- **Owner change** (vf21 0x6ff850). The types are removed from the old owner and applied to the new one, with no
  corruption test.
- [M] As a Galactic_Automatic ability, the apply is retried until it succeeds. The port tries in every planet service
  while nobody holds the planet's bonuses.
- [port] The list order is planet row order.

### 4.3 Tactical spawn [H]

- **When.** 0x2afc10 is called from the battle setup 0x2a1eb0 after the units are placed: first for the attacker, then
  for the defender. Mode 2 uses the space list and mode 1 the land list.
- **Per listed type.** A type is skipped when:
  - it has no affiliation with the player's faction (0x3751a0);
  - it has a Previous_Upgrade_Level_Type (T +0xA60);
  - the player already owns that type, or one further along its Next_Upgrade_Level_Type chain (T +0xA70).
- **The spawn.** Otherwise the object is created at the origin for the player.
- **Effect.** The upgrade objects carry Combat_Bonus auras. For example Bespin_Krayt_Damage_Upgrade is Space_Automatic,
  targets Krayt_Class_Destroyer, damage +0.25.
- [port] The spawns run after the landing points are placed.

## 5. Reduce_Technology_Price_Ability (The Maw) [H]

- **Holder.** The same planet-holder model as §4.
- **Post-load** (0x70c260). Price_Reduction_Percentage must be below 1.0; otherwise it becomes 1.0 with an error.
  Galactic_Automatic only.
- **Apply.** Adds an entry to the owner's technology-cost list (player +0x270):
  - value: −Price_Reduction_Percentage;
  - key: the source;
  - never expiring.

  An owner change moves the entry, as in §4.
- **Read** (0x281960). m = 1 + the sum of the entries; a result of 0 or less becomes 0.0001.
- The list's clamps +0x280 / +0x284 have no known writer [M] and are not modelled.

## 6. Political_Transition_Bonus_Ability (Tarkin) [H]

- **Defaults.** The ctor (0x709590) sets Galactic_Automatic.
- **Post-load** (0x709b50). Time_Reduction_Percentage outside [0, 1) becomes 0.
- **Accept** (0x709980). The planet itself, or the own-planet test: a hero of the planet's owner at the planet.
- **While held** (vf30 0x709640 / vf31 0x7099e0). The planet's transition map (planet data +0x78) holds an entry:
  - value: Time_Reduction_Percentage;
  - never expiring;
  - filed under Stacking_Category.

  The entry leaves with the hero.
- **Political ramp** (0x4adab0). The step is e / (FPS × m × T), with m defined as:
  1. take the smallest entry in each category;
  2. multiply those values across the categories;
  3. apply the map's clamps.

  So 0.25 makes the transition four times faster.
- [M] The alignment factor's own category is not recovered. The port multiplies the Tarkin factor with the alignment
  factor.
- [M] Holders are the owner's live objects on the surface and in stationary orbit fleets.

### 6.1 Political_Control_Protection_Ability (Tarkin) [H]

- **Defaults.** The ctor (0x7090a0) sets Galactic_Automatic.
- **Accept** (vf5 0x709450). The own-planet test: a hero of the planet's owner at the planet.
- **While held** (vf30 0x709140; vf31 0x709470 removes it). The planet's protection list (planet data +0x250, count
  +0x258) holds an entry {true, never expiring, the Text_ID CRC of the holder's type name, a key of the holder}.
- **Reader** (0x4b8510). It folds the list with a logical AND, so the result is true with any entry and false when the
  list is empty.
- **Effect.** The political update 0x3fb040 (new derived owner, new target control) first notes whether the update is
  adverse: the derived owner differs from the current owner, or the new target is below the current control. While
  the planet is protected, an adverse update skips the ramp step 0x4adab0. The owner flip itself (control set to 0) is
  not blocked.
- [M] Holders are the same as in §6: the owner's live objects on the surface and in stationary orbit fleets. The port
  tests protection before the flip, as the list still holds the old owner's entries at that point.

## 7. Neutralize_Hero_Ability and Hero_Protection_Ability [H unless marked]

### 7.1 Fields and load rules

**Neutralize_Hero_Ability.** Ctor 0x615b70, post-load vf29 0x617f70.

| Field | Offset | Default | Post-load |
|---|---|---|---|
| Cost_Mod_By_Base_Level | +0x178 | — | fewer than 2 points → (0,1)-(5,1) |
| Cost_Mod_By_Previous_Neutralizations | +0x1A0 | — | fewer than 2 points → (0,1)-(1000,1) |
| Cost_Mod_By_Planetary_Corruption_Level | +0x1C8 | — | fewer than 2 points → (0,1)-(1000,1) |
| General_Minor_Hero_Cost_Mod | +0x1F0 | 1.0 | 0 or less → 1.0, with an error |
| General_Major_Hero_Cost_Mod | +0x1F4 | 1.0 | 0 or less → 1.0, with an error |
| Owner_Respawn_Time_In_Secs (min, max) | +0x1F8 / +0x1FC | 0, 0 | — |
| Target_Respawn_Time_In_Secs (min, max) | +0x200 / +0x204 | 0, 0 | — |
| Can_Neutralize_Minor_Heroes | +0x208 | Yes | — |
| Can_Neutralize_Major_Heroes | +0x209 | Yes | neither flag set → a warning only |

The style is Ground_Activated.

**Unit-type fields:**
- Neutralization_Cost (T +0x1FBC, float).
- Can_Be_Neutralized_By_Minor_Heroes (T +0x7B).
- Can_Be_Neutralized_By_Major_Heroes (T +0x7C).

**Hero_Protection_Ability** (ctor 0x703090):
- Neutralization_Cost_Multiplier (+0x178), default 1.0.
- Stacking_Category (+0x17C).
- Galactic_Automatic.

### 7.2 Accept and candidates

**Accept** (vf5 0x6166d0). The target is a planet, and 0x61b610 finds nothing in its orbit. 0x61b610 is the
hack-target super-weapon search that `Hack_Super_Weapon` also uses (P4 3.2). So no hero can be neutralized while a
Death Star orbits.

**Candidates** (vf30 0x615fd0 with 0x615d20 / 0x616540). Each candidate is a 0x68-byte record.
- **Order:**
  1. The heroes in the planet's orbit list. Fleets are entered recursively; a transport gives its first member that
     yields a hero. Space flag (+0x60) = 1.
  2. The surface list, with flag 0.
- **A hero is skipped** when:
  - it is allied with the neutralizer's player;
  - its Neutralization_Cost is 0 or less;
  - it is not visible to that player (0x50bba0).
- **Visibility** (0x50bba0) [M]:
  - A stealthed hero is seen only with the minor-stealth-hero (generic) or major-stealth-hero (named) planet
    information bit.
  - An unstealthed one is seen with that bit, or with the planet's fleet-contents (orbit) or ground-company-contents
    (surface) information.
- **The record:**
  - +0 the hero's type;
  - +8 its container's type;
  - +0x10 its object id;
  - +0x60 the space flag;
  - +0x61 eligible;
  - +0x62 siphon.
- **Eligible** (+0x61):
  - A generic target needs Can_Neutralize_Minor_Heroes, and a named target needs Can_Neutralize_Major_Heroes.
  - A named neutralizer needs the target's Can_Be_Neutralized_By_Major_Heroes. Any other neutralizer needs
    Can_Be_Neutralized_By_Minor_Heroes.
- **The siphon candidate.** It is appended when the planet is siphoned (§3) by a player not allied with the
  neutralizer's player:
  - type = the siphoning hero's type (+0x218);
  - container type from +0x220;
  - siphon flag 1;
  - eligibility by the same rules;
  - no cost or visibility test.

### 7.3 Hero protection

**Holding an entry** (vf30 0x703140; vf31 0x703600 removes it). While a holder is at the planet and its accept holds,
the planet's protection map (planet data +0x2A8) holds an entry under the holder's Stacking_Category. The entry is
{Neutralization_Cost_Multiplier, never expiring, the Text_ID CRC of the holder's type name, a key of the holder}.

**Accept** (vf5 0x703470):
- The planet holding the ability itself passes.
- Otherwise it depends on the multiplier:
  - above 1: the holder's player must be allied with the planet's owner;
  - below 1: the holder's player must not be allied with the planet's owner;
  - exactly 1: never.

So a protector raises the price on friendly ground, and a "reducer" lowers it on enemy ground. In FoC, Jabba has 2.0 and
Han Solo 1.25, both in category 0.

**Cost multiplier for a target.** The cost function copies the map and removes every entry keyed by the target itself,
so a hero's own protection does not cover him. Then:
1. take the largest value in each category;
2. multiply those values across the categories, in ascending category order;
3. apply the map's clamps (+0x2B8 / +0x2BC with flags +0x2C0).

The map's clamps have no known writer [M].

### 7.4 Cost (0x6171e0), per candidate in list order

**Respawn draws.** Two synced draws Get_Float (0x1ffbb0) are made, the owner's first:
- the neutralizer's respawn (+0x38), from Owner_Respawn_Time_In_Secs;
- the target's (+0x3C), from Target_Respawn_Time_In_Secs.

When either bound of a pair is 0 or less, Default_Hero_Respawn_Time is used and there is no draw.

**Base.** base = Neutralization_Cost × General_Major_Hero_Cost_Mod for a named target, × General_Minor_Hero_Cost_Mod for
a generic one.

**Cost lines.** "Base cost" is always present; the others only when non-zero.

| Line (text id) | Value |
|---|---|
| TEXT_HERO_NEUTRALIZE_BASE_COST | base |
| TEXT_HERO_NEUTRALIZE_COST_BASE_LEVELS | Cost_Mod_By_Base_Level(level) × base. level = 0 unless the target's owner (the siphon player for the siphon entry) is allied with the planet's owner; then the starbase level (planet data +0x148) for an orbit candidate, or the ground base level (+0x130) for a surface one |
| TEXT_HERO_NEUTRALIZE_COST_HERO_PROTECTION | (protection multiplier − 1) × base |
| TEXT_HERO_NEUTRALIZE_COST_PRIOR_NEUTRALIZATIONS | Cost_Mod_By_Previous_Neutralizations(n) × base. Only for a hero candidate whose type is named. n = GameScoring `Get_Neutralized_Count_For_Type(container type, neutralizer's player)` |
| TEXT_HERO_NEUTRALIZE_COST_PLANETARY_CORRUPTION | −Cost_Mod_By_Planetary_Corruption_Level(1 if the planet is corrupted, else 0) × base |

**Price** (0x4b40b0). The sum of the lines in list order, at least 1.0. The record's ctor sets that minimum (+0x28,
flag +0x30).

[M] The curve arguments are read from the surrounding code (the decompile drops them).

FoC values: Boba Fett / Bossk / IG-88 / Mara Jade have
- base levels (0,0)-(5,1);
- prior neutralizations (0,0)-(1000,750);
- corruption (0,0)-(1,0.25).

So each base level adds 20 % of the base, each prior neutralization of that hero adds 75 %, and a corrupted planet takes
off 25 %.

### 7.5 Choice

**No candidate.** The local player's error sound, and nothing is applied.

**A local human.** The panel opens (0x3c8d90) with the list, and nothing is applied now.
- The pick returns as SpecialAbility kind 3 (0x4b4990 case 3). The payload carries:
  - the neutralizer and planet ids;
  - the candidate's object id and both types;
  - the price as a q8 code;
  - both respawn times as q5 codes;
  - the three flags.
- The handler (0x4b4d70 case 3) rebuilds the cost list as one entry (the q8 price), re-reads the times, and runs the
  executor for a neutralizer that is at a planet.
- [port] The port keeps the list (`GalacticSim::neutralize_offer`) and sends a `NeutralizeDialog` event. The panel itself
  is backlog work for the UI.

**An AI** (0x2814d0):
1. Each candidate that is eligible and whose price is at most the player's credits gets weight 1, in list order.
2. One synced draw Get_Float(0, n) (0x532480).
3. A binary search picks the first candidate whose cumulative weight is at least the draw.
4. The executor runs on it.

With no such candidate there is no draw. A remote human: nothing.

### 7.6 Executor (0x616740)

1. **Checks.** Each failure gives the error sound:
   - a siphon entry needs the planet still siphoned;
   - the candidate must be eligible, and its price at most the credits;
   - a hero entry needs its target object found.
2. **Vigilance.** On a Planet_Vigilance planet, a generic neutralizer is caught:
   - the messages are shown;
   - a respawn at the default time is scheduled;
   - the neutralizer is killed;
   - the ability fails.
3. **Markers.** The target's "killed by" byte (+0x330) gets the neutralizer's owner; the neutralizer's gets 0xFF.
4. **The target.**
   - A hero target: its respawn is scheduled with the candidate's target time (0x48eb10, flag 1). It gets the object
     signal 0x27 ("neutralized"; GameScoring `Galactic_Neutralized_Event` counts it) and is despawned (0x48cf20).
   - The siphon entry: GameScoring `Galactic_Neutralized_Event(container type, neutralizer)` is called directly
     (0x4879b0), and the siphon record is cleared (0x3ff1f0).
5. **Charge.** Add_Credits(−price), without the AI notify.
6. **Story.** LAND_ON / DEPLOY (0x3284c0), then the story generic event "hero_neutralized".
7. **The neutralizer leaves.** Its respawn is scheduled with the candidate's owner time, and it is despawned.

GameScoring (`Data/Scripts/Miscellaneous/GameScoring.lua`) keeps `GalacticNeutralizedTable[killer owner id][type]` as a
count. The cost reads it back for the prior-neutralization line.
- [port] The port keeps the count in the galactic sim: `GalacticSim::neutralized_count`, saved (galactic save v8) and
  hashed.
- [M] Both paths are counted under the hero's galactic type. That is the container type in the original; the port's
  galactic hero objects carry that type.

## 8. `Variable_Self.HomePlanet.*` [H]

- **Setting the home planet.** Campaign setup stores, for each player, the home planet in player +0x188. The value is
  0x4af240(campaign, faction): the planet of the first case-insensitive Home_Location entry for the faction.
- **The token.** The player evaluator (vf8 0x66b9f0) resolves `HomePlanet` to the planet lookup
  `RootLookup("Planet_" + type name)` when +0x188 is set. Otherwise the lookup is unbound and every leaf below it fails.

  So `Variable_Self.HomePlanet.Type.Token` resolves through Home_Location. For example, Need_First_Research_Facility
  tests `Is_Home_Planet` / the home planet's facilities.
- [port] The galactic sim stores the home planet per player (`PlayerState::home`, saved in v8). The AI token resolver
  maps the `HomePlanet.` prefix to it.
