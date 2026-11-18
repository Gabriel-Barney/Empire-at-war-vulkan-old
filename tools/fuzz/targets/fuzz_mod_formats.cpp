// libFuzzer target for one of the smoke targets (smoke/fuzz_targets.hpp), chosen at build time by ALAMO_FUZZ_TARGET
// (fuzz_ted, fuzz_dat, ... in tools/fuzz/CMakeLists.txt; mcx2 t08). A target this tree lacks makes every input a no-op.
#include <cstddef>
#include <cstdint>
#include <span>

#include "fuzz_targets.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static const alamo::fuzz::Target* t = alamo::fuzz::find_target(ALAMO_FUZZ_TARGET);
    if (t) t->run(std::span<const std::uint8_t>(data, size));
    return 0;
}
