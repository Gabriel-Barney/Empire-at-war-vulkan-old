# bkx1 (backlog b0123): tactical goal-system cadence

Question: does the land / space battle AI's goal system use the galactic cadence recovered in A2M_R2_RE_A §2 (Service
0x6cacd0, slice 0x6cb120, Init 0x6ca1f0, quota cap from the PlayerList), and with which N, S and T? Sources: the
Ghidra decompilation of the Steam FoC `StarWarsG.exe` (RVAs), the vtables read from the same exe, shipped
`DIFFICULTYADJUSTMENTS.XML`. Behaviour only. Tags: [H] read from code / data, [M] one inferred link, [L] guess.

## 1. Same class code; only the land Init adds a step

Vtables (slots 0..11, read from the exe) [H]:

| slot | AIGoalSystemClass 0x8bf568 | Galactic 0x8be298 | Land 0x8be308 | Space 0x8be378 |
|---|---|---|---|---|
| 2 Service | 0x6cacd0 | 0x6cacd0 | 0x6cacd0 | 0x6cacd0 |
| 3 service interval | 0x35deb0 (returns 0) | same | same | same |
| 6 Init (begin mode) | 0x6ca1f0 | 0x6ca1f0 | **0x6b87e0** | 0x6ca1f0 |
| 7 end mode | 0x6cafb0 | 0x6cafb0 | 0x6b8860 | 0x6cafb0 |
| 8 target for object | 0x6c9a40 | 0x6b8590 | 0x6c9a40 | 0x6c9a40 |
| 9 skip candidate | (pure) | 0x6b84f0 | 0x6b8710 | 0x6b8900 |
| 10 accept proposal | (pure) | 0x6b8660 | 0x6b8850 | 0x6b8850 |
| 11 sleep after commit | (pure) | 0x6b8630 | 0x6b87b0 | 0x6b8950 |

- Service 0x6cacd0, the slice 0x6cb120 (called directly by Service, not virtual), Commit 0x6c8710 and the quota rule
  are **the same code** for galactic, land and space. No subclass overrides Service or the slice. [H]
- Land Init 0x6b87e0 **first calls the base Init 0x6ca1f0 unchanged**, then does the campaign credit transfer
  (0x612b00, AI_TACTICAL §1.3: only when the mode under the land mode is galactic). It does not touch +0x178 / +0x188 /
  +0x18C. Space uses the base Init itself. So the seed and the first quota are the galactic rule in both modes. [H]
- Slot 11 (frames slept after each Commit, written to the countdown +0x18C) [H]:
  - land 0x6b87b0 = (int)(FPS x difficulty +0x48 `Land_AI_Goal_Cycle_Sleep_Duration`);
  - space 0x6b8950 = (int)(FPS x difficulty +0x44 `Space_AI_Goal_Cycle_Sleep_Duration`);
  - difficulty record = 0x4b0500(the AI's player), as for galactic +0x40. FPS = 30.
  - Shipped: Easy 15.0 s (450 frames) for land and space, Normal and Hard 0.0 (galactic 0.0 on all three).

## 2. N is the global PlayerList, also in battles

- Init and Service read the same globals in every mode: N = size of the PlayerList vector 0x140a16fd0, H = 0x294b80
  (players with `Is_Human` +0x62 and active +0x180). Nothing mode-specific. [H]
- **Skirmish**: `Start_Game` 0x28e890 calls `Create_Default_Players` 0x293390(PlayerList, 0) whenever the mode stack is
  empty (GMM +0x50 == 0), so every new session (campaign or skirmish) builds the list with it. [H] 0x293390 adds the
  session's slot players first (descriptor table 0x140b155e8, sorted; each slot's faction is marked in a bitmask), then
  loops over the whole faction table (count 0x140a172e8) and adds one default player for **every faction no slot
  player has**. The loop has no mode test. [H]
  - So N = (slot players) + (factions not taken by any slot). FoC (8 factions): a 1v1 Empire vs Rebel skirmish has
    N = 2 + 6 = 8, H = 1, N - H = 7, cap (int)(20/7 + 0.5) = **3**. AI vs AI (H = 0): 8 -> cap 3. [H]
  - Two slots with the same faction add a player without freeing a faction: N = slots + 8 - distinct factions. Example:
    4 slots (2 Empire, 2 Rebel), one human: N = 4 + 6 = 10, N - H = 9, cap (int)(2.22 + 0.5) = **2**. [H arithmetic;
    M that skirmish slots may repeat a faction]
  - Only filled slots are in the descriptor table [M: closed slots not checked].
- **Battle entered from a galactic campaign**: the mode stack is not empty, so Start_Game does not rebuild the list; the
  battle uses the campaign's PlayerList (players are only deactivated, never removed). N and H are the galactic ones
  (HotM, human Empire: 8 - 1 = 7, cap 3). [H]
- The other caller that rebuilds the list in a running game (mode load vf13 0x35f970, the "loading base map" step of a
  save load) also uses 0x293390 and then restores the saved players (0x291420). Out of scope (no retail saves), and the
  count follows the same rule. [M]

## 3. S and T in the first pass (Init 0x6ca1f0)

+0x188 = S x max(1, T), quota = max(1, min(ceil(+0x188 / 150), cap)); +0x188 is not cleared. [H]
- **S** = the size of the source list 0x60f6c0(AIPlayerType, mode type), where the mode type is the context's mode
  (context +0x34 -> 0x5f55e0 -> mode vf+0xE0: 0 galactic, 1 land, 2 space). 0x60f6c0 returns AIPlayerType +0xA0 for 0,
  **+0x40 for land, +0x70 for space**. These are the (goal type, function) pairs of the player type's
  `GoalProposalFunctionSets` for that game mode, the same list the slice walks. [H for the call and offsets; M that
  the lists are filtered by the goal type's `GameMode` (AI_GALACTIC §1.4)]
- **T** = the size of the context's perception target list: perception (context +0x38) slot 8, which for land and
  space returns the target vector at +0xE0 (AI_TACTICAL §3, 0x6b9130), unfiltered. The slice uses the same list. [H]
- Timing: context first service 0x5f5ab0 runs slot 6 on Perception **before** Goal (AI_TACTICAL §1.2), so T counts
  what perception's begin-mode added: land = every existing object with `Has_Land_Evaluator` plus every AI hint zone
  region target (0x6b9140); space = every existing object with `Has_Space_Evaluator` plus the region grid (0x6ba570).
  Objects that arrive later (reinforcements, spawns) are not in the seed. [H]
- The campaign player types (e.g. `AI_Player_Control`) and skirmish types (`Faction.Basic_AI`) give different S; take
  S from the battle's AIPlayerType for the battle's mode. [H]

## 4. Other differences from galactic

- **No tactical-only countdown or frame budget.** Service interval slot 3 returns 0 for every goal system, so the next
  service frame is NOW + max(1, 0 x FPS) = every frame (0x64c4d0); the only countdown is +0x18C from slot 11. [H]
- **Slot 9 changes what is counted** (a skipped candidate is not an evaluation, A2M_R2_RE_A §2.1) [H code; M meaning]:
  - land 0x6b8710: skip when the (type, target) goal is already active (0x721a50), or when the candidate's target has
    a reach object (target vf+0x38) whose test vf+0x30 with the goal type's `Reachability` (+0x2C) returns nothing
    (unreachable);
  - space 0x6b8900: skip only when the goal is already active.
- Slot 10 (land and space 0x6b8850: the planning system's plan check 0x6bae50; no budget test as in galactic 0x6b8660)
  runs after the evaluation is counted, so it does not change E. [H]
- Service runs budget weights 0x614590 in tactical too; it does not change the cadence. [H]
- Each battle creates a fresh goal system (context factory at mode creation, AI_TACTICAL §1.1), so the Init seed and
  the "passes 1 and 2 at the cap" effect happen again at every battle start. [H]
- The Goal Service runs only while the context's mode is current and TheAI is enabled for it (0x4d9f80 in 0x5f5ab0);
  while suspended (`Suspend_AI`), neither the countdown nor the cursors move. [H]
- Init and the first Service call are in the same context call (slot 6 loop, then the service loop of 0x5f5ab0) [M:
  that the goal system's next-service frame starts at or below NOW].

## Port rule (tactical)

1. Use the galactic cadence for land and space: set `retail_cadence` for the tactical goal systems (seeded first quota
   S x max(1, T) kept for pass 1, slice ends when its counter reaches the quota, floor(E/q) + 1 frames per pass).
2. `quota_players` = N - H with N = the PlayerList rule of §2: session players + one per faction no session player has
   (the galactic `PlayerAi::quota_players` formula), **not** only the players present in the battle; for a campaign
   battle use the campaign's list. H = active human players.
3. Seed S = the (goal type, function) sources of the AIPlayerType for land (or space); T = the perception target count
   right after perception's begin-mode (existing objects with the mode's evaluator + hint zones / region grid).
4. Countdown after each Commit = (int)(30 x `Land_` / `Space_AI_Goal_Cycle_Sleep_Duration`) of the player's
   difficulty (Easy 450, else 0); no other sleep or budget.
5. Count a candidate only if it passes the mode's slot 9: land also skips unreachable targets, space only active goals.

## Open

- Land slot 9's reach test: which object the target's vf+0x38 returns and what vf+0x30 with `Reachability` checks
  (the land analogue of galactic "no planet for the goal level"). Needed only if port E differs from retail. [M]
- Whether a skirmish slot table can hold closed / empty slots (would add players to N). [M]
- Whether the first Goal Service runs in the same frame as Init (initial next-service frame of the goal system). [M]
