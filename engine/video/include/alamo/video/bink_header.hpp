// alamo_video - the Bink container header, read by the port itself (no FFmpeg) so that a movie the decoder cannot play
// is named before FFmpeg's "not a Bink file" (mcx2 t05 / t08, docs/MOD_COMPAT_DESIGN.md 10.3).
//
// Layout (little-endian u32 after the 4-byte signature): 'BIK' + revision letter (Bink 1, 'b'..'k') or 'KB2' +
// revision (Bink 2); +4 file size - 8, +8 frame count, +12 largest frame bytes, +20 width, +24 height, +28 fps
// numerator, +32 fps denominator, +36 flags, +40 audio track count. Values are returned as stored (no range checks:
// FFmpeg validates what it decodes).
// Retail FoC x64 1.121 links bink2w64.dll (the Bink 2 SDK, which also plays Bink 1), so retail plays both; the port's
// FFmpeg path plays Bink 1 only, and a Bink 2 movie is skipped with bink_unsupported_reason() [honour gap].
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace alamo::video {

inline constexpr size_t kBinkHeaderBytes = 44;

struct BinkHeader {
    bool bink2 = false;
    char revision = 0;
    uint32_t file_size = 0, frames = 0, largest_frame = 0, width = 0, height = 0, fps_num = 0, fps_den = 0, flags = 0,
             audio_tracks = 0;
};

// nullopt when the bytes are not a Bink 1 / Bink 2 header (wrong signature, fewer than kBinkHeaderBytes).
std::optional<BinkHeader> probe_bink_header(std::span<const uint8_t> bytes);
// Why the port cannot play a movie with this header ("" when it can try).
std::string bink_unsupported_reason(const BinkHeader& h);

}  // namespace alamo::video
