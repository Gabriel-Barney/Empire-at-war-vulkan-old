// alamo_render - vis1 t01: the presentation side of tactical weather (docs/re/WEATHER_VISUALS.md; the sim side is
// tactical::combat::WeatherSystem, L1_S4_REMAINING §3.2). The sim exposes per tick the weather's emitter intensity,
// lightning intensity and fog-plane alpha; the battle host turns them into
//   * the scenario's weather emitter (emitter_name) in the particle world, its intensity as the system Detail scalar
//     (WEATHER_VISUALS §1; ParticleWorld::set_scalar);
//   * lightning: random white flashes added to the spherical-harmonic lighting (LightningFlash, Lighting::flash, §2);
//   * the fog plane: the scene fog's start pulled toward its end by the fog-plane alpha (apply_fog_plane_alpha, §3).
// Presentation only: nothing here is read by the sim, and the flash uses its own free generator (never SimRng).
#pragma once
#include <cstdint>
#include <string>

#include "alamo/render/lighting.hpp"

namespace alamo::render {

// What the battle host reads from the sim each tick (land battles; inactive in space or without a scenario).
struct WeatherView {
    bool active = false;
    std::string emitter;            // the scenario's emitter_name ("" = none)
    float emitter_intensity = 0.0f; // current (eased) Emitter_Intensity
    float lightning = 0.0f;         // current lightning intensity (the environment's TEE 21 when no weather runs)
    float fog_alpha = 0.0f;         // current Fog_Plane_Interpolation_Alpha
    bool operator==(const WeatherView&) const = default;
};

// §3: with scene fog on, fog start := (1 - alpha) x start + alpha x (end - 0.0001); the end stays. Alpha 0 = the
// environment's fog; alpha 1 = fog only at the far end. No effect with fog off.
void apply_fog_plane_alpha(Lighting& l, float alpha);

// §2: the lightning-flash schedule of one scene. Each step at lightning intensity L > 0:
//   * a flash is running (flash steps left > 0): one step less, and this step's flash is a white light of strength
//     r x 0.5 x L in a new random direction (each component uniform in [-1, 1], normalised);
//   * else a wait is running: one step less, no flash;
//   * else both are over: wait := floor(r x 500 x L) steps, flash := floor(r x 30 x L) steps (r = fresh draws in
//     [0, 1]); no flash this step.
// L = 0: nothing changes and no flash. Retail steps once per rendered frame; [port] the port steps at kStepHz of wall
// clock (advance), so the timing does not depend on the frame rate, and between steps the last flash stays.
class LightningFlash {
public:
    static constexpr double kStepHz = 60.0;
    explicit LightningFlash(uint32_t seed = 0x4c69676eu) : rng_(seed) {}
    void step(float lightning);
    // Steps for `seconds` of wall clock (at most 8 steps per call; a long stall does not replay a backlog).
    void advance(float lightning, double seconds);
    // The flash of the latest step (intensity 0 = none).
    const DirLight& light() const { return light_; }
    int32_t wait_steps() const { return wait_; }
    int32_t flash_steps() const { return flash_; }

private:
    float draw();  // [0, 1]
    uint32_t rng_;
    int32_t wait_ = 0, flash_ = 0;
    double acc_ = 0.0;
    DirLight light_{{0, 0, 1}, {1, 1, 1}, {0, 0, 0}, 0.0f};
};

}  // namespace alamo::render
