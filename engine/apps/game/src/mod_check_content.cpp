// Mod check, content family (mod_check_detail.hpp): the counts a session sees, the skirmish map rows, the guard limits
// the data reaches (mcx5) and build_galaxy_map of every campaign.
#include "mod_check_detail.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/sim/game_modes.hpp"  // sim::kMaxPlayers (mcx5 limit checks)
#include "alamo/vfs/vfs.hpp"

namespace alamo::game::mod_check_detail {

void count_content(const GameData& d, ModCheckReport& r) {
    r.counts.campaigns = d.db.campaigns().size();
    r.counts.planets = d.db.planets().size();
    r.counts.trade_routes = d.db.trade_routes().size();
    r.counts.object_types = d.types.size();
    r.counts.factions = d.rules.factions.size();
    // mcx5 t05 (MOD_COMPAT_DESIGN §8): data that reaches a guard. A campaign makes one player per faction (BKC1_RE_ANSWERS
    // Q2), and the sim's player ids stop at sim::kMaxPlayers (start_galactic refuses more).
    if (r.counts.factions > sim::kMaxPlayers)
        r.limits.push_back({"sim::kMaxPlayers (one player per faction)", sim::kMaxPlayers, r.counts.factions});
#ifdef ALAMO_GAME_HAVE_GALACTIC_AI
    if (d.ai) {
        r.counts.ai = true;
        r.counts.ai_plans = d.ai->plan_files.size();
#ifdef ALAMO_SIM_HAVE_TACTICAL_AI
        if (d.ai->tactical) {
            r.counts.tactical_space_plans = d.ai->tactical->space_plans.size();
            r.counts.tactical_land_plans = d.ai->tactical->land_plans.size();
        }
#endif
    }
#endif
    if (d.vfs) {
        const SkirmishMapCatalog cat = build_skirmish_map_catalog(*d.vfs);
        const std::vector<SkirmishMapEntry> rows = skirmish_map_rows(cat, false), custom = skirmish_map_rows(cat, true);
        r.counts.official_maps = rows.size();
        r.counts.custom_maps = custom.size();
        for (const auto* tab : {&rows, &custom})
            for (const SkirmishMapEntry& e : *tab) {
                const auto res = d.vfs->resolve(e.path);
                r.skirmish_maps.push_back({e.map, e.kind == 1, e.players, res && res->layer == vfs::Vfs::Resolution::Mod, tab == &custom});
            }
    }
}

// Mods ship one campaign per playable faction, so the same skipped route shows up in many: one line per message.
void check_campaigns(const GameData& d, ModCheckReport& r) {
    std::map<std::string, std::vector<std::string>> by_message;  // message -> campaigns, in database order
    for (const data::CampaignDef& c : d.db.campaigns()) {
        std::vector<std::string> diag;
        const auto map = data::build_galaxy_map(d.db, c.name, &diag);
        ++r.campaigns_checked;
        if (map.planets.size() == 0) r.campaigns_without_planets.push_back(c.name);
        else r.campaigns_startable.push_back(c.name);
        const std::string prefix = c.name + ": ";
        for (std::string_view msg : diag) {
            if (msg.starts_with(prefix)) msg.remove_prefix(prefix.size());
            by_message[std::string(msg)].push_back(c.name);
        }
    }
    for (const auto& [msg, camps] : by_message) {
        std::string line = msg + " (" + std::to_string(camps.size()) + (camps.size() == 1 ? " campaign: " : " campaigns: ");
        constexpr size_t kNamed = 3;
        for (size_t i = 0; i < camps.size() && i < kNamed; ++i) line += (i ? ", " : "") + camps[i];
        if (camps.size() > kNamed) line += ", ...";
        r.campaign_warnings.push_back(line + ")");
        r.campaign_warning_count += camps.size();
    }
}

}  // namespace alamo::game::mod_check_detail
