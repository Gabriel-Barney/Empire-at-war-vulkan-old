#include "alamo/core/compat_issue.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace alamo {

namespace {

constexpr std::array<std::string_view, static_cast<size_t>(IssueKind::Count)> kKindNames = {
    "unread_tag",         "honour_gap",     "retail_equivalent", "asset_rejected", "asset_salvaged", "binding_unconsumed",
    "reward_missing",     "token_missing",  "limit_hit",         "meg_unsorted",   "meg_name_mismatch", "meg_duplicate",
    "gui_fallback",       "script_runaway", "script_error",      "missing_file",   "xml_extra_root",  "text_salvaged",
};

}  // namespace

std::string_view issue_kind_name(IssueKind kind) {
    const size_t i = static_cast<size_t>(kind);
    return i < kKindNames.size() ? kKindNames[i] : std::string_view{};
}

bool parse_issue_kind(std::string_view name, IssueKind& out) {
    for (size_t i = 0; i < kKindNames.size(); ++i)
        if (kKindNames[i] == name) {
            out = static_cast<IssueKind>(i);
            return true;
        }
    return false;
}

std::string_view issue_severity_name(IssueSeverity severity) {
    switch (severity) {
        case IssueSeverity::Info: return "info";
        case IssueSeverity::Warning: return "warning";
        case IssueSeverity::Error: return "error";
    }
    return {};
}

const CompatIssue& IssueSink::add(CompatIssue issue) {
    if (issue.count == 0) issue.count = 1;
    Key key{issue.kind, issue.subject, issue.source};
    const auto it = index_.find(key);
    if (it == index_.end()) {
        index_.emplace(std::move(key), issues_.size());
        issues_.push_back(std::move(issue));
        return issues_.back();
    }
    CompatIssue& r = issues_[it->second];
    r.count += issue.count;
    r.severity = std::max(r.severity, issue.severity);
    r.retail_equivalent = r.retail_equivalent && issue.retail_equivalent;
    return r;
}

const CompatIssue& IssueSink::add(IssueKind kind, IssueSeverity severity, std::string subject, std::string source,
                                  std::string detail, bool retail_equivalent, uint16_t layer) {
    CompatIssue i;
    i.kind = kind;
    i.severity = severity;
    i.retail_equivalent = retail_equivalent;
    i.subject = std::move(subject);
    i.source = std::move(source);
    i.layer = layer;
    i.detail = std::move(detail);
    return add(std::move(i));
}

void IssueSink::merge(const IssueSink& other) {
    for (const CompatIssue& i : other.issues_) add(i);
}

IssueSink IssueSink::merged(std::span<const IssueSink> buffers) {
    IssueSink out;
    for (const IssueSink& b : buffers) out.merge(b);
    return out;
}

size_t IssueSink::count(IssueKind kind) const {
    return static_cast<size_t>(std::count_if(issues_.begin(), issues_.end(), [kind](const CompatIssue& i) { return i.kind == kind; }));
}

const CompatIssue* IssueSink::find(IssueKind kind, std::string_view subject, std::string_view source) const {
    const auto it = index_.find(Key{kind, std::string(subject), std::string(source)});
    return it == index_.end() ? nullptr : &issues_[it->second];
}

void IssueSink::clear() {
    issues_.clear();
    index_.clear();
}

}  // namespace alamo
