#include "alamo/objects/type_fields.hpp"

#include <string>
#include <unordered_map>

#include "alamo/core/hash.hpp"
#include "alamo/data/values.hpp"
#include "alamo/data/xml_values.hpp"
#include "alamo/objects/field_schema.hpp"

namespace alamo::objects {

namespace {

bool is_type_segment(const FieldSchema& r) {
    return r.table == 0 && (r.owner == "GameObjectType" || r.owner == "SpaceProp" || r.owner == "HeroUnit");
}

struct FieldIndexTable {
    std::vector<TypeField> fields;
    std::unordered_map<std::string, uint32_t> by_tag;  // lookup only
    FieldIndexTable() {
        const auto rows = field_schema();
        for (uint32_t i = 0; i < static_cast<uint32_t>(rows.size()); ++i) {
            if (!is_type_segment(rows[i])) continue;
            const auto [it, inserted] = by_tag.emplace(normalize_name(rows[i].tag), static_cast<uint32_t>(fields.size()));
            (void)it;
            if (!inserted) continue;  // a tag listed twice keeps its first row
            fields.push_back({rows[i].tag, rows[i].code, i, conv_of_code(rows[i].code), merge_of_code(rows[i].code)});
        }
    }
};

const FieldIndexTable& table() {
    static const FieldIndexTable t;
    return t;
}

}  // namespace

Conv conv_of_code(uint8_t code) {
    switch (code) {
        case 0x0: return Conv::Bool;
        case 0x1: case 0x5: case 0x6: return Conv::Int32;
        case 0x2: case 0x3: return Conv::Int8;
        case 0x4: case 0x7: return Conv::UIntClamp100;
        case 0x8: return Conv::Float;
        case 0x9: return Conv::Float01;
        case 0xa: return Conv::Double;
        case 0xb: return Conv::Double01;
        case 0xf: return Conv::Float2;
        case 0x10: return Conv::Vec3;
        case 0x11: return Conv::Float4;
        case 0x16: return Conv::Rgba;
        case 0x1c: return Conv::StringUpper;
        case 0xe: case 0x32: case 0x43: case 0x4a: case 0x52: return Conv::Enum;
        case 0x54: case 0x56: return Conv::Mask;
        case 0x1d: return Conv::TypeRef;
        case 0x38: case 0x39: return Conv::SubObjects;
        default: break;
    }
    return merge_of_code(code) == Merge::Scalar ? Conv::String : Conv::Tokens;
}

Merge merge_of_code(uint8_t code) {
    switch (code) {
        // §3.3 clearing list kinds; 0x42 (SFXEvent_Hardpoint_Destroyed: "only the last line is
        // effective" in variants, §4.4).
        case 0x12: case 0x13: case 0x14: case 0x15: case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x2a:
        case 0x2b: case 0x2d: case 0x34: case 0x35: case 0x37: case 0x38: case 0x39: case 0x3a: case 0x3d:
        case 0x44: case 0x45: case 0x48: case 0x4f: case 0x51: case 0x55: case 0x42:
            return Merge::ClearList;
        case 0x1e: case 0x3b: return Merge::TypeRefList;
        // 0x22 / 0x3c (§9.6): one pair per occurrence, never cleared (variants append to the copy).
        case 0x46: case 0x47: case 0x53: case 0x22: case 0x3c: return Merge::AppendList;
        default: return Merge::Scalar;
    }
}

std::span<const TypeField> type_fields() { return table().fields; }

uint32_t find_type_field(std::string_view tag) {
    const auto& m = table().by_tag;
    const auto it = m.find(normalize_name(tag));
    return it == m.end() ? 0xFFFFFFFFu : it->second;
}

int32_t c_atoi(std::string_view s) { return data::xml_values::c_atoi(s); }

double c_atof(std::string_view s) { return data::xml_values::c_atof(s); }

bool parse_type_bool(std::string_view s) { return data::parse_type_bool(s); }

std::vector<std::string_view> split_type_list(std::string_view s, bool keep_bar, bool split_bar) {
    return data::xml_values::split_list(s, keep_bar, split_bar);
}

}  // namespace alamo::objects
