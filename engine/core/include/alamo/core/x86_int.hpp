// x86 integer conversion semantics for sim code (mcx7 2026-11-18 in the abilities module; moved to core by mcx8 t11).
//
// The original converts "seconds * frames per second" to an int32 frame count with a truncating conversion
// (cvttss2si) and counts it down with a 32-bit decrement. On x86 both are defined: a value outside the int32 range (or
// NaN) gives 0x80000000 ("integer indefinite"), and 0x80000000 - 1 wraps to 0x7fffffff. In C++ both are undefined
// behaviour, and the two compilers of the port really do differ: Star Wars: Age of Legends sets
// Defense_Duration_In_Secs to 99999999999 (a laser defense that never ends) on about a hundred units, and the clang build
// dropped the effect after one frame while the MSVC build (like retail) kept it, so the mod's skirmish CRC differed
// between the compilers from tick 2 (mod golden aol_space). These helpers give the x86 result on every compiler, so a
// mod value out of int32 range keeps the per-tick CRC equal between MSVC and clang (CLAUDE.md rule 3,
// docs/MOD_COMPAT_DESIGN.md §13.3).
//
// Use them wherever sim code turns a data-driven float into an int (static_cast<int32_t>(float) is UB out of range) and
// wherever it decrements an int32 countdown that may start at INT32_MIN.
#pragma once
#include <cstdint>

namespace alamo {

// float -> int32 truncation toward zero; NaN and values outside [-2^31, 2^31) give INT32_MIN, as cvttss2si does.
constexpr int32_t trunc_i32(float v) noexcept {
    if (!(v >= -2147483648.0f && v < 2147483648.0f)) return INT32_MIN;
    return static_cast<int32_t>(v);
}

// The same for a double (cvttsd2si).
constexpr int32_t trunc_i32(double v) noexcept {
    if (!(v >= -2147483648.0 && v < 2147483648.0)) return INT32_MIN;
    return static_cast<int32_t>(v);
}

// v - 1 with 32-bit wrap-around (INT32_MIN - 1 == INT32_MAX), the machine decrement instead of signed overflow.
constexpr int32_t dec_wrap(int32_t v) noexcept { return static_cast<int32_t>(static_cast<uint32_t>(v) - 1u); }

// a + b with 32-bit wrap-around (the machine add of two int32 frame counts).
constexpr int32_t add_wrap(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

}  // namespace alamo
