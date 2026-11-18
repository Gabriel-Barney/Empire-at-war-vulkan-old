#include "alamo/data/game_database.hpp"

#include <cmath>
#include <algorithm>
#include <functional>
#include <map>
#include <unordered_set>

#include "alamo/core/hash.hpp"
#include "alamo/core/log.hpp"
#include "alamo/data/values.hpp"
#include "alamo/data/xml_values.hpp"

namespace alamo::data {

namespace {

constexpr std::string_view kConstantsPath = "Data\\XML\\GameConstants.xml";
constexpr std::string_view kGameObjectList = "Data\\XML\\GameObjectFiles.xml";
constexpr std::string_view kTradeRouteList = "Data\\XML\\TradeRouteFiles.xml";
constexpr std::string_view kCampaignList = "Data\\XML\\CampaignFiles.xml";

std::string where(const XmlNode& n) {
    const XmlDocument* d = n.document();
    return (d ? d->file() : std::string("?")) + "(" + std::to_string(n.line()) + ")";
}

template <class Map>
uint32_t find_by_crc(const Map& m, std::string_view name,
                     const std::function<std::string_view(uint32_t)>& name_of) {
    const auto it = m.find(name_crc(name));
    if (it == m.end()) return kInvalidPlanet;
    for (uint32_t i : it->second)
        if (iequals(name_of(i), name)) return i;
    return kInvalidPlanet;
}

bool same_content(const TradeRouteDef& a, const TradeRouteDef& b) {
    return iequals(a.point_a, b.point_a) && iequals(a.point_b, b.point_b) && a.hs_speed_factor == b.hs_speed_factor &&
           a.political_control_gain == b.political_control_gain && a.credit_gain_factor == b.credit_gain_factor &&
           a.visible_line_name == b.visible_line_name;
}

bool same_content(const CampaignDef& a, const CampaignDef& b) {
    if (a.raw.size() != b.raw.size()) return false;
    for (size_t i = 0; i < a.raw.size(); ++i)
        if (!iequals(a.raw[i].first, b.raw[i].first) || a.raw[i].second != b.raw[i].second) return false;
    return true;
}

using WarnFn = std::function<void(std::string)>;

// A TradeRouteFiles / CampaignFiles entry and the provider its file falls back to (extra campaign lists only).
struct OrderedEntry {
    ListedXmlEntry list;
    const FileProvider* fallback = nullptr;
};
using CrcOrderedList = std::map<uint32_t, OrderedEntry>;  // key: list_file_crc of the entry name

// cu5 t05 (Q13): the entries are de-duplicated by list_file_crc and parsed in ascending CRC order; an entry whose CRC
// is already taken collapses into the first one listed (warned when `report`; the extra campaign lists do not).
void add_crc_ordered(CrcOrderedList& ordered, ListedXmlEntry en, const FileProvider* fallback, std::string_view list_path,
                     bool report, const WarnFn& warn) {
    const uint32_t key = list_file_crc(en.name);
    if (const auto it = ordered.find(key); it != ordered.end()) {
        if (report)
            warn(std::string(list_path) + ": '" + en.name + "' collapses with '" + it->second.list.name +
                 "' (same name CRC); loaded once");
        return;
    }
    ordered.emplace(key, OrderedEntry{std::move(en), fallback});
}

// cu5 t05 (Q13): last wins with whole-object replacement. A new name is appended (and indexed in `by_crc`); a repeated
// name replaces the earlier definition in its slot, counting the definitions, with the duplicate warning.
template <class Def>
void upsert_last_wins(std::vector<Def>& defs, std::unordered_map<uint32_t, std::vector<uint32_t>>& by_crc, Def d,
                      const XmlNode& element, std::string_view kind, const WarnFn& warn) {
    const uint32_t idx = find_by_crc(by_crc, d.name, [&defs](uint32_t k) { return std::string_view(defs[k].name); });
    if (idx == kInvalidPlanet) {
        by_crc[d.name_crc].push_back(static_cast<uint32_t>(defs.size()));
        defs.push_back(std::move(d));
        return;
    }
    warn(where(element) + ": duplicate " + std::string(kind) + " '" + d.name + "' replaces the earlier definition (" +
         (same_content(defs[idx], d) ? "identical content" : "CONTENT DIFFERS") + ")");
    d.definitions = defs[idx].definitions + 1;
    defs[idx] = std::move(d);  // last wins, no field merge
}

}  // namespace

uint32_t list_file_crc(std::string_view listed_name) {
    const std::string_view n = trim(listed_name);
    return crc32(n.data(), n.size());
}

// ------------------------------------------------------------------------------------------
// Small accessors

std::optional<std::string_view> GameConstants::get(std::string_view tag) const {
    const auto it = index_.find(normalize_name(tag));
    if (it == index_.end()) return std::nullopt;
    return fields[it->second].second;
}

// mcx1 t02: retail's converters (MCX1_RE_ANSWERS Q1 / Q2, OBJECT_TYPES §4.5): a stored value always converts (atoi / atof
// prefix, garbage 0; bool "1" or a first Y / T); only an absent tag gives the fallback (fields holds no empty or TBD values).
float GameConstants::get_float(std::string_view tag, float fallback) const {
    const auto v = get(tag);
    return v ? xml_values::to_float(*v) : fallback;
}

int32_t GameConstants::get_int(std::string_view tag, int32_t fallback) const {
    const auto v = get(tag);
    return v ? xml_values::to_int(*v) : fallback;
}

bool GameConstants::get_bool(std::string_view tag, bool fallback) const {
    const auto v = get(tag);
    return v ? xml_values::to_bool(*v) : fallback;
}

uint32_t PlanetTypes::find(std::string_view planet_name) const {
    return find_by_crc(by_crc_, planet_name, [this](uint32_t i) { return std::string_view(name[i]); });
}

std::string_view CampaignDef::raw_value(std::string_view tag) const {
    // mcx1 t03 (MCX1_RE_ANSWERS Q1): a repeated scalar tag takes its last occurrence (`raw` holds no empty / TBD value).
    for (auto it = raw.rbegin(); it != raw.rend(); ++it)
        if (iequals(it->first, tag)) return it->second;
    return {};
}

std::vector<std::string_view> CampaignDef::raw_values(std::string_view tag) const {
    std::vector<std::string_view> out;
    for (const auto& [k, v] : raw)
        if (iequals(k, tag)) out.push_back(v);
    return out;
}

const TradeRouteDef* GameDatabase::find_trade_route(std::string_view name) const {
    const uint32_t i = find_by_crc(route_by_crc_, name, [this](uint32_t k) { return std::string_view(routes_[k].name); });
    return i == kInvalidPlanet ? nullptr : &routes_[i];
}

const CampaignDef* GameDatabase::find_campaign(std::string_view name) const {
    const uint32_t i =
        find_by_crc(campaign_by_crc_, name, [this](uint32_t k) { return std::string_view(campaigns_[k].name); });
    return i == kInvalidPlanet ? nullptr : &campaigns_[i];
}

const GameObjectRecord* GameDatabase::find_game_object(std::string_view name) const {
    const uint32_t i = find_by_crc(object_by_crc_, name, [this](uint32_t k) { return objects_[k].name; });
    return i == kInvalidPlanet ? nullptr : &objects_[i];
}

std::vector<const GameObjectRecord*> GameDatabase::find_game_objects(std::string_view name) const {
    std::vector<const GameObjectRecord*> out;
    const auto it = object_by_crc_.find(name_crc(name));
    if (it == object_by_crc_.end()) return out;
    for (uint32_t i : it->second)
        if (iequals(objects_[i].name, name)) out.push_back(&objects_[i]);
    return out;
}

std::vector<const CampaignDef*> GameDatabase::campaigns_by_sort_order() const {
    std::vector<const CampaignDef*> out;
    out.reserve(campaigns_.size());
    for (const CampaignDef& c : campaigns_) out.push_back(&c);
    std::stable_sort(out.begin(), out.end(),
                     [](const CampaignDef* a, const CampaignDef* b) { return a->sort_order < b->sort_order; });
    return out;
}

// ------------------------------------------------------------------------------------------
// Loading

void GameDatabase::warn(std::string message) {
    log::warnf("data", "%s", message.c_str());
    if (diag_) diag_->push_back(std::move(message));
}

const XmlDocument* GameDatabase::load_file(const FileProvider& provider, const FileProvider* fallback,
                                           std::string_view path, bool required) {
    const std::string key = normalize_path(path);
    if (const auto it = file_index_.find(key); it != file_index_.end()) {
        const XmlDocument* cached = files_[it->second].doc.get();
        return cached->ok() ? cached : nullptr;
    }

    std::vector<uint8_t> bytes;
    const FileProvider* used = &provider;
    if (!provider.read(path, bytes)) {
        if (!fallback || !fallback->read(path, bytes)) {
            // mcx1 t08 (MCX1_RE_ANSWERS Q6.1): GameConstants.xml and the core list files are fatal in the original too
            // (its init fails and the process ends); the port refuses the load and says so.
            warn(std::string(required ? "error: " : "") + "unable to open XML file '" + std::string(path) + "'" +
                 (required ? " (the original game does not start without it either)" : " (skipped)"));
            return nullptr;
        }
        used = fallback;
    }
    LoadedXmlFile f;
    f.path = std::string(path);
    f.origin = used->origin(path);
    f.doc = std::make_unique<XmlDocument>(XmlDocument::parse(bytes, f.path));
    for (const XmlDiagnostic& d : f.doc->diagnostics()) warn(d.to_string());
    const XmlDocument* doc = f.doc.get();
    file_index_.emplace(key, static_cast<uint32_t>(files_.size()));
    files_.push_back(std::move(f));
    return doc->ok() ? doc : nullptr;
}

// mcx1 t08 (MCX1_RE_ANSWERS Q6.1): a listed trade-route or campaign file that opens but does not parse ends the original's
// init (the process exits); the port refuses the load and says why. A listed file that does not open is skipped there too.
bool GameDatabase::listed_file_unparsable(std::string_view path, const char* what) {
    const auto it = file_index_.find(normalize_path(path));
    if (it == file_index_.end() || files_[it->second].doc->ok()) return false;
    warn(std::string("error: listed ") + what + " file '" + std::string(path) +
         "' does not parse (the original game does not start with it either)");
    return true;
}

// Every <File> entry in list order (duplicates kept; GameObjectFiles.xml parses them twice).
std::vector<ListedXmlEntry> GameDatabase::read_list(const FileProvider& provider, std::string_view list_path, bool& ok) {
    const XmlDocument* doc = load_file(provider, nullptr, list_path, true);
    if (!doc) {
        ok = false;
        return {};
    }
    return listed_xml_entries(*doc);
}

void GameDatabase::load_constants(const FileProvider& provider, bool& ok) {
    const XmlDocument* doc = load_file(provider, nullptr, kConstantsPath, true);
    if (!doc) {
        ok = false;
        return;
    }
    GameConstants& c = constants_;
    c.element = doc->root();
    // mcx1 t03 (MCX1_RE_ANSWERS Q1): every element of the file, nested ones descended into; an empty or TBD value changes
    // nothing; a repeated tag: the last one wins.
    for (const auto& [tag, value] : xml_values::fields(c.element, xml_values::kData)) {
        c.index_[normalize_name(tag)] = static_cast<uint32_t>(c.fields.size());
        c.fields.emplace_back(tag, value);
    }
    // Retail atof, like get_float: an unreadable value gives 0 (mcx1 t02).
    if (const auto v = c.get("MaximumFleetMovementDistance")) c.max_fleet_move_distance = xml_values::to_float(*v);
}

void GameDatabase::load_game_objects(const FileProvider& provider, bool& ok) {
    // Q13: list order, a file listed twice is parsed twice (its objects become duplicates).
    const std::vector<ListedXmlEntry> list = read_list(provider, kGameObjectList, ok);
    std::unordered_set<std::string> listed;
    for (const ListedXmlEntry& en : list) {
        if (!listed.insert(normalize_path(en.path)).second)
            warn(std::string(kGameObjectList) + ": '" + en.name + "' listed twice; parsed twice");
        const XmlDocument* doc = load_file(provider, nullptr, en.path, false);
        if (!doc) continue;
        for (XmlNode e : doc->root().children()) {
            const std::string_view name = trim(e.attribute("Name"));
            if (name.empty()) continue;
            const uint32_t crc = name_crc(name);
            auto& bucket = object_by_crc_[crc];
            bool dup = false;
            for (uint32_t i : bucket)
                if (iequals(objects_[i].name, name)) dup = true;
            if (dup)
                warn(where(e) + ": duplicate object name '" + std::string(name) +
                     "' (kept; name lookup returns the first definition)");
            bucket.push_back(static_cast<uint32_t>(objects_.size()));
            objects_.push_back({e.name(), name, crc, e, dup});
        }
    }

    // The variant chain of an object element: the element, then its parents (first definition of each name, in any file;
    // §3.4); a missing parent or a cycle leaves the element alone.
    auto variant_chain = [this](XmlNode e) {
        std::vector<XmlNode> chain{e};
        for (int depth = 0; depth < 16; ++depth) {
            const std::string_view parent = trim(xml_values::text(chain.back(), "Variant_Of_Existing_Type", xml_values::kObject));
            if (parent.empty()) break;
            const GameObjectRecord* rec = find_game_object(parent);
            if (!rec || std::find(chain.begin(), chain.end(), rec->element) != chain.end()) {
                chain.resize(1);  // unresolved: own tags only
                break;
            }
            chain.push_back(rec->element);
        }
        return chain;
    };
    // A behaviour list along the chain: a plain type appends every occurrence (§4.4); in a variant the last non-empty one
    // replaces the parent's list.
    auto behavior_list = [](const std::vector<XmlNode>& chain, std::string_view tag) {
        std::vector<std::string> out;
        for (const XmlNode& n : chain) {
            const bool variant = !trim(xml_values::text(n, "Variant_Of_Existing_Type", xml_values::kObject)).empty() && chain.size() > 1;
            // kData walk (OBJECT_TYPES §4.2 / §4.4): nested groups descended into, empty and TBD occurrences skipped.
            std::vector<std::string> own;
            for (std::string_view v : xml_values::texts(n, tag, xml_values::kObject)) {
                if (variant) own.clear();
                for (std::string_view tok : split_list(v)) own.emplace_back(tok);
            }
            if (!own.empty()) {
                out = std::move(own);
                break;
            }
        }
        return out;
    };
    auto has_planet = [](const std::vector<std::string>& toks) {
        for (const std::string& t : toks)
            if (iequals(t, "PLANET")) return true;
        return false;
    };

    // mcx1 t05 (D4; MCX1_RE_ANSWERS Q7.3, OBJECT_TYPES §0 rule 2): the element tag carries no behaviour. A planet type is
    // any type whose resolved Behavior or GalacticBehavior list holds PLANET, whatever element it is written in. A <Planet>
    // element without PLANET keeps its row without kPlanetHasPlanetBehavior, as before (it is not a planet either way).
    PlanetTypes& p = planets_;
    std::vector<std::vector<XmlNode>> chains;
    std::vector<std::vector<std::string>> behaviors;
    std::vector<uint8_t> planet_behavior;
    for (const GameObjectRecord& o : objects_) {
        std::vector<XmlNode> chain = variant_chain(o.element);
        std::vector<std::string> beh = behavior_list(chain, "Behavior");
        const bool is_planet = has_planet(beh) || has_planet(behavior_list(chain, "GalacticBehavior"));
        if (!is_planet && !iequals(o.type, "Planet")) continue;
        const uint32_t row = static_cast<uint32_t>(p.name.size());
        p.name.emplace_back(o.name);
        p.name_crc.push_back(o.name_crc);
        p.by_crc_[o.name_crc].push_back(row);  // find() returns the first matching row
        p.element.push_back(o.element);
        p.duplicate.push_back(o.duplicate ? uint8_t{1} : uint8_t{0});
        chains.push_back(std::move(chain));
        behaviors.push_back(std::move(beh));
        planet_behavior.push_back(is_planet ? uint8_t{1} : uint8_t{0});
    }

    // The planet columns, after every file is read: a <Variant_Of_Existing_Type> planet takes each field it does not
    // set from its resolved parent (OBJECT_TYPES.md §3.3: scalars from the child when it has the tag). Within one element
    // a repeated scalar tag takes its last occurrence and an empty or TBD value changes nothing (mcx1 t03, §4.4); values
    // convert by their type codes (mcx1 t02: In_Background / Always_Instantiate_Galactic bool 0x0, the ints atoi).
    // modc t06: a mod defines every planet as a variant of one template planet; base FoC / EaW define no planet variant.
    for (uint32_t row = 0; row < p.name.size(); ++row) {
        const std::vector<XmlNode>& chain = chains[row];
        // The nearest element of the chain that sets <tag>; its last occurrence.
        auto text = [&](std::string_view tag) -> std::string_view {
            for (const XmlNode& n : chain)
                if (const std::string_view v = xml_values::text(n, tag, xml_values::kObject); !v.empty()) return v;
            return {};
        };
        Vec3 pos{};
        if (const std::string_view gp = text("Galactic_Position"); !gp.empty()) {
            if (const auto v = parse_vec3(gp)) pos = *v;
            else warn(where(p.element[row]) + ": planet '" + p.name[row] + "' has a bad Galactic_Position");
        }
        p.position.push_back(pos);
        uint32_t flags = 0;
        if (xml_values::bool_or(text("In_Background"), false)) flags |= kPlanetInBackground;
        if (xml_values::bool_or(text("Always_Instantiate_Galactic"), false)) flags |= kPlanetAlwaysInstantiate;
        if (planet_behavior[row] != 0) flags |= kPlanetHasPlanetBehavior;
        p.behaviors.push_back(std::move(behaviors[row]));
        p.flags.push_back(flags);
        p.credit_value.push_back(xml_values::int_or(text("Planet_Credit_Value"), 0));
        p.max_space_base.push_back(xml_values::int_or(text("Max_Space_Base"), 0));
        p.max_ground_base.push_back(xml_values::int_or(text("Max_Ground_Base"), 0));
    }
}

void GameDatabase::load_trade_routes(const FileProvider& provider, bool& ok) {
    const WarnFn warn_fn = [this](std::string m) { warn(std::move(m)); };
    // Q13: de-duplicated and parsed in ascending CRC order of the listed name.
    CrcOrderedList ordered;
    for (ListedXmlEntry& en : read_list(provider, kTradeRouteList, ok))
        add_crc_ordered(ordered, std::move(en), nullptr, kTradeRouteList, true, warn_fn);
    for (const auto& [key, oe] : ordered) {
        const ListedXmlEntry& en = oe.list;
        const XmlDocument* doc = load_file(provider, nullptr, en.path, false);
        if (!doc) {
            if (listed_file_unparsable(en.path, "trade-route")) ok = false;
            continue;
        }
        for (XmlNode e : doc->root().children("TradeRoute")) {
            const std::string_view name = trim(e.attribute("Name"));
            if (name.empty()) {
                warn(where(e) + ": <TradeRoute> without a Name ignored");
                continue;
            }
            TradeRouteDef r;  // a whole new object: defaults + this element's XML
            r.name = std::string(name);
            r.name_crc = name_crc(name);
            r.file = en.path;
            r.element = e;
            // mcx1 t02 / t03 (MCX1_RE_ANSWERS Q1): every field (nested elements descended into, empty / TBD change nothing),
            // the last occurrence wins; the numbers are atof (0x8) / atoi (Political_Control_Gain 0x5), garbage 0.
            for (const auto& [tag, v] : xml_values::fields(e, xml_values::kData)) {
                if (iequals(tag, "Point_A")) r.point_a = std::string(v);
                else if (iequals(tag, "Point_B")) r.point_b = std::string(v);
                else if (iequals(tag, "HS_Speed_Factor")) r.hs_speed_factor = xml_values::to_float(v);
                else if (iequals(tag, "Political_Control_Gain"))
                    r.political_control_gain = static_cast<float>(xml_values::to_int(v));
                else if (iequals(tag, "Credit_Gain_Factor")) r.credit_gain_factor = xml_values::to_float(v);
                else if (iequals(tag, "Visible_Line_Name")) r.visible_line_name = std::string(v);
            }
            upsert_last_wins(routes_, route_by_crc_, std::move(r), e, "trade route", warn_fn);
        }
    }
}

void GameDatabase::load_campaigns(const FileProvider& provider, const LoadOptions& options, bool& ok) {
    const WarnFn warn_fn = [this](std::string m) { warn(std::move(m)); };
    // Q13: de-duplicated and parsed in ascending CRC order of the listed name.
    CrcOrderedList ordered;
    for (ListedXmlEntry& en : read_list(provider, kCampaignList, ok))
        add_crc_ordered(ordered, std::move(en), nullptr, kCampaignList, true, warn_fn);
    if (options.merge_campaign_lists) {
        for (const LoadOptions::ExtraList& extra : options.extra_campaign_lists) {
            const FileProvider& lp = extra.provider ? *extra.provider : provider;
            // Extra lists are parsed separately even if their path equals the primary list's,
            // because they normally come from a different provider (e.g. the EaW tree).
            std::vector<uint8_t> bytes;
            if (!lp.read(extra.path, bytes)) {
                warn("unable to open extra campaign list '" + extra.path + "' (skipped)");
                continue;
            }
            LoadedXmlFile f;
            f.path = extra.path;
            f.origin = lp.origin(extra.path);
            f.doc = std::make_unique<XmlDocument>(XmlDocument::parse(bytes, f.origin.empty() ? f.path : f.origin));
            for (const XmlDiagnostic& d : f.doc->diagnostics()) warn(d.to_string());
            const XmlDocument* doc = f.doc.get();
            files_.push_back(std::move(f));  // not indexed: its path may shadow the primary list
            for (ListedXmlEntry& en : listed_xml_entries(*doc))
                add_crc_ordered(ordered, std::move(en), extra.provider, kCampaignList, false, warn_fn);
        }
    }

    for (const auto& [key, en] : ordered) {
        const XmlDocument* doc = load_file(provider, en.fallback, en.list.path, false);
        if (!doc) {
            if (listed_file_unparsable(en.list.path, "campaign")) ok = false;
            continue;
        }
        // mcx1 t05 (D4; MCX1_RE_ANSWERS Q1): every top-level child of a campaign file is a campaign, whatever its tag.
        for (XmlNode e = doc->root().first_child(); e; e = e.next_sibling()) {
            const std::string_view name = trim(e.attribute("Name"));
            if (name.empty()) {
                warn(where(e) + ": <" + std::string(e.name()) + "> without a Name ignored (campaign file)");
                continue;
            }
            CampaignDef c;  // a whole new object: defaults + this element's XML
            c.name = std::string(name);
            c.name_crc = name_crc(name);
            c.file = en.list.path;
            c.element = e;
            // mcx1 t03 (MCX1_RE_ANSWERS Q1): every field in document order, nested elements descended into, empty and TBD
            // values change nothing; a repeated scalar takes the last occurrence, a repeated list appends.
            for (const auto& [tag, v] : xml_values::fields(e, xml_values::kData)) {
                c.raw.emplace_back(tag, v);
                if (iequals(tag, "Campaign_Set")) c.campaign_set = std::string(v);
                else if (iequals(tag, "Text_ID")) c.text_id = std::string(v);
                else if (iequals(tag, "Description_Text")) c.description_text = std::string(v);
                else if (iequals(tag, "Starting_Active_Player")) c.starting_active_player = std::string(v);
                else if (iequals(tag, "Sort_Order")) c.sort_order = xml_values::to_int(v);  // code 0x5: atoi
                else if (iequals(tag, "Locations")) {
                    for (std::string_view s : split_list(v)) c.locations.emplace_back(s);
                } else if (iequals(tag, "Trade_Routes")) {
                    for (std::string_view s : split_list(v)) c.trade_routes.emplace_back(s);
                } else if (iequals(tag, "Home_Location")) {
                    if (const auto pr = parse_pair(v)) c.home_locations.emplace_back(pr->first, pr->second);
                    else warn(where(e) + ": campaign '" + c.name + "': bad Home_Location '" + std::string(v) + "'");
                } else if (iequals(tag, "AI_Player_Control")) c.ai_player_control.emplace_back(v);
                else if (iequals(tag, "Starting_Forces")) c.starting_forces.emplace_back(v);
                else if (iequals(tag, "Starting_Credits")) c.starting_credits.emplace_back(v);
                else if (iequals(tag, "Starting_Tech_Level")) c.starting_tech_level.emplace_back(v);
                else if (iequals(tag, "Max_Tech_Level")) c.max_tech_level.emplace_back(v);
            }
            upsert_last_wins(campaigns_, campaign_by_crc_, std::move(c), e, "campaign", warn_fn);
        }
    }
}

bool GameDatabase::load(const FileProvider& provider, const LoadOptions& options, std::vector<std::string>* diagnostics) {
    *this = GameDatabase();
    diag_ = diagnostics;
    bool ok = true;
    load_constants(provider, ok);
    load_game_objects(provider, ok);
    load_trade_routes(provider, ok);
    load_campaigns(provider, options, ok);
    // A game install keeps Data\XML inside MEG archives; a plain folder provider then sees only
    // Data\MegaFiles.xml. Say so instead of leaving four bare "unable to open" lines.
    if (!ok && files_.empty() && provider.exists("Data\\MegaFiles.xml"))
        warn("hint: no XML found loose but Data\\MegaFiles.xml exists: this looks like a game install with "
             "XML packed in MEG archives; load through alamo::vfs::Vfs instead of a folder provider");
    diag_ = nullptr;
    return ok;
}

// ------------------------------------------------------------------------------------------
// GalaxyMap

GalaxyMap build_galaxy_map(const GameDatabase& db, std::string_view campaign, std::vector<std::string>* diagnostics) {
    GalaxyMap map;
    auto report = [&](std::string s) {
        if (diagnostics) diagnostics->push_back(std::move(s));
    };
    const CampaignDef* c = db.find_campaign(campaign);
    if (!c) {
        report("campaign '" + std::string(campaign) + "' not found");
        return map;
    }
    map.campaign_name = c->name;
    map.max_fleet_move_distance = db.constants().max_fleet_move_distance;
    const std::string prefix = c->name + ": ";

    const PlanetTypes& types = db.planets();
    std::unordered_map<uint32_t, std::vector<PlanetIndex>> row_by_crc;
    auto find_row = [&](std::string_view n) -> PlanetIndex {
        const auto it = row_by_crc.find(name_crc(n));
        if (it == row_by_crc.end()) return kInvalidPlanet;
        for (PlanetIndex r : it->second)
            if (iequals(map.planets.name[r], n)) return r;
        return kInvalidPlanet;
    };

    for (const std::string& loc : c->locations) {
        const uint32_t t = types.find(loc);
        if (t == kInvalidPlanet) {
            report(prefix + "unknown planet '" + loc + "' in <Locations>");
            continue;
        }
        if (find_row(loc) != kInvalidPlanet) {
            report(prefix + "planet '" + loc + "' listed twice in <Locations>");
            continue;
        }
        const PlanetIndex r = map.planets.add(types.name[t]);
        row_by_crc[map.planets.name_crc[r]].push_back(r);
        map.planets.position[r] = types.position[t];
        map.planets.flags[r] = types.flags[t];
        map.planets.credit_value[r] = types.credit_value[t];
        map.planets.max_space_base[r] = types.max_space_base[t];
        map.planets.max_ground_base[r] = types.max_ground_base[t];
    }

    std::unordered_set<uint32_t> used_routes;
    for (const std::string& rn : c->trade_routes) {
        const TradeRouteDef* def = db.find_trade_route(rn);
        if (!def) {
            report(prefix + "unknown trade route '" + rn + "' in <Trade_Routes>");
            continue;
        }
        if (!used_routes.insert(def->name_crc).second) {
            report(prefix + "trade route '" + rn + "' listed twice in <Trade_Routes>");
            continue;
        }
        const PlanetIndex a = find_row(def->point_a);
        const PlanetIndex b = find_row(def->point_b);
        if (a == kInvalidPlanet || b == kInvalidPlanet) {
            report(prefix + "trade route '" + rn + "' endpoint '" + (a == kInvalidPlanet ? def->point_a : def->point_b) +
                   "' is not a campaign location");
            continue;
        }
        if (a == b) {
            report(prefix + "trade route '" + rn + "' connects '" + def->point_a + "' to itself");
            continue;
        }
        if (!(def->hs_speed_factor > 0.0f) || !std::isfinite(def->hs_speed_factor)) {
            report(prefix + "trade route '" + rn + "' has HS_Speed_Factor <= 0");
            continue;
        }
        const uint32_t r = map.routes.add(def->name);
        map.routes.point_a[r] = a;
        map.routes.point_b[r] = b;
        map.routes.hs_speed_factor[r] = def->hs_speed_factor;
        map.routes.political_control_gain[r] = def->political_control_gain;
        map.routes.credit_gain_factor[r] = def->credit_gain_factor;
    }
    return map;
}

}  // namespace alamo::data
