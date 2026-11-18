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
        if (r.state == LedgerState::Consumed || r.state == LedgerState::Verified) CHECK(r.consumers > 0 && !r.consumer.empty());
        if (r.state == LedgerState::Unread || r.state == LedgerState::Stored) CHECK(r.consumers == 0);
    }
    CHECK(missing == 0);
    // Case-insensitive lookup; class matters.
    const TagLedgerRow* speed = find_tag_ledger("gameobjecttype", "MAX_SPEED");
    CHECK(speed != nullptr && speed->tag == "Max_Speed" && speed->state != LedgerState::Unread);
    CHECK(find_tag_ledger("Faction", "Max_Speed") == nullptr);
    CHECK(find_tag_ledger("GameObjectType", "No_Such_Tag") == nullptr);
    CHECK(is_honour_gap(LedgerState::Stored) && is_honour_gap(LedgerState::Unread) && !is_honour_gap(LedgerState::Consumed));
    CHECK(std::string_view(to_string(LedgerState::Verified)) == "verified");
    return ::alamo::test::report("alamo_objects_tag_ledger_tests");
}
