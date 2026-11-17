// storyb t13: the 3D story text crawl over a movie (Movies.xml Text_Crawl_Name -> a <TextScroll> of
// StarWars3DTextCrawl.xml; video::TextCrawlSpec holds the data). Retail renders the text into a texture
// (Texture_Width_Pow_2 x Texture_Height_Pow_2, Font_Name / Font_Size) mapped on W_TextScroll.alo and films it from the
// model's camera bone; the model's animation is not read here. [port] This draws the same text as HUD glyph lines on a
// receding plane: lines laid out in texture pixels (the texture width, the font at Font_Size pixels), scrolled up at
// a fixed rate, each line scaled and placed by a perspective divide towards a horizon, fading in near the horizon and
// with the Fadein / Fadeout frames. The rate, the plane depth and the column width are port values for Gabriel's look.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "alamo/ui/hud_canvas.hpp"
#include "alamo/ui/hud_draw.hpp"

namespace alamo::ui {

class FontCache;

struct TextCrawlLayout {
    std::string font = "Arial Bold";  // Font_Name
    float font_size = 52.0f;          // Font_Size: glyph pixel height in the texture
    int32_t texture_width = 512;      // Texture_Width_Pow_2: the column width in texture pixels
    float stretch = 1.0f;             // Font_Stretch_Factor
    float char_padding = 0.0f;        // Font_Character_Padding (pixels after each glyph)
    std::string header_font = "Arial Bold";
    float header_font_size = 100.0f;
    int32_t header_texture_width = 1024;
    float header_stretch = 1.0f;
    float header_char_padding = 0.0f;
    // Movie frames from the crawl's start (-1 unset): alpha 0 -> 1 from fadein_start to fadein_end, 1 -> 0 from
    // fadeout_start to fadeout_end (unset end: fadeout_start + 60).
    int32_t fadein_start = -1, fadein_end = -1, fadeout_start = -1, fadeout_end = -1;
    // [port] look values.
    float lines_per_second = 0.8f;   // scroll rate in body lines
    float column_fraction = 0.62f;   // the column's width on screen at the bottom edge, of the screen width
    float depth_lines = 9.0f;        // plane distance (in body lines) at which a line is drawn at half size
    float horizon = 0.08f;           // horizon height, fraction of the screen height from the top
};

// Draws the crawl `frame` movie frames (30 per second) after its start: `header` (empty: none) above the paragraphs,
// each paragraph wrapped to the column and followed by one blank line. Yellow glyphs (255, 232, 31) on `layer`.
// Returns the number of lines drawn (0 before any line reached the screen, after they all passed the horizon, or when
// no font can be made).
uint32_t add_text_crawl(HudDrawList& dl, uint32_t layer, FontCache& fonts, const HudCanvas& canvas, const TextCrawlLayout& layout,
                        std::u32string_view header, std::span<const std::u32string> paragraphs, int32_t frame);

}  // namespace alamo::ui
