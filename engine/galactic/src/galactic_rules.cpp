#include "alamo/galactic/galactic_rules.hpp"

#include <algorithm>
#include <charconv>
#include <memory>

#include "alamo/data/game_database.hpp"
#include "alamo/core/ascii.hpp"
#include "alamo/core/hash.hpp"
#include "alamo/data/xml.hpp"
#include "alamo/data/xml_list.hpp"
#include "alamo/data/xml_values.hpp"
#include "alamo/objects/behaviors.hpp"
#include "alamo/objects/enums.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/objects/type_fields.hpp"

namespace alamo::galactic {

namespace {

float atof_or(std::string_view text, float fallback) {
    for (char c : text)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return static_cast<float>(objects::c_atof(text));
    return fallback;
}

void note(std::vector<std::string>* diag, std::string msg) {
    if (diag) diag->push_back(std::move(msg));
}

std::unique_ptr<data::XmlDocument> read_xml(const FileProvider& provider, const std::string& path,
                                            std::vector<std::string>* diag) {
    std::vector<uint8_t> bytes;
    if (!provider.read(path, bytes)) {
        note(diag, "galactic: unable to open " + path);
        return nullptr;
    }
    auto doc = std::make_unique<data::XmlDocument>(data::XmlDocument::parse(bytes, path));
    if (!doc->ok()) {
        note(diag, "galactic: unparsable " + path);
        return nullptr;
    }
    return doc;
}

bool has_field(const objects::GameObjectTypes& types, objects::TypeIndex t, std::string_view tag) {
    return types.value(t, objects::find_type_field(tag)) != nullptr;
}

int32_t int_or(const objects::GameObjectTypes& types, objects::TypeIndex t, std::string_view tag, int32_t fallback) {
    return has_field(types, t, tag) ? types.get_int(t, tag) : fallback;
}

uint32_t resolve_or_none(const objects::GameObjectTypes& types, std::string_view name) {
    const objects::TypeIndex r = types.resolve_name(ascii_trim(name));
    return r == objects::kNone ? kNoIndex : static_cast<uint32_t>(r);
}

// OI galactic (STRATEGIC_GAPS 1.2 / 1.3 / 1.7): dummy behaviour, company transport, buildability fields.
void extract_oi_fields(const objects::GameObjectTypes& types, objects::TypeIndex t, TypeInfo& i) {
    i.data_rules = true;
    i.dummy = galactic_dummy(types, static_cast<uint32_t>(t));  // mcx1 t05: generic Behavior, retail order (Q7.2)
    i.transport_behavior = types.has_behavior(t, objects::BehaviorSet::Union, objects::beh::Transport);
    for (objects::TypeIndex r : types.get_type_refs(t, "Company_Transport_Unit")) {
        if (r != objects::kNone) i.company_transport = r;
        break;  // the first entry is used (0x400f30)
    }
    for (objects::TypeIndex m : types.get_type_refs(t, "Company_Units"))
        if (m != objects::kNone) i.company_units.push_back(m);
    i.cinematic_only = types.get_bool(t, "Cinematic_Object_Only");
    i.always_spawn_in_orbit = types.get_bool(t, "Always_Spawn_In_Orbit");
    i.super_weapon_killer = types.get_bool(t, "Is_Super_Weapon_Killer");
    i.named_hero = types.get_bool(t, "Is_Named_Hero");
    i.generic_hero = types.get_bool(t, "Is_Generic_Hero");
    i.requires_initial_placement = types.get_bool(t, "Build_Requires_Initial_Placement");
    // Required_Special_Structures: the parser keeps '|' as a token (0x53 OR markers): `A | B, C` = (A or B) and C.
    bool join = false;
    for (std::string_view tok : types.get_list(t, "Required_Special_Structures")) {
        tok = ascii_trim(tok);
        if (tok.empty()) continue;
        if (tok == "|") {
            join = true;
            continue;
        }
        const uint32_t r = resolve_or_none(types, tok);
        if (join && !i.required_special_groups.empty()) i.required_special_groups.back().push_back(r);
        else i.required_special_groups.push_back({r});
        join = false;
    }
    for (objects::TypeIndex r : types.get_type_refs(t, "Required_Orbiting_Units"))
        if (r != objects::kNone) i.required_orbiting_units.push_back(r);
    {
        const std::span<const std::string_view> lst = types.get_list(t, "Required_Planets");
        std::vector<std::string_view> names(lst.begin(), lst.end());
        if (names.empty()) names = objects::split_type_list(types.get_string(t, "Required_Planets"));
        for (std::string_view n : names)
            if (!ascii_trim(n).empty()) i.required_planets.push_back(resolve_or_none(types, n));
    }
    // Defaults when a tag is absent: -1 = unlimited [L: the type constructor values were not read].
    i.max_instances_per_planet = int_or(types, t, "Build_Max_Instances_Per_Planet", -1);
    i.limit_lifetime = int_or(types, t, "Build_Limit_Lifetime_Per_Player", -1);
    i.limit_current = int_or(types, t, "Build_Limit_Current_Per_Player", -1);
    i.max_space_base = int_or(types, t, "Max_Space_Base", i.max_space_base);
    i.ground_structure_slots = int_or(types, t, "Special_Structures_Land", -1);
    i.orbital_structure_slots = int_or(types, t, "Special_Structures_Space", -1);
}

}  // namespace

uint8_t galactic_dummy(const objects::GameObjectTypes& types, uint32_t type) {
    static constexpr uint8_t kOrder[] = {kDummyGroundBase,      kDummyStarBase,      kDummyGroundStructure, kDummyOrbitalStructure,
                                         kDummyGroundCompany,   kDummyFighterSquadron, kDummyStarship};
    for (const uint8_t id : kOrder)
        if (types.has_behavior(static_cast<objects::TypeIndex>(type), objects::BehaviorSet::Generic, id)) return id;
    return 0;
}

bool has_planet_behavior(const objects::GameObjectTypes& types, uint32_t type) {
    const auto t = static_cast<objects::TypeIndex>(type);
    return types.has_behavior(t, objects::BehaviorSet::Generic, objects::beh::Planet) ||
           types.has_behavior(t, objects::BehaviorSet::Galactic, objects::beh::Planet);
}

// The behaviour ids 0x5B..0x61 (SG 1.2). Orbital structures share GroundStructure: the port keeps one structure list
// per planet.
UnitKind unit_kind_from_dummy(uint8_t dummy) {
    switch (dummy) {
    case kDummyStarBase: return UnitKind::StarBase;
    case kDummyGroundBase:
    case kDummyGroundStructure:
    case kDummyOrbitalStructure: return UnitKind::GroundStructure;
    case kDummyGroundCompany: return UnitKind::GroundUnit;
    case kDummyFighterSquadron: return UnitKind::Squadron;
    case kDummyStarship: return UnitKind::SpaceUnit;
    default: return UnitKind::Other;
    }
}

UnitKind galactic_unit_kind(uint8_t dummy, bool planet) { return planet ? UnitKind::Planet : unit_kind_from_dummy(dummy); }

uint32_t GalacticRules::find_faction(std::string_view name) const {
    for (size_t i = 0; i < factions.size(); ++i)
        if (ascii_iequals(factions[i].name, name)) return static_cast<uint32_t>(i);
    return kNoIndex;
}

uint32_t GalacticRules::find_difficulty(std::string_view name) const {
    for (size_t i = 0; i < difficulties.size(); ++i)
        if (ascii_iequals(difficulties[i].name, name)) return static_cast<uint32_t>(i);
    return kNoIndex;
}

uint32_t GalacticRules::find_type(std::string_view name) const {
    for (size_t i = 0; i < types.size(); ++i)
        if (ascii_iequals(types[i].name, name)) return static_cast<uint32_t>(i);
    return kNoIndex;
}

const GalacticRules::HopRoute* GalacticRules::find_hop_route(uint32_t planet_crc_a, uint32_t planet_crc_b) const {
    const uint32_t lo = std::min(planet_crc_a, planet_crc_b), hi = std::max(planet_crc_a, planet_crc_b);
    const auto it = std::lower_bound(hop_routes.begin(), hop_routes.end(), std::pair<uint32_t, uint32_t>{lo, hi},
                                     [](const HopRoute& r, const std::pair<uint32_t, uint32_t>& k) {
                                         return r.lo != k.first ? r.lo < k.first : r.hi < k.second;
                                     });
    return it != hop_routes.end() && it->lo == lo && it->hi == hi ? &*it : nullptr;
}

uint32_t GalacticRules::neutral_faction() const {
    for (size_t i = 0; i < factions.size(); ++i)
        if (factions[i].is_neutral) return static_cast<uint32_t>(i);
    return kNoIndex;
}

uint32_t GalacticRules::native_owner_faction() const {
    for (size_t i = 0; i < factions.size(); ++i)
        if (!factions[i].is_playable && !factions[i].is_neutral && !factions[i].can_control_planets) return static_cast<uint32_t>(i);
    return kNoIndex;
}

EconomyConstants load_economy_constants(const data::GameDatabase& db) {
    EconomyConstants c;
    const data::GameConstants& g = db.constants();
    c.fiscal_cycle_seconds = g.get_float("Fiscal_Cycle_Time_In_Secs", c.fiscal_cycle_seconds);
    c.credit_cap_per_planet = g.get_float("Credit_Cap_Per_Planet", c.credit_cap_per_planet);
    c.black_market_min = g.get_float("Black_Market_Income_Mult_Min", c.black_market_min);
    c.black_market_max = g.get_float("Black_Market_Income_Mult_Max", c.black_market_max);
    if (auto t = g.get("Corruption_Choice_Income_Percentage")) {
        const std::vector<std::string_view> tok = objects::split_type_list(*t);
        c.corruption_income_pct.fill(0.0f);
        for (size_t i = 0; i < tok.size() && i < c.corruption_income_pct.size(); ++i)
            c.corruption_income_pct[i] = static_cast<float>(objects::c_atof(tok[i]));
    }
    c.credit_alignment_bonus = g.get_float("MaxCreditIncomeAlignmentBonus", c.credit_alignment_bonus);
    c.credit_alignment_penalty = g.get_float("MaxCreditIncomeAlignmentPenalty", c.credit_alignment_penalty);
    if (auto t = g.get("Political_Income_Curve")) c.political_income_curve = PiecewiseCurve::parse(*t);
    c.max_build_queue = g.get_int("Max_Build_Queue", c.max_build_queue);
    c.pay_as_you_go = g.get_bool("Pay_As_You_Go", c.pay_as_you_go);
    c.production_speed_factor = g.get_float("Production_Speed_Factor", c.production_speed_factor);
    for (int i = 1; i <= 4; ++i)
        c.base_vs_tech[static_cast<size_t>(i)] = g.get_float(
            "Production_Speed_Mod_Base_Vs_Tech_" + std::to_string(i), c.base_vs_tech[static_cast<size_t>(i)]);
    c.max_ground_forces_on_planet = g.get_int("Max_Ground_Forces_On_Planet", c.max_ground_forces_on_planet);
    c.health_low_percent_threshold = g.get_float("Health_Low_Percent_Threshold", c.health_low_percent_threshold);  // a2l t03
    {  // a2m t05: Low_ / Medium_ / High_Threat_Reachability_Tolerance (A2M_T04_RE_ANSWERS Q3)
        static constexpr const char* kTol[3] = {"Low_Threat_Reachability_Tolerance", "Medium_Threat_Reachability_Tolerance",
                                                "High_Threat_Reachability_Tolerance"};
        for (size_t i = 0; i < 3; ++i) c.threat_reachability_tolerance[i] = g.get_float(kTol[i], c.threat_reachability_tolerance[i]);
    }
    c.corruption_hyperspace_bonus = g.get_float("Corruption_Hyperspace_Bonus", c.corruption_hyperspace_bonus);
    c.political_control_change_seconds =
        g.get_float("Political_Control_Change_Time_Seconds", c.political_control_change_seconds);
    c.influence_alignment_bonus = g.get_float("MaxInfluenceTransitionAlignmentBonus", c.influence_alignment_bonus);
    c.influence_alignment_penalty =
        g.get_float("MaxInfluenceTransitionAlignmentPenalty", c.influence_alignment_penalty);
    c.ai_uses_galactic_pop_cap = g.get_bool("AI_Uses_Galactic_Pop_Cap", c.ai_uses_galactic_pop_cap);  // A2 galactic
    // OI galactic (STRATEGIC_GAPS 2.1): the galactic mode's speed multiplier M.
    c.fleet_speed_multiplier = g.get_float("Object_Max_Speed_Multiplier_Galactic", c.fleet_speed_multiplier);
    c.planet_reveal_delay_seconds = g.get_float("Planet_Reveal_Delay_Time", c.planet_reveal_delay_seconds);  // GRC 1.3
    const auto float_list = [&g](const char* tag, std::vector<float>& out) {  // GRC 4.5
        if (auto t = g.get(tag)) {
            out.clear();
            for (std::string_view tok : objects::split_type_list(*t)) out.push_back(static_cast<float>(objects::c_atof(tok)));
        }
    };
    float_list("Particle_Brightness_Per_Corruption_Level", c.particle_brightness_per_level);
    float_list("Particle_Scale_Per_Corruption_Level", c.particle_scale_per_level);
    float_list("Particle_Energy_Per_Corruption_Level", c.particle_energy_per_level);
    if (auto t = g.get("Good_Side_Name")) c.good_side_name = std::string(ascii_trim(*t));  // REMAINING_GAPS 1.5
    if (auto t = g.get("Evil_Side_Name")) c.evil_side_name = std::string(ascii_trim(*t));  // modc t20 (presentation)
    return c;
}

std::vector<FactionRules> load_factions(const FileProvider& provider, std::vector<std::string>* diagnostics) {
    std::vector<FactionRules> out;
    constexpr std::string_view kList = "Data\\XML\\FactionFiles.xml";
    data::ListedXmlOptions opt;  // every entry is opened (an empty one fails to), each failure noted
    opt.keep_empty_entries = true;
    opt.visit_missing = true;
    opt.visit_unparsable = true;
    const data::ListStatus st = data::for_each_listed_xml(provider, kList, [&](data::ListedXmlFile& file) {
        if (!file.doc || !file.doc->ok()) {
            note(diagnostics, (file.doc ? "galactic: unparsable " : "galactic: unable to open ") + file.entry.path);
            return;
        }
        // mcx1 t03 (MCX1_RE_ANSWERS Q1): every top-level child of a faction file is a faction, whatever its tag; its fields
        // are read by the data rule (every child, nested elements descended into, empty / TBD change nothing): a repeated
        // scalar takes its last occurrence, a repeated list appends. Values convert by type code (xml_field_tables.md
        // Faction segment; t02): bools 0x0, ints 0x6 atoi, floats 0x8 atof, float lists 0x13, name lists 0x2a / 0x1e.
        for (data::XmlNode n = file.doc->root().first_child(); n; n = n.next_sibling()) {
            namespace xv = data::xml_values;
            const auto text = [&n](std::string_view tag) { return xv::text(n, tag); };
            FactionRules r;
            r.name = std::string(n.attribute("Name"));
            for (const auto& [tag, value] : xv::fields(n)) r.xml_fields.emplace_back(tag, value);
            r.is_neutral = xv::bool_or(n, "Is_Neutral", false);
            r.can_control_planets = xv::bool_or(n, "Can_Control_Planets", false);
            r.is_playable = xv::bool_or(n, "Is_Playable", false);
            r.benefits_from_corruption = xv::bool_or(n, "Benefits_From_Corruption", false);
            r.basic_ai = std::string(text("Basic_AI"));
            r.text_id = std::string(text("Text_ID"));
            r.tech_tree_dialog = std::string(text("Tech_Tree_Dialog_Name"));
            r.tech_level_icon = std::string(text("Tech_Level_Icon_Name"));
            r.icon = std::string(text("Icon_Name"));
            if (r.icon.empty()) r.icon = std::string(text("Alternate_Icon_Name"));
            r.land_selection_blob = std::string(text("Land_Mode_Selection_Blob_Material_Name"));
            r.space_selection_blob = std::string(text("Space_Mode_Selection_Blob_Material_Name"));
            r.hyperspace_speed_factor = xv::float_or(n, "Hyperspace_Speed_Factor", r.hyperspace_speed_factor);
            r.maintenance_cost = xv::float_or(n, "Maintenance_Cost", r.maintenance_cost);
            r.force_alignment = parse_force_side(text("Force_Alignment"));
            // Code 0x13 float lists append; the pair is the first two values of the appended list.
            const auto pair_list = [&n](std::string_view tag, std::array<float, 2> fallback) {
                const std::vector<std::string_view> tok = xv::list(n, tag);
                for (size_t i = 0; i < tok.size() && i < 2; ++i) fallback[i] = xv::to_float(tok[i]);
                return fallback;
            };
            r.corruption_time_mult = pair_list("Corruption_Galactic_Production_Time_Multipliers", r.corruption_time_mult);
            r.corruption_price_mult = pair_list("Corruption_Galactic_Production_Price_Multipliers", r.corruption_price_mult);
            // a2m r2: the corruption upgrades by corruption type (resolved in load_galactic_rules).
            for (std::string_view u : xv::list(n, "Automatic_Planetary_Corruption_Upgrade_Type"))
                r.automatic_corruption_upgrade_names.emplace_back(u);
            r.base_population_capacity = xv::int_or(n, "Base_Population_Capacity", 0);         // code 0x6
            r.displayed_tech_level_adjustment = xv::int_or(n, "Displayed_Tech_Level_Adjustment", 0);  // code 0x6 ("+1" = 1)
            // OI galactic: victory monitor inputs (resolved in load_galactic_rules).
            r.home_planet = std::string(text("Home_Planet"));
            r.leader_name = std::string(text("Faction_Leader"));
            r.leader_company_name = std::string(text("Faction_Leader_Company"));
            r.primary_enemy_name = std::string(text("Primary_Enemy"));
            // S7 t30 (TACTICAL_COMBAT §2.10): the relation lists (code 0x2a: every occurrence appends).
            for (std::string_view a : xv::list(n, "Allies")) r.allies.emplace_back(a);
            for (std::string_view e : xv::list(n, "Enemies")) r.enemies.emplace_back(e);
            out.push_back(std::move(r));
        }
    }, opt);
    if (st == data::ListStatus::Missing) note(diagnostics, "galactic: unable to open " + std::string(kList));
    if (st == data::ListStatus::Unparsable) note(diagnostics, "galactic: unparsable " + std::string(kList));
    return out;
}

std::vector<DifficultyRules> load_difficulties(const FileProvider& provider, std::vector<std::string>* diagnostics) {
    std::vector<DifficultyRules> out;
    auto doc = read_xml(provider, "Data\\XML\\DifficultyAdjustments.xml", diagnostics);
    if (!doc) return out;
    for (data::XmlNode n : doc->root().children("Difficulty_Adjustment")) {
        DifficultyRules d;
        d.name = std::string(n.attribute("Name"));
        d.credit_multiplier = atof_or(n.child_text("Credit_Multiplier"), 1.0f);
        d.galactic_build_time_multiplier = atof_or(n.child_text("Galactic_Build_Time_Multiplier"), 1.0f);
        d.damage_multiplier = atof_or(n.child_text("Damage_Multiplier"), 1.0f);
        d.health_multiplier = atof_or(n.child_text("Health_Multiplier"), 1.0f);
        d.shield_multiplier = atof_or(n.child_text("Shield_Multiplier"), 1.0f);
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<TypeInfo> extract_type_info(const objects::GameObjectTypes& types, const std::vector<FactionRules>& factions) {
    std::vector<TypeInfo> out(types.size());
    for (objects::TypeIndex t = 0; t < types.size(); ++t) {
        TypeInfo& i = out[t];
        i.name = std::string(types.raw().types().name[t]);
        i.hyperspace = types.get_bool(t, "Hyperspace");
        i.build_cost = types.get_int(t, "Build_Cost_Credits");
        i.build_time_seconds = types.get_int(t, "Build_Time_Seconds");
        i.tech_level = types.get_int(t, "Tech_Level");
        i.required_ground_base = types.get_int(t, "Required_Ground_Base_Level");
        i.required_star_base = types.get_int(t, "Required_Star_Base_Level");
        i.base_level = types.get_int(t, "Base_Level");
        i.next_level_base = resolve_or_none(types, types.get_string(t, "Next_Level_Base"));  // 0x3f8fd0 (A2M_R3_RE_A §1.2)
        i.advances_tech = types.get_int(t, "Build_Advances_Tech_Level");
        i.squadron_capacity = types.get_int(t, "Squadron_Capacity");
        i.hyperspace_speed = types.get_float(t, "Hyperspace_Speed");
        i.influence_range = types.get_float(t, "Galactic_Influence_Range");
        i.piracy_value = types.get_float(t, "Piracy_Value_Credits");
        i.maintenance_cost = types.get_float(t, "Maintenance_Cost");
        i.ai_combat_power = types.columns().ai_combat_power[t];
        i.reduced_by_factories = types.get_bool(t, "Build_Time_Reduced_By_Multiple_Factories");
        i.build_initially_locked = types.get_bool(t, "Build_Initially_Locked");
        i.sliceable = types.get_bool(t, "Build_Can_Be_Unlocked_By_Slicer");
        for (objects::TypeIndex r : types.get_type_refs(t, "Required_Special_Structures"))
            if (r != objects::kNone) i.required_special_structures.push_back(r);
        const std::span<const std::string_view> aff_list = types.get_list(t, "Affiliation");
        std::vector<std::string_view> aff(aff_list.begin(), aff_list.end());
        if (aff.empty()) aff = objects::split_type_list(types.get_string(t, "Affiliation"));
        for (std::string_view a : aff)
            for (size_t f = 0; f < factions.size(); ++f)
                if (ascii_iequals(factions[f].name, a)) {
                    i.affiliation.push_back(static_cast<uint32_t>(f));
                    break;
                }
        i.credit_value = types.get_int(t, "Planet_Credit_Value");
        i.destroyed_credit_value = types.get_int(t, "Planet_Destroyed_Credit_Value");
        i.capture_bonus = types.get_int(t, "Planet_Capture_Bonus_Reward");
        i.black_market_influences = types.get_bool(t, "Planet_Black_Market_Influences");
        i.starbase_stealth = types.get_bool(t, "Planet_Starbase_Stealth");
        i.groundbase_stealth = types.get_bool(t, "Planet_Groundbase_Stealth");
        i.surface_accessible =
            types.value(t, objects::find_type_field("Planet_Surface_Accessible")) == nullptr
                ? true  // ASSUMPTION: an unset Planet_Surface_Accessible means accessible
                : types.get_bool(t, "Planet_Surface_Accessible");
        i.the_force = parse_force_side(types.get_string(t, "The_Force"));
        i.force_strength = types.get_float(t, "Force_Strength");
        i.political_control = types.get_int(t, "Political_Control");  // bkg2 b0125
        if (const std::string_view pf = ascii_trim(types.get_string(t, "Political_Faction")); !pf.empty())
            for (size_t f = 0; f < factions.size(); ++f)
                if (ascii_iequals(factions[f].name, pf)) {
                    i.political_faction = static_cast<uint32_t>(f);
                    break;
                }
        {  // list code 0x3c: one (<faction name>, <int>) pair per occurrence, appended
            const std::span<const std::string_view> mx = types.get_list(t, "Planet_Max_Political_Control_For_Faction");
            for (size_t k = 0; k + 1 < mx.size(); k += 2)
                for (size_t f = 0; f < factions.size(); ++f)
                    if (ascii_iequals(factions[f].name, ascii_trim(mx[k]))) {
                        int32_t v = 0;
                        const std::string_view s = ascii_trim(mx[k + 1]);
                        std::from_chars(s.data(), s.data() + s.size(), v);
                        i.max_political_control.emplace_back(static_cast<uint32_t>(f), v);
                        break;
                    }
        }
        // A2 galactic (AI_GALACTIC_3 §3, §6).
        i.population = types.columns().population_value[t];
        i.population_capacity = types.get_int(t, "Additional_Population_Capacity");
        i.categories = types.get_mask(t, "CategoryMask");
        i.movement_class = types.get_enum(t, "MovementClass");
        for (std::string_view m : types.get_list(t, "Company_Units")) {  // a company: its first member's class
            const objects::TypeIndex mt = types.resolve_name(ascii_trim(m));
            if (mt != objects::kNone) i.movement_class = types.get_enum(mt, "MovementClass");
            break;
        }
        {
            const std::string terrain(types.get_string(t, "Terrain"));
            i.terrain = terrain_id(ascii_trim(terrain));
        }
        i.restricted_categories = types.get_mask(t, "Planet_Restricted_Unit_Categories");
        for (objects::TypeIndex r : types.get_type_refs(t, "Planet_Restricted_Unit_Types"))
            if (r != objects::kNone) i.restricted_types.push_back(r);
        extract_oi_fields(types, t, i);  // OI galactic (STRATEGIC_GAPS 1.2 / 1.3 / 1.7)
        i.kind = galactic_unit_kind(i.dummy, has_planet_behavior(types, static_cast<uint32_t>(t)));  // mcx1 t05 (D4)
    }
    // Company / squadron membership flags (type derivation 0x377500: +0x2A / +0x29 on every listed member).
    for (objects::TypeIndex t = 0; t < types.size(); ++t) {
        for (uint32_t m : out[t].company_units)
            if (m < out.size()) out[m].company_member = true;
        for (objects::TypeIndex m : types.get_type_refs(t, "Squadron_Units"))
            if (m != objects::kNone && m < out.size()) out[m].squadron_member = true;
    }
    return out;
}

int32_t terrain_id(std::string_view name) {
    for (int32_t i = 0; i < 8; ++i)
        if (ascii_iequals(name, kTerrainNames[i])) return i;
    return -1;
}

double TerrainEffectiveness::get(int32_t terrain, int32_t movement_class) const {
    if (terrain < 0 || movement_class < 0 || static_cast<size_t>(terrain) >= value.size()) return 1.0;
    const std::vector<double>& row = value[static_cast<size_t>(terrain)];
    return static_cast<size_t>(movement_class) < row.size() ? row[static_cast<size_t>(movement_class)] : 1.0;
}

TerrainEffectiveness load_terrain_effectiveness(const FileProvider& provider, const objects::GameObjectTypes& types,
                                                std::vector<std::string>* diagnostics) {
    TerrainEffectiveness out;
    if (!provider.exists("Data\\XML\\AITerrainEffectiveness.xml")) return out;
    auto doc = read_xml(provider, "Data\\XML\\AITerrainEffectiveness.xml", diagnostics);
    if (!doc) return out;
    const objects::EnumConverter& mc = types.enums().get(objects::EnumId::MovementClass);
    for (data::XmlNode terr : doc->root().children()) {
        const int32_t tid = terrain_id(terr.name());
        if (tid < 0) {
            note(diagnostics, "galactic: AITerrainEffectiveness: unknown terrain " + std::string(terr.name()));
            continue;
        }
        if (out.value.size() <= static_cast<size_t>(tid)) out.value.resize(static_cast<size_t>(tid) + 1);
        for (data::XmlNode cls : terr.children()) {
            const auto id = mc.lookup(cls.name());
            if (!id || *id < 0) {
                note(diagnostics, "galactic: AITerrainEffectiveness: unknown movement class " + std::string(cls.name()));
                continue;
            }
            std::vector<double>& row = out.value[static_cast<size_t>(tid)];
            if (row.size() <= static_cast<size_t>(*id)) row.resize(static_cast<size_t>(*id) + 1, 1.0);
            row[static_cast<size_t>(*id)] = objects::c_atof(cls.text_trimmed());
        }
    }
    return out;
}

bool load_galactic_rules(const FileProvider& provider, const data::GameDatabase& db,
                         const objects::GameObjectTypes& types, GalacticRules& out,
                         std::vector<std::string>* diagnostics) {
    out.constants = load_economy_constants(db);
    out.factions = load_factions(provider, diagnostics);
    out.difficulties = load_difficulties(provider, diagnostics);
    out.types = extract_type_info(types, out.factions);
    out.terrain_effectiveness = load_terrain_effectiveness(provider, types, diagnostics);
    load_special_rules(provider, db, types, out, diagnostics);  // P4 sim
    // bkg3 b0115: the hop-factor route per planet pair = the lowest name CRC among every loaded TradeRoute joining the two
    // planet types (a name defined twice counts once, with its last definition: GameDatabase keeps one entry per name).
    out.hop_routes.clear();
    for (const data::TradeRouteDef& r : db.trade_routes()) {
        const uint32_t a = name_crc(r.point_a), b = name_crc(r.point_b);
        GalacticRules::HopRoute h;
        h.lo = std::min(a, b);
        h.hi = std::max(a, b);
        h.route_crc = r.name_crc;
        h.hs = r.hs_speed_factor;
        out.hop_routes.push_back(h);
    }
    std::sort(out.hop_routes.begin(), out.hop_routes.end(), [](const GalacticRules::HopRoute& x, const GalacticRules::HopRoute& y) {
        if (x.lo != y.lo) return x.lo < y.lo;
        if (x.hi != y.hi) return x.hi < y.hi;
        return x.route_crc < y.route_crc;
    });
    out.hop_routes.erase(std::unique(out.hop_routes.begin(), out.hop_routes.end(),
                                     [](const GalacticRules::HopRoute& x, const GalacticRules::HopRoute& y) {
                                         return x.lo == y.lo && x.hi == y.hi;
                                     }),
                         out.hop_routes.end());
    // OI galactic: faction victory inputs (STRATEGIC_GAPS 1.4).
    for (FactionRules& f : out.factions) {
        if (!f.home_planet.empty()) f.home_planet_type = out.find_type(f.home_planet);
        if (!f.leader_name.empty()) f.leader = out.find_type(f.leader_name);
        if (!f.leader_company_name.empty()) f.leader_company = out.find_type(f.leader_company_name);
        if (!f.primary_enemy_name.empty()) f.primary_enemy = out.find_faction(f.primary_enemy_name);
        f.automatic_corruption_upgrades.clear();  // a2m r2
        for (const std::string& u : f.automatic_corruption_upgrade_names) f.automatic_corruption_upgrades.push_back(out.find_type(u));
    }
    return !out.factions.empty();
}

}  // namespace alamo::galactic
