// Mod compatibility issue sink (docs/MOD_COMPAT_DESIGN.md §11.1, roadmap mcx0 t01).
//
// One record type for every finding where the port does less than retail, or where retail itself refuses or ignores
// something: an unread or unhonoured tag, an unconsumed Lua binding, a story reward the port lacks, a perception token
// it does not evaluate, a rejected or salvaged asset, a hit limit, a GUI fallback, a runaway script. The load report
// (alamo_mod_check, report v4), the in-game compat panel and the corpus runner all read the same records.
//
// Rules:
//   * Diagnostics only. Sim logic never reads an IssueSink and nothing in it enters a CRC (CLAUDE.md rule 3).
//   * De-duplicated by (kind, subject, source): a repeat adds to `count`, keeps the first record's text, raises the
//     severity to the highest seen and keeps retail_equivalent only when every occurrence was retail-equivalent.
//   * Order is the order of first add, so a report written from it is the same on every run.
//   * Not synchronised. Loaders that run on jobs give each job index its own IssueSink and merge the buffers in index
//     order afterwards (merge / merged); the result does not depend on the worker count.
#pragma once
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace alamo {

enum class IssueKind : uint8_t {
    UnreadTag,          // a tag retail does not read either (unknown to its descriptor tables)
    HonourGap,          // a tag retail reads that the port parses but does not act on (tag ledger state unread / stored)
    RetailEquivalent,   // something retail refuses or ignores the same way (BOM script, 32-bit bytecode, unknown reward)
    AssetRejected,      // an asset the port cannot decode
    AssetSalvaged,      // an asset decoded under a salvage rule (§10.2)
    BindingUnconsumed,  // a Lua binding the mod calls whose effect the port records but never shows, or a no-op gap
    RewardMissing,      // a retail story reward the port does not implement
    TokenMissing,       // a perception token path the port does not evaluate
    LimitHit,           // data that hits a guard or a cited retail limit
    MegUnsorted,        // a MEG whose table is not sorted by CRC
    MegNameMismatch,    // a MEG entry whose stored CRC does not match its normalised name
    MegDuplicate,       // a name stored twice in one MEG
    GuiFallback,        // a mod GUI file failed to load; the next layer's copy was used
    ScriptRunaway,      // a script that never yields was stopped
    ScriptError,        // a script that does not compile or raises an error
    MissingFile,        // a file the data names that no layer has
    XmlExtraRoot,       // an XML file with more than one top-level element
    TextSalvaged,       // a text database read up to a bad record
    Count
};
// "unread_tag", "honour_gap", ... (the spelling the JSON report uses); "" for Count.
std::string_view issue_kind_name(IssueKind kind);
// Inverse of issue_kind_name; false for an unknown name.
bool parse_issue_kind(std::string_view name, IssueKind& out);

enum class IssueSeverity : uint8_t { Info, Warning, Error };  // ordered: a higher value is worse
std::string_view issue_severity_name(IssueSeverity severity);  // "info", "warning", "error"

// The mount layer that served the file behind an issue (§6 provenance): 0.. = the mod's index in the effective mod
// search list (earliest = 0), kBaseLayer = the base game (FoC loose files and MEGs, fallbacks), kNoLayer = not a file.
inline constexpr uint16_t kBaseLayer = 0xFFFE;
inline constexpr uint16_t kNoLayer = 0xFFFF;

struct CompatIssue {
    IssueKind kind = IssueKind::Count;
    IssueSeverity severity = IssueSeverity::Warning;
    bool retail_equivalent = false;  // retail does the same (the detail cites where): never lowers a grade
    std::string subject;             // tag / binding / file / token / reward name
    std::string source;              // game path that caused it ("" when none)
    uint16_t layer = kNoLayer;
    std::string detail;
    uint32_t count = 1;              // occurrences folded into this record
};

class IssueSink {
public:
    // Adds the issue, or counts it on the record with the same (kind, subject, source). Returns that record.
    const CompatIssue& add(CompatIssue issue);
    const CompatIssue& add(IssueKind kind, IssueSeverity severity, std::string subject, std::string source,
                           std::string detail = {}, bool retail_equivalent = false, uint16_t layer = kNoLayer);
    // Adds every record of `other` in its order (counts included), as add() would.
    void merge(const IssueSink& other);
    // The per-index buffers of a parallel load, merged in index order.
    static IssueSink merged(std::span<const IssueSink> buffers);

    const std::vector<CompatIssue>& issues() const { return issues_; }  // first-add order
    size_t size() const { return issues_.size(); }
    bool empty() const { return issues_.empty(); }
    size_t count(IssueKind kind) const;  // records (not occurrences) of `kind`
    // nullptr when no record has that key.
    const CompatIssue* find(IssueKind kind, std::string_view subject, std::string_view source) const;
    void clear();

private:
    using Key = std::tuple<IssueKind, std::string, std::string>;
    std::vector<CompatIssue> issues_;
    std::map<Key, size_t, std::less<>> index_;  // lookup only, never iterated for output
};

}  // namespace alamo
