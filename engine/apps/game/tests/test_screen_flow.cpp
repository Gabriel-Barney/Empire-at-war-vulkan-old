// ref6 t05: the screen rule table without a window or a GPU (app/screen_flow.hpp).
#include <cstdio>
#include <cstring>

#include "../app/screen_flow.hpp"
#include "alamo/test/support.hpp"

using namespace alamo_game_app;
using ::alamo::test::g_fail;

namespace {

constexpr Screen kScreens[] = {Screen::Menu, Screen::SessionLoad, Screen::Galactic, Screen::BattleLoad,
                               Screen::Battle, Screen::Quit, Screen::Movie};
constexpr ScreenEvent kEvents[] = {ScreenEvent::IntroMovies,   ScreenEvent::MainMenu,    ScreenEvent::CampaignStarted,
                                   ScreenEvent::SkirmishStarted, ScreenEvent::BattleLoading, ScreenEvent::BattleShown,
                                   ScreenEvent::LoadScreenDone,  ScreenEvent::BattleOver,  ScreenEvent::Quit};

void table() {
    for (const Screen s : kScreens) {
        CHECK(std::strcmp(screen_name(s), "?") != 0);
        for (const ScreenEvent e : kEvents) {
            CHECK(std::strcmp(screen_event_name(e), "?") != 0);
            const auto to = ScreenFlow::next(s, e);
            if (s == Screen::Quit) {
                CHECK(!to);  // final
            } else if (e == ScreenEvent::LoadScreenDone) {
                CHECK(s == Screen::BattleLoad ? to == Screen::Battle : !to);
            } else {
                CHECK(to.has_value());
            }
        }
    }
    CHECK(ScreenFlow::next(Screen::Battle, ScreenEvent::BattleOver) == Screen::Galactic);
    CHECK(ScreenFlow::next(Screen::Menu, ScreenEvent::SkirmishStarted) == Screen::SessionLoad);
    CHECK(ScreenFlow::next(Screen::Menu, ScreenEvent::IntroMovies) == Screen::Movie);
}

// A campaign: menu -> galaxy -> load screen -> battle -> galaxy -> quit to menu -> exit; a stray Begin and anything
// after the exit are refused and leave the screen.
void campaign_run() {
    ScreenFlow f;
    CHECK(f.screen() == Screen::Menu && !f.world() && f.history().empty());
    CHECK(f.on(ScreenEvent::CampaignStarted) && f.world());
    CHECK(!f.on(ScreenEvent::LoadScreenDone) && f.screen() == Screen::Galactic && f.refused() == 1);
    CHECK(f.on(ScreenEvent::BattleLoading) && f.screen() == Screen::BattleLoad && !f.world());
    CHECK(f.on(ScreenEvent::LoadScreenDone) && f.screen() == Screen::Battle && f.world());
    CHECK(f.on(ScreenEvent::BattleOver) && f.screen() == Screen::Galactic);
    CHECK(f.on(ScreenEvent::MainMenu) && f.on(ScreenEvent::Quit) && f.screen() == Screen::Quit);
    CHECK(!f.on(ScreenEvent::MainMenu) && f.screen() == Screen::Quit && f.refused() == 2);
    CHECK(f.history().size() == 6 && f.history().front().from == Screen::Menu && f.history().front().event == ScreenEvent::CampaignStarted &&
          f.history().back().to == Screen::Quit);
    ScreenFlow g;
    for (size_t i = 0; i < ScreenFlow::kHistory + 6; ++i) CHECK(g.on(i % 2 ? ScreenEvent::MainMenu : ScreenEvent::IntroMovies));
    CHECK(g.history().size() == ScreenFlow::kHistory && g.history().back().to == Screen::Menu);
}

}  // namespace

int main() {
    table();
    campaign_run();
    return ::alamo::test::report("alamo_game_screen_flow_tests");
}
