// Tag ledger (docs/MOD_COMPAT_DESIGN.md §7.2, roadmap mcx0 t02): one row per (class, tag) of retail's XML descriptor
// tables with the port's state for it. Generated into engine/objects/generated/tag_ledger.inc by
// engine/objects/tools/gen_tag_ledger.py (human view docs/status/tag-ledger.md); tools/lint/check_tag_ledger.py keeps it
// in step with the code. Read by the load report (alamo_mod_check v4 honour_gaps); never by sim logic.
#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace alamo::objects {

enum class LedgerState : uint8_t {
    Unread,    // the port's loader does not keep the tag
    Stored,    // kept (typed view, sub-object fields, GameConstants, campaign raw list), nothing reads it
    Consumed,  // engine code reads it
    Verified,  // consumed, and a test checks its effect against a docs/re spec or probe
};
const char* to_string(LedgerState s);  // "unread", "stored", "consumed", "verified"
// A tag the port does not act on: Unread or Stored.
inline bool is_honour_gap(LedgerState s) { return s == LedgerState::Unread || s == LedgerState::Stored; }

struct TagLedgerRow {
    std::string_view cls;       // descriptor segment owner: "GameObjectType", "GameConstants", "Faction", ...
    std::string_view tag;       // as written in docs/re/xml_field_tables.md
    uint8_t code = 0;           // parser type code
    LedgerState state = LedgerState::Unread;
    uint32_t consumers = 0;     // engine source files that read it
    std::string_view consumer;  // the first of them ("" when none)
    std::string_view spec;      // the first docs/re page naming the tag ("" when none)
    uint32_t base_uses = 0;     // elements of that name in base FoC's XML (any class)
    uint8_t tier = 1;           // parity tier a gap here belongs to
};

// Every row, in descriptor-table order.
std::span<const TagLedgerRow> tag_ledger();
// The row for (class, tag), both ASCII case-insensitive; nullptr when the class's table has no such tag.
const TagLedgerRow* find_tag_ledger(std::string_view cls, std::string_view tag);

}  // namespace alamo::objects
