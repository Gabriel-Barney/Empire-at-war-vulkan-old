// Movies.xml (docs/re/STRATEGIC_GAPS.md §7.3 [H]: Movie_File, Commandbar_Offset, Alpha, Cannot_Skip, Loop) for the
// movie players: which movies have an alpha plane (decoded with OpenOptions::alpha, drawn with straight alpha on
// PrimAlpha - ui::add_movie_quad(..., alpha = true)) and which are opaque (PrimOpaque). Built only with engine/data.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/video/video.hpp"

namespace alamo::data {
class XmlNode;
}

namespace alamo::video {

// Every <Movie Name="..."> under the <Movies> root, in file order (a later entry of the same name is kept too;
// find_movie returns the last one). Missing fields keep the MovieEntry defaults.
std::vector<MovieEntry> parse_movies(const data::XmlNode& movies_root);
// By Name (ASCII case-insensitive), else by the Movie_File's base name without extension (the host opens movies by
// file name, e.g. "pirate_loop2"); nullptr if none.
const MovieEntry* find_movie(const std::vector<MovieEntry>& movies, std::string_view name_or_file);

// storyb t13: one <TextScroll name> of Data\XML\StarWars3DTextCrawl.xml (the 3D story text crawl: Star_Wars_Intro,
// Star_Wars_Intro_Underworld, Underworld_Prolog_Text ...), fields of the Data_Specifications table (xml_field_tables.md
// "fields of `Data_Specifications` objects"). Unset ints are -1, unset floats 1 (stretch) / 0, unset strings "".
struct TextCrawlSpec {
    std::string name;
    std::string model_name, polygon_shader_name, polygon_tex_param_name, model_camera_bone_name;
    int32_t fadein_start_frame = -1, fadein_end_frame = -1, fadeout_start_frame = -1, fadeout_end_frame = -1;
    std::vector<std::string> text_ids;  // Text_IDs, in order (the crawl's paragraphs)
    std::string font_name;
    int32_t font_size = -1, texture_width_pow_2 = -1, texture_height_pow_2 = -1;
    float font_stretch_factor = 1.0f;
    int32_t font_character_padding = 0;
    bool has_header = false;
    std::vector<std::string> header_text_ids;
    std::string header_font_name;
    int32_t header_font_size = -1, header_texture_width_pow_2 = -1, header_texture_height_pow_2 = -1;
    float header_font_stretch_factor = 1.0f;
    int32_t header_font_character_padding = 0;
};
// Every <TextScroll name> under the <StarWars3DTextScroll> root, in file order, for `language`: the English
// <Data_Specifications> first, then the fields the language's own block sets replace them (the file's rule "All other
// fields will be filled in from English" [port: read from the data comment, not the binary]). A language without a block
// gets English; a scroll without an English block starts from the defaults.
std::vector<TextCrawlSpec> parse_text_crawls(const data::XmlNode& root, std::string_view language = "English");
// By name (ASCII case-insensitive; Movies.xml writes "A_Long_Time_Ago" as the file does), last one wins; nullptr if none.
const TextCrawlSpec* find_text_crawl(const std::vector<TextCrawlSpec>& crawls, std::string_view name);

}  // namespace alamo::video
