// ref5 t04: ship names (ship_names.hpp; docs/re/MCX8_BINDINGS_OBJECTS.md §"Ship names").
#include "alamo/sim/ship_names.hpp"

#include <algorithm>

#include "alamo/objects/behaviors.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/objects/object_types.hpp"

namespace alamo::sim {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto lo = [](char ch) { return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch; };
        if (lo(a[i]) != lo(b[i])) return false;
    }
    return true;
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// UTF-16 code units of one line to UTF-8 (a lone surrogate becomes U+FFFD).
std::string utf8_of(const std::vector<uint16_t>& units) {
    std::string out;
    for (size_t i = 0; i < units.size(); ++i) {
        uint32_t cp = units[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (units[i + 1] - 0xDC00u);
            ++i;
        } else if (cp >= 0xD800 && cp < 0xE000) {
            cp = 0xFFFD;
        }
        append_utf8(out, cp);
    }
    return out;
}

// [port] the shuffle's generator: splitmix32 over the seed (the synced state, read only).
struct ShuffleRng {
    uint32_t s;
    uint32_t next() {
        uint32_t z = (s += 0x9E3779B9u);
        z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
        z = (z ^ (z >> 13)) * 0xC2B2AE35u;
        return z ^ (z >> 16);
    }
};

}  // namespace

std::vector<std::string> ShipNames::parse_list(const std::vector<uint8_t>& bytes) {
    std::vector<std::string> out;
    std::vector<uint16_t> line;
    const size_t units = bytes.size() / 2;
    for (size_t i = 1; i < units; ++i) {  // the first code unit is skipped (BOM assumed)
        const auto u = static_cast<uint16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8));
        if (u == u'\n') {
            if (!line.empty()) out.push_back(utf8_of(line));
            line.clear();
        } else if (u != u'\r' && u != 0) {
            line.push_back(u);
        }
    }
    if (!line.empty()) out.push_back(utf8_of(line));
    return out;
}

std::string ShipNames::roman(uint32_t n) {
    if (n == 0 || n > 3999) return {};
    static constexpr struct {
        uint32_t v;
        const char* s;
    } kDigits[] = {{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"}, {50, "L"},
                   {40, "XL"},  {10, "X"},   {9, "IX"},  {5, "V"},    {4, "IV"},  {1, "I"}};
    std::string out;
    for (const auto& d : kDigits)
        while (n >= d.v) {
            out += d.s;
            n -= d.v;
        }
    return out;
}

void ShipNames::load(std::string_view spec, const objects::GameObjectTypes& types, const ReadFile& read,
                     std::vector<std::string>* log) {
    lists_.clear();
    by_type_.clear();
    names_.clear();
    shuffled_ = false;
    std::vector<std::string> tokens;
    std::string cur;
    for (const char ch : spec) {
        if (ch == ' ' || ch == ',' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (!cur.empty()) tokens.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) tokens.push_back(std::move(cur));
    if (tokens.size() % 2 != 0) {
        if (log) log->push_back("Missing string value mapping type " + tokens.back() + " in type, string list ShipNameTextFiles");
        tokens.pop_back();
    }
    for (size_t i = 0; i + 1 < tokens.size(); i += 2) {
        const std::string& type_name = tokens[i];
        const std::string& file = tokens[i + 1];
        const objects::TypeIndex t = iequals(type_name, "None") ? objects::kNone : types.raw().find(type_name);
        std::optional<uint32_t> list;
        for (uint32_t k = 0; k < lists_.size() && !list; ++k)
            if (iequals(lists_[k].file, file)) list = k;
        if (!list) {
            const std::optional<std::vector<uint8_t>> bytes = read ? read(file) : std::nullopt;
            std::vector<std::string> names = bytes ? parse_list(*bytes) : std::vector<std::string>{};
            if (names.empty()) {
                continue;  // no list: the type gets no name
            }
            lists_.push_back({file, std::move(names), 0, 0});
            list = static_cast<uint32_t>(lists_.size() - 1);
        }
        if (t != objects::kNone) by_type_[t] = *list;  // the same type twice: the later pair wins
    }
}

void ShipNames::start_game(bool new_session) {
    shuffled_ = false;
    for (List& l : lists_) l.cursor = l.wrap = 0;
    if (new_session) names_.clear();
}

std::string ShipNames::pick(objects::TypeIndex type, uint32_t seed) {
    const auto it = by_type_.find(type);
    if (it == by_type_.end()) return {};
    if (!shuffled_) {
        shuffled_ = true;
        ShuffleRng rng{seed};
        for (List& l : lists_)
            for (size_t i = l.names.size(); i > 1; --i) std::swap(l.names[i - 1], l.names[rng.next() % i]);
    }
    List& l = lists_[it->second];
    if (l.names.empty()) return {};
    std::string out = "\"" + l.names[l.cursor];
    if (l.wrap > 0) out += " " + roman(l.wrap);
    out += "\"";
    if (++l.cursor >= l.names.size()) {
        l.cursor = 0;
        ++l.wrap;
    }
    return out;
}

void ShipNames::service(const objects::ObjectStore& store, const objects::ObjectStore* parent, uint32_t seed) {
    if (lists_.empty()) return;
    for (const objects::ObjectHandle h : store.all_objects()) {
        if (!store.alive(h) || !store.has_behavior(h, objects::beh::Selectable)) continue;
        const objects::ObjectId id = store.id(h);
        if (names_.count(id)) continue;
        std::string name;
        const objects::ObjectId pid = store.parent_mode_id(h);
        if (pid != objects::kNoObjectId && parent && parent->exists(parent->find(pid))) {
            const auto it = names_.find(pid);
            if (it != names_.end()) name = it->second;
        }
        if (name.empty()) name = pick(store.type(h), seed);
        names_.emplace(id, std::move(name));
    }
}

std::string_view ShipNames::name(objects::ObjectId id) const {
    const auto it = names_.find(id);
    return it == names_.end() ? std::string_view() : std::string_view(it->second);
}

void ShipNames::write(ByteWriter& w) const {
    w.u32(static_cast<uint32_t>(names_.size()));
    for (const auto& [id, n] : names_) {
        w.i32(id);
        w.str(n);
    }
    w.u32(static_cast<uint32_t>(lists_.size()));
    for (const List& l : lists_) {
        w.str(l.file);
        w.u32(l.cursor);
        w.u32(l.wrap);
        w.u32(static_cast<uint32_t>(l.names.size()));
        for (const std::string& n : l.names) w.str(n);
    }
    w.boolean(shuffled_);
}

bool ShipNames::read(ByteReader& r) {
    uint32_t n = 0;
    if (!r.count(n, 8)) return false;
    std::map<objects::ObjectId, std::string> names;
    for (uint32_t i = 0; i < n; ++i) {
        const objects::ObjectId id = r.i32();
        names[id] = r.str();
    }
    uint32_t lists = 0;
    if (!r.ok() || !r.count(lists, 16) || lists != lists_.size()) return false;
    std::vector<List> loaded(lists);
    for (List& l : loaded) {
        l.file = r.str();
        l.cursor = r.u32();
        l.wrap = r.u32();
        uint32_t k = 0;
        if (!r.count(k, 4)) return false;
        for (uint32_t j = 0; j < k; ++j) l.names.push_back(r.str());
        if (!r.ok() || l.names.empty() || l.cursor >= l.names.size()) return false;
    }
    const bool shuffled = r.boolean();
    if (!r.ok() || !r.done()) return false;
    for (size_t i = 0; i < loaded.size(); ++i)
        if (!iequals(loaded[i].file, lists_[i].file)) return false;
    lists_ = std::move(loaded);
    names_ = std::move(names);
    shuffled_ = shuffled;
    return true;
}

}  // namespace alamo::sim
