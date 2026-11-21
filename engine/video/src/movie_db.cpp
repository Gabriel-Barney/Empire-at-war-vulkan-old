#include "alamo/video/movie_db.hpp"

#include <cmath>
#include <cstdlib>
#include <map>
#include <string>

#include "alamo/core/ascii.hpp"
#include "alamo/data/values.hpp"
#include "alamo/data/xml.hpp"

namespace alamo::video {

namespace {
bool flag(const data::XmlNode& m, std::string_view tag, bool fallback) {
    const std::string_view t = m.child_text(tag);
    if (t.empty()) return fallback;
    if (const auto b = data::parse_bool(t)) return *b;
    return fallback;
}
std::string_view stem(std::string_view path) {
    const size_t s = path.find_last_of("\\/");
    if (s != std::string_view::npos) path.remove_prefix(s + 1);
    const size_t d = path.find_last_of('.');
    if (d != std::string_view::npos) path = path.substr(0, d);
    return path;
}

// "TEXT_ID, 123" -> (id, value); false when either part is missing or the number does not parse.
bool id_and_int(std::string_view t, std::string& id, int32_t& value) {
    const size_t comma = t.find(',');
    if (comma == std::string_view::npos) return false;
    const std::string_view a = ascii_trim(t.substr(0, comma)), b = ascii_trim(t.substr(comma + 1));
    if (a.empty() || b.empty()) return false;
    const std::string num(b);
    char* end = nullptr;
    const long v = std::strtol(num.c_str(), &end, 10);
    if (end == num.c_str()) return false;
    id = std::string(a);
    value = static_cast<int32_t>(v);
    return true;
}

// Caption_Frame / Caption_Duration of one <Movie> (ui5 t12): maps keyed by the text id, as retail keeps them.
std::vector<MovieCaption> parse_captions(const data::XmlNode& m) {
    std::map<std::string, int32_t> frames, durations;
    std::string id;
    int32_t v = 0;
    for (data::XmlNode c : m.children("Caption_Frame"))
        if (id_and_int(c.text(), id, v)) frames[id] = v;
    for (data::XmlNode c : m.children("Caption_Duration"))
        if (id_and_int(c.text(), id, v)) durations[id] = v;
    std::vector<MovieCaption> out;
    for (const auto& [key, frame] : frames) {
        MovieCaption c;
        c.text_id = key;
        c.frame = frame;
        if (const auto it = durations.find(key); it != durations.end()) c.duration = it->second;
        out.push_back(std::move(c));
    }
    return out;
}
}  // namespace

std::vector<MovieEntry> parse_movies(const data::XmlNode& root) {
    std::vector<MovieEntry> out;
    for (data::XmlNode m : root.children("Movie")) {
        MovieEntry e;
        e.name = std::string(m.attribute("Name"));
        e.file = std::string(m.child_text("Movie_File"));
        e.alpha = flag(m, "Alpha", false);
        e.cannot_skip = flag(m, "Cannot_Skip", false);
        e.loop = flag(m, "Loop", false);
        float off[2] = {0, 0};
        if (data::parse_floats(m.child_text("Commandbar_Offset"), off, 2) == 2) {
            e.commandbar_offset[0] = static_cast<int>(std::lround(off[0]));
            e.commandbar_offset[1] = static_cast<int>(std::lround(off[1]));
        }
        e.captions = parse_captions(m);
        e.text_crawl_name = std::string(data::trim(m.child_text("Text_Crawl_Name")));  // storyb t13
        e.text_crawl_start_frame = data::parse_int_or(m.child_text("Text_Crawl_Start_Frame"), 0);
        e.text_crawl_end_frame = data::parse_int_or(m.child_text("Text_Crawl_End_Frame"), -1);
        out.push_back(std::move(e));
    }
    return out;
}

const MovieEntry* find_movie(const std::vector<MovieEntry>& movies, std::string_view key) {
    const MovieEntry* by_file = nullptr;
    for (size_t i = movies.size(); i-- > 0;) {
        if (ascii_iequals(movies[i].name, key)) return &movies[i];  // exact ASCII fold: `key` may be a path
        if (!by_file && !movies[i].file.empty() && ascii_iequals(stem(movies[i].file), stem(key))) by_file = &movies[i];
    }
    return by_file;
}

// ---- storyb t13: StarWars3DTextCrawl.xml ----------------------------------------------------------------------------

namespace {
std::vector<std::string> names(std::string_view t) {
    std::vector<std::string> out;
    for (std::string_view s : data::split_list(t)) out.emplace_back(s);
    return out;
}
// The fields `spec` sets replace those of `c` (a tag present with empty text counts as set).
void apply_spec(const data::XmlNode& spec, TextCrawlSpec& c) {
    auto str = [&spec](std::string_view tag, std::string& out) {
        if (spec.child(tag)) out = std::string(data::trim(spec.child_text(tag)));
    };
    auto num = [&spec](std::string_view tag, int32_t& out) {
        if (spec.child(tag)) out = data::parse_int_or(spec.child_text(tag), out);
    };
    auto real = [&spec](std::string_view tag, float& out) {
        if (spec.child(tag)) out = data::parse_float_or(spec.child_text(tag), out);
    };
    auto list = [&spec](std::string_view tag, std::vector<std::string>& out) {
        if (spec.child(tag)) out = names(spec.child_text(tag));
    };
    str("Model_Name", c.model_name);
    str("Polygon_Shader_Name", c.polygon_shader_name);
    str("Polygon_Tex_Param_Name", c.polygon_tex_param_name);
    str("Model_Camera_Bone_Name", c.model_camera_bone_name);
    num("Fadein_Start_Frame", c.fadein_start_frame);
    num("Fadein_End_Frame", c.fadein_end_frame);
    num("Fadeout_Start_Frame", c.fadeout_start_frame);
    num("Fadeout_End_Frame", c.fadeout_end_frame);
    list("Text_IDs", c.text_ids);
    str("Font_Name", c.font_name);
    num("Font_Size", c.font_size);
    num("Texture_Width_Pow_2", c.texture_width_pow_2);
    num("Texture_Height_Pow_2", c.texture_height_pow_2);
    real("Font_Stretch_Factor", c.font_stretch_factor);
    num("Font_Character_Padding", c.font_character_padding);
    if (spec.child("Has_Header")) c.has_header = flag(spec, "Has_Header", c.has_header);
    list("Header_Text_IDs", c.header_text_ids);
    str("Header_Font_Name", c.header_font_name);
    num("Header_Font_Size", c.header_font_size);
    num("Header_Texture_Width_Pow_2", c.header_texture_width_pow_2);
    num("Header_Texture_Height_Pow_2", c.header_texture_height_pow_2);
    real("Header_Font_Stretch_Factor", c.header_font_stretch_factor);
    num("Header_Font_Character_Padding", c.header_font_character_padding);
}
}  // namespace

std::vector<TextCrawlSpec> parse_text_crawls(const data::XmlNode& root, std::string_view language) {
    std::vector<TextCrawlSpec> out;
    for (data::XmlNode s : root.children("TextScroll")) {
        TextCrawlSpec c;
        c.name = std::string(s.attribute("name"));
        for (data::XmlNode d : s.children("Data_Specifications"))
            if (ascii_iequals(d.attribute("language"), "English")) apply_spec(d, c);
        if (!ascii_iequals(language, "English"))
            for (data::XmlNode d : s.children("Data_Specifications"))
                if (ascii_iequals(d.attribute("language"), language)) apply_spec(d, c);
        out.push_back(std::move(c));
    }
    return out;
}

const TextCrawlSpec* find_text_crawl(const std::vector<TextCrawlSpec>& crawls, std::string_view name) {
    for (size_t i = crawls.size(); i-- > 0;)
        if (ascii_iequals(crawls[i].name, name)) return &crawls[i];
    return nullptr;
}

}  // namespace alamo::video
