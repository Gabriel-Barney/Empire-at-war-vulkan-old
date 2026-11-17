# STORY_REWARDS_GALACTIC: galactic, tutorial, Underworld and tactical story rewards (STY1 lane B)

Answers `STY1_B_RE_QUESTIONS.md` (2026-11-15). Behaviour read from StarWarsG.exe (FoC Steam x64, Ghidra 12.1.4 via ReVa).
Prose only. Confidence: **[H]** read in code, **[M]** inferred from partial code, **[L]** guess or not read. RVAs are
image-relative (image base 0x140000000 removed), as in the other `docs/re/` files. Shipped parameter shapes come from
`extracted/foc_data_text.zip` and `eaw_data_text.zip` (`Reward_Type` / `Reward_ParamN` lines).

## 0. Common frame

- **Dispatcher** 0x456970 [H]: switch on the event's reward enum (event +0x3c). Codes above 0x8e, and every code
  with no case, log "STORY REWARD - ERROR! Reward for event %s needs some code support!" and do nothing. Codes with
  no case today: 0x3b (unused), 0x63 ENABLE_OVERWHELMING_ODDS, 0x80 (unused).
- **Enum table** [H]: `Reward_Type` text is turned into the code by a table of 16-byte records (name pointer, int
  value) at 0x00a29140, 221 records, shared with the other story enums (FILTER_*, GROUND/SPACE/EITHER, TUTORIAL_*,
  comparisons, region, destroy and slot enums). A name that is not in the table gives code 0 (no case in the switch =
  silent no-op) [M: the unknown-name path of the XML parser was not read]. Notably **there is no DISABLE_INVASION
  code**: the string in the handler says so, but the only code is ENABLE_INVASION (0x84).
- **Parameter slots** [H]: event +0x50 / +0x70 / +0x90 / +0xb0 / +0xd0 hold Reward_Param1..5 as strings; the length
  at +0x60 / +0x80 / +0xa0 / +0xc0 / +0xe0 tells whether the parameter was given (length 0 = absent). `Reward_Param_List`
  is a vector at +0x210 (only RESET_EVENT uses it). Numbers are read with `atoi` / `atof`, so text that is not a
  number reads as 0.
- **Plot player** = the plot's player, `plot +0x638` (`event +0x228` is the plot) [H]. "Story manager" below is the
  singleton at 0x00b30350 (STORY.md; BATTLE_TRANSITIONS §1.6). "Object lookup by name" = 0x407e00 (name to game
  object type) then 0x2a87d0 (list of live instances of that type in the current mode's object manager, mode +0x18);
  a planet is therefore found by its **type name** (the same name the data uses, e.g. `Kashyyyk`) and the **first**
  instance is used [H].
- **Story manager flags and lists** (all rewards in §3 write here) [H for the offsets; see §3.8 for save and reset]:

  | Offset | Content | Setter | Reader (RVA) |
  |---|---|---|---|
  | +0x40 | event gate mode (1 = everything allowed, 0 = only listed allowed) | 0x446c90 / 0x4468e0 | 0x446220 |
  | +0x41 | "every battle must be fought" (autoresolve forbidden everywhere) | not set by a reward found | 0x4472a0 |
  | +0x1e0 | direct invasion allowed, ctor value 1 | ENABLE / DISABLE_DIRECT_INVASION | not found (see §3.7) |
  | +0x280 list | planets where invasion is **disabled** | 0x446f40 (ENABLE_INVASION) | 0x4472e0 |
  | +0x2a0 list | planets where all abilities are restricted | 0x44aae0 (RESTRICT_ALL_ABILITIES, SET_PLANET_RESTRICTED P3) | 0x4470b0 |
  | +0x2b8 list | planets where corruption is restricted | 0x44ad20 | 0x447110 |
  | +0x2d0 list | planets where the black market is restricted | 0x44ac60 | 0x4470e0 |
  | +0x2e8 list | planets where sabotage is restricted | 0x44ade0 | 0x447140 |
  | +0x300 list | planets where autoresolve is restricted | 0x44aba0 (RESTRICT_AUTORESOLVE) | 0x4472a0 |
  | +0x299 | bounty collection on, ctor 1 | 0x7b / 0x7c | death handler 0x39bdb0 |
  | +0x29a | sabotage enabled (global), ctor 1 | 0x82 | 0x2e6770, 0x3cf2e0 |
  | +0x29b | fleet combining on, ctor 1 | 0x89 | 0x3d4c80, 0x3d5530, 0x3d62a0 |
  | +0x29c | combat cinematic on, ctor 1 | 0x8a | 0x2cc080 |
  | +0x29e | campaign victory movie on, ctor 0 | 0x8e | 0x92bb0 |

  The five planet lists share one rule [H]: setter(planet, flag): `flag = 1` adds the planet if absent; `flag = 0`
  removes it if present (0x446f40, the invasion list, is the one with the opposite sense: `flag = 0` adds, so the list
  means "disabled"). Membership is by planet object pointer (the first instance of the type).

## 1. Galactic rewards

### 1.1 SET_PLANET_RESTRICTED (0x4f, handler 0x461850) [H unless tagged]

- **Parameters.** P1 planet name and P2 are both required (otherwise "incomplete parameters", nothing happens). P2:
  `atoi != 0` = restricted. Optional P3 (+0x90): `atoi != 0` also puts the planet in the story manager's
  "all abilities restricted" list (+0x2a0), **absent or 0 removes it from that list**. Optional P4 (+0xb0): second
  planet flag, default 1. Shipped data uses only `planet, 0|1` (28 files, planets Tatooine, Bothawui, Kashyyyk, Ilum
  and others), so in shipped campaigns the abilities list is cleared by every call and P4 stays 1.
- **Errors** (each ends the reward): "invalid planet %s" (name not a type), "non-existant planet %s" (no instance),
  "%s is not a planet" (the object has no planet behaviour, vtable slot 3).
- **State written.** The planet behaviour block (object +0xb8): byte +0x2e0 := (P2 != 0), byte +0x2e1 := P4 (0x3fe730).
  Plus the ability list above. Per planet, global across factions: there is no per-faction value.
- **Readers.**
  - **0x3fa420 (`Is_Planet_Revealed`)**: returns "not revealed" whenever +0x2e0 is set (before the auto-reveal and the
    reveal flag +0x2c9 are looked at; in multiplayer it is always revealed). So a restricted planet counts as
    unrevealed for every caller of 0x3fa420: route legality for non-human fleets (0x3f06d0, 0x3ef4c0, 0x3f2400 take
    the planets of a hop), AI income and population loops (0x2abb20, 0x2ac320, 0x2ac700, GALACTIC economy
    notes), the Lua `GalacticFreeStore.Move_Object` refusal ("restricted planet", LUA_API) via 0x68f5b0, 0x4d3990 and a
    few more (14 callers). [H callers, M that all of them mean "movement or AI target refused"]
  - **0x3fa270 (planet blocked for a fleet)**: true when +0x2e0 is set and either no fleet is given, or the fleet's
    owner is a **human** player (+0x62) and the story callback 0x326920 does not allow it (that function says "allowed"
    for a hero or lone special unit in some cases and when an active plot's `+0x660` script hook returns true).
    Callers: 0x3bd20 (move command path: aborts the command and raises a story-manager event through 0x327bd0),
    0x56a1e0 (fleet locomotor, each hop and arrival: stops the fleet, 0x569980), 0x5af260 (FleetManagement event drop
    placement). So humans are refused at command time and on arrival; the AI is refused through the reveal check.
    [H for the tests, M for the exact effect of each caller]
  - **Visual** 0x3fe1b0: when both +0x2e0 and +0x2e1 are set, the planet object gets the "restricted" look (tint and
    selection off: 0x3a8bd0 / 0x3a8ba0 / 0x3a8b70); with P4 = 0 the planet is restricted but looks normal. [H test, M look]
  - **Ability list** reader 0x3d2020 (planet ability slot builder, the `TEXT_ABILITY_SLOT_*` strings) [H call, M what it
    hides].
  - Not read: invasion, building and GUI selection do not read +0x2e0 directly (the scan of the planet block found
    only 0x3fa270, 0x3fa420 and 0x3fe1b0 as readers; the whole-image scan covered every `+0x2e0` byte compare). [M]
- **Defaults and save.** The block is the planet's own state (saved with the planet); no default beyond the planet type
  [M]. The story-manager lists are in the manager's save (§0.8 of §3).

### 1.2 PLANET_FACTION (0x4c, handler 0x460e70) [H]

- **Parameters.** P1 planet name, P2 faction name, both required. Planet resolved by **type name** (first instance), the
  faction by the faction-table lookup 0x3303d0 (shipped: Empire, Rebel, Underworld, Pirates, Neutral).
- **Failures** (logged, nothing changes): incomplete parameters, "invalid planet", "non-existant planet", "invalid
  faction".
- **Effect.** Calls the planet **Change_Owner routine 0x3f7280**, the same one the Lua `Change_Owner` uses
  (GALACTIC_ECONOMY §"Forced transfers"): returns at once with no effect when the owner's faction already equals the
  target; else clears the conflict, resets the starbase (0x4bda40), re-evaluates the space queue, resets the starbase
  owner, retreats / transfers units (0x4b7670, 0x4b7860), clears political breakdown, raises planet signal 0x26 and the
  owner-changed notifications, and finally applies the new owner at full control through 0x3fb040. 0x3fb040 is the
  routine that calls the story **conquest dispatch (0x326c00, the STORY_CONQUER / CONQUER_COUNT notification)** [H for the
  chain 0x460e70 to 0x3f7280 to 0x3fb040 to 0x326c00; M that the dispatch fires on every call, the condition inside
  0x3fb040 was not read]. It does not spawn or destroy structures itself beyond what 0x3f7280 does. [M]
- Neutral is a valid target (shipped `Neutral`, `Pirates`).

### 1.3 FORCE_RESPAWN (0x66, handler 0x45f1d0) [H]

- **Parameters.** One: the hero (team) type name, P1. No planet parameter. Missing P1: "incomplete parameters"; type
  not found: "invalid hero unit type %s".
- **Effect.** Walks every planet object in the galaxy (object category 3 list). Each planet behaviour has a **hero
  respawn queue** (list at planet behaviour +0x240: records of hero type at +0x18, ready time at +0x10, two more ints).
  For every planet whose queue holds a record for that type, the record's ready time is **set to the current mode time**
  (mode +0x10) via 0x3f8000, so the normal respawn service sees it as due on its next pass. Nothing is placed or created
  by the reward; the planet where the hero comes back is the planet that already has the record. Alive hero: no record,
  so nothing. Queued: due now (the timer is skipped, not the respawn). [H for the code; M for what the respawn service
  then does with a due record]
- Shipped: 13 files, always one parameter (`Urai_Fen_Team`, `Sundered_Heart`, `Boba_Fett_Team`, `Han_Solo_Team`).

### 1.4 REMOVE_UNIT (0x03, handler 0x4612a0) [H]

- **Parameters.** P1 type name (not found: nothing happens, no log). P2 optional: **any non-empty text = "all
  instances"** (not read as a number); P3 is not read (shipped `True` / `1` there is ignored).
- **Which objects.** Not filtered by owner. Pass 1: walks the whole object list of the current mode (mode +0x18 + 0x40);
  every object whose type equals P1 and whose state byte +0x3a0 has none of 0x46 set (not dead / being removed) gets the
  **silent removal** 0x391310(obj, 1). With P2 empty only the first match is removed and the handler returns (0x3a:
  also before pass 2). It first calls 0x3def00 on the galactic GUI singleton 0xb27f60 with 0 (drops the selection
  display) [M].
- Pass 2 (reached only for "all", or when nothing matched in pass 1): for every planet, walks that planet's hero respawn
  queue (the list of §1.3) and deletes the records whose hero type is P1 or whose type's first member (team type
  members at +0x958, count +0x960) is P1 (0x3fdb40). That is how a **queued hero respawn** is cancelled. Per record,
  P2 empty returns after the first deletion.
- **Events.** 0x391310 is the plain removal: it does not reach the death handler 0x39bdb0, so **no bounty, no destruction
  event, no Take_Damage** (A2M_R2_RE_B 0x391310 note). No population free is explicit here; removal from the owner's
  object lists frees what those lists account for. [M]
- Shipped: 11 files (`Sundered_Heart`, `Droids_Team`, `Boba_Fett_Team`, `Smuggler_Team_R`, `Han_Solo_Team`); the "remove
  all" variant is P2 = `1` (comment in the data: "Remove all instances of this type").

### 1.5 NEW_POWER_FOR_ALL (0x26) and REMOVE_POWER_FROM_ALL (0x27) (inline in 0x456970, core 0x288890) [H]

- **Parameters.** P1 object type name, P2 power (ability) name; both required. Shipped: `Droid_R2D2 R2D2_SLICER` (grant
  and revoke), `Boba_Fett Boba_Fett_Neutralize_Hero` (revoke). Unknown type: "Unable to reward %s with power %s.
  Unknown object type" (or "Unable to remove power ...").
- **What "power" is.** A named **special ability of a unit type** (the type's ability list at type +0x1a98, matched
  case-insensitively against P2 on each live unit). The "for all" is only: every live object of type P1 **owned by the
  plot player** (not all players); the object must have its ability block (+0x100).
- **State.** Two things. (1) The plot player has a table at player +0x378 keyed by (type, lowercased power name) with a
  boolean (+0x2c): NEW stores 0, REMOVE stores 1. If the key exists with the same value, nothing changes and the reward
  logs "It may already have been granted / revoked". (2) For a changed entry every live unit of the type owned by the
  player has the ability enabled (grant) or disabled (revoke) at once through 0x37df00 (it only acts when the unit's
  current ability state is the opposite). 0x37df00's internal state values (0, 1 and what a disabled ability still
  shows) were not decoded. [H flow, L ability-state meaning]
- **Readers of the table** (new units of the type picking up the override) were not traced. [L: probably the unit
  spawn path reads player +0x378, but it was not found]
- Persistence: the table lives on the player (saved with it) [M].

### 1.6 MOVE_FLEET (0x1e, handler 0x460050) [H unless tagged]

- **Parameters.** P1 from-planet, P2 to-planet, P3 FILTER name; **all three required, otherwise it returns silently**
  (no log). Shipped: `NalHutta Kashyyyk FILTER_ENEMY_ONLY`, `NalHutta Bothawui FILTER_ENEMY_ONLY`, `Corellia Tatooine
  FILTER_FRIENDLY_ONLY`.
- **FILTER values** (table 0x00a29140): FILTER_NONE 0, FRIENDLY_ONLY 1, NEUTRAL_ONLY 2, ENEMY_ONLY 3,
  FRIENDLY_AND_NEUTRAL 4, ENEMY_AND_NEUTRAL 5, FRIENDLY_AND_ENEMY 6. The name is hashed (0x215a30, no case folding) and
  looked up in the enum map by 0x45c3f0; unknown name gives 0. **Only 1 and 3 do anything**: the test per fleet is 0x39a2c0
  (is the fleet's owner a friend of the plot player, 0x2823e0): FRIENDLY_ONLY takes the fleets that pass, ENEMY_ONLY the
  ones that fail (so a neutral owner counts as "enemy" here). Every other value selects nothing and the reward ends
  silently.
- **Which fleets.** The objects present at the from-planet (the planet's occupant list, planet block +0xc0) that pass
  the filter. If a planet name does not resolve, "Unable to find planet %s" (text copied from FLASH_OBJECT) and stop.
- **What happens.** Not a teleport. The selected fleets are merged into the first selected one (one fleet-management
  event, kind 3, per extra fleet, queued on the galactic event queue 0xb2ed18), then a route from the from-planet to the
  to-planet is computed with the pathfinder 0x3f0010; if it finds one, a normal **travel command** for the merged fleet is
  queued (0x3c41c0 / 0x3c4a30) and "Moving fleet from %s to %s" is logged. No route: nothing moves (a human plot
  player's route can fail on a restricted planet, §1.1). Arrival, battle and conquest are the normal arrival rules
  (BATTLE_TRANSITIONS). [H for the structure, M for "kind 3 means merge"]
- **Acting player.** The plot player; for ENEMY_ONLY the commands are issued as the **first entry of the plot player's
  enemy list** (0x281400 over 0x294910) [M], so the enemy fleets are moved by their own side.

### 1.7 ENABLE_DIRECT_INVASION (0x58) / DISABLE_DIRECT_INVASION (0x59) [H write, L reader]

- No parameters. Sets story manager +0x1e0 to 1 / 0 and logs "Direct invasion has been ENABLED / DISABLED". Global,
  not per player. Default 1 (constructor 0x445ad0), saved with the manager (0x447310 writes +0x1e0, the loader 0x448ed0
  reads it).
- **Reader: not found.** No instruction of the manager's own methods reads +0x1e0 and none of the 55 functions that
  reference the manager's address reads that byte; a whole-image scan for `byte [reg+0x1e0]` timed out half way and
  found only unrelated classes. The likely reader is the galaxy-map GUI drop handler 0x3d6c50 (the "drag a fleet onto an
  enemy planet" path, which reads the invasion-disabled list instead). **Open.** Shipped: DISABLE_DIRECT_INVASION in
  4 files, ENABLE_ in none.

### 1.8 SET_PLANET_SPAWN (0x48, handler inline) and SET_SPAWN (0x42) [H code, L effect]

- **SET_PLANET_SPAWN.** Needs P1, P2 and P3 all present, else "Set planet spawn failed due to incomplete parameters".
  P1 planet name (first instance), P2 spawn name, P3 float (`atof`). It writes the float into a map in the planet block
  (+0x2d0, keyed by the CRC of P2) through 0x4b6e00. The reader of that map was not found. [L for the meaning:
  "indigenous spawn rate / chance per spawner name"]
- **Shipped use is a no-op.** `INTERVENTION_INDIGENOUS_REVOLT.XML` (both games, two events) sets **only
  Reward_Param3** (`1.0`, `0.0`): P1 and P2 are empty, so the handler logs the "incomplete parameters" error and does
  nothing. The question's "in no shipped XML" is wrong in this one respect.
- **SET_SPAWN (0x42)** is a different reward: P1 and P2 required (P2 `atof`), then 0x531070(P1, value), not traced.

### 1.9 Other galactic rewards touched while reading (for completeness) [H]

- **CHANGE_OWNER (0x2d)** is in §4.1 (it is the same code in both modes).
- REMOVE_CORRUPTION (0x7d) is in §3.

## 2. Tutorial rewards

### 2.1 LOAD_CAMPAIGN (0x23) [H]

- P1 campaign name (copied, **upper-cased**, hashed with 0x215a30), P2 faction index (`atoi`; the log prints "EMPIRE"
  for non-zero and "REBEL" for 0, but the **number** itself is passed on, the Underworld comment in the data uses 6). Both
  required, else "Requires two parameters: campaign name & rebel/empire index (0 or 1)". Optional P3 (`atoi`, stored only
  if below 3) and P4 (stored only if below 6).
- Lookup [H] (0x4792f0, 0x479320, campaign table 0xb31160): P1 is upper-cased and hashed; 0x4792f0 finds the campaign
  with that name. **P1 only selects the Campaign_Set**: its set id (+0x208, from `Campaign_Set`) goes to 0x479320, which
  walks every campaign and returns the first one with the same set id whose `Starting_Active_Player` (+0xa8, resolved to
  a faction) is **faction-list entry P2** (the faction type vector 0xa172e0, XML load order). So P2 is a plain faction
  index, the same index space as the GC menu's faction combo (0x95ca0 / 0x950f0 / 0x957a0 call 0x479320 the same way),
  not a side flag: 0 and 1 are the first two factions, FotR's 6..11 are its later factions. The started campaign is the
  set member whose active player is that faction, which need not be P1 itself. There is **no range check** on P2 (the
  vector is indexed directly) [H read; what an out-of-range index does is L]. The log's "REBEL" / "EMPIRE" text only
  means P2 == 0 / P2 != 0.
- Not found [H]: P1 unknown, or no member of its set for faction P2: the log "STORY REWARD - Unable to find campaign data"
  and **nothing else**: the pending slot, P3 and P4 are not touched and the running game goes on. P1 or P2 missing: the
  "Requires two parameters" log and nothing. Reward parameters are parsed with `atoi`, so a non-numeric P2 is faction 0.
- Found [H]: the record goes to the **story / tutorial controller**, a global object at 0xb30350 (ctor 0x445ad0): field
  +0x80 (= the address 0xb303d0, which is why the earlier reading saw a bare global) := campaign record; P3 (`atoi`, kept
  only when 0..2 unsigned) -> +0x88 (0xb303d8); P4 (kept only when 0..5) -> +0x8c (0xb303dc). Both start as -1 (ctor) and
  an out-of-range P3 / P4 leaves the previous value. A second LOAD_CAMPAIGN before the consumer runs overwrites the
  record (last one wins).
- **Consumer: 0x44a3f0, the controller's per-frame update** [H], called from the main loop 0x5d990 in the
  simulation-frame step, after the game mode manager update (0x28d400), the player list update and the current mode's
  per-frame calls. Nothing else dereferences the record: the scan of every RIP-relative, absolute and image-base reference
  to 0xb303c0..0xb303e8 finds only the dispatcher writes, three null tests (below) and this function, which reaches the
  fields through the object base. When +0x80 is set, in this order:
  1. It calls the controller's callback +8, set once at startup by 0x447190 (from 0x4b250) to **0x5f6b0 with argument 1,
     the "end current game" path** (the same one a player leaving uses): in single player it queues the local player as
     leaving, then tears down: every game mode on the stack is destroyed (0x28d940 pops and deletes the galactic mode and
     any tactical mode above it, resets the frame clock and the type / object registries), the player list is reset
     (0x297630), the GUI / audio / event state is cleared (0x1c1e00, 0x3027d0, 0x2d2790, ...). Its last step would open
     the main menu (0xc58e0) only when the pending record is null, so it does not. No game-end, battle-summary or victory
     screen is part of this path. [H calls read; M for what each cleanup call clears]
  2. 0x28dbe0 (game mode manager "start campaign", the same function the GC menu's Play button uses in 0x957a0) with
     the controller's +0x78 (set at startup to the same start parameter the menu passes, 0xa61a10), the record, "new game"
     (not a load), **difficulty 1** and no faction override. Because the mode stack is now empty, Start_Game 0x28e890
     takes its fresh-start branch (frame clock and player list reset, new galactic mode, galaxy load), and 0x28dbe0 sets
     every player up from the campaign (0x289a90), loads its story plots and applies its starting credits, tech and forces
     (0x292050 / 0x291f70 / 0x292590 / 0x291d80). The new campaign's `Starting_Active_Player` faction is the human side,
     as for a menu start [M: the local-player assignment inside the player setup was not traced].
  3. If +0x88 (P3) is 0..2: 0x28d760 sets the game difficulty (GMM +0xb8) and every AI-controlled, non-human player's AI
     difficulty to P3. Without P3 the new campaign runs at difficulty 1 (Normal), whatever the old game used.
  4. If +0x8c (P4) is 0..5: for **every** player in the list, 0x288980(player, P4, 1, 1) sets the tech level (clamped to
     0..the player's max tech) and runs the tech-unlock pass for `Build_Initially_Locked` types below that level.
  5. +0x88 / +0x8c := -1, +0x80 := 0.
- Delay [M]: the reward is applied inside the story update of a simulation frame and 0x44a3f0 runs later in the same
  frame step, so the switch starts in the same or the next simulation frame. There is no timer. The ~30 s seen in the
  FotR run is the teardown plus the synchronous load of the new galaxy. Whether a loading screen is drawn meanwhile was
  not traced [L]; with no simulation frames (game paused) the consumer would not run [L].
- Kept across the switch [M, from what the path does not write]: the game options record 0xb1549c (start credits / tech
  bounds, option bits) is **not** re-copied from the GC menu (0x957a0 copies it before its own start; this path does not),
  so the old game's options stay; the player profile is untouched. Everything in the old game (planets, units, credits,
  story state, players) is gone.
- The three other readers of +0x80 only test it for null [H]: 0x5f6b0 (step 1 above, also the multiplayer leave path),
  0x884c0 (battle-summary dialog closed) and 0x93ef0 (movie finished). Each skips its return to the main menu when a
  campaign is pending, because 0x44a3f0 starts it on the next frame anyway. 0xb303d8 / 0xb303dc have no reader outside
  the dispatcher and 0x44a3f0.
- Retail run modc_t45 (2026-11-17) matches: in Fall of the Republic's one-planet loader GC a Lua popup fires this reward
  (P1 `Clone_Wars_Medium_Era_2_EMPIRE`, P2 the faction index, P3 the difficulty; `StoryUtil.LoadCampaign`, event
  `Template_Load_Campaign` in `Player_Agnostic_Plot.xml`); the era campaign's galaxy replaces the loader galaxy within
  about 30 s with no game-end screen, and the credits change to the new campaign's starting credits.
- Shipped: 19 files, `Tutorial_One..Seven` and `Full_Story_Campaign_*` with side 0 / 1; the Underworld tutorial ends with
  a commented-out LOAD_CAMPAIGN.
- Port (modc t45): `StorySession` queues the request with P3 / P4 kept under the same ranges. The game host
  (`App::story_reward_step` → `load_story_campaign`) takes it on the next frame and resolves the target with
  `game::load_campaign_target` (set by P1, member by faction index P2). It then replaces the session: the target's
  `Starting_Active_Player` plays, difficulty P3 or 1, the old game's options, seed and battle settings kept, no end
  screen. An unknown target leaves the game running. [port] P4 (every player's tech level) is not applied: no shipped or
  reference-mod data sets it. The headless tools (`alamo_headless`) do not switch campaigns.

### 2.2 TUTORIAL_PLAYER (0x60) [H]

- P1 faction name (required: "needs param1 = player faction"). Finds the faction, then the plot-independent **player of
  that faction** in the player list, and sets that player's byte **+0x3c0 := 1**. Unknown faction: error (the text is the
  SET_MAX_TECH_LEVEL one for the no-player case, a copy-paste). It is **not** a "local player" setter; it marks a
  player. Nothing in the handler clears it.
- Reader of player +0x3c0: 118 instruction sites; the ones checked: the battle-prompt shell choice 0x3ca480 (single
  "Begin" shell, GALACTIC_DIALOGS §1.4), the story-allow test 0x326920, many GUI components. So a +0x3c0 player gets the
  tutorial-style forced UI: no auto-resolve button, retreat locks and the scripted-lock gates. [M]
- Shipped: 25 files (`Rebel`, `Empire`, `Underworld`).

### 2.3 FINISHED_TUTORIAL (0x79) [H]

- P1 tutorial name (required). The name's raw-byte CRC (0x215a30, case-sensitive, length taken from the parameter's
  length field) is appended as a decimal text to the comma-separated list kept in the options record 0xa7c230 (0x203ee0;
  duplicates are not appended, test 0x203c40), and the options are written to disk at once (0x203330). The list is the
  persisted key **`TutorialsPlayed`** (string 0x851bb0, used by the options writer 0x203330 and loader 0x2029d0). So it
  is a profile / options value, not save-game state. The readers (the select-tutorial dialog, `TEXT_SUGGEST_TUTORIAL`
  0xe9920) were not opened. [H write, M readers]
- Shipped: `TUTORIAL_ONE .. TUTORIAL_SIX` (14 files).

### 2.4 DISABLE_EVENT (0x10, 0x4468e0) and ENABLE_EVENT (0x11, 0x446c90) [H]

- **Parameters.** P1 event kind (enum: TUTORIAL_CLICK_GUI 1, TUTORIAL_CLICK_PLANET 2, TUTORIAL_DRAG_FLEET 3, TUTORIAL_ZOOM 4,
  TUTORIAL_ALL 5); P2 and P3 optional strings (GUI component name, then second name). Without P1 nothing happens.
  Unknown kind name = 0 = nothing.
- **State** (story manager +0x40 mode and two lists of (kind, P2, P3) records): `TUTORIAL_ALL` resets everything:
  DISABLE sets mode 0 ("only listed events are allowed") and clears both lists; ENABLE sets mode 1 ("everything is
  allowed except listed") and clears both lists. Otherwise: DISABLE adds the record to the **blocked list** (+0x30,
  skipping an identical one); ENABLE, in mode 0, adds the record to the **allowed list** (+0x18), and in mode 1 deletes the
  matching record from the blocked list. Records match on kind and both strings with an exact, case-sensitive,
  length-checked compare. Note the asymmetry: in mode 0 a DISABLE of a single name does nothing (the blocked list is not
  read in that mode), and in mode 1 an ENABLE of a name that was never blocked does nothing.
- **Readers.** 0x446220(kind, name1, name2) returns "allowed". Mode 1: a record with the same kind and names blocks
  (record with an empty first name blocks the whole kind); no record means allowed. Mode 0: allowed only when an allowed
  record matches (an allowed record with an empty first name allows the whole kind); in addition the four GUI buttons
  `b_option_g`, `b_option_t`, `b_story_arc_g` and `b_story_arc_t` (kind TUTORIAL_CLICK_GUI) are **always** allowed.
  Callers (the GUI handlers that would fire the tutorial notification, 0x2f6c30, 0x2fae90, 0x2fd330, 0x3e4660, 0x3e48a0,
  0x3e49f0, 0x3e8a80): an event that is not allowed is dropped before it reaches the story. [H for the function, M for
  "what each caller drops"; TUTORIAL_CLICK_GUI names are the shipped `b_retreat`, `b_fast_forward`, `queue00`,
  `g_ground_sell`, `market_close`, `b_Create_NN`, `b_droid_help`]
- Reset to mode 1 with both lists empty by 0x448dc0 (§3.8). Saved with the manager.
- Shipped: DISABLE_EVENT 19 files, ENABLE_EVENT 7 files.

### 2.5 SKIRMISH_RULES (0x68) [H write, M effect]

- P1 required (`atoi != 0` on, else off); error text if missing. Writes the global byte **0xb155c5**. Its only reader
  is the credit-add routine **0x27f370**: after adding the amount to the player's credits it normally clamps to the
  player's credit cap (player +0x74) **unless** the cap is negative, or the current mode object's vtable +0xf8
  answers "false" **and** 0xb155c5 is set. In other words: with the flag set and a mode that does not answer
  "skirmish-like", income and CREDITS rewards **are not clamped to the cap**. [H code, L for the naming]
- Shipped: 4 files, value 1.

### 2.6 RESET_GALACTIC_FILTERS (0x69) [H]

- No parameters. On the galactic GUI singleton 0xb27f60, 0x3ca430 calls the "reset" virtual (+0x1d0, argument 1) of the
  four filter GUI components 0x3f..0x42 and zeroes the GUI's field +0x21c4. Pure UI state, not saved. [H]

### 2.7 SET_SANDBOX_OBJECTIVES (0x65) [L]

- No parameters. Runs 0x32bd10 on the plot's story-mode object (`plot player +0x630`). It needs that object's sub-plot
  (+0xd8); without it logs "Unable to set sandbox objectives due to missing subplot" and stops. The function is 9.5 KB;
  the strings it uses are `Sandbox_Objective_1 .. _4`, `_Complete`, `_Failed` and `_Enable` variants (the names of story
  events of the sandbox sub-plot to arm or complete, per skirmish objective), and the sandbox plot files
  `sandbox_standard.xml`, `sandbox_land_capture.xml`, `sandbox_land_destroy.xml` are named in 0x329530 (not this
  function). The body was not decoded. Shipped: 6 files. **Open.**

## 3. Underworld rewards (all write the story manager)

Shapes: planet-bound rewards take P1 planet name (type name, first instance) and an optional P2; **P2 absent = 1**, a
given P2 is `atoi != 0` (shipped: `Felucia 0`, `Shola 1`). Errors: "invalid planet" (name not a type), "non-existent planet"
(no instance). All are global flags per planet, not per faction. Defaults come from the constructor 0x445ad0
(list empty, flags as in §0).

### 3.1 ENABLE_INVASION (0x84) [H write, M reader]

- P1 planet, P2 default 1. **Flag 1 = invasion allowed** (removes the planet from the disabled list), **flag 0 = disabled**
  (adds it). Log: "Invasions DISABLED at %s". Handler error strings say SABOTAGE_STRUCTURE / DISABLE_INVASION
  (copy-paste); no DISABLE_INVASION code exists.
- Readers of the list (0x4472e0, true when the planet is **not** in it): 0x3d62a0 (the planet-click / fleet-arrival GUI
  path; logs "Invasion at planet %s has been disabled through script" and returns) and 0x3d6c50 (the galaxy-map drag-and-drop
  handler: a fleet dropped on a planet of another player, the command is ignored). So the human invade action from the
  GUI is refused; scripted AI invasion goes through Lua tasks and was not seen to read the list. [H callers, M scope]
- Shipped: 4 files (`NalHutta_Prologue 0`, `Mandalore_Prologue 0`, `Felucia 0`, `Shola 0/1`, `Saleucami 1`).

### 3.2 RESTRICT_SABOTAGE (0x88), RESTRICT_CORRUPTION (0x86), RESTRICT_BLACK_MARKET (0x87), RESTRICT_ALL_ABILITIES (0x85) [H write, M readers]

- Same shape (P1 planet, P2 default 1 = restricted, 0 = unrestricted). Lists at +0x2e8, +0x2b8, +0x2d0, +0x2a0.
- Readers: sabotage 0x2e6770 (the "can this planet be sabotaged" test also requires the global sabotage flag +0x29a and a
  target structure test 0x3a0910); corruption 0x2e59b0 and 0x2e6500; black market 0x2e5740 (each walks the planet's
  possible-ability table and hides / refuses entries when the planet is in the list); all-abilities 0x3d2020 (the planet
  ability slot builder: restricted planet gets no slots). What each refuses: the corresponding **Underworld planet
  ability (sabotage, corrupt, black-market purchase) on that planet for the human player's GUI**. The AI side was not
  traced. [M]
- Shipped: RESTRICT_SABOTAGE and RESTRICT_CORRUPTION (Yavin, The_Maw, Shola, NalHutta_Story, Kessel).

### 3.3 RESTRICT_AUTORESOLVE (0x8b) [H write, M reader]

- Handler strings call it ENABLE_AUTORESOLVE (copy-paste). P1 planet, P2 default 1; **flag 1 = autoresolve DISABLED
  there** (planet added to the +0x300 list), 0 = enabled. Readers 0x4472a0 (true when the global +0x41 byte is 1 or the
  planet is in the list) from the battle prompt 0x3ca480, the retreat / resolve UI 0x30d710 / 0x30e2f0 and both auto-resolve
  paths 0x2b76a0, 0x2b92e0 ("this battle must be fought"; BATTLE_TRANSITIONS §1.6). [H callers]
- ENABLE_AUTORESOLVE (0x2a) and DISABLE_AUTORESOLVE (0x29) are dead: log "no longer working" only.

### 3.4 ENABLE_FLEET_COMBINE (0x89) [H write, M reader]

- P1 optional, default 1; `atoi != 0` on. Writes +0x29b, ctor 1. Readers: the galaxy-map drop handler 0x3d4c80 (a fleet
  dropped onto a fleet **of the same owner**: the "GUI_Drop_Fleet" merge request is made only when the flag is set),
  0x3d5530 and 0x3d62a0 (same drop paths, the "drop onto own fleet" case returns when the flag is off). So **off =
  the human cannot merge fleets by dropping one on another**. Shipped: 0 in three files, 1 in two.

### 3.5 ENABLE_BOUNTY_COLLECTION (0x7b) / DISABLE_BOUNTY_COLLECTION (0x7c) [H]

- No parameters; +0x299 := 1 / 0. Reader: the object death handler 0x39bdb0, which calls the bounty payout 0x392600 only when
  the flag is set (A2M_R2_RE_B): so **every death that reaches the death handler**, tactical and galactic, pays or not.
  Default 1. The auto-resolve payout is not a separate reader. Plain removals (REMOVE_UNIT, §1.4) never pay.

### 3.6 ENABLE_CAMPAIGN_VICTORY_MOVIE (0x8e) [H write, M reader]

- P1 optional, default 1 (`atoi != 0`); log prints the value. +0x29e := flag, constructor 0 (movie **off** by default),
  reset to 0 by 0x448e30. Reader 0x92bb0 (campaign-end dialog builder, MAIN_LOOP T11): with the flag 0 **or in multiplayer** it
  takes the "no movie" path (clears the movie name); with 1 it picks and plays the faction's campaign victory movie
  before the score dialog. [M]

### 3.7 ENABLE_COMBAT_CINEMATIC (0x8a), ENABLE_SABOTAGE (0x82), ENABLE_GALACTIC_CORRUPTION_HOLOGRAM (0x8c) [H]

- Combat cinematic: +0x29c (ctor 1), P1 default 1; reader 0x2cc080 (cinematic camera, event kind 0x4e, skip when 0).
- ENABLE_SABOTAGE: +0x29a (ctor 1), P1 default 1; readers 0x2e6770 and 0x3cf2e0 (sabotage slot builder; needs the flag).
- Corruption hologram: P1 planet, P2 default 1; sets planet block byte +0x2e4 (per planet, not the manager); reader not looked
  up.

### 3.8 Save, reset

- The manager's **save routine 0x447310 and load routine 0x448ed0 handle** the offsets +0x40, +0x41, +0x1e0, the five
  planet lists and the flags +0x298..+0x29e [M: the offsets seen in the routines, the field order was not mapped]. So
  restrictions, flags and event gates survive a save and load.
- **Reset 0x448e30** (caller 0x3d3200, the galactic GUI initialisation reached from the mode entry 0x4b21f0 / 0x4b23c0):
  clears the ability / corruption / black-market / sabotage / autoresolve / invasion lists, sets +0x29b := 1,
  +0x29d and +0x29e := 0. **Reset 0x448dc0** (caller 0x3027d0, on mode transitions): +0x40 := 1, +0x299 := 1, +0x29a := 1,
  clears the event lists. A reward written at campaign start therefore lives until the next GUI init or mode
  transition resets it, unless it was saved and loaded. [M on the exact trigger points]

### 3.9 REMOVE_CORRUPTION (0x7d) [H]

- P1 planet (required), P2 optional seconds (`atof`), default **10.0** (constant at 0x805898). Calls 0x3f6770(planet
  behaviour, planet, 0, delay): starts the removal of corruption on that planet with that delay (log "Removing
  corruption on %s in %d seconds"). Not a planet: "not being a planet". Shipped: 1 file.

## 4. Tactical rewards

### 4.1 CHANGE_OWNER (0x2d) and DESTROY_OBJECT (0x2e) [H]

- **CHANGE_OWNER.** P1 object type name, P2 faction name, both required (unknown faction or no player of it: silent). P3
  optional count: absent = **first instance only**; `atoi` 0 = **all**; n > 0 = the first min(n, instances). Each
  chosen object: if it is an ordinary object (byte +0x335 = -1, i.e. no galactic planet block) it gets the generic
  owner change 0x38e5a0(obj, new player id, no list override), which also raises the `ObjectOwnerChanged` event (signal
  0x1a); a planet-block object gets the planet owner updates 0x3faa10 + 0x3f8060 instead. This is **not** the full
  Change_Owner 0x3f7280 (that is PLANET_FACTION). Lookup is by type name in the current mode's object list, so in a
  tactical battle it finds the battle's objects. [H flow, M for what +0x335 means] Shipped: 4 files, `Reveal_Marker_02` and
  `INVISIBLE_REVEAL_OBJECT` handed to Rebel.
- **DESTROY_OBJECT.** P1 type name. Only the **first** instance in the mode's list is killed (no "all" option, no owner
  filter; none found: "Unable to find object %s to destroy"). The kill is the normal damage routine `Take_Damage`
  (0x3a9e30) called with kind 1 and an amount of **1,000,000.0** (constant 0x803524), so the object takes the normal death
  path (death effects, hardpoints, destruction notifications, and the bounty payout while §3.5 is on), not a silent
  removal. [H for the call, M for the full death path] Shipped: 3 files (`Urai_Fen`, `Tyber_Zann_Passenger`,
  `R_Ground_Light_Vehicle_Factory`).

### 4.2 DISABLE_REINFORCEMENTS (0x3a, handler 0x45eca0) [H write, M readers]

- **Parameters.** P1 (`atoi`): **0 = reinforcements enabled, non-zero = disabled** (the name is "disable"; shipped uses
  `1` to disable and `0` to re-enable). P2 optional faction name. Without P2: every player in the player list. With P2:
  the player of that faction; unknown faction or no player: "Unable to %s reinforcements for player of faction %s"
  (`enable` / `disable`) and nothing else. Logs "Reinforcements ENABLED / DISABLED for player id %d".
- **State.** Player byte **+0x39c := (P1 == 0)** (1 = enabled). Not saved by the reward itself (player state).
- **Readers** of player +0x39c (tests found): the tactical command-bar reinforcement button (0x312570, 0x317d00: component
  0xe7 / 0xe8 greyed or hidden when 0 and no scripted override), the drag-reinforcement flash (0x2e5580), the space / land
  reinforcement placement checks (0x2b0a20, 0x42f160, 0x7276a0, a function that picks the reinforcement point per slot).
  So with 0: the player cannot call reinforcements through the GUI; the AI reinforcement requests go through the same
  placement checks. [M: the exact refusals of 0x2b0a20, 0x42f160 and 0x7276a0 were not read]
- Shipped: 21 files, P1 `0|1`, optional P2 `EMPIRE`. (The task text's "UNDERWORLD 1" form is DISABLE_RETREAT's, not this
  reward's.)

### 4.3 BOMBARD_DELAY (0x7a) [H write, H reader from L1_S4_REMAINING §3.8]

- P1 required (`atof`, seconds), shipped `20`. Writes `mode +0x40 -> +0x50` (the bombard manager's "delay override") only
  when the current mode object has that manager (a land battle); otherwise nothing is written (no error). In
  `Start` (0x528140) a value above 0 replaces the faction's `Initial_Delay` (`int(FPS x seconds)`), and the extra time
  over the faction value delays the first salvo particle. Scope: **the current land battle**; the manager is rebuilt
  for each battle, so it does not persist [M: the manager's init was not re-read for +0x50]. It is a global override over
  every faction's `Initial_Delay`. Log: "Setting planetary bombard delay".
- [L] If the current mode is the galactic mode and its +0x40 field is non-null, the same write would hit that object's
  +0x50; shipped use is in a land story.

### 5.x TRIGGER_AI (0x1f, inline in 0x456970) [H unless tagged]

Not a no-op and not an AI/Lua notification: it **sets a named boolean flag in the target faction's AI**, which AI Lua
polls with `Check_Story_Flag`. Enum code 0x1f (name string 0x86c358, table entry beside MOVE_FLEET 0x1e).

- **Parameters** [H]: Param1 = flag name (required), Param2 = faction name (required), Param3 = planet type name
  (optional). Log first: "STORY REWARD - Triggering AI". Missing Param1 or Param2 (length 0): returns silently, nothing
  written. Param2 is looked up by faction name (0x331c40, faction list 0xb310b8), then the player of that faction in the
  player list (0x294d30); a miss logs "faction %s not found" / "cannot find player of faction %s" and stops. A player with
  no AI (player +0x360 null, e.g. a human) logs "player of faction %s has no AI, trigger failed" and stops. So a missing
  Param2 never defaults to the plot player.
  Many shipped events (INTERVENTION_*, SANDBOX_EVENT_CONTRIVED_ATTACK, SANDBOX_EVENT_TRAP_GALACTIC, ...) give only
  Param1, so they are **dropped by the missing-Param2 rule**: [M] the check on +0x80 is read in the decompile exactly as
  written; no second default path was seen. Events with Param2 (Rebel / Empire) do reach the AI.
- **Param3** [H]: planet found by type name through 0x407e00 + 0x2a87d0 (first live instance); if not found it logs
  "STORY REWARD TRIGGER AI- Unable to find planet %s" and the flag is still set with **no target** (key target 0).
- **Write** [H]: 0x4b0d20 finds the AI sub-object whose mode equals the current game mode (the player AI keeps one
  per mode, vector +0x38 / +0x40; none for this mode: nothing happens) and 0x4e0190 stores `true` in that mode manager's
  flag map (manager +0x78, an unordered map). Key = (target pointer or 0, flag name as given, case kept); value one byte.
  Setting an existing flag again is just `true` again; flags do not count.
- **Reader** [H]: Lua `Check_Story_Flag(player, flag_name, target_or_nil, clear)` (0x6a2f90, 4 arguments exactly):
  looks the same key up in the manager of the player's AI for the current mode (0x4b06d0, manager +0x38, lookup
  0x4dd530); returns the byte (false when absent). If the flag was true and `clear` is true it writes it back to false
  (0x4e0190 with 0). The target comes from the AITarget / game object argument, nil means target 0, so a flag set
  with Param3 is only seen by a check with that same planet, and one set without Param3 only by a check with nil.
  Shipped reader: `Wait_On_Flag` in `SCRIPTS/LIBRARY/PGINTERVENTIONS.LUA` (loops with Sleep(1), `clear` true), used by
  the intervention goal scripts.
- **Default / reset** [M]: flags start absent (false). 0x4df960 (called from 0x4e3e30 and 0x6b9580, mode / AI manager
  init) clears the map and refills it from a saved vector at manager +0xb8; treated as init and load, not per-tick.
- **Not read**: `Story_GCMenu.xml` is a Thrawn's Revenge file and is not in `extracted/*.zip`; its TRIGGER_AI use (per
  `Q6_T24_TR_GCMENU.md` §2) follows the same handler. With no AI player, or none for the menu mode, the reward only logs.
  No story-side effect: it neither fires a STORY_AI_NOTIFICATION nor touches the story manager.

## 5. Questions still open

1. DIRECT_INVASION reader (§1.7): none found.
2. ~~What consumes the pending campaign (§2.1)~~ answered 2026-11-17 (modc t45): controller update 0x44a3f0. Still
   open: whether a loading screen is drawn during the switch, and the local-player assignment from the campaign.
3. SET_SANDBOX_OBJECTIVES body (§2.7).
4. SET_PLANET_SPAWN: reader of the planet spawn map (§1.8); shipped use is a no-op.
5. Power rewards: the unit-side state values of 0x37df00 and the reader of player +0x378 (§1.5).
6. Exact refusals of the three GUI readers of player +0x39c and of the planet-restricted caller 0x3bd20 (§1.1, §4.2).
7. Whether the conquest dispatch inside 0x3fb040 always fires for PLANET_FACTION (§1.2).
8. Whether the manager flags are reset on a plain save-load (§3.8: reset call sites 0x3d3200 / 0x3027d0 are mode and GUI
   init, load-time order not checked).
