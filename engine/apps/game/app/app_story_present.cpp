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

void App::story_present_frame() {
    // Retail shows the next hint only with no Bink movie open, no advanced map and no cinematic mode (§6.3).
    const bool blocked = full_movie_.playing() || planet_map_open_ || story_present_.cinematic_mode();
    story_present_.update(now_s_, blocked);
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

void App::build_story_overlay(ui::HudDrawList& dl, const ui::HudCanvas& canvas) {
    build_story_text(dl, canvas);
    build_story_hint(dl, canvas);
}

}  // namespace alamo_game_app
