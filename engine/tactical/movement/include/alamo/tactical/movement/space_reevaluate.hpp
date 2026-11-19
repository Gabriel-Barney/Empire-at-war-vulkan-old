// mcx4 t11: the space coordinator's attack re-evaluation (vf5 0x5d51e0 -> 0x5d55f0 -> re-plan 0x5d4730),
// docs/re/MCX4_T11_T12_RE_ANSWERS.md §B4 row MovementReevaluationFrameCount and its Round 4 paragraph [H unless tagged].
// The rules as pure functions; the battle host runs the pass (TacticalBattle::space_attack_reevaluate) because the
// port's space coordinator keeps no attack state.
#pragma once
#include <cstdint>

namespace alamo::tactical::movement {

// The gate of 0x5d51e0: coordinator id % N = frame % N and frame >= the coordinator's not-before frame (+0xb0; [port M]
// 0: the port's re-plan has no stagger). N = MovementReevaluationFrameCount; [port] N = 0 never runs (retail divides by 0).
inline bool space_reevaluation_due(uint32_t id, uint32_t frame, uint32_t n, uint32_t not_before = 0) {
    return n != 0 && id % n == frame % n && frame >= not_before;
}

// 0x5d55f0 for an ordinary (non-guard) block: TARGETING slot 43 (In_Max_Range, 0x5673b0) against the coordinator's
// target is tried from the lead's slot point first, then from its position. Neither in range: re-plan. One in range:
// re-plan when slot 45 (Inside_Min_Range, 0x5688c0) holds.
inline bool space_attack_replan(bool in_range_from_slot, bool in_range_from_pos, bool inside_min) {
    if (!in_range_from_slot && !in_range_from_pos) return true;
    return inside_min;
}

}  // namespace alamo::tactical::movement
