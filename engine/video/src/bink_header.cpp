#include "alamo/video/bink_header.hpp"

#include <cstring>

namespace alamo::video {

namespace {
uint32_t rd32(std::span<const uint8_t> b, size_t off) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + off, 4);
    return v;
}
}  // namespace

std::optional<BinkHeader> probe_bink_header(std::span<const uint8_t> bytes) {
    if (bytes.size() < kBinkHeaderBytes) return std::nullopt;
    BinkHeader h;
    if (bytes[0] == 'B' && bytes[1] == 'I' && bytes[2] == 'K') {
        h.bink2 = false;
    } else if (bytes[0] == 'K' && bytes[1] == 'B' && bytes[2] == '2') {
        h.bink2 = true;
    } else {
        return std::nullopt;
    }
    h.revision = static_cast<char>(bytes[3]);
    h.file_size = rd32(bytes, 4);
    h.frames = rd32(bytes, 8);
    h.largest_frame = rd32(bytes, 12);
    h.width = rd32(bytes, 20);
    h.height = rd32(bytes, 24);
    h.fps_num = rd32(bytes, 28);
    h.fps_den = rd32(bytes, 32);
    h.flags = rd32(bytes, 36);
    h.audio_tracks = rd32(bytes, 40);
    return h;
}

std::string bink_unsupported_reason(const BinkHeader& h) {
    if (h.bink2)
        return std::string("Bink 2 movie (KB2") + h.revision +
               "): the port's FFmpeg decoder has no Bink 2; retail plays it (bink2w64.dll) [honour gap]";
    return {};
}

}  // namespace alamo::video
