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
