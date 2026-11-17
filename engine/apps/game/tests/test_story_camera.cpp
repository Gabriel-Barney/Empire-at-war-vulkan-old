// ui2 t03 / t04 / t05: the story script's cinematic camera, fade and letterbox models (story_camera.hpp), data-free.
#include <cmath>
#include <cstdio>

#include "alamo/game/story_camera.hpp"
#include "alamo/test/support.hpp"

using namespace alamo::game;
using alamo::tactical::Vec3;

namespace {

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool near(const Vec3& a, const Vec3& b, float eps = 1e-3f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }

void fade_test() {
    alamo::test::section("fade: black only, linear, render clock (STORY_PRESENTATION_LUA.md 1.2)");
    ScreenFade f;
    CHECK(!f.active() && f.alpha(0.0) == 0.0f);
    f.fade_out(2.0, 10.0);  // always starts from clear, ends and stays black
    CHECK(near(f.alpha(10.0), 0.0f) && near(f.alpha(11.0), 0.5f) && near(f.alpha(12.0), 1.0f) && near(f.alpha(99.0), 1.0f));
    f.fade_in(1.0, 20.0);  // always starts from black, ends clear
    CHECK(near(f.alpha(20.0), 1.0f) && near(f.alpha(20.5), 0.5f) && near(f.alpha(21.0), 0.0f) && near(f.alpha(30.0), 0.0f));
    f.fade_in(1.0, 40.0);  // a fade-in with no fade-out before it blinks to black and fades up
    CHECK(near(f.alpha(40.0), 1.0f));
    f.fade_out(1.0, 50.0);  // a fade-out right after restarts from clear, not from the current alpha
    CHECK(near(f.alpha(50.0), 0.0f) && near(f.alpha(51.0), 1.0f));
    f.fade_out(0.0, 60.0);  // t = 0: black at once
    CHECK(near(f.alpha(60.0), 1.0f));
    f.off();
    CHECK(near(f.alpha(61.0), 0.0f));
    f.on();
    CHECK(near(f.alpha(61.0), 1.0f));
    f.clear();  // a mode end clears it (1.5)
    CHECK(!f.active() && near(f.alpha(62.0), 0.0f));
}

void letterbox_test() {
    alamo::test::section("letterbox: bar-in hides the HUD until the bars are out (1.4)");
    ScreenLetterbox l;
    CHECK(!l.hud_hidden() && near(l.level(0.0), 0.0f));
    l.bar_out(1.0, 0.0);  // no bars: nothing
    CHECK(near(l.level(0.5), 0.0f));
    l.bar_in(2.0, 10.0);
    CHECK(l.hud_hidden() && near(l.level(10.0), 0.0f) && near(l.level(11.0), 0.5f) && near(l.level(12.0), 1.0f));
    l.bar_in(5.0, 11.0);  // while animating in: nothing
    CHECK(near(l.level(12.0), 1.0f));
    l.bar_out(2.0, 20.0);  // from settled: the time as given
    CHECK(near(l.level(21.0), 0.5f) && near(l.level(22.0), 0.0f));
    l.update(21.0);
    CHECK(l.hud_hidden());
    l.update(22.5);
    CHECK(!l.hud_hidden());
    l.on();
    CHECK(l.hud_hidden() && near(l.level(0.0), 1.0f));
    l.off();
    CHECK(!l.hud_hidden() && near(l.level(0.0), 0.0f));
    l.bar_in(0.0, 5.0);  // t = 0: full bars at once
    CHECK(near(l.level(5.0), 1.0f));
    l.bar_out(2.0, 6.0);  // mid-animation reversal scales the time by the fraction showing
    l.bar_in(2.0, 6.5);   // back in from 0.75
    CHECK(near(l.level(6.5), 0.75f) && near(l.level(6.5 + 0.5), 1.0f));
}

ScriptKeyCall call(Vec3 base, float a = 0, float b = 0, float c = 0, bool euler = false, int32_t object = -1, bool rot = false) {
    ScriptKeyCall k;
    k.base = base;
    k.a = a;
    k.b = b;
    k.c = c;
    k.euler = euler;
    k.object = object;
    k.use_rotation = rot;
    return k;
}

void camera_test() {
    alamo::test::section("camera: hold keys cut, transition keys blend with ease 0.5 / 0.5 (2.3, 2.5)");
    ScriptCamera cam;
    const ScriptCamera::Resolver none;
    CHECK(!cam.active());
    cam.set_key(true, call({1, 2, 3}), 100);  // not started: ignored
    CHECK(cam.key_count(true) == 0);
    cam.start({{0, -100, 50}, {0, 0, 0}}, 100);
    CHECK(cam.active() && cam.key_count(true) == 1 && cam.key_count(false) == 1);
    ScriptPose p = cam.pose(100, none);
    CHECK(near(p.eye, {0, -100, 50}) && near(p.target, {0, 0, 0}));
    cam.set_key(true, call({10, 20, 30}, 1, 2, 3), 130);  // a cut at clock 30: base + offset
    p = cam.pose(129, none);
    CHECK(near(p.eye, {0, -100, 50}));  // the hold key of Start until the next key's frame
    p = cam.pose(130, none);
    CHECK(near(p.eye, {11, 22, 33}));
    cam.transition_key(true, call({0, 0, 0}, 100, 0, 0), 2.0, 160);  // 60 frames: from the last key (11,22,33) to (100,0,0)
    CHECK(near(cam.pose(160, none).eye, {11, 22, 33}));
    const Vec3 mid = cam.pose(190, none).eye;  // sigma 0.5: eased 0.5
    CHECK(near(mid, {55.5f, 11.0f, 16.5f}));
    const Vec3 q = cam.pose(175, none).eye;  // sigma 0.25: 2 x 0.25^2 = 0.125
    CHECK(near(q.x, 11.0f + 0.125f * 89.0f, 1e-2f));
    CHECK(near(cam.pose(220, none).eye, {100, 0, 0}) && near(cam.pose(500, none).eye, {100, 0, 0}));  // holds after the end key
    cam.transition_key(false, call({5, 5, 5}), 0.0, 230);  // an instant: a hold key
    CHECK(near(cam.pose(230, none).target, {5, 5, 5}));
    cam.end();
    CHECK(!cam.active() && cam.key_count(true) == 0);

    alamo::test::section("camera: spherical offsets (arg 5) and attached keys follow the object");
    cam.start({{0, 0, 0}, {0, 0, 0}}, 0);
    cam.set_key(true, call({0, 0, 0}, 10, 0, 0, true), 0);  // distance 10, pitch 0, yaw 0: 10 behind (-y)
    CHECK(near(cam.pose(0, none).eye, {0, -10, 0}));
    cam.set_key(true, call({0, 0, 0}, 10, 90, 0, true), 0);  // pitch 90: straight above
    CHECK(near(cam.pose(0, none).eye, {0, 0, 10}, 1e-2f));
    cam.set_key(true, call({0, 0, 0}, 10, 0, 90, true), 0);  // yaw 90 about z
    CHECK(near(cam.pose(0, none).eye, {10, 0, 0}, 1e-2f));
    Vec3 where{100, 200, 0};
    ScriptCamera::Resolver follow = [&](int32_t object, ObjectFrame& f) {
        if (object != 7) return false;
        f.valid = true;
        f.position = where;
        return true;
    };
    cam.set_key(false, call({0, 0, 0}, 0, 0, 7, false, 7), 0);  // attached, offset z 7: look 7 above the unit
    CHECK(near(cam.pose(1, follow).target, {100, 200, 7}));
    where = {150, 250, 0};  // the object moved: the key follows it every frame
    CHECK(near(cam.pose(2, follow).target, {150, 250, 7}));
    const ScriptCamera::Resolver gone = [](int32_t, ObjectFrame&) { return false; };
    CHECK(near(cam.pose(3, gone).target, {150, 250, 7}));  // gone: the last good position
    ScriptCamera cam2;
    cam2.start({{0, 0, 0}, {0, 0, 0}}, 0);
    cam2.set_key(true, call({0, 0, 0}, 5, 0, 0, false, 7, true), 0);  // rotating frame: the offset turns with the object
    ScriptCamera::Resolver turned = [](int32_t, ObjectFrame& f) {
        f.valid = true;
        f.position = {10, 10, 0};
        f.axis[0] = {0, 1, 0};  // model +X is world +Y
        f.axis[1] = {-1, 0, 0};
        return true;
    };
    CHECK(near(cam2.pose(1, turned).eye, {10, 15, 0}));

    alamo::test::section("camera: Transition_To_Tactical_Camera, Cinematic_Zoom (2.6, 3)");
    ScriptCamera c3;
    c3.start({{0, -100, 0}, {0, 0, 0}}, 0);
    c3.to_tactical({{0, -200, 100}, {0, 0, 10}}, 2.0, 30, none);  // blend 0.25 / 0.25 over 60 frames from the pose at frame 30
    CHECK(near(c3.pose(30, none).eye, {0, -100, 0}) && c3.active());
    CHECK(near(c3.pose(90, none).eye, {0, -200, 100}) && near(c3.pose(90, none).target, {0, 0, 10}));  // clock 60: the end
    CHECK(near(c3.pose(200, none).eye, {0, -200, 100}));  // holds; the cinematic stays on until End
    const float w = shot_accel(0.5f, 0.25f, 0.25f);
    CHECK(near(c3.pose(60, none).eye.y, -100.0f + w * -100.0f, 1e-2f));
    ScriptCamera c4;
    c4.start({{0, -100, 0}, {0, 0, 0}}, 0);
    c4.zoom(1.0, 0.5, 10, none);  // half the distance in 30 frames, the target stays
    CHECK(near(c4.pose(10, none).eye, {0, -100, 0}) && near(c4.pose(40, none).eye, {0, -50, 0}) && near(c4.pose(40, none).target, {0, 0, 0}));
}

}  // namespace

int main() {
    fade_test();
    letterbox_test();
    camera_test();
    return alamo::test::report("story_camera");
}
