// storyb t13: the story text crawl over the full-screen movie (Movies.xml Text_Crawl_Name / Text_Crawl_Start_Frame ->
// StarWars3DTextCrawl.xml, video::parse_text_crawls), drawn by ui::add_text_crawl ([port] look values there).
#include "game_app.hpp"

#include <vector>

#include "alamo/ui/text_crawl.hpp"
#if defined(ALAMO_GAME_HAVE_VIDEO) && defined(ALAMO_VIDEO_HAVE_MOVIE_DB)
#include "alamo/data/xml.hpp"
#include "alamo/video/movie_db.hpp"
#endif

namespace alamo_game_app {

struct TextCrawlCache {
#if defined(ALAMO_GAME_HAVE_VIDEO) && defined(ALAMO_VIDEO_HAVE_MOVIE_DB)
    std::vector<video::TextCrawlSpec> crawls;
#endif
};

void App::draw_text_crawl(ui::HudDrawList& dl, const ui::HudCanvas& canvas) {
#if defined(ALAMO_GAME_HAVE_VIDEO) && defined(ALAMO_VIDEO_HAVE_MOVIE_DB)
    if (!ui_.fonts || full_movie_.text_crawl().empty()) return;
    const int32_t frame = full_movie_.text_crawl_frame();
    if (frame < 0) return;
    if (!text_crawls_) {
        text_crawls_ = std::make_shared<TextCrawlCache>();
        std::vector<uint8_t> xml;
        if (data_.files && data_.files->read("Data\\XML\\StarWars3DTextCrawl.xml", xml)) {
            const data::XmlDocument doc = data::XmlDocument::parse(xml, "StarWars3DTextCrawl.xml");
            text_crawls_->crawls = video::parse_text_crawls(doc.root());  // [port] English (the text ids carry the language)
        }
    }
    const video::TextCrawlSpec* s = video::find_text_crawl(text_crawls_->crawls, full_movie_.text_crawl());
    if (!s) return;
    ui::TextCrawlLayout lay;
    if (!s->font_name.empty()) lay.font = s->font_name;
    if (s->font_size > 0) lay.font_size = static_cast<float>(s->font_size);
    if (s->texture_width_pow_2 > 0) lay.texture_width = s->texture_width_pow_2;
    lay.stretch = s->font_stretch_factor;
    lay.char_padding = static_cast<float>(s->font_character_padding);
    if (!s->header_font_name.empty()) lay.header_font = s->header_font_name;
    if (s->header_font_size > 0) lay.header_font_size = static_cast<float>(s->header_font_size);
    if (s->header_texture_width_pow_2 > 0) lay.header_texture_width = s->header_texture_width_pow_2;
    lay.header_stretch = s->header_font_stretch_factor;
    lay.header_char_padding = static_cast<float>(s->header_font_character_padding);
    lay.fadein_start = s->fadein_start_frame;
    lay.fadein_end = s->fadein_end_frame;
    lay.fadeout_start = s->fadeout_start_frame;
    lay.fadeout_end = s->fadeout_end_frame;
    auto text_of = [this](const std::string& id) {
        const std::u16string* t = ui_.text.find(id);
        return t ? ui::gui_unescape(ui::utf16_to_utf32(*t)) : std::u32string();
    };
    std::u32string header;
    if (s->has_header)
        for (const std::string& id : s->header_text_ids) {
            if (!header.empty()) header += U'\n';
            header += text_of(id);
        }
    std::vector<std::u32string> paragraphs;
    for (const std::string& id : s->text_ids) paragraphs.push_back(text_of(id));
    ui::add_text_crawl(dl, 1, *ui_.fonts, canvas, lay, header, paragraphs, frame);
#else
    (void)dl;
    (void)canvas;
#endif
}

}  // namespace alamo_game_app
