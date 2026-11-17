#include "alamo/render/weather_fx.hpp"

#include <algorithm>
#include <cmath>

namespace alamo::render {

void apply_fog_plane_alpha(Lighting& l, float alpha) {
    if (!l.fog_enabled || !(alpha != 0.0f)) return;
    l.fog_start = (1.0f - alpha) * l.fog_start + alpha * (l.fog_end - 0.0001f);
}

float LightningFlash::draw() {
    // [port] a 32-bit LCG with 15 output bits (presentation only; the draw order is the port's).
    rng_ = rng_ * 1103515245u + 12345u;
    return static_cast<float>((rng_ >> 16) & 0x7fffu) / 32767.0f;
}

void LightningFlash::step(float lightning) {
    light_.intensity = 0.0f;
    if (!(lightning != 0.0f)) return;
    if (flash_ > 0) {
        --flash_;
        float d[3];
        for (float& c : d) c = 2.0f * draw() - 1.0f;
        const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 0.0f)
            for (float& c : d) c /= len;
        light_.direction = {d[0], d[1], d[2]};
        light_.color = {1, 1, 1};
        light_.intensity = draw() * 0.5f * lightning;
        return;
    }
    if (wait_ > 0) {
        --wait_;
        return;
    }
    wait_ = static_cast<int32_t>(draw() * 500.0f * lightning);
    flash_ = static_cast<int32_t>(draw() * 30.0f * lightning);
}

void LightningFlash::advance(float lightning, double seconds) {
    if (!(seconds > 0.0)) return;
    acc_ += seconds * kStepHz;
    int n = 0;
    while (acc_ >= 1.0 && n < 8) {
        acc_ -= 1.0;
        step(lightning);
        ++n;
    }
    acc_ = std::min(acc_, 1.0);
}

}  // namespace alamo::render
