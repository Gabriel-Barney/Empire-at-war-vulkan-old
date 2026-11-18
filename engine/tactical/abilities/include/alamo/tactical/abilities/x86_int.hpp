// x86 integer semantics for the ability timers: the helpers moved to engine/core (mcx8 t11, alamo/core/x86_int.hpp);
// this header keeps the abilities module's spelling.
#pragma once
#include "alamo/core/x86_int.hpp"

namespace alamo::tactical::abilities {

using ::alamo::dec_wrap;
using ::alamo::trunc_i32;

}  // namespace alamo::tactical::abilities
