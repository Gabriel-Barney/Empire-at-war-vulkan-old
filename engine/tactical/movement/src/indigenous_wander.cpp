// mcx4 t11: the INDIGENOUS_UNIT wander point (0x621d80); see indigenous_wander.hpp.
#include "alamo/tactical/movement/indigenous_wander.hpp"

#include <algorithm>
#include <cmath>

namespace alamo::tactical::movement {

bool indigenous_wander_point(const IndigenousWanderInput& in, SimRng& rng, WanderPoint& out) {
    // 2. Can_Indigenous_Unit_Stop: stays put while the draw is above 0.1 (90 %)
    if (in.can_stop && rng.range_float(0.0f, 1.0f) > 0.1f) return false;
    // 3. no map rectangle
    if (in.cx == 0.0f && in.cy == 0.0f && in.hx == 0.0f && in.hy == 0.0f) return false;
    // 4. inside the leash around home (x first, then y inside the circle), else anywhere on the map rectangle
    WanderPoint p;
    const float d_leash = in.home.leash;
    if (in.home.has_home() && d_leash > 0.0f) {
        const float dx = rng.range_float(-d_leash, d_leash);
        const float s = std::sqrt(std::max(0.0f, d_leash * d_leash - dx * dx));
        const float dy = rng.range_float(-s, s);
        p = {in.home.home.x + dx, in.home.home.y + dy, in.home.home.z};
    } else {
        p.x = rng.range_float(in.cx - in.hx, in.cx + in.hx);
        p.y = rng.range_float(in.cy - in.hy, in.cy + in.hy);
        p.z = 0.0f;
    }
    // 5. the step length: a uniform distance between the bounds when the drawn point is too near or too far
    const float hi = in.burning ? kWanderBurningMax : kWanderMax, lo = in.burning ? kWanderBurningMin : kWanderMin;
    const float vx = p.x - in.pos.x, vy = p.y - in.pos.y, vz = p.z - in.pos.z;
    const float d = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (d > hi || d < lo) {
        const float t = rng.range_float(lo, hi);
        if (d > 0.0f) {
            const float k = t / d;
            p = {in.pos.x + vx * k, in.pos.y + vy * k, in.pos.z + vz * k};
        }
    }
    out = p;
    return true;
}

}  // namespace alamo::tactical::movement
