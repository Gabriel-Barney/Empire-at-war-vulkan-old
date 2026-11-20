// Data-driven seconds -> integer frame counts for sim code, one function per rounding form the original uses (ref4 t03).
//
// The original turns "seconds x frames per second" (a float from XML, Lua or GameConstants) into an int32 frame count with
// the x86 conversion instructions, where a NaN or a value outside [-2^31, 2^31) gives 0x80000000 (alamo/core/x86_int.hpp).
// A plain static_cast of such a float to an integer is undefined behaviour out of range, and MSVC and clang really differ
// there (the Age of Legends Defense_Duration_In_Secs split of the mod golden aol_space, 9632d392 / a2b08a39). Every sim
// conversion of data-driven seconds to frames goes through these, so a mod value out of range gives the same frame count
// on both compilers (CLAUDE.md rule 3); tools/lint/check_frame_casts.py flags a new cast outside engine/core.
//
// The argument is the frame value exactly as the site computes it, in float or double as the original does: seconds x
// frames per second, in either order; the "add half, then truncate" form the original writes as (int)(x + 0.5) is
// frames_trunc(x + 0.5f). Results in range are the plain cast's, so moving a site onto these moves no CRC.
#pragma once
#include <cmath>
#include <cstdint>

#include "alamo/core/x86_int.hpp"

namespace alamo {

// Truncation toward zero (cvttss2si / cvttsd2si): (int)(seconds x fps).
constexpr int32_t frames_trunc(float frames) noexcept { return trunc_i32(frames); }
constexpr int32_t frames_trunc(double frames) noexcept { return trunc_i32(frames); }

// Round half away from zero (std::round), then the x86 range behaviour.
inline int32_t frames_round(float frames) noexcept { return trunc_i32(std::round(frames)); }
inline int32_t frames_round(double frames) noexcept { return trunc_i32(std::round(frames)); }

// Ceiling (the first whole frame at or after the time), then the x86 range behaviour.
inline int32_t frames_ceil(float frames) noexcept { return trunc_i32(std::ceil(frames)); }
inline int32_t frames_ceil(double frames) noexcept { return trunc_i32(std::ceil(frames)); }

}  // namespace alamo
