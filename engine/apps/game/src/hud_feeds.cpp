#include "alamo/game/hud_feeds.hpp"
#ifdef ALAMO_GAME_HAVE_CONTROLS
#include <algorithm>
#include <cmath>

#include "alamo/galactic/economy.hpp"
#include "alamo/objects/type_fields.hpp"

namespace alamo::game {

float day_cycle_fraction(int32_t mode_frame, int32_t cycle_start, int32_t cycle_frames) {
    if (cycle_frames <= 0) return -1.0f;
    const float f = static_cast<float>(mode_frame - cycle_start) / static_cast<float>(cycle_frames);
    return std::clamp(f, 0.0f, 1.0f);
}

float galactic_cycle_fraction(const galactic::GalacticSim& g) {
    return day_cycle_fraction(g.frame(), g.last_cycle_frame(), galactic::econ::fiscal_period_frames(g.rules().constants.fiscal_cycle_seconds));
}

ui::HudOrderButtons order_buttons(std::span<const OrderUnitState> selection, int32_t local_player, bool controls_locked) {
    ui::HudOrderButtons b;
    b.controls_locked = controls_locked;
    if (controls_locked) return b;  // the scan is skipped (HUD_GAPS §5.1)
    for (const OrderUnitState& u : selection) {
        if (u.owner != local_player || local_player < 0) continue;  // enemy / allied units in the selection are ignored
        b.can_attack = b.can_attack || u.can_attack;
        b.can_move = b.can_move || u.can_move;
    }
    return b;
}

ui::HudBombingRunButton bombing_run_button(bool land, bool controls_locked, bool available, float fraction) {
    ui::HudBombingRunButton b;
    if (!land) return b;
    b.present = true;
    const bool ready = !controls_locked && available;
    b.disabled = !ready;
    if (!ready) b.dial = fraction > 0.0f ? std::min(fraction, 1.0f) : 1.0f;
    return b;
}

ui::HudBombingRunButton space_weapon_button(bool space, int32_t icon, bool allowed, bool controls_locked, float readiness) {
    ui::HudBombingRunButton b;
    if (!space) return b;
    b.present = true;
    b.icon = icon;
    b.hidden = icon < 0;
    b.tooltip = allowed ? icon : 4;
    const bool ready = icon >= 0 && allowed && !controls_locked && readiness >= 1.0f;
    b.disabled = !ready;
    if (!ready && icon >= 0) b.dial = allowed ? std::clamp(readiness, 0.0f, 1.0f) : 0.0f;
    return b;
}

BombingRunStyle bombing_run_style(const galactic::FactionRules& faction) {
    BombingRunStyle s;
    s.blob_material = std::string(faction.xml_text("Bombing_Run_Shadow_Blob_Material_Name"));
    if (const std::string_view v = faction.xml_text("Bombing_Run_Blob_Size"); !v.empty()) s.blob_size = static_cast<float>(objects::c_atof(v));
    return s;
}

const char* hardpoint_type_name(uint8_t kind) {
    static const char* const kNames[] = {"HARD_POINT_ENGINE",        "HARD_POINT_SHIELD_GENERATOR", "HARD_POINT_GRAVITY_WELL",
                                         "HARD_POINT_FIGHTER_BAY",   "HARD_POINT_TRACTOR_BEAM",     "HARD_POINT_WEAPON_LASER",
                                         "HARD_POINT_WEAPON_MISSILE", "HARD_POINT_WEAPON_TORPEDO",  "HARD_POINT_WEAPON_ION_CANNON",
                                         "HARD_POINT_WEAPON_MASS_DRIVER", "HARD_POINT_WEAPON_SPECIAL", "HARD_POINT_ENABLE_SPECIAL_ABILITY",
                                         "HARD_POINT_DUMMY_ART"};
    return kind < std::size(kNames) ? kNames[kind] : "";
}

ui::Vec3 hardpoint_world(ui::Vec3 position, ui::Vec3 facing, ui::Vec3 offset) {
    // Yaw only (ships bank / pitch little; presentation): forward = the facing's ground direction, left = +90 deg.
    float fx = facing.x, fy = facing.y;
    const float len = std::sqrt(fx * fx + fy * fy);
    if (len > 1e-6f) {
        fx /= len;
        fy /= len;
    } else {
        fx = 1.0f;
        fy = 0.0f;
    }
    ui::Vec3 out = position;
    out.x += fx * offset.x - fy * offset.y;
    out.y += fy * offset.x + fx * offset.y;
    out.z += offset.z;
    return out;
}

std::vector<ui::HeroEntry> galactic_hero_entries(const galactic::GalacticSim& g, int32_t player,
                                                 const std::function<std::string(uint32_t type)>& icon,
                                                 std::span<const uint32_t> selection, size_t max_entries,
                                                 const std::function<int32_t(uint32_t type)>& ranking) {
    std::vector<ui::HeroEntry> out;
    if (player < 0) return out;
    const objects::ObjectStore& st = g.store();
    auto selected = [&selection](uint32_t id) { return std::binary_search(selection.begin(), selection.end(), id); };
    std::vector<uint32_t> seen;  // one entry per hero type (§3.2 step 1)
    // Is_Named_Hero of the type, or of a company's first member (heroes travel as *_Team companies).
    auto named = [&](uint32_t t) { return is_named_hero(g, t); };
    auto keys = [&](ui::HeroEntry& e, uint32_t t) {  // §4 sort keys
        const uint32_t bar = hero_bar_type(g.rules(), t);
        e.ranking = ranking ? ranking(bar) : 0;
        e.type_name = bar < g.rules().types.size() ? g.rules().types[bar].name : std::string();
    };
    // FIXPASS_GALACTIC §2.2 click targets: planet = the planet up the hero's parent chain (an orbiting fleet's planet, the
    // surface's planet; kNoObject in hyperspace), container = its fleet (kNoObject on a surface).
    auto add_object = [&](objects::ObjectHandle h, uint32_t container_id, uint32_t planet_id) {
        // Own heroes only; galactic stealth (hidden(), e.g. Boba Fett) hides them from enemies, not from their owner's roster.
        if (out.size() >= max_entries || !st.alive(h) || st.owner(h) != player) return;
        const uint32_t t = static_cast<uint32_t>(st.type(h));
        if (!named(t) || std::find(seen.begin(), seen.end(), t) != seen.end()) return;
        std::string ic = icon ? icon(t) : std::string();
        if (ic.empty()) return;
        seen.push_back(t);
        ui::HeroEntry e;
        e.object = static_cast<uint32_t>(st.id(h));
        e.icon = std::move(ic);
        e.selected = selected(e.object) || (container_id != 0 && selected(container_id));
        e.planet = planet_id;
        e.container = container_id != 0 ? container_id : commands::kNoObject;
        keys(e, t);
        out.push_back(std::move(e));
    };
    auto planet_object = [&](PlanetIndex pi) {
        return pi < g.planets().size() && st.exists(g.planets()[pi].object) ? static_cast<uint32_t>(st.id(g.planets()[pi].object))
                                                                             : commands::kNoObject;
    };
    // Fleets (orbit and transit), then planet surfaces, in index order [port: the original's sort key is unread].
    for (const galactic::FleetState& fl : g.fleets()) {
        if (!fl.alive || fl.owner != player) continue;
        const uint32_t fid = st.exists(fl.object) ? static_cast<uint32_t>(st.id(fl.object)) : 0u;
        const uint32_t at = fl.moving ? commands::kNoObject : planet_object(fl.planet);
        for (const objects::ObjectHandle m : fl.contents) add_object(m, fid, at);
    }
    for (PlanetIndex pi = 0; pi < g.planets().size(); ++pi)
        for (const objects::ObjectHandle m : g.planets()[pi].surface) add_object(m, 0, planet_object(pi));
    // §3.2 step 2: named heroes being built (queue entries; the head's timer gives the dial) or awaiting respawn.
    const int32_t now = g.frame();
    const float respawn_frames = 30.0f * g.rules().special.default_hero_respawn_time;
    for (PlanetIndex pi = 0; pi < g.planets().size() && out.size() < max_entries; ++pi) {
        for (uint32_t q = 0; q < 2 && out.size() < max_entries; ++q) {
            const std::span<const galactic::BuildEntry> queue = g.queue_of(pi, q);
            for (size_t i = 0; i < queue.size() && out.size() < max_entries; ++i) {
                const galactic::BuildEntry& b = queue[i];
                if (b.player != player || !named(b.type)) continue;
                std::string ic = icon ? icon(b.type) : std::string();
                if (ic.empty()) continue;
                ui::HeroEntry e;
                e.icon = std::move(ic);
                e.respawn = (i == 0 && b.duration > 0) ? std::clamp(static_cast<float>(now - b.start) / static_cast<float>(b.duration), 0.0f, 1.0f) : 0.0f;
                keys(e, b.type);
                out.push_back(std::move(e));
            }
        }
        for (const galactic::RespawnEntry& r : g.respawns_pending(pi)) {
            if (out.size() >= max_entries) break;
            if (r.owner != player || !named(r.type)) continue;
            std::string ic = icon ? icon(r.type) : std::string();
            if (ic.empty()) continue;
            ui::HeroEntry e;
            e.icon = std::move(ic);
            // [port] the respawn list keeps only the due frame: start = due - Default_Hero_Respawn_Time.
            e.respawn = respawn_frames > 0.0f
                            ? std::clamp(1.0f - static_cast<float>(r.due - now) / respawn_frames, 0.0f, 1.0f)
                            : 0.0f;
            keys(e, r.type);
            out.push_back(std::move(e));
        }
    }
    return out;
}

bool is_named_hero(const galactic::GalacticSim& g, uint32_t type) {
    return type < g.rules().types.size() && (g.rules().types[type].named_hero || g.rules().special.type(type).named_hero);
}

uint32_t hero_bar_type(const galactic::GalacticRules& rules, uint32_t type) {
    if (type >= rules.types.size()) return type;
    const auto named = [&rules](uint32_t t) { return t < rules.types.size() && rules.types[t].named_hero; };
    const auto& company = rules.types[type];
    if (named(company.company_transport)) return company.company_transport;
    if (!company.company_units.empty() && named(company.company_units.front())) return company.company_units.front();
    return type;
}

std::vector<objects::ObjectHandle> conflict_units(const galactic::GalacticSim& g, const galactic::ConflictInfo& c, int32_t player) {
    std::vector<objects::ObjectHandle> out;
    const objects::ObjectStore& st = g.store();
    if (c.planet >= g.planets().size() || player < 0) return out;
    const galactic::PlanetState& p = g.planets()[c.planet];
    if (c.kind == galactic::ConflictKind::Space) {
        for (const uint32_t fi : g.fleets_at(c.planet, player))
            for (const objects::ObjectHandle m : g.fleets()[fi].contents)
                if (st.alive(m)) out.push_back(m);
        if (p.starbase_owner == player && st.alive(p.starbase)) out.push_back(p.starbase);
    } else if (player == c.attacker) {
        for (const objects::ObjectHandle m : c.units)
            if (st.alive(m)) out.push_back(m);
    } else {
        for (const objects::ObjectHandle m : p.surface)
            if (st.alive(m) && st.owner(m) == player) out.push_back(m);
    }
    return out;
}

BattleForceSummary battle_force_summary(const galactic::GalacticSim& g, std::span<const objects::ObjectHandle> units,
                                        const std::function<std::string(uint32_t type)>& icon, uint32_t ships_per_stack) {
    BattleForceSummary out;
    const objects::ObjectStore& st = g.store();
    std::string lead;
    for (const objects::ObjectHandle h : units) {
        if (!st.alive(h)) continue;
        ++out.units;
        const uint32_t t = static_cast<uint32_t>(st.type(h));
        if (out.hero) continue;
        std::string ic = icon ? icon(t) : std::string();
        if (ic.empty()) continue;
        if (is_named_hero(g, t)) {
            out.icon = std::move(ic);
            out.hero = true;
        } else if (lead.empty()) {
            lead = std::move(ic);
        }
    }
    if (!out.hero) out.icon = std::move(lead);
    const uint32_t per = ships_per_stack ? ships_per_stack : 1u;
    out.stacks = std::min(5u, (out.units + per - 1u) / per);
    return out;
}

std::string_view commander_icon_tag(bool land) {
    if (land) return "Infantry_Icon_Name";  // ledger: consumes Faction.Infantry_Icon_Name
    return "Fighter_Icon_Name";             // ledger: consumes Faction.Fighter_Icon_Name
}

void apply_commander_icon(BattleForceSummary& s, std::string_view faction_icon) {
    if (!s.hero && !faction_icon.empty()) s.icon = std::string(faction_icon);
}

int32_t ar_result_magnitude(bool win, uint32_t n_own, uint32_t n_other) {
    const uint64_t a = win ? n_other : n_own, b = win ? n_own : n_other;
    return a > 3u * b ? 0 : a > b ? 1 : 2;
}

ui::HudBuildOption::Block galactic_build_block(commands::RejectReason why) {
    using B = ui::HudBuildOption::Block;
    if (why == commands::RejectReason::None) return B::None;
    if (why == commands::RejectReason::QueueFull) return B::QueueFull;
    if (why == galactic::kRejectPopulationCap) return B::PopCap;
    if (why == commands::RejectReason::CannotAfford) return B::Funds;
    return B::Prerequisite;
}

namespace {
std::string_view first_word(std::string_view s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    size_t e = b;
    while (e < s.size() && s[e] != ' ' && s[e] != '\t' && s[e] != '\r' && s[e] != '\n') ++e;
    return s.substr(b, e - b);
}
}  // namespace

namespace {
std::vector<std::string> list_words(std::string_view s) {
    std::vector<std::string> out;
    size_t i = 0;
    const auto sep = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ','; };
    while (i < s.size()) {
        while (i < s.size() && sep(s[i])) ++i;
        size_t j = i;
        while (j < s.size() && !sep(s[j])) ++j;
        if (j > i) out.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}
}  // namespace

CorruptionChoiceArt CorruptionChoiceArt::from(std::string_view icon, std::string_view name, std::string_view benefit,
                                              std::string_view requirement_icon) {
    CorruptionChoiceArt a;
    a.icon = list_words(icon);
    a.name = list_words(name);
    a.benefit = list_words(benefit);
    a.requirement_icon = list_words(requirement_icon);
    return a;
}

std::string_view CorruptionChoiceArt::at(const std::vector<std::string>& list, int32_t type) {
    return type >= 0 && static_cast<size_t>(type) < list.size() ? std::string_view(list[static_cast<size_t>(type)]) : std::string_view();
}

WeatherLines planet_weather_lines(std::string_view name_field, std::string_view info_field) {
    return {first_word(name_field), first_word(info_field)};
}

GameOverArt game_over_art(bool victory, bool pick_land, std::string_view victory_text, std::string_view defeat_text,
                          std::string_view land_win, std::string_view space_win, std::string_view land_lose,
                          std::string_view space_lose) {
    GameOverArt a;
    a.text_key = victory ? victory_text : defeat_text;
    const std::string_view land = victory ? land_win : land_lose, space = victory ? space_win : space_lose;
    a.image = pick_land ? (land.empty() ? space : land) : (space.empty() ? land : space);
    return a;
}

int32_t movie_frame_cue(std::span<const int64_t> frames, int64_t prev, int64_t now) {
    for (size_t i = 0; i < frames.size(); ++i)
        if (frames[i] > prev && frames[i] <= now) return static_cast<int32_t>(i);
    return -1;
}

std::string_view win_lose_banner_text(bool local_side_won, bool corruption_mission) {
    if (local_side_won) return corruption_mission ? "TEXT_WIN_CORRUPTION_MISSION" : "TEXT_WIN_TACTICAL";
    return corruption_mission ? "TEXT_LOSE_CORRUPTION_MISSION" : "TEXT_LOSE_TACTICAL";
}

std::string_view encyclopedia_text_tag(bool skirmish_battle, bool has_mp_text) {
    // ledger: consumes GameObjectType.MP_Encyclopedia_Text
    return skirmish_battle && has_mp_text ? "MP_Encyclopedia_Text" : "Encyclopedia_Text";
}

bool galactic_build_listed(const std::function<bool(std::string_view)>& flag) {
    // ledger: consumes GameObjectType.Build_Tab_Special_Structures
    // ledger: consumes GameObjectType.Build_Tab_Heroes
    for (const char* tag : {"Build_Tab_Land_Units", "Build_Tab_Space_Units", "Build_Tab_Special_Structures", "Build_Tab_Heroes",
                            "Build_Tab_Outpost", "Build_Tab_Space_Station"})
        if (flag(tag)) return true;  // ledger: consumes GameObjectType.Build_Tab_Outpost
    return false;                    // ledger: consumes GameObjectType.Build_Tab_Space_Station
}

std::vector<ui::GalacticBuildOption> galactic_build_options(const galactic::GalacticSim& g, int32_t player, PlanetIndex planet,
                                                            const std::function<std::string(uint32_t type)>& icon) {
    std::vector<ui::GalacticBuildOption> out;
    if (planet >= g.planets().size() || player < 0 || static_cast<size_t>(player) >= g.players().size()) return out;
    if (g.planets()[planet].owner != player) return out;  // §5.1.1: the bar lists only at the local player's own planet
    for (const uint32_t t : g.buildable(planet, player)) {
        if (t >= g.rules().types.size() || t > 0xFFFFu) continue;
        const galactic::TypeInfo& ti = g.rules().types[t];
        ui::GalacticBuildOption o;
        o.type_index = static_cast<uint16_t>(t);
        o.icon = icon ? icon(t) : std::string();
        o.cost = g.build_cost_for(player, planet, t);
        o.block = galactic_build_block(g.can_enqueue(player, planet, t));
        o.enabled = o.block == ui::HudBuildOption::Block::None;
        for (uint32_t q = 0; q < 2; ++q)
            for (const galactic::BuildEntry& b : g.queue_of(planet, q))
                if (b.player == player && b.type == t) ++o.queued;
        o.structure = galactic::is_ground_structure_type(ti) || galactic::is_orbital_structure_type(ti) || galactic::is_star_base_type(ti);
        o.production_disabled = g.production_disabled(player, t);
        // GALACTIC_DIALOGS §5.1.1 (0x3145c0): the land tab lists Build_Cost_Credits > 0 types that need a ground base level or
        // are a DUMMY_GROUND_STRUCTURE (the can-build gate's ground-buildable test); every other type is a space-tab option.
        o.space = !(ti.build_cost > 0 && (ti.required_ground_base != 0 || galactic::is_ground_structure_type(ti)));
        out.push_back(std::move(o));
    }
    return out;
}

std::vector<std::u32string> droid_log_rows(std::u32string_view day_text, int32_t cycle, std::u32string_view text, std::u32string_view separator) {
    std::u32string day(day_text);
    std::u32string num;
    for (const char c : std::to_string(cycle)) num.push_back(static_cast<char32_t>(c));
    if (const size_t at = day.find(U"##0"); at != std::u32string::npos) day.replace(at, 3, num);
    else day += (day.empty() ? U"" : U" ") + num;
    return {std::move(day), std::u32string(text), std::u32string(separator)};
}

const char* galactic_announcement(const galactic::GalacticSim& g, const galactic::GalacticEvent& e, int32_t me) {
    using K = galactic::GalacticEventKind;
    if (me < 0) return nullptr;
    switch (e.kind) {
        case K::PlanetOwnerChanged:
            if (e.player == me && e.other_player != me) return "SFXEvent_Planet_Gained_Control";
            if (e.other_player == me && e.player >= 0 && e.player != me) return "SFXEvent_Planet_Lost_Control";
            return nullptr;
        case K::CorruptionChanged: {
            if (e.planet >= g.planets().size()) return nullptr;
            if (e.player != me && g.planets()[e.planet].owner != me) return nullptr;
            if (e.value >= 0 && e.value != e.other_player) return "SFXEvent_Planet_Corrupted";
            if (e.value < 0 && e.other_player >= 0) return "SFXEvent_Corruption_Removed";
            return nullptr;
        }
        case K::FleetDeparted: {
            const PlanetIndex to = e.other_planet;
            if (to >= g.planets().size() || g.planets()[to].owner != me || e.player < 0 || g.allied(me, e.player)) return nullptr;
            // ui4 t11 (P4_SIM_SYSTEMS §2.7.3): only when the local player has something in the destination's orbit [port:
            // its fleets; whether retail's orbit list also counts the starbase is M].
            bool present = false;
            for (const galactic::FleetState& f : g.fleets()) present = present || (f.alive && f.owner == me && f.planet == to && !f.contents.empty());
            if (!present) return nullptr;
            if (e.fleet < g.fleets().size()) {
                const galactic::FleetState& fl = g.fleets()[e.fleet];
                bool all_hidden = !fl.contents.empty();
                for (const objects::ObjectHandle m : fl.contents)
                    if (g.store().alive(m)) all_hidden = all_hidden && g.hidden(m);
                if (all_hidden) return nullptr;
            }
            return "SFXEvent_Enemy_Fleet_Approaching_Planet";
        }
        default: return nullptr;
    }
}

std::vector<PlanetIndex> fleet_drag_route(const galactic::GalacticSim& g, int32_t me, uint32_t fleet_id, PlanetIndex target) {
    std::vector<PlanetIndex> path;
    if (me < 0 || target >= g.planets().size()) return path;
    const objects::ObjectHandle h = g.store().find(static_cast<objects::ObjectId>(fleet_id));
    const uint32_t fi = h ? g.fleet_of(h) : galactic::kNoFleet;
    if (fi == galactic::kNoFleet || fi >= g.fleets().size()) return path;
    const galactic::FleetState& fl = g.fleets()[fi];
    if (fl.moving || fl.planet == kInvalidPlanet || fl.planet == target) return path;
    // 0x3da30's request: Path_Request_Init (every flag set), then flags 0..2 = the fleet is hidden, flag 4 = set.
    const bool hidden = g.hidden(h);
    galactic::PathRequest req;
    req.source = fl.planet;
    req.destination = target;
    req.allow_unknown_or_neutral = req.allow_enemy_owned = req.skip_danger_check = hidden;
    req.always_accept_destination = true;
    galactic::PathScratch scratch;
    if (!galactic::find_fleet_path(g.graph(), g.player_view(me), req, scratch, path)) path.clear();
    return path;
}

}  // namespace alamo::game
#endif
