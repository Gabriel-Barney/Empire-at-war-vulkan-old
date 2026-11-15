// libFuzzer target: the MEG archive reader (tools/meg, used by engine/vfs) on an in-memory archive.
// Parses the header and tables, verifies, then reads and looks up every entry.
#include "alamo/meg/meg.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::string err;
    auto archive = alamo::meg::Archive::open(
        alamo::meg::Source::from_memory(std::vector<std::uint8_t>(data, data + size)), &err);
    if (!archive) return 0;
    std::size_t sink = archive->verify().size();
    std::vector<std::uint8_t> bytes;
    for (const auto& e : archive->entries()) {
        if (archive->read(e, bytes)) sink += bytes.size();
        if (archive->find(e.name) != nullptr) ++sink;
    }
    return sink == static_cast<std::size_t>(-1) ? 1 : 0;
}
