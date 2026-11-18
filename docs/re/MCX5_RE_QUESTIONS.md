# MCX5 RE questions (limits policy, docs/MOD_COMPAT_DESIGN.md §8)

Group mcx5 (box MCX2). Every fixed size in sim code must be retail-cited, data, dynamic or a guard. These questions ask
the binary which of the port's fixed 5s / 100s / 1985s are retail's and which are GameConstants reads. Answers go to
`docs/re/MCX5_RE_ANSWERS.md` (behaviour in words, addresses, [H]/[M] marks). Lane mcx4-gal asks the non-AI readers of
the base-level maxima (`MCX4_RE_QUESTIONS.md` Q2.1: build / upgrade / HUD caps); do not duplicate that here.

## Q1. GameConstants maxima in the galactic AI (t02)

**Settled without Ghidra (2026-11-18, applied in t02):** Q1.1, Q1.3 and Q1.4 are answered by `AI_GALACTIC_TOKENS.md`
(§2 neighbour lists, the §4 target-token table, the §5 player-token table, all [H]) and `MCX4_RE_ANSWERS_GALACTIC.md`
Q2.1 (every RIP-relative read of GC +0xF0..+0x10F; +0xF0 0x3f8483 is inside potential income 0x3f83b0): each divisor is
the live GC field, and only MaxStructureSlots fails on 0. Still open: Q1.2, Q1.5-Q1.9.

GC field offsets (AI_GALACTIC_TOKENS.md, field_schema_table): +0xF0 `MaximumPoliticalControl`, +0xF4
`MaximumStarbaseLevel`, +0xF8 `MaximumGroundbaseLevel`, +0xFC `MaximumSpecialStructures`, +0x108
`MaximumFleetMovementDistance` (all 5 / 5 / 5 / 5 / 100.0 in stock FoC).

1. Token normalizers. For each of these galactic AI tokens, which GC field (offset) is the divisor, and is it read from
   the live GameConstants object at evaluation time (so a mod's value applies), or is it a literal in the code?
   `Target.StarbaseLevel`, `Target.GroundbaseLevel`, `Target.MaxStarbaseLevel`, `Target.MaxGroundbaseLevel`,
   `Target.MaxStructureSlots`, `Target.PoliticalControl` / `TargetPoliticalControl`, `Self.MaxStarbaseLevel`,
   `Self.MaxGroundbaseLevel` (and their `...Unnormalized` forms), `HasStarbaseOfLevel`, `NumStarbaseOfLevel`,
   `MaxedStarbases`, `MaxedGroundbases`. Is the division float or integer, and what does a 0 divisor do?
2. `Target.MaxGroundbaseLevel`: the port takes min(5, max ground base) before dividing. Is that min in retail, and is
   its 5 a literal or a GC field?
3. Political income on the token path (port galactic_ai_tokens.cpp ~1034, `income_political(b, 5, curve)`): which value
   is passed as the control level (GC +0xF0 or a literal)?
4. `MaximumFleetMovementDistance` in the AI: the `IsConnectedTo` neighbour test and the attenuated distance terms
   (`1 - d / 100 * A`): read from GC +0x108 or the literal 100?
5. Star base upgrade choice (0x372780 area, port galactic_ai.cpp ~448 `starbase_level < 5`): is the 5 a literal or
   `MaximumStarbaseLevel`?
6. Source-planet levels (0x665160, port `std::clamp(level, 0, 5)`, six cached lists): is the list count fixed at 6 in
   retail, and does the level range follow a GC field?
7. Lua `WaitForStarbase` / `WaitForGroundbase`: the "Must be between %d and %d" bounds - literals or the GC maxima?
8. MagicPlan spawnable test (`tech_level > 5`, AI_GALACTIC_3 §4.1) and the galactic AI tech cap: literal 5 or a
   faction / GC value?
9. The GameConstants defaults (0x2c91c0, used when a mod's GameConstants.xml has no tag): which values does it write
   for +0xF0, +0xF4, +0xF8 and +0x108? (+0xFC is 1, MCX4_RE_ANSWERS_GALACTIC Q2.1.)

## Q2. Terrain grid limit (t03)

The port's .ted reader refuses a map whose vertex grid is larger than 31 * 64 + 1 = 1985 per side (`ted.hpp`
`kMaxGridVerts`). Stock maps reach 373.
1. Does retail's terrain loader (map info chunk / height-map chunk reader) bound the grid width / height? If so, what
   value and what happens above it (refuse the map, clamp, crash)?
2. Any other fixed per-map array in the loader that bounds the size (patch count, material slots, passability grid)?
3. The `.ted` material slots: the port keeps 64 (`ted.hpp` `kMaterialSlots`, the map-info mini 05 count is 64 in every
   vanilla map), drops material records past 64 and clamps material ids >= 64 to 63. Does retail's loader (0x132a30)
   read the slot count from the file and size its table from it, or is 64 fixed?

## Q3. Game-spec tech range (t03)

Port `GameSession::start` (engine/apps/game/src/game_session.cpp ~130) clamps the advanced options' max / start tech
level to 1..5.
1. Retail's campaign / skirmish game-options path: where are the starting and max tech levels applied to the players,
   and are they clamped (range, literal or data)?
2. Does any data (faction XML, GameConstants, campaign XML) set the top tech level, or is 5 a literal across the code
   (tech upgrade, `Set_Tech_Level`, the tech research path)?

## Q4. Citations for limits the lint tagged retail without an address (t05)

1. Tactical battle sides: the port's per-side battle statistics hold 8 sides (`tactical_battle.hpp` `kMaxSides`,
   SKIRMISH_SETUP §1 "8 skirmish slots"). Where does retail bound the number of sides / players in a tactical battle
   (skirmish lobby slots, map player count, the battle's player array), and is it 8?
2. RANDOM_STORY CONSTRUCT_LEVEL retries (STORY_REWARDS_GALACTIC §1.15, 0x4bec90): the port tries 100 times
   (`story_random_story.cpp` `kMaxTries`, "the retail retry count"). Confirm the count and its address.
3. Story event cascades within one frame: the port drains reward-to-event chains in at most 8 rounds per story
   service (`story_session.hpp` `kMaxRounds`) and defers the rest one service. Does retail's story service loop until
   no event is pending (no cap), or does it have a round / depth bound?
