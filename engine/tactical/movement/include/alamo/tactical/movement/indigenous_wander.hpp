// mcx4 t11: the INDIGENOUS_UNIT wander point (IndigenousUnitBehaviorClass vf6 0x621d80), docs/re/MCX4_T11_T12_RE_ANSWERS.md
// §B2 "Max_Distance_From_Spawner" with its Round 4 paragraph, and docs/re/TRU1_C_RE.md §1.3 / §1.4 [H unless tagged].
// A pure function plus the per-unit home the spawner writes; the battle host runs the behaviour's service (interval 30,
// 1 while burning), makes the step-1 tests and issues the group move to the returned point.
#pragma once
#include <cstdint>

#include "alamo/core/sim_rng.hpp"

namespace alamo::tactical::movement {

struct WanderPoint {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// The INDIGENOUS_UNIT pack's home (pack +0x28..+0x30; (-1, -1, -1) = none) and leash D (pack +0x34), written when a
// SPAWN_INDIGENOUS_UNITS spawner places the unit (0x531b60): home := the spawner's position, D := the spawner type's
// Max_Distance_From_Spawner.
struct IndigenousHome {
    WanderPoint home{-1.0f, -1.0f, -1.0f};
    float leash = 0.0f;
    bool has_home() const { return !(home.x == -1.0f && home.y == -1.0f && home.z == -1.0f); }
};

// The step bounds of the wander (.data 0x140a442c8..0x140a442d4, read from the exe): (max, min) = (400, 50), or
// (150, 60) while the unit burns.
inline constexpr float kWanderMax = 400.0f, kWanderMin = 50.0f;
inline constexpr float kWanderBurningMax = 150.0f, kWanderBurningMin = 60.0f;

struct IndigenousWanderInput {
    WanderPoint pos;          // the unit's position
    IndigenousHome home;      // its pack's home and leash
    bool burning = false;     // object +0x3a1 bit 2 (the step bounds)
    bool can_stop = true;     // its type's Can_Indigenous_Unit_Stop (+0x84; default true)
    // the map rectangle (mode vf +0x1f8): centre and half extents; all zero = none (the service then returns)
    float cx = 0.0f, cy = 0.0f, hx = 0.0f, hy = 0.0f;
};

// Steps 2 - 5 of 0x621d80, for a unit that passed step 1 (idle: not moving, no target, an idle clip). Synced draws in
// retail order:
//   2. with can_stop: r = float(0, 1); r > 0.1 -> stay (false; [port M] the behaviour's +0x34 gate is taken as passed);
//   3. no map rectangle -> stay (false);
//   4. a home and D > 0: dx = float(-D, D), s = sqrt(D^2 - dx^2), dy = float(-s, s), point = home + (dx, dy, 0); else
//      point = (float(cx - hx, cx + hx), float(cy - hy, cy + hy), 0);
//   5. d = |point - pos| (3-D); outside [min, max]: t = float(min, max), point = pos + (point - pos) x t / d ([port] d = 0
//      keeps the point: retail would divide by 0).
// True: a group move to `out` (0x6218f0).
bool indigenous_wander_point(const IndigenousWanderInput& in, SimRng& rng, WanderPoint& out);

}  // namespace alamo::tactical::movement
