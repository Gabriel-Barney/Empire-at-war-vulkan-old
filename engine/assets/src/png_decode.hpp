// PNG reader for mod textures (mcx2 t04 follow-up, texture.hpp decode_png). Retail loads textures through D3DX, which
// picks the decoder by content, so a PNG file named .tga or .dds draws in retail (Thrawn's Revenge ships such icons).
// Own inflate (RFC 1950 / 1951) and PNG (RFC 2083) code, no third-party code. Untrusted input: every read is bounds
// checked, the inflated size is capped at what the header implies, and malformed data fails cleanly.
// Supported: colour types 0, 2, 3, 4, 6 at every legal bit depth (1, 2, 4, 8, 16), Adam7 interlacing, tRNS (palette
// alpha, grey / RGB colour key). 16-bit samples keep their high byte. Output: BGRA8, top row first.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace alamo::assets::png {

inline bool is_png(std::span<const std::uint8_t> b) {
    static constexpr std::uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (b.size() < 8) return false;
    for (int i = 0; i < 8; ++i)
        if (b[static_cast<std::size_t>(i)] != kSig[i]) return false;
    return true;
}

// max_pixels: refused above it (before anything is allocated).
bool decode(std::span<const std::uint8_t> file, std::uint32_t& width, std::uint32_t& height, std::vector<std::uint8_t>& bgra,
            std::uint64_t max_pixels, std::string* error);

}  // namespace alamo::assets::png
