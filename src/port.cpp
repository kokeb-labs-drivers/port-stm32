// The library's own translation unit for port.hpp: every type's conversion to its contract's *Ref
// and the version are checked by each build of the library (and every public header is seen by the
// MISRA scan's builds). SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include "kb/port/stm32/port.hpp"

namespace kb::port::stm32 {
static_assert((kVersionMajor == 0U) && (kVersionMinor == 1U) && (kVersionPatch == 0U),
              "version.hpp, CMakeLists.txt and CHANGELOG.md name one version");
} // namespace kb::port::stm32
