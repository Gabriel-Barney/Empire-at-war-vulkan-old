// mcx0 t01: IssueSink (compat_issue.hpp) - de-duplication by (kind, subject, source), counts, severity and
// retail-equivalent folding, first-add order, and the index-order merge of per-index buffers.
#include <cstdio>
#include <string>
#include <vector>

#include "alamo/core/compat_issue.hpp"
#include "alamo/test/support.hpp"

using alamo::CompatIssue;
using alamo::IssueKind;
using alamo::IssueSeverity;
using alamo::IssueSink;
using ::alamo::test::g_fail;

namespace {

void names() {
    ::alamo::test::section("kind names");
    for (size_t i = 0; i < static_cast<size_t>(IssueKind::Count); ++i) {
        const IssueKind k = static_cast<IssueKind>(i);
        IssueKind back = IssueKind::Count;
        CHECK(!alamo::issue_kind_name(k).empty());
        CHECK(alamo::parse_issue_kind(alamo::issue_kind_name(k), back) && back == k);
    }
    IssueKind k = IssueKind::Count;
    CHECK(!alamo::parse_issue_kind("no_such_kind", k) && k == IssueKind::Count);
    CHECK(alamo::issue_kind_name(IssueKind::HonourGap) == "honour_gap");
    // ums4 t07: appended last, so every earlier value keeps its number.
    CHECK(alamo::issue_kind_name(IssueKind::LoaderDiagnostic) == "loader_diagnostic");
    CHECK(static_cast<int>(IssueKind::TextSalvaged) == 17 && static_cast<int>(IssueKind::LoaderDiagnostic) == 18);
    CHECK(alamo::issue_severity_name(IssueSeverity::Error) == "error");
}

void dedup() {
    ::alamo::test::section("de-duplication");
    IssueSink s;
    s.add(IssueKind::HonourGap, IssueSeverity::Info, "Size_Value", "Data\\XML\\A.XML", "first", true, 2);
    s.add(IssueKind::HonourGap, IssueSeverity::Warning, "Size_Value", "Data\\XML\\A.XML", "second", false, 3);
    s.add(IssueKind::HonourGap, IssueSeverity::Info, "Size_Value", "Data\\XML\\B.XML");
    s.add(IssueKind::UnreadTag, IssueSeverity::Info, "Size_Value", "Data\\XML\\A.XML");
    CHECK(s.size() == 3);
    const CompatIssue* a = s.find(IssueKind::HonourGap, "Size_Value", "Data\\XML\\A.XML");
    CHECK(a != nullptr);
    if (a) {
        CHECK(a->count == 2);
        CHECK(a->severity == IssueSeverity::Warning);  // the worst seen
        CHECK(!a->retail_equivalent);                  // one occurrence was not
        CHECK(a->detail == "first" && a->layer == 2);  // the first record's text and layer
    }
    CHECK(s.count(IssueKind::HonourGap) == 2 && s.count(IssueKind::UnreadTag) == 1 && s.count(IssueKind::LimitHit) == 0);
    CHECK(s.issues()[0].source == "Data\\XML\\A.XML" && s.issues()[1].source == "Data\\XML\\B.XML" &&
          s.issues()[2].kind == IssueKind::UnreadTag);
    CHECK(s.find(IssueKind::HonourGap, "size_value", "Data\\XML\\A.XML") == nullptr);  // keys are exact
    s.clear();
    CHECK(s.empty() && s.find(IssueKind::UnreadTag, "Size_Value", "Data\\XML\\A.XML") == nullptr);
}

// Per-index buffers filled in any order merge to the same result as one serial pass in index order.
void merge_order() {
    ::alamo::test::section("index-order merge");
    std::vector<IssueSink> buffers(4);
    const auto fill = [](IssueSink& b, int i) {
        b.add(IssueKind::MissingFile, IssueSeverity::Warning, "file" + std::to_string(i % 2), "list.xml");
        b.add(IssueKind::AssetSalvaged, IssueSeverity::Info, "model" + std::to_string(i), "art");
    };
    for (int i = 3; i >= 0; --i) fill(buffers[static_cast<size_t>(i)], i);  // reverse fill order
    const IssueSink m = IssueSink::merged(buffers);
    IssueSink serial;
    for (int i = 0; i < 4; ++i) fill(serial, i);
    CHECK(m.size() == serial.size() && m.size() == 6);
    for (size_t i = 0; i < m.size() && i < serial.size(); ++i) {
        CHECK(m.issues()[i].kind == serial.issues()[i].kind);
        CHECK(m.issues()[i].subject == serial.issues()[i].subject);
        CHECK(m.issues()[i].count == serial.issues()[i].count);
    }
    const CompatIssue* f0 = m.find(IssueKind::MissingFile, "file0", "list.xml");
    CHECK(f0 && f0->count == 2);
    // A record with count 0 counts once.
    IssueSink z;
    CompatIssue c;
    c.kind = IssueKind::LimitHit;
    c.subject = "players";
    c.count = 0;
    CHECK(z.add(c).count == 1);
    CHECK(z.add(c).count == 2);
}

}  // namespace

int main() {
    names();
    dedup();
    merge_order();
    return ::alamo::test::report("alamo_core_compat_issue_tests");
}
