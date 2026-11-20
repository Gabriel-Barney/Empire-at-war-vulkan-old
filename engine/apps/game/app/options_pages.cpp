// alamo_game host, ref6 t04: the options pages' load / save and setup conversions (options_pages.hpp).
#include "options_pages.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <istream>
#include <ostream>

namespace alamo::game {

namespace {

std::string trim(std::string t) {
    while (!t.empty() && (t.back() == ' ' || t.back() == '\r' || t.back() == '\t')) t.pop_back();
    size_t b = 0;
    while (b < t.size() && (t[b] == ' ' || t[b] == '\t')) ++b;
    return t.substr(b);
}

template <typename S, typename T>
struct Field {
    const char* key;
    T S::*field;
};
constexpr Field<ui::GameplaySettings, int32_t> kGameplayInts[] = {{"game_speed", &ui::GameplaySettings::game_speed},
                                                                  {"strategic_speed", &ui::GameplaySettings::strategic_speed},
                                                                  {"scroll_speed", &ui::GameplaySettings::scroll_speed},
                                                                  {"difficulty", &ui::GameplaySettings::difficulty}};
constexpr Field<ui::GameplaySettings, bool> kGameplayChecks[] = {
    {"ask_autoresolve", &ui::GameplaySettings::ask_autoresolve},
    {"delay_tooltips", &ui::GameplaySettings::delay_tooltips},
    {"absolute_scroll", &ui::GameplaySettings::absolute_scroll},
    {"alternate_mouse", &ui::GameplaySettings::alternate_mouse},
    {"right_button_scroll", &ui::GameplaySettings::right_button_scroll},
    {"both_buttons_rotate", &ui::GameplaySettings::both_buttons_rotate},
    {"simplified_colors", &ui::GameplaySettings::simplified_colors},
    {"abilities_autofire", &ui::GameplaySettings::abilities_autofire}};
constexpr Field<ui::AudioVolumes, float> kVolumes[] = {{"volume_master", &ui::AudioVolumes::master},
                                                       {"volume_sfx", &ui::AudioVolumes::sfx},
                                                       {"volume_speech", &ui::AudioVolumes::speech},
                                                       {"volume_music", &ui::AudioVolumes::music}};
constexpr std::string_view kKeyPrefix = "key.";

}  // namespace

OptionsKeys OptionsKeys::parse(std::istream& in) {
    OptionsKeys k;
    for (std::string line; std::getline(in, line);) {
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        k.kv_[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return k;
}

const std::string* OptionsKeys::find(std::string_view key) const {
    const auto it = kv_.find(key);
    return it != kv_.end() && !it->second.empty() ? &it->second : nullptr;
}

std::optional<bool> OptionsKeys::flag(std::string_view key) const {
    const std::string* v = find(key);
    if (!v) return std::nullopt;
    return *v == "1" || *v == "true" || *v == "on";
}

std::optional<int32_t> OptionsKeys::integer(std::string_view key) const {
    const std::string* v = find(key);
    if (!v) return std::nullopt;
    return static_cast<int32_t>(std::strtol(v->c_str(), nullptr, 10));
}

std::optional<float> OptionsKeys::real(std::string_view key) const {
    const std::string* v = find(key);
    if (!v) return std::nullopt;
    return std::strtof(v->c_str(), nullptr);
}

std::string options_real(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", static_cast<double>(v));
    return buf;
}

// ---- Audio ----
void AudioPage::load(const OptionsKeys& k, Settings& s) {
    for (const auto& e : kVolumes)
        if (const auto v = k.real(e.key)) s.*e.field = std::clamp(*v, 0.0f, 1.0f);
    if (const auto v = k.flag("subtitles")) s.subtitles = *v;
}
void AudioPage::save(std::ostream& out, const Settings& s) {
    for (const auto& e : kVolumes) out << e.key << "=" << options_real(s.*e.field) << "\n";
    out << "subtitles=" << (s.subtitles ? 1 : 0) << "\n";
}

// ---- Gameplay ----
void GameplayPage::load(const OptionsKeys& k, Settings& s) {
    for (const auto& e : kGameplayInts)
        if (const auto v = k.integer(e.key)) s.*e.field = *v;
    s.difficulty = std::clamp(s.difficulty, 0, 2);
    for (const auto& e : kGameplayChecks)
        if (const auto v = k.flag(e.key)) s.*e.field = *v;
}
void GameplayPage::save(std::ostream& out, const Settings& s) {
    for (const auto& e : kGameplayInts) out << e.key << "=" << s.*e.field << "\n";
    for (const auto& e : kGameplayChecks) out << e.key << "=" << (s.*e.field ? 1 : 0) << "\n";
}

// ---- Camera and controls ----
void CameraPage::load(const OptionsKeys& k, Settings& s) {
    if (const auto v = k.real("galactic_fov")) s.camera.galactic_fov_deg = std::clamp(*v, ui::kGalacticFovMinDeg, ui::kGalacticFovMaxDeg);
    if (const auto v = k.real("tactical_fov")) s.camera.tactical_fov_deg = std::clamp(*v, ui::kTacticalFovMinDeg, ui::kTacticalFovMaxDeg);
    if (const auto v = k.flag("tactical_fov_set")) s.tactical_fov_set = *v;
    if (const std::string* v = k.find("queue_mods")) s.camera.queue_mods = static_cast<uint8_t>(std::strtoul(v->c_str(), nullptr, 10) & 0xFFu);
    if (const auto v = k.flag("show_path_lines")) s.camera.show_path_lines = *v;
}
void CameraPage::save(std::ostream& out, const Settings& s) {
    out << "galactic_fov=" << options_real(s.camera.galactic_fov_deg) << "\n";
    out << "tactical_fov=" << options_real(s.camera.tactical_fov_deg) << "\n";
    out << "tactical_fov_set=" << (s.tactical_fov_set ? 1 : 0) << "\n";
    out << "queue_mods=" << static_cast<unsigned>(s.camera.queue_mods) << "\n";
    out << "show_path_lines=" << (s.camera.show_path_lines ? 1 : 0) << "\n";
}

// ---- Keyboard ----
void KeysPage::load(const OptionsKeys& k, Settings& s) {
    for (const auto& [key, v] : k.all())
        if (key.size() > kKeyPrefix.size() && std::string_view(key).substr(0, kKeyPrefix.size()) == kKeyPrefix && !v.empty())
            s[key.substr(kKeyPrefix.size())] = static_cast<int32_t>(std::strtol(v.c_str(), nullptr, 10));
}
void KeysPage::save(std::ostream& out, const Settings& s) {
    for (const auto& [key, v] : s) out << kKeyPrefix << key << "=" << v << "\n";
}

// ---- Skirmish staging slot ----
void SkirmishSlotPage::load(const OptionsKeys& k, Settings& s) {
    if (const std::string* v = k.find("skirmish_faction")) s.faction = *v;
    if (const auto v = k.integer("skirmish_colour")) s.colour = std::clamp(*v, -1, 8);
}
void SkirmishSlotPage::save(std::ostream& out, const Settings& s) {
    if (!s.faction.empty()) out << "skirmish_faction=" << s.faction << "\n";
    if (s.colour >= 0) out << "skirmish_colour=" << s.colour << "\n";
}

// ---- Game setup pages ----
CampaignOptions CampaignOptionsPage::session_options(const Settings& s) {
    CampaignOptions o;
    if (!s.set) return o;
    o.set = true;
    o.credits = s.credits;
    o.start_tech = s.start_tech;
    o.max_tech = s.max_tech;
    o.auto_resolve = s.auto_resolve;
    o.random_events = s.random_events;
    return o;
}

sim::BattleConfig::SkirmishOptions SkirmishOptionsPage::battle_options(const Settings& s) {
    sim::BattleConfig::SkirmishOptions o;
    if (!s.set) return o;
    o.set = true;
    o.credits = std::max(0, s.credits);
    o.win_condition = static_cast<uint8_t>(std::clamp(s.win_condition, 0, 0xff));
    o.heroes = s.heroes;
    o.starting_units = s.starting_units;
    o.super_weapons = s.super_weapons;
    o.prebuilt_base = s.prebuilt_base;
    return o;
}

}  // namespace alamo::game
