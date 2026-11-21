// mcx1 t02: retail XML value semantics, one module for every data loader (docs/MOD_COMPAT_DESIGN.md §7.1).
//
// Retail converts a tag's text by the field's type code (docs/re/OBJECT_TYPES.md §4.5, the dispatcher every
// descriptor-table class shares: docs/re/MCX1_RE_ANSWERS.md Q1). The XML reader has already trimmed the value (§4.3);
// these functions trim too, so a caller may pass raw element text. Each function is the rule for one value kind:
//
//   skipped     exactly "TBD", or empty after trimming: the field keeps its previous value (§4.4, §6.3)
//   to_bool     code 0x0: true iff the value is "1" or starts with Y / y / T / t ("10", "On", "No" are false)
//   to_int      codes 0x1 / 0x5 / 0x6: C atoi - optional sign, the digit prefix, rest ignored, no digits -> 0,
//               out of range clamped to the int32 limits (MCX1_RE_ANSWERS Q3)
//   to_float    code 0x8: C atof - the numeric prefix, rest ignored, no number -> 0
//   to_list     list codes: tokens split on space, comma, tab, CR, LF (consecutive delimiters collapse)
//   to_mask     mask codes 0x54 / 0x56: tokens split on the list delimiters and '|'
//
// Repeated tags in one entry: scalars take the last occurrence, lists append every occurrence (§4.4).
// last_child() is the scalar read; for lists iterate XmlNode::children(tag).
#pragma once
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "alamo/data/xml.hpp"

namespace alamo::data::xml_values {

// The C runtime conversions retail's converters call (MSVC CRT semantics, locale-free): leading C whitespace, sign,
// digits, the rest ignored, garbage -> 0. c_atoi clamps to the int32 range on overflow (strtol); c_atof accepts decimal
// with exponent, "inf" / "infinity" / "nan" and hex ("0x1.8p3"); overflow -> +-inf, underflow -> +-0. No trimming
// beyond the C whitespace skip. (Moved here from objects::c_atoi / c_atof, which forward.)
int32_t c_atoi(std::string_view s);
double c_atof(std::string_view s);
// The list tokeniser of OBJECT_TYPES.md §4.5: delimiters space, comma, tab, CR, LF (collapsed). keep_bar: '|' is a token
// of its own (code 0x53 OR groups); split_bar: '|' is a delimiter (masks). Views point into `s`.
std::vector<std::string_view> split_list(std::string_view s, bool keep_bar = false, bool split_bar = false);

bool skipped(std::string_view v);
bool to_bool(std::string_view v);
int32_t to_int(std::string_view v);
float to_float(std::string_view v);
std::vector<std::string_view> to_list(std::string_view v);
std::vector<std::string_view> to_mask(std::string_view v);

// The last child element named `tag` (case-insensitive), or a null node: retail's "last occurrence wins" for a scalar.
XmlNode last_child(XmlNode e, std::string_view tag);

// How one class's entry parse walks its element (docs/re/MCX1_RE_ANSWERS.md Q1):
// - kData: Faction, Campaign, GameConstants, TradeRoute, HardPoint: every child, a child with element children is
//   descended into at any depth and its children are fields of the same entry; `TBD` skipped.
// - kObject: GameObjectType: as kData, except that a container marked SubObjectList="Yes" (exactly) holds sub-objects, not
//   fields, and is not descended into (OBJECT_TYPES.md §4.2; Q1: the other data classes have no such branch).
// - kAbility: the `*_Ability` and `Unit_Ability` sub-objects: direct children only, `TBD` is a value like any other.
// In all of them an empty value never changes a field.
struct EntryRule {
    bool descend = true;
    bool skip_tbd = true;
    bool sub_object_lists = false;
};
inline constexpr EntryRule kData{true, true, false};
inline constexpr EntryRule kObject{true, true, true};
inline constexpr EntryRule kAbility{false, false, false};

// Scalar reads of a field of entry `e` with retail's rules: the last occurrence that changes the field decides; a missing
// tag, or only empty / skipped occurrences, gives `fallback` (the field keeps its default).
bool bool_or(XmlNode e, std::string_view tag, bool fallback, EntryRule rule = kData);
int32_t int_or(XmlNode e, std::string_view tag, int32_t fallback, EntryRule rule = kData);
float float_or(XmlNode e, std::string_view tag, float fallback, EntryRule rule = kData);
// The trimmed text of that last occurrence, or empty.
std::string_view text(XmlNode e, std::string_view tag, EntryRule rule = kData);
// Every occurrence that changes the field, in document order (a list field appends each one).
std::vector<std::string_view> texts(XmlNode e, std::string_view tag, EntryRule rule = kData);
// to_list() of every occurrence, appended.
std::vector<std::string_view> list(XmlNode e, std::string_view tag, EntryRule rule = kData);
// Every field of the entry in document order as (tag, trimmed value), with the rule's walk; occurrences that do not change a
// field (empty, or `TBD` under kData) are left out.
std::vector<std::pair<std::string_view, std::string_view>> fields(XmlNode e, EntryRule rule = kData);

// The same rules on a value already taken from a table (GameConstants keeps tag / value pairs).
inline bool bool_or(std::string_view v, bool fallback) { return skipped(v) ? fallback : to_bool(v); }
inline int32_t int_or(std::string_view v, int32_t fallback) { return skipped(v) ? fallback : to_int(v); }
inline float float_or(std::string_view v, float fallback) { return skipped(v) ? fallback : to_float(v); }

}  // namespace alamo::data::xml_values
