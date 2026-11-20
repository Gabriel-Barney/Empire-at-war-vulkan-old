// ref6 t01: typed object-type field handles. Engine code outside engine/objects reads a type's tags through these instead
// of a tag string (GameObjectTypes::get_float(t, "Tag") gives 0 on a typo and lets every reader carry its own default):
//   types.get(t, tags::Guard_Chase_Range)        the effective value (the XML value, else the tag's one default)
//   types.name(t)                                the type's name
//   types.sub_objects(t, tags::Unit_Abilities_Data)
// Each handle names its tag once and carries the tag's one default - the value an unset tag reads, which is the
// type-data default of game_object_types.cpp (kDefaults, OBJECT_TYPES §4.6 / §5.4) or the converter's zero; the objects
// test alamo_objects_tests checks that every handle names a type field and that its default is what an unset tag reads.
// tools/lint/check_tag_reads.py keeps the string reads in sim code from growing (target 0). New tags: add a row to
// ALAMO_TYPE_TAGS (one tag family per commit).
#pragma once
#include <cstdint>
#include <string_view>

namespace alamo::objects {

template <class T>
struct TypeTag {
    std::string_view tag;
    T def{};  // what an unset tag reads (checked against the type data)
};
using FloatTag = TypeTag<float>;
using IntTag = TypeTag<int32_t>;  // also enums (get_enum is get_int)
using BoolTag = TypeTag<bool>;
using TextTag = TypeTag<std::string_view>;  // the value as written (trimmed); def is what an unset tag reads
// Kinds whose unset value is always empty / zero / none, so the handle carries only the name.
struct VecTag {  // up to four floats, zeros unset
    std::string_view tag;
};
struct ListTag {  // a list of words (get), or the types they name (GameObjectTypes::get_type_refs); empty unset
    std::string_view tag;
};
struct RefTag {  // one lazy type reference, kNone unset
    std::string_view tag;
};
struct MaskTag {  // a converted 64-bit mask, 0 unset
    std::string_view tag;
};
struct SubObjectsTag {
    std::string_view tag;
};

// X(kind, Name, default): kind Float / Int / Bool / Text / Vec / List / Ref / Mask / Sub; the default is used by
// Float / Int / Bool / Text and is 0 for the others (their unset value is fixed).
// clang-format off
#define ALAMO_TYPE_TAGS(X)                                                                                            \
    /* chase ranges (FIGHTER_FLYING §4.10 A; BKR1 Q8): tactical_battle_chase.cpp */                                   \
    X(Float, Guard_Chase_Range, 0.0f)                                                                                 \
    X(Float, Idle_Chase_Range, 0.0f)                                                                                  \
    X(Float, Attack_Move_Response_Range, 0.0f)                                                                        \
    X(Float, Autonomous_Move_Extension_Vs_Attacker, 0.0f)                                                             \
    /* hard extents (MOVEMENT §1.3, TACTICAL_COMBAT §3.1): Custom_Hard_* and Scale_Factor */                          \
    X(Float, Custom_Hard_XExtent, 0.0f)                                                                               \
    X(Float, Custom_Hard_YExtent, 0.0f)                                                                               \
    X(Float, Custom_Hard_XExtent_Deployed, 0.0f)                                                                      \
    X(Float, Custom_Hard_YExtent_Deployed, 0.0f)                                                                      \
    X(Float, Scale_Factor, 1.0f)                                                                                      \
    /* LOB_SOLVER §1.1: type +0x4dc, constructor default 6.0 */                                                       \
    X(Float, Ranged_Target_Z_Adjust, 6.0f)                                                                            \
    /* TRU1_A_RE_ANSWERS A1 / A2: type +0x8c, default true (OBJECT_TYPES §4.6) */                                     \
    X(Bool, Shield_Requires_Base_Power, true)                                                                         \
    /* movement unit parameters (MOVEMENT, LOCOMOTOR): tactical/movement unit_params.cpp, ted_grid.cpp, fighter.cpp */ \
    X(Float, Max_Speed, 0.25f)                                                                                        \
    X(Float, Max_Rate_Of_Turn, 2.3f)                                                                                  \
    X(Float, OverrideAcceleration, 0.0f)                                                                              \
    X(Float, OverrideDeceleration, 0.0f)                                                                              \
    X(Float, Hover_Height, 0.0f)                                                                                      \
    X(Float, Overall_Length, 0.0f)                                                                                    \
    X(Float, Overall_Width, 0.0f)                                                                                     \
    X(Float, Close_Enough_Angle_For_Move_Start, 0.0f)                                                                 \
    X(Float, MaxFacingLookAheadFrames, 0.0f)                                                                          \
    X(Float, LateralAcceleration, 0.0f)                                                                               \
    X(Int, MovementClass, -1)                                                                                         \
    X(Int, UnitCollisionClass, -1)                                                                                    \
    X(Float, FormationSpacing, 0.0f)                                                                                  \
    X(Mask, CategoryMask, 0)                                                                                          \
    X(Int, FormationGrouping, 0)                                                                                      \
    X(Int, FormationOrder, 0)                                                                                         \
    X(Int, ContainerArrangement, 0)                                                                                   \
    X(Float, MovementPredictionInterval, 0.0f)                                                                        \
    X(Bool, Avoidance_Disabled, false)                                                                                \
    X(Mask, Space_Layer, 0)                                                                                           \
    X(Float, Layer_Z_Adjust, 0.0f)                                                                                    \
    X(Int, Stationary_Space_Layer, 0)                                                                                 \
    X(Float, Hover_Offset, 0.0f)                                                                                      \
    X(Float, Max_Lift, 0.8f)                                                                                          \
    X(Float, Max_Rate_Of_Roll, 2.0f)                                                                                  \
    X(Float, Bank_Turn_Angle, 70.0f)                                                                                  \
    X(Float, Min_Speed, 0.0f)                                                                                         \
    X(Float, Min_Speed_Fraction_For_Turn, 0.0f)                                                                       \
    X(Float, Strafe_Distance, 0.0f)                                                                                   \
    X(Float, Prepare_Strafe_Height, 0.0f)                                                                             \
    X(Bool, Has_Pre_Turn_Anim, false)                                                                                 \
    X(Bool, Uses_Multiple_Locomotors, false)                                                                          \
    X(Text, Secondary_Locomotor_Name, "")                                                                             \
    X(Text, Primary_Locomotor_Name, "")                                                                               \
    X(Float, Alternate_Max_Speed, 1.0f)                                                                               \
    X(Float, Alternate_Max_Rate_Of_Turn, 2.0f)                                                                        \
    X(Float, Deployment_Anim_Rate, 0.0f)                                                                              \
    X(Bool, Deploys, false)                                                                                           \
    X(Bool, Auto_Deploys, true)                                                                                       \
    X(Float, Max_Dist_Walk_While_Deployed, 0.0f)                                                                      \
    X(Float, Deployed_Walk_Speed, 0.0f)                                                                               \
    X(Bool, Walk_Transition, false)                                                                                   \
    X(Float, Movement_Animation_Speed, 0.25f)                                                                         \
    X(Float, Stopped_Rate_Of_Turn, 0.0f)                                                                              \
    X(Int, Movement_Directions_Count, 0)                                                                              \
    X(Float, Rotation_Animation_Speed, 0.0f)                                                                          \
    X(Bool, UseSecondaryFacing, false)                                                                                \
    X(Float, Turret_Rotate_Speed, 0.0f)                                                                               \
    X(Float, Turret_Rotate_Extent_Degrees, 0.0f)                                                                      \
    X(Float, Deployed_Turret_Rotate_Extent_Degrees, 0.0f)                                                             \
    X(Vec, Turret_Rest_Angle, 0)                                                                                      \
    X(Vec, Turret_Deployed_Rest_Angle, 0)                                                                             \
    X(Float, Turret_Rotate_To_Rest_Delay_Sec, 0.0f)                                                                   \
    X(Int, PathFrameDelay, 0)                                                                                         \
    X(Float, FormationRaggedness, 0.0f)                                                                               \
    X(Float, MaxJiggleDistance, 0.0f)                                                                                 \
    X(Bool, Is_Supercrusher, false)                                                                                   \
    X(Bool, Is_Squashable, false)                                                                                     \
    X(Bool, Is_Squashable_By_Supercrusher, false)                                                                     \
    X(Float, MovementBoxExpansionFactor, 0.0f)                                                                        \
    X(Float, Custom_Soft_Footprint_Radius, 0.0f)                                                                      \
    X(Ref, Obstacle_Proxy_Type, 0)                                                                                    \
    X(Float, Obstacle_Width, 0.0f)                                                                                    \
    X(Float, Obstacle_Height, 0.0f)                                                                                   \
    X(Float, Obstacle_X_Offset, 0.0f)                                                                                 \
    X(Float, Obstacle_Y_Offset, 0.0f)                                                                                 \
    X(Bool, Fine_Tune_Occupied_Passability, true)                                                                     \
    X(Mask, OverridePassability, 0)                                                                                   \
    X(Vec, Space_Obstacle_Offset, 0)                                                                                  \
    X(Float, Space_Obstacle_Radius, 0.0f)                                                                             \
    X(Bool, Is_Impassable_Asteroid, false)                                                                            \
    X(Float, Max_Thrust, 0.015f)                                                                                      \
    X(Float, Minimum_Follow_Distance, 0.0f)                                                                           \
    X(Float, Targeting_Max_Attack_Distance, 0.0f)                                                                     \
    X(Float, Squadron_Formation_Error_Tolerance, 0.0f)                                                                \
    X(Float, SecondaryTurnInPlaceROTCoefficient, 0.0f)                                                                \
    X(Float, SecondaryTurnAngle, 0.0f)                                                                                \
    X(Float, MinSecondaryTurnROTCoefficient, 0.0f)                                                                    \
    X(Float, MaxSecondaryTurnROTCoefficient, 0.0f)                                                                    \
    X(Float, SecondaryTurnLookaheadDistance, 0.0f)                                                                    \
    /* galactic rules (GALACTIC, STRATEGIC_GAPS): galactic_rules.cpp, special_rules.cpp, conflicts.cpp, bounty.cpp */ \
    X(List, Company_Transport_Unit, 0)                                                                                \
    X(List, Company_Units, 0)                                                                                         \
    X(Bool, Cinematic_Object_Only, false)                                                                             \
    X(Bool, Always_Spawn_In_Orbit, false)                                                                             \
    X(Bool, Is_Super_Weapon_Killer, false)                                                                            \
    X(Bool, Is_Named_Hero, false)                                                                                     \
    X(Bool, Is_Generic_Hero, false)                                                                                   \
    X(Bool, Build_Requires_Initial_Placement, false)                                                                  \
    X(List, Required_Special_Structures, 0)                                                                           \
    X(List, Required_Orbiting_Units, 0)                                                                               \
    X(List, Required_Planets, 0)                                                                                      \
    X(Bool, Hyperspace, false)                                                                                        \
    X(Int, Build_Cost_Credits, 0)                                                                                     \
    X(Int, Build_Time_Seconds, 0)                                                                                     \
    X(Int, Tech_Level, 0)                                                                                             \
    X(Int, Required_Ground_Base_Level, 0)                                                                             \
    X(Int, Required_Star_Base_Level, 0)                                                                               \
    X(Int, Base_Level, 0)                                                                                             \
    X(Text, Next_Level_Base, "")                                                                                      \
    X(Int, Build_Advances_Tech_Level, 0)                                                                              \
    X(Int, Squadron_Capacity, 0)                                                                                      \
    X(Float, Hyperspace_Speed, 0.0f)                                                                                  \
    X(Float, Galactic_Influence_Range, 0.0f)                                                                          \
    X(Float, Piracy_Value_Credits, 0.0f)                                                                              \
    X(Float, Maintenance_Cost, 0.0f)                                                                                  \
    X(Bool, Prevents_Story_Campaign_Autoresolve, false)                                                               \
    X(Float, Hyperspace_Fleet_Reveal_Range, 0.0f)                                                                     \
    X(Ref, Respawn_Tied_To_Structure, 0)                                                                              \
    X(Bool, Build_Countdown_Timer, false)                                                                             \
    X(Text, Build_Speech_Countdowns, "")                                                                              \
    X(Bool, Build_Time_Reduced_By_Multiple_Factories, false)                                                          \
    X(Bool, Build_Initially_Locked, false)                                                                            \
    X(Bool, Build_Can_Be_Unlocked_By_Slicer, false)                                                                   \
    X(List, Affiliation, 0)                                                                                           \
    X(Int, Planet_Credit_Value, 0)                                                                                    \
    X(Int, Planet_Destroyed_Credit_Value, 0)                                                                          \
    X(Int, Planet_Capture_Bonus_Reward, 0)                                                                            \
    X(Bool, Planet_Black_Market_Influences, false)                                                                    \
    X(Bool, Planet_Starbase_Stealth, false)                                                                           \
    X(Bool, Planet_Groundbase_Stealth, false)                                                                         \
    X(Bool, Planet_Surface_Accessible, true)                                                                          \
    X(Text, The_Force, "")                                                                                            \
    X(Float, Force_Strength, 0.0f)                                                                                    \
    X(Int, Political_Control, 0)                                                                                      \
    X(Text, Political_Faction, "")                                                                                    \
    X(List, Planet_Max_Political_Control_For_Faction, 0)                                                              \
    X(Int, Additional_Population_Capacity, 0)                                                                         \
    X(Text, Terrain, "")                                                                                              \
    X(Mask, Planet_Restricted_Unit_Categories, 0)                                                                     \
    X(List, Planet_Restricted_Unit_Types, 0)                                                                          \
    X(List, Squadron_Units, 0)                                                                                        \
    X(Int, Cheap_Bribe_Cost, 0)                                                                                       \
    X(Int, Expensive_Bribe_Cost, 0)                                                                                   \
    X(Int, Build_Max_Instances_Per_Planet, -1)                                                                        \
    X(Int, Build_Limit_Lifetime_Per_Player, -1)                                                                       \
    X(Int, Build_Limit_Current_Per_Player, -1)                                                                        \
    X(Int, Max_Space_Base, 0)                                                                                         \
    X(Int, Special_Structures_Land, 0)                                                                                \
    X(Int, Special_Structures_Space, 0)                                                                               \
    X(Sub, Abilities, 0)                                                                                              \
    X(Int, Sabotage_Cost_Credits, 0)                                                                                  \
    X(Int, Tutorial_Sabotage_Cost_Credits, 0)                                                                         \
    X(Bool, Planet_Vigilance, false)                                                                                  \
    X(Bool, Planet_Is_Mining_Colony, false)                                                                           \
    X(Text, Planet_Slice_Difficulty_Rating, "")                                                                       \
    X(Text, Planet_Tech_Availability_Rating, "")                                                                      \
    X(Text, Slice_Cost_Credits, "")                                                                                   \
    X(Text, Planet_Black_Market_Modifier, "")                                                                         \
    X(Bool, Destroy_When_Used_As_Corruption_Prereq, true)                                                             \
    X(Float, Planet_Corruption_Removal_Base_Cost, 0.0f)                                                               \
    X(Bool, Prevents_Minor_Credit_Siphons, false)                                                                     \
    X(Float, Neutralization_Cost, 0.0f)                                                                               \
    X(Bool, Can_Be_Neutralized_By_Minor_Heroes, false)                                                                \
    X(Bool, Can_Be_Neutralized_By_Major_Heroes, false)                                                                \
    X(Text, Prev_Level_Base, "")                                                                                      \
    X(List, Text_ID, 0)                                                                                               \
    X(List, Planet_Slice_Affinity_Types, 0)                                                                           \
    X(Int, Space_Override_Population_Value, -1)                                                                       \
    X(Bool, Respawn_Whole_Team_When_Killed, false)                                                                    \
    X(Text, Space_Tactical_Map, "")                                                                                   \
    X(Text, Land_Tactical_Map, "")                                                                                    \
    X(Float, Override_Bounty_SP, 0.0f)                                                                                \
    X(Float, Override_Bounty_MP, 0.0f)                                                                                \
    /* garrison and cash / capture points (GARRISON): tactical/garrison garrison_types.cpp, cash_point.cpp; movement special_locomotors.cpp */ \
    X(Mask, Property_Flags, 0)                                                                                        \
    X(Mask, Garrison_Category, 0)                                                                                     \
    X(Int, Num_Garrison_Slots, 0)                                                                                     \
    X(Float, Garrison_Enter_Dist, 0.0f)                                                                               \
    X(Float, Garrison_Exit_Dist, 0.0f)                                                                                \
    X(Float, Garrison_Radius, 0.0f)                                                                                   \
    X(Int, Garrison_Value, 0)                                                                                         \
    X(Float, Capture_Point_Radius, 0.0f)                                                                              \
    X(Float, Capture_Point_Transition_Time_Seconds, 0.0f)                                                             \
    X(Bool, Ownership_Sticks, false)                                                                                  \
    X(Bool, Influences_Capture_Point, true)                                                                           \
    X(Float, Cash_Point_Credits_Per_Second, 0.0f)                                                                     \
    X(Float, Cash_Point_Radius, 0.0f)                                                                                 \
    X(Float, Cash_Point_Transition_Time_Seconds, 0.0f)                                                                \
    X(List, Behavior, 0)                                                                                              \
    X(List, LandBehavior, 0)                                                                                          \
    X(List, SpaceBehavior, 0)                                                                                         \
    /* model and animation parameters (ANIMATION): anim type_params.cpp */                                            \
    X(Text, Galactic_Model_Name, "")                                                                                  \
    X(Text, Land_Model_Name, "")                                                                                      \
    X(Text, Land_Model_Anim_Override_Name, "")                                                                        \
    X(Text, Space_Model_Name, "")                                                                                     \
    X(Text, Space_Model_Anim_Override_Name, "")                                                                       \
    X(Bool, Loop_Idle_Anim_00, false)                                                                                 \
    X(Float, Idle_Anim_00_Rate_Mod, 0.0f)                                                                             \
    X(Bool, Attack_Animation_Is_Overlay, false)                                                                       \
    X(Text, Specific_Death_Anim_Type, "")                                                                             \
    X(Text, Specific_Death_Anim_Index, "")                                                                            \
    X(Bool, Remove_Upon_Death, false)                                                                                 \
    X(Bool, Spin_Away_On_Death, false)                                                                                \
    X(Text, Spin_Away_On_Death_Explosion, "")                                                                         \
    X(Bool, Allow_Idle_When_Moving, false)                                                                            \
    X(Float, Death_Persistence_Duration, 0.0f)                                                                        \
    X(Float, Death_Fade_Time, 0.0f)                                                                                   \
    X(Float, Max_Flinch_Blend_Seconds, 0.0f)                                                                          \
    X(Float, Max_Flinch_Hold_Seconds, 0.0f)                                                                           \
    X(Float, Idle_Flinch_Interval_Seconds, 0.0f)                                                                      \
    X(Float, Attack_Flinch_Interval_Seconds, 0.0f)                                                                    \
    X(Float, Spin_Away_On_Death_Chance, 0.0f)                                                                         \
    X(Float, Spin_Away_On_Death_Time, 0.0f)                                                                           \
    X(Float, Deployed_Walk_Anim_Speed, 0.0f)                                                                          \
    /* battle catalog unit / projectile / weapon descriptors (TACTICAL_COMBAT, OBJECT_TYPES): apps/sim battle_catalog.cpp */ \
    X(Text, Projectile_Object_Detonation_Particle, "")                                                                \
    X(Text, Projectile_Object_Armor_Reduced_Detonation_Particle, "")                                                  \
    X(Text, Projectile_Absorbed_By_Shields_Particle, "")                                                              \
    X(Text, Projectile_Lifetime_Detonation_Particle, "")                                                              \
    X(Text, Projectile_Ground_Detonation_Particle, "")                                                                \
    X(Float, Projectile_Damage, 0.0f)                                                                                 \
    X(Text, Damage_Type, "Damage_Default")                                                                            \
    X(Int, Projectile_Category, -1)                                                                                   \
    X(Bool, Causes_Infection, false)                                                                                  \
    X(Float, Projectile_Grenade_Gravity, 0.0f)                                                                        \
    X(Float, Projectile_Grenade_Gravity_Lob_Mod, 0.0f)                                                                \
    X(Bool, Projectile_Grenade_Can_Lob_Slower, false)                                                                 \
    X(Bool, Projectile_Grenade_Sticks_On_Collision, false)                                                            \
    X(Float, Friendly_Damage_Amount, 0.0f)                                                                            \
    X(Bool, Projectile_Does_Shield_Damage, true)                                                                      \
    X(Bool, Projectile_Does_Hitpoint_Damage, true)                                                                    \
    X(Float, Projectile_Max_Flight_Distance, 50.0f)                                                                   \
    X(Float, Projectile_Max_Lifetime, 0.0f)                                                                           \
    X(Float, Projectile_Blast_Area_Damage, 0.0f)                                                                      \
    X(Float, Projectile_Blast_Area_Range, 0.0f)                                                                       \
    X(Bool, Projectile_Blast_Area_Dropoff, false)                                                                     \
    X(Int, Projectile_Blast_Area_Dropoff_Tiers, 5)                                                                    \
    X(Float, Projectile_Bomb_Fall_Accel_Rate, 0.0f)                                                                   \
    X(Int, Projectile_Blast_Area_Max_Victims, 0)                                                                      \
    X(Float, Projectile_Redirect_Chance_Modifier, 0.0f)                                                               \
    X(Float, Projectile_Block_Chance_Modifier, 0.0f)                                                                  \
    X(Text, Projectile_Blast_Area_Immune_Faction, "")                                                                 \
    X(Bool, Projectile_Damages_Random_Hard_Points, false)                                                             \
    X(Float, Projectile_Rocket_Straight_Distance, 0.0f)                                                               \
    X(Float, Projectile_Rocket_Curve_Distance, 0.0f)                                                                  \
    X(Float, Projectile_Rocket_Curve_Offset, 0.0f)                                                                    \
    X(Bool, Projectile_Target_Point_On_Terrain, false)                                                                \
    X(Bool, Explode_When_Reached_Target_Radius, false)                                                                \
    X(Float, Projectile_Damage_Delay_Secs, 0.0f)                                                                      \
    X(Bool, Projectile_Ion_Stun_On_Detonation, false)                                                                 \
    X(Float, Projectile_Ion_Stun_Duration, 0.0f)                                                                      \
    X(Float, Projectile_Ion_Stun_Speed_Reduction_Percent, 0.0f)                                                       \
    X(Float, Projectile_Ion_Stun_Shot_Rate_Reduction_Percent, 0.0f)                                                   \
    X(Float, Projectile_Energy_Per_Shot, 0.0f)                                                                        \
    X(Bool, Projectile_Does_Energy_Damage, false)                                                                     \
    X(Bool, Projectile_Disables_Engines_When_Power_Drained, false)                                                    \
    X(Float, Projectile_Disable_Engines_Duration, 0.0f)                                                               \
    X(Bool, Projectile_Ion_Stun_Stack_Duration, false)                                                                \
    X(Float, Projectile_Ion_Stun_Radius, 0.0f)                                                                        \
    X(Bool, Projectile_Stun_On_Detonation, false)                                                                     \
    X(Int, Projectile_Stun_Duration_Frames, 0)                                                                        \
    X(Float, Projectile_Stun_Radius, 0.0f)                                                                            \
    X(Mask, Projectile_Stun_Victims_Category_Mask, 0)                                                                 \
    X(List, Projectile_Stun_Victims_Unit_Types, 0)                                                                    \
    X(Bool, Projectile_Distract_On_Detonation, false)                                                                 \
    X(Float, Projectile_Distract_Radius, 0.0f)                                                                        \
    X(Mask, Projectile_Distract_Victims_Category_Mask, 0)                                                             \
    X(Bool, Projectile_Instant_Heal_On_Detonation, false)                                                             \
    X(Float, Projectile_Instant_Heal_Radius, 0.0f)                                                                    \
    X(Mask, Projectile_Instant_Heal_Targets_Category_Mask, 0)                                                         \
    X(Int, Projectile_Instant_Heal_Health_Increase, 0)                                                                \
    X(Bool, Projectile_Convert_Enemy_On_Detonation, false)                                                            \
    X(Float, Projectile_Convert_Enemy_Radius, 0.0f)                                                                   \
    X(Mask, Projectile_Convert_Enemy_Targets_Category_Mask, 0)                                                        \
    X(Bool, Projectile_Cause_Invulnerability_On_Detonation, false)                                                    \
    X(Int, Projectile_Cause_Invulnerability_Duration_Frames, 0)                                                       \
    X(Float, Projectile_Cause_Invulnerability_Radius, 0.0f)                                                           \
    X(Int, Projectile_Cause_Invulnerability_Max_Targets, 0)                                                           \
    X(Mask, Projectile_Cause_Invulnerability_Targets_Category_Mask, 0)                                                \
    X(Bool, Projectile_Weaken_Enemy_On_Detonation, false)                                                             \
    X(Float, Projectile_Weaken_Enemy_Duration_Seconds, 0.0f)                                                          \
    X(Float, Projectile_Weaken_Enemy_Radius, 0.0f)                                                                    \
    X(Float, Projectile_Weaken_Enemy_Take_Damage_Increase_Percent, 0.0f)                                              \
    X(Float, Projectile_Weaken_Enemy_Cause_Damage_Reduction_Percent, 0.0f)                                            \
    X(Mask, Projectile_Weaken_Enemy_Targets_Category_Mask, 0)                                                         \
    X(Text, Projectile_Combat_Mod, "")                                                                                \
    X(Float, Projectile_Combat_Mod_Duration, 0.0f)                                                                    \
    X(Float, Projectile_Combat_Mod_Value, 0.0f)                                                                       \
    X(Mask, Projectile_Combat_Mod_Category_Mask, 0)                                                                   \
    X(Float, Shield_Refresh_Rate, 0.0f)                                                                               \
    X(Text, Armor_Type, "Armor_Default")                                                                              \
    X(Text, Shield_Armor_Type, "Shield_Default")                                                                      \
    X(Float, Deployed_Max_Attack_Distance_Multiplier, 0.0f)                                                           \
    X(Bool, Immune_To_Damage, false)                                                                                  \
    X(Float, Out_Of_Combat_Defense_Adjustment, 0.0f)                                                                  \
    X(Float, Targeting_Min_Attack_Distance, 0.0f)                                                                     \
    X(Float, Targeting_Fire_Inaccuracy_Fixed_Radius, 0.0f)                                                            \
    X(List, Targeting_Fire_Inaccuracy, 0)                                                                             \
    X(Text, Targeting_Priority_Set, "")                                                                               \
    X(Float, Targeting_Stickiness_Time_Threshold, 0.0f)                                                               \
    X(Bool, Should_Be_Destroyed_When_All_Hardpoints_Destroyed, true)                                                  \
    X(Mask, Attack_Category_Restrictions, 0)                                                                          \
    X(List, Projectile_Types, 0)                                                                                      \
    X(Float, Projectile_Fire_Recharge_Seconds, 0.0f)                                                                  \
    X(Int, Projectile_Fire_Pulse_Count, 1)                                                                            \
    X(Float, Projectile_Fire_Pulse_Delay_Seconds, 1.0f)                                                               \
    X(Bool, Can_Fighter_Fire_When_Idle, false)                                                                        \
    X(Int, Lobbing_Superweapon_Chargeup_Frames, 0)                                                                    \
    X(Float, Turret_Elevate_Extent_Degrees, 0.0f)                                                                     \
    X(Float, Deployed_Turret_Elevate_Extent_Degrees, 0.0f)                                                            \
    X(Bool, Turret_XY_Only, false)                                                                                    \
    X(Int, Num_Garrison_Guns, 0)                                                                                      \
    X(List, Land_Damage_Thresholds, 0)                                                                                \
    X(List, Land_Damage_Alternates, 0)                                                                                \
    X(Bool, Collidable_By_Projectile_Living, false)                                                                   \
    X(Bool, Is_Valid_Target, true)                                                                                    \
    X(Float, Surface_Type_Cover_Damage_Shield, 0.0f)                                                                  \
    X(Float, On_Fire_Speed_Modifier, 0.0f)                                                                            \
    X(Bool, Destroy_When_Stunned_Over_Water, false)                                                                   \
    X(List, SurfaceFX_Name, 0)                                                                                        \
    X(List, Damage_Hit_Particles, 0)                                                                                  \
    X(List, Shield_Hit_Particles, 0)                                                                                  \
    X(List, Death_Explosions, 0)                                                                                      \
    X(Text, Damaged_Smoke_Asset_Name, "")                                                                             \
    X(Bool, Target_Stealth_Units, false)                                                                              \
    X(Bool, Redirect_Damage_To_Teammates, false)                                                                      \
    X(Bool, Share_Damage_With_Teammates, false)                                                                       \
    X(Bool, Is_Combustible, false)                                                                                    \
    X(Float, Burning_Damage_Per_Second, 0.0f)                                                                         \
    X(Float, Energy_Capacity, 0.0f)                                                                                   \
    X(Float, Energy_Refresh_Rate, 0.0f)                                                                               \
    X(Bool, Requires_Base_Power, false)                                                                               \
    X(Float, Passive_Missile_Shield_Radius, 0.0f)                                                                     \
    X(Float, Converted_To_Enemy_Die_Time_Seconds, 0.0f)                                                               \
    X(List, Death_Clone, 0)                                                                                           \
    X(Bool, Must_Face_Target_To_Attack_When_Deployed, false)                                                          \
    X(Bool, Is_Stationary_When_Attacking, false)                                                                      \
    X(Bool, Shielded_Only_When_Deployed, false)                                                                       \
    X(Bool, Should_Attacker_Hold_Fire_For_Special_Ability, false)                                                     \
    X(Bool, Should_Cause_Limited_Turrets_To_Turn, true)                                                               \
    X(Bool, Fires_Forward, false)                                                                                     \
    X(List, Death_Projectiles, 0)                                                                                     \
    X(List, Destruction_Survivors, 0)                                                                                 \
    X(Int, Minimum_Destruction_Survivor_Count, 0)                                                                     \
    X(Int, Maximum_Destruction_Survivor_Count, 0)                                                                     \
    X(Float, Tactical_Respawn_Time_In_Secs, 0.0f)                                                                     \
    X(Float, Base_Shield_Radius, 0.0f)                                                                                \
    X(Bool, Base_Shield_Always_Off, false)                                                                            \
    X(Int, Tactical_Bribe_Cost, 0)                                                                                    \
    X(Text, Unique_Ground_Unit, "")                                                                                   \
    X(Text, Unique_Space_Unit, "")                                                                                    \
    X(Bool, Apply_Special_Ability_To_Self_On_Creation, false)                                                         \
    X(Float, Gravity_Control_Field_Range, 0.0f)                                                                       \
    X(Float, Gravity_Control_Field_Slow_Fraction, 0.0f)                                                               \
    X(Text, Gravity_Control_Field_Effect, "")                                                                         \
    X(Bool, Is_Affected_By_Gravity_Control_Field, false)                                                              \
    X(Mask, Special_Weapon_Valid_Targets, 0)                                                                          \
    X(Bool, Is_Force_Sensitive, false)                                                                                \
    X(Sub, Unit_Abilities_Data, 0)                                                                                    \
    X(Bool, Fire_Weapon_When_Deployed, true)                                                                          \
    X(Bool, Fire_Weapon_When_Undeployed, true)                                                                        \
    X(Bool, Fire_Weapon_When_In_Rocket_Attack_Mode, true)                                                             \
    X(Bool, Fire_Weapon_When_In_Normal_Attack_Mode, true)                                                             \
    X(Float, Bribe_Modifier_On_Corrupted_Planets, 0.0f)                                                               \
    X(Float, Bribe_Modifier_On_Uncorrupted_Planets, 0.0f)                                                             \
    /* battle catalog model / bone names: apps/sim battle_catalog.cpp */                                              \
    X(Text, Muzzle_Bone_Name, "")                                                                                     \
    X(List, Target_Bones, 0)                                                                                          \
    X(List, Garrison_Bone_Names, 0)                                                                                   \
    /* tactical battle (TACTICAL_*, HANGAR, FOG, L1): apps/sim tactical_battle*.cpp, game_modes.cpp */                \
    X(Bool, Is_Asteroid_Field, false)                                                                                 \
    X(Bool, Is_Ion_Storm, false)                                                                                      \
    X(Bool, Is_Nebula, false)                                                                                         \
    X(Bool, Create_Team, false)                                                                                       \
    X(Bool, Snap_Movement_Orders_To_Center, false)                                                                    \
    X(Text, Tactical_Buildable_Constructed, "")                                                                       \
    X(Int, Tactical_Build_Cost_Multiplayer, 0)                                                                        \
    X(Int, Tactical_Build_Cost_Campaign, -1)                                                                          \
    X(Float, Tactical_Sell_Percentage, 0.5f)                                                                          \
    X(Bool, Prevents_Blockade_Run_Attrition, false)                                                                   \
    X(Text, Create_Team_Type, "")                                                                                     \
    X(Int, Max_Squad_Size, 10)                                                                                        \
    X(List, Map_Load_Spawn_Table, 0)                                                                                  \
    X(List, Marker_For_Specific_Object_Type, 0)                                                                       \
    X(Bool, Is_Branched_Map_Discardable, true)                                                                        \
    X(Bool, Loads_When_Faction_Present, false)                                                                        \
    X(Bool, Is_Marker, false)                                                                                         \
    X(List, Space_Escort_Unit_Types, 0)                                                                               \
    X(Bool, Is_Decoration, false)                                                                                     \
    X(Text, Tactical_Additional_Structure_Type, "")                                                                   \
    X(Text, Place_Other_Type_At_Every_Specific_Marker_Position, "")                                                   \
    X(Text, Base_Position, "")                                                                                        \
    X(Float, Reinforcement_Prevention_Radius, 0.0f)                                                                   \
    X(Int, Ship_Class, -1)                                                                                            \
    X(List, Squadron_Offsets, 0)                                                                                      \
    X(Text, Tactical_Build_Attachment_Bone_Name, "")                                                                  \
    X(Bool, Respects_Skirmish_Pre_Build_Bases_Option, true)                                                           \
    X(Mask, SecondaryOccupationPassability, 0)                                                                        \
    X(Bool, Modifies_Reveal_Range, false)                                                                             \
    X(Float, Reveal_Range_Modifier, 0.0f)                                                                             \
    X(Text, Weather_Category, "")                                                                                     \
    X(Float, Dense_FOW_Reveal_Range_Multiplier, 0.5f)                                                                 \
    X(Float, Custom_Hard_XExtent_Offset, 0.0f)                                                                        \
    X(Float, Custom_Hard_YExtent_Offset, 0.0f)                                                                        \
    X(Bool, Reveal_During_Setup_Phase_Only, false)                                                                    \
    X(Bool, Multisample_FOW_Check, false)                                                                             \
    X(Bool, Reveal_During_Setup_Phase, false)                                                                         \
    X(Bool, Reveal_For_Attacker, false)                                                                               \
    X(Bool, Reveal_For_Defender, false)                                                                               \
    X(Bool, Influences_Cash_Point, true)                                                                              \
    X(List, Faction_Anim_Subindex, 0)                                                                                 \
    X(Text, Spawned_Squadron_Delay_Seconds, "")                                                                       \
    X(Bool, Is_Community_Property, false)                                                                             \
    X(List, Spawned_Squadron_Location_Bones, 0)                                                                       \
    X(List, Spawned_Squadron_Location_Flyout_Distances, 0)                                                            \
    X(Bool, Spawn_Garrison_On_Load, false)                                                                            \
    X(Float, Holster_Transition_Time_In_Secs, 0.0f)                                                                   \
    X(Float, Holster_Minimum_Drawn_Time_In_Secs, 0.0f)                                                                \
    X(Bool, Holster_Disable_Engine_Loops, false)                                                                      \
    X(Text, Holster_Weapon_Bone_Name, "")                                                                             \
    X(Bool, Is_Skirmish_Tactical_Super_Weapon, false)                                                                 \
    X(List, Tactical_Build_Prerequisites, 0)                                                                          \
    X(Int, Build_Limit_Lifetime_For_All_Allies, -1)                                                                   \
    X(Int, Build_Limit_Current_For_All_Allies, -1)                                                                    \
    X(Int, Tactical_Build_Time_Seconds, 0)                                                                            \
    X(Bool, Tactical_Destruction_Decrements_Tech_Level, false)                                                        \
    X(Bool, Tactical_Build_Increments_Tech_Level, false)                                                              \
    X(Int, Tactical_Production_Queue, -1)                                                                             \
    X(Text, Next_Upgrade_Level_Type, "")                                                                              \
    X(Text, Previous_Upgrade_Level_Type, "")                                                                          \
    X(Bool, Locomotor_Has_Animation_Priority, false)                                                                  \
    X(Bool, Is_Special_Weapon_In_Space, false)                                                                        \
    X(Int, Special_Weapon_Index, 0)                                                                                   \
    X(Ref, Land_Bomber_Type, 0)                                                                                       \
    X(List, Asteroid_Damage_Hit_Particles, 0)                                                                         \
    X(Int, Special_Weapon_Target_Action_Index, 0)                                                                     \
    X(Float, TSW_Start_Distance_From_Target, 0.0f)                                                                    \
    X(Float, TSW_Attack_Distance_From_Target, 0.0f)                                                                   \
    X(Bool, TSW_Start_Pos_Match_Targets_Z, true)                                                                      \
    X(Float, TSW_Z_Adjust_Relative_To_Target_Pos, 0.0f)                                                               \
    X(Float, TSW_Power_Up_Countdown_Seconds, 0.0f)                                                                    \
    X(Float, TSW_Attack_Anim_Duration_Seconds, 0.0f)                                                                  \
    X(Int, TSW_Explosion_Debris_Creation_Frame_Delay, 0)                                                              \
    X(Int, TSW_Post_Destruction_Wait_Frames, 0)                                                                       \
    X(Bool, Planet_Is_Destroyable_By_TSW, true)                                                                       \
    X(Float, Disable_Force_Abilities_Range, 0.0f)                                                                     \
    X(List, Death_By_TSW_Replacements, 0)                                                                             \
    X(Float, Bombing_Run_Prevention_Radius, 0.0f)                                                                     \
    X(Text, Vehicle_Thief_Inside_Clone, "")                                                                           \
    X(List, DeployedBehavior, 0)                                                                                      \
    X(Text, Shield_On_Anim, "")                                                                                       \
    X(Text, Shield_Off_Anim, "")                                                                                      \
    X(Bool, Tactically_Built_Child_Object_Persists, true)                                                             \
    X(Bool, Destroy_When_Child_Dies, false)                                                                           \
    X(Float, Minimum_Time_Before_Pad_Can_Build_Again, 0.0f)                                                           \
    X(Bool, Stay_In_Transport_During_Ground_Battle, false)                                                            \
    X(Text, Landing_Transport_Variant, "")                                                                            \
    X(Float, Reinforcement_Enabling_Radius, 0.0f)                                                                     \
    X(Bool, Victory_Relevant, false)                                                                                  \
    X(Bool, Disregard_Space_Layer_For_Guard_Attack_Move, false)                                                       \
    X(Float, Presence_Range, 0.0f)                                                                                    \
    X(List, Presence_Induced_Animations, 0)                                                                           \
    X(Int, Particle_Lifetime_Frames, 0)                                                                               \
    X(Bool, IsDeathCloneObstacle, false)                                                                              \
    X(Int, Squash_Damage_Type, 0)                                                                                     \
    X(Int, Tactical_Health, 100)                                                                                      \
    X(List, Spawned_Indigenous_Units_Type, 0)                                                                         \
    X(Text, Spawned_Indigenous_Pack_Type, "")                                                                         \
    X(Int, Debris_Min_Lifetime_Seconds, 0)                                                                            \
    X(Int, Debris_Max_Lifetime_Seconds, 0)                                                                            \
    X(Text, Debris_Attached_Particle, "")                                                                             \
    X(Vec, Debris_Movement_Vector, 0)                                                                                 \
    X(Vec, Debris_Facing_Rotate_Vector, 0)                                                                            \
    X(Float, Spawn_Indigenous_Units_Chance, 0.0f)                                                                     \
    X(Int, Spawned_Indigenous_Units_Quantity, 0)                                                                      \
    X(Bool, Spawn_Indigenous_Units_In_Packs, false)                                                                   \
    X(Float, Spawned_Indigenous_Units_Delay_Seconds, 0.0f)                                                            \
    X(Bool, Is_Indigenous_Unit_Corruptible, true)                                                                     \
    X(Float, Spawn_Indigenous_Units_Radius, 0.0f)                                                                     \
    X(Bool, Control_Point_Domination_Condition_Relevant, false)                                                       \
    X(Bool, HQ_Win_Condition_Relevant, false)                                                                         \
    X(Float, Collision_Box_Modifier, 0.0f)                                                                            \
    X(Bool, Collidable_By_Projectile_Dead, false)                                                                     \
    X(Bool, Death_Clone_Is_Obstacle, true)                                                                            \
    X(Bool, Space_Victory_Relevant, false)                                                                            \
    X(Bool, Land_Victory_Relevant, false)                                                                             \
    X(List, Tactical_Buildable_Objects_Multiplayer, 0)                                                                \
    X(List, Tactical_Buildable_Objects_Campaign, 0)                                                                   \
    /* hangar spawn slots (HANGAR; tactical/hangar kTechSlots = 6) */                                                 \
    X(List, Starting_Spawned_Units_Tech_0, 0)                                                                         \
    X(List, Starting_Spawned_Units_Tech_1, 0)                                                                         \
    X(List, Starting_Spawned_Units_Tech_2, 0)                                                                         \
    X(List, Starting_Spawned_Units_Tech_3, 0)                                                                         \
    X(List, Starting_Spawned_Units_Tech_4, 0)                                                                         \
    X(List, Starting_Spawned_Units_Tech_5, 0)                                                                         \
    X(List, Reserve_Spawned_Units_Tech_0, 0)                                                                          \
    X(List, Reserve_Spawned_Units_Tech_1, 0)                                                                          \
    X(List, Reserve_Spawned_Units_Tech_2, 0)                                                                          \
    X(List, Reserve_Spawned_Units_Tech_3, 0)                                                                          \
    X(List, Reserve_Spawned_Units_Tech_4, 0)                                                                          \
    X(List, Reserve_Spawned_Units_Tech_5, 0)
// clang-format on

namespace tags {
#define ALAMO_TYPE_TAG_DECL_Float(name, def) inline constexpr FloatTag name{#name, def};
#define ALAMO_TYPE_TAG_DECL_Int(name, def) inline constexpr IntTag name{#name, def};
#define ALAMO_TYPE_TAG_DECL_Bool(name, def) inline constexpr BoolTag name{#name, def};
#define ALAMO_TYPE_TAG_DECL_Text(name, def) inline constexpr TextTag name{#name, def};
#define ALAMO_TYPE_TAG_DECL_Vec(name, def) inline constexpr VecTag name{#name};
#define ALAMO_TYPE_TAG_DECL_List(name, def) inline constexpr ListTag name{#name};
#define ALAMO_TYPE_TAG_DECL_Ref(name, def) inline constexpr RefTag name{#name};
#define ALAMO_TYPE_TAG_DECL_Mask(name, def) inline constexpr MaskTag name{#name};
#define ALAMO_TYPE_TAG_DECL_Sub(name, def) inline constexpr SubObjectsTag name{#name};
#define ALAMO_TYPE_TAG_DECL(kind, name, def) ALAMO_TYPE_TAG_DECL_##kind(name, def)
ALAMO_TYPE_TAGS(ALAMO_TYPE_TAG_DECL)
#undef ALAMO_TYPE_TAG_DECL
}  // namespace tags

}  // namespace alamo::objects
