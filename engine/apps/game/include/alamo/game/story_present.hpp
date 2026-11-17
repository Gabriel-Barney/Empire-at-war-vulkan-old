// ui2: the host side of the story presentation rewards (docs/re/STORY_PRESENTATION_REWARDS.md): what retail's reward
// dispatcher (0x456970) does to the local HUD / camera / input when a story event fires. Presentation only (rule 3):
// nothing here reads or writes sim state; the fired reward log is the input, the state below is what the host draws and
// obeys. The class is data-free and clock-free (`now` is the host's wall time in seconds), so its dispatch is covered by
// tests without a window.
//
//   Text         SCREEN_TEXT / MULTIMEDIA text lines (§1.1): channels HINT / SYSTEM / TASK / ALIEN_SPEECH, seconds (-1 =
//                persistent), P4 'remove' = delete every line with that text id, typewriter flag, colour override; Lua
//                Remove_All_Text. The Lua Game_Message is NOT a line: it joins the advisor hint queue (§1.5, §6.3).
//   Pointers     the arrow slots of FLASH_GUI / FLASH_PLANET_GUI / FLASH_FLEET_WITH_UNIT / FLASH_SPECIAL_ABILITY /
//                FLASH_PRODUCTION_CHOICE (ten slots, §3.2; the flash id is only a label, HIDE_TUTORIAL_CURSOR hides the first
//                slot with that label), the numeric 2D pointers of HIGHLIGHT_OBJECT / FLASH_TERRAIN, component flashes
//                (0.5 s x 3), object highlights (reward and Lua Highlight / Highlight_Small).
//   Locks        LOCK_CONTROLS, SCROLL_LOCK, LOCK_PLANET_SELECTION, ENABLE_OBJECTIVE_DISPLAY, SHOW_COMMAND_BAR (a hide
//                counter), START / STOP_CINEMATIC_MODE, DISABLE_MOVIES, DISABLE_RETREAT (by faction), the HIDE / SHOW slots.
//   Camera       POSITION_CAMERA / SCROLL_CAMERA / ZOOM_IN / ZOOM_OUT / SELECT_PLANET / PAUSE_*: queued commands the host's
//                camera code executes (take_camera_commands()).
//   Advisor      SET_ADVISOR (per game mode, §6.1), the hint queue and its show rules (§6.3), the periodic hint (§6.4).
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace alamo::data {
struct GameConstants;
class GameDatabase;
}  // namespace alamo::data

namespace alamo::game {

struct StoryColor {
    uint8_t r = 255, g = 255, b = 255, a = 255;
    friend bool operator==(const StoryColor&, const StoryColor&) = default;
};

// SCREEN_TEXT P7 (§1.1). Subtitle = empty / unknown: shown only when the movie-subtitles option is on.
enum class StoryTextChannel : uint8_t { Subtitle, Hint, System, Task, AlienSpeech };

struct StoryTextLine {
    std::string id;       // the text id (P1): removal compares ids, not displayed text
    std::u32string text;  // resolved
    StoryColor color;
    StoryTextChannel channel = StoryTextChannel::Subtitle;
    double start = 0.0;    // host time of the add
    double seconds = 5.0;  // lifetime from `start` (ignored when persistent)
    bool persistent = false;
    bool typewriter = true;
    // Characters revealed at `now` (the whole text when not a typewriter line). [port] the retail rate (global
    // 0x140b15ab4) was not read: kTypewriterCharsPerSecond.
    size_t revealed(double now) const;
};

// One of the ten tutorial arrow slots (§3.2).
struct StoryArrow {
    enum class Kind : uint8_t { Component, Planet, Fleet, Production, Ability, MapObject };
    Kind kind = Kind::Component;
    std::string target;  // component name / planet type / unit type / ability / object type
    std::string region;  // FLASH_PLANET_GUI P2 (upper case)
    int32_t sub = 0;     // FLASH_PLANET_GUI P3
    std::string label;   // the flash id (HIDE_TUTORIAL_CURSOR compares it, case-sensitive)
    uint8_t hud_mode = 0;
    bool has_pointer = true;  // FLASH_PLANET_GUI P5: an arrow as well as the flash (FLASH_GUI and the others always)
    friend bool operator==(const StoryArrow&, const StoryArrow&) = default;
};

// A numeric 2D pointer (HIGHLIGHT_OBJECT P3, FLASH_TERRAIN): at an object type's first instance or at a world position.
struct StoryPointer {
    int32_t id = -1;
    std::string object_type;  // empty for a terrain pointer
    float x = 0, y = 0, z = 0;
    bool terrain = false;
    double until = -1.0;  // host time; < 0 = persistent
    friend bool operator==(const StoryPointer&, const StoryPointer&) = default;
};

// A component flash (FLASH_GUI §3.1: 0.5 s per flash, 3 flashes).
struct StoryComponentFlash {
    std::string component;
    double start = 0.0, until = 0.0;
    friend bool operator==(const StoryComponentFlash&, const StoryComponentFlash&) = default;
};

// FLASH_PLANET / FLASH_OBJECT (§2.2: the object's colour flash) and FLASH_ADVANCED_MAP_OBJECT (§3.8): by object type.
struct StoryObjectFlash {
    std::string type_name;
    double start = 0.0, until = 0.0;
    friend bool operator==(const StoryObjectFlash&, const StoryObjectFlash&) = default;
};

// An object highlight: by object type name (HIGHLIGHT_OBJECT: the first instance) or by object id (Lua Highlight).
struct StoryHighlight {
    std::string type_name;  // reward form
    int32_t object = 0;     // Lua form (object id), 0 when by type
    bool small = false;     // Object_Highlight_Small (Lua Highlight_Small)
    float z_offset = 0.0f;
    friend bool operator==(const StoryHighlight&, const StoryHighlight&) = default;
};

struct StoryCameraCommand {
    enum class Kind : uint8_t { Position, Scroll, ZoomIn, ZoomOut, SelectPlanet, PauseGame, PauseGalactic, UnpauseGalactic };
    Kind kind = Kind::Position;
    std::string type;  // planet / object type (P1; empty = the event's own position)
    float ox = 0, oy = 0, oz = 0;  // POSITION_CAMERA / SCROLL_CAMERA P2 "x,y,z"
    friend bool operator==(const StoryCameraCommand&, const StoryCameraCommand&) = default;
};

// TUTORIAL_DIALOG (§4): IDD_TUTORIAL_DIALOG with the text and an optional Continue button (its click raises the story's
// generic trigger kContinueTutorial).
struct StoryTutorialDialog {
    std::string text_id;
    bool continue_button = false;
    friend bool operator==(const StoryTutorialDialog&, const StoryTutorialDialog&) = default;
};

// A queued advisor hint (§6.3).
struct StoryHint {
    std::u32string text;
    StoryColor color;
    enum class Sound : uint8_t { Message, Urgent, Hint } sound = Sound::Message;
};

// The hint on screen: shown for characters x Advisor_Hint_Duration seconds, then fades out in alpha steps of 8.
struct StoryShownHint {
    StoryHint hint;
    double start = 0.0, seconds = 0.0;
};

inline constexpr std::string_view kContinueTutorial = "Continue_Tutorial";
inline constexpr double kTypewriterCharsPerSecond = 40.0;  // [port] retail's rate (0x140b15ab4) not read
inline constexpr double kFlashSeconds = 0.5;               // §3.1
inline constexpr int kFlashCount = 3;
inline constexpr size_t kArrowSlots = 10;                  // §3.2
inline constexpr double kHintFadeSeconds = 0.5;

class StoryPresenter {
public:
    struct Hooks {
        std::function<std::u32string(std::string_view id)> text;       // localized text of an id ("" = missing)
        std::function<std::u32string(std::string_view type)> type_name;  // SCREEN_TEXT P3: the type's display name
    };
    struct Colors {  // GameConstants Hint_Text_Color / System_ / Task_ / Speech_Text_Color (§1.1)
        StoryColor hint{0, 193, 91, 255}, system{255, 24, 39, 255}, task{109, 247, 255, 255}, speech{255, 255, 255, 255};
    };
    struct Config {
        double advisor_hint_duration = 0.1;   // GameConstants Advisor_Hint_Duration, seconds per character
        double advisor_hint_interval = 300.0; // Advisor_Hint_Interval, seconds (ticks at kTicksPerSecond)
        uint32_t first_hint_tick = 90;        // §6.5: player +0x3e0 := 0x5a at mode start
        bool subtitles = true;                // IDC_USE_MOVIE_SUBTITLES
    };
    static constexpr uint32_t kTicksPerSecond = 30;

    void set_hooks(Hooks h) { hooks_ = std::move(h); }
    void set_colors(const Colors& c) { colors_ = c; }
    Config& config() { return config_; }
    const Config& config() const { return config_; }

    // A new story / session: everything back to the defaults.
    void reset();

    // One fired reward (type upper case as in StoryFired; params[0] = Reward_Param1). `now` = host seconds, `mode` = the
    // game mode type of the plot (0 galactic, 1 space, 2 land; SET_ADVISOR's slot). Returns true when this class handles
    // the reward (the media rewards START_MOVIE / SPEECH playback stay with story_reward_media).
    bool on_reward(std::string_view type, const std::vector<std::string>& params, double now, uint8_t mode);

    // Lua presentation calls (presentation.hpp).
    void remove_all_text();                                     // Remove_All_Text
    void game_message(std::string_view text_id);                // Game_Message: front of the hint queue, white
    void lua_highlight(int32_t object, bool on, bool small, float z_offset);  // GameObjectWrapper.Highlight(_Small)

    // Per frame: expires lines, pointers and flashes; shows the next hint when `hints_blocked` is false (retail: no Bink
    // movie open, no advanced map, no cinematic mode).
    void update(double now, bool hints_blocked);

    // §6.4 periodic hint: call once per tick of the local human's mode; `hints` = that mode's faction hint ids.
    // Returns true when a hint was queued (at the back, the Hint sound).
    bool tick_advisor(uint8_t mode, uint32_t tick, const std::vector<std::string>& hints);
    // COMMANDBAR_MOVIE / MULTIMEDIA's head (6.2): the hint text on screen goes first, unless the movie is "Light_Loop".
    void clear_shown_hint(std::string_view movie);
    uint32_t next_hint_index(uint8_t mode) const { return mode < 3 ? hint_index_[mode] : 0; }
    void begin_mode(uint8_t mode);  // the mode started: the first hint is due config().first_hint_tick ticks later

    // ---- state the host draws / obeys ----
    const std::vector<StoryTextLine>& lines(double now) const;  // not expired
    const std::vector<StoryTextLine>& lines() const { return lines_; }
    const std::vector<StoryArrow>& arrows() const { return arrows_; }
    const std::vector<StoryPointer>& pointers() const { return pointers_; }
    const std::vector<StoryComponentFlash>& flashes() const { return flashes_; }
    const std::vector<StoryHighlight>& highlights() const { return highlights_; }
    const std::vector<StoryObjectFlash>& object_flashes() const { return object_flashes_; }
    const std::vector<StoryObjectFlash>& map_flashes() const { return map_flashes_; }
    bool component_flashing(std::string_view component, double now) const;
    // The hint on screen (nullptr: none) and its alpha at `now`: fades in over kHintFadeSeconds, holds, fades out
    // [port] retail steps the alpha by 8 per GUI update, about half a second at 60 fps.
    const StoryShownHint* shown_hint() const { return hint_shown_ ? &shown_ : nullptr; }
    uint8_t hint_alpha(double now) const;
    size_t hint_queue_size() const { return hint_queue_.size(); }
    bool controls_locked() const { return controls_locked_; }
    bool scroll_locked() const { return scroll_locked_; }
    bool planet_selection_locked() const { return planet_selection_locked_; }
    bool objectives_shown() const { return objectives_shown_; }
    // 5.5: ENABLE_OBJECTIVE_DISPLAY writes the byte the objectives key (S) toggles: the host applies the reward's value to that
    // one flag, once (so the key can undo it and it can undo the key).
    std::optional<bool> take_objective_display() { return std::exchange(objective_event_, std::nullopt); }
    bool command_bar_hidden() const { return command_bar_hide_ > 0; }
    bool cinematic_mode() const { return cinematic_mode_; }
    bool movies_disabled() const { return movies_disabled_; }
    bool retry_dialog_used() const { return retry_dialog_; }
    bool retry_dialog_requested() const { return retry_requested_; }
    // 1.1: a HINT-channel text line plays the SFX event "GUI_Text_Hint_SFX" when it is added: how many since the last call.
    uint32_t take_text_hint_sounds() { return std::exchange(text_hint_sounds_, 0u); }
    bool take_retry_request() { return std::exchange(retry_requested_, false); }  // ACTIVATE_RETRY_DIALOG reward: show it once
    bool hide_cursor_on_click() const { return hide_cursor_on_click_; }
    bool hide_autoresolve() const { return hide_autoresolve_; }
    bool advisor_enabled(uint8_t mode) const { return mode < 3 && advisor_[mode]; }
    bool retreat_disabled(std::string_view faction) const;  // DISABLE_RETREAT: that faction cannot retreat
    bool corruption_hologram(std::string_view planet) const;
    // SHOW_*_SLOT / SHOW_SPECIAL_SLOT: the slot (RAID, SMUGGLE, STEAL, BLACK_MARKET, SABOTAGE, CORRUPTION, ...) is forced
    // visible on that planet (§5.7).
    bool slot_forced(std::string_view planet, std::string_view slot) const;
    std::vector<StoryCameraCommand> take_camera_commands();
    std::vector<StoryTutorialDialog> take_dialogs();
    std::vector<std::string> take_forced_clicks();  // FORCE_CLICK_GUI: the components to click, in order
    bool hint_started() { return std::exchange(hint_started_, false); }  // a hint was shown since the last call (sound cue)
    // The count of rewards handled / ignored since reset (tests, probes).
    uint64_t handled() const { return handled_; }

private:
    void add_text(std::string_view type, const std::vector<std::string>& p, double now);
    void add_arrow(StoryArrow a);
    std::u32string resolve(std::string_view id) const;

    Hooks hooks_;
    Colors colors_;
    Config config_;
    std::vector<StoryTextLine> lines_;
    std::vector<StoryArrow> arrows_;
    std::vector<StoryPointer> pointers_;
    std::vector<StoryComponentFlash> flashes_;
    std::vector<StoryHighlight> highlights_;
    std::vector<StoryObjectFlash> object_flashes_, map_flashes_;
    std::vector<std::string> forced_clicks_;
    bool hint_started_ = false;
    std::vector<StoryCameraCommand> camera_;
    std::vector<StoryTutorialDialog> dialogs_;
    std::vector<StoryHint> hint_queue_;
    bool hint_shown_ = false;
    StoryShownHint shown_;
    bool controls_locked_ = false, scroll_locked_ = false, planet_selection_locked_ = false, objectives_shown_ = true;
    int command_bar_hide_ = 0;
    uint32_t text_hint_sounds_ = 0;
    std::optional<bool> objective_event_;
    bool cinematic_mode_ = false, movies_disabled_ = false, retry_dialog_ = false, retry_requested_ = false;
    bool hide_cursor_on_click_ = false, hide_autoresolve_ = false;
    std::array<bool, 3> advisor_{true, true, true};  // [port] default on: the retail default was not read
    std::array<uint32_t, 3> next_hint_{90, 90, 90};
    std::array<uint32_t, 3> hint_index_{0, 0, 0};
    std::set<std::string> retreat_off_, hologram_;  // upper case
    std::set<std::string> slots_;                    // "PLANET|SLOT" upper case
    mutable std::vector<StoryTextLine> live_;
    uint64_t handled_ = 0;
};

// ---- data readers (ui2 t07, STORY_PRESENTATION_REWARDS.md 6.4) ----
// A comma / whitespace separated id list ("A, B C" -> A, B, C).
std::vector<std::string> split_story_ids(std::string_view list);
// GameConstants Advisor_Hint_Interval (seconds, shipped 300) and Advisor_Hint_Duration (seconds per character, shipped 0.1).
void load_story_advisor_config(const data::GameConstants& gc, StoryPresenter::Config& out);
// The faction's hint text ids for a game mode (0 galactic, 1 space, 2 land): Galactic_ / Space_ / Land_Advisor_Hints.
std::vector<std::string> faction_advisor_hints(const data::GameDatabase& db, std::string_view faction, uint8_t mode);

}  // namespace alamo::game
