// P4 sim (owner: galactic agent): data tables of the FoC galactic special systems (docs/re/P4_SIM_SYSTEMS.md = P4):
// BlackMarketItems.xml, the galactic special abilities of the object types' <Abilities> lists (Black_Market_Ability,
// Galactic_Sabotage_Ability, Hack_Super_Weapon_Ability, Corruption_Ability, Planet_Income_Bonus_Ability,
// Planet_Income_Gambling_Ability), the per-type fields those systems read and the GameConstants they use.
// Loaded once by load_special_rules (galactic_rules.hpp load_galactic_rules calls it) into GalacticRules::special;
// the runtime and the public API are alamo/galactic/special_systems.hpp.
//
// Names: every ability / item name keeps its spelling; name CRCs are special_name_crc = CRC-32 over the bytes as
// written (P4 1.1 "0x215a30 over the bytes as written", 1.1 "no upper-casing").
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/galactic/economy.hpp"

namespace alamo::galactic {

// CRC-32 (IEEE) over the bytes as written, no case folding (P4 1.1). Ability / item name CRCs, income source tags.
uint32_t special_name_crc(std::string_view name);

// Activation_Style enum (ABILITIES_HARDPOINTS 3.1) [H]: 0 = unset.
enum class ActivationStyle : uint8_t {
    None = 0, GlobalAutomatic = 1, CombatAutomatic = 2, GalacticAutomatic = 3, SpaceAutomatic = 4, GroundAutomatic = 5,
    GroundActivated = 6, HeroDetected = 7, CombatImminent = 8, SpecialAttack = 9, TakeDamage = 10, UserInput = 11,
    SkirmishAutomatic = 12
};
ActivationStyle parse_activation_style(std::string_view text);  // case-insensitive, None if unknown

enum class SpecialAbilityClass : uint8_t {
    Other = 0,          // any other <Abilities> entry (kept for list order, never applied by the galactic sim)
    BlackMarket = 1,    // Black_Market_Ability (P4 1.2)
    Sabotage = 2,       // Galactic_Sabotage_Ability (P4 2.1)
    Hack = 3,           // Hack_Super_Weapon_Ability (P4 3.2)
    Corruption = 4,     // Corruption_Ability (P4 5.1)
    IncomeBonus = 5,    // Planet_Income_Bonus_Ability (P4 4.1)
    IncomeGambling = 6, // Planet_Income_Gambling_Ability (P4 4.2)
    Slicer = 7,         // Slicer_Ability (REMAINING_GAPS 1.1: R2D2_Slicer)
    SystemSpy = 8,      // System_Spy_Ability (GRC 2.3 / 2.3.1: planet visibility modifiers)
    ReducePrice = 9,    // Reduce_Production_Price_Ability (A2K_T01_BUILD_COST_RE 1.3)
    ReduceTime = 10,    // Reduce_Production_Time_Ability (same class family, the time tables)
    // galab (docs/re/GALAB_ABILITIES_RE.md)
    RemoveCorruption = 11,     // Remove_Corruption_Ability (heroes; §1)
    PlanetDestruction = 12,    // Planet_Destruction_Ability (Death Star / Death Star II; §2)
    SiphonCredits = 13,        // Siphon_Credits_Ability (Han Solo, smugglers; §3)
    GalaxyWideUpgrade = 14,    // Galaxy_Wide_Upgrade_Ability (planet bonuses, PLANETS.XML; §4)
    ReduceTechPrice = 15,      // Reduce_Technology_Price_Ability (The Maw; §5)
    PoliticalTransition = 16,  // Political_Transition_Bonus_Ability (Tarkin; §6)
    NeutralizeHero = 17,       // Neutralize_Hero_Ability (bounty hunters; §7)
    HeroProtection = 18,       // Hero_Protection_Ability (Neutralization_Cost_Multiplier; §7)
    PoliticalProtection = 19,  // Political_Control_Protection_Ability (Tarkin; §6.1)
};

struct SpecialAbilityDef {
    SpecialAbilityClass cls = SpecialAbilityClass::Other;
    std::string tag;                    // XML element name (class)
    std::string name;                   // Name attribute
    uint32_t name_crc = 0;              // special_name_crc(name) (special-ability +0x38)
    ActivationStyle style = ActivationStyle::None;
    bool initially_enabled = true;      // Initially_Enabled; default [inferred] Yes (the income abilities never set it)
    bool causes_despawn = false;        // Causes_Despawn (+0x41)
    float min_respawn = 0.0f;           // Min_Respawn_Time (+0x54), default 0 [M] (P4 8 item 4)
    float max_respawn = 0.0f;           // Max_Respawn_Time (+0x58)
    bool must_be_bought = false;        // Must_Be_Bought_On_Black_Market (+0x168)
    // Black_Market_Ability (P4 1.2)
    std::vector<float> bm_min_respawn, bm_max_respawn;  // Min/Max_Respawn_Times, index = tech level - 1 (cleared when min > max)
    float price_modifier = 1.0f;        // Price_Modifier
    int32_t tech_level_adjustment = 0;  // Tech_Level_Adjustment (parsed; no reader found [M])
    // Galactic_Sabotage_Ability (P4 2.1): option flags 0..4 in option order
    std::array<bool, 5> sabotage_can{};  // Can_Reduce_Ground_Base_Level, _Starbase_Level, Can_Cancel_Production,
                                         // Can_Destroy_Special_Structure, Can_Halt_Credit_Production
    float halt_duration = 0.0f;          // Duration_Of_Credit_Halt (10.0 when halting is on and <= 0; -1 = permanent)
    float cost_increase_factor = 2.0f;   // Sabotage_Cost_Increase_Factor
    // Planet_Income_Bonus_Ability (P4 4.1)
    float income_percentage = 0.0f;      // Percentage_Income_Modifier (a fraction, >= -1; smaller -> 0 with a diagnostic)
    float income_absolute = 0.0f;        // Absolute_Income_Modifier
    float mining_multiplier = 1.0f;      // Additional_Multiplier_On_Mining_Colonies
    std::string source_text;             // Specific_Mod_Source_Text
    uint32_t source_tag = 0;             // special_name_crc(source_text), 0 = empty (the owner's Text_ID CRC is used)
    // Planet_Income_Gambling_Ability (P4 4.2)
    float evaluation_interval = 0.0f;    // Evaluation_Interval_In_Secs (< 0 -> 0)
    float min_income_modifier = 0.0f;    // Min_Income_Modifier (-1..0)
    float max_income_modifier = 0.0f;    // Max_Income_Modifier (>= 0)
    float chance_to_win = 0.0f;          // Chance_To_Win (0..1)
    // Slicer_Ability (REMAINING_GAPS 1.1). Respawn lists: index = tech level (Min/Max_Respawn_Time_Per_Tech_Level);
    // curves keep >= 2 points (loader post-check 0x619980).
    std::vector<float> slice_min_respawn, slice_max_respawn;
    PiecewiseCurve chance_by_base_level;       // Chance_Modifier_By_Base_Level (+0x1a8)
    PiecewiseCurve chance_by_difficulty;       // Chance_Modifier_By_Planet_Difficulty (+0x1d0)
    PiecewiseCurve cost_by_tech_availability;  // Cost_Multiplier_By_Planet_Tech_Availability (+0x1f8)
    float chance_affinity = 0.0f;              // Chance_Affinity_Modifier (+0x220)
    float cost_affinity = 0.0f;                // Cost_Affinity_Modifier (+0x224)
    // System_Spy_Ability (GRC 2.3.1, load-time 0x70fd20): the See_* flags as a VisibilityLevelType mask (never bit 0)
    // after the prerequisite fix-ups, and Duration_In_Secs after the style fix-ups (Galactic_Automatic -> -1,
    // Ground_Activated <= 0 -> 30; any other style -> style None).
    uint32_t spy_mask = 0;                     // ability +0x18C
    float spy_duration = 0.0f;                 // Duration_In_Secs
    // Reduce_Production_Price_Ability / Reduce_Production_Time_Ability (A2K_T01_BUILD_COST_RE 1.2-1.3): the fraction
    // (Price_Reduction_Percentage +0x178 / Time_Reduction_Percentage; >= 1.0 is set to 1.0 with a diagnostic, negative
    // values lengthen / raise), Stacking_Category +0x17c, Galaxy_Wide +0x180 and the applicable set (0x48d070:
    // Applicable_Unit_Categories / _Types minus Excluded_Unit_Types; "All" = every type [port choice]).
    float production_reduction = 0.0f;
    int32_t stacking_category = 0;
    bool galaxy_wide = false;
    uint64_t applicable_categories = 0;      // GameObjectCategoryType mask; ~0 = All
    std::vector<uint32_t> applicable_types;  // type indices
    std::vector<uint32_t> excluded_types;    // type indices
    // galab: Remove_Corruption_Ability (GALAB §1; ctor 0x5b1310, post-load 0x5b17f0): Corruption_Removal_Cost_Multiplier
    // +0x17C (negative -> 1.0) and Corruption_Removal_Time_In_Secs +0x180 (float; <= 0 -> 10.0).
    float removal_cost_multiplier = 1.0f;
    float removal_seconds = 10.0f;
    // Siphon_Credits_Ability (GALAB §3; post-load 0x70e9e0): Min / Max_Siphon_Percentage +0x178 / +0x17C (outside 0..1:
    // clamped with an error), Siphon_Duration_In_Cycles +0x180 (< 1 -> 1); Ground_Activated only.
    float siphon_min = 0.0f;
    float siphon_max = 0.0f;
    int32_t siphon_cycles = 0;
    // Galaxy_Wide_Upgrade_Ability (GALAB §4): Space_Upgrade_Type +0x180 / Land_Upgrade_Type +0x190 (type indices,
    // 0xFFFFFFFF = none). Galactic_Automatic only (post-load 0x6ffae0).
    uint32_t space_upgrade = 0xFFFFFFFFu;
    uint32_t land_upgrade = 0xFFFFFFFFu;
    std::string space_upgrade_name, land_upgrade_name;  // as written (resolved by the loader)
    // Reduce_Technology_Price_Ability (GALAB §5) and Political_Transition_Bonus_Ability (GALAB §6) keep their
    // Price_ / Time_Reduction_Percentage in `production_reduction` and Stacking_Category in `stacking_category`.
    // Neutralize_Hero_Ability (GALAB §7; ctor 0x615b70, post-load 0x617f70): the three cost curves (fewer than 2 points ->
    // the defaults (0,1)-(5,1), (0,1)-(1000,1), (0,1)-(1000,1)), General_Minor / Major_Hero_Cost_Mod (<= 0 -> 1.0), the
    // Owner_ / Target_Respawn_Time_In_Secs pairs (min, max; either <= 0 -> Default_Hero_Respawn_Time) and
    // Can_Neutralize_Minor / Major_Heroes (default Yes). Ground_Activated by default (ctor style 6).
    PiecewiseCurve cost_by_base_level;          // Cost_Mod_By_Base_Level (+0x178)
    PiecewiseCurve cost_by_neutralizations;     // Cost_Mod_By_Previous_Neutralizations (+0x1A0)
    PiecewiseCurve cost_by_corruption;          // Cost_Mod_By_Planetary_Corruption_Level (+0x1C8)
    float minor_cost_mod = 1.0f;                // +0x1F0
    float major_cost_mod = 1.0f;                // +0x1F4
    float owner_respawn_min = 0.0f, owner_respawn_max = 0.0f;    // +0x1F8 / +0x1FC
    float target_respawn_min = 0.0f, target_respawn_max = 0.0f;  // +0x200 / +0x204
    bool can_neutralize_minor = true;           // +0x208
    bool can_neutralize_major = true;           // +0x209
    // Hero_Protection_Ability (GALAB §7.3; ctor 0x703090): Neutralization_Cost_Multiplier +0x178 (default 1.0) under
    // `stacking_category`; Galactic_Automatic.
    float protection_multiplier = 1.0f;
};

// BlackMarketItems.xml entry (P4 1.1), table order = file order.
struct BlackMarketItemDef {
    std::string name;
    uint32_t name_crc = 0;               // +0xB8: identifies the item in commands and saves
    float uncommon_price_multiplier = 0.0f;
    uint32_t faction = 0xFFFFFFFFu;      // GalacticRules::factions index, kNoIndex = none
    int32_t tech_level = 1;
    std::vector<uint32_t> ability_crcs;  // Ability_Names, special_name_crc each
    std::vector<std::string> ability_names;
    int32_t base_cost = 0;               // Base_Cost_Credits
    int32_t tutorial_tech_level = 0;     // Tutorial_Tech_Level (not set by the ctor [L]: 0)
    int32_t tutorial_base_cost = 0;      // Tutorial_Base_Cost_Credits ([L]: 0)
    std::string icon, localized_name, localized_description;  // UI
    std::vector<std::string> used_by_units;                     // UI only [M]
};

// One corruption choice of a planet type (Corruption_N_*, P4 5.1 record 0x52c570), slot order 1, 2, 3. Presentation
// fields (speech events, briefing, Bink holograms) are not read.
// Corruption mission [inferred until docs/re/L1_S4_REMAINING.md section C]: a choice whose Tactical_Game_Mode is Land /
// Space and whose Tactical_Map_Name is set starts a tactical battle at the planet on that map (attacker = the corrupting
// player, defender = the planet's owner) with Tactical_Script_Name as the tactical story (apps/sim
// CampaignBattles::start_corruption); a choice without one corrupts the planet directly. Credits and the unit
// requirement are charged / checked by GalacticSim::corruption_charge.
struct CorruptionChoiceDef {
    uint32_t slot = 0;
    int8_t type = -1;                            // CorruptionTypeEnum 0..7
    float credit_cost = 0.0f;                    // Corruption_N_Credit_Cost (float)
    // S4: the same field as an int (get_int, else the float rounded half up): start_corruption's credits pre-check and
    // its corruption_charge fallback cost.
    int32_t credit_cost_rounded = 0;
    std::vector<uint32_t> required_unit_types;   // Corruption_N_Required_Unit_Type (type indices, a union)
    int32_t required_unit_count = 0;
    int8_t required_mission = -1;                // Corruption_N_Required_Mission_Type
    int32_t required_mission_count = 0;
    std::vector<uint32_t> required_planets;      // Corruption_N_Required_Planets (planet type indices)
    uint32_t success_unlock = 0xFFFFFFFFu;       // Corruption_N_Success_Unlock_Unit_Type (REMAINING_GAPS 1.5 item 5)
    float success_credit_bonus = 0.0f;           // Corruption_N_Success_Credit_Bonus (float; MCX4_T09_RE_ANSWERS Q1)
    // Tactical mission (S4): Corruption_N_Tactical_Game_Mode (tSubGameModeType: Galactic 0, Land 1, Space 2),
    // Tactical_Map_Name and Tactical_Script_Name (a Story_Plots file), both trimmed.
    bool tactical = false;        // the mode is Land / Space and the map is set
    bool tactical_space = false;  // mode Space (else Land)
    std::string tactical_map;
    std::string tactical_plots;
};

// a2m r2: Bounty_On_Faction_Ability. In tactical (A2M_R2_RE_B §2.4, not modelled) the holder's player gains
// Bounty_Percentage_Amount x the bounty (bounty.hpp) of each destroyed object owned by a Target_Faction_Names faction (max
// per Stacking_Category, summed over categories). The galactic bounty does not read it (§2.3, ConflictManager).
// Vanilla FoC: Underworld_Racketeering_Salvage_Effect (1.0, on the Underworld's Racketeering corruption upgrade),
// Underworld_Planetary_Corruption_Bounty (1.2, a tactical upgrade) and NalHutta_Bounty (0.15, Nal Hutta's planet bonus).
struct BountyAbilityDef {
    std::string name;                     // Name attribute
    float percentage = 0.0f;              // Bounty_Percentage_Amount
    int32_t stacking_category = 0;        // Stacking_Category
    std::vector<uint32_t> target_factions;  // Target_Faction_Names (GalacticRules::factions indices; unknown names dropped)
    bool apply_to_all_allies = false;     // Apply_To_All_Allies (parsed; allies are not modelled [port])
};

inline constexpr uint32_t kGalabRef = 0x80000000u;  // TypeSpecial::trigger_order: the entry is a `galab` index

// Per object type (index = TypeIndex). Default-constructed for every type without these fields.
struct TypeSpecial {
    std::vector<uint32_t> abilities;     // SpecialRules::abilities indices, <Abilities> list order; a team without its own
                                         // list uses its first Company_Units member's [inferred] (P4 1.5 "first member")
    bool has_income_ability = false;     // an IncomeBonus / IncomeGambling entry in `abilities`
    std::vector<uint32_t> members;       // Company_Units (type indices)
    int32_t sabotage_cost = 0;           // Sabotage_Cost_Credits (T +0x870)
    int32_t tutorial_sabotage_cost = 0;  // Tutorial_Sabotage_Cost_Credits (T +0x874)
    bool planet_vigilance = false;       // Planet_Vigilance (T +0xC0)
    bool mining_colony = false;          // Planet_Is_Mining_Colony (T +0xBE)
    float slice_difficulty = 0.0f;       // Planet_Slice_Difficulty_Rating (T +0x7d4, read as float)
    float tech_availability = 0.0f;      // Planet_Tech_Availability_Rating (T +0x7d8, float)
    std::vector<uint32_t> slice_affinity;  // Planet_Slice_Affinity_Types (T +0x7e8, resolved)
    float slice_cost = 0.0f;             // Slice_Cost_Credits (T +0x868, float)
    float black_market_modifier = 0.0f;  // Planet_Black_Market_Modifier (T +0x800, read as float)
    bool destroy_when_prereq = false;    // Destroy_When_Used_As_Corruption_Prereq (T +0xE4)
    bool generic_hero = false;           // Is_Generic_Hero (T +0x14), own or first member's
    bool named_hero = false;             // Is_Named_Hero (T +0x13), own or first member's
    uint32_t prev_level_base = 0xFFFFFFFFu;  // Prev_Level_Base (T +0x8B0)
    uint32_t text_crc = 0;               // special_name_crc(Text_ID) (T +0x120 [M]): fallback income source tag
    std::vector<CorruptionChoiceDef> corruption;  // planets: non-empty slots, slot order
    // OI galactic (STRATEGIC_GAPS 2.2): an Initially_Enabled Galactic_Stealth_Ability (Galactic_Automatic: active from
    // creation). A container without its own entry: every Company_Units member has one (a transport is hidden only when
    // all its cargo is).
    bool galactic_stealth = false;
    float detection_chance = -1.0f;  // DetectHeroAbility-style Detection_Chance (ability +0x17C), -1 = no detector
    // mcx4 t06 (MCX4_T09_RE_ANSWERS Q11): the Galactic_Stealth_Ability's Evade_Detection_Chance (+0x178), clamped to
    // [0, 1] as the activation stores it (0x3a86d0). A team without its own stealth entry takes its first member's
    // [port, as for the abilities]. FoC: 0.0 everywhere.
    float evade_detection_chance = 0.0f;
    // mcx4 t09 (Q6): the Name of every <Abilities> entry, list order (a team without its own: its first member's), for
    // the Activated_*_Ability_Names match of the galaxy-map ability button.
    std::vector<std::string> ability_entry_names;
    // GRC 2.3: OR of the spy masks of this type's Initially_Enabled Galactic_Automatic System_Spy_Ability entries (the
    // always-on spies: smugglers, bounty hunters, OLRS, palaces, Bothawui). Derived at load from `abilities`.
    uint32_t auto_spy_mask = 0;
    // SpecialRules::production indices of this type's Initially_Enabled Reduce_Production_Price / _Time abilities (list
    // order; a team without its own uses its first member's). Kept out of `abilities` so those indices stay unchanged.
    std::vector<uint32_t> production;
    // a2m r2: SpecialRules::bounty indices of this type's Bounty_On_Faction_Ability entries (list order). Kept out of
    // `abilities` so those indices stay unchanged.
    std::vector<uint32_t> bounty;
    // galab: SpecialRules::galab indices of this type's GALAB abilities (Remove_Corruption, Planet_Destruction,
    // Siphon_Credits, Galaxy_Wide_Upgrade, Reduce_Technology_Price, Political_Transition_Bonus, Neutralize_Hero,
    // Hero_Protection; list order; a team without its own uses its first member's). Kept out of `abilities` so those
    // indices (and the CRC of sessions that never use these abilities) stay unchanged.
    std::vector<uint32_t> galab;
    // The Ground_Activated walk order of P4 6.1 over both tables, <Abilities> list order: an `abilities` index, or
    // kGalabRef | a `galab` index. Equal to `abilities` for a type without GALAB entries.
    std::vector<uint32_t> trigger_order;
    // The walk list: trigger_order, or `abilities` when it is empty (tables built by hand, without GALAB entries).
    const std::vector<uint32_t>& triggers() const { return trigger_order.empty() ? abilities : trigger_order; }
    float corruption_removal_cost = 0.0f;  // planets: Planet_Corruption_Removal_Base_Cost (T +0x804, read as float)
    // GALAB §7: Neutralization_Cost (T +0x1FBC, own or first member's) and Can_Be_Neutralized_By_Minor / Major_Heroes
    // (T +0x7B / +0x7C, own or first member's; default No).
    float neutralization_cost = 0.0f;
    bool neutralizable_by_minor = false;
    bool neutralizable_by_major = false;
    bool prevents_minor_siphons = false;   // Prevents_Minor_Credit_Siphons (T +0x63; no FoC type sets it)
};

// mcx4 t09 (docs/re/MCX4_T09_RE_ANSWERS.md Q6): the galaxy-map ability button kind of a type. The kinds are tested in
// the order 6, 1, 9, 2, 3, 4, 5, 7, 8, 10; the first whose Activated_*_Ability_Names list names one of the type's
// <Abilities> entries (TypeSpecial::ability_entry_names, case-insensitive) wins: 1 Slice, 2 Sabotage, 3 System_Spy,
// 4 Neutralize_Hero, 5 Siphon_Credits, 6 Destroy_Planet, 7 Corrupt_Planet, 8 Remove_Corruption, 9 Black_Market,
// 10 Hack_Super_Weapon. kind -1 = no button; `label` = the text key ("" where it was not traced). Presentation only: no
// rule or AI reads it. [port] "applies to the planet" (0x2e6fe0 -> 0x37c430) is the name match alone.
struct ActivatedAbilitySlot {
    int32_t kind = -1;
    const char* label = "";
};
struct SpecialRules;
ActivatedAbilitySlot activated_ability_slot_of(const SpecialRules& rules, uint32_t type);

struct SpecialRules {
    std::vector<SpecialAbilityDef> abilities;  // every galactic-relevant <Abilities> entry, type order then list order
    std::vector<SpecialAbilityDef> production; // Reduce_Production_Price / _Time entries (ReducePrice / ReduceTime)
    std::vector<BountyAbilityDef> bounty;      // a2m r2: Bounty_On_Faction_Ability entries, type order then list order
    std::vector<SpecialAbilityDef> galab;      // galab: GALAB ability entries (TypeSpecial::galab), type order then list order
    std::vector<BlackMarketItemDef> items;     // BlackMarketItems.xml, file order
    std::vector<TypeSpecial> types;            // index = TypeIndex (empty in hand-built rules: every lookup defaults)
    // GameConstants
    float default_hero_respawn_time = 360.0f;  // Default_Hero_Respawn_Time (+0x11F0). Thrawn's Revenge: -360 (P4 6.2)
    uint32_t hack_required_type = 0xFFFFFFFFu; // Hack_Super_Weapon_Required_Type (+0x12040), kNoIndex = none (TR)
    float hack_cost = 50000.0f;                // Hack_Super_Weapon_Cost (+0x12140)
    std::string hack_particle;                 // Hack_Super_Weapon_Particle_Effect (presentation)
    std::string sabotage_particle;             // Sabotage_Particle_Effect (presentation)
    std::vector<std::string> activated_black_market, activated_sabotage, activated_corrupt, activated_hack;
    // mcx4 t09 (MCX4_T09_RE_ANSWERS Q6): the other six Activated_*_Ability_Names lists, read only by the galaxy-map
    // ability button (GalacticSim::activated_ability_slot).
    std::vector<std::string> activated_slice, activated_neutralize_hero, activated_siphon_credits, activated_system_spy,
        activated_destroy_planet, activated_remove_corruption;

    const TypeSpecial& type(uint32_t t) const;  // a static default for unknown / missing types
    uint32_t find_item(uint32_t name_crc) const;  // table index (first match), 0xFFFFFFFF if absent (0x48f550)
    uint32_t find_item_by_name(std::string_view name) const;  // exact name (GIVE_BLACK_MARKET 0x7E: length + memcmp)
    // 0x48d070 applicability of a ReducePrice / ReduceTime ability to type `t` with category mask `categories`:
    // excluded types never; listed types; else the category mask ("All" = ~0 matches every type) [port choice].
    static bool production_applies(const SpecialAbilityDef& a, uint32_t t, uint64_t categories);
};

}  // namespace alamo::galactic
