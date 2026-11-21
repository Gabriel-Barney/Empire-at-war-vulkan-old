# r3g: animation-timed gameplay (retail StarWarsG.exe, read with REA, 2026-11-16)

Group r3g ("Animation-timed gameplay"). Behaviour recovered from the binary in words; addresses are image offsets
(base 0x140000000). Tags: [H] read in the code, [M] inferred, [port] the port's own choice. The port runs these on
static clip data (frame count, fps of the model's clip), never on a playing clock: a one-shot clip of N frames at
fps F, speed s, finishes ceil(max(N - 2, 0) / (F / 30 x s)) sim frames after it starts (`anim::one_shot_frames`), and a
progress test reads n x rate / N unclamped. The clip data is the same with and without the animation service attached
(the battle reads the model through the service, else through its own model cache: the refixf t04 rule).

## 1. Reinforcement landing: Land / Takeoff (t01; extends A1_READINGS §35.2 / §35.3) [H]

- The landing object's TransportLanding service plays `Land` (anim type 0x2b) at speed 1.15 when it leaves the
  clearance wait (state 4 -> 1). State 1 passes to the unload (state 2, the 2 s x (1 + s) step timer starts) only on
  the first service where the clip's progress has passed 98 %.
- After the last unload step the landing plays `Takeoff` (0x2c) at 1.15 (0x644330) and is removed once that clip passes
  98 %. A landing destroyed while unloading is removed as before.
- [port] a landing model without the clip passes the state at once (the earlier untimed behaviour). The Land clip runs
  from the clearance: the play at attach time is rewound while the landing waits in state 4.

## 2. DEPLOY_TROOPERS (AT-AT, Veers) with clips (t02; extends REMAINING_GAPS §5.2) [H unless tagged]

DeployTroopersBehavior service 0x591a90 (interval 1). Clips: Drop Rope 0x61, Deploy Troopers 0x5e, Lift Rope 0x62,
trooper Rope Land 0x60.

1. On activation the walker's order is stopped and it is locked (0x58f6a0); it waits until it no longer moves or turns,
   then asks to deploy (0x391260).
2. Once deployed (0x39a390) it plays Drop Rope; at its end state 4 (0x590730) plays Deploy Troopers and spawns the
   troopers at their attachment bones, where they hang (Rope Slide, obj +0x3a2 |= 2).
3. At the end of Deploy Troopers each trooper is released from its Owner_Attachment_Bone (0x392160) and its LANDING
   behaviour (0x630fe0) runs from the next frame: while sliding it drops 3.9 units per frame, so the slide takes
   int(drop / 3.9) + 1 services (one service with no drop), then Rope Land plays if the model has it; when that has
   finished the trooper joins its coordinator (0x39bcb0) and is unlocked. The walker plays Lift Rope.
4. At the end of Lift Rope the behaviour turns off, the recharge starts, the walker is unlocked (0x58f600) and
   undeploys if this activation deployed it (0x3ac100(obj, 0)).

[port] states 6 / 7 (trooper squad grouping 0x590bb0, 0x58fb80) fold into 5 -> 8; the 0x39b950(obj, 45) test is not
modelled; a walker that cannot deploy counts as deployed; a trooper still hanging when its walker dies or goes idle is
released at once; without clip data on the host (`AbilityHost::anim_clip` unset) the earlier instant path runs.

## 3. Land SPAWN_SQUADRON: Structure Open / Hold / Close (t03; extends STATION_HANGARS_SOUND §2.5 / §2.7) [H unless tagged]

SpawnSquadron service 0x594880 (land mode, every 30 frames), signal handler 0x593760, bulk spawn 0x593960. No bays.

- State 0: the can-spawn checks (§2.4 items 1-5, 7, 8; the bay step is skipped) -> play Structure Open (68) -> state 1.
  No Open clip: launch at once -> state 2.
- State 1: at the first service where Open has finished, launch one unit, play Structure Hold (69, looping) -> state 2.
  A failed launch retries next service.
- The launch (0x594dc0 land branch): at the spawner, or with Spawned_Squadron_Location_Bones at a synced bone pick
  Get_Int(0, bones - 1) (no draw for one bone); the unit (a company: its members, then the team) is created there and
  moved to the fly-out point = spawn point + facing x the bone's Spawned_Squadron_Location_Flyout_Distances entry.
  The structure listens to that unit (+0x58).
- State 2 waits for signal 10 from the leaving unit (its move finished) or its removal (0x593760): stop listening,
  play Structure Close (70) -> state 3; no Close clip: state 0 with the respawn time int(now + FPS x delay).
- State 3: Close finished -> state 0 with that respawn time.
- Spawn_Garrison_On_Load (vf13 0x594400 -> 0x593960; not in skirmish, spawning enabled): builds the list, runs
  can-spawn (with its draws), and spawns every entry's max-alive count at once at one point (one bone pick); no
  Open / Close, no leaving unit.

Shipped FoC land spawners (the game XML): 17 structures - the E / R / U barracks and vehicle factories, the
advanced vehicle factory, Ground_Infiltrator_Facility, the Hutt and Pirate command centres and the Secondary_*
facilities; 14 of them set Spawn_Garrison_On_Load, and all of them name a spawn bone with a fly-out distance.

[port] the bone's facing is the structure's facing (so the fly-out direction of every shipped spawner is a guess:
filed as r3j t02); the distance without a flyout entry is the type's movement radius
(retail: a model size); Spawn_Garrison_On_Load runs at the first update after the structure is added [M: vf13's
trigger]; signal 10 is "no goal left on the mover, or on every live member of a container".

## 4. Cable attack: wraps, trip or hold (t04; extends TACTICAL_GAPS Cable / CU8K) [H unless tagged]

CableAttackAbility service 0x6f2d50 (interval 1), states at +0x138; phase fields in frames: Drop_Phase_Duration +0x184,
Pull_Phase_Duration +0x188, Paralyze_Phase_Duration +0x18c.

- State 3 (0x6f3180): +1 per service; at Drop + Pull the wrapping starts (state 4, counter 0).
- State 4 (0x6f3800): +1 per service; once the count passes 30:
  - a walker still **moving** (locomotor speed > 0.0001 or a path left, 0x556840) trips: it takes damage equal to its
    health with damage kind 8 (Cable_Attack, so its Damage_Cable death clone / CA_Die), source the user;
  - a **stopped** walker is paralyzed: its order is stopped (0x3a9880), it is locked (0x3a8e50(target, 1)) and held for
    max(Paralyze_Phase_Duration, 70) services (state 5), then unlocked and the cable ends (state 6, End_Effect 0x48e920).
- The effect ends early if the target or the user dies.

The roadmap wording ("trip after > 30 wraps") is the moving case; a stopped walker is held, not tripped.
[port] the cable render object and the circling check are not modelled (a user that stops circling does not cancel
the cable, so the port trips some walkers retail lets go: filed as r3j t03).

## 5. Crush kind (t05; extends MOVEMENT §5.5) [H]

Squash_Damage_Type (type +0x36c) is a tDamageType: the kill's damage kind, i.e. the Death_Clone key, not a
Damage_Types armour row. The type reset 0x367250 writes 14 (Damage_Crush) as its default, so a crusher without the tag
kills through the victim's Damage_Crush clones (21 FoC types; their Specific_Death_Anim_Type is Crushed). The crush
damage is applied without an armour row (Take_Damage without a projectile).

## 6. Crouch under SPREAD_OUT (t06; extends TACTICAL_GAPS §6.2) [H]

SPREAD_OUT sets the member flag obj +0x448. The locomotor's idle choice 0x395920 reads it first: crouched units idle
on Idle Crouch (71), move with CROUCHMOVE (67) and turn with the crouch turns; a change of the flag forces a new idle.

## 7. Death clip comes from the clone only (t07; extends S9_DEATH_CLONE_SHIELDS §1.3 / §1.4) [H]

DeathBehavior attach 0x58cdb0 plays the dying type's Specific_Death_Anim_Type / _Index (+0xd98 / +0xd9c). The damage
kind never picks the clip directly; it picks the Death_Clone, whose own Specific_Death_Anim_Type gives the clip (a
fire kill shows its fire clip only through a Damage_Fire clone). The port's `death_type_for_kind` mapping was removed.

## 8. Repair plays REPAIRING (t08; extends TACTICAL_COMBAT §4.13) [H]

RepairAbility apply 0x70c580 plays 0x5b (REPAIRING; random variant, blend 0.1); its update 0x70cb90 repairs once 0x5b
has finished. Hack keeps 0x5a (HACKING). Retail refuses the apply when the model lacks the clip (0x397ae0 == 0).
[port] the port cannot tell a model without the clip from a host without clip data and keeps the 60-frame fallback.

## 9. Open: the weapon's animation fire gate (roadmap group r3i)

WeaponBehavior's fire service 0x54d090 calls 0x54c910 as its gate before it fires; that gate holds fire on
animation state (flinch, a crossfade in progress, the CROUCHMOVE facing cone). The port's `fire_gate` has none of
these conditions. Recovering the exact rule (and its effect on fire timing) is left to the task that owns it.
