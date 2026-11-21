// libFuzzer target: ALO model decoding (engine/assets build_model, alamo_formats underneath) on arbitrary bytes.
#include "alamo/assets/model.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> bytes(data, size);
    std::size_t sink = alamo::assets::is_particle_alo(bytes) ? 1 : 0;
    alamo::assets::ModelAsset model;
    std::string err;
    if (alamo::assets::build_model(bytes, "FUZZ.ALO", model, &err)) {
        sink += model.meshes.size() + model.submeshes.size() + model.proxies.size() + model.warnings.size();
    }
    return sink == static_cast<std::size_t>(-1) ? 1 : 0;
}
