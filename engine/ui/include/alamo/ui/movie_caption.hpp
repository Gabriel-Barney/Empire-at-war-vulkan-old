// ui5 t02 / t12: the full-screen movie player's captions (docs/re/AUDIO.md §3.4 "Movie subtitles"): with the
// IDC_USE_MOVIE_SUBTITLES option on, a Movies.xml Caption_Frame's text is drawn over the movie in GameConstants
// Bink_Player_Caption_Font_Name / _Font_Size, white, each line centred, the last line's top at 0.95 of the screen height
// and the lines above it one font line apart. Which caption shows when is video::CaptionTrack's job; this only draws.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "alamo/ui/hud_canvas.hpp"
#include "alamo/ui/hud_draw.hpp"

namespace alamo::data {
struct GameConstants;
}

namespace alamo::ui {

class FontCache;

struct MovieCaptionStyle {
    std::string font = "EmpireAtWar-Medium";  // FoC GameConstants values
    float font_size = 10.0f;
};
MovieCaptionStyle movie_caption_style(const data::GameConstants& gc);

// Draws `text` (already looked up; "" draws nothing) wrapped at the screen width [M] on `layer`. Returns the number of
// lines drawn (0 when the font cannot be made).
uint32_t add_movie_caption(HudDrawList& dl, uint32_t layer, FontCache& fonts, const HudCanvas& canvas, std::u32string_view text,
                           const MovieCaptionStyle& style);

}  // namespace alamo::ui
