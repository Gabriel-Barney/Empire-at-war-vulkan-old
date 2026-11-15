// libFuzzer target: texture decoding by content (engine/assets decode_texture: DDS, BMP, TGA), then the mip generator.
#include "alamo/assets/texture.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    alamo::assets::TextureAsset tex;
    std::string err;
    std::size_t sink = 0;
    if (alamo::assets::decode_texture(std::vector<std::uint8_t>(data, data + size), tex, &err)) {
        sink += tex.width + tex.height + tex.levels.size();
        if (alamo::assets::generate_mips(tex)) sink += tex.levels.size();
    }
    return sink == static_cast<std::size_t>(-1) ? 1 : 0;
}
