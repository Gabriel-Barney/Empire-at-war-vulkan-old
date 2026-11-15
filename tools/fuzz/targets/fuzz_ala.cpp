// libFuzzer target: ALA animation decoding (engine/assets build_animation) on arbitrary bytes.
#include "alamo/assets/skeleton.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    alamo::assets::AnimationClip clip;
    std::string err;
    std::size_t sink = 0;
    if (alamo::assets::build_animation(std::span<const std::uint8_t>(data, size), "FUZZ.ALA", clip, &err)) {
        sink += clip.frame_count + clip.tracks.size();
    }
    return sink == static_cast<std::size_t>(-1) ? 1 : 0;
}
