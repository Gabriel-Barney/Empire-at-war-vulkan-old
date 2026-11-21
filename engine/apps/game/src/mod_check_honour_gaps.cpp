// Mod check, honour-gap family (mod_check_detail.hpp, report v4, docs/MOD_COMPAT_DESIGN.md §12.1): tags of retail's
// descriptor tables the data sets to a non-default value that the tag ledger says the port does not act on.
#include "mod_check_detail.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "alamo/core/ascii.hpp"
#include "alamo/data/xml.hpp"
#include "alamo/objects/field_schema.hpp"
#include "alamo/objects/tag_ledger.hpp"
#include "alamo/objects/type_fields.hpp"

namespace alamo::game::mod_check_detail {

namespace {

// True when `text` leaves the field at `def` (the class default; "" = zero / false / empty): empty, TBD, or the same
// value after the type code's conversion (objects/type_fields.hpp). A tag set to its default does not need honouring.
bool default_value(std::string_view text, uint8_t code, std::string_view def) {
    text = ascii_trim(text);
    if (text.empty() || iequals(text, "TBD")) return true;
    if (objects::kind_of_code(code) == objects::FieldKind::Bool)  // 0x0 and the flag-bit booleans 0x50
        return objects::parse_type_bool(text) == (!def.empty() && objects::parse_type_bool(def));
    switch (objects::conv_of_code(code)) {
        case objects::Conv::Bool: return objects::parse_type_bool(text) == (!def.empty() && objects::parse_type_bool(def));
        case objects::Conv::Int32:
        case objects::Conv::Int8:
        case objects::Conv::UIntClamp100: return objects::c_atoi(text) == objects::c_atoi(def.empty() ? "0" : def);
        case objects::Conv::Float:
        case objects::Conv::Float01:
        case objects::Conv::Double:
        case objects::Conv::Double01: return objects::c_atof(text) == objects::c_atof(def.empty() ? "0" : def);
        default: return !def.empty() && iequals(text, def);
    }
}

struct GapAcc {
    HonourGap g;
    std::set<std::string> entries;
};
using GapMap = std::map<std::pair<std::string, std::string>, GapAcc>;  // (class, tag lower): sorted, deterministic

void note_gap(GapMap& m, const objects::TagLedgerRow& row, const std::string& entry, const std::string& file, bool in_mod) {
    GapAcc& a = m[{std::string(row.cls), ascii_lower(row.tag)}];
    if (a.g.uses++ == 0) {
        a.g.cls = std::string(row.cls);
        a.g.tag = std::string(row.tag);
        a.g.state = objects::to_string(row.state);
        a.g.first_file = file;
        a.g.tier = row.tier;
        a.g.base_uses = row.base_uses;
        a.g.spec = std::string(row.spec);
    }
    if (in_mod && !a.g.in_mod) a.g.first_file = file;  // a mod's finding cites the mod's file
    a.g.in_mod = a.g.in_mod || in_mod;
    a.entries.insert(entry);
}

bool object_type_owner(std::string_view owner) {
    return owner == "GameObjectType" || owner == "SpaceProp" || owner == "HeroUnit";
}

void honour_gaps_object_types(const GameData& d, GapMap& m) {
    const objects::ObjectTypeDatabase& db = d.raw;
    const objects::FieldColumns& f = db.fields();
    const auto& files = db.files();
    std::map<std::string, std::string_view, std::less<>> defaults;  // lower tag -> constructor default text
    for (const auto& [tag, value] : objects::type_field_defaults()) defaults.emplace(ascii_lower(tag), value);
    const auto rows = objects::field_schema();
    std::vector<const objects::TagLedgerRow*> ledger(rows.size(), nullptr);
    std::vector<uint8_t> looked(rows.size(), 0);
    std::vector<int8_t> file_in_mod(files.size(), -1);
    for (size_t i = 0; i < f.size(); ++i) {
        const uint32_t sr = f.schema[i];
        if (sr >= rows.size()) continue;
        if (!looked[sr]) {
            looked[sr] = 1;
            const objects::TagLedgerRow* row = objects::find_tag_ledger(rows[sr].owner, rows[sr].tag);
            ledger[sr] = row && objects::is_honour_gap(row->state) ? row : nullptr;
        }
        const objects::TagLedgerRow* row = ledger[sr];
        if (!row) continue;
        std::string_view def;
        if (object_type_owner(rows[sr].owner)) {
            const auto it = defaults.find(ascii_lower(rows[sr].tag));
            if (it != defaults.end()) def = it->second;
        }
        if (default_value(f.text[i], rows[sr].code, def)) continue;
        const objects::TypeIndex t = f.type[i];
        const uint32_t fi = db.types().file[t];
        if (fi >= files.size()) continue;
        if (file_in_mod[fi] < 0) file_in_mod[fi] = static_cast<int8_t>(d.vfs && in_mod_layer(*d.vfs, files[fi].path) ? 1 : 0);
        note_gap(m, *row, "type#" + std::to_string(t), files[fi].path, file_in_mod[fi] != 0);
    }
}

void honour_gaps_databases(const GameData& d, GapMap& m) {
    std::vector<uint8_t> bytes;
    for (const XmlDatabase& db : xml_databases()) {
        for (const std::string& path : database_files(d, db)) {
            if (!d.files->read(path, bytes)) continue;
            const data::XmlDocument doc = data::XmlDocument::parse(bytes, path);
            if (!doc.ok()) continue;
            const bool in_mod = d.vfs && in_mod_layer(*d.vfs, path);
            for (const auto& [e, c] : checked_elements(doc, db)) {
                for (const std::string_view owner : db.owners) {
                    const objects::TagLedgerRow* row = objects::find_tag_ledger(owner, c.name());
                    if (!row) continue;  // the first owner that lists the tag decides
                    if (objects::is_honour_gap(row->state) && !default_value(c.text_trimmed(), row->code, {}))
                        note_gap(m, *row, path + "#" + std::to_string(e.index()), path, in_mod);
                    break;
                }
            }
        }
    }
}

}  // namespace

void check_honour_gaps(const GameData& d, ModCheckReport& r) {
    GapMap m;
    honour_gaps_object_types(d, m);
    if (d.files) honour_gaps_databases(d, m);
    for (auto& [key, a] : m) {
        a.g.entries = static_cast<uint32_t>(a.entries.size());
        r.honour_gaps.push_back(std::move(a.g));
    }
    std::stable_sort(r.honour_gaps.begin(), r.honour_gaps.end(), [](const HonourGap& a, const HonourGap& b) {
        if (a.uses != b.uses) return a.uses > b.uses;
        if (a.cls != b.cls) return a.cls < b.cls;
        return a.tag < b.tag;
    });
}

}  // namespace alamo::game::mod_check_detail
