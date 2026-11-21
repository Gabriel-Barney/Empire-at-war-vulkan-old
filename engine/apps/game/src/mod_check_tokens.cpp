// Mod check, token-coverage family (mod_check_detail.hpp, report v4): perception equation token paths the resolver of
// the equation's mode does not evaluate.
#include "mod_check_detail.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/vfs/vfs.hpp"

#ifdef ALAMO_GAME_HAVE_GALACTIC_AI
#include "alamo/ai/galactic_ai_tokens.hpp"
#include "alamo/ai/perception_equations.hpp"
#include "alamo/ai/tactical/tactical_perception.hpp"
#endif

namespace alamo::game::mod_check_detail {

namespace {

#ifdef ALAMO_GAME_HAVE_GALACTIC_AI
// Which resolvers evaluate each equation (bit 0 galactic, bit 1 tactical; 0 = not known). Retail loads every
// PerceptualEquations file into one set, and the caller decides the mode: the template mode for a template's trigger /
// untrigger / budget equations, and the caller's mode for an equation another one names. Goal proposal functions and
// equations Lua plans name stay 0 (either resolver counts): a set's entry names its goal, not the mode it is evaluated in
// (EAW-Remake parks land functions on the galactic AlwaysOff goal), and the plans are not traced.
constexpr uint8_t kModeGalactic = 1, kModeTactical = 2;
std::vector<uint8_t> equation_modes(const ai::EquationSet& set, const ai::AiDefinitions& defs) {
    std::vector<uint8_t> modes(set.size(), 0);
    const auto bit = [](ai::AiMode m) { return m == ai::AiMode::Galactic ? kModeGalactic : kModeTactical; };
    const auto seed = [&](std::string_view name, uint8_t b) {
        const uint32_t i = name.empty() ? ai::kUnresolved : set.find(name);
        if (i < modes.size()) modes[i] = static_cast<uint8_t>(modes[i] | b);
    };
    for (const ai::AiPlayerTypeDef& p : defs.players)
        for (uint32_t m = 0; m < ai::kAiModeCount; ++m)
            for (const std::string& name : p.templates[m])
                if (const ai::TemplateDef* t = defs.find_template(name)) {
                    const uint8_t b = bit(static_cast<ai::AiMode>(m));
                    seed(t->trigger, b);
                    seed(t->untrigger, b);
                    for (const auto& [category, eq] : t->budget) seed(eq, b);
                }
    // An equation named by another one runs in its caller's modes (index order until nothing changes).
    for (bool changed = true; changed;) {
        changed = false;
        for (uint32_t i = 0; i < static_cast<uint32_t>(set.size()); ++i) {
            if (!modes[i]) continue;
            for (const ai::ExprNode& n : set.equation(i).expr.nodes) {
                if (n.op != ai::Op::Function || n.ref >= modes.size()) continue;
                const uint8_t m = static_cast<uint8_t>(modes[n.ref] | modes[i]);
                if (m != modes[n.ref]) {
                    modes[n.ref] = m;
                    changed = true;
                }
            }
        }
    }
    return modes;
}
#endif

}  // namespace

void check_token_coverage(const GameData& d, ModCheckReport& r) {
#ifdef ALAMO_GAME_HAVE_GALACTIC_AI
    if (!d.ai) return;
    const ai::EquationSet& set = d.ai->equations;
    TokenCoverage& tc = r.token_coverage;
    tc.checked = true;
    struct Acc {
        TokenGap g;
        bool unregistered = false;
        bool galactic = false, tactical = false;  // the resolver evaluates the path
        bool implemented = true;                  // every equation using it runs in a mode that evaluates it
        bool cited = false, cited_in_mod = false; // g.first_equation / first_file are set (from a mod file)
    };
    std::map<std::string, Acc> paths;  // display path: sorted, deterministic
    std::map<std::string, int8_t> file_in_mod;
    const std::vector<uint8_t> modes = equation_modes(set, d.ai->defs);
    for (uint32_t i = 0; i < static_cast<uint32_t>(set.size()); ++i) {
        const ai::Equation& e = set.equation(i);
        if (!e.parsed) continue;
        ++tc.equations;
        std::set<std::string> seen;  // an equation counts once per path
        for (const ai::ExprNode& n : e.expr.nodes) {
            if (n.op != ai::Op::Token) continue;
            const auto path = std::span<const ai::TokenId>(e.expr.path).subspan(n.path_begin, n.path_len);
            std::string shown = ai::root_name(n.root);  // as the equations write it: Variable_Target.EnemyForce.X
            for (const ai::TokenId t : path) shown += "." + std::string(set.tokens().name(t));
            if (!seen.insert(shown).second) continue;
            Acc& a = paths[shown];
            if (a.g.equations++ == 0) {
                a.g.token = shown;
                a.g.first_equation = e.name;
                a.g.first_file = e.file;
                std::string key;
                bool registered = true;
                for (const ai::TokenId t : path) {
                    registered = registered && ai::TokenTable::is_registered(t);
                    if (!key.empty()) key += '.';
                    key += normalize_name(set.tokens().name(t));
                }
                // The port evaluates it: fine whatever the token table says. Not evaluated and a name the token table does
                // not know: retail fails the equation too (retail-equivalent). Else a port gap.
                a.galactic = ai::GalacticSimTokenResolver::path_registered(n.root, key);
                a.tactical = ai::tactical::TacticalTokenResolver::path_implemented(n.root, path, set.tokens());
                a.unregistered = !a.galactic && !a.tactical && !registered;
            }
            // Each mode the equation runs in needs its resolver; an equation of unknown mode either one.
            const uint8_t mode = modes[i];
            const bool covered = mode == 0 ? a.galactic || a.tactical
                                           : (!(mode & kModeGalactic) || a.galactic) && (!(mode & kModeTactical) || a.tactical);
            if (!covered) a.implemented = false;
            int8_t& im = file_in_mod[e.file];
            if (im == 0) im = static_cast<int8_t>(d.vfs && in_mod_layer(*d.vfs, e.file) ? 1 : -1);
            // The finding cites the first equation that shows it (one that misses the path, or any for an unregistered
            // name), and the mod's first one when a mod's equation does.
            if ((!covered || a.unregistered) && (!a.cited || (im > 0 && !a.cited_in_mod))) {
                a.g.first_equation = e.name;
                a.g.first_file = e.file;
                a.cited = true;
                a.cited_in_mod = im > 0;
            }
            a.g.in_mod = a.g.in_mod || (im > 0 && (!covered || a.unregistered));
        }
    }
    tc.leaves = static_cast<uint32_t>(paths.size());
    for (auto& [shown, a] : paths) {
        if (a.unregistered) {
            r.retail_equivalent.push_back({"unregistered_token", shown, a.g.equations, a.g.first_file, a.g.in_mod,
                                           "a token retail's token table does not register: retail fails the whole equation too"});
            continue;
        }
        if (!a.implemented) tc.missing.push_back(std::move(a.g));
    }
    std::stable_sort(tc.missing.begin(), tc.missing.end(), [](const TokenGap& a, const TokenGap& b) {
        if (a.equations != b.equations) return a.equations > b.equations;
        return a.token < b.token;
    });
#else
    (void)d;
    (void)r;
#endif
}

}  // namespace alamo::game::mod_check_detail
