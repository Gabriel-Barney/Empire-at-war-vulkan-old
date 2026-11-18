// alamo_game_story_present_tests (data-free): StoryPresenter (story_present.hpp), the host side of the story presentation
// rewards (docs/re/STORY_PRESENTATION_REWARDS.md). One block per spec section.
#include <string>
#include <vector>

#include "alamo/data/xml.hpp"
#include "alamo/game/story_present.hpp"
#include "alamo/test/support.hpp"

using namespace alamo;

namespace {

using P = std::vector<std::string>;

game::StoryPresenter make() {
    game::StoryPresenter s;
    game::StoryPresenter::Hooks h;
    h.text = [](std::string_view id) -> std::u32string {
        if (id == "TEXT_MISSING") return {};
        std::u32string r = U"<";
        for (const char c : id) r.push_back(static_cast<char32_t>(c));
        return r + U">";
    };
    h.type_name = [](std::string_view t) -> std::u32string { return t == "X_WING" ? U"X-wing" : U""; };
    s.set_hooks(h);
    return s;
}

void text_test() {
    alamo::test::section("SCREEN_TEXT lines (1.1)");
    game::StoryPresenter s = make();
    CHECK(s.on_reward("SCREEN_TEXT", P{"TEXT_A", "10", "", "", "", "", "Hint"}, 1.0, 0));
    CHECK(s.lines().size() == 1);
    const game::StoryTextLine l = s.lines()[0];
    CHECK(l.id == "TEXT_A" && l.text == U"<TEXT_A>");
    CHECK(l.channel == game::StoryTextChannel::Hint && l.color == (game::StoryColor{0, 193, 91, 255}));
    CHECK(l.seconds == 10.0 && !l.persistent && l.typewriter);
    CHECK(s.lines(10.9).size() == 1 && s.lines(11.0).empty());  // expires at start + seconds
    // A repeated id never stacks.
    s.on_reward("SCREEN_TEXT", P{"TEXT_A", "5", "", "", "", "", "Task"}, 2.0, 0);
    CHECK(s.lines().size() == 1 && s.lines()[0].channel == game::StoryTextChannel::Task && s.lines()[0].start == 2.0);
    CHECK(s.lines()[0].color == (game::StoryColor{109, 247, 255, 255}));
    // -1 = persistent; the typewriter flag P5 = 0 shows at once; P6 overrides the colour; P2 default 5 s.
    s.on_reward("SCREEN_TEXT", P{"TEXT_B", "-1", "", "", "0", "1,2,3", "System"}, 3.0, 0);
    s.on_reward("SCREEN_TEXT", P{"TEXT_C"}, 3.0, 0);
    CHECK(s.lines().size() == 3);
    CHECK(s.lines()[1].persistent && !s.lines()[1].typewriter && s.lines()[1].color == (game::StoryColor{1, 2, 3, 255}));
    CHECK(s.lines()[1].revealed(3.0) == s.lines()[1].text.size());
    CHECK(s.lines()[2].seconds == 5.0 && s.lines()[2].channel == game::StoryTextChannel::Subtitle);
    CHECK(s.lines()[2].revealed(3.0) == 0 && s.lines()[2].revealed(4.0) > 0);
    s.update(100.0, false);  // the persistent line stays, the timed ones go
    CHECK(s.lines().size() == 1 && s.lines()[0].id == "TEXT_B");
    // P4 given = remove every line with that id.
    s.on_reward("SCREEN_TEXT", P{"TEXT_B", "", "", "remove"}, 101.0, 0);
    CHECK(s.lines().empty());
    // No id: nothing. Missing text: the id itself [L].
    s.on_reward("SCREEN_TEXT", P{""}, 0.0, 0);
    CHECK(s.lines().empty());
    s.on_reward("SCREEN_TEXT", P{"TEXT_MISSING"}, 0.0, 0);
    CHECK(s.lines().size() == 1 && s.lines()[0].text == U"TEXT_MISSING");
    // P3: the type's display name replaces %s in the text.
    game::StoryPresenter f;
    game::StoryPresenter::Hooks h;
    h.text = [](std::string_view) { return std::u32string(U"Build an %s now"); };
    h.type_name = [](std::string_view) { return std::u32string(U"X-wing"); };
    f.set_hooks(h);
    f.on_reward("SCREEN_TEXT", P{"TEXT_F", "5", "X_WING"}, 0.0, 0);
    CHECK(f.lines().size() == 1 && f.lines()[0].text == U"Build an X-wing now");
    // 1.1: a HINT-channel line plays GUI_Text_Hint_SFX once when added.
    CHECK(f.take_text_hint_sounds() == 0);
    f.on_reward("SCREEN_TEXT", P{"TEXT_H", "5", "", "", "", "", "HINT"}, 0.0, 0);
    CHECK(f.take_text_hint_sounds() == 1 && f.take_text_hint_sounds() == 0);
    // Remove_All_Text clears them.
    s.remove_all_text();
    CHECK(s.lines().empty());
    // An unknown channel is a subtitle: hidden with the option off; the named channels always show.
    s.config().subtitles = false;
    s.on_reward("SCREEN_TEXT", P{"TEXT_D", "5", "", "", "", "", ""}, 0.0, 0);
    CHECK(s.lines().empty());
    s.on_reward("SCREEN_TEXT", P{"TEXT_D", "5", "", "", "", "", "task"}, 0.0, 0);
    CHECK(s.lines().size() == 1);
    // MULTIMEDIA's text part is a SCREEN_TEXT (1.3).
    s.remove_all_text();
    s.on_reward("MULTIMEDIA", P{"TEXT_M", "-1", "", "", "", "", "Hint", "SPEECH_EVT", "Han_Loop", "1"}, 0.0, 0);
    CHECK(s.lines().size() == 1 && s.lines()[0].persistent);
    // Unknown rewards are not handled.
    CHECK(!s.on_reward("CREDITS", P{"100"}, 0.0, 0));
}

void pointer_test() {
    alamo::test::section("flash rewards, arrow slots, HIDE_TUTORIAL_CURSOR (3)");
    game::StoryPresenter s = make();
    s.on_reward("FLASH_GUI", P{"b_zoom", "CLICK_ZOOM"}, 1.0, 0);
    CHECK(s.arrows().size() == 1 && s.arrows()[0].label == "CLICK_ZOOM" && s.arrows()[0].target == "b_zoom");
    CHECK(s.component_flashing("b_zoom", 1.0) && s.component_flashing("b_zoom", 2.4) && !s.component_flashing("b_zoom", 2.5));
    s.on_reward("FLASH_PLANET_GUI", P{"Kashyyyk", "FLASH_TROOPS", "1", "Kashyyyk_GUI_Flash", "True"}, 1.0, 0);
    CHECK(s.arrows().size() == 2 && s.arrows()[1].kind == game::StoryArrow::Kind::Planet && s.arrows()[1].region == "FLASH_TROOPS" &&
          s.arrows()[1].sub == 1 && s.arrows()[1].has_pointer);
    s.on_reward("FLASH_PLANET_GUI", P{"Ilum", "FLASH_PLANET"}, 1.0, 0);
    CHECK(s.arrows().size() == 3 && !s.arrows()[2].has_pointer && s.arrows()[2].label.empty());
    CHECK(s.object_flashes().size() == 1 && s.object_flashes()[0].type_name == "Kashyyyk");  // TROOPS flashes the planet; PLANET is the arrow alone
    s.on_reward("FLASH_FLEET_WITH_UNIT", P{"Han_Solo", "Han_Solo_Team_Flash"}, 1.0, 0);
    s.on_reward("FLASH_SPECIAL_ABILITY", P{"STEALTH", "flash_stealh_buttons"}, 1.0, 0);
    s.on_reward("FLASH_PRODUCTION_CHOICE", P{"Probe_Droid_Team", "Flash_Probe_Droid"}, 1.0, 0);
    CHECK(s.arrows().size() == 6);
    CHECK(s.arrows()[3].kind == game::StoryArrow::Kind::Fleet && s.arrows()[4].kind == game::StoryArrow::Kind::Ability &&
          s.arrows()[5].kind == game::StoryArrow::Kind::Production);
    // The label is only a name: hide the first slot with it (case-sensitive); an empty P1 hides the first unlabeled slot.
    s.on_reward("HIDE_TUTORIAL_CURSOR", P{"han_solo_team_flash"}, 2.0, 0);
    CHECK(s.arrows().size() == 6);
    s.on_reward("HIDE_TUTORIAL_CURSOR", P{"Han_Solo_Team_Flash"}, 2.0, 0);
    CHECK(s.arrows().size() == 5);
    s.on_reward("HIDE_TUTORIAL_CURSOR", P{}, 2.0, 0);
    CHECK(s.arrows().size() == 4 && s.arrows()[2].kind == game::StoryArrow::Kind::Ability);  // the Ilum slot (no label) went
    // Ten slots: an eleventh request gets no arrow (the flash still happens).
    game::StoryPresenter t = make();
    for (int i = 0; i < 12; ++i) t.on_reward("FLASH_GUI", P{"c" + std::to_string(i), "L"}, 0.0, 0);
    CHECK(t.arrows().size() == game::kArrowSlots && t.flashes().size() == 12);
    t.update(1.6, false);  // the flashes ran out; the arrows stay until hidden
    CHECK(t.flashes().empty() && t.arrows().size() == game::kArrowSlots);
    // FORCE_CLICK_GUI queues the component clicks.
    t.on_reward("FORCE_CLICK_GUI", P{"b_planet_tab"}, 0.0, 0);
    CHECK(t.take_forced_clicks() == std::vector<std::string>{"b_planet_tab"} && t.take_forced_clicks().empty());
    // HIGHLIGHT_OBJECT: on / off by type; P3 adds / removes a numeric pointer.
    game::StoryPresenter h = make();
    h.on_reward("HIGHLIGHT_OBJECT", P{"STORY_TRIGGER_ZONE_01", "1", "2"}, 0.0, 2);
    CHECK(h.highlights().size() == 1 && h.pointers().size() == 1 && h.pointers()[0].id == 2);
    h.on_reward("HIGHLIGHT_OBJECT", P{"story_trigger_zone_01", "1"}, 0.0, 2);  // same object again: one highlight
    CHECK(h.highlights().size() == 1);
    h.on_reward("HIGHLIGHT_OBJECT", P{"STORY_TRIGGER_ZONE_01", "0", "2"}, 0.0, 2);
    CHECK(h.highlights().empty() && h.pointers().empty());
    // Lua Highlight / Highlight_Small by object id.
    h.lua_highlight(7, true, false, 250.0f);
    h.lua_highlight(7, true, true, -100.0f);
    CHECK(h.highlights().size() == 2);
    h.lua_highlight(7, false, false, 0.0f);
    CHECK(h.highlights().size() == 1 && h.highlights()[0].small && h.highlights()[0].z_offset == -100.0f);
    // FLASH_PLANET / FLASH_ADVANCED_MAP_OBJECT flash an object type.
    h.on_reward("FLASH_PLANET", P{"Ilum"}, 1.0, 0);
    h.on_reward("FLASH_ADVANCED_MAP_OBJECT", P{"Ground_Ion_Cannon"}, 1.0, 0);
    CHECK(h.object_flashes().size() == 1 && h.map_flashes().size() == 1);
    h.update(10.0, false);
    CHECK(h.object_flashes().empty() && h.map_flashes().empty());
}

// mcx4 t14 / t15 (docs/re/STORY_PRESENTATION_REWARDS.md §1.1): the screen-text channel colours come from GameConstants
// Hint_Text_Color / System_Text_Color / Task_Text_Color / Speech_Text_Color ("r g b", alpha 255); a short value keeps
// the default.
// ledger: verifies GameConstants.Hint_Text_Color
// ledger: verifies GameConstants.System_Text_Color
// ledger: verifies GameConstants.Task_Text_Color
// ledger: verifies GameConstants.Speech_Text_Color
void colors_test() {
    alamo::test::section("text colours (1.1)");
    const data::XmlDocument doc = data::XmlDocument::parse(std::string_view(
        "<GameConstants><Hint_Text_Color>1 2 3</Hint_Text_Color><System_Text_Color>4, 5, 6, 7</System_Text_Color>"
        "<Task_Text_Color>8 9</Task_Text_Color><Speech_Text_Color> 10 11 12 </Speech_Text_Color></GameConstants>"));
    game::StoryPresenter::Colors c;
    game::load_story_colors(doc.root(), c);
    CHECK((c.hint == game::StoryColor{1, 2, 3, 255}) && (c.system == game::StoryColor{4, 5, 6, 7}));
    CHECK((c.task == game::StoryColor{109, 247, 255, 255}) && (c.speech == game::StoryColor{10, 11, 12, 255}));
}

void lock_test() {
    alamo::test::section("control, camera and mode rewards (5)");
    game::StoryPresenter s = make();
    CHECK(!s.controls_locked() && s.objectives_shown() && !s.command_bar_hidden());
    s.on_reward("LOCK_CONTROLS", P{"1"}, 0.0, 0);
    CHECK(s.controls_locked());
    s.on_reward("LOCK_CONTROLS", P{}, 0.0, 0);  // no P1: unchanged
    CHECK(s.controls_locked());
    s.on_reward("LOCK_CONTROLS", P{"0"}, 0.0, 0);
    CHECK(!s.controls_locked());
    s.on_reward("LOCK_CONTROLS", P{"1"}, 0.0, 2);
    s.unlock_controls();  // a battle ends with its lock on: the map is not locked
    CHECK(!s.controls_locked());
    s.on_reward("SCROLL_LOCK", P{"1"}, 0.0, 0);
    s.on_reward("LOCK_PLANET_SELECTION", P{"1"}, 0.0, 0);
    CHECK(s.scroll_locked() && s.planet_selection_locked());
    s.on_reward("SCROLL_LOCK", P{"0"}, 0.0, 0);
    CHECK(!s.scroll_locked());
    s.on_reward("ENABLE_OBJECTIVE_DISPLAY", P{"0"}, 0.0, 0);
    CHECK(!s.objectives_shown());
    s.on_reward("ENABLE_OBJECTIVE_DISPLAY", P{}, 0.0, 0);  // P1 defaults to 1
    CHECK(s.objectives_shown());
    // SHOW_COMMAND_BAR keeps a hide counter in a battle (mode 2): nested hides need matching shows.
    s.on_reward("SHOW_COMMAND_BAR", P{"0"}, 0.0, 2);
    s.on_reward("SHOW_COMMAND_BAR", P{"0"}, 0.0, 2);
    s.on_reward("SHOW_COMMAND_BAR", P{"1"}, 0.0, 2);
    CHECK(s.command_bar_hidden());
    s.on_reward("SHOW_COMMAND_BAR", P{"1"}, 0.0, 2);
    s.on_reward("SHOW_COMMAND_BAR", P{"1"}, 0.0, 2);  // never below 0
    CHECK(!s.command_bar_hidden());
    // 5.6 galactic (mode 0): a plain show / hide, no counter.
    s.on_reward("SHOW_COMMAND_BAR", P{"0"}, 0.0, 0);
    s.on_reward("SHOW_COMMAND_BAR", P{"0"}, 0.0, 0);
    s.on_reward("SHOW_COMMAND_BAR", P{"1"}, 0.0, 0);
    CHECK(!s.command_bar_hidden());
    // 5.5: ENABLE_OBJECTIVE_DISPLAY hands its value to the host once (the objectives key's byte).
    s.on_reward("ENABLE_OBJECTIVE_DISPLAY", P{"0"}, 0.0, 0);
    CHECK(s.take_objective_display() == std::optional<bool>(false) && !s.take_objective_display());
    s.on_reward("ENABLE_OBJECTIVE_DISPLAY", P{}, 0.0, 0);
    CHECK(s.take_objective_display() == std::optional<bool>(true));
    s.on_reward("START_CINEMATIC_MODE", P{}, 0.0, 0);
    CHECK(s.cinematic_mode());
    s.on_reward("STOP_CINEMATIC_MODE", P{}, 0.0, 0);
    CHECK(!s.cinematic_mode());
    s.on_reward("DISABLE_MOVIES", P{"1"}, 0.0, 0);
    CHECK(s.movies_disabled());
    // DISABLE_RETREAT by faction (both parameters required).
    s.on_reward("DISABLE_RETREAT", P{"Rebel", "1"}, 0.0, 2);
    s.on_reward("DISABLE_RETREAT", P{"EMPIRE"}, 0.0, 2);
    CHECK(s.retreat_disabled("REBEL") && s.retreat_disabled("rebel") && !s.retreat_disabled("EMPIRE"));
    s.on_reward("DISABLE_RETREAT", P{"REBEL", "0"}, 0.0, 2);
    CHECK(!s.retreat_disabled("REBEL"));
    s.on_reward("USE_RETRY_DIALOG", P{}, 0.0, 2);
    s.on_reward("HIDE_AUTORESOLVE", P{}, 0.0, 2);
    s.on_reward("HIDE_CURSOR_ON_CLICK", P{"1"}, 0.0, 0);
    CHECK(s.retry_dialog_used() && s.hide_autoresolve() && s.hide_cursor_on_click());
    // Slots and the hologram.
    s.on_reward("SHOW_SPECIAL_SLOT", P{"NalHutta_Prologue", "CORRUPTION_SLOT", ""}, 0.0, 0);
    s.on_reward("SHOW_SMUGGLE_SLOT", P{"NalHutta"}, 0.0, 0);
    CHECK(s.slot_forced("nalhutta_prologue", "CORRUPTION") && s.slot_forced("NalHutta", "SMUGGLE") && !s.slot_forced("NalHutta", "STEAL"));
    s.on_reward("HIDE_SMUGGLE_SLOT", P{"NalHutta"}, 0.0, 0);
    s.on_reward("SHOW_SPECIAL_SLOT", P{"NalHutta_Prologue", "CORRUPTION_SLOT", "0"}, 0.0, 0);
    CHECK(!s.slot_forced("NalHutta", "SMUGGLE") && !s.slot_forced("NalHutta_Prologue", "CORRUPTION"));
    s.on_reward("ENABLE_GALACTIC_CORRUPTION_HOLOGRAM", P{"Kuat", "1"}, 0.0, 0);
    CHECK(s.corruption_hologram("KUAT"));
    // Camera commands and TUTORIAL_DIALOG queue for the host.
    s.on_reward("POSITION_CAMERA", P{"Bothawui", "0,-80,0"}, 0.0, 0);
    s.on_reward("SELECT_PLANET", P{"Ryloth"}, 0.0, 0);
    s.on_reward("ZOOM_IN", P{}, 0.0, 0);
    s.on_reward("PAUSE_GALACTIC_GAME", P{"0"}, 0.0, 0);
    s.on_reward("START_CINEMATIC_MODE", P{}, 0.0, 0);
    CHECK(s.cinematic_mode());
    s.on_reward("TUTORIAL_DIALOG", P{"TEXT_TUTORIAL_2_COMPLETE", "1"}, 0.0, 0);
    CHECK(!s.cinematic_mode());  // the dialog ends it
    const std::vector<game::StoryCameraCommand> cams = s.take_camera_commands();
    CHECK(cams.size() == 4 && s.take_camera_commands().empty());
    CHECK(cams[0].kind == game::StoryCameraCommand::Kind::Position && cams[0].type == "Bothawui" && cams[0].oy == -80.0f);
    CHECK(cams[1].kind == game::StoryCameraCommand::Kind::SelectPlanet && cams[1].type == "Ryloth");
    CHECK(cams[2].kind == game::StoryCameraCommand::Kind::ZoomIn && cams[3].kind == game::StoryCameraCommand::Kind::UnpauseGalactic);
    const std::vector<game::StoryTutorialDialog> dl = s.take_dialogs();
    CHECK(dl.size() == 1 && dl[0].text_id == "TEXT_TUTORIAL_2_COMPLETE" && dl[0].continue_button);
}

void advisor_test() {
    alamo::test::section("advisor hints (6)");
    game::StoryPresenter s = make();
    s.config().advisor_hint_duration = 0.1;
    s.config().advisor_hint_interval = 300.0;
    const std::vector<std::string> hints = {"TEXT_H1", "TEXT_H2"};
    s.begin_mode(0);
    CHECK(!s.tick_advisor(0, 89, hints));  // the first hint is due 90 ticks after the mode starts
    CHECK(s.tick_advisor(0, 90, hints) && s.hint_queue_size() == 1);
    CHECK(!s.tick_advisor(0, 91, hints) && !s.tick_advisor(0, 9089, hints));  // next = 90 + 30 * 300
    CHECK(s.tick_advisor(0, 9090, hints) && s.hint_queue_size() == 2);
    CHECK(s.tick_advisor(0, 9090 + 9000, hints) && s.hint_queue_size() == 3);  // the list wraps
    // 6.5: a new mode start sets only the next tick; the list position runs on (every battle does not repeat the first hint).
    {
        game::StoryPresenter t = make();
        t.begin_mode(0);
        CHECK(t.tick_advisor(0, 90, hints) && t.next_hint_index(0) == 1);
        t.begin_mode(0);
        CHECK(t.next_hint_index(0) == 1 && t.tick_advisor(0, 90, hints) && t.next_hint_index(0) == 0);
    }
    // SET_ADVISOR 0 (plot player, per mode) stops the periodic hint of that mode only.
    s.on_reward("SET_ADVISOR", P{"0"}, 0.0, 1);
    CHECK(!s.advisor_enabled(1) && s.advisor_enabled(0));
    s.begin_mode(1);
    CHECK(!s.tick_advisor(1, 100000, hints));
    // The queue shows when nothing blocks; shown for characters x duration, then fades.
    s.update(0.0, true);
    CHECK(s.shown_hint() == nullptr);
    s.update(1.0, false);
    CHECK(s.shown_hint() && s.shown_hint()->hint.text == U"<TEXT_H1>" && s.hint_queue_size() == 2);
    CHECK(s.shown_hint()->hint.sound == game::StoryHint::Sound::Hint && s.hint_started());
    const double secs = s.shown_hint()->seconds;
    CHECK(secs > 0.89 && secs < 0.91);  // 9 characters x 0.1 s
    CHECK(s.hint_alpha(1.0) == 0 && s.hint_alpha(1.0 + game::kHintFadeSeconds) == 255 && s.hint_alpha(1.0 + secs / 2 + 0.25) == 255);
    s.update(1.0 + secs + game::kHintFadeSeconds, false);  // gone, the next one shows at once
    CHECK(s.shown_hint() && s.shown_hint()->hint.text == U"<TEXT_H2>");
    // Game_Message goes to the front, white, and replaces what is up.
    s.game_message("TEXT_MSG");
    CHECK(s.shown_hint() == nullptr && s.hint_queue_size() == 2);
    s.update(5.0, false);
    CHECK(s.shown_hint() && s.shown_hint()->hint.text == U"<TEXT_MSG>" && s.shown_hint()->hint.color == (game::StoryColor{255, 255, 255, 255}));
    CHECK(s.shown_hint()->hint.sound == game::StoryHint::Sound::Urgent);
    // COMMANDBAR_MOVIE clears the hint on screen first, but not for Light_Loop (6.2).
    s.clear_shown_hint("light_loop");
    CHECK(s.shown_hint() != nullptr);
    s.clear_shown_hint("Vader_Loop");
    CHECK(s.shown_hint() == nullptr);
    // The faction's hint lists are comma / whitespace separated ids.
    const std::vector<std::string> ids = game::split_story_ids(" TEXT_A, TEXT_B\n TEXT_C ,, ");
    CHECK(ids.size() == 3 && ids[0] == "TEXT_A" && ids[2] == "TEXT_C" && game::split_story_ids("").empty());
}

// q6 t36 (Q6_T27_ELIM_END_TIMING.md "Probe answer" [H]): the galactic hint text queue as retail ran it in the q6 t33 probe.
void hint_queue_test() {
    alamo::test::section("q6 t36: hint text queue");
    game::StoryPresenter s = make();
    s.config().advisor_hint_duration = 0.1;
    // The owner setter's line: %ls planet / old / new, %d the bonus (TEXT_PLANET_CHANGED_HANDS[_BONUS] as shipped).
    const std::u32string plain = game::changed_hands_text(U"%ls: Control changed from %ls to %ls", U"Tatooine", U"Empire", U"Neutral", {});
    CHECK(plain == U"Tatooine: Control changed from Empire to Neutral" && plain.size() == 48);  // the probe's 48 characters
    const std::u32string bonus = game::changed_hands_text(U"%ls: Control changed from %ls to %ls. Credit bonus: %d", U"Tatooine",
                                                          U"Neutral", U"Empire", 900);
    CHECK(bonus == U"Tatooine: Control changed from Neutral to Empire. Credit bonus: 900" && bonus.size() == 67);
    CHECK(game::changed_hands_text(U"%s %S %ls %ls 100%% %d %x", U"a", U"b", U"c", {}) == U"a b c  100%  %x");
    // Two game texts at the back, then a Game_Message: the script's message, queued last, shows first (finding 8).
    s.queue_message(plain, game::kChangedHandsColor);
    s.queue_message(U"", game::kChangedHandsColor);  // an empty text is not queued
    CHECK(s.hint_queue_size() == 1);
    s.queue_message(bonus, game::kChangedHandsColor);
    s.game_message("TEXT_STORY_MSG");  // "<TEXT_STORY_MSG>": 16 characters
    CHECK(s.hint_queue_size() == 3 && !s.hint_up(0.0));
    // A blocking movie or pop-up holds the whole queue (finding 7).
    s.update(0.0, true);
    CHECK(s.shown_hint() == nullptr && !s.hint_up(0.0));
    s.update(10.0, false);
    CHECK(s.shown_hint() && s.shown_hint()->hint.text == U"<TEXT_STORY_MSG>" && s.hint_up(10.0) && s.hint_up(11.59) && !s.hint_up(11.61));
    // Each text stays characters x Advisor_Hint_Duration and the next follows at once: no wait for the fade-out (finding 3).
    s.update(11.61, false);
    CHECK(s.shown_hint() && s.shown_hint()->hint.text == plain && s.shown_hint()->hint.color == game::kChangedHandsColor);
    CHECK(s.shown_hint()->hint.sound == game::StoryHint::Sound::Message && s.hint_queue_size() == 1);
    s.update(11.61 + 4.81, false);
    CHECK(s.shown_hint() && s.shown_hint()->hint.text == bonus && s.hint_queue_size() == 0);
    const double end = 11.61 + 4.81 + 6.7;
    CHECK(s.hint_up(end - 0.01) && !s.hint_up(end + 0.01));
    // The last text of a run is still drawn fading, but no longer "up" (the Light_Loop closes, the end screen may come).
    s.update(end + 0.1, false);
    CHECK(s.shown_hint() != nullptr && !s.hint_up(end + 0.1) && s.hint_alpha(end + 0.1) > 0);
    s.update(end + game::kHintFadeSeconds + 0.01, false);
    CHECK(s.shown_hint() == nullptr);
}

}  // namespace

int main() {
    text_test();
    pointer_test();
    colors_test();  // mcx4 t14 / t15
    lock_test();
    advisor_test();
    hint_queue_test();
    return alamo::test::report("alamo_game_story_present_tests");
}
