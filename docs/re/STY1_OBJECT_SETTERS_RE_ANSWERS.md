# sty1 t01: RE answers for four Lua object-state setters (Prevent_Opportunity_Fire, Disable_Capture, Set_Check_Contested_Space, Set_In_Limbo) (2026-11-15)

The port stores these four `GameObjectWrapper` setters in `lua::ScriptObjectState` but the sim ignores them. This file
says what retail does: which field each one writes, what true / false mean, every reader of that field, and the shipped
use. Companions: `A2M_T04_RE_ANSWERS.md` (limbo writers), `HERO_CLASH.md` §8 (the +0x3a1 & 0x10 bit, written there as
"incognito"), `TACTICAL_COMBAT.md` §2.3 / s78j (targeting, `Prevent_All_Fire`), `L1_S4_REMAINING.md` §4.2 (capture rules),
`TSW_PLANET_DESTROY.md` (planet destruction).

Source: Ghidra decompilation of the FoC x64 `StarWarsG.exe` through REA (RVAs relative to 0x140000000; ReVa did not connect
in this session). Readers were found by searching the whole image for the encoded field displacements (+0x3a1, +0x3a2,
+0x3a7) and reading every containing function, then checking the shipped scripts in `extracted/foc_data_text.zip`.
A displacement search finds accesses with the field offset in the instruction, so a reader that reaches the byte
through a different base-plus-offset split would be missed [M for completeness]; for +0x3a1 & 0x10 the independent list in
`HERO_CLASH.md` §8.4 matches this one exactly. Behaviour is described in words; no decompiled code is reproduced.
Tags: [H] read directly; [M] one inferred link; [L] hypothesis.

Object fields used (see `OBJECT_RUNTIME.md` §2.1, `A2M_T04_RE_ANSWERS.md`): flag bytes obj +0x3a0 / +0x3a1 / +0x3a2,
limbo byte +0x3a7, behaviour slot bytes at obj +0x332 + id (−1 = absent): TEAM (0x16) +0x348, CAPTURE_POINT (0x3d)
+0x36f; parent +0x2b0; owner +0x58. A behaviour has a suspend counter at +0x3c; the object service runs a behaviour only
while that counter is 0 (`MOVEMENT.md` §3.3, `TACTICAL_COMBAT.md` s78j t02) [H].

## Short answers

| Lua setter | Wrapper RVA | Writes | Effect |
|---|---|---|---|
| `Prevent_Opportunity_Fire(b)` | 0x57f050 | obj +0x3a1 bit 0x10 (the same bit the vehicle thief and the Blast ability set, and `HERO_CLASH.md` calls "incognito"), on the object and on every member of its TEAM behaviour | The unit stops auto-acquiring targets and other units stop auto-acquiring it. Direct orders still work. Retail also clears the bit as soon as the unit deals or takes damage. Not the same as `Prevent_All_Fire` |
| `Disable_Capture(b)` | 0x5755c0 | the suspend counter (+0x3c) of the object's CAPTURE_POINT behaviour: true adds 1, false subtracts 1 (floor 0); error log when the object has no such behaviour | While the counter is above 0 the capture service never runs: the point keeps its owner and progress, no capture can start or finish |
| `Set_Check_Contested_Space(b)` | 0x580180 | obj +0x3a2 bit 0x40; every object starts with the bit **set** | Read only by the galactic planet-destruction ability (Death Star, 0x707670): with the bit set the shot is refused while a visible non-friendly fleet is in orbit at the target planet |
| `Set_In_Limbo(b)` | 0x580460 | obj +0x3a7 := b (the whole byte; the same "contained / hidden in a container" byte garrison, transport landing and vehicle-thief boarding set) | Marks the object as contained. About 60 functions read it (list in §4). The Lua setter writes **only the byte**: it does not hide the model, suspend weapons, or take the object out of the player list or the object grid |

## 1. Prevent_Opportunity_Fire (0x57f050)

### 1.1 What the wrapper does [H]

- Needs a wrapped object and exactly one boolean argument; otherwise it logs "invalid number of parameters" / "invalid type
  for parameter 1. Expected boolean" and changes nothing.
- Writes bit 0x10 of obj +0x3a1 to the argument (a single bit; other bits untouched). Idempotent: two `true` calls are the
  same as one, one `false` clears it (unlike the counters of `Prevent_All_Fire` and `Disable_Capture`).
- If the object has a TEAM behaviour slot (obj +0x348 ≠ −1, a land squad / company container), the same bit is first written
  on **every member** of the TEAM member vector (behaviour type 0x16), then on the container itself. If the slot says TEAM
  but the behaviour lookup returns nothing, nothing at all is written (not even the object's own bit) [H, edge case].
- Matches the shipped use: 148 `Prevent_Opportunity_Fire(true)` calls in `Data/Scripts/Story` (Rebel / Empire / Underworld
  campaign missions: cinematic ships, prisoners, passive objectives, guards that must not start the fight), usually undone
  with `(false)` when the scripted phase ends.

### 1.2 Readers of obj +0x3a1 & 0x10 [H unless tagged]

All other users of the bit are the writers and clears below. The meaning is "not auto-acquired, does not auto-acquire":

| RVA | Reader | What it does with the bit |
|---|---|---|
| 0x5662b0 | TargetingBehavior candidate scan (`Scan_Player`) | Skips a candidate that has the bit: enemies never pick it as an automatic target |
| 0x385190 | hardpoint target pick | Skips a candidate with the bit |
| 0x387400 | hardpoint auto-scan service | An **owner** with the bit does not scan for targets at all (its hardpoints stay idle) |
| 0x634810 | targeting service | When the unit has no current target, a unit with the bit returns "nothing acquired" before the target search runs [H] |
| 0x633ae0 | targeting service (team / unit variant) | Same gate before its target search [H gate; M that it covers only the no-current-target branch] |
| 0x3a9e30 | Take_Damage | Clears the bit (see 1.3); no other use |

Not read anywhere else. In particular the **explicit** attack path (`Order_Attack` 0x38d730, `Set_Target` 0x569160,
`Validate_Current_Target` 0x567c50) does not test the bit: a flagged unit ordered to attack, or flagged and already holding
a target, still fires; a flagged unit can still be attacked by an explicit order. Saved with the object (0x39ce40 /
0x3a5ad0) and copied by the object copy 0x472560 [H].

### 1.3 What clears it [H, `HERO_CLASH.md` §8.4.1, re-checked]

- Take_Damage 0x3a9e30: on the victim, when a source is given and that source is not a projectile (no projectile pack) or
  is a projectile whose target is this victim. Not on splash from a projectile aimed at someone else.
- Take_Damage 0x3a9e30, on the **source**: a non-projectile source that has the bit loses it; the shooter of a projectile
  loses it. So a flagged unit that deals damage becomes normal at its first damaging hit.
- MaximumFirepower ability 0x707390: clears the bit on its team members.
- Not cleared by owner change, death clone creation, or any timer.
- Gating: Take_Damage returns early for `Immune_To_Damage`, limbo (+0x3a7), and a few other cases before reaching the clear,
  so an immune or limbo unit keeps the bit.

Consequence for the shipped scripts: `Prevent_Opportunity_Fire(true)` is a "until first damage" protection, not a
permanent mute; scripts that re-assert it (several do, every phase) keep it alive.

### 1.4 Difference from Prevent_All_Fire (0x57ee20) [H]

`Prevent_All_Fire(true)` adds 1 to the suspend counter of the object's TARGETING (9) and WEAPON (10) behaviours (and of each
TEAM member's), and clears the auto-fire flag (+0x6c) of the object's weapon-type hardpoints (0x392570), `false` subtracts
1 and sets the flag back (0x393b20). The unit then neither acquires nor fires at all, even on direct orders; the counters
stack. `Prevent_Opportunity_Fire` leaves TARGETING / WEAPON running: only the automatic choice of targets is switched off
(in both directions); ordered attacks and hardpoint fire at an ordered target work. "Opportunity fire" here means automatic
fire at targets of opportunity (what the unit would shoot while idle or on the way), not the AI plan feature
`Add_Opportunity_Fire_Event_Subscription` (that is a TaskForce event on hardpoint fire; the flag only keeps a flagged unit
from producing such events by acquiring) [M].

## 2. Disable_Capture (0x5755c0)

### 2.1 What the wrapper does [H]

- One boolean argument, wrapped object required. It looks up the object's CAPTURE_POINT behaviour (type 0x3d, slot +0x36f).
  If there is none it logs "<object type> isn't captureable to begin with - no capture point behavior" and does nothing.
- `true`: calls the behaviour's suspend (vtable +0xc0, adds 1 to the behaviour's counter at +0x3c, floor 0).
  `false`: calls resume (vtable +0xb8, subtracts 1, floor 0). The pair is the shared behaviour suspend / resume
  (0x4c30b0 / 0x4c30a0) that `Prevent_All_Fire` uses on TARGETING / WEAPON. So two `Disable_Capture(true)` need two
  `(false)` to re-enable, and an extra `(false)` is harmless.

### 2.2 Readers [H for the scheduler, M for the rest]

- Retail has **no capture command and no capture cursor**: a land or space capture point changes hands passively through
  the CapturePointBehavior service (vf6 0x501980; controller scan 0x500a00; progress; completion 0x500e30; rules in
  `L1_S4_REMAINING.md` §4.2). Units "capture" only by standing inside `Capture_Point_Radius` with `Influences_Capture_Point`.
- The only thing the counter does is stop that service from being scheduled (generic behaviour rule, counter > 0 = not
  serviced). The readers of the suspended test (0x4c36f0: 0x569540, 0x3a4070, 0x3c0020 and the behaviour services of
  targeting / weapon / locomotor classes) are not capture code. No target filter, ability-accept function or cursor reads
  the counter [H for the 17 callers of the accessor; M that no code reads the +0x3c field directly].
- Effect while disabled: no controller scan, no progress change, no ownership transition, no capture-complete story event
  (STORY_CAPTURE_STRUCTURE), no radar event, no color transition. Progress and pending controller stay frozen at the values
  they had and resume from there on `false` [M: follows from "service not run"].
- Other code that treats "has CAPTURE_POINT" as a capture-ability test is unaffected, so AI perception (`IsContestable`
  0x731400, which only checks that the behaviour exists) and the tactical AI still see the object as capturable, and an AI
  unit may stand in the radius for nothing [M].
- What a capturing unit sees: nothing. No error text, no sound, no cursor change; the point simply never reacts [M: no
  presentation code reads the counter].
- Direct owner changes (Lua `Change_Owner`, the neutral / owner reset that 0x501050 re-derives) do not go through the
  service and are not blocked [M].

### 2.3 Shipped use [H]

Only `Data/Scripts/GameObject/SecondaryHeavyFactory.lua` and `SecondaryMiningFacility.lua` (same code in EaW and FoC):
the object waits (`Set_Garrison_Spawn(false)`) until its owner is a playable faction, then enters its controlled state,
calls `Set_Garrison_Spawn(true)`, `Disable_Capture(true)`, sleeps 45 s and takes 10001 damage (the building is a one-shot
prize). So `Disable_Capture(true)` there keeps enemy units from re-capturing the building during those 45 s. (A story
XML event named `..._Disable_Capture_Text` in Rebel A3 M07A is only text.)

## 3. Set_Check_Contested_Space (0x580180)

### 3.1 What the wrapper does [H]

Writes bit 0x40 of obj +0x3a2 to the argument (single bit, other bits untouched). The byte is initialised by both object
constructors (0x388720, 0x388b60) from a static initial value of **0x60**, so the bit is **set (true) for every object**
by default [H]; the bit 0x20 is also set at creation (not part of this setter). Saved and copied with the object like the
rest of +0x3a2.

### 3.2 The only reader [H]

`PlanetDestructionAbility` activation (vtable slot 30, 0x707670), used by the galactic Death Star. Order, as read:

1. The ability's own validity test (slot 5, 0x7079c0) must pass: target is a planet object, not already destroyed
   (planet data +0x2c8), and the planet's owner is not friendly to the shooter's owner.
2. The target planet must carry its planetary data pack. Otherwise the call returns "not done".
3. **If the shooter object (the Death Star unit that carries the ability) has bit 0x40 set**: walk the planet's orbit list
   (planet data +0xc8, count +0xd0, the same list the blockade test uses, `A2GAL_READINGS.md` §4). For each fleet that is
   not hidden (stealth predicate 0x3a9760 false): if the fleet's owner and the shooter's owner are not friendly (0x39a2c0:
   same player or ally), the ability is **refused**: nothing is destroyed, the call returns "not done", and if the
   shooter's owner is the local player the generic local error feedback event (global 0x140b301c8) is played. [H for the
   walk and refusal; M that the relation is evaluated from the fleet owner's side (the call passes the fleet's owner as
   the player argument); relations are symmetric in vanilla.]
4. With the bit clear (or no blocking fleet) it continues: galactic victory leader test, then the destroy routine 0x3f76c0
   and the visual refresh (`TSW_PLANET_DESTROY.md` §5 and §6).

So "contested space" means: **an enemy (non-friendly, visible) fleet is in orbit at the target planet.** One blocker is
enough; unlike the planet blockade test, it is not "the first non-hidden entry decides". Friendly and allied fleets,
including the Death Star's own fleet, and fully stealthed fleets do not block. Ground units, starbases and orbit objects
that are not fleets in that list are not examined here.

No code outside 0x707670 reads the bit: no UI greying, no AI check, no other ability [H by the displacement search].
Build pads, reinforcement points, garrisons and space-station build rules do not use it.

### 3.3 Shipped use [H]

`STORY_REBEL_ACTIV_M12_GALACTIC.LUA` only (Rebel Act IV mission 12, the Death Star advancing toward Mon Mothma; 4 call sites): the script clears the
flag, calls `Activate_Ability("Death_Star")` on the Death Star fleet, and sets the flag back at once, both when the Death
Star first appears over Alderaan and at each later step toward the planet holding Mon Mothma. The scripted Death Star
therefore fires even with Rebel fleets in orbit, while a player-driven or default-flag Death Star would be refused. The
EaW copy of the script has the same calls.

## 4. Set_In_Limbo (0x580460)

### 4.1 What the wrapper does [H]

Stores the argument into the single byte obj +0x3a7 (1 / 0). That is the whole effect of the Lua call. The same byte is
the general "contained / hidden in a container" flag; readers test it as zero / non-zero, a few as exactly 1.
Writers (`A2M_T04_RE_ANSWERS.md`): containment 0x37ade0 (garrison enter 0x4446b0, transport landing 0x644330, tactical
battle setup 0x2a1eb0 / 0x2a00b0 / 0x2afdb0, Lua spawn 0x2bfdd0, vehicle thief 0x63fae0), TransportLanding 0x643a30 /
0x643b00, and this Lua setter. Clears: Lua `false`, garrison exit / eject 0x37a060 / 0x37a8d0, TransportLanding unload
0x642c70, and constructor init to 0. Death, owner change and battle end do not clear it; save / load (0x39ce40 / 0x3a5ad0)
and the object copy 0x472560 carry it [H].

**What the Lua setter does not do** [H]: the containment routine 0x37ade0 also hides the model, suspends TARGETING (9)
and WEAPON (10) and a third behaviour (id 0xb), stops the locomotor (vtable +0x200), removes the object from its player's
object list (0x2a6430), from the coordinator system (0x426d50) and from the object grid (0x539470), and signals the host.
`Set_In_Limbo` runs none of it. A Lua-limbo object keeps its model (scripts add `Hide(true)` themselves), its behaviours
and its list / grid registration; only the readers below see the byte.

### 4.2 Readers, by effect

Each row lists the functions found by the displacement search. "Skip" means the object is treated as absent.

**Damage and targeting (tier 1)**

| RVA | Reader | Effect of limbo |
|---|---|---|
| 0x3a9e30 | Take_Damage | Returns before any damage is applied (after the `Immune_To_Damage` test), so no health loss, no bit clears, no kill. Applies to every damage kind including kind 0 [H] |
| 0x5662b0 | candidate scan | Candidate in limbo (byte equals 1) is skipped: not auto-acquired [H] |
| 0x5680b0 | target validity | A limbo object is not a valid target [H] |
| 0x569160 | `Set_Target` | Refuses a limbo target [H] |
| 0x567c50 | `Validate_Current_Target` | A current target that is in limbo is invalid (result code 2) and gets dropped [H] |
| 0x38d730 | `Order_Attack` | Refuses an attack order on a limbo target (returns 0) [H] |
| 0x385190 | hardpoint target pick | Skips limbo candidates [H] |
| 0x387400 / 0x396fe0 | hardpoint auto-scan | The owner does not scan while "limbo and not inside a garrison that has occupants" (0x396fe0: true when limbo and the container has no garrison pack or garrison state 0) [H] |
| 0x634810, 0x633ae0, 0x62eeb0 | targeting services (unit / team) | The unit itself in limbo (or hero clash +0x3b4, or dying) does not acquire a target; returns "none" [H] |
| 0x54d090, 0x54ade0, 0x3857d0, 0x3749d0 | weapon service, fire origin, hardpoint fire range, attack range | Limbo is the "garrisoned shooter" case: fire range is multiplied by the garrison range factor (global 0x140b278ac), shots may start from the host's garrison fire bones, the origin falls back to the parent's position (`LOB_SOLVER.md`, `TACTICAL_COMBAT.md` §2) [H range factor; M the rest] |
| 0x2a8fb0 | GOM spatial / per-player query | Skips limbo objects, so splash damage, area abilities and AI perception queries never see them [H] |
| 0x6fed40, 0x711eb0, 0x717110, 0x6fbc20, 0x6fbb20, 0x6cc790, 0x6cc810, 0x636e80 | ability target scans and infection (force heal, find-weakness style scans, infection ability and service) | Candidate or user in limbo is skipped; the INFECTION service does nothing while limbo [H] |
| 0x1406fbb20 | ability timing guard | Reads limbo as "not exactly 1" for its cooldown update [M] |

**Abilities**

| RVA | Reader | Effect |
|---|---|---|
| 0x39b480 | ability-available test (`ABILITIES_HARDPOINTS.md` §2.4: rule 4 and the per-type table) | Limbo with no occupied garrison container: every ability unavailable; types 18, 19, 23, 24, 25, 74 are unavailable whenever limbo equals 1 [H] |
| 0x38a350, 0x3a1930, 0x3a1ce0, 0x3a1fb0, 0x3a3260, 0x3a3500, 0x3a38d0 | ability activation dispatcher and the individual activators (DEPLOY 0x3a1ce0, BARRAGE 0x3a1930, FORCE_WHIRLWIND 0x3a1fb0, ...) | Refuse activation for a limbo object unless it sits inside a garrison whose pack is occupied (state ≠ 0) [H] |
| 0x573e80, 0x5fbcb0 | Lua `Can_Garrison` (object / TaskForce) | Returns false for a limbo unit [H] |

**Selection, picking, HUD**

| RVA | Reader | Effect |
|---|---|---|
| 0x3c2310 | SelectBehavior service | While limbo, the selection ring / marker is not created and an existing one is torn down [H] |
| 0x36600 | world pick (mouse ray) | Limbo objects are not hit-tested, so the cursor cannot hover or click them [H] |
| 0x27fbf0, 0x287ce0 | control-group events | Limbo members are left out of the group's center and count [H] |
| 0x35b4d0 | radar / minimap blip update | Returns early: no blip [H code; M meaning] |
| 0x30e660, 0x31b590, 0x312b90, 0x2f62f0, 0x2f64e0, 0x14033900, 0x406e30 | command-bar and land HUD builds, hero portrait click, tracker presentation | Skip limbo members [M: presentation only] |

**World presence, fog, collision, capture**

| RVA | Reader | Effect |
|---|---|---|
| 0x505020, 0x508170 | collision pairing | No collision pair forms when either side is limbo (`MOVEMENT.md` §5.2 / §5.3, "either side +0x3a7"); the unit passes through others [H] |
| 0x53a400, 0x53a900, 0x53acf0 | object-grid / tracking-layer insert and refresh | Skips limbo objects. The Lua setter does not remove an existing registration, so a unit already in the grid stays until its next re-insert [H for the skip; M for the stale entry] |
| 0x5373c0 | RevealBehavior service (fog of war) | While limbo, the unit's reveal list is undone on every allied player and cleared, and the service returns: **no vision from a limbo unit** [H] |
| 0x53ddc0, 0x53daa0, 0x53dd70 | HideWhenFogged service / hooks | Returns before updating (a limbo unit's fog-hide state is not refreshed) [H gate; M effect] |
| 0x500a00 | capture controller scan | A limbo unit is not a capture candidate [H] |
| 0x3a6b80 | object service, animation step | Model animation advance and the animation-keyed SFX / SURFACE rows are skipped while limbo (`ANIM_PARTICLES.md` A.5, `AUDIO.md`): the model freezes [H] |
| 0x3a56b0 (callers: Lua `Teleport` 0x5819e0, `Teleport_And_Face` 0x581ac0) | Teleport helper | A limbo unit whose parent has a garrison pack is first removed from the garrison (0x37a8d0) before the teleport; without such a parent nothing happens [H] |
| 0x3a5ad0 / 0x39ce40, 0x472560 | save / load, object copy | Carry the byte [H] |

### 4.3 What does not read limbo [H by displacement search, M for completeness]

- **Victory counting.** The tactical "all enemy units destroyed" test (0x344710 family, `BATTLE_TRANSITIONS.md` §1) never
  reads +0x3a7: a limbo unit still blocks the enemy's victory as long as its type is victory-relevant and it is alive. Lua
  `Find_All_Objects_Of_Type` and the general object lists do not filter limbo either, so scripts and AI that enumerate
  by type still see the object (garrison containment does remove the unit from the player's list; the Lua setter does not).
- **Movement.** The locomotor classes (Walk, Fleet, flight) do not read the byte. A limbo unit can still be ordered to move
  and move; `Teleport` works.
- **Rendering.** No render or model-visibility code reads +0x3a7: the model stays visible unless `Hide(true)` (or a
  garrison enter) hides it [M].
- **Health, ownership, production.** No effect on health regeneration or owner change.

### 4.4 Shipped use [H]

`STORY_UNDERWORLD_ACTIII_M11_LAND.LUA` only (Underworld Act III mission 11, land; 17 calls): each time the player's control
passes between Silri, Urai Fen, Tyber Zann and Cuddles the Rancor (and the Stormtrooper extras), the script puts the
benched characters in limbo together with `Hide(true)`, `Set_Selectable(false)` and `Make_Invulnerable(true)` and takes
them out of limbo with the opposite calls. Limbo's contribution there is "cannot be targeted or damaged, has no
ability, collision or vision, does not show in picking / radar / HUD, model frozen". The script itself hides and
protects them, so the port only needs the readers, not an automatic hide.

## 5. Port implementation notes (minimal rules for retail-equivalent tier-1 / tier-2 behaviour)

Port flag names are the ones in `engine/tactical/combat/include/alamo/tactical/combat/combat_system.hpp`
(`kUnitIncognito`, `kUnitGarrisoned`, `kUnitTargetingSuspended`, `kUnitWeaponsSuspended`).

1. **Prevent_Opportunity_Fire = the incognito flag.** Map the setter to the same flag the vehicle thief and Blast already
   set (`kUnitIncognito`, +0x3a1 & 0x10), on the object **and every TEAM member** (members first, then the container; no
   write at all if the TEAM lookup fails). Single bit, idempotent. Do not touch TARGETING / WEAPON suspend flags (those are
   `Prevent_All_Fire`). The existing incognito readers (candidate scans skip it, owner does not auto-acquire, Take_Damage
   clears it per `HERO_CLASH.md` §8.4.1) then give retail behaviour; confirm that those readers include hardpoint
   auto-scan and the targeting services, and that explicit attack orders are not gated.
2. **Disable_Capture = a stacking suspend counter on the capture point.** Per object, count: `true` +1, `false` −1 floor
   0, log and ignore when the object has no capture-point behaviour. While the counter is above 0 the capture service of
   that object does nothing (no controller scan, no progress change, no ownership change, no STORY_CAPTURE_STRUCTURE).
   Progress and pending controller stay as they were. Direct `Change_Owner` stays allowed. No UI / AI change.
3. **Set_Check_Contested_Space = a default-true per-object bit read by one rule.** New objects start with the bit true. The
   galactic planet-destruction (Death Star) activation reads the shooter's bit: when set and any non-hidden fleet in the
   target planet's orbit list is not friendly to the shooter's owner, refuse the activation (no destruction, local error
   feedback for the local player). Bit clear = the check is skipped. No other consumer.
4. **Set_In_Limbo = write the shared contained byte only.** Wire the setter to the same predicate the port uses for
   garrisoned / transported units (`kUnitGarrisoned` or a dedicated limbo bit if the garrison code must keep its side
   effects), without running the containment side effects (no model hide, no suspend of targeting / weapons, no removal
   from player lists). The predicate must then be honoured by: Take_Damage (no damage), target candidate scans, target
   validity, `Set_Target`, `Validate_Current_Target` and attack orders (target in limbo is invalid), the unit's own
   auto-acquire and hardpoint auto-scan, spatial / area queries and splash victims, ability availability and activation
   (refused unless inside an occupied garrison), `Can_Garrison`, collision pairing, capture candidates, fog reveal
   (limbo unit gives no vision), animation advance, selection ring, picking and radar. Victory counting, Lua type lists and
   movement ignore it. `false` simply clears the byte.

### 5.1 Points that stay [M] / [L] (need a probe to settle)

- Prevent_Opportunity_Fire: exactly which branch of 0x633ae0 the flag gate sits in ([M]: same as 0x634810, no current
  target); whether a flagged unit's hardpoints fire at a target taken by `Attack_Target` while it also holds the flag
  (code says yes, [H] by absence of a gate, but not run).
- Disable_Capture: that the service is the only thing frozen (no presentation reads the counter) and that a mid-capture
  progress resumes rather than resets ([M]); the precise error SFX of the Death Star refusal (generic event, [M]).
- Set_Check_Contested_Space: relation direction (fleet owner versus shooter owner) is [M]; which planet-list entries
  other than fleets can appear in the orbit list is [L]; whether AI-driven Death Star usage (outside the Rebel A4 M12
  script) ever relies on the bit is untested.
- Set_In_Limbo: the unread-by-rendering claim (model stays visible) and the stale object-grid registration after a Lua
  limbo are [M]; HUD roster, command bar and "tracker" readers (0x30e660, 0x31b590, 0x312b90, 0x2f62f0, 0x2f64e0,
  0x14033900, 0x406e30) are classified as presentation without a full read ([M]); WeaponBehavior 0x54d090's exact limbo
  branch is [M]. A probe that puts a visible, selectable unit in limbo without `Hide` and checks (a) enemy auto-fire,
  (b) enemy ordered attack, (c) splash damage, (d) vision, (e) radar blip would settle most of these.
