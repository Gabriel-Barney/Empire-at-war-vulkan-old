// ui2 t03 / t05: the story script's cinematic camera, fade and letterbox (story_camera.hpp).
#include "alamo/game/story_camera.hpp"

#include <algorithm>
#include <cmath>

namespace alamo::game {

using tactical::Vec3;

namespace {
constexpr float kDegToRad = 3.14159265358979f / 180.0f;
}  // namespace

// §2.4 arg 5: (a, b, c) = (distance, pitch, yaw) degrees: a vector of length `distance` along -y turned by -pitch about x
// (a positive pitch puts the point above the base), then by yaw about z. Else the plain xyz offset.
Vec3 ScriptCamera::offset_of(const ScriptKeyCall& k) {
    if (!k.euler) return {k.a, k.b, k.c};
    const float pitch = k.b * kDegToRad, yaw = k.c * kDegToRad;
    const float flat = k.a * std::cos(pitch);
    return {flat * std::sin(yaw), -flat * std::cos(yaw), k.a * std::sin(pitch)};
}

ScriptCamera::Key ScriptCamera::key_from(const ScriptKeyCall& k, int32_t clock) {
    Key key;
    key.clock = clock;
    key.attached = k.object >= 0;
    key.object = k.object;
    key.base = k.base;
    key.offset = offset_of(k);
    key.use_rotation = k.use_rotation;
    return key;
}

Vec3 ScriptCamera::position(const Key& k, const Resolver& r, int track) const {
    if (k.attached && r) {
        ObjectFrame f;
        if (r(k.object, f) && f.valid) {
            const Vec3 p = k.use_rotation ? f.apply(k.offset) : f.position + k.offset;
            last_good_[track] = p;
            return p;
        }
        return last_good_[track];  // the object is gone (or was never seen): the last good position
    }
    return k.base + k.offset;
}

Vec3 ScriptCamera::evaluate(const std::vector<Key>& keys, int track, int32_t clock, const Resolver& r) const {
    if (keys.empty()) return last_good_[track];
    if (clock < keys.front().clock) return position(keys.front(), r, track);
    size_t k = 0;
    while (k + 1 < keys.size() && keys[k + 1].clock <= clock) ++k;  // the latest of equal clocks wins
    const Key& a = keys[k];
    if (k + 1 >= keys.size() || !a.blend) return position(a, r, track);
    const Key& b = keys[k + 1];
    const float sigma = static_cast<float>(clock - a.clock) / static_cast<float>(b.clock - a.clock);
    const float w = shot_accel(sigma, a.ease_out, b.ease_in);
    const Vec3 pa = position(a, r, track), pb = position(b, r, track);
    return pa + (pb - pa) * w;
}

void ScriptCamera::insert(std::vector<Key>& keys, const Key& k) {
    std::erase_if(keys, [&](const Key& o) { return o.clock > k.clock; });  // a new key replaces the track's future (§2.4)
    keys.push_back(k);
}

void ScriptCamera::start(const ScriptPose& rts, uint32_t frame) {
    active_ = true;
    start_frame_ = frame;
    eye_.clear();
    look_.clear();
    Key e, l;
    e.base = rts.eye;
    l.base = rts.target;
    eye_.push_back(e);
    look_.push_back(l);
    last_key_[0] = last_good_[0] = rts.eye;
    last_key_[1] = last_good_[1] = rts.target;
}

void ScriptCamera::end() {
    active_ = false;
    eye_.clear();
    look_.clear();
}

void ScriptCamera::set_key(bool camera, const ScriptKeyCall& k, uint32_t frame) {
    if (!active_) return;
    const int track = camera ? 0 : 1;
    const Key key = key_from(k, clock(frame));
    insert(camera ? eye_ : look_, key);
    last_key_[track] = key.base + key.offset;
}

void ScriptCamera::transition_key(bool camera, const ScriptKeyCall& k, double seconds, uint32_t frame) {
    if (!active_) return;
    const int32_t frames = static_cast<int32_t>(static_cast<double>(kFps) * seconds);
    if (!(seconds > 1.0 / kFps) || frames <= 0) {  // an instant: a hold key like Set_*_Key
        set_key(camera, k, frame);
        return;
    }
    const int track = camera ? 0 : 1;
    std::vector<Key>& keys = camera ? eye_ : look_;
    const int32_t c = clock(frame);
    Key start;
    start.clock = c;
    start.blend = true;
    start.base = last_key_[track];  // the move starts from wherever the track was last put or ended up
    insert(keys, start);
    const Key end = key_from(k, c + frames);
    keys.push_back(end);
    last_key_[track] = end.base + end.offset;
}

ScriptPose ScriptCamera::pose(uint32_t frame, const Resolver& r) const {
    const int32_t c = clock(frame);
    return {evaluate(eye_, 0, c, r), evaluate(look_, 1, c, r)};
}

void ScriptCamera::to_tactical(const ScriptPose& rts, double seconds, uint32_t frame, const Resolver& r) {
    if (!active_) return;
    const ScriptPose now = pose(frame, r);
    start_frame_ = frame;
    eye_.clear();
    look_.clear();
    const int32_t frames = std::max(1, static_cast<int32_t>(static_cast<double>(kFps) * seconds));
    auto track = [&](std::vector<Key>& keys, const Vec3& from, const Vec3& to) {
        Key a, b;
        a.base = from;
        a.blend = true;
        a.ease_out = a.ease_in = 0.25f;
        b.clock = frames;
        b.base = to;
        b.ease_out = b.ease_in = 0.25f;
        keys.push_back(a);
        keys.push_back(b);
    };
    track(eye_, now.eye, rts.eye);
    track(look_, now.target, rts.target);
    last_key_[0] = rts.eye;
    last_key_[1] = rts.target;
}

void ScriptCamera::zoom(double seconds, double scale, uint32_t frame, const Resolver& r) {
    if (!active_) return;
    const ScriptPose now = pose(frame, r);
    start_frame_ = frame;
    eye_.clear();
    look_.clear();
    const int32_t frames = std::max(1, static_cast<int32_t>(static_cast<double>(kFps) * seconds));
    const Vec3 to = now.target + (now.eye - now.target) * static_cast<float>(scale);
    Key a, b;
    a.base = now.eye;
    a.blend = true;
    b.clock = frames;
    b.base = to;
    eye_.push_back(a);
    eye_.push_back(b);
    Key l;
    l.base = now.target;
    look_.push_back(l);
    last_key_[0] = to;
    last_key_[1] = now.target;
}

// ---- fade ---------------------------------------------------------------------------------------------------------

void ScreenFade::begin(Mode m, double seconds, double now) {
    mode_ = m;
    start_ = now;
    seconds_ = seconds;
}

float ScreenFade::alpha(double now) const {
    const double t = seconds_ > 0.0 ? std::clamp((now - start_) / seconds_, 0.0, 1.0) : 1.0;
    switch (mode_) {
        case Mode::Clear: return 0.0f;
        case Mode::Black: return 1.0f;
        case Mode::Out: return static_cast<float>(t);        // clear -> black, then it stays black
        case Mode::In: return static_cast<float>(1.0 - t);   // from black to clear
    }
    return 0.0f;
}

// ---- letterbox ----------------------------------------------------------------------------------------------------

float ScreenLetterbox::level(double now) const {
    if (!(seconds_ > 0.0)) return to_;
    const float t = static_cast<float>(std::clamp((now - t0_) / seconds_, 0.0, 1.0));
    return from_ + (to_ - from_) * t;
}

void ScreenLetterbox::bar_in(double seconds, double now) {
    if (to_ >= 1.0f) return;  // in already, or animating in: nothing
    from_ = level(now);
    seconds_ = seconds * static_cast<double>(1.0f - from_);  // reversing an out: scaled by what is still to show [M]
    t0_ = now;
    to_ = 1.0f;
    hud_hidden_ = true;  // the first bar-in hides the HUD
}

void ScreenLetterbox::bar_out(double seconds, double now) {
    const float cur = level(now);
    if (cur <= 0.0f && to_ <= 0.0f) return;  // no bars: nothing
    from_ = cur;
    seconds_ = seconds * static_cast<double>(cur);  // from settled: the time as given; mid-animation: scaled by the fraction
    t0_ = now;
    to_ = 0.0f;
}

void ScreenLetterbox::on() {
    from_ = to_ = 1.0f;
    seconds_ = 0.0;
    hud_hidden_ = true;
}

void ScreenLetterbox::off() {
    from_ = to_ = 0.0f;
    seconds_ = 0.0;
    hud_hidden_ = false;
}

void ScreenLetterbox::update(double now) {
    if (to_ <= 0.0f && level(now) <= 0.0f) hud_hidden_ = false;  // the end of the bar-out restores the HUD
}

}  // namespace alamo::game
