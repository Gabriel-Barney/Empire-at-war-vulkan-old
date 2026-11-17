#include "alamo/ui/text_crawl.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "alamo/ui/hud_font.hpp"

namespace alamo::ui {

namespace {
// The point size whose pixel height at screen height 450 is `px` (font_pixel_height: point x (450 x 96 / 600) / 72 = point).
constexpr uint32_t kTexelScreen = 450;

float ramp(int32_t f, int32_t a, int32_t b) {
    if (a < 0) return 1.0f;
    if (b <= a) return f >= a ? 1.0f : 0.0f;
    return std::clamp(static_cast<float>(f - a) / static_cast<float>(b - a), 0.0f, 1.0f);
}

struct Line {
    std::u32string text;
    bool header = false;
    float top = 0.0f;  // texture pixels from the crawl's top, in body-texture units
    float height = 0.0f;
};
}  // namespace

uint32_t add_text_crawl(HudDrawList& dl, uint32_t layer, FontCache& fonts, const HudCanvas& cv, const TextCrawlLayout& lay,
                        std::u32string_view header, std::span<const std::u32string> paragraphs, int32_t frame) {
    if (frame < 0 || lay.texture_width <= 0) return 0;
    const int32_t body = fonts.font(lay.font, lay.font_size, kTexelScreen, lay.stretch);
    if (body < 0) return 0;
    const int32_t head = header.empty() ? -1 : fonts.font(lay.header_font, lay.header_font_size, kTexelScreen, lay.header_stretch);
    const float body_lh = static_cast<float>(fonts.metrics(body).line_height);
    const float tw = static_cast<float>(lay.texture_width);
    // Header texels are mapped to the same column width: one header texel = tw / header_texture_width body texels.
    const float head_k = lay.header_texture_width > 0 ? tw / static_cast<float>(lay.header_texture_width) : 1.0f;

    std::vector<Line> lines;
    float y = 0.0f;
    if (head >= 0) {
        const float lh = static_cast<float>(fonts.metrics(head).line_height) * head_k;
        for (std::u32string& t : wrap_text(fonts, head, header, static_cast<float>(lay.header_texture_width))) {
            lines.push_back({std::move(t), true, y, lh});
            y += lh;
        }
        y += body_lh;
    }
    for (const std::u32string& p : paragraphs) {
        for (std::u32string& t : wrap_text(fonts, body, p, tw)) {
            lines.push_back({std::move(t), false, y, body_lh});
            y += body_lh;
        }
        y += body_lh;
    }

    const float sw = static_cast<float>(cv.screen_width), sh = static_cast<float>(cv.screen_height);
    const float scroll = static_cast<float>(frame) / 30.0f * lay.lines_per_second * body_lh;  // texels moved up
    const float depth = std::max(1.0f, lay.depth_lines * body_lh);
    const float base = lay.column_fraction * sw / tw;  // screen pixels per texel at the bottom edge
    const float horizon_y = lay.horizon * sh;
    const float alpha_t = ramp(frame, lay.fadein_start, lay.fadein_end) *
                          (lay.fadeout_start < 0 ? 1.0f
                                                 : 1.0f - ramp(frame, lay.fadeout_start,
                                                               lay.fadeout_end >= 0 ? lay.fadeout_end : lay.fadeout_start + 60));
    if (alpha_t <= 0.0f) return 0;
    uint32_t drawn = 0;
    for (const Line& l : lines) {
        const float d = scroll - (l.top + l.height);  // distance along the plane of the line's bottom past the screen edge
        if (d < -l.height) continue;                  // not on screen yet
        const float f = 1.0f / (1.0f + std::max(0.0f, d) / depth);
        if (f < 0.04f) continue;  // past the horizon
        const float bottom = horizon_y + (sh - horizon_y) * f + std::min(0.0f, d) * -base;  // entering: slides in from below
        const float px_h = l.height * base * f;
        if (bottom - px_h > sh) continue;
        HudTextStyle st;
        st.font = l.header ? head : body;
        st.scale = base * f * (l.header ? head_k : 1.0f);
        st.char_padding_px = l.header ? lay.header_char_padding : lay.char_padding;
        st.align = TextAlign::Center;
        st.snap = false;
        const float a = alpha_t * std::clamp((f - 0.04f) / 0.2f, 0.0f, 1.0f);
        st.color = {255, 232, 31, static_cast<uint8_t>(std::lround(255.0f * a))};
        add_text(dl, layer, HudList::Text, fonts, cv, l.text, cv.pixel_to_canvas(0.5f * sw, bottom - 0.5f * px_h), st);
        ++drawn;
    }
    return drawn;
}

}  // namespace alamo::ui
