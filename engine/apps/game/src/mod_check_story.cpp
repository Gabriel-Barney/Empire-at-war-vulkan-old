// Mod check, story family (mod_check_detail.hpp, report v4): the story plots' reward and event types against the
// port's tables (sim/story_rewards.hpp).
#include "mod_check_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/core/ascii.hpp"
#include "alamo/sim/story_rewards.hpp"
#include "alamo/vfs/vfs.hpp"

namespace alamo::game::mod_check_detail {

namespace {

// Every <Event_Type> / <Reward_Type> value of the Data\XML files (the story plots; a plain text scan, so a mod's plot
// counts whether or not a campaign names it).
void scan_story_values(std::string_view text, std::string_view lower, std::string_view open, std::vector<std::string>& out) {
    size_t pos = 0;
    while ((pos = lower.find(open, pos)) != std::string_view::npos) {
        pos += open.size();
        const size_t end = text.find('<', pos);
        if (end == std::string_view::npos) break;
        std::string v = normalize_name(ascii_trim(text.substr(pos, end - pos)));
        if (!v.empty()) out.push_back(std::move(v));
        pos = end;
    }
}

}  // namespace

void check_story(const GameData& d, ModCheckReport& r) {
    if (!d.vfs || !d.files) return;
    std::map<std::string, StoryUse> rewards, events;  // upper-case name: sorted, deterministic
    std::vector<uint8_t> bytes;
    std::vector<std::string> found;
    for (const std::string& path : d.vfs->list("Data\\XML\\**.xml")) {
        if (!d.files->read(path, bytes)) continue;
        std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        for (size_t c = text.find("<!--"); c != std::string::npos; c = text.find("<!--", c)) {  // commented-out events
            const size_t e = text.find("-->", c + 4);
            const size_t end = e == std::string::npos ? text.size() : e + 3;
            std::fill(text.begin() + static_cast<std::ptrdiff_t>(c), text.begin() + static_cast<std::ptrdiff_t>(end), ' ');
            c = end;
        }
        const std::string lower = ascii_lower(text);
        if (lower.find("<event_type>") == std::string::npos) continue;
        ++r.story.files;
        const bool in_mod = in_mod_layer(*d.vfs, path);
        for (const bool reward : {true, false}) {
            found.clear();
            scan_story_values(text, lower, reward ? "<reward_type>" : "<event_type>", found);
            std::map<std::string, StoryUse>& m = reward ? rewards : events;
            for (const std::string& v : found) {
                StoryUse& u = m[v];
                if (u.uses++ == 0) {
                    u.name = v;
                    u.first_file = path;
                }
                if (in_mod && !u.in_mod) u.first_file = path;  // a mod's finding cites the mod's file
                u.in_mod = u.in_mod || in_mod;
            }
        }
    }
    r.story.rewards_used = static_cast<uint32_t>(rewards.size());
    r.story.events_used = static_cast<uint32_t>(events.size());
    for (auto& [name, u] : rewards) {
        const sim::StoryRewardRow* row = sim::find_story_reward(name);
        if (row && row->owner == sim::StoryRewardOwner::Missing) {
            u.note = std::string(row->note);
            r.story.rewards_missing.push_back(u);
        } else if (!row || row->owner == sim::StoryRewardOwner::Retail) {
            std::string why = row ? std::string(row->note)
                                  : std::string("not in retail's Reward_Type enum: its dispatcher ignores it (STORY_REWARDS_GALACTIC.md 0.1)");
            r.retail_equivalent.push_back({"unknown_reward", name, u.uses, u.first_file, u.in_mod, std::move(why)});
        }
    }
    for (auto& [name, u] : events) {
        const sim::StoryEventRow* row = sim::find_story_event(name);
        if (row && row->state == sim::StoryEventState::Partial) {
            u.note = std::string(row->note);
            r.story.events_partial.push_back(u);
        } else if (!row || row->state == sim::StoryEventState::Retail) {
            std::string why = row ? std::string(row->note) : std::string("not a retail event type: retail creates no event for it");
            r.retail_equivalent.push_back({"dropped_event", name, u.uses, u.first_file, u.in_mod, std::move(why)});
        }
    }
}

}  // namespace alamo::game::mod_check_detail
