#include "alamo/game/story_present.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "alamo/core/ascii.hpp"
#include "alamo/core/bytes.hpp"
#include "alamo/data/game_database.hpp"
#include "alamo/data/values.hpp"  // parse_floats
#include "alamo/data/xml.hpp"

namespace alamo::game {

namespace {

std::string_view trim(std::string_view v) {
    while (!v.empty() && static_cast<unsigned char>(v.front()) <= 0x20) v.remove_prefix(1);
    while (!v.empty() && static_cast<unsigned char>(v.back()) <= 0x20) v.remove_suffix(1);
    return v;
}

// Reward_ParamN (1-based) trimmed; "" when absent.
std::string_view param(const std::vector<std::string>& p, size_t n) { return n >= 1 && n <= p.size() ? trim(p[n - 1]) : std::string_view{}; }

// The original reads numbers with atoi / atof: text that is not a number reads as 0.
int32_t to_int(std::string_view s) {
    const std::string t(trim(s));
    return static_cast<int32_t>(std::strtol(t.c_str(), nullptr, 10));
}
double to_float(std::string_view s) {
    const std::string t(trim(s));
    return std::strtod(t.c_str(), nullptr);
}

std::u32string widen(std::string_view s) {
    std::u32string r;
    r.reserve(s.size());
    for (const char c : s) r.push_back(static_cast<char32_t>(static_cast<unsigned char>(c)));
    return r;
}

bool parse_rgb(std::string_view s, StoryColor& out) {
    int v[3] = {0, 0, 0};
    int n = 0;
    size_t i = 0;
    while (i <= s.size() && n < 3) {
        const size_t e = std::min(s.find(',', i), s.size());
        v[n++] = static_cast<int>(to_int(s.substr(i, e - i)));
        i = e + 1;
    }
    if (n < 3) return false;
    out = {static_cast<uint8_t>(std::clamp(v[0], 0, 255)), static_cast<uint8_t>(std::clamp(v[1], 0, 255)),
           static_cast<uint8_t>(std::clamp(v[2], 0, 255)), 255};
    return true;
}

// "x,y,z" (POSITION_CAMERA P2); missing parts read as 0.
void parse_xyz(std::string_view s, float& x, float& y, float& z) {
    float v[3] = {0, 0, 0};
    size_t i = 0;
    for (int n = 0; n < 3 && i <= s.size(); ++n) {
        const size_t e = std::min(s.find(',', i), s.size());
        v[n] = static_cast<float>(to_float(s.substr(i, e - i)));
        i = e + 1;
    }
    x = v[0];
    y = v[1];
    z = v[2];
}

StoryTextChannel channel_of(std::string_view p7) {
    const std::string u = ascii_upper(trim(p7));
    if (u == "HINT") return StoryTextChannel::Hint;
    if (u == "SYSTEM") return StoryTextChannel::System;
    if (u == "TASK") return StoryTextChannel::Task;
    if (u == "ALIEN_SPEECH") return StoryTextChannel::AlienSpeech;
    return StoryTextChannel::Subtitle;
}

std::string upper(std::string_view s) { return ascii_upper(trim(s)); }

}  // namespace

size_t StoryTextLine::revealed(double now, double chars_per_second) const {
    if (!typewriter) return text.size();
    const double n = std::max(0.0, now - start) * std::max(0.0, chars_per_second);
    return n >= static_cast<double>(text.size()) ? text.size() : static_cast<size_t>(n);
}

void StoryPresenter::reset() {
    const Hooks h = hooks_;
    const Colors c = colors_;
    const Config cfg = config_;
    *this = StoryPresenter();
    hooks_ = h;
    colors_ = c;
    config_ = cfg;
}

std::u32string StoryPresenter::resolve(std::string_view id) const {
    std::u32string t = hooks_.text ? hooks_.text(id) : std::u32string();
    if (t.empty()) t = widen(id);  // [L] a missing id: the id itself (the retail miss behaviour was not read)
    return t;
}

void StoryPresenter::add_text(std::string_view type, const std::vector<std::string>& p, double now) {
    (void)type;
    const std::string_view id = param(p, 1);
    if (id.empty()) return;  // §1.1: no id, nothing
    if (!param(p, 4).empty()) {  // P4 given: remove every line with this id
        std::erase_if(lines_, [&](const StoryTextLine& l) { return l.id == id; });
        return;
    }
    StoryTextLine l;
    l.id = std::string(id);
    l.channel = channel_of(param(p, 7));
    if (l.channel == StoryTextChannel::Subtitle && !config_.subtitles) return;  // shown only with the subtitles option
    std::erase_if(lines_, [&](const StoryTextLine& o) { return o.id == l.id; });  // a repeated id never stacks
    l.seconds = param(p, 2).empty() ? 5.0 : to_float(param(p, 2));
    l.persistent = l.seconds < 0.0;  // -1: stays until removed
    l.typewriter = param(p, 5).empty() ? true : to_int(param(p, 5)) != 0;
    l.start = now;
    switch (l.channel) {
        case StoryTextChannel::Hint: l.color = colors_.hint; break;
        case StoryTextChannel::System: l.color = colors_.system; break;
        case StoryTextChannel::Task: l.color = colors_.task; break;
        default: l.color = colors_.speech; break;
    }
    StoryColor over;
    if (!param(p, 6).empty() && parse_rgb(param(p, 6), over)) l.color = over;
    l.text = resolve(id);
    if (l.channel == StoryTextChannel::Hint) ++text_hint_sounds_;
    if (const std::string_view tn = param(p, 3); !tn.empty() && hooks_.type_name) {  // P3: the type's name replaces %s
        const std::u32string name = hooks_.type_name(tn);
        const size_t at = l.text.find(U"%s");
        if (at != std::u32string::npos && !name.empty()) l.text.replace(at, 2, name);
    }
    lines_.push_back(std::move(l));
}

void StoryPresenter::add_arrow(StoryArrow a) {
    if (arrows_.size() >= kArrowSlots) return;  // all ten used: no arrow (the flash still happens)
    arrows_.push_back(std::move(a));
}

// ref5 t02: the presentation rewards, one handler per Reward_Type name (the Ui rows of the sim's story reward table that
// this class draws or obeys; the media rewards are story_reward_media's). The game's coverage test fails for a Ui row that
// neither registers.
namespace {
using RC = StoryPresenter::RewardCall;
bool flag_at(const RC& c, size_t n, bool def) { return param(c.p, n).empty() ? def : to_int(param(c.p, n)) != 0; }
}  // namespace

void StoryPresenter::push_camera(StoryCameraCommand::Kind kind, const std::vector<std::string>& p) {
    StoryCameraCommand c;
    c.kind = kind;
    c.type = std::string(param(p, 1));
    if (!param(p, 2).empty()) parse_xyz(param(p, 2), c.ox, c.oy, c.oz);
    camera_.push_back(std::move(c));
}

// DUAL_FLASH (§2.5): while armed, FLASH_GUI / FLASH_PLANET_GUI flashes are held; the second one held starts the pair, the
// two alternating at `rate` seconds for `time` seconds [M: the alternation's phase and end]. The pair disarms it [M].
void StoryPresenter::flash_or_hold(bool planet, std::string target, double now, double seconds) {
    if (dual_.armed && !(dual_.time > 0.0)) dual_ = {};  // [port] no time to alternate in: the flashes run as without it
    if (!dual_.armed) {
        if (planet) object_flashes_.push_back({std::move(target), now, now + seconds});
        else flashes_.push_back({std::move(target), now, now + seconds});
        return;
    }
    dual_.held.push_back({planet, std::move(target)});
    if (dual_.held.size() < 2) return;
    const double rate = dual_.rate > 0.0 ? dual_.rate : dual_.time;
    // limit: [port] at most 256 alternation steps (a zero or tiny rate would otherwise queue without bound)
    for (size_t k = 0; k < 256 && rate > 0.0 && static_cast<double>(k) * rate < dual_.time; ++k) {
        const DualHeld& h = dual_.held[k % 2];
        const double a = now + static_cast<double>(k) * rate, b = std::min(a + rate, now + dual_.time);
        if (h.planet) object_flashes_.push_back({h.target, a, b});
        else flashes_.push_back({h.target, a, b});
    }
    dual_ = {};
}

bool StoryPresenter::on_reward(std::string_view type, const std::vector<std::string>& p, double now, uint8_t mode) {
    const std::string ty = ascii_upper(type);
    const RewardCall c{ty, p, now, mode};
    for (const RewardHandler& h : reward_handlers())
        if (h.name == ty) {
            h.fn(*this, c);
            ++handled_;
            return true;
        }
    return false;
}

bool StoryPresenter::has_reward_handler(std::string_view name) {
    for (const RewardHandler& h : reward_handlers())
        if (h.name == name) return true;
    return false;
}

std::vector<std::string_view> StoryPresenter::reward_names() {
    std::vector<std::string_view> out;
    for (const RewardHandler& h : reward_handlers()) out.push_back(h.name);
    return out;
}

// FLASH_FLEET_WITH_UNIT / FLASH_SPECIAL_ABILITY / FLASH_PRODUCTION_CHOICE: P1 target, P2 flash id; an arrow slot.
void StoryPresenter::add_flash_arrow(StoryArrow::Kind kind, const RewardCall& c) {
    if (param(c.p, 1).empty()) return;
    StoryArrow a;
    a.kind = kind;
    a.target = std::string(param(c.p, 1));
    a.label = std::string(param(c.p, 2));
    add_arrow(std::move(a));
}

std::span<const StoryPresenter::RewardHandler> StoryPresenter::reward_handlers() {
    using K = StoryCameraCommand::Kind;
    using P = StoryPresenter;
    static constexpr RewardHandler kHandlers[] = {
        {"SCREEN_TEXT", [](P& s, const RC& c) { s.add_text(c.type, c.p, c.now); }},
        // The text part; the speech and the head are story_reward_media's.
        {"MULTIMEDIA", [](P& s, const RC& c) { s.add_text(c.type, c.p, c.now); }},
        {"FLASH_GUI",
         [](P& s, const RC& c) {
             const std::string_view comp = param(c.p, 1);
             if (comp.empty()) return;
             s.flash_or_hold(false, std::string(comp), c.now, kFlashSeconds * kFlashCount);
             StoryArrow a;
             a.kind = StoryArrow::Kind::Component;
             a.target = std::string(comp);
             a.label = std::string(param(c.p, 2));
             s.add_arrow(std::move(a));
         }},
        {"FLASH_PLANET_GUI",
         [](P& s, const RC& c) {
             if (param(c.p, 1).empty()) return;
             StoryArrow a;
             a.kind = StoryArrow::Kind::Planet;
             a.target = std::string(param(c.p, 1));
             a.region = upper(param(c.p, 2));
             a.sub = to_int(param(c.p, 3));
             a.label = std::string(param(c.p, 4));
             a.has_pointer = !param(c.p, 5).empty();
             // 3.4: every region but PLANET flashes its planet-panel entry (the port has no such panel: the planet itself
             // flashes [port]); AFFILIATION / CREDITS / COINS are no-ops; PLANET is the arrow alone.
             std::string_view r = a.region;  // the shipped scripts spell the regions FLASH_TROOPS, FLASH_PLANET ...
             if (r.starts_with("FLASH_")) r.remove_prefix(6);
             if (r != "PLANET" && r != "AFFILIATION" && r != "CREDITS" && r != "COINS")
                 s.flash_or_hold(true, a.target, c.now, kFlashSeconds * kFlashCount);
             s.add_arrow(std::move(a));
         }},
        {"FLASH_FLEET_WITH_UNIT", [](P& s, const RC& c) { s.add_flash_arrow(StoryArrow::Kind::Fleet, c); }},
        {"FLASH_SPECIAL_ABILITY", [](P& s, const RC& c) { s.add_flash_arrow(StoryArrow::Kind::Ability, c); }},
        {"FLASH_PRODUCTION_CHOICE", [](P& s, const RC& c) { s.add_flash_arrow(StoryArrow::Kind::Production, c); }},
        {"FLASH_ADVANCED_MAP_OBJECT",
         [](P& s, const RC& c) {
             if (!param(c.p, 1).empty())  // §3.8: flashed six times, no arrow
                 s.map_flashes_.push_back({std::string(param(c.p, 1)), c.now, c.now + kFlashSeconds * 6});
         }},
        {"FLASH_PLANET",
         [](P& s, const RC& c) {
             if (!param(c.p, 1).empty())  // GUI_Flash_Duration x 3
                 s.object_flashes_.push_back({std::string(param(c.p, 1)), c.now, c.now + s.config_.flash_duration * 3});
         }},
        {"FLASH_OBJECT",
         [](P& s, const RC& c) {
             if (!param(c.p, 1).empty())
                 s.object_flashes_.push_back({std::string(param(c.p, 1)), c.now, c.now + s.config_.flash_duration * 3});
         }},
        // §2.4: P1 region (ORGANIZE / PRODUCTION / SELECTION; another name: retail logs "Can't find region", nothing), P2 unit
        // type: the command-bar button holding that type flashes with an arrow (an arrow slot of kind Unit). [port] the host
        // does not find the button yet: the slot is taken, nothing is drawn.
        {"FLASH_UNIT",
         [](P& s, const RC& c) {
             const std::string region = upper(param(c.p, 1));
             if (param(c.p, 2).empty() || (region != "ORGANIZE" && region != "PRODUCTION" && region != "SELECTION")) return;
             StoryArrow a;
             a.kind = StoryArrow::Kind::Unit;
             a.target = std::string(param(c.p, 2));
             a.region = region;
             s.add_arrow(std::move(a));
         }},
        // §2.3: P1 seconds; the 2D pointer and the 3D arrow at the fired event's own position. [port] the fired reward log
        // (lua::StoryFired) does not carry that position, so nothing is placed.
        {"FLASH_TERRAIN", [](P&, const RC&) {}},
        // §2.5: P1 time, P2 rate: arms the pairing of the next two FLASH_GUI / FLASH_PLANET_GUI flashes (flash_or_hold).
        {"DUAL_FLASH",
         [](P& s, const RC& c) {
             s.dual_ = {};
             s.dual_.armed = true;
             s.dual_.time = to_float(param(c.p, 1));
             s.dual_.rate = to_float(param(c.p, 2));
         }},
        {"FORCE_CLICK_GUI",
         [](P& s, const RC& c) {
             if (!param(c.p, 1).empty()) s.forced_clicks_.push_back(std::string(param(c.p, 1)));
         }},
        {"HIDE_TUTORIAL_CURSOR",
         [](P& s, const RC& c) {
             const std::string_view label = param(c.p, 1);
             const auto it = std::find_if(s.arrows_.begin(), s.arrows_.end(), [&](const StoryArrow& a) { return a.label == label; });
             if (it != s.arrows_.end()) s.arrows_.erase(it);
             s.dual_ = {};  // §2.5: the held DUAL_FLASH flashes go too
         }},
        {"HIDE_CURSOR_ON_CLICK", [](P& s, const RC& c) { s.hide_cursor_on_click_ = to_int(param(c.p, 1)) != 0; }},  // §3.11: no reader
        {"HIGHLIGHT_OBJECT",
         [](P& s, const RC& c) {
             const std::string_view obj = param(c.p, 1);
             if (obj.empty() || param(c.p, 2).empty()) return;
             const bool on = to_int(param(c.p, 2)) != 0;
             const int32_t pid = param(c.p, 3).empty() ? -1 : to_int(param(c.p, 3));
             auto same = [&](const StoryHighlight& h) { return h.object == 0 && ascii_iequals(h.type_name, obj); };
             std::erase_if(s.highlights_, same);
             if (on) s.highlights_.push_back({std::string(obj), 0, false, 0.0f});
             if (on && pid != -1) {
                 std::erase_if(s.pointers_, [&](const StoryPointer& q) { return q.id == pid; });
                 StoryPointer q;
                 q.id = pid;
                 q.object_type = std::string(obj);
                 s.pointers_.push_back(std::move(q));
             } else if (!on && pid != -1) {
                 std::erase_if(s.pointers_, [&](const StoryPointer& q) { return q.id == pid; });
             }
         }},
        {"TUTORIAL_DIALOG",
         [](P& s, const RC& c) {
             if (param(c.p, 1).empty()) return;
             s.cinematic_mode_ = false;  // 4: a cinematic mode in force ends first
             s.dialogs_.push_back({std::string(param(c.p, 1)), to_int(param(c.p, 2)) != 0});
         }},
        {"LOCK_CONTROLS",
         [](P& s, const RC& c) {
             if (!param(c.p, 1).empty()) s.controls_locked_ = to_int(param(c.p, 1)) != 0;
         }},
        {"SCROLL_LOCK", [](P& s, const RC& c) { s.scroll_locked_ = to_int(param(c.p, 1)) != 0; }},
        {"LOCK_PLANET_SELECTION", [](P& s, const RC& c) { s.planet_selection_locked_ = to_int(param(c.p, 1)) != 0; }},
        {"ENABLE_OBJECTIVE_DISPLAY", [](P& s, const RC& c) { s.objective_event_ = s.objectives_shown_ = flag_at(c, 1, true); }},
        {"SHOW_COMMAND_BAR",
         [](P& s, const RC& c) {
             // 5.6: galactic = a plain show / hide; space and land = the hide counter (nested hides need matching shows)
             if (c.mode == 0) s.command_bar_hide_ = flag_at(c, 1, false) ? 0 : 1;
             else if (flag_at(c, 1, false)) s.command_bar_hide_ = std::max(0, s.command_bar_hide_ - 1);
             else ++s.command_bar_hide_;
         }},
        {"START_CINEMATIC_MODE", [](P& s, const RC&) { s.cinematic_mode_ = true; }},
        {"STOP_CINEMATIC_MODE", [](P& s, const RC&) { s.cinematic_mode_ = false; }},
        {"DISABLE_MOVIES", [](P& s, const RC& c) { s.movies_disabled_ = to_int(param(c.p, 1)) != 0; }},
        {"DISABLE_RETREAT",
         [](P& s, const RC& c) {
             if (param(c.p, 1).empty() || param(c.p, 2).empty()) return;
             if (to_int(param(c.p, 2)) != 0) s.retreat_off_.insert(upper(param(c.p, 1)));
             else s.retreat_off_.erase(upper(param(c.p, 1)));
         }},
        {"HIDE_AUTORESOLVE", [](P& s, const RC&) { s.hide_autoresolve_ = true; }},  // §5.3: no reader in retail
        {"USE_RETRY_DIALOG", [](P& s, const RC&) { s.retry_dialog_ = true; }},
        {"ACTIVATE_RETRY_DIALOG", [](P& s, const RC&) { s.retry_requested_ = true; }},
        {"ENABLE_GALACTIC_CORRUPTION_HOLOGRAM",
         [](P& s, const RC& c) {
             if (param(c.p, 1).empty()) return;
             if (flag_at(c, 2, true)) s.hologram_.insert(upper(param(c.p, 1)));
             else s.hologram_.erase(upper(param(c.p, 1)));
         }},
        {"SHOW_SMUGGLE_SLOT", [](P& s, const RC& c) { s.force_slot(c, "SMUGGLE", true); }},
        {"HIDE_SMUGGLE_SLOT", [](P& s, const RC& c) { s.force_slot(c, "SMUGGLE", false); }},
        {"SHOW_RAID_SLOT", [](P& s, const RC& c) { s.force_slot(c, "RAID", true); }},
        {"HIDE_RAID_SLOT", [](P& s, const RC& c) { s.force_slot(c, "RAID", false); }},
        {"SHOW_STEAL_SLOT", [](P& s, const RC& c) { s.force_slot(c, "STEAL", true); }},
        {"HIDE_STEAL_SLOT", [](P& s, const RC& c) { s.force_slot(c, "STEAL", false); }},
        {"SHOW_SPECIAL_SLOT",
         [](P& s, const RC& c) {
             if (param(c.p, 1).empty() || param(c.p, 2).empty()) return;
             std::string slot = upper(param(c.p, 2));
             if (slot.size() > 5 && slot.compare(slot.size() - 5, 5, "_SLOT") == 0) slot.resize(slot.size() - 5);
             const std::string key = upper(param(c.p, 1)) + "|" + slot;
             if (param(c.p, 3).empty() || to_int(param(c.p, 3)) != 0) s.slots_.insert(key);
             else s.slots_.erase(key);
         }},
        {"SELECT_PLANET", [](P& s, const RC& c) { s.push_camera(K::SelectPlanet, c.p); }},
        {"ZOOM_IN", [](P& s, const RC& c) { s.push_camera(K::ZoomIn, c.p); }},
        {"ZOOM_OUT", [](P& s, const RC& c) { s.push_camera(K::ZoomOut, c.p); }},
        {"POSITION_CAMERA", [](P& s, const RC& c) { s.push_camera(K::Position, c.p); }},
        {"SCROLL_CAMERA", [](P& s, const RC& c) { s.push_camera(K::Scroll, c.p); }},
        {"PAUSE_GAME", [](P& s, const RC& c) { s.push_camera(K::PauseGame, c.p); }},
        {"PAUSE_GALACTIC", [](P& s, const RC& c) { s.push_camera(K::PauseGalactic, c.p); }},
        {"UNPAUSE_GALACTIC", [](P& s, const RC& c) { s.push_camera(K::UnpauseGalactic, c.p); }},
        {"PAUSE_GALACTIC_GAME",
         [](P& s, const RC& c) {
             if (!param(c.p, 1).empty() && to_int(param(c.p, 1)) == 0) s.push_camera(K::UnpauseGalactic, c.p);
             else s.push_camera(K::PauseGalactic, c.p);
         }},
        {"SET_ADVISOR",
         [](P& s, const RC& c) {
             if (!param(c.p, 1).empty() && c.mode < 3) s.advisor_[c.mode] = to_int(param(c.p, 1)) != 0;
         }},
    };
    return kHandlers;
}

// SHOW_ / HIDE_ SMUGGLE / RAID / STEAL _SLOT (§5.7): P1 planet; the slot is forced visible there (show) or not (hide).
void StoryPresenter::force_slot(const RewardCall& c, std::string_view slot, bool show) {
    if (param(c.p, 1).empty()) return;
    const std::string key = upper(param(c.p, 1)) + "|" + std::string(slot);
    if (show) slots_.insert(key);
    else slots_.erase(key);
}

void StoryPresenter::remove_all_text() { lines_.clear(); }

void StoryPresenter::flash_component(std::string_view component, int32_t count, double now) {
    if (component.empty() || count <= 0) return;
    flashes_.push_back({std::string(component), now, now + kFlashSeconds * static_cast<double>(count)});
}

void StoryPresenter::game_message(std::string_view text_id) {
    StoryHint h;
    h.text = resolve(text_id);
    h.color = {255, 255, 255, 255};
    h.sound = StoryHint::Sound::Urgent;
    hint_queue_.insert(hint_queue_.begin(), std::move(h));
    hint_shown_ = false;  // a front insert resets the display timer: it shows at once
}

void StoryPresenter::urgent_message(std::u32string text, StoryColor color) {
    StoryHint h;
    h.text = std::move(text);
    h.color = color;
    h.sound = StoryHint::Sound::Urgent;
    hint_queue_.insert(hint_queue_.begin(), std::move(h));
    hint_shown_ = false;  // a front insert shows at once
}

void StoryPresenter::queue_message(std::u32string text, StoryColor color) {
    if (text.empty()) return;  // §6.3: the enqueue needs a non-empty text
    StoryHint h;
    h.text = std::move(text);
    h.color = color;
    h.sound = StoryHint::Sound::Message;
    hint_queue_.push_back(std::move(h));
}

void StoryPresenter::lua_highlight(int32_t object, bool on, bool small, float z_offset) {
    std::erase_if(highlights_, [&](const StoryHighlight& h) { return h.object == object && h.small == small; });
    if (on && object != 0) highlights_.push_back({{}, object, small, z_offset});
}

void StoryPresenter::update(double now, bool hints_blocked) {
    std::erase_if(lines_, [&](const StoryTextLine& l) { return !l.persistent && now >= l.start + l.seconds; });
    std::erase_if(pointers_, [&](const StoryPointer& q) { return q.until >= 0.0 && now >= q.until; });
    std::erase_if(flashes_, [&](const StoryComponentFlash& f) { return now >= f.until; });
    std::erase_if(object_flashes_, [&](const StoryObjectFlash& f) { return now >= f.until; });
    std::erase_if(map_flashes_, [&](const StoryObjectFlash& f) { return now >= f.until; });
    if (hint_shown_ && now >= shown_.start + shown_.seconds + kHintFadeSeconds) hint_shown_ = false;
    if (!hint_up(now) && !hints_blocked && !hint_queue_.empty()) {  // q6 t36: a fading text is replaced at once
        shown_.hint = std::move(hint_queue_.front());
        hint_queue_.erase(hint_queue_.begin());
        shown_.start = now;
        shown_.seconds = static_cast<double>(shown_.hint.text.size()) * config_.advisor_hint_duration;  // characters x Advisor_Hint_Duration
        hint_shown_ = true;
        hint_started_ = true;
    }
}

uint8_t StoryPresenter::hint_alpha(double now) const {
    if (!hint_shown_) return 0;
    const double t = now - shown_.start;
    double a = 1.0;
    if (t < kHintFadeSeconds) a = t / kHintFadeSeconds;
    else if (t > shown_.seconds) a = 1.0 - (t - shown_.seconds) / kHintFadeSeconds;
    return static_cast<uint8_t>(std::clamp(a, 0.0, 1.0) * 255.0 + 0.5);
}

void StoryPresenter::clear_shown_hint(std::string_view movie) {
    if (!ascii_iequals(movie, "Light_Loop")) hint_shown_ = false;
}

void StoryPresenter::begin_mode(uint8_t mode) {
    if (mode >= 3) return;
    next_hint_[mode] = config_.first_hint_tick;  // 6.5: only the next-tick field is set; the hint index runs on across modes / battles
}

bool StoryPresenter::tick_advisor(uint8_t mode, uint32_t tick, const std::vector<std::string>& hints) {
    if (mode >= 3 || !advisor_[mode] || tick < next_hint_[mode]) return false;
    next_hint_[mode] = tick + static_cast<uint32_t>(static_cast<double>(kTicksPerSecond) * config_.advisor_hint_interval + 0.5);
    if (hints.empty()) return false;
    uint32_t& idx = hint_index_[mode];
    if (idx >= hints.size()) idx = 0;
    std::u32string t = hooks_.text ? hooks_.text(hints[idx]) : std::u32string();
    idx = static_cast<uint32_t>((idx + 1) % hints.size());
    if (t.empty()) return false;  // §6.3: the enqueue needs a non-empty text
    StoryHint h;
    h.text = std::move(t);
    h.color = colors_.hint;
    h.sound = StoryHint::Sound::Hint;
    hint_queue_.push_back(std::move(h));
    return true;
}

const std::vector<StoryTextLine>& StoryPresenter::lines(double now) const {
    live_.clear();
    for (const StoryTextLine& l : lines_)
        if (l.persistent || now < l.start + l.seconds) live_.push_back(l);
    return live_;
}

bool StoryPresenter::component_flashing(std::string_view component, double now) const {
    for (const StoryComponentFlash& f : flashes_)
        if (f.component == component && now >= f.start && now < f.until) return true;  // ref5 t02: a held DUAL_FLASH step starts later
    return false;
}

bool StoryPresenter::retreat_disabled(std::string_view faction) const { return retreat_off_.count(upper(faction)) != 0; }
bool StoryPresenter::corruption_hologram(std::string_view planet) const { return hologram_.count(upper(planet)) != 0; }
bool StoryPresenter::slot_forced(std::string_view planet, std::string_view slot) const {
    return slots_.count(upper(planet) + "|" + upper(slot)) != 0;
}

std::vector<StoryCameraCommand> StoryPresenter::take_camera_commands() { return std::exchange(camera_, {}); }
std::vector<StoryTutorialDialog> StoryPresenter::take_dialogs() { return std::exchange(dialogs_, {}); }
std::vector<std::string> StoryPresenter::take_forced_clicks() { return std::exchange(forced_clicks_, {}); }

// ---- ref4 t01: save / read ----
namespace {

constexpr uint32_t kPresenterSaveVersion = 2;  // 2 (ref5 t02): + the DUAL_FLASH state at the end; 1 still reads

void put_u32s(ByteWriter& w, std::u32string_view s) {
    w.u32(static_cast<uint32_t>(s.size()));
    for (const char32_t c : s) w.u32(static_cast<uint32_t>(c));
}
std::u32string get_u32s(ByteReader& r) {
    uint32_t n = 0;
    if (!r.count(n, 4)) {
        r.fail();
        return {};
    }
    std::u32string s(n, U'\0');
    for (char32_t& c : s) c = static_cast<char32_t>(r.u32());
    return s;
}
void put_color(ByteWriter& w, const StoryColor& c) { w.u8(c.r).u8(c.g).u8(c.b).u8(c.a); }
StoryColor get_color(ByteReader& r) {
    StoryColor c;
    c.r = r.u8();
    c.g = r.u8();
    c.b = r.u8();
    c.a = r.u8();
    return c;
}
// An enum stored as a byte; a value past `last` clears ok().
template <class E>
E get_enum(ByteReader& r, E last) {
    const uint8_t v = r.u8();
    if (v > static_cast<uint8_t>(last)) {
        r.fail();
        return static_cast<E>(0);
    }
    return static_cast<E>(v);
}
// A list: u32 count, then `put(element)` for each; get_list reads it back (each element at least `min_bytes`).
template <class T, class F>
void put_list(ByteWriter& w, const std::vector<T>& v, F&& put) {
    w.u32(static_cast<uint32_t>(v.size()));
    for (const T& e : v) put(e);
}
template <class T, class F>
bool get_list(ByteReader& r, std::vector<T>& out, size_t min_bytes, F&& get) {
    uint32_t n = 0;
    if (!r.count(n, min_bytes)) return false;
    out.clear();
    out.reserve(n);
    for (uint32_t i = 0; i < n && r.ok(); ++i) out.push_back(get());
    return r.ok();
}
void put_set(ByteWriter& w, const std::set<std::string>& s) {
    w.u32(static_cast<uint32_t>(s.size()));
    for (const std::string& e : s) w.str(e);
}
bool get_set(ByteReader& r, std::set<std::string>& out) {
    uint32_t n = 0;
    if (!r.count(n, 4)) return false;
    for (uint32_t i = 0; i < n && r.ok(); ++i) out.insert(r.str());
    return r.ok();
}

}  // namespace

void StoryPresenter::write(ByteWriter& w, double now) const {
    w.u32(kPresenterSaveVersion);
    put_list(w, lines_, [&](const StoryTextLine& l) {
        w.str(l.id);
        put_u32s(w, l.text);
        put_color(w, l.color);
        w.u8(static_cast<uint8_t>(l.channel)).f64(l.start - now).f64(l.seconds).boolean(l.persistent).boolean(l.typewriter);
    });
    put_list(w, arrows_, [&](const StoryArrow& a) {
        w.u8(static_cast<uint8_t>(a.kind)).str(a.target).str(a.region).i32(a.sub).str(a.label).u8(a.hud_mode).boolean(a.has_pointer);
    });
    put_list(w, pointers_, [&](const StoryPointer& q) {
        w.i32(q.id).str(q.object_type).f32(q.x).f32(q.y).f32(q.z).boolean(q.terrain);
        w.boolean(q.until >= 0.0).f64(q.until >= 0.0 ? q.until - now : 0.0);
    });
    put_list(w, flashes_, [&](const StoryComponentFlash& f) { w.str(f.component).f64(f.start - now).f64(f.until - now); });
    put_list(w, highlights_, [&](const StoryHighlight& h) { w.str(h.type_name).i32(h.object).boolean(h.small).f32(h.z_offset); });
    for (const std::vector<StoryObjectFlash>* v : {&object_flashes_, &map_flashes_})
        put_list(w, *v, [&](const StoryObjectFlash& f) { w.str(f.type_name).f64(f.start - now).f64(f.until - now); });
    put_list(w, forced_clicks_, [&](const std::string& s) { w.str(s); });
    put_list(w, camera_, [&](const StoryCameraCommand& c) { w.u8(static_cast<uint8_t>(c.kind)).str(c.type).f32(c.ox).f32(c.oy).f32(c.oz); });
    put_list(w, dialogs_, [&](const StoryTutorialDialog& d) { w.str(d.text_id).boolean(d.continue_button); });
    const auto put_hint = [&](const StoryHint& h) {
        put_u32s(w, h.text);
        put_color(w, h.color);
        w.u8(static_cast<uint8_t>(h.sound));
    };
    put_list(w, hint_queue_, put_hint);
    w.boolean(hint_shown_).boolean(hint_started_);
    put_hint(shown_.hint);
    w.f64(shown_.start - now).f64(shown_.seconds);
    w.boolean(controls_locked_).boolean(scroll_locked_).boolean(planet_selection_locked_).boolean(objectives_shown_);
    w.i32(command_bar_hide_).u32(text_hint_sounds_);
    w.boolean(objective_event_.has_value()).boolean(objective_event_.value_or(false));
    w.boolean(cinematic_mode_).boolean(movies_disabled_).boolean(retry_dialog_).boolean(retry_requested_);
    w.boolean(hide_cursor_on_click_).boolean(hide_autoresolve_);
    for (size_t m = 0; m < 3; ++m) w.boolean(advisor_[m]).u32(next_hint_[m]).u32(hint_index_[m]);
    put_set(w, retreat_off_);
    put_set(w, hologram_);
    put_set(w, slots_);
    w.u64(handled_);
    w.boolean(dual_.armed).f64(dual_.time).f64(dual_.rate);  // v2
    put_list(w, dual_.held, [&](const DualHeld& d) { w.boolean(d.planet).str(d.target); });
}

bool StoryPresenter::read(ByteReader& r, double now) {
    const uint32_t version = r.u32();
    if (version < 1 || version > kPresenterSaveVersion || !r.ok()) return false;
    StoryPresenter t;
    bool ok = get_list(r, t.lines_, 30, [&] {
        StoryTextLine l;
        l.id = r.str();
        l.text = get_u32s(r);
        l.color = get_color(r);
        l.channel = get_enum(r, StoryTextChannel::AlienSpeech);
        l.start = r.f64() + now;
        l.seconds = r.f64();
        l.persistent = r.boolean();
        l.typewriter = r.boolean();
        return l;
    });
    ok = ok && get_list(r, t.arrows_, 19, [&] {
        StoryArrow a;
        a.kind = get_enum(r, StoryArrow::Kind::Unit);
        a.target = r.str();
        a.region = r.str();
        a.sub = r.i32();
        a.label = r.str();
        a.hud_mode = r.u8();
        a.has_pointer = r.boolean();
        return a;
    });
    ok = ok && get_list(r, t.pointers_, 30, [&] {
        StoryPointer q;
        q.id = r.i32();
        q.object_type = r.str();
        q.x = r.f32();
        q.y = r.f32();
        q.z = r.f32();
        q.terrain = r.boolean();
        const bool timed = r.boolean();
        const double rel = r.f64();
        q.until = timed ? rel + now : -1.0;
        return q;
    });
    ok = ok && get_list(r, t.flashes_, 20, [&] {
        StoryComponentFlash f;
        f.component = r.str();
        f.start = r.f64() + now;
        f.until = r.f64() + now;
        return f;
    });
    ok = ok && get_list(r, t.highlights_, 13, [&] {
        StoryHighlight h;
        h.type_name = r.str();
        h.object = r.i32();
        h.small = r.boolean();
        h.z_offset = r.f32();
        return h;
    });
    for (std::vector<StoryObjectFlash>* v : {&t.object_flashes_, &t.map_flashes_})
        ok = ok && get_list(r, *v, 20, [&] {
            StoryObjectFlash f;
            f.type_name = r.str();
            f.start = r.f64() + now;
            f.until = r.f64() + now;
            return f;
        });
    ok = ok && get_list(r, t.forced_clicks_, 4, [&] { return r.str(); });
    ok = ok && get_list(r, t.camera_, 17, [&] {
        StoryCameraCommand c;
        c.kind = get_enum(r, StoryCameraCommand::Kind::UnpauseGalactic);
        c.type = r.str();
        c.ox = r.f32();
        c.oy = r.f32();
        c.oz = r.f32();
        return c;
    });
    ok = ok && get_list(r, t.dialogs_, 5, [&] {
        StoryTutorialDialog d;
        d.text_id = r.str();
        d.continue_button = r.boolean();
        return d;
    });
    const auto get_hint = [&] {
        StoryHint h;
        h.text = get_u32s(r);
        h.color = get_color(r);
        h.sound = get_enum(r, StoryHint::Sound::Hint);
        return h;
    };
    ok = ok && get_list(r, t.hint_queue_, 9, get_hint);
    if (!ok) return false;
    t.hint_shown_ = r.boolean();
    t.hint_started_ = r.boolean();
    t.shown_.hint = get_hint();
    t.shown_.start = r.f64() + now;
    t.shown_.seconds = r.f64();
    t.controls_locked_ = r.boolean();
    t.scroll_locked_ = r.boolean();
    t.planet_selection_locked_ = r.boolean();
    t.objectives_shown_ = r.boolean();
    t.command_bar_hide_ = r.i32();
    t.text_hint_sounds_ = r.u32();
    const bool has_event = r.boolean(), event = r.boolean();
    if (has_event) t.objective_event_ = event;
    t.cinematic_mode_ = r.boolean();
    t.movies_disabled_ = r.boolean();
    t.retry_dialog_ = r.boolean();
    t.retry_requested_ = r.boolean();
    t.hide_cursor_on_click_ = r.boolean();
    t.hide_autoresolve_ = r.boolean();
    for (size_t m = 0; m < 3; ++m) {
        t.advisor_[m] = r.boolean();
        t.next_hint_[m] = r.u32();
        t.hint_index_[m] = r.u32();
    }
    if (!get_set(r, t.retreat_off_) || !get_set(r, t.hologram_) || !get_set(r, t.slots_)) return false;
    t.handled_ = r.u64();
    if (version >= 2) {
        t.dual_.armed = r.boolean();
        t.dual_.time = r.f64();
        t.dual_.rate = r.f64();
        if (!get_list(r, t.dual_.held, 5, [&] {
                DualHeld d;
                d.planet = r.boolean();
                d.target = r.str();
                return d;
            }))
            return false;
    }
    if (!r.ok()) return false;
    t.hooks_ = std::move(hooks_);
    t.colors_ = colors_;
    t.config_ = config_;
    *this = std::move(t);
    return true;
}

bool StoryPresenter::is_lasting_reward(std::string_view type) {
    static constexpr std::string_view kLasting[] = {
        "LOCK_CONTROLS", "SCROLL_LOCK", "LOCK_PLANET_SELECTION", "ENABLE_OBJECTIVE_DISPLAY", "SHOW_COMMAND_BAR",
        "DISABLE_MOVIES", "SET_ADVISOR", "START_CINEMATIC_MODE", "STOP_CINEMATIC_MODE", "HIDE_AUTORESOLVE",
        "ENABLE_GALACTIC_CORRUPTION_HOLOGRAM", "DISABLE_RETREAT", "USE_RETRY_DIALOG", "HIDE_CURSOR_ON_CLICK",
        "SHOW_SMUGGLE_SLOT", "HIDE_SMUGGLE_SLOT", "SHOW_RAID_SLOT", "HIDE_RAID_SLOT", "SHOW_STEAL_SLOT", "HIDE_STEAL_SLOT",
        "SHOW_SPECIAL_SLOT"};
    for (const std::string_view k : kLasting)
        if (ascii_iequals(type, k)) return true;
    return false;
}

std::vector<std::string> split_story_ids(std::string_view list) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : list) {
        if (c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!cur.empty()) out.push_back(std::exchange(cur, {}));
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

void load_story_advisor_config(const data::GameConstants& gc, StoryPresenter::Config& out) {
    out.advisor_hint_interval = std::max(0.0, static_cast<double>(gc.get_float("Advisor_Hint_Interval", static_cast<float>(out.advisor_hint_interval))));
    out.advisor_hint_duration = std::max(0.0, static_cast<double>(gc.get_float("Advisor_Hint_Duration", static_cast<float>(out.advisor_hint_duration))));
    out.flash_duration = std::max(0.0, static_cast<double>(gc.get_float("GUI_Flash_Duration", static_cast<float>(out.flash_duration))));
}

void load_story_colors(const data::XmlNode& gc, StoryPresenter::Colors& out) {
    const auto color = [&gc](std::string_view tag, StoryColor& c) {
        const std::string_view t = gc.child_text(tag);
        float v[4] = {0.0f, 0.0f, 0.0f, 255.0f};
        if (t.empty() || data::parse_floats(t, v, 4) < 3) return;
        const auto byte = [](float f) { return static_cast<uint8_t>(std::clamp(std::lround(f), 0L, 255L)); };
        c = StoryColor{byte(v[0]), byte(v[1]), byte(v[2]), byte(v[3])};
    };
    color("Hint_Text_Color", out.hint);
    color("System_Text_Color", out.system);
    color("Task_Text_Color", out.task);
    color("Speech_Text_Color", out.speech);
}

std::vector<std::string> faction_advisor_hints(const data::GameDatabase& db, std::string_view faction, uint8_t mode) {
    static constexpr const char* kTags[3] = {"Galactic_Advisor_Hints", "Space_Advisor_Hints", "Land_Advisor_Hints"};
    if (mode >= 3 || faction.empty()) return {};
    for (const data::GameObjectRecord* rec : db.find_game_objects(faction))
        if (rec->type == "Faction") return split_story_ids(rec->element.child_text(kTags[mode]));
    return {};
}

std::u32string changed_hands_text(std::u32string_view fmt, std::u32string_view planet, std::u32string_view from,
                                  std::u32string_view to, std::optional<int32_t> bonus) {
    const std::u32string_view strings[3] = {planet, from, to};
    size_t next = 0;
    bool number_used = false;
    std::u32string out;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != U'%' || i + 1 >= fmt.size()) {
            out.push_back(fmt[i]);
            continue;
        }
        const char32_t n = fmt[i + 1];
        if (n == U'%') {
            out.push_back(U'%');
            ++i;
        } else if (n == U's' || n == U'S' || (n == U'l' && i + 2 < fmt.size() && fmt[i + 2] == U's')) {
            if (next < 3) out.append(strings[next]);
            ++next;
            i += n == U'l' ? 2 : 1;
        } else if (n == U'd' || n == U'i') {
            if (bonus && !number_used) {
                for (const char ch : std::to_string(*bonus)) out.push_back(static_cast<char32_t>(static_cast<unsigned char>(ch)));
            }
            number_used = true;
            ++i;
        } else {
            out.push_back(fmt[i]);
        }
    }
    return out;
}

}  // namespace alamo::game
