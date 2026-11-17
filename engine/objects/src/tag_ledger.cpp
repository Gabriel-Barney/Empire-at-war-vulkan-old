#include "alamo/objects/tag_ledger.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace alamo::objects {

namespace {

constexpr TagLedgerRow kRows[] = {
#include "../generated/tag_ledger.inc"
};

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

std::string key_of(std::string_view cls, std::string_view tag) {
    std::string k;
    k.reserve(cls.size() + tag.size() + 1);
    for (const char c : cls) k += lower(c);
    k += '\n';
    for (const char c : tag) k += lower(c);
    return k;
}

// Lookup only (never iterated): (class, tag) lower case -> row.
const std::unordered_map<std::string, size_t>& index() {
    static const std::unordered_map<std::string, size_t> idx = [] {
        std::unordered_map<std::string, size_t> m;
        for (size_t i = 0; i < std::size(kRows); ++i) m.emplace(key_of(kRows[i].cls, kRows[i].tag), i);
        return m;
    }();
    return idx;
}

}  // namespace

const char* to_string(LedgerState s) {
    switch (s) {
        case LedgerState::Unread: return "unread";
        case LedgerState::Stored: return "stored";
        case LedgerState::Consumed: return "consumed";
        case LedgerState::Verified: return "verified";
    }
    return "?";
}

std::span<const TagLedgerRow> tag_ledger() { return kRows; }

const TagLedgerRow* find_tag_ledger(std::string_view cls, std::string_view tag) {
    const auto& idx = index();
    const auto it = idx.find(key_of(cls, tag));
    return it == idx.end() ? nullptr : &kRows[it->second];
}

}  // namespace alamo::objects
