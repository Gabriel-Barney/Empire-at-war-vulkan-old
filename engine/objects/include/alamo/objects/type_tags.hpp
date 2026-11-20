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
using IntTag = TypeTag<int32_t>;
using BoolTag = TypeTag<bool>;
using TextTag = TypeTag<std::string_view>;  // the value as written (trimmed), "" unset
struct SubObjectsTag {
    std::string_view tag;
};

// X(kind, Name, default): kind Float / Int / Bool / Text (default ignored for Text: "").
// clang-format off
#define ALAMO_TYPE_TAGS(X)                                                                                            \
    /* chase ranges (FIGHTER_FLYING §4.10 A; BKR1 Q8): tactical_battle_chase.cpp */                                  \
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
    X(Bool, Shield_Requires_Base_Power, true)
// clang-format on

namespace tags {
#define ALAMO_TYPE_TAG_DECL_Float(name, def) inline constexpr FloatTag name{#name, def};
#define ALAMO_TYPE_TAG_DECL_Int(name, def) inline constexpr IntTag name{#name, def};
#define ALAMO_TYPE_TAG_DECL_Bool(name, def) inline constexpr BoolTag name{#name, def};
#define ALAMO_TYPE_TAG_DECL_Text(name, def) inline constexpr TextTag name{#name, {}};
#define ALAMO_TYPE_TAG_DECL(kind, name, def) ALAMO_TYPE_TAG_DECL_##kind(name, def)
ALAMO_TYPE_TAGS(ALAMO_TYPE_TAG_DECL)
#undef ALAMO_TYPE_TAG_DECL
}  // namespace tags

}  // namespace alamo::objects
