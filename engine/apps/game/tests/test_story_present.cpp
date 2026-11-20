// alamo_game_story_present_tests (data-free): StoryPresenter (story_present.hpp), the host side of the story presentation
// rewards (docs/re/STORY_PRESENTATION_REWARDS.md). One block per spec section.
#include <cstdint>
#include <cstdio>
#include <set>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

#include "alamo/core/bytes.hpp"

#include "alamo/data/xml.hpp"
#include "alamo/game/story_present.hpp"
#include "alamo/game/story_ui.hpp"
#include "alamo/sim/story_rewards.hpp"
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
    {  // mcx4 t14 (docs/re/MCX4_T14_RE_ANSWERS.md "GameConstants.Text_Reveal_Rate"): elapsed seconds x the rate
        game::StoryTextLine tl;
        tl.text = U"abcdefghijklmnopqrstuvwxyz";
        tl.start = 10.0;
        CHECK(tl.revealed(10.125, 80.0) == 10 && tl.revealed(10.125) == 15 && tl.revealed(20.0, 80.0) == 26);
    }
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
// mcx4 t14 (docs/re/MCX4_T14_RE_ANSWERS.md "GameConstants.CB_Flash_Count"): a flash run by count, no arrow.
void count_flash_test() {
    alamo::test::section("count-run component flash (CB_Flash_Count)");
    game::StoryPresenter s = make();
    s.flash_component("b_reinforcement", 4, 1.0);  // FoC CB_Flash_Count 4: 4 x 0.5 s
    CHECK(s.arrows().empty() && s.component_flashing("b_reinforcement", 2.9) && !s.component_flashing("b_reinforcement", 3.0));
    s.flash_component("b_reinforcement", 0, 5.0);
    CHECK(!s.component_flashing("b_reinforcement", 5.0));
}

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

// ref4 t01: every reward kind on_reward handles (plus the Lua / host calls) into one presenter, in an order that leaves
// every lasting switch set.
void feed_everything(game::StoryPresenter& s) {
    const double t = 10.0;
    s.config().flash_duration = 0.25;  // times that survive the save's rebase exactly (the config is not saved)
    s.on_reward("SCREEN_TEXT", P{"TEXT_A", "30", "", "", "", "", "Hint"}, t, 0);
    s.on_reward("SCREEN_TEXT", P{"TEXT_B", "-1", "X_WING", "", "0", "1,2,3", "System"}, t + 0.5, 0);
    s.on_reward("MULTIMEDIA", P{"TEXT_C", "20", "", "", "", "", "Task"}, t + 1.0, 0);
    s.on_reward("FLASH_GUI", P{"b_build", "flash_1"}, t, 0);
    s.on_reward("FLASH_PLANET_GUI", P{"Kuat", "FLASH_TROOPS", "2", "flash_2", "1"}, t, 0);
    s.on_reward("FLASH_FLEET_WITH_UNIT", P{"X_WING", "flash_3"}, t, 0);
    s.on_reward("FLASH_SPECIAL_ABILITY", P{"Stealth", "flash_4"}, t, 0);
    s.on_reward("FLASH_PRODUCTION_CHOICE", P{"Y_WING", "flash_5"}, t, 0);
    s.on_reward("FLASH_GUI", P{"b_gone", "flash_gone"}, t, 0);
    s.on_reward("HIDE_TUTORIAL_CURSOR", P{"flash_gone"}, t, 0);
    s.on_reward("FLASH_ADVANCED_MAP_OBJECT", P{"Moff_Base"}, t, 0);
    s.on_reward("FLASH_PLANET", P{"Ryloth"}, t, 0);
    s.on_reward("FLASH_OBJECT", P{"AT_AT"}, t, 0);
    s.on_reward("FORCE_CLICK_GUI", P{"b_ok"}, t, 0);
    s.on_reward("HIDE_CURSOR_ON_CLICK", P{"1"}, t, 0);
    s.on_reward("HIGHLIGHT_OBJECT", P{"Rebel_Base", "1", "7"}, t, 0);
    s.on_reward("TUTORIAL_DIALOG", P{"TEXT_TUTORIAL", "1"}, t, 0);
    s.on_reward("LOCK_CONTROLS", P{"1"}, t, 0);
    s.on_reward("SCROLL_LOCK", P{"1"}, t, 0);
    s.on_reward("LOCK_PLANET_SELECTION", P{"1"}, t, 0);
    s.on_reward("ENABLE_OBJECTIVE_DISPLAY", P{"0"}, t, 0);
    s.on_reward("SHOW_COMMAND_BAR", P{"0"}, t, 2);
    s.on_reward("SHOW_COMMAND_BAR", P{"0"}, t, 2);
    s.on_reward("STOP_CINEMATIC_MODE", P{}, t, 0);
    s.on_reward("START_CINEMATIC_MODE", P{}, t, 0);
    s.on_reward("DISABLE_MOVIES", P{"1"}, t, 0);
    s.on_reward("DISABLE_RETREAT", P{"Rebel", "1"}, t, 2);
    s.on_reward("HIDE_AUTORESOLVE", P{}, t, 2);
    s.on_reward("USE_RETRY_DIALOG", P{}, t, 2);
    s.on_reward("ACTIVATE_RETRY_DIALOG", P{}, t, 2);
    s.on_reward("ENABLE_GALACTIC_CORRUPTION_HOLOGRAM", P{"Kuat", "1"}, t, 0);
    s.on_reward("SHOW_SMUGGLE_SLOT", P{"NalHutta"}, t, 0);
    s.on_reward("SHOW_RAID_SLOT", P{"Tatooine"}, t, 0);
    s.on_reward("HIDE_RAID_SLOT", P{"Tatooine"}, t, 0);
    s.on_reward("SHOW_STEAL_SLOT", P{"Kuat"}, t, 0);
    s.on_reward("HIDE_STEAL_SLOT", P{"Corulag"}, t, 0);
    s.on_reward("HIDE_SMUGGLE_SLOT", P{"Corulag"}, t, 0);
    s.on_reward("SHOW_SPECIAL_SLOT", P{"NalHutta_Prologue", "CORRUPTION_SLOT", ""}, t, 0);
    s.on_reward("SELECT_PLANET", P{"Ryloth"}, t, 0);
    s.on_reward("ZOOM_IN", P{}, t, 0);
    s.on_reward("ZOOM_OUT", P{}, t, 0);
    s.on_reward("POSITION_CAMERA", P{"Bothawui", "0,-80,0"}, t, 0);
    s.on_reward("SCROLL_CAMERA", P{"Kuat", "1,2,3"}, t, 0);
    s.on_reward("PAUSE_GAME", P{}, t, 0);
    s.on_reward("PAUSE_GALACTIC", P{}, t, 0);
    s.on_reward("UNPAUSE_GALACTIC", P{}, t, 0);
    s.on_reward("PAUSE_GALACTIC_GAME", P{"0"}, t, 0);
    s.on_reward("SET_ADVISOR", P{"0"}, t, 1);
    s.flash_component("b_reinforcement", 2, t);
    s.lua_highlight(42, true, true, 3.5f);
    s.begin_mode(2);
    CHECK(s.tick_advisor(2, 95, {"TEXT_HINT_1", "TEXT_HINT_2"}));
    s.game_message("TEXT_GM");
    s.urgent_message(U"urgent", game::StoryColor{1, 2, 3, 255});
    s.queue_message(U"later", game::StoryColor{4, 5, 6, 255});
    s.update(t + 0.25, false);  // the urgent line shows; three wait (the flashes still run)
}

// Every query of `a` at host time `ta` equals `b`'s at `tb` (the take_* ones are drained from both).
void same_presenter(game::StoryPresenter& a, double ta, game::StoryPresenter& b, double tb) {
    const double d = tb - ta;
    CHECK(a.lines().size() == b.lines().size());
    for (size_t i = 0; i < a.lines().size() && i < b.lines().size(); ++i) {
        const game::StoryTextLine &x = a.lines()[i], &y = b.lines()[i];
        CHECK(x.id == y.id && x.text == y.text && x.color == y.color && x.channel == y.channel);
        CHECK(x.start + d == y.start && x.seconds == y.seconds && x.persistent == y.persistent && x.typewriter == y.typewriter);
    }
    CHECK(a.lines(ta).size() == b.lines(tb).size());
    CHECK(a.arrows() == b.arrows());
    CHECK(a.pointers().size() == b.pointers().size());
    for (size_t i = 0; i < a.pointers().size() && i < b.pointers().size(); ++i) {
        game::StoryPointer p = a.pointers()[i];
        if (p.until >= 0.0) p.until += d;
        CHECK(p == b.pointers()[i]);
    }
    auto shifted = [d](auto v) {
        for (auto& f : v) {
            f.start += d;
            f.until += d;
        }
        return v;
    };
    CHECK(shifted(a.flashes()) == b.flashes());
    CHECK(shifted(a.object_flashes()) == b.object_flashes());
    CHECK(shifted(a.map_flashes()) == b.map_flashes());
    CHECK(a.highlights() == b.highlights());
    CHECK(a.component_flashing("b_build", ta) == b.component_flashing("b_build", tb));
    CHECK((a.shown_hint() == nullptr) == (b.shown_hint() == nullptr));
    if (a.shown_hint() && b.shown_hint()) {
        const game::StoryShownHint &x = *a.shown_hint(), &y = *b.shown_hint();
        CHECK(x.hint.text == y.hint.text && x.hint.color == y.hint.color && x.hint.sound == y.hint.sound);
        CHECK(x.start + d == y.start && x.seconds == y.seconds);
    }
    CHECK(a.hint_alpha(ta) == b.hint_alpha(tb) && a.hint_up(ta) == b.hint_up(tb));
    CHECK(a.hint_queue_size() == b.hint_queue_size());
    CHECK(a.controls_locked() == b.controls_locked() && a.scroll_locked() == b.scroll_locked());
    CHECK(a.planet_selection_locked() == b.planet_selection_locked() && a.objectives_shown() == b.objectives_shown());
    CHECK(a.command_bar_hidden() == b.command_bar_hidden() && a.cinematic_mode() == b.cinematic_mode());
    CHECK(a.movies_disabled() == b.movies_disabled());
    CHECK(a.retry_dialog_used() == b.retry_dialog_used() && a.retry_dialog_requested() == b.retry_dialog_requested());
    CHECK(a.hide_cursor_on_click() == b.hide_cursor_on_click() && a.hide_autoresolve() == b.hide_autoresolve());
    for (uint8_t m = 0; m < 3; ++m) CHECK(a.advisor_enabled(m) == b.advisor_enabled(m) && a.next_hint_index(m) == b.next_hint_index(m));
    for (const char* f : {"REBEL", "EMPIRE"}) CHECK(a.retreat_disabled(f) == b.retreat_disabled(f));
    for (const char* p : {"KUAT", "RYLOTH"}) CHECK(a.corruption_hologram(p) == b.corruption_hologram(p));
    for (const char* p : {"NalHutta", "Tatooine", "Kuat", "Corulag", "NalHutta_Prologue"})
        for (const char* sl : {"SMUGGLE", "RAID", "STEAL", "CORRUPTION"}) CHECK(a.slot_forced(p, sl) == b.slot_forced(p, sl));
    CHECK(a.handled() == b.handled());
    // The periodic hint clock: the next tick a hint is due (one tick before it nothing is queued, at it one is).
    CHECK(a.tick_advisor(2, 9094, {"H"}) == b.tick_advisor(2, 9094, {"H"}));
    CHECK(a.tick_advisor(2, 9095, {"H"}) == b.tick_advisor(2, 9095, {"H"}));
    CHECK(a.take_objective_display() == b.take_objective_display());
    CHECK(a.take_text_hint_sounds() == b.take_text_hint_sounds());
    CHECK(a.take_retry_request() == b.take_retry_request());
    CHECK(a.hint_started() == b.hint_started());
    CHECK(a.take_camera_commands() == b.take_camera_commands());
    CHECK(a.take_dialogs() == b.take_dialogs());
    CHECK(a.take_forced_clicks() == b.take_forced_clicks());
}

void save_test() {
    alamo::test::section("ref4 t01: save / read gives the same presenter");
    game::StoryPresenter a = make();
    feed_everything(a);
    CHECK(a.retreat_disabled("REBEL") && a.slot_forced("Kuat", "STEAL") && a.slot_forced("NalHutta_Prologue", "CORRUPTION"));
    CHECK(a.retry_dialog_used() && a.cinematic_mode() && a.controls_locked() && !a.advisor_enabled(1) && a.shown_hint());
    CHECK(!a.lines().empty() && !a.arrows().empty() && !a.pointers().empty() && !a.highlights().empty());
    CHECK(!a.flashes().empty() && !a.object_flashes().empty() && !a.map_flashes().empty());
    CHECK(a.hint_queue_size() == 3);
    ByteWriter w;
    a.write(w, 20.0);
    game::StoryPresenter b = make();
    {
        ByteReader r(w.bytes());
        CHECK(b.read(r, 520.0) && r.done());
    }
    ByteWriter w2;  // save / load / save: the same bytes
    b.write(w2, 520.0);
    CHECK(w2.bytes() == w.bytes());
    same_presenter(a, 20.0, b, 520.0);

    alamo::test::section("ref4 t01: a bad blob is refused and changes nothing");
    game::StoryPresenter c = make();
    c.on_reward("LOCK_CONTROLS", P{"1"}, 0.0, 0);
    std::vector<uint8_t> cut(w.bytes().begin(), w.bytes().end() - 3);
    ByteReader rc(cut);
    CHECK(!c.read(rc, 0.0) && c.controls_locked() && !c.scroll_locked() && c.handled() == 1);
    std::vector<uint8_t> wrong = w.bytes();
    wrong[0] = 99;  // the version
    ByteReader rw(wrong);
    CHECK(!c.read(rw, 0.0) && c.controls_locked() && c.handled() == 1);

    alamo::test::section("ref4 t01: a v1 save replays the lasting rewards");
    game::StoryPresenter full = make(), replayed = make();
    // The same feed, recorded: the v1 fallback feeds only the lasting ones again.
    struct Fed {
        std::string type;
        P p;
        uint8_t mode;
    };
    for (const auto& [type, p, mode] : std::vector<Fed>{
             {"LOCK_CONTROLS", P{"1"}, 0}, {"SCREEN_TEXT", P{"TEXT_A"}, 0}, {"DISABLE_RETREAT", P{"Rebel", "1"}, 2},
             {"SHOW_STEAL_SLOT", P{"Kuat"}, 0}, {"SHOW_SPECIAL_SLOT", P{"NalHutta", "SABOTAGE_SLOT", "1"}, 0},
             {"USE_RETRY_DIALOG", P{}, 2}, {"HIDE_CURSOR_ON_CLICK", P{"1"}, 0}, {"FLASH_GUI", P{"b_build"}, 0},
             {"POSITION_CAMERA", P{"Kuat"}, 0}, {"SET_ADVISOR", P{"0"}, 1}, {"SHOW_COMMAND_BAR", P{"0"}, 2}}) {
        full.on_reward(type, p, 1.0, mode);
        if (game::StoryPresenter::is_lasting_reward(type)) replayed.on_reward(type, p, 0.0, mode);
    }
    CHECK(replayed.controls_locked() && replayed.retreat_disabled("REBEL") && replayed.slot_forced("Kuat", "STEAL"));
    CHECK(replayed.slot_forced("NalHutta", "SABOTAGE") && replayed.retry_dialog_used() && replayed.hide_cursor_on_click());
    CHECK(!replayed.advisor_enabled(1) && replayed.command_bar_hidden());
    CHECK(replayed.lines().empty() && replayed.flashes().empty() && replayed.take_camera_commands().empty());
    CHECK(!game::StoryPresenter::is_lasting_reward("SCREEN_TEXT") && game::StoryPresenter::is_lasting_reward("disable_retreat"));
}

// ref5 t02: every Ui row of the sim's story reward table has a handler in the presenter or the media table; every name either
// registers is a row of the table; DUAL_FLASH pairs the next two flashes, FLASH_UNIT takes an arrow slot, SFX is a media cue.
void handler_test() {
    alamo::test::section("ref5 t02: Ui rows have a registered handler");
    size_t ui = 0;
    for (const sim::StoryRewardRow& row : sim::story_reward_table()) {
        if (row.owner != sim::StoryRewardOwner::Ui) continue;
        ++ui;
        const bool ok = game::StoryPresenter::has_reward_handler(row.name) || game::story_reward_media_handles(row.name);
        if (!ok) std::printf("    Ui row without a handler: %.*s\n", static_cast<int>(row.name.size()), row.name.data());
        CHECK(ok);
    }
    CHECK(ui > 50);
    std::set<std::string_view> seen;
    for (const std::string_view n : game::StoryPresenter::reward_names()) {
        CHECK(sim::find_story_reward(n) != nullptr && seen.insert(n).second);
    }
    seen.clear();
    for (const std::string_view n : game::story_reward_media_names()) {
        const sim::StoryRewardRow* row = sim::find_story_reward(n);
        CHECK(row && row->owner == sim::StoryRewardOwner::Ui && seen.insert(n).second);
    }

    alamo::test::section("ref5 t02: DUAL_FLASH alternates the next two flashes (2.5)");
    game::StoryPresenter s = make();
    CHECK(s.on_reward("DUAL_FLASH", P{"2", "0.5"}, 10.0, 0));
    s.on_reward("FLASH_GUI", P{"b_zoom"}, 10.0, 0);
    CHECK(!s.component_flashing("b_zoom", 10.0) && s.arrows().size() == 1);  // held, the arrow is not
    s.on_reward("FLASH_GUI", P{"b_build"}, 10.0, 0);
    CHECK(s.component_flashing("b_zoom", 10.1) && !s.component_flashing("b_build", 10.1));
    CHECK(!s.component_flashing("b_zoom", 10.6) && s.component_flashing("b_build", 10.6));
    CHECK(s.component_flashing("b_zoom", 11.1) && !s.component_flashing("b_zoom", 12.1) && !s.component_flashing("b_build", 12.1));
    {  // a zero time holds nothing: the flash runs as without DUAL_FLASH
        game::StoryPresenter z = make();
        z.on_reward("DUAL_FLASH", P{}, 0.0, 0);
        z.on_reward("FLASH_GUI", P{"b_zoom"}, 0.0, 0);
        CHECK(z.component_flashing("b_zoom", 0.1));
        ByteWriter zw;  // a v1 blob (no DUAL_FLASH tail: bool, f64, f64, u32 count) still reads
        z.write(zw, 0.0);
        std::vector<uint8_t> v1(zw.bytes().begin(), zw.bytes().end() - 21);
        v1[0] = 1;
        game::StoryPresenter zr = make();
        ByteReader zrr(v1);
        CHECK(zr.read(zrr, 0.0) && zrr.done() && zr.component_flashing("b_zoom", 0.1));
    }
    s.on_reward("FLASH_GUI", P{"b_next"}, 20.0, 0);  // the pair disarmed it: a flash at once
    CHECK(s.component_flashing("b_next", 20.0));
    s.on_reward("DUAL_FLASH", P{"2", "0.5"}, 30.0, 0);
    s.on_reward("FLASH_GUI", P{"b_a"}, 30.0, 0);
    s.on_reward("HIDE_TUTORIAL_CURSOR", P{"none"}, 30.0, 0);  // clears the held flash and disarms
    s.on_reward("FLASH_GUI", P{"b_b"}, 30.0, 0);
    CHECK(!s.component_flashing("b_a", 30.1) && s.component_flashing("b_b", 30.1));
    s.on_reward("DUAL_FLASH", P{"1", "0.25"}, 40.0, 0);  // a saved held flash stays held
    s.on_reward("FLASH_PLANET_GUI", P{"Kuat", "FLASH_TROOPS"}, 40.0, 0);
    ByteWriter w;
    s.write(w, 40.0);
    game::StoryPresenter r = make();
    ByteReader rd(w.bytes());
    CHECK(r.read(rd, 40.0) && rd.done());
    r.on_reward("FLASH_PLANET_GUI", P{"Ilum", "FLASH_TROOPS"}, 40.0, 0);
    size_t kuat = 0, ilum = 0;
    for (const game::StoryObjectFlash& f : r.object_flashes()) (f.type_name == "Kuat" ? kuat : ilum) += f.start >= 40.0 ? 1u : 0u;
    CHECK(kuat == 2 && ilum == 2);  // 1 s at 0.25 s: four steps, two each

    alamo::test::section("ref5 t02: FLASH_UNIT arrow slot (2.4), SFX cue (§1.22)");
    game::StoryPresenter u = make();
    CHECK(u.on_reward("FLASH_UNIT", P{"production", "Rebel_Infantry_Squad"}, 0.0, 2));
    CHECK(u.arrows().size() == 1 && u.arrows()[0].kind == game::StoryArrow::Kind::Unit && u.arrows()[0].region == "PRODUCTION");
    u.on_reward("FLASH_UNIT", P{"NO_REGION", "Rebel_Infantry_Squad"}, 0.0, 2);  // unknown region: nothing
    CHECK(u.arrows().size() == 1 && u.on_reward("FLASH_TERRAIN", P{"3"}, 0.0, 2));
    std::vector<game::StoryMediaCue> cues;
    game::story_reward_media("SFX", P{"Unit_Select_SFX"}, cues);
    CHECK(cues.size() == 1 && cues[0].kind == game::StoryMediaCue::Kind::Sfx && cues[0].name == "Unit_Select_SFX");
}

}  // namespace

int main() {
    text_test();
    pointer_test();
    colors_test();  // mcx4 t14 / t15
    count_flash_test();  // mcx4 t14
    lock_test();
    advisor_test();
    hint_queue_test();
    save_test();  // ref4 t01
    handler_test();  // ref5 t02
    return alamo::test::report("alamo_game_story_present_tests");
}
