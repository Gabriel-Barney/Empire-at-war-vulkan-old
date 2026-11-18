// P2 HUD fonts (docs/re/UI.md §3.4 [H]): TrueType faces rasterised at runtime into glyph texture pages,
// colours applied at draw time.
//   * Pixel em height = point size * (screenH * 96 / 600) / 72 (hud_canvas.hpp font_pixel_height_int);
//   * weight 700 for bold faces else 400, antialiased; Stretch_Factor s outside 0.9999..1.0001 re-creates the font at
//     height floor(h x s + 0.5) keeping the unstretched tmAveCharWidth (taller / squatter glyphs, same widths;
//     GALACTIC_DIALOGS.md §6);
//   * a face that cannot be created falls back to "EmpireAtWar-Medium"; Windows face substitution is logged.
// GlyphRasterizer is the OS seam: make_system_rasterizer() is GDI on Windows (CreateFontW +
// GetGlyphOutlineW GGO_GRAY8_BITMAP into 8-bit coverage), and the deterministic BoxRasterizer elsewhere
// (filled boxes; for platforms without GDI and for pixel-exact tests). The port contract is the size rule,
// not GDI (UI.md §3.4 "Port").
// [port] bold = the face name contains "Bold" (the data names faces "EmpireAtWar-Bold", "Arial Bold"); a registered
// game font (register_game_fonts) keeps its own weight instead.
//
// Presentation only: nothing here feeds the sim, so OS rasteriser differences are allowed.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace alamo::ui {

inline constexpr const char* kFallbackFontFace = "EmpireAtWar-Medium";

struct FontRequest {
    std::string face;
    int32_t pixel_height = 12;  // em height in pixels (CreateFont negative height)
    bool bold = false;
    float stretch = 1.0f;       // Stretch_Factor
};

struct FontMetrics {
    int32_t pixel_height = 0;
    int32_t ascent = 0, descent = 0, line_height = 0;  // pixels
    std::string actual_face;                           // face the OS used (substitution check)
};

// One glyph's 8-bit coverage bitmap (row-major, width * height, top row first).
struct GlyphBitmap {
    int32_t width = 0, height = 0;
    int32_t origin_x = 0;  // pen x -> bitmap left
    int32_t origin_y = 0;  // baseline -> bitmap top (pixels above the baseline)
    int32_t advance = 0;   // pen advance
    std::vector<uint8_t> coverage;
};

class GlyphRasterizer {
public:
    virtual ~GlyphRasterizer() = default;
    // Creates a font; returns its id (>= 0) or -1.
    virtual int32_t create_font(const FontRequest& req, FontMetrics& out) = 0;
    virtual bool rasterize(int32_t font, char32_t code_point, GlyphBitmap& out) = 0;
    virtual const char* name() const = 0;
};
// GDI on Windows, else BoxRasterizer.
std::unique_ptr<GlyphRasterizer> make_system_rasterizer();
// Deterministic stand-in: every glyph a filled box of 0.5 x 0.7 em, advance 0.6 em, spaces empty.
std::unique_ptr<GlyphRasterizer> make_box_rasterizer();

// Game fonts. The data names "EmpireAtWar-Medium", "EmpireAtWar-Bold", "EmpireAtWar-light" (plus Arial faces, which
// are system fonts). The Steam install has no font files and installs none: the four EmpireAtWar TrueType fonts
// (Light / Medium / Bold / Stencil; families "Empire At War <Style>", full names "EmpireAtWar-<Style>") are stored
// whole inside StarWarsG.exe of both GameData and corruption. Older installs put .ttf files next to the game.
// register_game_fonts() finds them under each root (earlier roots win per full name):
//   <root>\*.ttf|*.otf, <root>\Fonts\, <root>\Data\Fonts\, <root>\Data\Art\Fonts\ (loose files), then the sfnt blobs
//   embedded in <root>\StarWarsG.exe;
// and makes each visible to this process only (Windows AddFontMemResourceEx; no install, no registry). Idempotent.
// Afterwards the system rasteriser resolves a requested face by full, PostScript or family name to the registered
// family and keeps the font's own weight (no synthetic bold on "EmpireAtWar-Bold", which is a weight-600 face).
struct TrueTypeFontInfo {
    size_t offset = 0, size = 0;  // the sfnt blob inside the scanned bytes
    std::string family;           // name id 1 (e.g. "Empire At War Bold")
    std::string full_name;        // name id 4 (e.g. "EmpireAtWar-Bold")
    std::string postscript_name;  // name id 6
    uint16_t weight = 400;        // OS/2 usWeightClass
};
// Every well-formed TrueType / OpenType font (sfnt version 0x00010000 or 'OTTO', valid table directory with
// head / cmap / name, head magic) stored in `bytes`, in file order. Pure parsing (portable, no OS calls).
std::vector<TrueTypeFontInfo> find_truetype_fonts(const uint8_t* bytes, size_t size);

struct GameFont {
    std::string full_name, family, postscript_name;
    uint16_t weight = 400;
    std::string source;  // host path it came from ("...\StarWarsG.exe@0x9dd860" for embedded fonts)
};
// Returns the fonts newly registered by this call (already-registered names are skipped). Roots: mod dirs first,
// then the FoC root (the "corruption" dir), then the EaW root (GameData). Empty entries are ignored.
std::vector<GameFont> register_game_fonts(const std::vector<std::string>& game_roots);
// Every font registered so far (process-wide, registration order).
std::vector<GameFont> registered_game_fonts();
// The registered font a face name means (case-insensitive full, PostScript or family name), or false.
bool find_game_font(std::string_view face, GameFont* out = nullptr);

// mcx4 t14 (MCX4_T14_RE_ANSWERS "GameConstants.Command_Bar_Default_Font_Name family", "Game_Object_Name_Font_Name",
// "Tool_Tip_Font_Name family"): the GameConstants default fonts (defaults = FoC's values), set once by the host.
enum class FontUse : uint8_t {
    Text,     // a text component without its own font: Command_Bar_Default_Font_Name / _Size
    Button,   // a text button without its own font: Text_Button_Default_Font_Name / _Size
    World,    // in-world / HUD text (control-group numbers, fly-text, waypoint numbers): Game_Object_Name_Font_Name / _Size
    Tooltip,  // the tooltip text: Tool_Tip_Font_Name / _Size
};
struct DefaultFonts {
    std::string text_face = kFallbackFontFace, button_face = kFallbackFontFace, world_face = kFallbackFontFace;
    std::string tooltip_face = "Arial";
    float text_size = 7.0f, button_size = 7.0f, world_size = 7.0f, tooltip_size = 7.0f;
};

// Glyphs packed into square RGBA8 pages (white, alpha = coverage). Pages only grow; dirty pages are
// re-uploaded by the renderer (generation counter). clear() on a resolution change (font sizes depend
// on the screen height).
class FontCache {
public:
    explicit FontCache(GlyphRasterizer& r, uint32_t page_size = 512) : r_(&r), page_size_(page_size) {}

    // Font id for a face / point size at a screen height (cached). Unknown faces fall back to
    // kFallbackFontFace; -1 only when even that fails.
    int32_t font(std::string_view face, float point_size, uint32_t screen_height, float stretch = 1.0f);
    const FontMetrics& metrics(int32_t font) const { return fonts_[static_cast<size_t>(font)].metrics; }
    // mcx4 t14: the default fonts and a component's font by the retail rule (0x349a20 / 0x351d50): its own name (size:
    // its own, else the use's); a size without a name takes the use's face (Text / Button: Command_Bar_Default_Font_Name);
    // neither: the use's default font. `name` empty / `size` <= 0 = unset.
    void set_defaults(DefaultFonts d) { defaults_ = std::move(d); }
    const DefaultFonts& defaults() const { return defaults_; }
    int32_t component_font(std::string_view name, float size, FontUse use, uint32_t screen_height);

    struct Glyph {
        uint16_t page = 0;
        uint32_t x = 0, y = 0, width = 0, height = 0;  // in the page (pixels)
        int32_t origin_x = 0, origin_y = 0, advance = 0;
    };
    const Glyph* glyph(int32_t font, char32_t c);
    // Width in pixels of a run (sum of advances).
    int32_t measure(int32_t font, std::u32string_view text);

    uint32_t page_size() const { return page_size_; }
    size_t page_count() const { return pages_.size(); }
    const std::vector<uint8_t>& page_rgba(size_t i) const { return pages_[i].rgba; }
    uint64_t page_generation(size_t i) const { return pages_[i].generation; }
    void clear();
    uint32_t substitutions() const { return substitutions_; }

private:
    struct Page {
        std::vector<uint8_t> rgba;
        uint32_t shelf_y = 0, shelf_h = 0, cursor_x = 0;
        uint64_t generation = 0;
    };
    struct Font {
        int32_t raster_id = -1;
        FontMetrics metrics;
        std::map<char32_t, Glyph> glyphs;  // ordered: deterministic
    };
    bool pack(uint32_t w, uint32_t h, uint16_t& page, uint32_t& x, uint32_t& y);

    GlyphRasterizer* r_;
    DefaultFonts defaults_;
    uint32_t page_size_;
    std::vector<Font> fonts_;
    std::map<std::tuple<std::string, int32_t, int32_t>, int32_t> by_key_;  // (FACE, px, stretch*1000) -> font
    std::vector<Page> pages_;
    uint64_t generation_ = 0;
    uint32_t substitutions_ = 0;
};

bool face_is_bold(std::string_view face);

}  // namespace alamo::ui
