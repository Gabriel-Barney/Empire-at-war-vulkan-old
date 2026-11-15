// libFuzzer target: the compiled-effect (.fxo, FX 2.0) reader that engine/materials runs on mod effects at load time.
// parseEffect throws ParseError on malformed input; anything else (a crash, another exception, a hang) is a finding.
#include "fx2_reader.hpp"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    try {
        const alamo::fx::Effect e = alamo::fx::parseEffect(data, size);
        return e.parameters.size() == static_cast<std::size_t>(-1) ? 1 : 0;
    } catch (const alamo::fx::ParseError&) {
        return 0;
    }
}
