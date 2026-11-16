# TRU1 lane A: RE questions (t02 base shields, t08 ground detonation particle)

Roadmap group TRU1 (box ABIL), lane tru1-a, 2026-11-15. Answers go into **`docs/re/TRU1_A_RE_ANSWERS.md`** (new file),
with a one-line pointer added to `docs/re/TACTICAL_COMBAT.md` §3.5 / §3.6 and `docs/PROBES.md` if a probe is suggested.
Write behaviour in words, RVAs for every rule, read vs inferred separated ([H] / [M] / [L]).

## A. Base shields (t02)

What the port has today (engine/tactical/combat): `Take_Damage` step 6 multiplies by
`Base_Shield_Vulnerability_Modifier` (GameConstants, FoC 2.0) when the victim's `kUnitInBaseShield` flag is set, but nothing
ever sets that flag. No SHIELD-hardpoint absorption (step 15, first bullet). BASE_SHIELD and AFFECTED_BY_SHIELD behaviours
are registered ids only. The sim host (`TacticalBattle::opposite_sides_of_shield`, `l1_base_shield_covers`) treats a live
BASE_SHIELD object with `Base_Shield_Radius` > 0 as active ([port], no base-power model). Shipped data:
14 BASE_SHIELD types (`E/R/U/P_Ground_Base_Shield[_Small]`, radius 740 / 370; `Base_Shield_Always_Off`, `Base_Shield_Upgrade`,
`Base_Shield_Delay_Time` 10.0; mobile shield generator); AFFECTED_BY_SHIELD on Juggernaut, Nightsisters, MAL, MDU, Pulse Tank,
F9TZ, Dark Troopers, ...; GameConstants `Base_Shield_Vulnerability_Modifier` 2.0, `Base_Shield_Speed_Modifier` 0.5;
`Base_Shield_Penetration_Particle` (34 types), `SFXEvent_HUD_[Enemy_]Base_Shield_Penetrated`.

Anchors: `BaseShieldBehaviorClass` vtable 0x89a130 (vf5 0x56ac80 = interval FPS/2, vf6 0x56b290, vf9 0x56aca0, vf10
0x56af30, vf11 0x56ac00, vf15 0x56ba00; sub_56ad00, sub_56ae30, sub_56af70, sub_56b2d0, sub_56b470; 0x59efe0, 0x59f330,
0x59f430); `BaseShieldDataPackClass` 0x5635b0 / 0x563660; `AffectedByShieldBehaviorClass` vtable 0x87b710 (vf6 0x502f70;
sub_502770, sub_502c50, sub_502ce0, sub_502e30, sub_5031a0, sub_5031c0; 0x53e0f0, 0x53e180); `AffectedByShieldDataPackClass`
0x53e110..0x53e300; `Take_Damage` 0x3a9e30; shield pack active test (+0x28 → +0x1c8, +8 == 1) from A1_READINGS §30.4 /
L1_S4_REMAINING §1.2; type field `Base_Shield_Radius` +0x21f4.

- **A1. BASE_SHIELD service (every 15 frames).** What does one service do, in order? The shield state machine: states and
  transitions (what turns it on / off: base power, `Base_Shield_Always_Off`, `Base_Shield_Delay_Time`, stun / ion stun,
  owner change, death, `Base_Shield_Upgrade`); which objects it scans (store list, tree query, owners: enemies only or all;
  2-D or 3-D radius, strict or not); what it writes on them (which object field / bit, set and clear rules); events,
  particles, SFX it raises.
- **A2. AFFECTED_BY_SHIELD.** What does the behaviour do per service (interval?) and on attach? Does it set the flag that
  `Take_Damage` step 6 reads? For which shields (enemy only? active only?). How `Base_Shield_Speed_Modifier` is applied
  (modifier list and category, or a direct speed factor; when removed). When `Base_Shield_Penetration_Particle` and the
  penetrated SFX events fire. What happens to a unit *without* AFFECTED_BY_SHIELD at an enemy shield edge (blocked by an
  obstacle? pushed out?) - just name the mechanism and RVA, it is another lane's code.
- **A3. `Take_Damage` step 6.** The exact condition read for "inside a base shield" (object field / bit and its writer).
  Is it only for AFFECTED_BY_SHIELD victims?
- **A4. `Take_Damage` step 15, "a base-shield SHIELD hardpoint absorbs everything".** Exact condition: is the victim the
  shield object itself with hardpoint name "SHIELD" (is there such a hardpoint on the shield model / type), or a victim
  inside a shield hit from outside? What does it report (out-absorbed true, absorbed amount = x, the Absorbed_By_Shields
  particle?), and does the shield object lose anything (health, a shield pool, a timer)?
- **A5. Projectiles and the dome.** Is a projectile fired from outside at a target inside an active enemy shield stopped at
  the dome (collision with the shield object / its SHIELD hardpoint)? Which shots pass: `Projectile_Does_Shield_Damage`
  false, shooters with AFFECTED_BY_SHIELD, shield pack +0xc (the "Are_On_Opposite_Sides_Of_Shield" gate in L1_S4 §1.2)? Where
  does the projectile detonate and which particle shows? Area damage from a blast outside: does it reach units inside?

## B. Ground detonation particle (t08)

Port: `ProjectileVisuals` reads `Projectile_Object_Detonation_Particle`, `_Object_Armor_Reduced_Detonation_Particle`,
`_Absorbed_By_Shields_Particle` and `_Lifetime_Detonation_Particle`. Every detonation without a victim
(`CombatSystem::expire_projectile`, engine/tactical/combat/src/combat_system_part2.cpp, called from lifetime / distance
expiry, the land straight-shot ground test 0x4d0630, grenade ground hits, rocket path ends, fuse-outs, fire-at-position
arrival) spawns the lifetime particle. `Projectile_Ground_Detonation_Particle` (70 types: Projectile_Dirt_Collide,
Missile_Explosion_Ground_Hit, bombing run / bombard land explosions) and `Projectile_Ground_Detonation_SurfaceFX` are not read.

Anchors: `ProjectileBehaviorClass` vtable 0x878d58 (vf6 0x4ce6a0), Process_Hit 0x4cb280, the straight-shot land ground test
0x4d0630, the grenade ground test (LOB_SOLVER §6.2), rocket step 0x4cfc40, the type loader field table for the tag
`Projectile_Ground_Detonation_Particle` (find its type offset), TACTICAL_COMBAT §3.3 / §3.4.

- **B1.** Which detonation paths spawn `Projectile_Ground_Detonation_Particle` and which spawn
  `Projectile_Lifetime_Detonation_Particle`? List each detonation call site (lifetime / max-distance expiry, ground / terrain
  / water hit, fire-at-position arrival, grenade landing, rocket path end, fuse, space) with the particle it spawns. Is
  there a fallback (ground field empty -> lifetime particle, or nothing)?
- **B2.** Where is the particle placed (projectile position, the ground hit point, terrain height at x/y) and with what
  orientation / owner? Are any synced draws taken on this path (count, order relative to area damage)?
- **B3.** Same questions for `Projectile_Ground_Detonation_SurfaceFX` (only whether it is a sim object or presentation; the
  port may leave it to the renderer).
