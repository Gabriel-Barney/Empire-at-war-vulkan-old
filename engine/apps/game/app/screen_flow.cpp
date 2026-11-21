// alamo_game host, ref6 t05: the screen rule table (screen_flow.hpp).
#include "screen_flow.hpp"

namespace alamo_game_app {

const char* screen_name(Screen s) {
    switch (s) {
        case Screen::Movie: return "movie";
        case Screen::Menu: return "menu";
        case Screen::SessionLoad: return "session-load";
        case Screen::Galactic: return "galactic";
        case Screen::BattleLoad: return "battle-load";
        case Screen::Battle: return "battle";
        case Screen::Quit: return "quit";
    }
    return "?";
}

const char* screen_event_name(ScreenEvent e) {
    switch (e) {
        case ScreenEvent::IntroMovies: return "intro-movies";
        case ScreenEvent::MainMenu: return "main-menu";
        case ScreenEvent::CampaignStarted: return "campaign-started";
        case ScreenEvent::SkirmishStarted: return "skirmish-started";
        case ScreenEvent::BattleLoading: return "battle-loading";
        case ScreenEvent::BattleShown: return "battle-shown";
        case ScreenEvent::LoadScreenDone: return "load-screen-done";
        case ScreenEvent::BattleOver: return "battle-over";
        case ScreenEvent::Quit: return "quit";
    }
    return "?";
}

std::optional<Screen> ScreenFlow::next(Screen from, ScreenEvent event) {
    if (from == Screen::Quit) return std::nullopt;  // the frame loop ends; nothing leaves Quit
    switch (event) {
        case ScreenEvent::IntroMovies: return Screen::Movie;
        case ScreenEvent::MainMenu: return Screen::Menu;
        case ScreenEvent::CampaignStarted: return Screen::Galactic;
        case ScreenEvent::SkirmishStarted: return Screen::SessionLoad;
        case ScreenEvent::BattleLoading: return Screen::BattleLoad;
        case ScreenEvent::BattleShown: return Screen::Battle;
        case ScreenEvent::LoadScreenDone:
            if (from != Screen::BattleLoad) return std::nullopt;
            return Screen::Battle;
        case ScreenEvent::BattleOver: return Screen::Galactic;
        case ScreenEvent::Quit: return Screen::Quit;
    }
    return std::nullopt;
}

bool ScreenFlow::on(ScreenEvent event) {
    const std::optional<Screen> to = next(screen_, event);
    if (!to) {
        ++refused_;
        return false;
    }
    if (history_.size() == kHistory) history_.erase(history_.begin());
    history_.push_back({screen_, *to, event});
    screen_ = *to;
    return true;
}

}  // namespace alamo_game_app
