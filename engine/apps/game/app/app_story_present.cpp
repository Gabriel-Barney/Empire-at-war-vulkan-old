// alamo_game App: the story presentation rewards (ui2; docs/re/STORY_PRESENTATION_REWARDS.md). The fired rewards of the
// local player's plots feed game::StoryPresenter (story_present.hpp, data-free and tested without a window); this file
// wires its state to the host: the text column (SCREEN_TEXT / MULTIMEDIA lines on the tutorial_text / tutorial_text_back
// look), the advisor hint box, and what the other tasks of the group read (locks, camera commands, pointers).
//
// Presentation only (rule 3): nothing here writes sim state. The presenter's clock is now_s_ (wall time).
#include "game_app.hpp"

namespace alamo_game_app {

namespace {
// tutorial_text_back (COMMANDBARCOMPONENTS.XML): i_tooltip_back.tga, colour 255 255 255 220, base layer 8; tutorial_text:
// EmpireAtWar-Medium 6 pt, outline, left justified, 11 units a line. The block's width is the back component's.
constexpr float kBackWidth = 512.0f, kLineStep = 11.0f, kTextInset = 9.0f;
constexpr uint32_t kTextLayer = 9, kBackLayer = 8;
// [port] the column's origin (HUD +0x10dc / +0x10e0 depends on the HUD layout, not traced): top-left, under the top bar.
constexpr float kColumnLeft = 12.0f, kColumnTop = 72.0f;
}  // namespace

// The presenter's hooks (text ids, type display names) and the per-session reset.
void App::story_present_setup() {
    StoryPresenter::Hooks h;
    h.text = [this](std::string_view id) -> std::u32string {
        if (const std::u16string* s = ui_.text.find(id)) return ui::utf16_to_utf32(*s);
        return {};
    };
    StoryPresenter::Config& cfg = story_present_.config();  // ui2 t07: Advisor_Hint_Interval / Advisor_Hint_Duration
    load_story_advisor_config(data_.db.constants(), cfg);
    advisor_mode_ = 255;
    h.type_name = [this](std::string_view type) -> std::u32string {
        const uint32_t t = data_.rules.find_type(type);
        if (t == 0xFFFFFFFFu) return {};
        return type_label(data_, ui_.text, static_cast<objects::TypeIndex>(t));
    };
    story_present_.set_hooks(std::move(h));
}

// Feed one reward and the clocks; called by story_reward_step for every reward of a local plot set.
void App::story_present_reward(const lua::StoryFired& f, uint8_t mode) {
    std::vector<std::string> params;
    params.reserve(f.reward_params.size());
    for (const lua::StoryParam& p : f.reward_params) params.push_back(p.text);
    story_present_.on_reward(f.reward_type, params, now_s_, mode);
}

// Lua calls that reach the presenter (lua_presentation).
void App::story_present_lua(const lua::PresentationEvent& e) {
    using Op = lua::PresentationOp;
    if (e.op == Op::GameMessage && !e.args.empty()) {
        story_present_.game_message(e.args.front().text);
    } else if (e.op == Op::RemoveAllText) {
        story_present_.remove_all_text();
    } else if ((e.op == Op::ObjectHighlight || e.op == Op::ObjectHighlightSmall) && e.args.size() >= 3) {
        story_present_.lua_highlight(static_cast<int32_t>(e.args[0].id), e.args[1].b, e.op == Op::ObjectHighlightSmall,
                                     static_cast<float>(e.args[2].number));
    }
}

// 6.4 / 6.5: the local player's periodic hint text (Galactic / Space / Land_Advisor_Hints of the faction), the first one 90
// ticks after the mode starts, then every Advisor_Hint_Interval seconds; the hint sound per queue entry (6.6). Retail runs it per
// sim tick of the mode: a battle uses its frame (a pause stops it), the galactic mode the render clock since it began [port].
void App::story_advisor_frame() {
    if (!session_ || bench_active() || demo_session()) return;
    uint8_t mode = 255;
    uint32_t tick = 0;
    if (const sim::TacticalBattle* b = battle(); b && screen_ == Screen::Battle) {
        mode = bview_.space() ? 1 : 2;
        tick = static_cast<uint32_t>(b->frame());
    } else if (galaxy() && screen_ == Screen::Galactic) {
        mode = 0;
        tick = static_cast<uint32_t>(std::max(0.0, (now_s_ - advisor_t0_) * static_cast<double>(StoryPresenter::kTicksPerSecond)));
    }
    if (mode == 255) {
        advisor_mode_ = 255;
        return;
    }
    if (mode != advisor_mode_ || (mode != 0 && tick < advisor_last_tick_)) {  // a new mode (or a new battle): the first hint is due again
        advisor_mode_ = mode;
        advisor_t0_ = now_s_;
        tick = 0;
        story_present_.begin_mode(mode);
        advisor_hints_ = faction_advisor_hints(data_.db, local_faction(), mode);
    }
    advisor_last_tick_ = tick;
    story_present_.tick_advisor(mode, tick, advisor_hints_);
}

void App::story_present_frame() {
    // Retail shows the next hint only with no Bink movie open, no advanced map and no cinematic mode (§6.3).
    const bool blocked = full_movie_.playing() || planet_map_open_ || story_present_.cinematic_mode();
    story_present_.update(now_s_, blocked);
    // ACTIVATE_RETRY_DIALOG as a reward (5.4): the same IDD_RETRY_DIALOG as the Lua call. USE_RETRY_DIALOG only marks the end-of-battle
    // choice (retail reads it in the battle-end flow, which the port's defeat path already routes to this dialog); HIDE_AUTORESOLVE
    // has no reader in retail (5.3), so the flag is kept and nothing reads it.
    if (story_present_.take_retry_request() && screens_.show_retry()) ++retry_boxes_;
    story_advisor_frame();
    if (story_present_.hint_started()) {  // 6.6: Message for the back of the queue, Urgent for a front insert, Hint for the periodic text
        const StoryShownHint* sh = story_present_.shown_hint();
        const StoryHint::Sound snd = sh ? sh->hint.sound : StoryHint::Sound::Message;
        audio_.faction_sfx(snd == StoryHint::Sound::Urgent ? "SFXEvent_HUD_Advisor_Urgent_Message"
                           : snd == StoryHint::Sound::Hint ? "SFXEvent_HUD_Advisor_Hint"
                                                           : "SFXEvent_HUD_Advisor_Message");
    }
}

// The text column (§1.1): lines from the top-left, the typewriter reveal, one back plate around the block.
void App::build_story_text(ui::HudDrawList& dl, const ui::HudCanvas& canvas) {
    const auto& lines = story_present_.lines(now_s_);
    if (lines.empty() || !ui_.fonts) return;
    const int32_t font = ui_.fonts->font("EmpireAtWar-Medium", 6.0f, canvas.font_height());
    if (font < 0) return;
    const float wrap_px = ui::wrap_width_px(canvas, kBackWidth - kTextInset);
    struct Row {
        std::u32string text;
        ui::Rgba8 color;
    };
    std::vector<Row> rows;
    for (const StoryTextLine& l : lines) {
        const std::vector<std::u32string> wrapped = ui::wrap_text(*ui_.fonts, font, l.text, wrap_px);
        const size_t revealed = l.typewriter ? l.revealed(now_s_) : l.text.size();
        size_t pos = 0;
        for (const std::u32string& w : wrapped) {
            // a continuation row starts only after the row above has finished (§1.1): the characters are revealed in order
            const size_t take = revealed > pos ? std::min(w.size(), revealed - pos) : 0;
            pos += w.size();
            rows.push_back({w.substr(0, take), {l.color.r, l.color.g, l.color.b, l.color.a}});
        }
    }
    if (rows.empty()) return;
    const float left = -0.5f * canvas.width + kColumnLeft, top = 0.5f * canvas.height - kColumnTop;
    const float height = kLineStep * static_cast<float>(rows.size()) + 4.0f;
    ui::add_atlas_quad(dl, kBackLayer, ui::HudList::Alpha, ui_.atlas, "i_tooltip_back.tga", {left + 0.5f * kBackWidth, top - 0.5f * height},
                       {kBackWidth, height}, 1.0f, ui::Rgba8{255, 255, 255, 220}, {kBackWidth, height});
    ui::HudTextStyle st;
    st.font = font;
    st.outline = true;
    float y = top - 0.5f * kLineStep - 2.0f;
    for (const Row& r : rows) {
        if (!r.text.empty()) {
            st.color = r.color;
            ui::add_text(dl, kTextLayer, ui::HudList::Text, *ui_.fonts, canvas, r.text, {left + 4.0f, y}, st);
        }
        y -= kLineStep;
    }
}

// The advisor hint (§6.3): one hint at a time in a plate above the command bar, fading in and out. [port] placement.
void App::build_story_hint(ui::HudDrawList& dl, const ui::HudCanvas& canvas) {
    const StoryShownHint* sh = story_present_.shown_hint();
    if (!sh || !ui_.fonts) return;
    const uint8_t alpha = story_present_.hint_alpha(now_s_);
    if (alpha == 0) return;
    const int32_t font = ui_.fonts->font("EmpireAtWar-Medium", 7.0f, canvas.font_height());
    if (font < 0) return;
    ui::HudTextStyle st;
    st.font = font;
    st.outline = true;
    st.align = ui::TextAlign::Center;
    st.color = {sh->hint.color.r, sh->hint.color.g, sh->hint.color.b, alpha};
    st.outline_color = {0, 0, 0, alpha};
    const float width = 520.0f;
    const ui::TextBlock tb = ui::measure_text_block(*ui_.fonts, canvas, sh->hint.text, width, st);
    const float cx = 0.0f, top = -0.5f * canvas.height + 190.0f + tb.height;
    ui::HudQuad plate;
    plate.min = {cx - 0.5f * width - 8.0f, top - tb.height - 6.0f};
    plate.max = {cx + 0.5f * width + 8.0f, top + 6.0f};
    plate.color = {0, 0, 0, static_cast<uint8_t>(alpha * 150u / 255u)};
    dl.add(kBackLayer, ui::HudList::Alpha, plate);
    ui::add_text_block(dl, kTextLayer, ui::HudList::Text, *ui_.fonts, canvas, sh->hint.text, {cx, top}, width, st);
}

// ---- pointers, flashes, highlights (t02) ----------------------------------------------------------------------------

namespace {
constexpr uint32_t kPointerLayer = 10;
constexpr ui::Rgba8 kArrowColor{255, 224, 40, 255}, kFlashColor{255, 255, 80, 255}, kHighlightColor{80, 255, 120, 255};

// A downward arrow whose tip is at `tip` (canvas units) bouncing with the clock: [port] a 2D stand-in for the 3D model
// i_tutorial_arrow.alo (docs/re/STORY_PRESENTATION_REWARDS.md §3.2).
void draw_arrow(ui::HudDrawList& dl, ui::Vec2f tip, double now, float scale = 1.0f) {
    const float bob = 5.0f * static_cast<float>(std::sin(now * 6.0));
    const float x = tip.x, y = tip.y + 6.0f * scale + std::abs(bob);
    ui::HudQuad head;
    head.has_corners = true;
    head.corners = {ui::Vec2f{x - 11.0f * scale, y + 16.0f * scale}, ui::Vec2f{x + 11.0f * scale, y + 16.0f * scale}, ui::Vec2f{x, y},
                    ui::Vec2f{x, y}};
    head.color = kArrowColor;
    dl.add(kPointerLayer, ui::HudList::Alpha, head);
    ui::HudQuad stem;
    stem.min = {x - 4.0f * scale, y + 16.0f * scale};
    stem.max = {x + 4.0f * scale, y + 34.0f * scale};
    stem.color = kArrowColor;
    dl.add(kPointerLayer, ui::HudList::Alpha, stem);
}

// Four corner brackets around `center` pulsing with the clock: the stand-in for the Object_Highlight effect.
void draw_highlight(ui::HudDrawList& dl, ui::Vec2f center, float half, double now, ui::Rgba8 color) {
    const float pulse = 0.75f + 0.25f * static_cast<float>(std::sin(now * 5.0));
    const float h = half * pulse, t = 2.0f, l = std::max(6.0f, half * 0.45f);
    color.a = static_cast<uint8_t>(190.0f + 65.0f * pulse);
    auto bar = [&](float x0, float y0, float x1, float y1) {
        ui::HudQuad q;
        q.min = {std::min(x0, x1), std::min(y0, y1)};
        q.max = {std::max(x0, x1), std::max(y0, y1)};
        q.color = color;
        dl.add(kPointerLayer, ui::HudList::Alpha, q);
    };
    for (const float sx : {-1.0f, 1.0f})
        for (const float sy : {-1.0f, 1.0f}) {
            const float cx = center.x + sx * h, cy = center.y + sy * h;
            bar(cx, cy, cx - sx * l, cy + sy * t);
            bar(cx, cy, cx + sx * t, cy - sy * l);
        }
}
}  // namespace

// The screen position (canvas units) of a world point in the screen's world view, false off screen.
bool App::story_project(const ui::HudCanvas& canvas, float x, float y, float z, ui::Vec2f& out) const {
    const uint32_t w = targets_.width, h = targets_.height;
    if (w == 0 || h == 0) return false;
    ui::View v;
    if (screen_ == Screen::Galactic && gview_.attached()) v = ui::make_view(gview_.camera_params(w, h), w, h);
    else if (screen_ == Screen::Battle && bview_.active()) v = ui::make_view(bview_.camera_params(w, h), w, h);
    else return false;
    float sx = 0.0f, sy = 0.0f;
    if (!ui::project(v, {x, y, z}, sx, sy)) return false;
    if (sx < 0.0f || sy < 0.0f || sx >= static_cast<float>(w) || sy >= static_cast<float>(h)) return false;
    out = canvas.pixel_to_canvas(sx, sy);
    return true;
}

// First instance of an object type by name: the first planet of that planet type on the galactic screen, the first object
// in the snapshot on a battle (STORY_PRESENTATION_REWARDS.md §2.1, §3: "the first instance").
bool App::story_type_position(std::string_view type, ui::Vec3& out) const {
    const uint32_t t = data_.rules.find_type(type);
    if (t == galactic::kNoIndex) return false;
    if (screen_ == Screen::Galactic) {
        const galactic::GalacticSim* g = galaxy();
        if (!g) return false;
        for (const galactic::PlanetState& p : g->planets())
            if (p.type == t) {
                out = {p.position.x, p.position.y, p.position.z};
                return true;
            }
        return false;
    }
    if (screen_ == Screen::Battle && bview_.active())
        for (const render::SnapshotObject& o : bview_.frame().objects)
            if (o.type == t) {
                out = {o.position.x, o.position.y, o.position.z};
                return true;
            }
    return false;
}

bool App::story_object_position(int32_t object, ui::Vec3& out) const {
    if (screen_ == Screen::Galactic) {
        const galactic::GalacticSim* g = galaxy();
        if (!g) return false;
        const PlanetIndex row = planet_index(*g, static_cast<uint32_t>(object));
        if (row == kInvalidPlanet) return false;
        const Vec3 p = g->planets()[row].position;
        out = {p.x, p.y, p.z};
        return true;
    }
    if (screen_ == Screen::Battle && bview_.active())
        for (const render::SnapshotObject& o : bview_.frame().objects)
            if (static_cast<int32_t>(o.id) == object) {
                out = {o.position.x, o.position.y, o.position.z};
                return true;
            }
    return false;
}

// The component flashes (§3.1) set the button's runtime flash; a component the presenter flashes is outlined as well, so a
// component without a flash texture still reads [port].
void App::story_component_flash(ui::HudDrawList& dl, const ui::HudCanvas& canvas) {
    (void)canvas;
    const bool gal = screen_ == Screen::Galactic;
    if (!gal && screen_ != Screen::Battle) return;
    ui::HudButtons& buttons = gal ? ui_.galactic_hud.buttons() : ui_.tactical_hud.buttons();
    const ui::HudLayout& layout = gal ? ui_.galactic_hud.layout() : ui_.tactical_hud.layout();
    std::set<int32_t> active;
    for (const StoryComponentFlash& f : story_present_.flashes()) {
        if (!story_present_.component_flashing(f.component, now_s_)) continue;
        const int32_t idx = ui_.components.find(f.component);
        if (idx < 0) continue;
        active.insert(idx);
        ui::ButtonRuntime& st = buttons.state(static_cast<uint32_t>(idx));
        st.flash = true;
        const ui::ComponentPlacement& pl = layout.placement(static_cast<uint32_t>(idx));
        if (!pl.placed || st.hidden) continue;
        const bool on = (static_cast<int64_t>(std::floor((now_s_ - f.start) / (kFlashSeconds * 0.5))) % 2) == 0;
        if (on)
            ui::frame_rect(dl, kPointerLayer, pl.center, {std::max(pl.size.x, 24.0f) + 6.0f, std::max(pl.size.y, 24.0f) + 6.0f},
                           ui::Rgba8{255, 255, 80, 40}, kFlashColor);
    }
    for (const int32_t idx : story_flashed_)
        if (!active.contains(idx)) buttons.state(static_cast<uint32_t>(idx)).flash = false;
    story_flashed_ = std::move(active);
}

void App::build_story_pointers(ui::HudDrawList& dl, const ui::HudCanvas& canvas) {
    if (screen_ != Screen::Galactic && screen_ != Screen::Battle) return;
    story_component_flash(dl, canvas);
    const bool gal = screen_ == Screen::Galactic;
    const ui::HudLayout& layout = gal ? ui_.galactic_hud.layout() : ui_.tactical_hud.layout();
    const ui::HudButtons& buttons = gal ? ui_.galactic_hud.buttons() : ui_.tactical_hud.buttons();
    // Arrow slots (§3.2): one arrow per slot at its target.
    for (const StoryArrow& a : story_present_.arrows()) {
        if (!a.has_pointer) continue;
        ui::Vec2f at;
        bool ok = false;
        if (a.kind == StoryArrow::Kind::Component) {
            const int32_t idx = ui_.components.find(a.target);
            if (idx >= 0) {
                const ui::ComponentPlacement& pl = layout.placement(static_cast<uint32_t>(idx));
                const ui::ButtonRuntime* st = buttons.find_state(static_cast<uint32_t>(idx));
                if (pl.placed && !(st && st->hidden)) {
                    at = {pl.center.x, pl.center.y + 0.5f * std::max(pl.size.y, 24.0f)};
                    ok = true;
                }
            }
        } else if (a.kind == StoryArrow::Kind::Planet || a.kind == StoryArrow::Kind::MapObject) {
            ui::Vec3 p;
            if (story_type_position(a.target, p) && story_project(canvas, p.x, p.y, p.z, at)) {
                at.y += 14.0f;
                ok = true;
            }
        }
        if (ok) draw_arrow(dl, at, now_s_);
    }
    // Numeric 2D pointers (HIGHLIGHT_OBJECT P3, FLASH_TERRAIN): at an object type's first instance or a world position.
    for (const StoryPointer& pt : story_present_.pointers()) {
        ui::Vec3 p{pt.x, pt.y, pt.z};
        ui::Vec2f at;
        if ((pt.terrain || story_type_position(pt.object_type, p)) && story_project(canvas, p.x, p.y, p.z, at)) draw_arrow(dl, at, now_s_, 0.8f);
    }
    // Object highlights: the reward's by type (first instance) and Lua Highlight / Highlight_Small by object id.
    for (const StoryHighlight& hl : story_present_.highlights()) {
        ui::Vec3 p;
        ui::Vec2f at;
        const bool found = hl.object != 0 ? story_object_position(hl.object, p) : story_type_position(hl.type_name, p);
        if (found && story_project(canvas, p.x, p.y, p.z + hl.z_offset, at))
            draw_highlight(dl, at, hl.small ? 14.0f : 26.0f, now_s_, kHighlightColor);
    }
    for (const StoryObjectFlash& f : story_present_.object_flashes()) {  // FLASH_OBJECT / FLASH_PLANET: the object's colour flash
        if (now_s_ >= f.until) continue;
        ui::Vec3 p;
        ui::Vec2f at;
        if ((static_cast<int64_t>(std::floor((now_s_ - f.start) / 0.2)) % 2) == 0 && story_type_position(f.type_name, p) &&
            story_project(canvas, p.x, p.y, p.z, at))
            draw_highlight(dl, at, 22.0f, now_s_, kFlashColor);
    }
    // Planet highlights of the story (Add_Planet_Highlight): a bracket frame on the planet while listed.
#ifdef ALAMO_SIM_HAVE_STORY
    if (gal)
        if (const sim::StorySession* story = session_ && !demo_session() ? session_->host().story() : nullptr)
            for (size_t set = 0; set < story->plot_set_count(); ++set) {
                if (story->plot_set_player(set) != local_player()) continue;
                for (const lua::StoryHighlight& hl : story->model(set).highlights()) {
                    ui::Vec3 p;
                    ui::Vec2f at;
                    if (story_object_position(hl.object_id, p) && story_project(canvas, p.x, p.y, p.z, at))
                        draw_highlight(dl, at, 30.0f, now_s_, kHighlightColor);
                }
            }
#endif
}

void App::build_story_overlay(ui::HudDrawList& dl, const ui::HudCanvas& canvas) {
    build_story_text(dl, canvas);
    build_story_pointers(dl, canvas);
    build_story_hint(dl, canvas);
}

// FORCE_CLICK_GUI (§3.4): the presenter's queued component names become a click on the component, ahead of the HUD's own
// input handling (appended to the routed pointer events).
void App::story_forced_clicks() {
    if (screen_ != Screen::Galactic && screen_ != Screen::Battle) {
        (void)story_present_.take_forced_clicks();
        return;
    }
    const bool gal = screen_ == Screen::Galactic;
    const ui::HudLayout& layout = gal ? ui_.galactic_hud.layout() : ui_.tactical_hud.layout();
    for (const std::string& name : story_present_.take_forced_clicks()) {
        const int32_t idx = ui_.components.find(name);
        if (idx < 0) continue;
        const ui::ComponentPlacement& pl = layout.placement(static_cast<uint32_t>(idx));
        const ui::Vec2f px = ui_.gui.canvas().canvas_to_pixel(pl.center.x, pl.center.y);
        routed_.hud.push_back({ui::HudPointerEvent::Kind::Click, ui::Button::Left, idx, static_cast<int32_t>(px.x), static_cast<int32_t>(px.y)});
    }
}

}  // namespace alamo_game_app
