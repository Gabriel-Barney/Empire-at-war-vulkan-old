// ui4 t04: the FoC galactic planet map ("Base Layout", docs/re/GALACTIC_PLANET_MAP.md) inside GalacticHud: map_shell's
// texts and icons (§7), the land-map picture on map_window (§3), the marker pads (§4) and the ground forces in orbit
// (§5), and the drops of §6 (galactic_hud.hpp, kMapOrbitIndex).
#include <algorithm>
#include <string>

#include "alamo/data/values.hpp"
#include "alamo/ui/galactic_hud.hpp"
#include "alamo/ui/gui_dialog.hpp"
#include "ui_strings.hpp"

namespace alamo::ui {

namespace {
Vec2f map_half_extent(const CommandBarComponent& c) {
    // §2: map_window Size 512 x 512, Scale 1.25 -> 640 canvas units square.
    const float sx = c.has_size && c.size.x > 0.0f ? c.size.x : 512.0f, sy = c.has_size && c.size.y > 0.0f ? c.size.y : 512.0f;
    const float s = c.scale > 0.0f ? c.scale : 1.25f;
    return {0.5f * sx * s, 0.5f * sy * s};
}
}  // namespace

void GalacticHud::build_planet_map(const GalacticHudFrame& f) {
    const GalacticPlanetMap& m = f.planet_map;
    for (const auto* v : {&map_build_pad_, &map_unit_pad_})
        for (const int32_t ci : *v)
            if (ci >= 0) buttons_.state(static_cast<uint32_t>(ci)).hidden = true;
    map_foreign_ = map_open_ && m.foreign;
    map_surface_full_ = map_open_ && m.surface_full;
    map_planet_ = map_open_ ? m.planet : 0u;
    if (!map_open_) {
        for (const int32_t ci : land_) buttons_.state(static_cast<uint32_t>(ci)).disabled = false;  // the organiser's land pads
        return;
    }
    stats_.planet_map = true;
    auto state = [&](int32_t ci) -> ButtonRuntime* { return ci >= 0 ? &buttons_.state(static_cast<uint32_t>(ci)) : nullptr; };
    // §1.2 step 7 / §8: Planetary_Income_number and the c_filters are hidden on every open (all pads always show).
    for (const int32_t ci : map_filters_)
        if (ButtonRuntime* r = state(ci)) r->hidden = true;
    if (ButtonRuntime* r = state(map_income_)) r->hidden = true;
    // §7 texts.
    const char* keys[4] = {"TEXT_GROUND_FORCES", "TEXT_DRAG_AND_DROP", "TEXT_UNIT_CAP", "TEXT_BUILDING_CAP"};
    const char32_t* fallback[4] = {U"Ground forces in orbit", U"Drag and drop onto map", U"Unit Cap", U"Building Cap"};
    for (size_t k = 0; k < map_texts_.size() && k < 4; ++k)
        if (ButtonRuntime* r = state(map_texts_[k])) r->text = text_or(keys[k], fallback[k]);
    if (ButtonRuntime* r = state(map_back_)) r->text = text_or("TEXT_BACK_BUTTON", U"Back");
    auto ratio = [](int32_t a, int32_t b) { return b >= 0 ? num32(std::max(0, a)) + U"/" + num32(b) : std::u32string(); };
    if (ButtonRuntime* r = state(unit_cap_)) r->text = ratio(m.units, m.unit_cap);
    if (ButtonRuntime* r = state(building_cap_)) r->text = ratio(m.structures, m.structure_cap);
    const GalacticPlanetInfo* sel = selected_ >= 0 ? &f.planets[static_cast<size_t>(selected_)] : nullptr;
    if (ButtonRuntime* r = state(map_name_)) {  // the planet's display name upper-cased, in the owner's colour
        std::u32string name = sel ? sel->name : std::u32string();
        for (char32_t& ch : name)
            if (ch >= U'a' && ch <= U'z') ch = static_cast<char32_t>(ch - U'a' + U'A');
        r->text = std::move(name);
        r->text_color_set = sel != nullptr;
        if (sel) r->text_color = Rgba8{sel->owner_color.r, sel->owner_color.g, sel->owner_color.b, 255};
    }
    if (ButtonRuntime* r = state(map_allegiance_)) {  // Text_Allegiance_A's emblem alternates by owner faction
        const std::string* icon = nullptr;
        if (sel && sel->allegiance >= 0 && allegiance_ >= 0) {
            const std::vector<std::string>& alts = (*table_)[static_cast<size_t>(allegiance_)].icon_alternate_texture;
            if (static_cast<size_t>(sel->allegiance) < alts.size()) icon = &alts[static_cast<size_t>(sel->allegiance)];
        }
        r->hidden = icon == nullptr;
        r->icon_override = icon ? *icon : std::string();
    }
    if (ButtonRuntime* r = state(map_day_)) {  // as Text_Day_Counter: "%s: %d" with TEXT_CYCLES
        std::u32string t = text_or("TEXT_CYCLES", U"Day");
        while (!t.empty() && t.back() == U' ') t.pop_back();
        if (!t.empty() && t.back() != U':') t.push_back(U':');
        r->text = f.day >= 0 ? t + U" " + num32(f.day) : std::u32string();
    }
    if (ButtonRuntime* r = state(map_weather_)) {
        r->hidden = m.weather_icon.empty();
        r->icon_override = m.weather_icon;
    }
    if (ButtonRuntime* r = state(map_bonus_)) {
        r->hidden = m.ability_icon.empty();
        r->icon_override = m.ability_icon;
    }
    // §3: the land-map picture fills map_window (tint 200, 200, 200).
    if (map_window_ < 0) return;
    const ComponentPlacement& wp = layout_.placement(static_cast<uint32_t>(map_window_));
    if (!wp.placed || !wp.from_bone) return;
    const CommandBarComponent& wc = (*table_)[static_cast<size_t>(map_window_)];
    const Vec2f half = map_half_extent(wc);
    const uint32_t wlayer = layer_of(wc, wp);
    GuiImage img;
    if (m.images && *m.images) {
        if (!m.picture.empty()) img = (*m.images)(m.picture);
        if (!img.valid) img = (*m.images)("maptemp.dds");
    }
    const Rgba8 tint = wc.has_color ? wc.color : Rgba8{200, 200, 200, 255};
    if (img.valid) {
        HudQuad q;
        q.min = {wp.center.x - half.x, wp.center.y - half.y};
        q.max = {wp.center.x + half.x, wp.center.y + half.y};
        q.uv0 = img.uv0;
        q.uv1 = img.uv1;
        q.page = img.page;
        q.color = tint;
        dl_.add(wlayer, HudList::Alpha, q);
        stats_.map_picture = true;
    }
    hit(map_window_, wlayer, wp.center, {2.0f * half.x, 2.0f * half.y});
    {
        PadSlot ps;  // the drop target of an orbit stack (§6.2 rule 1)
        ps.component = map_window_;
        pads_.push_back(std::move(ps));
    }
    // §4.3: pad = map_window centre + (half.x u, half.y v), u = 2 (x - 0.5), v = 2 (y - 0.5) [port: .ted y up].
    for (const GalacticMapPad& gp : m.pads) {
        const std::vector<int32_t>& group = gp.unit_pad ? map_unit_pad_ : map_build_pad_;
        if (gp.index < 0 || static_cast<size_t>(gp.index) >= group.size()) continue;
        const int32_t ci = group[static_cast<size_t>(gp.index)];
        if (ci < 0) continue;
        const auto u = static_cast<uint32_t>(ci);
        const CommandBarComponent& c = (*table_)[u];
        const Vec2f at{wp.center.x + half.x * 2.0f * (gp.uv.x - 0.5f), wp.center.y + half.y * 2.0f * (gp.uv.y - 0.5f)};
        ButtonRuntime& r = buttons_.state(u);
        r.hidden = false;
        r.icon_override = gp.icon;
        r.alt = gp.alt;
        // §4.6: disabled when empty (or holding a structure type) or in foreign view.
        r.disabled = m.foreign || gp.object == 0;
        r.tint = gp.dimmed ? Rgba8{255, 255, 255, 192} : Rgba8{255, 255, 255, 255};
        const uint32_t layer = std::max(layer_of(c, layout_.placement(u)), wlayer + 1);
        if (gp.shield > 0.0f) {  // §4.4: the shield's reach drawn on the map
            const float d = gp.shield * 2.0f * half.x;
            add_atlas_quad(dl_, layer, HudList::Alpha, *atlas_, "shield_range.tga", at, {d, d}, 1.0f, Rgba8{255, 255, 255, 255});
            ++stats_.map_shields;
        }
        const Vec2f sz = buttons_.draw(dl_, *atlas_, fonts_, canvas_, u, at, {}, f.faction_alt, f.time,
                                       static_cast<int32_t>(layer) - c.base_layer);
        hit(ci, layer, at, sz.x > 0.0f ? sz : component_size(u, layout_.placement(u)));
        PadSlot ps;
        ps.component = ci;
        ps.fleet_index = gp.unit_pad ? kMapUnitPadIndex : kMapBuildPadIndex;
        ps.unit = gp.object;
        ps.icon = gp.icon;
        ps.foreign = m.foreign;
        ps.map_tag = gp.tag;
        // §6.1: an enabled pad holding an object can be dragged. [port] build pads only take part in rule 5, which needs
        // SetMarkerID (no sim handler yet): not draggable until then.
        ps.draggable = gp.unit_pad && !m.foreign && gp.object != 0 && data::parse_flag_char(c.raw_value("DRAG_AND_DROP"));
        pads_.push_back(std::move(ps));
        ++stats_.map_pads;
    }
    // §5: the ground forces in orbit on land_fleet_00..19 (disabled while the surface is full or in foreign view).
    for (size_t k = 0; k < land_.size(); ++k) {
        const auto u = static_cast<uint32_t>(land_[k]);
        ButtonRuntime& r = buttons_.state(u);
        const ComponentPlacement& p = layout_.placement(u);
        if (k >= m.orbit.size() || !p.placed || !p.from_bone) {
            r.hidden = true;
            continue;
        }
        const GalacticMapStack& st = m.orbit[k];
        const CommandBarComponent& c = (*table_)[u];
        r.hidden = false;
        r.icon_override = st.icon;
        r.text = st.count > 1 ? num32(st.count) : std::u32string();
        r.disabled = m.foreign || m.surface_full;
        r.selected = false;
        const uint32_t layer = layer_of(c, p);
        const Vec2f sz = buttons_.draw(dl_, *atlas_, fonts_, canvas_, u, p.center, p.size, f.faction_alt, f.time,
                                       static_cast<int32_t>(layer) - c.base_layer);
        hit(land_[k], layer, p.size.x > 0 ? p.rect_center : p.center, p.size.x > 0 ? p.size : (sz.x > 0 ? sz : component_size(u, p)));
        PadSlot ps;
        ps.component = land_[k];
        ps.fleet_index = kMapOrbitIndex;
        ps.fleet = st.fleet;
        ps.unit = st.unit;
        ps.icon = st.icon;
        ps.foreign = m.foreign;
        ps.draggable = !r.disabled && st.unit != 0 && data::parse_flag_char(c.raw_value("DRAG_AND_DROP"));
        pads_.push_back(std::move(ps));
        ++stats_.map_stacks;
    }
}

bool GalacticHud::map_drop(std::vector<commands::EventData>* out) const {
    constexpr uint32_t kNone = commands::kNoObject;
    const auto emit = [out](commands::EventData e) {
        if (out) out->push_back(std::move(e));
        return true;
    };
    if (!map_open_ || map_foreign_ || map_planet_ == 0) return false;
    const PadSlot* over = pad_of(drag_.over_component);
    if (!over || drag_.unit == 0) return false;
    // A launch into a new fleet at this planet (§6.2 rules 2 and 4: op 4 launch + op 0 form, one command here).
    const auto launch = [&](uint32_t unit) {
        return emit(fleet_management_event(commands::kFleetMoveNew, map_planet_, kNone, unit, map_planet_));
    };
    if (drag_.fleet_index == kMapOrbitIndex) {  // rules 1 and 2: land the stack's representative
        const bool to_pad = over->fleet_index == kMapUnitPadIndex;
        if (over->component != map_window_ && !to_pad) return false;
        bool any = false;
        if (!map_surface_full_) any = emit(fleet_management_event(commands::kFleetLand, map_planet_, drag_.fleet, drag_.unit));
        if (to_pad && over->unit != 0) any = launch(over->unit) || any;  // the pad's occupant goes to orbit
        return any;
    }
    if (drag_.fleet_index == kMapUnitPadIndex && over->fleet_index == kMapOrbitIndex) return launch(drag_.unit);  // rule 4
    return false;  // rules 3 and 5 move markers only (SetMarkerID): [port] not yet
}

}  // namespace alamo::ui
