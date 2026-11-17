// alamo_game App: the story script's screen and camera calls (ui2 t03 / t04 / t05; docs/re/STORY_PRESENTATION_LUA.md):
// Fade_Screen_In / _Out and Fade_On / _Off, Letter_Box_*, the cinematic camera (Start / End_Cinematic_Camera, the key
// calls, Transition_To_Tactical_Camera, Cinematic_Zoom), Start / End_Cinematic_Mode, and the RTS camera moves
// (Point_Camera_At, Scroll_Camera_To, Camera_To_Follow, Rotate_Camera_By / _To, Zoom_Camera) plus the camera commands of
// the story rewards (POSITION_CAMERA, SCROLL_CAMERA, ZOOM_IN / _OUT, SELECT_PLANET, the pause rewards).
//
// Presentation only (rule 3): the Lua presentation log (lua_presentation) and the presenter's queue are the input; the fade,
// bars and camera are drawn state, never sim state. Fades, bars and rotation run on the render clock (now_s_, §1.2 / §3),
// the camera keys on the tactical frame count (§2.3, 30 per second, a pause stops them).
#include "game_app.hpp"

namespace alamo_game_app {

namespace {

// Play_Lightning_Effect's block length in battle frames (the Lua config default, PresentationConfig::lightning_frames): the bolts show that long.
constexpr uint32_t kStoryLightningFrames = 30;

// The trailing "@pos", x, y, z of a camera event (bind_presentation.cpp append_position), false when the call had none.
bool resolved_position(const lua::PresentationEvent& e, ui::Vec3& out) {
    const size_t n = e.args.size();
    if (n < 4) return false;
    const lua::PresentationArg& tag = e.args[n - 4];
    if (tag.kind != lua::PresentationArg::Kind::String || tag.text != "@pos") return false;
    out = {static_cast<float>(e.args[n - 3].number), static_cast<float>(e.args[n - 2].number), static_cast<float>(e.args[n - 1].number)};
    return true;
}

bool truthy(const lua::PresentationArg& a) {
    if (a.kind == lua::PresentationArg::Kind::Bool) return a.b;
    return a.kind == lua::PresentationArg::Kind::Number && a.number > 0.0;
}

double number_or(const lua::PresentationEvent& e, size_t i, double fallback) {
    return i < e.args.size() && e.args[i].kind == lua::PresentationArg::Kind::Number ? e.args[i].number : fallback;
}

// Set_Cinematic_*_Key (8 args: pos, x, y, z, euler, attach, rot, anim) or Transition_* (9: pos, time, ...).
bool key_call(const lua::PresentationEvent& e, bool transition, ScriptKeyCall& k, double& seconds) {
    const size_t o = transition ? 1 : 0;  // the time shifts every later argument
    if (e.args.size() < 8 + o) return false;
    ui::Vec3 base;
    if (!resolved_position(e, base)) return false;
    k.base = base;
    k.a = static_cast<float>(number_or(e, 1 + o, 0.0));
    k.b = static_cast<float>(number_or(e, 2 + o, 0.0));
    k.c = static_cast<float>(number_or(e, 3 + o, 0.0));
    k.euler = truthy(e.args[4 + o]);
    k.object = e.args[5 + o].kind == lua::PresentationArg::Kind::Object ? e.args[5 + o].id : -1;  // a number is "no object"
    k.use_rotation = truthy(e.args[6 + o]);
    k.animation = truthy(e.args[7 + o]);
    seconds = transition ? number_or(e, 1, 0.0) : 0.0;
    return true;
}

}  // namespace

uint32_t App::story_frame() const {
    const sim::TacticalBattle* b = battle();
    return b ? static_cast<uint32_t>(b->frame()) : 0u;
}

// The attached object's drawn frame (§2.4): the snapshot's transform, z raised by the type's Ranged_Target_Z_Adjust (default
// 6.0) for a model object.
bool App::story_object_frame(int32_t id, ObjectFrame& out) const {
    if (!bview_.active()) return false;
    const std::vector<render::SnapshotObject>& objs = bview_.frame().objects;  // ascending id
    const auto it = std::lower_bound(objs.begin(), objs.end(), static_cast<objects::ObjectId>(id),
                                     [](const render::SnapshotObject& o, objects::ObjectId v) { return o.id < v; });
    if (it == objs.end() || static_cast<int32_t>(it->id) != id) return false;
    const assets::Mat3x4 m = render::object_transform(*it, 1.0f);
    out.valid = true;
    out.position = {m.m[0][3], m.m[1][3], m.m[2][3]};
    for (int j = 0; j < 3; ++j) out.axis[j] = {m.m[0][j], m.m[1][j], m.m[2][j]};
    static const uint32_t f = objects::find_type_field("Ranged_Target_Z_Adjust");
    const objects::FieldValue* v = f != 0xFFFFFFFFu ? data_.types.value(it->type, f) : nullptr;
    out.position.z += v ? v->f : 6.0f;
    return true;
}

ScriptPose App::story_rts_pose() const {
    const ui::CameraParams cp = bview_.camera_params(targets_.width ? targets_.width : 1280, targets_.height ? targets_.height : 720);
    return {cp.eye, cp.target};
}

// The cinematic camera over the RTS params (called with the other cinematic_camera sources).
bool App::story_camera_view(ui::CameraParams& cp) const {
    if (!story_cam_.active() || !bview_.active()) return false;
    const ScriptCamera::Resolver r = [this](int32_t id, ObjectFrame& f) { return story_object_frame(id, f); };
    const ScriptPose p = story_cam_.pose(story_frame(), r);
    cp.eye = p.eye;
    cp.target = p.target;
    cp.up = {0.0f, 0.0f, 1.0f};
    cp.roll = 0.0f;
    const ui::Vec3 d = p.eye - p.target;
    const float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (dist > 0.0f) {
        cp.distance = dist;
        cp.orbit_yaw = std::atan2(d.y, d.x);
        cp.orbit_pitch = std::asin(std::clamp(d.z / dist, -1.0f, 1.0f));
    }
    cp.zfar = std::max(cp.zfar, 10000.0f);
    return true;
}

bool App::story_hides_hud() const {
    return story_cam_.active() || story_box_.hud_hidden() || story_present_.cinematic_mode() || story_lua_mode_;
}

// One Lua presentation event (lua_presentation); the camera and screen ops only.
void App::story_script_event(const lua::PresentationEvent& e) {
    using Op = lua::PresentationOp;
    const bool tactical = screen_ == Screen::Battle && bview_.active();
    const uint32_t frame = story_frame();
    const ScriptCamera::Resolver r = [this](int32_t id, ObjectFrame& f) { return story_object_frame(id, f); };
    switch (e.op) {
        case Op::PlayLightningEffect: {  // 4.2: the effect's name, then the end (param 2) and the start (param 3) point at the call
            std::vector<ui::Vec3> pts;
            for (size_t i = 0; i + 3 < e.args.size(); ++i)
                if (e.args[i].kind == lua::PresentationArg::Kind::String && e.args[i].text == "@pos") {
                    pts.push_back({static_cast<float>(e.args[i + 1].number), static_cast<float>(e.args[i + 2].number), static_cast<float>(e.args[i + 3].number)});
                    i += 3;
                }
            if (pts.size() == 2 && !e.args.empty() && battle()) {
                const uint32_t f = story_frame();
                story_lightning_.push_back({e.args[0].text, pts[1], pts[0], f, f + kStoryLightningFrames});
            }
            break;
        }
        case Op::EnableFog:
            if (!e.args.empty()) story_fog_master_ = e.args[0].b;
            break;
        case Op::EnableDistanceFog:
            if (!e.args.empty()) story_fog_distance_ = e.args[0].b;
            break;
        case Op::FadeScreenOut: story_fade_.fade_out(number_or(e, 0, 0.5), now_s_); break;
        case Op::FadeScreenIn: story_fade_.fade_in(number_or(e, 0, 0.5), now_s_); break;
        case Op::FadeOn: story_fade_.on(); break;
        case Op::FadeOff: story_fade_.off(); break;
        case Op::LetterBoxIn: story_box_.bar_in(number_or(e, 0, 0.5), now_s_); break;
        case Op::LetterBoxOut: story_box_.bar_out(number_or(e, 0, 0.5), now_s_); break;
        case Op::LetterBoxOn: story_box_.on(); break;
        case Op::LetterBoxOff: story_box_.off(); break;
        case Op::StartCinematicMode: story_lua_mode_ = true; break;
        case Op::EndCinematicMode:  // §2.1: the bars go with it, and the controller ends
            story_lua_mode_ = false;
            story_box_.off();
            break;
        case Op::StartCinematicCamera:
            if (tactical) story_cam_.start(story_rts_pose(), frame);
            break;
        case Op::EndCinematicCamera: story_cam_.end(); break;
        case Op::SetCinematicCameraKey:
        case Op::SetCinematicTargetKey:
        case Op::TransitionCinematicCameraKey:
        case Op::TransitionCinematicTargetKey: {
            ScriptKeyCall k;
            double seconds = 0.0;
            const bool transition = e.op == Op::TransitionCinematicCameraKey || e.op == Op::TransitionCinematicTargetKey;
            const bool camera = e.op == Op::SetCinematicCameraKey || e.op == Op::TransitionCinematicCameraKey;
            if (!story_cam_.active() || !key_call(e, transition, k, seconds)) break;
            if (transition) story_cam_.transition_key(camera, k, seconds, frame);
            else story_cam_.set_key(camera, k, frame);
            break;
        }
        case Op::TransitionToTacticalCamera:
            if (story_cam_.active()) story_cam_.to_tactical(story_rts_pose(), number_or(e, 0, 1.0), frame, r);
            break;
        case Op::CinematicZoom:
            if (story_cam_.active()) story_cam_.zoom(number_or(e, 0, 1.0), number_or(e, 1, 1.0), frame, r);
            break;
        case Op::PointCameraAt: {  // instant: the RTS focus xy (z ignored)
            ui::Vec3 p;
            if (tactical && resolved_position(e, p)) {
                story_moves_.scroll = false;
                bview_.camera().jump_to({p.x, p.y, bview_.camera().look_at().z});
            }
            break;
        }
        case Op::ScrollCameraTo: {  // 10 world units per rendered frame towards the target, then follows an object
            ui::Vec3 p;
            if (tactical && resolved_position(e, p)) {
                story_moves_.scroll = true;
                story_moves_.target = p;
                story_moves_.follow_on_arrival = e.args.size() > 1 && e.args[1].kind == lua::PresentationArg::Kind::Object;
                story_moves_.follow_object = story_moves_.follow_on_arrival ? e.args[1].id : -1;
            }
            break;
        }
        case Op::CameraToFollow:
            story_moves_.follow_object = !e.args.empty() && e.args[0].kind == lua::PresentationArg::Kind::Object ? e.args[0].id : -1;
            break;
        case Op::RotateCameraBy:
        case Op::RotateCameraTo: {
            if (!tactical) break;
            const float yaw = bview_.camera().yaw_deg();
            float delta = static_cast<float>(number_or(e, 0, 0.0));
            if (e.op == Op::RotateCameraTo) {  // the way round: the shorter path unless dir picks the long one [M]
                const float to = std::fmod(std::fmod(std::clamp(delta, -360.0f, 360.0f), 360.0f) + 360.0f, 360.0f);
                float d = std::fmod(to - std::fmod(yaw + 360.0f, 360.0f), 360.0f);
                if (d > 180.0f) d -= 360.0f;
                if (d < -180.0f) d += 360.0f;
                delta = d;
            }
            story_moves_.rotate = true;
            story_moves_.yaw0 = yaw;
            story_moves_.dyaw = delta;
            story_moves_.t0 = now_s_;
            story_moves_.duration = std::max(0.0, number_or(e, 1, 0.0));
            break;
        }
        case Op::ZoomCamera: {  // f in [0, 1] between the camera's two distance limits; snap sets it at once
            if (!tactical) break;
            const float f = static_cast<float>(std::clamp(number_or(e, 0, 0.0), 0.0, 1.0));
            const ui::TacticalCameraConstants& c = bview_.camera().constants();
            const float d = c.distance_min + (c.distance_max - c.distance_min) * f;
            if (e.args.size() > 1 && truthy(e.args[1])) bview_.camera().set_distance(d);
            else if (c.distance_per_mouse_unit > 0.0f) bview_.camera().zoom((bview_.camera().target_distance() - d) / c.distance_per_mouse_unit);
            break;
        }
        default: break;
    }
}

// Per rendered frame: the bars' end, the RTS camera moves, a mode change clearing the fade / bars / camera (§1.5), the
// presenter's camera commands.
// A fade or bars left on at the end of a mode do not carry into the next (galactic <-> battle, battle -> battle). Called before the
// frame's sim step and Lua presentation, so what the new mode's first ticks set is not wiped.
void App::story_script_mode_check() {
    if (screen_ != story_fx_screen_ || battles_seen_ != story_fx_battle_) {
        story_fx_screen_ = screen_;
        story_fx_battle_ = battles_seen_;
        story_fade_.clear();
        story_box_.clear();
        story_cam_.end();
        story_moves_ = {};
        story_lua_mode_ = false;
        story_lightning_.clear();  // ui2 t08: the effects and fog switches belong to the mode
        story_fog_master_ = story_fog_distance_ = true;
    }
}

void App::story_script_frame() {
    story_box_.update(now_s_);
    if (screen_ == Screen::Battle && bview_.active()) {
        ui::TacticalCameraController& cam = bview_.camera();
        if (story_moves_.scroll) {
            const ui::Vec3 look = cam.look_at();
            ui::Vec3 d = story_moves_.target - look;
            const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (len < 10.0f) {
                cam.jump_to({story_moves_.target.x, story_moves_.target.y, look.z});
                story_moves_.scroll = false;
                if (story_moves_.follow_on_arrival) story_moves_.following = true;
            } else {
                cam.jump_to({look.x + d.x / len * 10.0f, look.y + d.y / len * 10.0f, look.z});  // the xy part of 10 units along 3D
            }
        }
        if (story_moves_.following && story_moves_.follow_object >= 0) {
            ObjectFrame f;
            if (story_object_frame(story_moves_.follow_object, f)) cam.jump_to({f.position.x, f.position.y, cam.look_at().z});
            else story_moves_.following = false;
        }
        if (story_moves_.rotate) {
            const double t = story_moves_.duration > 0.0 ? std::clamp((now_s_ - story_moves_.t0) / story_moves_.duration, 0.0, 1.0) : 1.0;
            float yaw = story_moves_.yaw0 + story_moves_.dyaw * static_cast<float>(t);
            yaw = std::fmod(std::fmod(yaw, 360.0f) + 360.0f, 360.0f);
            cam.set_yaw_deg(yaw);
            if (t >= 1.0) story_moves_.rotate = false;
        }
    }
    story_camera_commands();
    if (screen_ == Screen::Battle && bview_.active()) {  // ui2 t08: the script's lightning bolts and fog switches go to the scene
        const uint32_t now = story_frame();
        std::erase_if(story_lightning_, [now](const StoryLightning& l) { return now >= l.to; });
        std::vector<render::BeamInstance> beams;
        uint64_t key = 0x5C170000u;
        for (const StoryLightning& l : story_lightning_) {
            render::BeamInstance b;
            b.key = key++;
            b.kind = render::kBeamEnergyWeapon;
            b.start = {l.start.x, l.start.y, l.start.z};
            b.end = {l.end.x, l.end.y, l.end.z};
            b.style.texture.clear();  // bolts only: no ribbon, no dots
            b.style.width = 0.0f;
            b.style.dots = 0;
            b.style.lightning = l.name;
            b.continuous = true;
            beams.push_back(std::move(b));
        }
        bview_.set_script_beams(std::move(beams));
        bview_.set_fog_switches(story_fog_master_, story_fog_distance_);
    }
}

// The camera commands of the rewards (STORY_PRESENTATION_REWARDS.md §5): POSITION_CAMERA / SCROLL_CAMERA at an object type's
// first instance + an offset, ZOOM_IN / ZOOM_OUT / SELECT_PLANET on the galactic map, the pause rewards.
void App::story_camera_commands() {
    for (const StoryCameraCommand& c : story_present_.take_camera_commands()) {
        using K = StoryCameraCommand::Kind;
        const bool gal = screen_ == Screen::Galactic;
        ui::Vec3 p;
        switch (c.kind) {
            case K::Position:
            case K::Scroll:
                if (!c.type.empty() && story_type_position(c.type, p)) {
                    const ui::Vec3 at{p.x + c.ox, p.y + c.oy, p.z + c.oz};
                    if (gal) gview_.camera().set_look_at({at.x, at.y, 0.0f});
                    else if (screen_ == Screen::Battle && bview_.active()) bview_.camera().jump_to({at.x, at.y, bview_.camera().look_at().z});
                }
                break;
            case K::ZoomIn:
                if (gal && c.type.empty()) {  // 5.13: every shipped ZOOM_IN has no P1: the player's own zoom, into the selected planet
                    if (selected_planet_ != 0 && !gview_.camera().zoomed())
                        for (const ui::PickSphere& s : gview_.pick_items())
                            if (s.kind == ui::PickKind::Planet && s.id == selected_planet_) {
                                gview_.camera().set_look_at({s.center.x, s.center.y, 0.0f});
                                gview_.camera().zoom_to_planet(s.center, s.radius);
                                zoom_planet_ = s.id;
                            }
                    break;
                }
                [[fallthrough]];
            case K::SelectPlanet:
                if (gal && !c.type.empty() && story_type_position(c.type, p)) {
                    const galactic::GalacticSim* g = galaxy();
                    if (!g) break;
                    for (const galactic::PlanetState& ps : g->planets())
                        if (ps.type == data_.rules.find_type(c.type)) {
                            const uint32_t id = static_cast<uint32_t>(g->store().id(ps.object));
                            if (c.kind == K::SelectPlanet) {
                                selected_planet_ = id;
                                orders_.set_selection({});
                            } else {
                                for (const ui::PickSphere& s : gview_.pick_items())
                                    if (s.kind == ui::PickKind::Planet && s.id == id) {
                                        gview_.camera().set_look_at({s.center.x, s.center.y, 0.0f});
                                        gview_.camera().zoom_to_planet(s.center, s.radius);
                                        zoom_planet_ = id;
                                    }
                            }
                            break;
                        }
                }
                break;
            case K::ZoomOut:
                if (gal && gview_.camera().zoomed()) gview_.camera().zoom_out();
                break;
            case K::PauseGame:
            case K::PauseGalactic:
                if (session_ && !session_->pause_requested()) toggle_pause();
                break;
            case K::UnpauseGalactic:
                if (session_ && session_->pause_requested()) toggle_pause();
                break;
        }
    }
}

// The fade quad over everything but the cursor (§1.3), on the top layer of the dialogs list.
void App::build_story_fade(ui::HudDrawList& dl, const ui::HudCanvas& canvas, uint32_t w, uint32_t h) const {
    const float a = story_fade_.alpha(now_s_);
    if (!(a > 0.0f)) return;
    ui::HudQuad q;
    q.min = canvas.pixel_to_canvas(0.0f, static_cast<float>(h));
    q.max = canvas.pixel_to_canvas(static_cast<float>(w), 0.0f);
    q.color = {0, 0, 0, static_cast<uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f)};
    dl.add(ui::kHudLayers - 1, ui::HudList::Alpha, q);
}

}  // namespace alamo_game_app
