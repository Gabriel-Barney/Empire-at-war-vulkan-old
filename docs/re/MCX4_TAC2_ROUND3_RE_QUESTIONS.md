# MCX4 tac2 round 3: ReVa-only questions (lane mcx4-tac2, 2026-11-18)

Rows still open after REA and the decompile export (`MCX4_T10_RE_ANSWERS.md`, `MCX4_T11_T12_RE_ANSWERS.md`). Each needs
an indirect-call or displacement scan that only ReVa / Ghidra scripting gives. Answers go into the named section of the
named file (append a "Round 3" paragraph), with RVAs and [H] / [M] / [L]. "No reader / never acts on it [H]" is a valid
answer (the row becomes retail-unused).

1. **`GameObjectType.Is_Escort`** (+0xb8, 193 FoC uses) → `MCX4_T10_RE_ANSWERS.md` §1. The only type reader is
   `CombatantBehaviorClass` (vtable 0x1408b21d0, behaviour id 0x13) slot 32 (+0x100) = 0x140638b20. None of the 14
   decompiled functions that fetch behaviour 0x13 through Get_Behavior call +0x100, and there is no direct call. Find the
   indirect call sites whose receiver is a combatant behaviour (a stored pointer, a list of combatants, the squadron /
   company combatant classes 0x6cbb80's vtables) and what they do with the result (formation, guard / escort AI,
   targeting, fleet movement).
2. **`GameObjectType.Formation_Priority`** (+0x654, 195 uses) → `MCX4_T11_T12_RE_ANSWERS.md` §B2. Reader: combatant slot
   33 (+0x108) 0x638710 (and 0x6cbb80 for squadron / company). The 22 indirect +0x108 call sites listed in §B2 belong to
   several classes: separate the ones whose receiver is a combatant behaviour and say what the value orders.
3. **`GameObjectType.Space_Full_Stop_Command`** (+0x38, 29 uses) → §B2. No type-tagged access was found; judge the 8
   parameter-based byte readers listed there (0x24a870, 0x92940, 0x3c360, 0x67b310, 0x67b4c0, 0x67b730, 0x69aa80,
   0x7293e0) and any wide load of the type flag block that covers +0x38.
4. **`GameConstants.GripperCombatGridSnapDistance`** → §B4: who calls 0x405480 and what the true result does.
5. **AI player `Aggressiveness` / `Focus`** (ledger rows `Arc_Sweep_Attack_Ability.Aggressiveness` / `.Focus`; really the AI
   player type's `<Personality>` +0x20 / +0x24, `MCX4_T10_RE_ANSWERS.md` §14): readers of those offsets on an AI player type
   pointer, and the effect (tactical or galactic AI).

**Answered 2026-11-18 (ReVa), each in the "Round 3" paragraph of the named section.**
1. No caller [H].
2. Never read [H].
3. No reader [H for the readers judged, M overall].
4. HUD placement of the per-squadron elements, presentation only [H, M where tagged].
5. No reader found [M].

Rows 1, 2, 3 and 5 have no effect in retail; row 4 changes only the HUD.
