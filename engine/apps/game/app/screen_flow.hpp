// alamo_game host, ref6 t05: the screen the game shows and every change of it. Each change is a named event; the rule
// table (ScreenFlow::next) says which screen an event leads to from which screen, so it is tested without a window or a
// GPU (alamo_game_screen_flow_tests). The App owns one ScreenFlow and changes its screen only through on().
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace alamo_game_app {

enum class Screen : uint8_t { Menu, SessionLoad, Galactic, BattleLoad, Battle, Quit, Movie };
const char* screen_name(Screen s);

enum class ScreenEvent : uint8_t {
    IntroMovies,      // start-up with intro movies queued: Movie
    MainMenu,         // intro over, attract demo, quit to menu, tutorial / retry quit, campaign score or skirmish end: Menu
    CampaignStarted,  // a campaign session (galactic map): Galactic
    SkirmishStarted,  // a skirmish session; its hidden galaxy runs until the battle is pushed: SessionLoad
    BattleLoading,    // a battle pushed with the load screen open: BattleLoad
    BattleShown,      // a battle pushed without a load screen: Battle
    LoadScreenDone,   // the load screen's Begin (or the benchmark's / a failed view's close of it): Battle, from BattleLoad only
    BattleOver,       // a campaign battle ended (summary closed or a story link): Galactic
    Quit,             // exit, script or render failure, mod relaunch: Quit
};
const char* screen_event_name(ScreenEvent e);

struct ScreenChange {
    Screen from, to;
    ScreenEvent event;
};

class ScreenFlow {
public:
    // The screen `event` leads to from `from`; nullopt when it does not apply there (Quit is final; LoadScreenDone needs
    // BattleLoad).
    static std::optional<Screen> next(Screen from, ScreenEvent event);

    Screen screen() const { return screen_; }
    // Applies `event`; false (screen unchanged, counted in refused()) when next() refuses it.
    bool on(ScreenEvent event);
    // The galactic map or a battle: the screens that take world input (camera, orders, the pause menu's Esc).
    bool world() const { return screen_ == Screen::Galactic || screen_ == Screen::Battle; }
    // The last kHistory changes, oldest first (logs and tests).
    static constexpr size_t kHistory = 32;
    const std::vector<ScreenChange>& history() const { return history_; }
    uint32_t refused() const { return refused_; }

private:
    Screen screen_ = Screen::Menu;
    std::vector<ScreenChange> history_;
    uint32_t refused_ = 0;
};

}  // namespace alamo_game_app
