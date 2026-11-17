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

}  // namespace alamo::video
