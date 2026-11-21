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
    RetailUnused,  // nothing reads it, and an RE answer (spec) found no reader in retail either (roadmap mcx4)
    OutOfScope,    // nothing reads it on purpose: a scope reason (spec) of docs/DECISIONS.md "Out-of-scope tag-ledger rows"
    Misfiled,      // a segment error of xml_field_tables.md: the field belongs to another class the port reads (spec:
                   // docs/re/XML_FIELD_TABLE_SEGMENTS.md and the reader)
};
// "unread", "stored", "consumed", "verified", "retail_unused", "out_of_scope", "misfiled"
const char* to_string(LedgerState s);
// A tag the port does not act on although retail may: Unread or Stored (not RetailUnused: retail ignores it too; not
// OutOfScope: the port leaves it out on purpose, mcx4 t16; not Misfiled: the real owner's field is read).
inline bool is_honour_gap(LedgerState s) { return s == LedgerState::Unread || s == LedgerState::Stored; }

struct TagLedgerRow {
    std::string_view cls;       // descriptor segment owner: "GameObjectType", "GameConstants", "Faction", ...
    std::string_view tag;       // as written in docs/re/xml_field_tables.md
    uint8_t code = 0;           // parser type code
    LedgerState state = LedgerState::Unread;
    // No consumer column (roadmap ref1 t01): which files read a tag is in docs/status/tag-ledger.md, so moving a read
    // between files does not change this table.
    std::string_view spec;      // a docs/re page naming the tag ("" when none; sticky: the one already cited while it
                                // still names the tag); RetailUnused: the RE answer; OutOfScope: the DECISIONS.md
                                // record and reason; Misfiled: the segment page, the real owner and its reader
    uint32_t base_uses = 0;     // elements of that name in base FoC's XML (any class)
    uint8_t tier = 1;           // parity tier a gap here belongs to
};

// Every row, in descriptor-table order.
std::span<const TagLedgerRow> tag_ledger();
// The row for (class, tag), both ASCII case-insensitive; nullptr when the class's table has no such tag.
const TagLedgerRow* find_tag_ledger(std::string_view cls, std::string_view tag);

}  // namespace alamo::objects
