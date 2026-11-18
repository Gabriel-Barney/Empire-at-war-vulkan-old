# mcx4 RE answers, tactical / objects share

Questions: `MCX4_RE_QUESTIONS_TACTICAL.md` (lane mcx4-tac, roadmap group `mcx4`, tasks t03-t06). Behaviour in words, no
decompiled code. Sources are named per section: **[ref]** = read by the lane in the Ghidra export
`re_reference/ghidra/StarWarsG_decompiled.zip` (function bodies, no cross-references), **[ReVa]** = the re-analyst run.

## 1. Blast falloff: `Projectile_Blast_Area_Dropoff` / `_Tiers` (Q3.2) [H, ref]
Area damage 0x4c7f70, per victim (TACTICAL_COMBAT §3.5 corrected):
- The tier count is read from the projectile type +0x488 once per blast. The type constructor 0x367250 sets it to **5**.
- factor starts at 1. Only when the type's `Projectile_Blast_Area_Dropoff` (+0xaa) is set **and** R > 0 is the
  distance cut (precise model test 0x38f900 on the segment blast centre → victim position: a hit gives the 2D distance
  to the hit point when that is shorter, a start inside the model gives 0) and the factor computed as
  1 − floor(d × (tiers / R)) × (1 / (tiers + 1)), clamped to [0, 1] (single-precision, in that order).
- Without `Dropoff` the tiers are never used: every victim in R takes the whole blast damage.
- Port before mcx4: the combat code had the gate-free formula but the catalog never filled either field (tiers 0, so
  every blast was full damage, which happened to be right only without `Dropoff`). Base FoC sets `Dropoff` True on ten
  projectiles (tiers 3, 4, 5).

## 2. Tactical tech level: `Tactical_Build_Increments_Tech_Level` / `Tactical_Destruction_Decrements_Tech_Level` [H, ref]
- Build service 0x498170, at a build's completion (after the pool / spawn step and the +0xc8 kill list): when the type's
  +0xc5 is set, L = the building player's tech (+0x84); every player in the player list that is allied with the builder
  (0x2823e0, the builder itself included) **and** has the same faction (+0x68) gets tech L + 1 (0x288980).
- Kill path 0x391310: in a skirmish (0x28afb0) an object whose type has +0xc6 lowers **its owner's** tech by 1
  (0x288980(owner, tech − 1)); no ally sweep. STATION_HANGARS_SOUND §2.9: a dying container kills its contents, so a
  tech upgrade object (DUMMY_UPGRADE in its station) takes the tech back down when its station dies [M: that the
  contents' death goes through 0x391310].

## 3. Lifetime build count (B2, partial) [H, ref]
- The lifetime count is the player's (type → count) map at player +0x198 / count +0x1a0. 0x2821b0(player, type, n)
  adds n (creating the entry at first use); the tactical completion 0x498f70 calls it with 1 before it looks at the
  builder, as the galactic production completion does.
- 0x281c70(player, type, with_allies): without allies, the player's own entry; with allies, the sum of the own
  entries of every player p whose relation in the asking player's table (player +0x370, index p's id) is 0 (allied;
  the asking player itself included when its own entry is 0). Then, in both cases, the counts of the type's company
  members (+0x958 list, count +0x960), of the first +0x938 entry and of the +0x978 list (count +0x980) are added
  (each by the same rule without allies) [M: which XML lists +0x938 / +0x978 are].
- So `Build_Limit_Lifetime_For_All_Allies` (+0x88c) in the tactical check 0x497140: 0 refuses, > 0 refuses once the
  allied lifetime count reaches it. Base FoC sets −1 on every type (32 uses): no limit.

## 4. `Disallows_Hyperspace_Retreat` (B7, partial) [H, ref]
The retreat-permission test 0x282750 reads no object-type field: game type / mode / session checks, the player's
interdict list, then the TACTICAL_SUPER_WEAPON query (CU8F_T01_T02_T07_RE §3.2 is complete). BATTLE_TRANSITIONS §3.4's
"0x282750: `Disallows_Hyperspace_Retreat`" is therefore wrong about the reader; the type +0x39 reader is open (ReVa).
→ ReVa: no reader anywhere (§11).

## 5. Cash points: `Cash_Point_Credits_Per_Second` / `_Radius` / `_Transition_Time_Seconds` (Q5.2) [H, ref]
The CASH_POINT behaviour service 0x621370 (behaviour pack at obj +0x198: +8 the capturing player, −1 at start [M], +0xc
the progress). Type fields are floats: +0x1fb0 credits per second (ctor 0), +0x1fb4 radius (ctor 200), +0x1fb8 transition
seconds (ctor 2). With n = the frames since the last service (behaviour vf +0x28) and FPS = the game frame rate:
1. **Controller scan.** For every player in the player list, in order: the GOM radius query 0x2a8fb0 on that player's
   tree (XY distance, garrisoned objects skipped; TACTICAL_COMBAT §1.6) around the point with r = the radius; the player
   counts when a found object's type has `Influences_Cash_Point` (+0x79; the field defaults to true, the markers set
   No). A second counting player stops the scan and skips the capturer update (contested). After the scan, exactly one
   counting player that is not the point's owner (obj +0x58) becomes the capturer; one counting player that *is* the
   owner, or none, leaves the capturer unchanged.
2. **Progress** (only with a capturer and T = transition seconds ≠ 0), s = (1 / T) / FPS × n: capturer = owner →
   progress := min(progress + s, 1); otherwise progress −= s, and at ≤ 0 the point changes owner to the capturer
   (0x38e5a0) and progress := 0. So an enemy first drains the owner's progress, takes the point at 0, then fills its
   own progress back to 1 over T.
3. **Payout.** The owner's player gets `Cash_Point_Credits_Per_Second` / FPS × n credits (0x27f370) when its faction is
   not `Is_Neutral` (faction +0x108; no faction pays too), the capturer is that player and progress ≥ 1.
4. Presentation: a tint of the owner's colour × progress (not modelled).
Port: `tactical::garrison::CashPointSystem` (cash_point.hpp), served by the battle every frame (n = 1) [M: the
behaviour's service interval; n keeps the totals equal either way].

## 6. `Is_Indigenous_Unit_Corruptible` (B1) [H, ref; trigger M]
SpawnIndigenousUnitsBehaviorClass method 0x5314f0 (reached through the behaviour's vtable, no direct caller), with an
object argument: when the object's type has `Is_Indigenous_Unit_Corruptible` (+0xa9; the type default is true, FoC sets
False on the animals, the bird / sketto spawn props and the Imperial guards), the current conflict has a planet object
(0x28a890) and that planet's corruption type (planet data +0x2f4, 0x397450) is 4 = `Corruption_Corrupt_Militia`, the
object changes owner (0x38e5a0) to the first player in the player list whose faction has `Benefits_From_Corruption`
(+0x10b). [M] that the argument is each unit the spawner creates (the False values on animals point that way).
Port: `TacticalBattle::sp_corrupt_owner` for every unit a SPAWN_INDIGENOUS_UNITS object spawns; a pack follows its first
member [port].

## 7. Absorb_Blaster: `Damage_Absorb_Percentage` / `Damage_Absorb_Amount` (Q6.2) [H, ref]
- Load checks of the ability data: `Damage_Absorb_Percentage` (+0x17c) outside [0, 1] and `Damage_Absorb_Amount`
  (+0x180) below 0 are reported and read as 0.
- The Take_Damage handler 0x6ee840(ability, victim, projectile): when the ability is active (vf +0x28), has its data,
  passes its target filter (0x48e290) and the projectile's `Projectile_Category` (type +0x1fe8) is neither 1 (MISSILE)
  nor 3 (GRENADE): one draw r in [0, 1] from the game LCG 0x5324d0 (seed × 0x41c64e6d + 0xbdf, bits 10..24 / 32767);
  r < `Absorb_Chance` (+0x178) absorbs the shot (returns 1) and sets the victim's health (0x3a89d0, clamped to [0, max])
  to health + `Projectile_Damage` (type +0x474) × percentage + amount; a health gain plays the effect 0x48e4d0.
- FoC: Yoda, Palpatine, Luke 0.2 / 1 (Luke 0.8), Vader 0.1 / 1.

## 8. Tags no retail code reads (Q3.1, Q4.1, Q4.2) [H, ref]
Method: every access to the field's type offset in the export (all addressing forms: byte offset, 8-byte and 4-byte
index, base + index, the type pointer at object +0x298). The method finds the known readers of comparable fields (for
`Build_Limit_Lifetime_For_All_Allies` +0x88c: the build bar 0x288b80 and the tactical check 0x497140). For each tag
below the only accesses are the type constructor 0x367250 (default) and the type copy 0x36c000 (`Variant_Of_Existing_Type`):
- `Turret_Targets_Air_Vehicles` / `Turret_Targets_Ground_Vehicles` / `Turret_Targets_Ground_Infantry` /
  `Turret_Targets_Anything_Else` (+0x5d8 / +0x5dc / +0x5e0 / +0x5e4, ctor 9999 each) and `Air_Vehicle_Turret_Target` /
  `Ground_Infantry_Turret_Target` / `Ground_Vehicle_Turret_Target` (+0xb4 / +0xb5 / +0xb6): no reader. (The +0x5d8 /
  +0x5e0 hits elsewhere are GUI and particle structures, not object types.)
- `Size_Value` (+0x898): no reader (the +0x898 reads at 0x1604c0 / 0x1655f0 are particle-emitter data reached through
  +0x118, not a type).
- `Required_Timeline` (+0xf08): no reader.
So these are retail-unused: the port keeps ignoring them (RETAIL_UNUSED in `engine/objects/tools/gen_tag_ledger.py`).
A generic by-name field read through the descriptor table (none is known) would not show in this search [L].

## 9. `Respawn_Whole_Team_When_Killed` (Q5.3, partial) [M, ref]
Reader: the hero-respawn scheduler 0x48eb10 (P4 6.2; port `GalacticSim::schedule_hero_respawn`), never in a skirmish
(0x28afb0). For a dying object D (type +0x2008 set, which is the resolved `Respawn_Tied_To_Structure` (MCX4_T10_RE_ANSWERS
§8; corrected 2026-11-18): the structure count test there; else `Is_Named_Hero` +0x13 required) whose parent (obj +0x2b0) is a squadron or a transport:
- with `Respawn_Whole_Team_When_Killed` (type +0x16): every other active member of the squadron (flags +0x3a0 & 0x42
  clear), or every object in the transport and then the transport itself, is killed with 0x391310(·, 1); the respawn
  then proceeds;
- without it: an other active member stops the scheduling ("Cannot schedule the respawn of %s yet because its squadron /
  transport still contains active objects (%s)"), so the team respawns once its last member dies.
FoC: R2D2 / C3PO (a droid team), Luke_XWing, Wedge_XWing_Rogue. [M] the parent / pack byte tests that pick the squadron
or transport branch (obj +0x348, +0x336, +0x337) are read but not mapped to names. Open: the port's call site (the
battle-death path into `schedule_hero_respawn`) and a ReVa pass on those branch tests. → Answered by ReVa in §13.

## 10. §8 confirmed by a whole-image displacement scan (Q3.1, Q4.1, Q4.2) [H, ReVa]
Method: a Ghidra script over every instruction of `/StarWarsG.exe` (1 970 469) listed each memory operand whose
displacement is +0x5d8 / +0x5dc / +0x5e0 / +0x5e4, +0xb4 / +0xb5 / +0xb6, +0x898, +0xf08 (and +0x38 / +0x39 / +0x1e88 for
§11 / §12). For each hit the base register was traced back (up to 120 instructions, through register-to-register moves)
to its definition; a hit counts as "on an object type" when that definition is a load from +0x298 (the object's type
pointer) or the return of the type getters 0x396380 / 0x3973b0. Across all 18 283 hits only one is on a type pointer
that way: the bomb step at 0x4ce6a0 (+0x1e88, §12). Every hit was then also judged by hand:
- `Turret_Targets_*` (+0x5d8..+0x5e4, int): the only dword reads are in the type copy 0x36c000; the ctor 0x367250
  writes 9999 (0x270f) to each. The other hits are qword or byte accesses, stack slots, `LEA`s of sub-objects, or
  other classes' constructors (0x29a0c0 writes 1 / −999 there, 0x3484d0, 0x33a580, 0x2d9890). **No reader [H].**
- `Air_Vehicle_Turret_Target` / `Ground_Infantry_Turret_Target` / `Ground_Vehicle_Turret_Target` (+0xb4 / +0xb5 /
  +0xb6, bool): the ctor and copy touch them only through wider stores. No byte read of these offsets is on a type: the
  three adjacent byte reads in 0x4205b0 / 0x420a40 / 0x425ac0 copy a 0x68-byte formation element; the rest are the
  front-end registry 0x47bd60 (0x33a060, 0x47b030, 0x47bf40, low-address GUI code), a task-force definition (Lua
  `Refit_To_Definition` 0x6cfbe0) and an ability data block with floats at +0xac / +0xb0 (0x72bee0). **No reader [H].**
- `Size_Value` (+0x898): every other read (0x1604c0, 0x1655f0, 0x166020, 0x167350, 0x168130) comes from a pointer at
  +0x118 (particle emitter data); the writes at 0x2de90, 0x19d200, 0x2c4370 are other classes. **No reader [H].**
- `Required_Timeline` (+0xf08): ctor, copy and one qword write in another class's ctor (0x2c4370). **No reader [H].**
Remaining gap [L]: a type pointer that arrives through a function argument and is read in the callee; every such
("function start") hit at these offsets was read and none is a type.

## 11. `Disallows_Hyperspace_Retreat` (B7): no reader [H, ReVa]
- Same scan (§10) for type +0x39: no byte read of +0x39 and no 16-bit read of +0x38 has a type pointer as its base.
  The byte reads that exist are render colour bytes (0x309f10, 0x3078c0, 0x2ffcb0), small struct copies (0x349230,
  0x34b090, 0x7293e0: 0x50-byte records), a stack flag (0x566ca0), data packs at obj +0x108 (0x4f5920) and C runtime
  code (0x77xxxx..0x7dxxxx). The type ctor clears the +0x30 block with a wider store (default false [M]), the copy
  0x36c000 copies it in a block.
- So no retreat code (permission test 0x282750, the land / space retreat coordinators, the HUD button, galactic fleet
  retreat) reads it: the tag is **retail-unused**; a unit with it set retreats like any other. BATTLE_TRANSITIONS §3.4's
  reader claim is wrong (§4). FoC sets it once, to `No` (Merchant_Freighter), which equals the default anyway.
  Port: add it to RETAIL_UNUSED and keep ignoring it.

## 12. `Projectile_Bomb_Fall_Accel_Rate` (B8): the bomb flight step [H, ReVa]
`ProjectileBehaviorClass` service vf6 0x4ce6a0, the `Projectile_Category` (type +0x1fe8) switch; pack = obj +0xe8
(`ProjectileDataPackClass`, ctor 0x55b4d0):
- While the mode frame ≤ pack +0x60 the projectile does not move (on the frame equal to it, it only plays the pack's
  sound +0xb0) [M: +0x60 = the launch frame]. Then every service first clamps the pack speed +0x90 to ≥ 0 (it adds
  0.0).
- **2 BOMB and 9 CLUSTER_BOMB:** direction = the X axis of the rotation built from the object's yaw (+0x8c) and pitch
  (+0x88), in degrees (the same direction as the straight step). Next point = position + speed (pack +0x90) × direction,
  and its z is then lowered by the fall speed f (pack +0x94); **after** that, f += `Projectile_Bomb_Fall_Accel_Rate`
  (type +0x1e88). The point goes to the per-step function 0x4d0080 (sweep → hits → move and expire, TACTICAL_COMBAT
  §3.3). No cap on f, no mode test (land and space alike), the facing is not changed by this step [M: that 0x4d0080
  leaves the facing].
- f starts at **0** (the pack ctor 0x55b4d0 and reset 0x55b570 zero +0x94); the only writer is this step. So the first
  move drops nothing, the n-th move drops rate × (n − 1), and after N moves the bomb is rate × N(N − 1)/2 below the
  straight line. **Units: per frame** (speed in units per frame, the rate in units per frame²; the service runs once
  per frame, TACTICAL_COMBAT §3.3).
- **5 STATIC_BOMB:** the case returns at once: no move, no 0x4d0080 (so no sweep, hit or expiry from this service).
- FoC: the four bombing-run bombs (TIE_Bomber / Skipray / Y_Wing `_Bombing_Run_Bomb`, Proj_TIE_Bomber_Run_Bomb) are BOMB
  with speed 4 and rate 0.20 (6 units/frame of fall after 30 frames); Proj_Cluster_Bomb (CLUSTER_BOMB, speed 12) has no
  rate, so it flies straight [M: the ctor default of +0x1e88 is 0; the ctor writes it only through a wider store];
  Proj_Harmonic_Bomb_Slave_I and Proj_Speeder_Bomb are STATIC_BOMB with speed 0.

## 13. Hero respawn: the parent tests of 0x48eb10 and where a battle death reaches it (§9) [H, ReVa unless tagged]
Slot bytes: obj +0x332 + behaviour id, −1 = absent (A2M_T04 §0): +0x336 FLEET (4), +0x337 TRANSPORT (5), +0x348 TEAM
(0x16). DUMMY_SPACE_FIGHTER_SQUADRON is a separate behaviour, 0x5e (SquadronDummyBehaviorClass, behaviour table entry
0x140a2b990), not FLEET.

**Order inside 0x48eb10(D, planet, time, skip):**
1. Nothing in a skirmish; the type +0x2008 test (§9: `Respawn_Tied_To_Structure`, MCX4_T10_RE_ANSWERS §8).
2. Respawn type: a type with +0x2a (company member, AI_GALACTIC_3) schedules the first type of the global type list
   whose `Company_Units` (+0x958, count +0x960) holds D's type; a type with +0x29 (squadron member) the first whose
   `Squadron_Units` (+0x978, count +0x980) holds it; otherwise 0x396380(D). So Droid_R2D2 / Droid_C3P0 respawn as their
   company, Wedge_XWing_Rogue as Rogue_Squadron_Space. No such type → refused.
3. Planet: the argument, else D's planet (0x3963f0 [M]), else the first planet of D's owner (category-3 query); none
   → refused. A named hero that already has an entry at that planet (0x3fa300) → refused.
4. **Parent tests**, only when skip = 0 and D's parent P (obj +0x2b0) exists:
   - **P has TEAM** (a team container: a land infantry team or a space fighter-squadron container such as
     `Rogue_Squadron_Space_Container`): any other team member (TEAM list 0x405300 / 0x4052d0) whose flags +0x3a0 do not
     have both 0x40 and 0x02 set refuses ("its teammate %s is still alive"). `Respawn_Whole_Team_When_Killed` is **not
     read here** and nobody is killed.
   - **else P has TRANSPORT** (a galactic transport: a company or hero team carried in a fleet, its contents are the
     members): with the flag every active content ((flags & 0x42) = 0; the loop does not skip D) is killed
     (0x391310(·, 1)), then P itself; without it an active content other than D refuses ("transport").
   - **else P has FLEET and P's own parent has FLEET**: the same with P's fleet members (0x432730 / 0x432710), message
     "squadron", and no kill of P. [L] which objects nest a fleet in a fleet: galactic fighter squadrons are 0x5e, not
     FLEET, so no vanilla case was found.
   - otherwise (a plain fleet in orbit, a planet) no test.
5. time ≤ 0 → `Default_Hero_Respawn_Time` (GC +0x11f0); queue 0x3fe0a0(planet's PLANET behaviour, time, type, owner,
   D +0x330); log "%s will respawn at or near %s in %g seconds".

**Battle deaths.** `Die` 0x39bdb0 step 6 calls 0x48eb10(D, none, 0, 0) for `Is_Named_Hero` / `Is_Generic_Hero` only
when the current mode's vf+0xf8 is true: 1 in GalacticModeClass (0x17dc70), 0 in LandModeClass and SpaceModeClass
(0x13b7d0). So a hero killed in a tactical battle schedules nothing when it dies. The schedule is made in galactic mode,
by "hero defeated" 0x3a01f0(hero, killer) (needs +0x13 or +0x14): final-blow byte +0x330, STORY_DEFEAT_HERO, signal
0x25, "Hero %s has been defeated by %s!" for the local player, then 0x48eb10(hero, the conflict's planet [M: resolved
through the game-mode manager 0x28aa00 / 0x28aa50], `Default_Hero_Respawn_Time`, skip 0). Its callers:
- **land battle end** 0x28bf10 → 0x2b5360: over the galactic object list, objects with +0x3a0 bit 0x04 set and 0x02
  clear [M: the "lost in the battle" mark; its writer not traced] are removed (0x391310), first those that are neither
  fleets nor transports, then the rest; each hero (0x39a600) goes through 0x3a01f0(hero, none) first;
- **space battle end** 0x28bf10 → 0x2b4660 → fleet destroy 0x431860 (recursive over nested fleets; transports through
  0x4f3a20; other contents with `Is_Named_Hero` → 0x3a01f0, then removed);
- transport destroy 0x4f3a20 (each hero content → 0x3a01f0(hero, killer), then removed): from `Die` of a TRANSPORT
  object (any mode), the land conflict release 0x2b3e90, 0x431860, 0x2aa510, 0x2aa8e0, 0x2aea10, 0x3faa10;
- 0x379df0 (contents of a removed object's behaviour-0x50 container, from 0x391310) and the galactic abilities
  0x6f8c20, 0x702790 (assassinate).
Port: `GalacticSim::schedule_hero_respawn` belongs in the battle-end write-back (galactic), once per hero lost in the
battle, with the conflict planet, the default time and skip = false (so the parent tests above apply to the galactic
containers); the tactical death handler does not call it.

## 14. `Spawn_Ability` (Ewok_Handler `Handler_Spawn`) [H, ReVa unless tagged]
`SpawnAbilityClass`: ctor 0x71d9e0, vftable RVA 0x8cdb38: +0x10 name ("Spawn_Ability", 0x71e1c0), +0x18 service
interval **1 frame** (0x18ceb0), +0x20 update 0x71e290, +0x28 accepts-target 0x71e1d0, +0x68 object-removed notice
0x71de50, +0xd0 / +0xd8 min / max range (0x5b0eb0 / 0x5b0ea0), +0xe8 load check 0x71e5b0, +0xf0 apply 0x71db50, +0xf8
remove 0x71e260 (= the detonation 0x71dff0).

**Fields** (ability object; all floats, code 0x8, whatever the table's sample column says): +0x178 / +0x17c
`Activation_Min_Range` / `_Max_Range` (ctor 0); +0x180 `Object_Type` (type reference, resolved type at +0x188); +0x190
`Creation_Delay_In_Seconds` (ctor 0); +0x194 `Recharge_Time_In_Seconds` (ctor 1.0); +0x198 `Spawn_Bone_Name` (string);
+0x1b8 `Self_Destruct_Radius` (ctor 10); +0x1c0 `Explosion_Type` (resolved +0x1c8). From the base: +0x128 = the resolved
`Target_Particle_Effect` (+0x120; ABILITY_PARTICLES §2). Load check: a negative range, delay or recharge is reported and
set to 0; `Self_Destruct_Radius` ≤ 0 is reported and set to 10.
**Per-user state** in the SAP (user obj +0x100): +0x39c the tracked bomber's id (0x3fffff = none), +0x3a0 creation
countdown (frames), +0x3b0 recharge countdown (frames), +0x3b4 the frame of the last release [M: initial values].

**Trigger.** `Activation_Style` Special_Attack (9): a candidate in the auto-target ability pick 0x37d280 whenever the
unit's targeting issues an attack on a target (TACTICAL_GAPS §1.3); the pick becomes the pending ability and apply is
retried at the targeting cadence until it succeeds (ABILITIES_HARDPOINTS §2.9). Accepts-target: the target exists, is
hostile to the user's owner (0x39a450), is not a projectile (no PROJECTILE slot +0x33a), is not dying or dead
((+0x3a0 & 0x42) = 0) and has a model (+0x2a0).

**Apply** (vf30 0x71db50, user U, target T). Refused (0) when: U or T is missing, T is not accepted, T is out of range
(0x48d820 false; range from this class's min / max getters [M]) or must be closed on (0x48e100), U has no SAP, the
recharge countdown is > 0, a bomber is tracked, or fewer than FPS frames (1 s) have passed since +0x3b4. Otherwise:
1. hold U (0x63f060(U, 1): stops the locomotor), play animation 5 (Attack; 0x3a92f0, blend 0.1, speed 1.0), snap U's
   yaw to face T (0x3a8710 with pitch and roll 0, as Grenade / Remote_Bomb);
2. creation countdown := max(1, int(FPS × `Creation_Delay_In_Seconds`)); recharge countdown := int(FPS ×
   `Recharge_Time_In_Seconds`). **The cooldown runs from the activation**, not from the spawn;
3. if `Target_Particle_Effect` is set, create it for U's owner at the world position of `Spawn_Bone_Name` on U's model;
4. register T (an earlier, different target gets End_Effect first; Begin_Effect 0x48cca0(·, U, T, 1, 1) unless T is
   already listed); return 1.

**Update** (vf4 0x71e290, every frame, argument = the frame):
1. recharge countdown −1 while > 0;
2. if no bomber is tracked, or its id no longer resolves, or its locomotor (slot 1) reports moving (0x39ade0 →
   locomotor vf+0x1c0): when the creation countdown is > 0 it is decremented; on reaching 0:
   - the registered target is gone (no entry, not found, or (flags & 0x42) ≠ 0): release U's hold
     (0x63f060(U, 0, 1)), +0x3b4 := now; nothing is spawned;
   - else: create `Object_Type` for U's owner at U's position (0x29f810), then move it to the world position of
     `Spawn_Bone_Name` (bone index 0x12d2a0, bone matrix 0x12d2c0, position setter 0x3a8f90); +0x39c := its id; play a
     sound of the bomber's type attached to it (0x373700(type, 0) → 0x2d5320) [M: which event]; release U's hold;
     order the bomber to attack T (Issue_Auto_Target 0x38d730) and to move to T: a new coordinator (mode vf+0x228 →
     0x426010) and `Coordinator::Move_To` 0x4f8200 with kind 3 = T, avoidance 0x3f, threat 1e18, flags 0;
3. else (the bomber exists and has stopped moving): **detonate** (0x71dff0).

**Detonate** (0x71dff0; also the remove vf31): if a bomber is tracked and still exists: create `Explosion_Type` for U's
owner at the bomber's aim point (0x398440: position + `Ranged_Target_Z_Adjust`); when it has a projectile pack, pack
+0x58 (shooter id) := the bomber's id and pack +0x68 := explosion type +0x440 [M: a damage-type id]; then the bomber
takes 9999 damage of kind 9 (Take_Damage 0x3a9e30) and dies the normal way. +0x39c := none, +0x3b4 := now. Then
End_Effect (0x48e920) on the registered target.
**Object removed** (vf+0x68 0x71de50, from the removal broadcast 0x3920c0 → 0x37bcb0, for each allowed and ready
ability): the removed object is the tracked bomber → +0x39c := none (no explosion, no +0x3b4 stamp); it is the
registered target → that object gets a move to its own position (a stop) and End_Effect.

**What decides the explosion:** the bomber stopping (it reaches T, T is gone so its move ends, or it is blocked), not a
distance. **`Self_Destruct_Radius` has no reader** besides the load check (no other access in the class's code
0x71d000..0x71f000 or the base 0x48c000..0x490000 [H]). [L] A bomber killed before it stops may still be found while
its corpse exists and then detonate on the next update (whether its id still resolves after `Die` was not traced).
**`Recharge_Time_In_Seconds` vs `Recharge_Seconds`:** this class keeps its own countdown (above); `Recharge_Seconds`
(+0x24) is a unit-ability record field and the handler's `Unit_Abilities_Data` is empty [M].

FoC data (UNITS_LAND_UNDERWORLD_EWOK_BOMBER.XML): Special_Attack, range 75..270, `Ewok_Suicide_Bomber_For_Handler`
(Max_Speed 1, `Ranged_Target_Z_Adjust` 7), delay 2.5 s (75 frames), recharge 3 s (90 frames), bone `MuzzleA_00`,
radius 10 (unused), `Proj_Ewok_From_Handler_Bomb_Of_Doom` (flight distance 0, blast 300 in 25, Damage_Fire). Port: no
Spawn_Ability yet; this section is its spec.
