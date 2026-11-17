#include "alamo/game/story_present.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "alamo/core/ascii.hpp"
#include "alamo/data/game_database.hpp"

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

size_t StoryTextLine::revealed(double now) const {
    if (!typewriter) return text.size();
    const double n = std::max(0.0, now - start) * kTypewriterCharsPerSecond;
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

bool StoryPresenter::on_reward(std::string_view type, const std::vector<std::string>& p, double now, uint8_t mode) {
    const std::string ty = ascii_upper(type);
    auto flag = [&](size_t n, bool def) { return param(p, n).empty() ? def : to_int(param(p, n)) != 0; };
    auto cam = [&](StoryCameraCommand::Kind k) {
        StoryCameraCommand c;
        c.kind = k;
        c.type = std::string(param(p, 1));
        if (!param(p, 2).empty()) parse_xyz(param(p, 2), c.ox, c.oy, c.oz);
        camera_.push_back(std::move(c));
    };
    bool ok = true;
    if (ty == "SCREEN_TEXT") add_text(ty, p, now);
    else if (ty == "MULTIMEDIA") add_text(ty, p, now);  // the text part; the speech and the head are story_reward_media's
    else if (ty == "FLASH_GUI") {
        const std::string_view c = param(p, 1);
        if (!c.empty()) {
            flashes_.push_back({std::string(c), now, now + kFlashSeconds * kFlashCount});
            StoryArrow a;
            a.kind = StoryArrow::Kind::Component;
            a.target = std::string(c);
            a.label = std::string(param(p, 2));
            add_arrow(std::move(a));
        }
    } else if (ty == "FLASH_PLANET_GUI") {
        if (!param(p, 1).empty()) {
            StoryArrow a;
            a.kind = StoryArrow::Kind::Planet;
            a.target = std::string(param(p, 1));
            a.region = upper(param(p, 2));
            a.sub = to_int(param(p, 3));
            a.label = std::string(param(p, 4));
            a.has_pointer = !param(p, 5).empty();
            // 3.4: every region but PLANET flashes its planet-panel entry (the port has no such panel: the planet itself flashes
            // [port]); AFFILIATION / CREDITS / COINS are no-ops; PLANET is the arrow alone.
            std::string_view r = a.region;  // the shipped scripts spell the regions FLASH_TROOPS, FLASH_PLANET ...
            if (r.starts_with("FLASH_")) r.remove_prefix(6);
            if (r != "PLANET" && r != "AFFILIATION" && r != "CREDITS" && r != "COINS")
                object_flashes_.push_back({a.target, now, now + kFlashSeconds * kFlashCount});
            add_arrow(std::move(a));
        }
    } else if (ty == "FLASH_FLEET_WITH_UNIT" || ty == "FLASH_SPECIAL_ABILITY" || ty == "FLASH_PRODUCTION_CHOICE") {
        if (!param(p, 1).empty()) {
            StoryArrow a;
            a.kind = ty == "FLASH_FLEET_WITH_UNIT" ? StoryArrow::Kind::Fleet
                     : ty == "FLASH_SPECIAL_ABILITY" ? StoryArrow::Kind::Ability
                                                     : StoryArrow::Kind::Production;
            a.target = std::string(param(p, 1));
            a.label = std::string(param(p, 2));
            add_arrow(std::move(a));
        }
    } else if (ty == "FLASH_ADVANCED_MAP_OBJECT") {
        if (!param(p, 1).empty()) {  // §3.8: flashed six times, no arrow
            map_flashes_.push_back({std::string(param(p, 1)), now, now + kFlashSeconds * 6});
        }
    } else if (ty == "FLASH_PLANET" || ty == "FLASH_OBJECT") {
        if (!param(p, 1).empty()) object_flashes_.push_back({std::string(param(p, 1)), now, now + 0.4 * 3});  // GUI_Flash_Duration
    } else if (ty == "FORCE_CLICK_GUI") {
        if (!param(p, 1).empty()) forced_clicks_.push_back(std::string(param(p, 1)));
    } else if (ty == "HIDE_TUTORIAL_CURSOR") {
        const std::string_view label = param(p, 1);
        const auto it = std::find_if(arrows_.begin(), arrows_.end(), [&](const StoryArrow& a) { return a.label == label; });
        if (it != arrows_.end()) arrows_.erase(it);
    } else if (ty == "HIDE_CURSOR_ON_CLICK") {
        hide_cursor_on_click_ = to_int(param(p, 1)) != 0;  // §3.11: no reader in retail
    } else if (ty == "HIGHLIGHT_OBJECT") {
        const std::string_view obj = param(p, 1);
        if (!obj.empty() && !param(p, 2).empty()) {
            const bool on = to_int(param(p, 2)) != 0;
            const int32_t pid = param(p, 3).empty() ? -1 : to_int(param(p, 3));
            auto same = [&](const StoryHighlight& h) { return h.object == 0 && ascii_iequals(h.type_name, obj); };
            std::erase_if(highlights_, same);
            if (on) highlights_.push_back({std::string(obj), 0, false, 0.0f});
            if (on && pid != -1) {
                std::erase_if(pointers_, [&](const StoryPointer& q) { return q.id == pid; });
                StoryPointer q;
                q.id = pid;
                q.object_type = std::string(obj);
                pointers_.push_back(std::move(q));
            } else if (!on && pid != -1) {
                std::erase_if(pointers_, [&](const StoryPointer& q) { return q.id == pid; });
            }
        }
    } else if (ty == "TUTORIAL_DIALOG") {
        if (!param(p, 1).empty()) {
            cinematic_mode_ = false;  // 4: a cinematic mode in force ends first
            dialogs_.push_back({std::string(param(p, 1)), to_int(param(p, 2)) != 0});
        }
    } else if (ty == "LOCK_CONTROLS") {
        if (!param(p, 1).empty()) controls_locked_ = to_int(param(p, 1)) != 0;
    } else if (ty == "SCROLL_LOCK") scroll_locked_ = to_int(param(p, 1)) != 0;
    else if (ty == "LOCK_PLANET_SELECTION") planet_selection_locked_ = to_int(param(p, 1)) != 0;
    else if (ty == "ENABLE_OBJECTIVE_DISPLAY") objective_event_ = objectives_shown_ = flag(1, true);
    else if (ty == "SHOW_COMMAND_BAR") {
        // 5.6: galactic = a plain show / hide; space and land = the hide counter (nested hides need matching shows)
        if (mode == 0) command_bar_hide_ = flag(1, false) ? 0 : 1;
        else if (flag(1, false)) command_bar_hide_ = std::max(0, command_bar_hide_ - 1);
        else ++command_bar_hide_;
    } else if (ty == "START_CINEMATIC_MODE") cinematic_mode_ = true;
    else if (ty == "STOP_CINEMATIC_MODE") cinematic_mode_ = false;
    else if (ty == "DISABLE_MOVIES") movies_disabled_ = to_int(param(p, 1)) != 0;
    else if (ty == "DISABLE_RETREAT") {
        if (!param(p, 1).empty() && !param(p, 2).empty()) {
            if (to_int(param(p, 2)) != 0) retreat_off_.insert(upper(param(p, 1)));
            else retreat_off_.erase(upper(param(p, 1)));
        }
    } else if (ty == "HIDE_AUTORESOLVE") hide_autoresolve_ = true;  // §5.3: no reader in retail
    else if (ty == "USE_RETRY_DIALOG") retry_dialog_ = true;
    else if (ty == "ACTIVATE_RETRY_DIALOG") retry_requested_ = true;
    else if (ty == "ENABLE_GALACTIC_CORRUPTION_HOLOGRAM") {
        if (!param(p, 1).empty()) {
            if (flag(2, true)) hologram_.insert(upper(param(p, 1)));
            else hologram_.erase(upper(param(p, 1)));
        }
    } else if (ty == "SHOW_SMUGGLE_SLOT" || ty == "HIDE_SMUGGLE_SLOT" || ty == "SHOW_RAID_SLOT" || ty == "HIDE_RAID_SLOT" ||
               ty == "SHOW_STEAL_SLOT" || ty == "HIDE_STEAL_SLOT") {
        const std::string slot = ty.find("SMUGGLE") != std::string::npos ? "SMUGGLE" : ty.find("RAID") != std::string::npos ? "RAID" : "STEAL";
        if (!param(p, 1).empty()) {
            const std::string key = upper(param(p, 1)) + "|" + slot;
            if (ty.rfind("SHOW", 0) == 0) slots_.insert(key);
            else slots_.erase(key);
        }
    } else if (ty == "SHOW_SPECIAL_SLOT") {
        if (!param(p, 1).empty() && !param(p, 2).empty()) {
            std::string slot = upper(param(p, 2));
            if (slot.size() > 5 && slot.compare(slot.size() - 5, 5, "_SLOT") == 0) slot.resize(slot.size() - 5);
            const std::string key = upper(param(p, 1)) + "|" + slot;
            if (param(p, 3).empty() || to_int(param(p, 3)) != 0) slots_.insert(key);
            else slots_.erase(key);
        }
    } else if (ty == "SELECT_PLANET") cam(StoryCameraCommand::Kind::SelectPlanet);
    else if (ty == "ZOOM_IN") cam(StoryCameraCommand::Kind::ZoomIn);
    else if (ty == "ZOOM_OUT") cam(StoryCameraCommand::Kind::ZoomOut);
    else if (ty == "POSITION_CAMERA") cam(StoryCameraCommand::Kind::Position);
    else if (ty == "SCROLL_CAMERA") cam(StoryCameraCommand::Kind::Scroll);
    else if (ty == "PAUSE_GAME") cam(StoryCameraCommand::Kind::PauseGame);
    else if (ty == "PAUSE_GALACTIC") cam(StoryCameraCommand::Kind::PauseGalactic);
    else if (ty == "UNPAUSE_GALACTIC") cam(StoryCameraCommand::Kind::UnpauseGalactic);
    else if (ty == "PAUSE_GALACTIC_GAME") {
        if (!param(p, 1).empty() && to_int(param(p, 1)) == 0) cam(StoryCameraCommand::Kind::UnpauseGalactic);
        else cam(StoryCameraCommand::Kind::PauseGalactic);
    } else if (ty == "SET_ADVISOR") {
        if (!param(p, 1).empty() && mode < 3) advisor_[mode] = to_int(param(p, 1)) != 0;
    } else ok = false;
    if (ok) ++handled_;
    return ok;
}

void StoryPresenter::remove_all_text() { lines_.clear(); }

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
        if (f.component == component && now < f.until) return true;
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
