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

### 0.1 Reward_Type names (mcx0 t05, 2026-11-17) [H]

The reward records of the enum table at 0x00a29140 (§0), read from the table's data in the Steam x64 exe: 140 names,
codes 0x01..0x8e. The codes 0x3b and 0x80 have no name, and 0x63 ENABLE_OVERWHELMING_ODDS has a name but no case in the
dispatcher, so it is a no-op. The first record of the table, STORY_CORRUPTION_INCREASED with value 0x36, is not a
reward (an event name; the event table of STORY.md §0.1 is a different one and gives it 0x3d) [M]. The rest of the 221 records are the other story enums (filters, GROUND / SPACE / EITHER, statistics,
flash targets, tutorial, comparisons, battle composition, classes, regions, destroy, retreat, slots, corruption types).
The port's story ledger (`engine/apps/sim/src/story_rewards.cpp`, `retail_story_rewards()`) holds the same list; a name
outside it is ignored by retail's dispatcher (code 0, no case), so the load report counts it as retail-equivalent.

| Code, name | Code, name | Code, name | Code, name |
|---|---|---|---|
| 0x01 BUILDABLE_UNIT | 0x24 FLASH_TERRAIN | 0x48 SET_PLANET_SPAWN | 0x6b START_CINEMATIC_MODE |
| 0x02 UNIQUE_UNIT | 0x25 SET_HEALTH | 0x49 COMMANDBAR_MOVIE | 0x6c STOP_CINEMATIC_MODE |
| 0x03 REMOVE_UNIT | 0x26 NEW_POWER_FOR_ALL | 0x4a STOP_COMMANDBAR_MOVIE | 0x6d SHOW_COMMAND_BAR |
| 0x04 CREDITS | 0x27 REMOVE_POWER_FROM_ALL | 0x4b SET_WEATHER | 0x6e HIDE_AUTORESOLVE |
| 0x05 SPAWN_HERO | 0x28 SET_TACTICAL_MAP | 0x4c PLANET_FACTION | 0x6f DISABLE_BUILDABLE |
| 0x06 INFORMATION | 0x29 DISABLE_AUTORESOLVE | 0x4d LOCK_UNIT | 0x70 ENABLE_BUILDABLE |
| 0x07 STORY_ELEMENT | 0x2a ENABLE_AUTORESOLVE | 0x4e FORCE_RETREAT | 0x71 HIDE_CURSOR_ON_CLICK |
| 0x08 STATISTIC_CHANGE | 0x2b DISABLE_MOVIES | 0x4f SET_PLANET_RESTRICTED | 0x72 LOCK_PLANET_SELECTION |
| 0x09 SFX | 0x2c REMOVE_STORY_GOAL | 0x50 MULTIMEDIA | 0x73 SHOW_SMUGGLE_SLOT |
| 0x0a SPEECH | 0x2d CHANGE_OWNER | 0x51 SET_PLANET_VISIBILITY_LEVEL | 0x74 HIDE_SMUGGLE_SLOT |
| 0x0b FLASH_GUI | 0x2e DESTROY_OBJECT | 0x52 PAUSE_GALACTIC_GAME | 0x75 SHOW_RAID_SLOT |
| 0x0c FLASH_PLANET_GUI | 0x2f VICTORY | 0x53 ADD_OBJECTIVE | 0x76 HIDE_RAID_SLOT |
| 0x0d FLASH_PLANET | 0x30 SWITCH_CONTROL | 0x54 REMOVE_OBJECTIVE | 0x77 SHOW_STEAL_SLOT |
| 0x0e HIDE_TUTORIAL_CURSOR | 0x31 DISABLE_RETREAT | 0x55 OBJECTIVE_COMPLETE | 0x78 HIDE_STEAL_SLOT |
| 0x0f SCREEN_TEXT | 0x32 FLASH_UNIT | 0x56 REMOVE_ALL_OBJECTIVES | 0x79 FINISHED_TUTORIAL |
| 0x10 DISABLE_EVENT | 0x33 REVEAL_PLANET | 0x57 OBJECTIVE_FAILED | 0x7a BOMBARD_DELAY |
| 0x11 ENABLE_EVENT | 0x34 DUAL_FLASH | 0x58 ENABLE_DIRECT_INVASION | 0x7b ENABLE_BOUNTY_COLLECTION |
| 0x12 PICK_PLANET | 0x35 SET_FLAG | 0x59 DISABLE_DIRECT_INVASION | 0x7c DISABLE_BOUNTY_COLLECTION |
| 0x13 SWITCH_SIDES | 0x36 INCREMENT_FLAG | 0x5a ENABLE_OBJECTIVE_DISPLAY | 0x7d REMOVE_CORRUPTION |
| 0x14 ZOOM_IN | 0x37 HIGHLIGHT_OBJECT | 0x5b FLASH_FLEET_WITH_UNIT | 0x7e GIVE_BLACK_MARKET |
| 0x15 ZOOM_OUT | 0x38 STORY_GOAL_COMPLETED | 0x5c SELECT_PLANET | 0x7f SHOW_SPECIAL_SLOT |
| 0x16 PAUSE_GAME | 0x39 REVEAL_ALL_PLANETS | 0x5d FORCE_CLICK_GUI | 0x81 SABOTAGE_STRUCTURE |
| 0x17 TUTORIAL_DIALOG | 0x3a DISABLE_REINFORCEMENTS | 0x5e FLASH_PRODUCTION_CHOICE | 0x82 ENABLE_SABOTAGE |
| 0x18 POSITION_CAMERA | 0x3c RESET_BRANCH | 0x5f SET_MAX_TECH_LEVEL | 0x83 FLASH_ADVANCED_MAP_OBJECT |
| 0x19 LOCK_CONTROLS | 0x3d RESET_EVENT | 0x60 TUTORIAL_PLAYER | 0x84 ENABLE_INVASION |
| 0x1a SCROLL_CAMERA | 0x3e LINK_TACTICAL | 0x61 PAUSE_GALACTIC | 0x85 RESTRICT_ALL_ABILITIES |
| 0x1b FLASH_OBJECT | 0x3f RANDOM_STORY | 0x62 UNPAUSE_GALACTIC | 0x86 RESTRICT_CORRUPTION |
| 0x1c ENABLE_FOW | 0x40 DISABLE_SPECIAL_STRUCTURE | 0x63 ENABLE_OVERWHELMING_ODDS | 0x87 RESTRICT_BLACK_MARKET |
| 0x1d ENABLE_VICTORY | 0x41 SET_TECH_LEVEL | 0x64 FLASH_SPECIAL_ABILITY | 0x88 RESTRICT_SABOTAGE |
| 0x1e MOVE_FLEET | 0x42 SET_SPAWN | 0x65 SET_SANDBOX_OBJECTIVES | 0x89 ENABLE_FLEET_COMBINE |
| 0x1f TRIGGER_AI | 0x43 TRIGGER_EVENT | 0x66 FORCE_RESPAWN | 0x8a ENABLE_COMBAT_CINEMATIC |
| 0x20 DISABLE_STORY_EVENT | 0x44 ENABLE_GALACTIC_REVEAL | 0x67 SCROLL_LOCK | 0x8b RESTRICT_AUTORESOLVE |
| 0x21 DISABLE_BRANCH | 0x45 ACTIVATE_RETRY_DIALOG | 0x68 SKIRMISH_RULES | 0x8c ENABLE_GALACTIC_CORRUPTION_HOLOGRAM |
| 0x22 INVADE_PLANET | 0x46 USE_RETRY_DIALOG | 0x69 RESET_GALACTIC_FILTERS | 0x8d UPDATE_OBJECTIVE |
| 0x23 LOAD_CAMPAIGN | 0x47 START_MOVIE | 0x6a SET_ADVISOR | 0x8e ENABLE_CAMPAIGN_VICTORY_MOVIE |

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

### 1.8 SET_PLANET_SPAWN (0x48, handler inline) and SET_SPAWN (0x42) [H]

- **SET_PLANET_SPAWN.** Needs P1, P2 and P3 all present, else "Set planet spawn failed due to incomplete parameters".
  - P1 planet name (first instance), P2 spawner type name, P3 chance (`atof`).
  - It writes P3 into a map in the planet block (+0x2d0) through 0x4b6e00. The key is the CRC of P2 **as written**,
    not upper-cased. An existing key is overwritten; nothing removes an entry.
- **Reader (found 2026-11-17, mcx6)** [H]. 0x4b71c0 walks that map. Its one caller is the tactical battle start
  0x28fda0, for the battle's planet, after the map and its objects are loaded. For each entry it runs 0x5310b0(key,
  chance), the indigenous-spawner activation (§1.13; TRU1_C_RE.md §1.2).
  - So the entry sets the share of active spawners of that type in **every later battle at that planet**, in place of
    the type's `Spawn_Indigenous_Units_Chance`. The spawners are still undecided at that point, so their own first
    service no longer re-decides [M for the timing].
  - P2 must be the upper-case type name to match (§1.13).
  - Whether the map is saved with the planet was not traced [L].
- **Shipped use is a no-op.** `INTERVENTION_INDIGENOUS_REVOLT.XML` (both games, two events) sets **only
  Reward_Param3** (`1.0`, `0.0`): P1 and P2 are empty, so the handler logs the "incomplete parameters" error and does
  nothing. The question's "in no shipped XML" is wrong in this one respect.
- **SET_SPAWN (0x42)** is a different reward: it re-balances the spawners of a type in the running battle at once;
  see §1.13.
- **Implemented (mcx6):** the entry lives in `galactic::StoryGates::planet_spawn` (saved and hashed by the story
  session); `TacticalBattle::sp_planet_spawn` runs it at the battle's first spawner scan. [port] The map order is
  modelled as ascending key CRC per planet; an unknown planet logs "non-existant planet" (retail's message not read).

### 1.9 Other galactic rewards touched while reading (for completeness) [H]

- **CHANGE_OWNER (0x2d)** is in §4.1 (it is the same code in both modes).
- REMOVE_CORRUPTION (0x7d) is in §3.

### mcx6 Part A (2026-11-17): §1.10 to §1.21

Answers `MCX6_RE_QUESTIONS.md` Part A. Read through REA and the Ghidra export (`re_reference/`), not ReVa. None of
these rewards appears in shipped EaW / FoC XML or Lua. A search of the loose XML and Lua of the eight workshop mods
on this machine (Thrawn's Revenge 1125571106, Fall of the Republic 1976399102 and six others) found only
`Template_Switch_Sides` (§1.10). It found no FORCE_RETREAT, although MOD_AUDIT counts two; MEG-packed files were not
searched. Each section ends with **Port**, the existing port system the reward touches.

### 1.10 SWITCH_SIDES (0x13, inline in 0x456970; step 0x297e80 `PlayerListClass::Switch_Sides`) [H unless tagged]

- **Multiplayer: nothing.** The case does nothing when the game type is a multiplayer one (0x28af60: GMM +0x18 in
  {1, 2, 4}), and 0x297e80 repeats that test.
- **Parameters.** P1 optional: faction name. Nothing else is read.
- **One step (0x297e80).**
  - The player list (0xa16fd0) keeps the local player's index at list +0x30. A step adds 1, wraps to 0 at the end,
    and keeps adding while the player at that index has a faction with `Is_Debug_Switchable_To` (faction +0x10a)
    false.
  - Switchable in shipped data: FoC Rebel, Empire, Underworld; EaW Rebel, Empire, Pirates, Ewok. Not switchable:
    Neutral, Hostile, Hutts, Sarlacc and FoC Pirates.
  - If no player in the list is switchable, the loop never ends [H loop; M that the game then hangs].
  - Then the human flags, under three conditions: the debug byte 0xb1574e is clear, the new index is valid, and the
    new local player has **no AI system** (player +0x360 null, 0x2814d0; i.e. the campaign's `AI_Player_Control` gave
    it `Human`). If all hold: new local player +0x62 (human) := 1, and every other player +0x62 := 0.
  - If the new local player has an AI, the +0x62 flags are **not touched**. The local index moves (the HUD then acts
    for that player [M]), its AI keeps playing it, and the old human player keeps +0x62 = 1 with no AI.
  - Log "PlayerListClass::Switch_Sides: Switched to player index %d, faction %s".
  - Last, for every LOBBING_SUPERWEAPON (behaviour 0x37) object of the current mode it calls a method that is empty
    in this build (folded with the CFG stub 0x1fa90): no effect.
- **P1 absent:** one step, to the next switchable player.
- **P1 given:**
  - Note the local player's faction F0, then step until the new local player's faction is F0 again (full circle) or
    is P1's faction (looked up by name each round, 0x3303d0).
  - An unknown P1, or no switchable player of that faction: it goes full circle and ends on a player of F0, normally
    the original one.
  - The human flags end as the **last step that reached an AI-less player** left them.
  - If F0 is not switchable and P1 cannot be reached, the loop never ends [M].
- **After the steps (both cases).** For every mode on the game-mode stack (GMM +0x48, count +0x50), the per-object
  visibility refresh runs for the new local player over that mode's object manager: 0x2b59b0 calls 0x3a51e0 per
  object (the "visible to the local player" byte +0x39f, then each behaviour's vf+0xa8, e.g. the planet presence
  fold). It is the same refresh that runs after a battle (BATTLE_TRANSITIONS §3).
- **Not touched** [H, by absence in 0x456970 and 0x297e80]:
  - camera and GUI: no notification is sent; the HUD picks up the new player the next time it asks for the local
    player (0x294a40) [M];
  - AI systems: none is created, removed or re-seeded (+0x360 untouched);
  - story plots (plot player +0x638), credits, ownership.
  - No random draw.
- **Save.** +0x62 is player state. Whether the local index (list +0x30) is saved was not traced [L].
- **Mod use.** TR and FotR define `Template_Switch_Sides` in `Player_Agnostic_Plot.xml` and the tactical universal
  story files. `StoryUtil.SwitchHumanFaction(old, new)` fills its P1 with the new faction name, but no Lua file in
  either mod calls that function, so the reward is defined but never fired.
- **Port:** the local-player choice in apps/game (HUD, input, fog view) and the players' human flag. Skip it in
  multiplayer. It needs faction `Is_Debug_Switchable_To` (in the objects field schema). It makes no change to the sim
  AI. Re-run the fog / visibility view for the new local player.
- **Implemented (mcx6):** `GalacticSim::story_switch_sides` (local player and human flags, faction
  `Is_Debug_Switchable_To` loaded as `FactionRules::debug_switchable`). [port] The port is single player, so the
  multiplayer test is not made; a stepping loop that would never end in retail stops after one round of the player list.
  The HUD and fog view follow `GalacticSim::local_player()` (game host).

### 1.11 FORCE_RETREAT (0x4e, inline in 0x456970; request 0x340920) [H unless tagged]

- **Parameters.** P1 faction name, required (otherwise "Force retreat failed due to incomplete parameters").
  - The faction is looked up by name (0x3303d0); unknown: "invalid faction %s".
  - The player of that faction comes from 0x294d30; none: "no player of faction %s".
- **Tactical only.**
  - The reward needs the current mode's retreat coordinator (mode +0x30). Coordinators are created only by the
    tactical mode init 0x35e560 and the tactical load 0x35f970 (space 0x532b70, land 0x534bd0).
  - In galactic mode it therefore logs "no retreat coordinator" and does nothing [H creators; M that galactic +0x30
    stays null].
- **A retreat already under way** (coordinator +0x1c ≠ −1, 0x3400b0): silent return.
- **The request.** Otherwise 0x340920(coordinator, player id (player +0x4c), local-request flag 0). The reward then
  logs "Force retreat of player %s" whatever the outcome: the return code is ignored.
- **What 0x340920 refuses, with flag 0** (same checks as the HUD button, P4_SIM_SYSTEMS "Retreat", BATTLE_TRANSITIONS
  §3.4):
  - the player **is the local player** (code 1). So FORCE_RETREAT never retreats the human;
  - player id −1;
  - the coordinator's request check vf+0x48: not attacker or defender, or a game type that forbids it (1); retreat
    not yet allowed (2); another player retreating (5); faction / ability blocks such as
    `Disallows_Hyperspace_Retreat` and `RetreatPreventionAbility` (0x282750);
  - coordinator +0x28 > 0, the "retreat allowed" countdown (30 s from battle start in vanilla) still running (2);
  - a retreat already flagged (+0x7e, +0x1c or +0x18) (5).

  A refused request has no effect and is not retried.
- **Accepted.** A RetreatEvent (command 22; 0x532570, filled by 0x532620(event, player, 0)) is queued on the event
  queue 0xb2ed18. The HUD Retreat button and the AI's `Withdraw_Units` (AI_TACTICAL, RetreatPlan) queue the same
  event. From there the **normal retreat** runs: countdown, the retreatable list of that player's units (vf8),
  jump-out, destruction of what did not leave, victory type 0xc. It is not a direct move and not a galactic retreat.
  The units are all the units of that player the coordinator accepts, not a chosen subset.
- **Save / random.** No state of its own. No draw in the reward (the retreat run has its own, BATTLE_TRANSITIONS).
- **Port:** the tactical retreat request in apps/sim (the path the AI's Withdraw_Units uses), with "never the local
  player" semantics and every check above. In galactic mode, log only.
- **Implemented (mcx6):** the session checks the parameters and logs; with a battle attached the battle host calls
  `TacticalBattle::reward_force_retreat` (no request while a retreat runs, the local player refused, then
  `request_retreat`), and logs "Force retreat of player %s"; in the galactic mode "no retreat coordinator".

### 1.12 SET_HEALTH (0x25, inline in 0x456970; setter 0x3a8ad0) [H unless tagged]

- **Parameters.**
  - P1 type name and P2 percent, both required; a missing one is a silent return.
  - P1 is copied into a 192-byte buffer and upper-cased. P2 is `atoi`, an integer percent.
  - P3 optional count (`atoi`): absent = 1 (the first instance), 0 = all, n = the first n; a negative n acts as 1.
- **Log and refusals.** "Set health of %s, to %d percent". Unknown type: "Can't find type %s". No live instance:
  nothing.
- **Objects.** Instances of the type in the current mode's object manager, list order (0x2a87d0), any owner, any mode.
- **Per object, 0x3a8ad0(obj, P2 / 100).**
  - Health (obj +0x5c) := max × fraction, clamped to [0, max]. So P2 is a **percentage of the current maximum**:
    above 100 gives max, 0 or less gives 0.
  - max = 0x3727a0 = `Tactical_Health` (type +0xdcc), × `Object_Max_Health_Multiplier_Land` / `_Space` (GameConstants
    +0x15cc / +0x15c8) in a land / space mode, × the owning AI player's difficulty health factor (AI_TACTICAL), × the
    health-modifier abilities.
  - When the value changed and obj +0x50 equals the global 0xa286f0, it sets obj +0x3a0 bit 0x01 [L meaning].
  - Last, 0x396df0 re-clamps (it returns the hull fraction).
- **Not done.**
  - No damage, no death path, no events.
  - At 0 % a live object is left at 0 health; whether anything later kills it was not traced [L].
  - For a team type the instances are the team containers; writing a container's +0x5c leaves its members unchanged
    (team health is the mean of the members, 0x405230) [M].
- **Save.** Health is object state, saved with the object.
- **Port:** the object health setter of objects / tactical, with max health from the port's max-health function
  (Tactical_Health, mode multipliers, difficulty, abilities).
- **Implemented (mcx6):** battle: `TacticalBattle::reward_set_health` on the combat health (CombatSystem max health);
  galactic: the galactic store health. [port] In the galactic mode the maximum is the type's `Tactical_Health` without
  difficulty or ability factors (nothing reads a galactic object's health); obj +0x3a0 bit 0x01 [L] is not kept; a team
  container's own field is not modelled (its members keep their health, as in retail).

### 1.13 SET_SPAWN (0x42, inline in 0x456970; 0x531070 → 0x5310b0) [H unless tagged]

- **Parameters.** P1 spawner type name and P2 chance (`atof`), both required (otherwise "Set spawn failed due to
  incomplete parameters").
- **The key.** 0x531070 takes the CRC-32 (0x215a30) of P1 **as written**: length from the parameter's length field,
  no upper-casing. It calls 0x5310b0(key, chance), the indigenous-spawner activation of TRU1_C_RE.md §1.2 (each
  spawner's first service runs the same function with its type's `Spawn_Indigenous_Units_Chance`).
- **Which spawners (0x5310b0).**
  - It walks the global spawner list 0x140a43690: every SPAWN_INDIGENOUS_UNITS object attached in the current battle,
    in attach order.
  - It takes those whose type +0x11c equals the key. Type +0x11c is the CRC of the **upper-cased** type name, set at
    type load (0x3782a0). So P1 must be the type name in upper case, or nothing matches [H for both CRCs; M that a
    mixed-case P1 never matches].
  - Undecided spawners (state −1) become 0 (off) and join the off pool; state 1 forms the on pool. N = on + off.
    N = 0: return, nothing.
- **Chance c.**
  - P2 < 0: c = that type's `Spawn_Indigenous_Units_Chance` (+0xc1c), taken from the first off spawner, else the
    first on one.
  - Then c is clamped below at 0. Above 1 it is a percentage: c × 0.01, clamped to [0, 1].
- **Re-balance.** Target T = int(N × c).
  - T > on: random off spawners are switched on until on = T.
  - T < on: random on spawners are switched off.
  - Each pick is one synced draw `Get_Int(0, pool − 1)` (0x1ffb40 on the synced LCG 0x140a13e24). The pool is in list
    order and the picked spawner is removed from it.
- **Effect.**
  - Only state-1 spawners spawn (service 0x531760 step 2). Switched off: no new spawns, its units already spawned
    stay. Switched on: it spawns at its next due service.
  - A spawner attached later (state −1) re-balances its type with the type's own chance at its first service.
- **Mode.** It acts on the running battle. In galactic mode the list is empty and nothing happens [M].
- **Save.** The state sits in the spawner's data pack (obj +0x120, +0x38); whether the pack is saved was not traced
  [L].
- **Port:** the TRU1 t05 activation in apps/sim `tactical_battle_spawners.cpp`. Expose the per-type re-balance with an
  explicit chance, match on the upper-case name CRC, and use synced draws.
- **Implemented (mcx6):** `TacticalBattle::reward_set_spawn` / `sp_rebalance` (the TRU1 activation with an explicit
  chance, synced draws; the reward counts every spawner of the type, N = on + off, so the ones already off can come on
  again, while the first-service activation keeps TRU1's pools). [port] The key is compared as text with the upper-cased
  type name (the same test as the CRC compare, without the CRC); in the galactic mode only the incomplete-parameters log.

### 1.14 SET_WEATHER (0x4b, inline in 0x456970) [H]

- **A no-op.** The case only tests P1.
  - P1 given: the jump table entry (0x14045885e) goes straight to the dispatcher's common exit.
  - P1 empty: logs "Set weather failed due to incomplete parameters".
- Nothing is stored, so the battle-environment pick (CU8G_T02_ENV_PICK_RE.md: the map's environment record and the
  weather bind 0x46a2f0) has nothing to read.
- **Port:** a retail no-op (keep the error log for an empty P1).
- **Implemented (mcx6):** as a no-op with the log.

### 1.15 RANDOM_STORY (0x3f, inline in 0x456970; builder 0x4bfdc0 on the generator object 0x140a2aba0) [H structure, M content]

- **Parameters.** None: it reads no reward parameter. Logs "Generating random story". Needs a current mode.
- **Candidate pools.** Rebuilt on each call from the current mode's SELECTABLE (behaviour 2) objects (0x2a9ff0):
  - own heroes: objects of the **local** player (not the plot player) whose type is `Is_Named_Hero` (+0x13);
  - own planets: planet objects of the local player;
  - enemy heroes: named heroes of other owners;
  - other planets: planets of other owners that are revealed (planet block +0x2c9).
- **Plot.** The generator's counter (+0) goes up by 1, giving k, and a new plot `Random_Plot_<k>` is built (0x52d400).
- **Random stream.** Trigger count n = Get_Int(1, `Random_Story_Max_Triggers`) (GameConstants +0x618, vanilla 3).
  **Every draw of the generator uses the free (unsynced) RNG 0x140a13e20** (0x1ffb40 on it, in 0x4bfdc0, 0x4bec90,
  0x4be420, 0x4bdf60 and 0x4c0620) [H].
- **Trigger events** `RANDOM_TRIGGER_<i>`, i = 0..n−1 (0x4bec90). The type is a random entry of
  `Random_Story_Triggers` (GameConstants +0x600); vanilla has 12: ENTER, LAND_ON, CONQUER, CONSTRUCT, CONSTRUCT_LEVEL,
  DESTROY, DESTROY_BASE, DEPLOY, MOVE, ACCUMULATE, DEFEAT_HERO, WIN_BATTLES. Random parameters by type [M, from the
  switch]:
  - ENTER / LAND_ON / CONQUER: a revealed planet of another owner;
  - CONSTRUCT: a name from the side's `Random_Story_<side>_Construction`;
  - CONSTRUCT_LEVEL: an own planet, ground or space, and a level above the current one;
  - DESTROY: a name from `Random_Story_<side>_Destroy` and a count 1..3;
  - DESTROY_BASE: ground or space;
  - DEPLOY / MOVE: an own hero and a revealed other planet;
  - ACCUMULATE: a number 10..30 [L unit];
  - DEFEAT_HERO: an enemy hero;
  - WIN_BATTLES: a count 1..3, ground or space;
  - any other type: "Random story trigger %s currently unsupported!".
- **Around each trigger** [M]:
  - each trigger after the first has the previous one as Prereq (0x4bdfa0);
  - each gets a story-dialog chapter (0x4beb00: `[CHAPTER i]`, TITLE, IMAGE, `DIALOG <name><1..3>`, TEXT);
  - each gets a STORY_TRIGGER follow-up event whose reward is REMOVE_STORY_GOAL (0x4be210).
- **Final event** `RANDOM_TRIGGER_<n>` (0x4be420) [M]: a STORY_TRIGGER whose reward is a random entry of
  `Random_Story_Rewards` (vanilla BUILDABLE_UNIT, UNIQUE_UNIT, CREDITS), with the side's
  `Random_Story_Reward_<side>_Buildable` / `_Unit` name and a random own planet for a unit. An unknown name logs
  "Random story reward %s is unknown!".
- **Registration and offer.**
  - Plot player := the local player. The events are put into their type buckets (0x530180), and the plot is added to
    the current mode's story manager's plot map (mode +0x100, keyed by the CRC of its name) [M: that mode +0x100 is
    the manager's plot map].
  - The generated text is logged and, through 0x47d9c0 and 0x47e030, shown as the IDD_STORY_DIALOG offer
    (GALACTIC_DIALOGS §2.4).
  - Cancel (0x105270 → 0x4c0620) removes `Random_Plot_<k−1>` from the mode's story manager (0x32ab70) and its dialog.
    Accept and Close only close the dialog (0x4bdf60 formats a string and discards it).
  - Whether the new plot starts active was not traced [L].
- **Port:** a plot built from code (not XML) in the story session, plus the story-dialog offer. Draw from a non-sim
  RNG (retail's is unsynced). Low priority: no data uses it.
- **Implemented (mcx6):** `build_random_story` (apps/sim `story_random_story.cpp`), the plot added to the local player's
  plot set; the plot and the counter are saved (story save v6) and replayed on load.
  [port] Retail draws its unsynced RNG; the port's plots are hashed, saved sim state, so they draw a stream of the story
  session's own (seeded from the synced RNG's state at the first RANDOM_STORY, no synced draw; saved, story save v8, and
  hashed), and a run after a load builds the same plots.
  [port] The story-dialog offer is not shown (no Cancel: the plot stays, as after Accept); retail's buildable "used"
  marks are not kept; a draw from an empty pool rebuilds the trigger and a trigger that cannot be built after 64
  rebuilds is left without parameters; the follow-up REMOVE_STORY_GOAL event names the trigger as its tag [L]; a plot
  added while a battle is attached goes to the tactical plot set (a tactical edit: saved, replayed when a load rebuilds
  the battle, dropped at the battle's end). DEPLOY and MOVE draw one revealed planet of another owner (no owner test).
  The side lists are the Rebel ones for the `Good_Side_Name` faction, else the Empire ones [M].

### 1.16 PICK_PLANET (0x12, handler 0x460b30) [H unless tagged]

- **Parameters.** P1 FILTER name and P2 placeholder name, both required (silent return).
  - The filter is the CRC of P1 (as written) looked up in the story enum map 0xb30728; unknown gives 0 (FILTER_NONE).
    Values as in §1.6.
- **Which planet.** It walks the current mode's planets (PLANET behaviour 3, 0x2a9ff0, list order) and takes the
  **first** planet that passes the filter against the **local player** (not the plot player). Friendly is 0x39a230,
  enemy is 0x39a450. Not random.

  | Filter | Planet passes when |
  |---|---|
  | NONE | always |
  | FRIENDLY_ONLY | friendly |
  | NEUTRAL_ONLY | neither friendly nor enemy |
  | ENEMY_ONLY | enemy |
  | FRIENDLY_AND_NEUTRAL | not enemy |
  | ENEMY_AND_NEUTRAL | not friendly |
  | FRIENDLY_AND_ENEMY | friendly or enemy |

  No planet passes: nothing, no log.
- **Replacement.** P2 is copied and upper-cased; log "Replacing all instances of %s with planet %s". Then 0x32ae50 on
  the current mode's story manager walks every plot (every player's, active or not) and calls each event's vf+0x30:
  - Base 0x45db20: for every event whose own reward is not PICK_PLANET, each of Reward_Param1..3 whose upper-cased text
    equals the placeholder becomes the planet's type name (type +0xf8). Log "EVENT %s, Param %d, replacing %s with
    %s".
  - Event classes with a planet-name parameter list (overrides 0x45def0 on list +0x360; 0x45e230 and 0x45e360 on list
    +0x368) first replace matching entries of that list ("EVENT %s, replacing %s with %s"), then call the base. [H for
    the three overrides; M which event classes own them and how their compare treats case]
- **Save.** The event save (vf9 0x462fd0) writes the reward parameter strings, so the substitution survives a save
  [H for reward parameters; M for the lists].
- **Port:** a parameter substitution in the story session across all plots, using the local player's friend / enemy
  relations. It takes the first planet in object order, so the port's planet order must match retail's.
- **Implemented (mcx6):** `StorySession::pick_planet` + `lua::StoryModel::replace_placeholder`, kept as a definition
  edit (story save v6) and replayed on load, with the "EVENT %s, replacing %s with %s" / "EVENT %s, Param %d,
  replacing %s with %s" lines. [port] Planets are walked in row order (stands in for the mode's object order); every
  entry of an event parameter's name list (whitespace and comma separators, kept) that equals the placeholder is
  replaced, in every event class [M]; a substitution while a battle is attached changes the tactical plot set (a
  tactical edit: replayed when a load rebuilds the battle). A load replaces the session's edits with the save's.

### 1.17 STATISTIC_CHANGE (0x08, inline in 0x456970) [H write, M readers]

- **Parameters.**
  - P1 statistic name and P2 value (`atoi`), both required (silent return).
  - P3 optional: planet type name (first instance).
  - Without P3, or when P3 gives no instance, the planet is the dispatcher's second argument: the object the firing
    event matched, which Fire passes through (e.g. the planet of an Enter-family event) [M]. None:
    "Reward Statistic Change - No planet specified".
  - There is no check that the object is a planet; the write goes through obj +0xb8 [M: a crash for a non-planet].
- **Statistic enum** (table 0x00a29140): CREDIT_VALUE 1, POLITICAL_CONTROL 2, MAX_SPECIAL_STRUCTS 3,
  MAX_STARBASE_LEVEL 4, MAX_GROUND_BASE_LEVEL 5; unknown 0.
- **Writes** (planet data block, obj +0xb8; values not clamped):

  | Statistic | Write | Reader |
  |---|---|---|
  | 1 CREDIT_VALUE | +0x228 := P2 (save id 33); and the planet owner's player byte +0x181 := 1 | +0x228: Planet_Income's base (GALACTIC_ECONOMY §1.4). +0x181: no byte reader found by a whole-image search; the presence signal 0xE handler 0x3fcd60 also sets it [L meaning] |
  | 2 POLITICAL_CONTROL | +0x22c := P2 (save id 34) | 0x3734d0, instead of the type's `Political_Control` (+0x79c) |
  | 3 MAX_SPECIAL_STRUCTS | nothing | — |
  | 4 MAX_STARBASE_LEVEL | +0x238 := P2 (save id 37) | 0x372780, the AI token MaxStarbaseLevel (AI_GALACTIC_TOKENS) |
  | 5 MAX_GROUND_BASE_LEVEL | nothing: it calls an empty function (folded with 0x1fa90) | — |

- **Port:** the galactic planet's runtime fields (income base, political control, max starbase level) in the galactic
  planet state.
- **Implemented (mcx6):** CREDIT_VALUE writes `PlanetState::base_value`; POLITICAL_CONTROL and MAX_STARBASE_LEVEL are
  kept in `galactic::StoryGates` (`political_control`, `max_starbase`) and read by `target_political_control` and
  `GalacticSim::max_starbase_level` (the AI's MaxStarbaseLevel token and build check). The planet without P3 is the
  firing notification's planet (`lua::StoryModel::firing_notification`). [port] The owner's player byte +0x181 [L] is not
  kept; the planet statistics live in the story gates, not the planet save.

### 1.18 INFORMATION (0x06) [H]

- The jump table sends 0x06 to the dispatcher's common exit (as for code 0): nothing happens and nothing is logged.
- **Port:** a retail no-op.
- **Implemented (mcx6):** ledger owner Retail (a no-op).

### 1.19 ENABLE_OVERWHELMING_ODDS (0x63) [H]

- No case. The jump table sends 0x63 to the error path, as for 0x3b and 0x80: "STORY REWARD - ERROR! Reward for event
  %s needs some code support!".
- No field is written, so there is no reader. The only string in the exe is the enum name.
- **Port:** the existing Retail no-op entry matches.

### 1.20 DISABLE_SPECIAL_STRUCTURE (0x40, inline in 0x456970) [H unless tagged]

- **Parameters.** P1 type name and P2, both required (silent return). P2 is `atoi`.
- **Object.** The first live instance of P1 in the current mode (0x2a87d0). Unknown type or no instance: "Invalid
  structure %s in DISABLE_SPECIAL_STRUCTURE".
- **Write.** The object must have the BASE_SHIELD behaviour (0x39: slot byte +0x36b ≠ −1, fetched through vf+0x10);
  otherwise a silent return. Then the owner object's byte +0x3a1 bit 0x04 := (P2 ≠ 0). The owner is the same object.
- **Reader.** Only the base-shield service `Evaluate_State` 0x56bb00 (TRU1_A_RE_ANSWERS.md, its step 5).
  - Bit set: the shield is off from its next service (every 15 frames), with the offline HUD SFX and the dome hidden.
  - Bit cleared: the shield comes back after `Base_Shield_Delay_Time`.
  - A whole-image byte search finds one test of bit 0x04 at +0x3a1 (in 0x56bb00) and one write (this reward).
  - Despite the name, only base shield generators react.
- **Save.** +0x3a1 is in the object save chunk (0x3a5ad0 / 0x39ce40, HERO_CLASH.md) [M for this bit]. The object
  constructor clears it.
- **Port:** the tactical base-shield evaluator in apps/sim, plus an object flag.
- **Implemented (mcx6):** the battle host's `TacticalBattle::reward_disable_special_structure` sets the combat flag
  `kUnitShieldStoryOff`, which `CombatSystem::evaluate_base_shield` reads. [port] In the galactic mode only the
  "Invalid structure" log (no base-shield service runs there, the bit is not stored).

### 1.21 Readers of RESTRICT_BLACK_MARKET, RESTRICT_ALL_ABILITIES, ENABLE_SABOTAGE, ENABLE_COMBAT_CINEMATIC [H unless tagged]

All four readers act on the human player's galactic GUI or camera. None is read by the AI: the AI's sabotage path
(P4_SIM_SYSTEMS §2.4, 0x5b32c0) reads neither the sabotage flag nor the lists.

- **RESTRICT_BLACK_MARKET (0x87, setter 0x44ac60, list +0x2d0).**
  - The jump table sends 0x86 to RESTRICT_CORRUPTION (0x44ad20) and 0x87 to this setter; Ghidra prints the two as one
    case.
  - Only reader: 0x4470e0, called only by 0x2e5740, the "can this unit use a black-market ability here" test. It is
    false at once when the planet is in the list. Otherwise it is true when the unit holds an ability named in
    GameConstants `Activated_Black_Market_Ability_Names` (+0xff0), on itself or on the members of a fleet or
    transport it is [M: the member walk].
  - 0x2e5740's only caller is the galactic planet-ability slot builder 0x3d2020. For the selected unit with the lowest
    value of a GUI type field (+0x1660) [M: a sort order], the builder offers the first usable ability, in this order:
    destroy planet, slice, black market, sabotage, spy, neutralize hero, siphon credits, corrupt, remove corruption,
    hack super weapon.
  - So at a restricted planet the black-market slot is not offered, and the unit's next usable ability is offered
    instead.
- **RESTRICT_ALL_ABILITIES (0x85, setter 0x44aae0, list +0x2a0; also SET_PLANET_RESTRICTED P3).**
  - Only reader: 0x4470b0, the first test in 0x3d2020. A planet in the list gives −1: no ability slot at all.
  - 0x3e8890 then hides the ability slot button (component 0x5f, vf+0x78(1)). 0x3eb200 is the builder's other caller
    [M for what it does with −1].
- **ENABLE_SABOTAGE (0x82, manager +0x29a, P1 default 1).** Two readers:
  - 0x2e6770, the sabotage test in 0x3d2020. It needs the planet not in the RESTRICT_SABOTAGE list, the flag on,
    0x3a0910(planet) true, and an ability from `Activated_Sabotage_Ability_Names` (+0x1008). With the flag off the
    sabotage slot is never offered.
  - 0x3cf2e0, reached only through a data pointer [M: the sabotage dialog's confirm callback]. When the GUI's
    sabotage-dialog byte +0x2b08 is set and the flag is on, it queues the sabotage command (event 0x4b4210 filled as
    kind 5 by 0x4b53b0: saboteur, planet, structure, cost) on 0xb2ed18 and closes the dialog (0x3ceb90). With the flag
    off, the confirm does nothing and the dialog stays open.
- **ENABLE_COMBAT_CINEMATIC (0x8a, manager +0x29c).**
  - Only reader: 0x2cc080, the key-up handler of the cinematic / targeting controller 0x140b27a10 (BKC1_RE_ANSWERS).
    It acts only for the key mapped to command 0x4e [M: the cinematic-view key, `TEXT_CINEMATIC_VIEW`].
  - It returns "not handled" when the flag is 0, LOCK_CONTROLS (+0x42) is set, there is no mode, or a GUI dialog has
    focus. Then the combat cinematic camera neither starts nor moves to the next target.
  - Lua `Start_Cinematic_Camera` and the story cinematics do not read the flag.
- **Port:**
  - black market and all-abilities: the existing `galactic::StoryGates` planet sets, read by the galactic ability-slot
    / HUD code;
  - sabotage: a global flag in the story gates, read by the human sabotage slot and its dialog confirm;
  - combat cinematic: a flag read by the apps/game cinematic-camera key.
- **Implemented (mcx6):** `galactic::StoryGates` bits `kNoBlackMarket` / `kNoAbilities` (SET_PLANET_RESTRICTED P3
  writes `kNoAbilities` too) and the flags `sabotage_enabled` / `combat_cinematic`, saved and hashed by the story session.
  ENABLE_SABOTAGE off refuses a human player's sabotage in `GalacticSim::apply_sabotage` and
  `sabotage_execute_structure` [port: the slot test sits in the GUI; the sim refuses the use]. The ability-slot lists and
  the cinematic-view flag have no sim reader: the game host reads `GalacticSim::story_gates()`.

### 1.22 SFX (0x09, inline in 0x456970) [H handler, M player]

Read with REA (mcx6, 2026-11-17).

- **Parameters.** P1 SFX event name. P1 empty: nothing, no log.
- **Effect.** P1 is copied and passed to the SFX manager (0x140b27e60) play-by-name 0x2d5010, with no object and no
  position (a 2-D sound), default flags.
  - 0x2d5010 looks the event up by name (0x2d4750); unknown: "WARNING: Unable to find SFX event data for event name
    '%s'" and nothing plays.
- No player test and no mode test (the local machine plays it, as SPEECH does). No sim state, no save, no draw.
- **Implemented (mcx6):** ledger owner Ui: the game host plays the event from the fired reward (presentation).


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
The spawner part of the 0x32bd10 body is decoded in `MCX4_GAL2_RE_ANSWERS.md` Q5 [H]; the `Sandbox_Objective_1..4`
part is still open.


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
  traced. [M] The black-market, all-abilities and sabotage readers were read in full on 2026-11-17: §1.21 [H]. All
  three serve only the human GUI.
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

- Combat cinematic: +0x29c (ctor 1), P1 default 1; reader 0x2cc080 (the cinematic-camera key-up handler, command
  0x4e, skipped when 0; §1.21).
- ENABLE_SABOTAGE: +0x29a (ctor 1), P1 default 1; readers 0x2e6770 (sabotage slot test) and 0x3cf2e0 (sabotage dialog
  confirm); §1.21.
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
  Param1 in XML, but the AI plan scripts (`Data/Scripts/Interventions/*.lua`) fill Param2 (and Param3 for four) with
  `Set_Reward_Parameter` before `plot.Activate()`; only Pirate_Defection_02, Rebel_Sabotage_02 and
  Indigenous_Revolt_08 never get a Param2 and are dropped (storyb t10, `STORYB_T10_TRIGGER_AI.md` §2, corrects the
  earlier "all Param1-only events are dropped").
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
4. ~~SET_PLANET_SPAWN: reader of the planet spawn map (§1.8)~~ answered 2026-11-17 (mcx6): 0x4b71c0 at tactical
   battle start. Still open: whether the planet map and the spawner state (obj +0x120 +0x38) are saved.
5. Power rewards: the unit-side state values of 0x37df00 and the reader of player +0x378 (§1.5).
6. Exact refusals of the three GUI readers of player +0x39c and of the planet-restricted caller 0x3bd20 (§1.1, §4.2).
7. Whether the conquest dispatch inside 0x3fb040 always fires for PLANET_FACTION (§1.2).
8. Whether the manager flags are reset on a plain save-load (§3.8: reset call sites 0x3d3200 / 0x3027d0 are mode and GUI
   init, load-time order not checked).
9. mcx6 Part A leftovers (2026-11-17):
   - SWITCH_SIDES (§1.10): whether the local index (player list +0x30) is saved. The HUD's pick-up of the new local
     player is inferred [M].
   - SET_HEALTH (§1.12): whether an object left at 0 health is ever killed (a retail probe could show it; no shipped
     or mod data uses the reward). The meaning of the obj +0x3a0 bit 0x01 write.
   - RANDOM_STORY (§1.15): the per-trigger parameter details, and whether the new plot starts active, were read at
     summary level only [M / L]. No data uses the reward.
   - PICK_PLANET (§1.16): which event classes own the three list overrides, and their case rule.
   - STATISTIC_CHANGE (§1.17): the reader of player +0x181 (no byte compare found).
   - ENABLE_COMBAT_CINEMATIC (§1.21): the name of key command 0x4e.
