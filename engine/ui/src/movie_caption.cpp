#include "alamo/ui/movie_caption.hpp"

#include <vector>

#include "alamo/data/game_database.hpp"
#include "alamo/ui/hud_font.hpp"

namespace alamo::ui {

MovieCaptionStyle movie_caption_style(const data::GameConstants& gc) {
    MovieCaptionStyle s;
    if (const auto f = gc.get("Bink_Player_Caption_Font_Name"); f && !f->empty()) s.font = std::string(*f);
    const int32_t size = gc.get_int("Bink_Player_Caption_Font_Size", static_cast<int32_t>(s.font_size));
    if (size > 0) s.font_size = static_cast<float>(size);
    return s;
}

uint32_t add_movie_caption(HudDrawList& dl, uint32_t layer, FontCache& fonts, const HudCanvas& cv, std::u32string_view text,
                           const MovieCaptionStyle& style) {
    if (text.empty()) return 0;
    HudTextStyle st;
    st.font = fonts.font(style.font, style.font_size, cv.font_height());
    if (st.font < 0) return 0;
    st.color = {255, 255, 255, 255};
    st.align = TextAlign::Center;
    const float sw = static_cast<float>(cv.screen_width), sh = static_cast<float>(cv.screen_height);
    const std::vector<std::u32string> lines = wrap_text(fonts, st.font, text, sw);
    if (lines.empty()) return 0;
    const float lh = static_cast<float>(fonts.metrics(st.font).line_height);
    // The block's last line has its top at 0.95 of the screen height; add_text anchors a line's vertical centre.
    const float last_top = 0.95f * sh;
    const size_t n = lines.size();
    for (size_t i = 0; i < n; ++i) {
        const float top = last_top - static_cast<float>(n - 1 - i) * lh;
        add_text(dl, layer, HudList::Text, fonts, cv, lines[i], cv.pixel_to_canvas(0.5f * sw, top + 0.5f * lh), st);
    }
    return static_cast<uint32_t>(n);
}

}  // namespace alamo::ui
