// x86 integer semantics for the ability timers (mcx7, 2026-11-18).
//
// The original converts "seconds * frames per second" to an int32 frame count with a truncating conversion
// (cvttss2si) and counts it down with a 32-bit decrement. On x86 both are defined: a value outside the int32 range (or
// NaN) gives 0x80000000 ("integer indefinite"), and 0x80000000 - 1 wraps to 0x7fffffff. In C++ both are undefined
// behaviour, and the two compilers of the port really do differ: Star Wars: Age of Legends sets
// Defense_Duration_In_Secs to 99999999999 (a laser defense that never ends) on about a hundred units, and the clang build
// dropped the effect after one frame while the MSVC build (like retail) kept it, so the mod's skirmish CRC differed
// between the compilers from tick 2 (mod golden aol_space). These helpers give the x86 result on every compiler.
#pragma once
#include <cstdint>

namespace alamo::tactical::abilities {

// float -> int32 truncation toward zero; NaN and values outside [-2^31, 2^31) give INT32_MIN, as cvttss2si does.
constexpr int32_t trunc_i32(float v) noexcept {
    if (!(v >= -2147483648.0f && v < 2147483648.0f)) return INT32_MIN;
    return static_cast<int32_t>(v);
}

// v - 1 with 32-bit wrap-around (INT32_MIN - 1 == INT32_MAX), the machine decrement instead of signed overflow.
constexpr int32_t dec_wrap(int32_t v) noexcept { return static_cast<int32_t>(static_cast<uint32_t>(v) - 1u); }

}  // namespace alamo::tactical::abilities
