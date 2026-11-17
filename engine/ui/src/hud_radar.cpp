#include "alamo/ui/hud_radar.hpp"

#include <algorithm>
#include <cmath>

#include "alamo/core/ascii.hpp"
#include "alamo/core/file_provider.hpp"
#include "alamo/data/values.hpp"
#include "alamo/data/xml.hpp"

namespace alamo::ui {

namespace {

// Trimmed (data::trim), then ASCII upper case.
std::string upper(std::string_view s) { return ascii_upper(data::trim(s)); }

Rgba8 color(std::string_view text, Rgba8 fallback) {
    float v[4] = {static_cast<float>(fallback.r), static_cast<float>(fallback.g), static_cast<float>(fallback.b),
                  static_cast<float>(fallback.a)};
    std::string t(text);
    std::replace(t.begin(), t.end(), ',', ' ');
    if (data::parse_floats(t, v, 4) < 3) return fallback;
    auto c = [](float f) { return static_cast<uint8_t>(std::lround(std::clamp(f, 0.0f, 255.0f))); };
    return {c(v[0]), c(v[1]), c(v[2]), c(v[3])};
}

constexpr const char* kPassNames[kPassabilityCount] = {"PASSABILITY_STEEP",    "PASSABILITY_WATER",
                                                       "PASSABILITY_WALL",     "PASSABILITY_OBSTACLE",
                                                       "PASSABILITY_CLEAR",    "PASSABILITY_IMPASSABLE",
                                                       "PASSABILITY_INFANTRYONLY"};

}  // namespace

// modc t30 (docs/re/xml_field_tables.md, "Elements read outside the descriptor tables", loader 0x359390 / walker 0x3572c0):
// the original walks the whole file. It checks no element path (the root and the RadarMapEvents / RadarMapSettings
// containers are not compared), descends into every child, and at any depth treats Passability_Color_Settings, Color and
// Radar_Map_Event specially; every other element with text is a RadarMapClass table field. Two pieces of state carry from
// one element to the next: the selected passability block and the current radar event, whose Event_* values pass
// through one scratch record that is never reset, so a field one event leaves out keeps the previous event's value.
namespace {

constexpr const char* kBiomeBlocks[] = {"TEMPERATE", "ARCTIC", "DESERT", "FOREST", "SWAMP", "VOLCANIC", "URBAN", "SPACE"};
constexpr int32_t kAllBlocks = 8;  // any other name: a write goes to every terrain block (vanilla "Default")

struct RadarWalk {
    RadarSettings& out;
    int32_t block = -1;          // selected Passability_Color_Settings block, -1 none; kept when its element ends
    std::string event;           // current Radar_Map_Event key (upper case), empty none
    RadarEventType scratch{};    // the Event_* scratch values ([port] start at the record defaults)
    explicit RadarWalk(RadarSettings& s) : out(s) {
        scratch.scale = 0.0125f;
        scratch.duration = 2.0f;
    }

    static std::string name_attr(const data::XmlNode& n) { return upper(n.attribute("name", n.attribute("Name"))); }

    void pass_color(const data::XmlNode& n, size_t cls) {
        const Rgba8 fallback = out.palette(block == kAllBlocks ? "DEFAULT" : kBiomeBlocks[static_cast<size_t>(block)])[cls];
        const Rgba8 c = color(n.text_trimmed(), fallback);
        auto write = [&](const std::string& key) {
            auto it = out.palettes.find(key);
            if (it == out.palettes.end()) it = out.palettes.emplace(key, out.palette(key)).first;
            it->second[cls] = c;
        };
        if (block == kAllBlocks) {
            write("DEFAULT");
            for (const char* b : kBiomeBlocks) write(b);
        } else {
            write(kBiomeBlocks[static_cast<size_t>(block)]);
        }
    }

    void field(std::string_view tag, std::string_view text) {
        if (ascii_iequals(text, "TBD")) return;  // the original only warns
        RadarEventType* cur = nullptr;
        if (!event.empty())
            if (auto it = out.events.find(event); it != out.events.end()) cur = &it->second;
        if (ascii_iequals(tag, "Event_Model_Name")) {
            if (cur) cur->model = std::string(text);
            return;
        }
        bool ev = true;
        if (ascii_iequals(tag, "Event_Model_Scale")) scratch.scale = data::parse_float_or(text, scratch.scale);
        else if (ascii_iequals(tag, "Event_Duration")) scratch.duration = data::parse_float_or(text, scratch.duration);
        else if (ascii_iequals(tag, "Event_Single_Instance")) scratch.single_instance = data::parse_bool_or(text, false);
        else if (ascii_iequals(tag, "Event_Persistent")) scratch.persistent = data::parse_bool_or(text, false);
        else ev = false;
        if (ev) {
            if (cur) {  // all four scratch values, not only the one just read
                cur->scale = scratch.scale;
                cur->duration = scratch.duration;
                cur->single_instance = scratch.single_instance;
                cur->persistent = scratch.persistent;
            }
            return;
        }
        if (ascii_iequals(tag, "Space_Backdrop_Texture_Name")) out.space_backdrop = std::string(text);
        else if (ascii_iequals(tag, "Land_Backdrop_Texture_Name")) out.land_backdrop = std::string(text);
        else if (ascii_iequals(tag, "Space_FOW_Color")) out.space_fow = color(text, out.space_fow);
        else if (ascii_iequals(tag, "Land_FOW_Color")) out.land_fow = color(text, out.land_fow);
        else if (ascii_iequals(tag, "Space_Asteroid_Field_Color")) out.space_asteroid = color(text, out.space_asteroid);
        else if (ascii_iequals(tag, "Space_Asteroid_Field_Border_Color"))
            out.space_asteroid_border = color(text, out.space_asteroid_border);
        // other table fields (the commented-out positions, category colours) are not used by the port's radar
    }

    void walk(const data::XmlNode& parent) {
        for (data::XmlNode c = parent.first_child(); c; c = c.next_sibling()) {
            const std::string_view tag = c.name();
            if (ascii_iequals(tag, "Passability_Color_Settings")) {
                const std::string b = name_attr(c);
                block = kAllBlocks;
                for (int32_t i = 0; i < kAllBlocks; ++i)
                    if (b == kBiomeBlocks[i]) block = i;
                event.clear();
            } else if (ascii_iequals(tag, "Color")) {
                const std::string cn = name_attr(c);
                for (size_t i = 0; i < kPassabilityCount; ++i)
                    if (cn == kPassNames[i]) {
                        if (block >= 0) pass_color(c, i);  // no block selected: "Unprocessed entry"
                        event.clear();
                    }
                // a terrain category colour (DEFAULT, GRASS, ...): read by the original, not used by the port's radar
            } else if (ascii_iequals(tag, "Radar_Map_Event")) {
                const std::string key = name_attr(c);
                RadarEventType t;  // the record defaults: scale 0.0125, duration 2.0, both flags false
                t.name = std::string(data::trim(c.attribute("name", c.attribute("Name"))));
                t.scale = 0.0125f;
                t.duration = 2.0f;
                out.events[key] = std::move(t);
                event = key;
            } else if (const std::string_view text = data::trim(c.text()); !text.empty() && c.child_count() == 0) {
                field(tag, text);
            }
            if (c.child_count() > 0) walk(c);
        }
    }
};

}  // namespace

bool RadarSettings::parse(const data::XmlNode& root) {
    if (!root) return false;
    RadarWalk w(*this);
    w.walk(root);
    return true;
}

bool RadarSettings::load(const FileProvider& files, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!files.read("Data\\XML\\RadarMap.xml", bytes)) {
        if (error) *error = "RadarMap.xml not found";
        return false;
    }
    const data::XmlDocument doc = data::XmlDocument::parse(bytes, "RadarMap.xml");
    if (!doc.ok()) {
        if (error) *error = "RadarMap.xml: no root";
        return false;
    }
    return parse(doc.root());
}

const RadarEventType* RadarSettings::event(std::string_view name) const {
    const auto it = events.find(upper(name));
    return it == events.end() ? nullptr : &it->second;
}

PassabilityPalette RadarSettings::palette(std::string_view biome) const {
    auto it = palettes.find(upper(biome));
    if (it == palettes.end()) it = palettes.find("DEFAULT");
    if (it != palettes.end()) return it->second;
    PassabilityPalette p;
    for (size_t i = 0; i < kPassabilityCount; ++i) {
        const auto g = static_cast<uint8_t>(96 + 20 * i);
        p[i] = Rgba8{g, g, g, 255};
    }
    return p;
}

std::vector<uint8_t> passability_rgba(std::span<const uint8_t> classes, const PassabilityPalette& palette) {
    std::vector<uint8_t> out(classes.size() * 4);
    for (size_t i = 0; i < classes.size(); ++i) {
        const Rgba8 c = palette[std::min<size_t>(classes[i], kPassabilityCount - 1)];
        out[i * 4 + 0] = c.r;
        out[i * 4 + 1] = c.g;
        out[i * 4 + 2] = c.b;
        out[i * 4 + 3] = c.a;
    }
    return out;
}

void RadarImage::build(uint32_t size, std::span<const uint8_t> terrain, uint32_t tw, uint32_t th,
                       std::span<const uint8_t> vis, uint32_t fw, uint32_t fh, Rgba8 fow, const RadarFieldPaint& fields) {
    size = std::max<uint32_t>(size, 1);
    std::vector<uint8_t> px(static_cast<size_t>(size) * size * 4, 0);
    const bool has_terrain = tw && th && terrain.size() >= static_cast<size_t>(tw) * th * 4;
    const bool has_fog = fw && fh && vis.size() >= static_cast<size_t>(fw) * fh;
    const float span_x = fields.max_x - fields.min_x, span_y = fields.max_y - fields.min_y;
    const bool has_fields = !fields.fields.empty() && span_x > 0.0f && span_y > 0.0f;
    const float px_world = has_fields ? std::max(span_x, span_y) / static_cast<float>(size) : 0.0f;  // one radar pixel
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x) {
            uint8_t* o = &px[(static_cast<size_t>(y) * size + x) * 4];
            float c[4] = {0, 0, 0, 0};
            if (has_terrain) {
                const uint32_t sx = std::min(tw - 1, static_cast<uint32_t>((static_cast<uint64_t>(x) * tw) / size));
                const uint32_t sy = std::min(th - 1, static_cast<uint32_t>((static_cast<uint64_t>(y) * th) / size));
                const uint8_t* s = &terrain[(static_cast<size_t>(sy) * tw + sx) * 4];
                for (int k = 0; k < 4; ++k) c[k] = static_cast<float>(s[k]);
            }
            if (has_fields) {  // asteroid fields: border ring (one pixel) over the fill, alpha over the background
                const float wx = fields.min_x + (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * span_x;
                const float wy = fields.min_y + (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * span_y;
                int hit = 0;  // 0 none, 1 fill, 2 border
                for (const RadarField& fl : fields.fields) {
                    const float d = std::hypot(wx - fl.x, wy - fl.y);
                    if (d > fl.radius) continue;
                    hit = d > fl.radius - px_world ? 2 : std::max(hit, 1);
                    if (hit == 2) break;
                }
                if (hit) {
                    const Rgba8 fc = hit == 2 ? fields.border : fields.fill;
                    const float a = static_cast<float>(fc.a) / 255.0f;
                    const float col[3] = {static_cast<float>(fc.r), static_cast<float>(fc.g), static_cast<float>(fc.b)};
                    for (int k = 0; k < 3; ++k) c[k] = c[3] > 0.0f ? c[k] + (col[k] - c[k]) * a : col[k];
                    c[3] = std::max(c[3], static_cast<float>(fc.a));
                }
            }
            if (has_fog) {
                const uint32_t sx = std::min(fw - 1, static_cast<uint32_t>((static_cast<uint64_t>(x) * fw) / size));
                const uint32_t sy = std::min(fh - 1, static_cast<uint32_t>((static_cast<uint64_t>(y) * fh) / size));
                const float v = static_cast<float>(vis[static_cast<size_t>(sy) * fw + sx]) / 255.0f;
                const float a = static_cast<float>(fow.a) / 255.0f * (1.0f - v);
                const float fc[3] = {static_cast<float>(fow.r), static_cast<float>(fow.g), static_cast<float>(fow.b)};
                if (has_terrain) {
                    for (int k = 0; k < 3; ++k) c[k] = c[k] + (fc[k] - c[k]) * a;
                } else if (c[3] > 0.0f) {  // space, over an asteroid field: fog over the field
                    for (int k = 0; k < 3; ++k) c[k] = c[k] + (fc[k] - c[k]) * a;
                    c[3] = std::max(c[3], 255.0f * a);
                } else {  // space: the fog itself is the picture (over the atlas backdrop)
                    for (int k = 0; k < 3; ++k) c[k] = fc[k];
                    c[3] = 255.0f * a;
                }
            }
            for (int k = 0; k < 4; ++k) o[k] = static_cast<uint8_t>(std::lround(std::clamp(c[k], 0.0f, 255.0f)));
        }
    // Image rows run top (map max y) -> bottom (map min y): flip so row 0 = north (texture v = 0 at the top).
    std::vector<uint8_t> flipped(px.size());
    const size_t row = static_cast<size_t>(size) * 4;
    for (uint32_t y = 0; y < size; ++y)
        std::copy_n(&px[static_cast<size_t>(size - 1 - y) * row], row, &flipped[static_cast<size_t>(y) * row]);
    const bool changed = flipped != px_ || w_ != size;
    px_ = std::move(flipped);
    w_ = h_ = size;
    if (changed) ++generation_;
}

uint32_t RadarEvents::add(const RadarSettings& s, std::string_view type, float x, float y) {
    const RadarEventType* t = s.event(type);
    if (t && t->single_instance)
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [&](const RadarEvent& e) { return !e.marker && upper(e.type) == upper(t->name); }),
                     items_.end());
    RadarEvent e;
    e.type = t ? t->name : std::string(type);
    e.x = x;
    e.y = y;
    e.duration = t ? t->duration : 1.0f;
    e.persistent = t && t->persistent;
    e.id = next_++;
    items_.push_back(std::move(e));
    return items_.back().id;
}

void RadarEvents::remove(uint32_t id) {
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const RadarEvent& e) { return e.id == id; }), items_.end());
}

uint32_t RadarEvents::set_marker(std::string_view name, float x, float y) {
    const std::string key = upper(name);
    for (RadarEvent& e : items_)
        if (e.marker && upper(e.type) == key) {
            e.x = x;
            e.y = y;
            return e.id;
        }
    RadarEvent e;
    e.type = std::string(data::trim(name));
    e.x = x;
    e.y = y;
    e.persistent = true;
    e.marker = true;
    e.id = next_++;
    items_.push_back(std::move(e));
    return items_.back().id;
}

bool RadarEvents::remove_marker(std::string_view name) {
    const std::string key = upper(name);
    const size_t before = items_.size();
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const RadarEvent& e) { return e.marker && upper(e.type) == key; }),
                 items_.end());
    return items_.size() != before;
}

std::vector<std::string> RadarEvents::marker_names() const {
    std::vector<std::string> out;
    for (const RadarEvent& e : items_)
        if (e.marker) out.push_back(e.type);
    return out;
}

void RadarEvents::update(float dt) {
    for (RadarEvent& e : items_) e.age += dt;
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [](const RadarEvent& e) { return !e.persistent && e.age >= e.duration; }),
                 items_.end());
}

Vec2f RadarView::to_canvas(float wx, float wy) const {
    const float nx = (wx - min_x) / (max_x - min_x), ny = (wy - min_y) / (max_y - min_y);
    return {center.x + (nx - 0.5f) * size.x, center.y + (ny - 0.5f) * size.y};
}

bool RadarView::inside(Vec2f p) const {
    return std::fabs(p.x - center.x) <= 0.5f * size.x && std::fabs(p.y - center.y) <= 0.5f * size.y;
}

Vec2f RadarView::to_world(Vec2f p) const {
    const float nx = std::clamp((p.x - center.x) / size.x + 0.5f, 0.0f, 1.0f);
    const float ny = std::clamp((p.y - center.y) / size.y + 0.5f, 0.0f, 1.0f);
    return {min_x + nx * (max_x - min_x), min_y + ny * (max_y - min_y)};
}

std::array<Vec2f, 4> camera_footprint(const View& v, float ground_z) {
    const float w = static_cast<float>(v.width), h = static_cast<float>(v.height);
    const float corners[4][2] = {{0, 0}, {w, 0}, {w, h}, {0, h}};
    std::array<Vec2f, 4> out{};
    for (int i = 0; i < 4; ++i) {
        const tactical::Ray r = screen_ray(v, corners[i][0], corners[i][1]);
        Vec3 hit;
        if (ray_plane_z(r, ground_z, hit)) {
            out[static_cast<size_t>(i)] = {hit.x, hit.y};
        } else {  // above the horizon: the far end of the ray, projected down
            const Vec3 far = v.eye + view_dir(v, corners[i][0], corners[i][1]) * v.zfar;
            out[static_cast<size_t>(i)] = {far.x, far.y};
        }
    }
    return out;
}

}  // namespace alamo::ui
