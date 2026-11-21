// mcx0 t02: the compiled tag ledger (objects/tag_ledger.hpp) - lookup, states, and agreement with field_schema().
#include <string_view>

#include "alamo/objects/field_schema.hpp"
#include "alamo/objects/tag_ledger.hpp"
#include "alamo/test/support.hpp"

using namespace alamo::objects;
using ::alamo::test::g_fail;

int main() {
    const auto rows = tag_ledger();
    CHECK(rows.size() > 2000);
    // Every ledger row is a descriptor row of its class.
    size_t missing = 0;
    for (const TagLedgerRow& r : rows) {
        bool found = false;
        for (const FieldSchema& f : field_schema())
            if (f.owner == r.cls && f.tag == r.tag) {
                found = r.code == f.code;
                break;
            }
        if (!found) ++missing;
        CHECK(find_tag_ledger(r.cls, r.tag) != nullptr);
        // Consumer counts are not compiled (ref1 t01); the lint checks them against the code.
        CHECK(r.spec.empty() || r.spec.starts_with("docs/"));
        if (r.state == LedgerState::RetailUnused) CHECK(r.spec.starts_with("docs/re/"));
        if (r.state == LedgerState::OutOfScope) CHECK(r.spec.starts_with("docs/DECISIONS.md"));
        if (r.state == LedgerState::Misfiled) CHECK(r.spec.starts_with("docs/re/XML_FIELD_TABLE_SEGMENTS.md"));
    }
    CHECK(missing == 0);
    // Case-insensitive lookup; class matters.
    const TagLedgerRow* speed = find_tag_ledger("gameobjecttype", "MAX_SPEED");
    CHECK(speed != nullptr && speed->tag == "Max_Speed" && speed->state != LedgerState::Unread);
    CHECK(find_tag_ledger("Faction", "Max_Speed") == nullptr);
    CHECK(find_tag_ledger("GameObjectType", "No_Such_Tag") == nullptr);
    CHECK(is_honour_gap(LedgerState::Stored) && is_honour_gap(LedgerState::Unread) && !is_honour_gap(LedgerState::Consumed));
    CHECK(std::string_view(to_string(LedgerState::Verified)) == "verified");
    CHECK(std::string_view(to_string(LedgerState::RetailUnused)) == "retail_unused" &&
          !is_honour_gap(LedgerState::RetailUnused));
    // mcx4 t16: out-of-scope rows (hardware detection here) are settled, not honour gaps.
    const TagLedgerRow* cpu = find_tag_ledger("HardwareConfiguration", "CPUSpeed");
    CHECK(cpu != nullptr && cpu->state == LedgerState::OutOfScope && !is_honour_gap(cpu->state));
    CHECK(std::string_view(to_string(LedgerState::OutOfScope)) == "out_of_scope");
    // mcx4: a segment error of the descriptor tables (MousePointer.Icon_Name is the object types' Icon_Name) is settled.
    const TagLedgerRow* icon = find_tag_ledger("MousePointer", "Icon_Name");
    CHECK(icon != nullptr && icon->state == LedgerState::Misfiled && !is_honour_gap(icon->state));
    CHECK(std::string_view(to_string(LedgerState::Misfiled)) == "misfiled");
    return ::alamo::test::report("alamo_objects_tag_ledger_tests");
}
