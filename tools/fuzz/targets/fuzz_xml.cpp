// libFuzzer target: the XML reader (engine/data) on arbitrary bytes. Mods ship XML, so malformed input is real input.
// Walks the whole tree so text, attributes and line lookups are exercised too.
#include "alamo/data/xml.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace {
std::size_t walk(alamo::data::XmlNode n, int depth) {
    if (!n || depth > 512) return 0;
    std::size_t sink = n.name().size() + n.text().size() + n.text_trimmed().size() + n.line();
    for (const auto* a = n.attributes_begin(); a != n.attributes_end(); ++a) sink += a->name.size() + a->value.size();
    for (alamo::data::XmlNode c : n.children()) sink += walk(c, depth + 1);
    return sink;
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto doc = alamo::data::XmlDocument::parse(std::span<const std::uint8_t>(data, size), "fuzz.xml");
    std::size_t sink = doc.ok() ? walk(doc.root(), 0) : 0;
    for (const auto& d : doc.diagnostics()) sink += d.to_string().size();
    if (size > 0) sink += doc.line_of_offset(static_cast<std::uint32_t>(size - 1));
    return sink == static_cast<std::size_t>(-1) ? 1 : 0;
}
