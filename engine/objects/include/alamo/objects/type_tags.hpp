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
    X(Float, SecondaryTurnLookaheadDistance, 0.0f)
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
