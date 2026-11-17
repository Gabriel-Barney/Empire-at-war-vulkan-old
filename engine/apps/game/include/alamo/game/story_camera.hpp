// ui2 t03 / t05: the story script's cinematic camera (docs/re/STORY_PRESENTATION_LUA.md §2): key tracks for the eye and the
// look-at target, on the shot clock in mode frames (30 per second, a pause stops them). Set_Cinematic_*_Key writes a hold
// key, Transition_Cinematic_*_Key a start key at the last key position and a blend key `int(30 x seconds)` later (ease
// 0.5 / 0.5), Transition_To_Tactical_Camera a blend to the RTS camera's pose (ease 0.25 / 0.25), Cinematic_Zoom a move along
// the eye-to-target line. Presentation only, data-free and clock-free (the caller passes frames), so the model is covered
// by tests without a window.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "alamo/game/battle_fx.hpp"  // ObjectFrame, shot_accel
#include "alamo/tactical/geometry.hpp"

namespace alamo::game {

// One Set_/Transition_Cinematic_Camera_Key / _Target_Key call as the script gave it (§2.4).
struct ScriptKeyCall {
    tactical::Vec3 base;        // the position argument, resolved when the script called
    int32_t object = -1;        // attach_object (arg 6), -1 none
    float a = 0, b = 0, c = 0;  // x / y / z offset, or distance / pitch / yaw degrees when `euler`
    bool euler = false;         // arg 5 > 0
    bool use_rotation = false;  // arg 7 > 0: the offset in the attached object's rotating frame
    bool animation = false;     // arg 8 > 0 (the muzzle-bone sampling is not drawn: [port] the object's origin)
};

struct ScriptPose {
    tactical::Vec3 eye, target;
};

class ScriptCamera {
public:
    static constexpr int32_t kFps = 30;
    // The attached object's current frame (position + axes) or false (gone: the last good position is used).
    using Resolver = std::function<bool(int32_t object, ObjectFrame& out)>;

    bool active() const { return active_; }
    // Start_Cinematic_Camera: the tracks are cleared and a hold key each at clock 0 on the RTS camera's pose.
    void start(const ScriptPose& rts, uint32_t frame);
    void end();  // End_Cinematic_Camera: the keys go, the RTS camera is the view again
    void set_key(bool camera, const ScriptKeyCall& k, uint32_t frame);
    void transition_key(bool camera, const ScriptKeyCall& k, double seconds, uint32_t frame);
    // Transition_To_Tactical_Camera: from the pose at `frame` to `rts` over `seconds` (the shot clock restarts; the camera
    // stays cinematic until end()); at least one frame.
    void to_tactical(const ScriptPose& rts, double seconds, uint32_t frame, const Resolver& r);
    // Cinematic_Zoom(t, scale): the eye moves along its line to the target to `scale` x the current distance over `t` s.
    void zoom(double seconds, double scale, uint32_t frame, const Resolver& r);
    // The pose at mode frame `frame`.
    ScriptPose pose(uint32_t frame, const Resolver& r) const;
    int32_t clock(uint32_t frame) const { return frame >= start_frame_ ? static_cast<int32_t>(frame - start_frame_) : 0; }
    size_t key_count(bool camera) const { return (camera ? eye_ : look_).size(); }

private:
    struct Key {
        int32_t clock = 0;
        bool blend = false;  // false: hold (a cut at the next key)
        bool attached = false;
        int32_t object = -1;
        tactical::Vec3 base, offset;
        bool use_rotation = false;
        float ease_out = 0.5f, ease_in = 0.5f;
    };
    static tactical::Vec3 offset_of(const ScriptKeyCall& k);
    tactical::Vec3 position(const Key& k, const Resolver& r, int track) const;
    tactical::Vec3 evaluate(const std::vector<Key>& keys, int track, int32_t clock, const Resolver& r) const;
    static void insert(std::vector<Key>& keys, const Key& k);
    static Key key_from(const ScriptKeyCall& k, int32_t clock);

    bool active_ = false;
    uint32_t start_frame_ = 0;
    std::vector<Key> eye_, look_;
    mutable tactical::Vec3 last_good_[2];  // per track: the last position an attached key resolved to (the object is gone)
    tactical::Vec3 last_key_[2];           // per track: the "last key position" later Transition keys start from
};

// The script fade (STORY_PRESENTATION_LUA.md §1.2) on the render clock (seconds): black only, linear.
class ScreenFade {
public:
    void fade_out(double seconds, double now) { begin(Mode::Out, seconds, now); }  // clear -> black over `seconds`; stays black
    void fade_in(double seconds, double now) { begin(Mode::In, seconds, now); }    // snaps to black, then clears over `seconds`
    void on() { mode_ = Mode::Black; }
    void off() { mode_ = Mode::Clear; }
    void clear() { mode_ = Mode::Clear; }  // the mode ended (§1.5)
    float alpha(double now) const;         // 0 clear .. 1 black
    bool active() const { return mode_ != Mode::Clear; }

private:
    enum class Mode : uint8_t { Clear, Out, In, Black };
    void begin(Mode m, double seconds, double now);
    Mode mode_ = Mode::Clear;
    double start_ = 0.0, seconds_ = 0.0;
};

// The script letterbox (§1.4): bars of Letterbox_Height x level; the first bar-in hides the HUD until the bars are gone.
class ScreenLetterbox {
public:
    void bar_in(double seconds, double now);
    void bar_out(double seconds, double now);
    void on();
    void off();
    void clear() { off(); }
    float level(double now) const;
    bool hud_hidden() const { return hud_hidden_; }
    void update(double now);  // finishes a bar-out: the HUD shows again

private:
    float from_ = 0.0f, to_ = 0.0f;
    double t0_ = 0.0, seconds_ = 0.0;
    bool hud_hidden_ = false;
};

}  // namespace alamo::game
