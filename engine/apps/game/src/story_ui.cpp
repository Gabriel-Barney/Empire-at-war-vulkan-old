#include "alamo/game/story_ui.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <span>

#include "alamo/core/ascii.hpp"
#include "alamo/galactic/special_systems.hpp"

namespace alamo::game {

commands::EventData story_ui_event(const sim::StoryUiNotification& n) {
    std::vector<uint8_t> b;
    const size_t len = std::min(n.name.size(), kStoryUiMaxName);
    b.reserve(5 + len);
    b.push_back(static_cast<uint8_t>(static_cast<uint8_t>(n.kind) | (n.tactical ? kStoryUiTacticalBit : 0u)));
    const auto o = static_cast<uint32_t>(n.object);
    for (int k = 0; k < 4; ++k) b.push_back(static_cast<uint8_t>((o >> (8 * k)) & 0xFFu));
    for (size_t i = 0; i < len; ++i) b.push_back(static_cast<uint8_t>(n.name[i]));
    return commands::ev::port_extension(kStoryUiSubtype, std::span<const uint8_t>(b));
}

commands::EventData story_esc_event() { return commands::ev::port_extension(kStoryEscSubtype, {}); }

bool read_story_ui_event(const commands::EventData& ev, sim::StoryUiNotification& n) {
    if (ev.type() != commands::EventType::PortExtension || ev.u("subtype") != kStoryUiSubtype) return false;
    const std::span<const uint8_t> b = ev.bytes_view("data");
    const auto kind = static_cast<uint8_t>(b.empty() ? 0u : b[0] & ~kStoryUiTacticalBit);
    if (b.size() < 5 || b.size() > 5 + kStoryUiMaxName || kind >= static_cast<uint8_t>(sim::StoryUiEvent::Count)) return false;
    n = {};
    n.kind = static_cast<sim::StoryUiEvent>(kind);
    n.tactical = (b[0] & kStoryUiTacticalBit) != 0;
    uint32_t o = 0;
    for (int k = 0; k < 4; ++k) o |= static_cast<uint32_t>(b[static_cast<size_t>(1 + k)]) << (8 * k);
    n.object = static_cast<int32_t>(o);
    n.name.reserve(b.size() - 5);
    for (size_t i = 5; i < b.size(); ++i) n.name.push_back(static_cast<char>(b[i]));
    return true;
}

namespace {
using MediaParams = std::vector<std::string>;
std::string_view media_param(const MediaParams& params, size_t i) {
    if (i >= params.size()) return {};
    std::string_view v = params[i];
    while (!v.empty() && static_cast<unsigned char>(v.front()) <= 0x20) v.remove_prefix(1);
    while (!v.empty() && static_cast<unsigned char>(v.back()) <= 0x20) v.remove_suffix(1);
    return v;
}
// ui2 t07 (STORY_PRESENTATION_REWARDS.md 1.3, 1.4, 6.2): the loop flag is atoi(P10) / atoi(P2), an empty one 0.
bool media_flag(const MediaParams& params, size_t i) {
    const std::string_view v = media_param(params, i);
    return !v.empty() && std::atoi(std::string(v).c_str()) != 0;
}
// A cue with Reward_Param(i + 1) as its name, when that parameter is given.
void media_named(std::vector<StoryMediaCue>& out, StoryMediaCue::Kind kind, const MediaParams& params, size_t i) {
    if (!media_param(params, i).empty()) out.push_back({kind, std::string(media_param(params, i))});
}
struct MediaHandler {
    std::string_view name;
    void (*fn)(const MediaParams& params, std::vector<StoryMediaCue>& out);
};
// ref5 t02: one handler per media reward name.
constexpr MediaHandler kMediaHandlers[] = {
    {"START_MOVIE", [](const MediaParams& p, std::vector<StoryMediaCue>& out) { media_named(out, StoryMediaCue::Kind::Movie, p, 0); }},
    {"SPEECH", [](const MediaParams& p, std::vector<StoryMediaCue>& out) { media_named(out, StoryMediaCue::Kind::Speech, p, 0); }},
    {"MULTIMEDIA",
     [](const MediaParams& p, std::vector<StoryMediaCue>& out) {
         media_named(out, StoryMediaCue::Kind::Speech, p, 7);
         // FIXPASS_GALACTIC §4.2 (capture C2): Reward_Param9 = the talking head; the data also holds "" and "-1" (no head).
         // Retail opens any given name (the shipped ones end in _Loop), so the name is not tested; a head named without the
         // suffix plays too.
         if (!media_param(p, 8).empty() && media_param(p, 8) != "-1")
             out.push_back({StoryMediaCue::Kind::AdvisorMovie, std::string(media_param(p, 8)), media_flag(p, 9)});
     }},
    {"COMMANDBAR_MOVIE",
     [](const MediaParams& p, std::vector<StoryMediaCue>& out) {
         if (!media_param(p, 0).empty() && media_param(p, 0) != "-1")
             out.push_back({StoryMediaCue::Kind::AdvisorMovie, std::string(media_param(p, 0)), media_flag(p, 1)});
     }},
    {"STOP_COMMANDBAR_MOVIE", [](const MediaParams&, std::vector<StoryMediaCue>& out) { out.push_back({StoryMediaCue::Kind::AdvisorStop, {}}); }},
    // STORY_REWARDS_GALACTIC §1.22: the SFX event P1 through the SFX manager (no player test).
    {"SFX", [](const MediaParams& p, std::vector<StoryMediaCue>& out) { media_named(out, StoryMediaCue::Kind::Sfx, p, 0); }},
};
}  // namespace

void story_reward_media(std::string_view reward_type, const std::vector<std::string>& params, std::vector<StoryMediaCue>& out) {
    for (const MediaHandler& h : kMediaHandlers)
        if (h.name == reward_type) h.fn(params, out);
}

bool story_reward_media_handles(std::string_view reward_type) {
    for (const MediaHandler& h : kMediaHandlers)
        if (h.name == reward_type) return true;
    return false;
}

std::vector<std::string_view> story_reward_media_names() {
    std::vector<std::string_view> out;
    for (const MediaHandler& h : kMediaHandlers) out.push_back(h.name);
    return out;
}

void StoryUiFeed::reset() {
    last_ = {};
    last_battle_.clear();
    battle_ = battle_seeded_ = false;
    out_.clear();
}

void StoryUiFeed::set_battle(bool battle) {
    if (battle != battle_) {
        last_battle_.clear();
        battle_seeded_ = false;
    }
    battle_ = battle;
}

void StoryUiFeed::observe_battle(const std::vector<uint32_t>& selection) {
    std::vector<uint32_t> now = selection;
    std::sort(now.begin(), now.end());
    now.erase(std::unique(now.begin(), now.end()), now.end());
    now.erase(std::remove(now.begin(), now.end(), 0u), now.end());
    if (battle_seeded_)
        for (const uint32_t id : now)
            if (!std::binary_search(last_battle_.begin(), last_battle_.end(), id)) raise(sim::StoryUiEvent::SelectUnit, id, {}, true);
    battle_seeded_ = true;
    last_battle_ = std::move(now);
}

void StoryUiFeed::raise(sim::StoryUiEvent kind, uint32_t object, std::string_view name, bool tactical) {
    sim::StoryUiNotification n;
    n.kind = kind;
    n.tactical = tactical;
    n.object = static_cast<int32_t>(object);
    n.name = std::string(name.substr(0, kStoryUiMaxName));
    out_.push_back(std::move(n));
    ++counts_[static_cast<size_t>(kind)];
}

void StoryUiFeed::observe(const StoryUiView& v) {
    using E = sim::StoryUiEvent;
    StoryUiView now = v;
    std::sort(now.selection.begin(), now.selection.end());
    now.selection.erase(std::unique(now.selection.begin(), now.selection.end()), now.selection.end());
    now.selection.erase(std::remove(now.selection.begin(), now.selection.end(), 0u), now.selection.end());
    if (now.zoomed && now.zoom_planet == 0) now.zoom_planet = now.planet;
    if (!now.zoomed) now.zoom_planet = 0;
    for (const uint32_t id : now.selection)  // newly selected objects (last_.selection is sorted)
        if (!std::binary_search(last_.selection.begin(), last_.selection.end(), id)) raise(E::SelectUnit, id, {});
    if (now.planet != 0 && now.planet != last_.planet) raise(E::SelectPlanet, now.planet, {});
    if (last_.zoomed && (!now.zoomed || now.zoom_planet != last_.zoom_planet) && last_.zoom_planet != 0)
        raise(E::ZoomOutPlanet, last_.zoom_planet, {});
    if (now.zoomed && (!last_.zoomed || now.zoom_planet != last_.zoom_planet) && now.zoom_planet != 0)
        raise(E::ZoomIntoPlanet, now.zoom_planet, {});
    if (now.corruption_planet != 0 && now.corruption_planet != last_.corruption_planet)
        raise(E::OpenCorruption, now.corruption_planet, {});
    // STY1 t03: STORY_GENERIC names the story plots wait on (b0333). Zoom ends are approximated by the zoom state change
    // [port]; dialog names come from the special dialog that opened; drag names when a HUD drag begins.
    if (last_.zoomed && !now.zoomed) generic("zoomed_out");
    if (now.zoomed && !last_.zoomed) generic("zoomed_in");
    if (now.special_kind != 0 && now.special_kind != last_.special_kind) {
        switch (now.special_kind) {
            case galactic::kSpecialBlackMarket: generic("open_black_market_menu"); break;
            case galactic::kSpecialSabotage: generic("open_galactic_sabotage_menu"); break;
            case galactic::kSpecialSlice: generic("slice_dialog"); break;
            case galactic::kSpecialCorruption: generic("corruption_dialog"); break;
            default: break;
        }
    }
    if (now.drag != 0 && now.drag != last_.drag) generic(now.drag == 2 ? "start_land_drag" : "start_fleet_drag");
    last_ = std::move(now);
}

void StoryUiFeed::click_gui(std::string_view component) {
    if (!component.empty()) raise(sim::StoryUiEvent::ClickGui, 0, component, battle_);
}
void StoryUiFeed::speech_done(std::string_view event) {
    if (!event.empty()) raise(sim::StoryUiEvent::SpeechDone, 0, event, battle_);
}
void StoryUiFeed::movie_done(std::string_view movie) {
    if (!movie.empty()) raise(sim::StoryUiEvent::MovieDone, 0, movie);
}
void StoryUiFeed::generic(std::string_view name) {
    if (!name.empty()) raise(sim::StoryUiEvent::Generic, 0, name, battle_);
}
void StoryUiFeed::mission_lost(std::string_view name) {
    if (!name.empty()) raise(sim::StoryUiEvent::MissionLost, 0, name);
}

std::vector<sim::StoryUiNotification> StoryUiFeed::take() {
    std::vector<sim::StoryUiNotification> v;
    v.swap(out_);
    return v;
}

uint64_t StoryUiFeed::raised(sim::StoryUiEvent kind) const {
    const auto i = static_cast<size_t>(kind);
    return i < std::size(counts_) ? counts_[i] : 0;
}

uint64_t StoryUiFeed::raised_total() const {
    uint64_t t = 0;
    for (const uint64_t c : counts_) t += c;
    return t;
}

// --- story t13: LinkedBattleOverlay (story_ui.hpp) ---
void LinkedBattleOverlay::begin(bool letterbox, bool input_block, uint32_t battle_frame) {
    reset();
    start_ = battle_frame;
    block_ = input_block;
    letterbox_hold_ = letterbox;
    input_hold_ = input_block;
    blocked_ = input_block;
    if (letterbox) bars_.show();
}

void LinkedBattleOverlay::reset() {
    bars_.remove();
    start_ = 0;
    block_ = letterbox_hold_ = input_hold_ = blocked_ = false;
}

// ui2 t10: the script's own bars (Letter_Box_In / Out with their times) are the story box's (App::story_box_); this overlay
// only ends its battle-start hold. A held bar stays up until the script's bars take over; Out drops it at once so the
// two slides are not drawn on top of each other.
void LinkedBattleOverlay::script_letterbox(bool in) {
    letterbox_hold_ = false;
    if (!in) bars_.remove();
}

void LinkedBattleOverlay::script_controls() { input_hold_ = false; }

void LinkedBattleOverlay::update(uint32_t battle_frame, bool script_locked, float dt_seconds) {
    const bool late = battle_frame >= start_ && battle_frame - start_ >= kHandOverFrames;
    if (letterbox_hold_ && late) {
        letterbox_hold_ = false;
        bars_.slide_out();
    }
    if (input_hold_ && late) input_hold_ = false;
    blocked_ = block_ && (input_hold_ || script_locked);
    bars_.update(dt_seconds);
}

}  // namespace alamo::game
