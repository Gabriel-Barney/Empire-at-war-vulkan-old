// mcx4 t15 (MCX4_PRES_RE_ANSWERS_2 Q7, apply step 0x392fe0 from the colour callback 0x3a0c20): an object light effect
// is a colour pulse on the model's colour multiplier (RGB only; not an added light). Presentation only, wall time.
#pragma once
#include <cmath>
#include <cstdint>

namespace alamo::render {

struct LightPulse {
    uint8_t type = 0;  // 1 Color_Flash, 2 Linear_Pulse, 3 Smooth_Pulse, 4 Two_Way_Pulse, 5 Continuous_Smooth_Pulse
    float c1[3] = {1.0f, 1.0f, 1.0f}, c2[3] = {1.0f, 1.0f, 1.0f};
    float duration = 0.0f;  // D, seconds
    int32_t pulses = 1;     // N
    // mcx4 t14 (MCX4_PRES_RE_ANSWERS_3 Q10, the object colour flash 0x392ba0): type 1 restarts this many times in a row
    // (a GUI flash: count 1 or 3); total length D x repeats.
    int32_t repeats = 1;
    double start = 0.0;
};

// The RGB multiplier at time `now` for the base multiplier `base`; false once the pulse is over (e outside [0, D);
// type 5 never ends). Types (Q7.3): 1 C1 + (B - C1) e / D; 2..4 phase = frac(e / (D / N)), 2 triangle, 3 sin(pi phase)
// towards C1, 4 sin(2 pi phase) towards C1 (>= 0) or C2 (< 0); 5 C1 + (B - C1) (cos(2 pi frac(e / D)) + 1) / 2.
inline bool light_pulse_rgb(const LightPulse& p, double now, const float base[3], float out[3]) {
    constexpr double kPi = 3.14159265358979323846;
    for (int k = 0; k < 3; ++k) out[k] = base[k];
    if (p.type == 0 || !(p.duration > 0.0f)) return false;
    double e = now - p.start;
    const double reps = p.type == 1 && p.repeats > 1 ? static_cast<double>(p.repeats) : 1.0;
    if (p.type != 5 && (e < 0.0 || e >= static_cast<double>(p.duration) * reps)) return false;
    if (reps > 1.0 && e >= 0.0) e = std::fmod(e, static_cast<double>(p.duration));
    if (e < 0.0) return true;
    const auto lerp = [](float a, float b, double t) { return static_cast<float>(static_cast<double>(a) + (static_cast<double>(b) - a) * t); };
    const double d = static_cast<double>(p.duration);
    const double per = d / static_cast<double>(p.pulses > 0 ? p.pulses : 1);
    const double ph = e / per - std::floor(e / per);
    switch (p.type) {
        case 1:
            for (int k = 0; k < 3; ++k) out[k] = lerp(p.c1[k], base[k], e / d);
            break;
        case 2: {
            const double f = ph < 0.5 ? 2.0 * ph : 2.0 * (1.0 - ph);
            for (int k = 0; k < 3; ++k) out[k] = lerp(base[k], p.c1[k], f);
            break;
        }
        case 3: {
            const double f = std::sin(kPi * ph);
            for (int k = 0; k < 3; ++k) out[k] = lerp(base[k], p.c1[k], f);
            break;
        }
        case 4: {
            const double s = std::sin(2.0 * kPi * ph);
            for (int k = 0; k < 3; ++k) out[k] = s >= 0.0 ? lerp(base[k], p.c1[k], s) : lerp(base[k], p.c2[k], -s);
            break;
        }
        case 5: {
            const double f = (std::cos(2.0 * kPi * (e / d - std::floor(e / d))) + 1.0) / 2.0;
            for (int k = 0; k < 3; ++k) out[k] = lerp(p.c1[k], base[k], f);
            break;
        }
        default: return false;  // 6: code only (red-channel alpha, [L]); not modelled
    }
    return true;
}

}  // namespace alamo::render
