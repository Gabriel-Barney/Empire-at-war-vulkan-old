// alamo_game host, ref6 t04: one settings module per options page. A page names the settings struct its dialog fills and
// reads (alamo/ui/gui_menus.hpp), owns its keys in the options file (game_options.txt, OptionsProfile in game_display.hpp)
// and has one apply hook on the App (App::apply_<page>, app_options.cpp), called both when the dialog is accepted and when
// the profile is loaded. The two game setup pages (campaign advanced options, skirmish options) are not kept between runs:
// their page converts the accepted dialog into what the session start takes. The Display and Graphics pages keep their
// own file (VideoProfile, video_options.txt) and apply hook (App::apply_display / ApplyVideo); they are not listed here.
// Adding a setting to a page: the struct field, the page's load / save below and its apply hook; nothing else.
#pragma once
#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "alamo/game/session_spec.hpp"
#include "alamo/sim/tactical_battle.hpp"
#include "alamo/ui/gui_menus.hpp"
#include "alamo/ui/hotkeys.hpp"

namespace alamo::game {

// The options file's key = value lines: blank lines and '#' comments skipped, key and value trimmed. A key with an empty
// value counts as missing.
class OptionsKeys {
public:
    static OptionsKeys parse(std::istream& in);
    const std::string* find(std::string_view key) const;
    std::optional<bool> flag(std::string_view key) const;  // "1", "true" or "on" = true
    std::optional<int32_t> integer(std::string_view key) const;
    std::optional<float> real(std::string_view key) const;
    const std::map<std::string, std::string, std::less<>>& all() const { return kv_; }

private:
    std::map<std::string, std::string, std::less<>> kv_;
};
// A float as the options file writes it ("%.4f").
std::string options_real(float v);

// Options > Audio (IDD_AUDIO_OPTIONS_DIALOG): volumes and subtitles. Keys volume_master / _sfx / _speech / _music, subtitles.
struct AudioPage {
    using Settings = ui::AudioVolumes;
    static void load(const OptionsKeys& k, Settings& s);
    static void save(std::ostream& out, const Settings& s);
};

// Options > Gameplay (IDD_GAMEPLAY_OPTIONS_DIALOG): speeds, difficulty and the checks. Keys game_speed, strategic_speed,
// scroll_speed, difficulty (clamped 0..2), then one key per check.
struct GameplayPage {
    using Settings = ui::GameplaySettings;
    static void load(const OptionsKeys& k, Settings& s);
    static void save(std::ostream& out, const Settings& s);
};

// [port: Gabriel] Options > Camera and controls (IDD_PORT_CAMERA_CONTROLS_DIALOG): FOVs (clamped to their ranges), the
// queue modifier and the path lines; tactical_fov_set = the player moved the tactical FOV off TacticalCameras.xml's.
struct CameraPage {
    struct Settings {
        ui::CameraSettings camera;
        bool tactical_fov_set = false;
    };
    static void load(const OptionsKeys& k, Settings& s);
    static void save(std::ostream& out, const Settings& s);
};

// The keyboard dialogs: the HotkeyTable profile, one "key.<name>" line per entry.
struct KeysPage {
    using Settings = ui::HotkeyProfile;
    static void load(const OptionsKeys& k, Settings& s);
    static void save(std::ostream& out, const Settings& s);
};

// modc t47: the skirmish staging dialog's local slot (faction, colour), kept as retail's profile keeps them. Keys
// skirmish_faction, skirmish_colour (clamped -1..8); written only once set.
struct SkirmishSlotPage {
    using Settings = ui::SkirmishProfile;
    static void load(const OptionsKeys& k, Settings& s);
    static void save(std::ostream& out, const Settings& s);
};

// ui1 t06: IDD_CAMPAIGN_ADVANCED_OPTIONS_DIALOG. Not kept between runs: an accepted dialog (set) becomes the campaign
// start's options, else the campaign's own (CampaignOptions::set false).
struct CampaignOptionsPage {
    using Settings = ui::GameOptionsSetup;
    static CampaignOptions session_options(const Settings& s);
};

// skm1 t02: the skirmish staging screen's options dialog. Not kept between runs: an accepted dialog becomes the battle's
// skirmish options (credits at least 0, the win condition as its byte), else GameSession takes the MP_Default_* values.
struct SkirmishOptionsPage {
    using Settings = ui::SkirmishOptionsSetup;
    static sim::BattleConfig::SkirmishOptions battle_options(const Settings& s);
};

}  // namespace alamo::game
