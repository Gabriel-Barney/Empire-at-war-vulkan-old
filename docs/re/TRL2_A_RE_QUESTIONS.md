# trl2 lane A: RE questions (2026-11-20)

Questions for `re-analyst` (ReVa). Answers go to `docs/re/TRL2_A_RE_ANSWERS.md`. The port reads them for roadmap group
trl2 (t01, t02, t05) and tru2 t02. Q1 blocks t01. Q2-Q4 check defaults the lane already took (listed under each).

## Q1 (t01, b0237, S1): ground special weapon in a campaign space battle

In a campaign space battle at a planet whose surface holds a `Ground_Ion_Cannon` or `Ground_Empire_Hypervelocity_Gun`
(types with `Is_Special_Weapon_In_Space`), retail puts a space special weapon into the battle for its owner. Skirmish does
this in 0x293d60 (SKIRMISH_SETUP §2.3: `Standalone_Space_Maps_Special_Weapon_A/_B` at the first
DUMMY_SPECIAL_WEAPON_SOURCE object, else the map origin, registered with 0x35c520). The campaign path is not in `docs/re`.

1. Which function spawns it in a campaign space battle (battle setup 0x28fda0, the force split 0x2bce30 / 0x2bd500, the
   post-load placement, or elsewhere), and at which point of the setup order (before / after the starting forces, the
   starbase, the map objects; any synced draw)?
2. Which object type is created: the ground structure's own type, or a type it names (a field)? One per qualifying
   structure, or one per player / per type?
3. For which player: the structure's owner, the planet owner, only the defender? What if the attacker owns such a
   structure on the planet (or the structure is under construction / disabled / corrupted)?
4. Where: the first DUMMY_SPECIAL_WEAPON_SOURCE object (as Spawn_Special_Weapon) or something else; what without one?
5. Does the object count as a battle participant (victory relevance, damage, write-back to the galactic structure when
   it dies in space, retreat lists, the population cap)?

## Q2 (t02): campaign land start forces and the population list

BKG1_RE_ANSWERS §7.1 says skirmish start forces (0x2a4fc0 -> 0x29e960 with the population flag off) never enter the
population list (0x29bcd0). Does the campaign land arrival placement 0x2a00b0 register the companies that start the
battle on the map (attacker's first wave, defender's garrison) in the population list? Default taken: yes (they count).

## Q3 (t05): land post-retreat (LandRetreatCoordinator vf11, vf8)

1. Land vf8: which objects enter the retreat list (locomotor units; team containers or their members; transports)?
2. Land vf11: which objects of the retreating player are destroyed at the end, which are preserved (BT §3.4: "land
   preserves transports" [M])? Structures, turrets, landed transports, garrisoned units, heroes?
Default taken: every object of the player without a locomotor dies, except TRANSPORT-behaviour objects; the movers
retreat.

## Q4 (tru2 t02): space retreat start heading (0x533220, locomotor vf +0x218)

Is vf +0x218 on a space locomotor (SIMPLE_SPACE / capital) a turn-in-place order run at the turn rate over later frames
(with the TurnInPlaceSlowdown factors), or an immediate heading write? Does it drop the unit's current move order?
Default taken: a turn in place at the turn rate x the layer's slowdown that drops the current order (the port's space
arrival turn), from the frame after the request.
