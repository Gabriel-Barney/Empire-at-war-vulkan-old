// mcx6 RANDOM_STORY plot builder (docs/re/STORY_REWARDS_GALACTIC.md §1.15; retail builder 0x4bfdc0, trigger 0x4bec90,
// final event 0x4be420 on the generator object 0x140a2aba0). Retail draws every value from its free (unsynced) RNG; the
// port passes the story session's own saved RANDOM_STORY stream (StorySession::random_story_rng_), so a plot - sim state -
// comes out the same after a load.
// [port] choices (listed next to the reward in STORY_REWARDS_GALACTIC.md):
//   - a draw from an empty pool, or a trigger whose retries all fail, rebuilds the trigger (retail indexes the empty
//     list / recurses); after kMaxRebuilds rebuilds the trigger is left without parameters (retail would not end);
//   - the follow-up REMOVE_STORY_GOAL event of each trigger names the trigger event as its tag [L];
//   - BUILDABLE_UNIT draws from the whole list (retail marks a rewarded entry used for the game and redraws the reward
//     without BUILDABLE_UNIT once every entry is used); a reward whose list is empty gives an event without a reward.
#include "story_random_story.hpp"

#include <string_view>

#include "alamo/core/ascii.hpp"

namespace alamo::sim {

namespace {

// limit: retail STORY_REWARDS_GALACTIC §1.15 (0x4bec90); count: MCX5_RE_QUESTIONS Q4
constexpr int32_t kMaxTries = 100;    // the retail retry count (CONSTRUCT_LEVEL)
// limit: guard [port] trigger rebuilds (retail would not end)
constexpr int32_t kMaxRebuilds = 64;  // [port] guard

std::string escape(std::string_view s) {
    std::string out;
    for (const char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
    return out;
}

int32_t draw_index(SimRng& rng, size_t n) { return rng.range_int(0, static_cast<int32_t>(n) - 1); }

struct Event {
    std::string name, type, reward;
    std::vector<std::string> params, reward_params;
    std::string prereq;
    std::string text() const {
        std::string s = " <Event Name=\"" + escape(name) + "\"><Event_Type>" + escape(type) + "</Event_Type>";
        for (size_t i = 0; i < params.size(); ++i) {
            const std::string tag = "Event_Param" + std::to_string(i + 1);
            s += "<" + tag + ">" + escape(params[i]) + "</" + tag + ">";
        }
        if (!reward.empty()) {
            s += "<Reward_Type>" + escape(reward) + "</Reward_Type>";
            for (size_t i = 0; i < reward_params.size(); ++i) {
                const std::string tag = "Reward_Param" + std::to_string(i + 1);
                s += "<" + tag + ">" + escape(reward_params[i]) + "</" + tag + ">";
            }
        }
        if (!prereq.empty()) s += "<Prereq>" + escape(prereq) + "</Prereq>";
        return s + "</Event>\n";
    }
};

// One trigger event (0x4bec90). False = this attempt failed (the caller rebuilds it with fresh draws).
bool build_trigger(const RandomStoryInput& in, SimRng& rng, Event& ev, std::vector<std::string>& log) {
    const std::string type = ascii_upper(in.triggers[static_cast<size_t>(draw_index(rng, in.triggers.size()))]);
    ev.type = type;
    ev.params.clear();
    auto pick = [&rng](const std::vector<std::string>& v, std::string& out) {
        if (v.empty()) return false;
        out = v[static_cast<size_t>(draw_index(rng, v.size()))];
        return true;
    };
    std::string p1, p2, p3;
    if (type == "STORY_ENTER" || type == "STORY_LAND_ON" || type == "STORY_CONQUER") {
        if (in.other_planets.empty()) return false;
        p1 = in.other_planets[static_cast<size_t>(draw_index(rng, in.other_planets.size()))].name;
        ev.params = {p1};
    } else if (type == "STORY_CONSTRUCT") {
        if (!pick(in.construction, p1)) return false;
        ev.params = {p1};
    } else if (type == "STORY_CONSTRUCT_LEVEL") {
        // An own planet, ground (0) or space (1), whose level is below its maximum; up to 100 tries.
        if (in.own_planets.empty()) return false;
        bool found = false;
        for (int32_t k = 0; k < kMaxTries && !found; ++k) {
            const RandomStoryInput::Planet& p = in.own_planets[static_cast<size_t>(draw_index(rng, in.own_planets.size()))];
            const bool ground = rng.range_int(0, 1) == 0;
            const int32_t cur = ground ? p.ground_level : p.space_level, mx = ground ? p.ground_max : p.space_max;
            if (cur >= mx) continue;
            ev.params = {p.name, std::to_string(rng.range_int(cur + 1, mx)), ground ? "GROUND" : "SPACE"};
            found = true;
        }
        if (!found) return false;
    } else if (type == "STORY_DESTROY") {
        if (!pick(in.destroy, p1)) return false;
        ev.params = {p1, std::to_string(rng.range_int(1, 3))};
    } else if (type == "STORY_DESTROY_BASE") {
        ev.params = {rng.range_int(0, 1) == 0 ? "GROUND" : "SPACE"};
    } else if (type == "STORY_DEPLOY" || type == "STORY_MOVE") {
        // An own hero, then a revealed planet of another owner (§1.15: no further owner test for either type).
        if (!pick(in.own_heroes, p1) || in.other_planets.empty()) return false;
        p2 = in.other_planets[static_cast<size_t>(draw_index(rng, in.other_planets.size()))].name;
        ev.params = {p1, p2};
    } else if (type == "STORY_ACCUMULATE") {
        // The local player's credits (int) + 1000 x Get_Int(10, 30).
        const int32_t add = rng.range_int(10, 30);
        ev.params = {std::to_string(static_cast<int32_t>(in.local_credits) + add * 1000)};
    } else if (type == "STORY_DEFEAT_HERO") {
        if (!pick(in.enemy_heroes, p1)) return false;
        ev.params = {p1};
    } else if (type == "STORY_WIN_BATTLES") {
        const int32_t count = rng.range_int(1, 3);
        ev.params = {std::to_string(count), rng.range_int(0, 1) == 0 ? "GROUND" : "SPACE"};
    } else {
        log.push_back("Warning! Random story trigger " + type + " currently unsupported!");
    }
    return true;
}

}  // namespace

RandomStoryPlot build_random_story(uint32_t k, const RandomStoryInput& in, SimRng& rng) {
    RandomStoryPlot out;
    out.name = "Random_Plot_" + std::to_string(k);
    out.log.push_back("Generating random story");
    std::vector<Event> events;
    if (in.triggers.empty() || in.max_triggers < 1) {
        out.log.push_back("Random story: no Random_Story_Triggers");  // [port] no constants: an empty plot
        out.xml = "<Story>\n</Story>\n";
        return out;
    }
    const int32_t n = rng.range_int(1, in.max_triggers);
    auto chapter = [&](int32_t i) {  // [CHAPTER i], TITLE, IMAGE, DIALOG <name><1..3>, TEXT (one draw)
        out.dialog += "[CHAPTER " + std::to_string(i) + "]\nTITLE\nIMAGE\nDIALOG " + std::to_string(rng.range_int(1, 3)) + "\nTEXT\n";
    };
    for (int32_t i = 0; i < n; ++i) {
        Event ev;
        ev.name = "RANDOM_TRIGGER_" + std::to_string(i);
        if (i > 0) ev.prereq = "RANDOM_TRIGGER_" + std::to_string(i - 1);
        bool ok = false;
        for (int32_t r = 0; r < kMaxRebuilds && !ok; ++r) ok = build_trigger(in, rng, ev, out.log);
        if (!ok) ev.params.clear();
        out.log.push_back("Event " + ev.name + ", Type " + ev.type);
        chapter(i);
        Event done;  // the follow-up: a STORY_TRIGGER whose reward removes the trigger's holocron goal [M]
        done.name = ev.name + "_GOAL";
        done.type = "STORY_TRIGGER";
        done.reward = "REMOVE_STORY_GOAL";
        done.reward_params = {ev.name};
        done.prereq = ev.name;
        events.push_back(std::move(ev));
        events.push_back(std::move(done));
    }
    // The final event RANDOM_TRIGGER_<n> (0x4be420): a STORY_TRIGGER with a random reward of Random_Story_Rewards.
    Event fin;
    fin.name = "RANDOM_TRIGGER_" + std::to_string(n);
    fin.type = "STORY_TRIGGER";
    fin.prereq = "RANDOM_TRIGGER_" + std::to_string(n - 1);
    if (!in.rewards.empty()) {
        const std::string reward = ascii_upper(in.rewards[static_cast<size_t>(draw_index(rng, in.rewards.size()))]);
        if (reward == "BUILDABLE_UNIT" && !in.buildable.empty()) {
            fin.reward = reward;
            fin.reward_params = {in.buildable[static_cast<size_t>(draw_index(rng, in.buildable.size()))]};
        } else if (reward == "UNIQUE_UNIT" && !in.unit.empty() && !in.own_planets.empty()) {
            fin.reward = reward;
            const std::string unit = in.unit[static_cast<size_t>(draw_index(rng, in.unit.size()))];
            fin.reward_params = {unit, in.own_planets[static_cast<size_t>(draw_index(rng, in.own_planets.size()))].name};
        } else if (reward == "CREDITS") {
            fin.reward = reward;
            fin.reward_params = {std::to_string(n * 10000)};
        } else {
            out.log.push_back("Random story reward " + reward + " is unknown!");
        }
        if (!fin.reward.empty()) out.log.push_back("Reward type " + fin.reward + ", Param1 " + fin.reward_params.front());
    }
    chapter(n);
    events.push_back(std::move(fin));
    out.xml = "<Story>\n";
    for (const Event& e : events) out.xml += e.text();
    out.xml += "</Story>\n";
    return out;
}

}  // namespace alamo::sim
