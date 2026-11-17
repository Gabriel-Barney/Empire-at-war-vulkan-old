// ui4 t04: planet map layout rules (planet_map.hpp, docs/re/GALACTIC_PLANET_MAP.md §4-5).
#include "alamo/game/planet_map.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace alamo::game {

namespace {

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

bool lists(const PlanetMapPad& p, uint32_t type) {
    return type != kNoMapType && std::find(p.specific.begin(), p.specific.end(), type) != p.specific.end();
}

bool empty_pad(const PlanetMapPad& p) { return p.type == kNoMapType && p.object == 0; }

}  // namespace

MapMarkerClass classify_map_marker(std::string_view name, bool has_specific) {
    if (iequal(name, "OUT BASE SPECIAL STRUCTURE POSITION")) return MapMarkerClass::Out;
    if (iequal(name, "IN BASE SPECIAL STRUCTURE POSITION")) return MapMarkerClass::In;
    if (iequal(name, "DEFENDING FORCES POSITION")) return MapMarkerClass::Defender;
    if (has_specific) return MapMarkerClass::Specific;
    constexpr std::string_view kReinforce = "REINFORCEMENT_POINT";  // 19-character prefix
    if (name.size() >= kReinforce.size() && iequal(name.substr(0, kReinforce.size()), kReinforce)) return MapMarkerClass::Reinforcement;
    if (iequal(name, "GARRISON_BUNKER_CAPTURABLE")) return MapMarkerClass::Bunker;
    if (iequal(name, "SKIRMISH_BUILD_PAD")) return MapMarkerClass::BuildPad;
    return MapMarkerClass::None;
}

std::vector<PlanetMapPad> planet_map_pads(std::span<const PlanetMapMarker> markers) {
    std::vector<PlanetMapPad> out;
    auto fill = [&](MapPadGroup group, int32_t first, int32_t count, std::initializer_list<MapMarkerClass> classes) {
        int32_t next = first;
        for (const MapMarkerClass cls : classes)
            for (const PlanetMapMarker& m : markers) {
                if (m.cls != cls || next >= first + count) continue;
                PlanetMapPad p;
                p.group = group;
                p.index = next++;
                p.marker = m.id;
                p.cls = cls;
                p.tag = cls == MapMarkerClass::In || cls == MapMarkerClass::Out ? 1 : cls == MapMarkerClass::Specific ? 3 : 0;
                p.x = m.x;
                p.y = m.y;
                p.specific = m.specific;
                out.push_back(std::move(p));
            }
    };
    fill(MapPadGroup::Build, 0, 33, {MapMarkerClass::In, MapMarkerClass::Specific, MapMarkerClass::Out});
    fill(MapPadGroup::Build, 33, 7, {MapMarkerClass::Reinforcement});
    fill(MapPadGroup::Build, 40, 40, {MapMarkerClass::Bunker, MapMarkerClass::BuildPad});
    fill(MapPadGroup::Unit, 0, kMapUnitPads, {MapMarkerClass::Defender});
    return out;
}

void place_map_structures(std::vector<PlanetMapPad>& pads, std::span<const PlanetMapStructure> structures, const MapShieldRadius& shield,
                          std::vector<MapMarkerSend>* sends) {
    // Step 1: Place_Other_Type_At_Every_Specific_Marker_Position.
    for (const PlanetMapStructure& s : structures) {
        if (s.place_other == kNoMapType) continue;
        for (PlanetMapPad& p : pads)
            if (p.cls == MapMarkerClass::Specific && empty_pad(p) && lists(p, s.place_other)) {
                p.type = s.type;
                p.holds_type = true;
            }
    }
    // Step 2: the other structures' types, then each additional type once, on the first free Specific pad listing it.
    auto specific_pad = [&](uint32_t type) -> PlanetMapPad* {
        for (PlanetMapPad& p : pads)
            if (p.cls == MapMarkerClass::Specific && empty_pad(p) && lists(p, type)) return &p;
        return nullptr;
    };
    auto put_type = [&](PlanetMapPad& p, uint32_t type) {
        p.type = type;
        p.holds_type = true;
        p.dimmed = true;
        p.shield_radius = shield ? std::max(0.0f, shield(type)) : 0.0f;
    };
    std::vector<uint8_t> placed(structures.size(), 0);
    for (size_t i = 0; i < structures.size(); ++i) {
        const PlanetMapStructure& s = structures[i];
        if (s.place_other != kNoMapType) {
            placed[i] = 1;  // shown on the Specific pads of step 1 only
            continue;
        }
        if (PlanetMapPad* p = specific_pad(s.type)) {
            put_type(*p, s.type);
            placed[i] = 1;
        }
    }
    std::vector<uint32_t> extra;
    for (const PlanetMapStructure& s : structures)
        for (const uint32_t a : s.additional)
            if (a != kNoMapType && std::find(extra.begin(), extra.end(), a) == extra.end()) extra.push_back(a);
    for (const uint32_t a : extra)
        if (PlanetMapPad* p = specific_pad(a)) put_type(*p, a);
    // Step 3: by Base_Position.
    auto in_group = [](const PlanetMapPad& p, uint8_t base) {
        return p.tag == 1 && ((base == 1 && p.cls == MapMarkerClass::In) || (base == 2 && p.cls == MapMarkerClass::Out));
    };
    auto put_object = [sends](PlanetMapPad& p, const PlanetMapStructure& s) {
        p.object = s.object;
        p.type = s.type;
        if (sends && s.marker != p.marker) sends->push_back({s.object, p.marker});
    };
    for (size_t i = 0; i < structures.size(); ++i) {  // a remembered marker first
        const PlanetMapStructure& s = structures[i];
        if (placed[i] || s.marker < 0 || (s.base != 1 && s.base != 2)) continue;
        for (PlanetMapPad& p : pads)
            if (in_group(p, s.base) && empty_pad(p) && p.marker == s.marker) {
                put_object(p, s);
                placed[i] = 1;
                break;
            }
    }
    for (size_t i = 0; i < structures.size(); ++i) {
        const PlanetMapStructure& s = structures[i];
        if (placed[i] || (s.base != 1 && s.base != 2)) continue;
        for (const uint8_t base : {s.base, static_cast<uint8_t>(s.base == 1 ? 2 : 1)}) {
            for (PlanetMapPad& p : pads)
                if (in_group(p, base) && empty_pad(p)) {
                    put_object(p, s);
                    placed[i] = 1;
                    break;
                }
            if (placed[i]) break;
        }
    }
}

void place_map_units(std::vector<PlanetMapPad>& pads, std::span<const uint32_t> units, std::span<const uint32_t> types,
                     std::span<const int32_t> markers, std::vector<MapMarkerSend>* sends) {
    auto type_of = [&](size_t i) { return i < types.size() ? types[i] : kNoMapType; };
    auto marker_of = [&](size_t i) { return i < markers.size() ? markers[i] : -1; };
    auto put = [&](PlanetMapPad& p, size_t i) {
        p.object = units[i];
        p.type = type_of(i);
        if (sends && marker_of(i) != p.marker) sends->push_back({units[i], p.marker});
    };
    std::vector<uint8_t> placed(units.size(), 0);
    std::vector<size_t> waiting;
    for (size_t i = 0; i < units.size(); ++i) {  // 1. a remembered marker
        if (marker_of(i) < 0) continue;
        for (PlanetMapPad& p : pads)
            if (p.group == MapPadGroup::Unit && empty_pad(p) && p.marker == marker_of(i)) {
                put(p, i);
                placed[i] = 1;
                break;
            }
        if (!placed[i]) waiting.push_back(i);
    }
    std::vector<size_t> order;  // 2. no marker, then the waiting ones
    for (size_t i = 0; i < units.size(); ++i)
        if (!placed[i] && marker_of(i) < 0) order.push_back(i);
    order.insert(order.end(), waiting.begin(), waiting.end());
    size_t next = 0;
    for (PlanetMapPad& p : pads) {
        if (p.group != MapPadGroup::Unit || !empty_pad(p)) continue;
        if (next >= order.size()) break;
        put(p, order[next]);
        placed[order[next]] = 1;
        ++next;
    }
    if (sends)  // a waiting unit with no pad keeps the -1 it was sent
        for (const size_t i : waiting)
            if (!placed[i]) sends->push_back({units[i], -1});
}

std::vector<MapOrbitStack> map_orbit_stacks(std::span<const MapOrbitUnit> units) {
    std::vector<MapOrbitStack> out;
    for (const MapOrbitUnit& u : units) {
        auto it = std::find_if(out.begin(), out.end(), [&](const MapOrbitStack& s) { return s.type == u.type; });
        if (it != out.end()) {
            ++it->count;
            continue;
        }
        out.push_back({u.type, u.unit, u.fleet, 1});
    }
    std::stable_sort(out.begin(), out.end(), [](const MapOrbitStack& a, const MapOrbitStack& b) { return a.type < b.type; });
    if (out.size() > static_cast<size_t>(kMapOrbitStacks)) out.resize(static_cast<size_t>(kMapOrbitStacks));
    return out;
}

}  // namespace alamo::game
