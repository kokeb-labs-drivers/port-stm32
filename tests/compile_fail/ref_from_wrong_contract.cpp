// A pin that only drives cannot be named by an input reference.
// EXPECT: (constraint|satisf|no matching)
// Test code. SPDX-License-Identifier: MIT
#include "kb/port/stm32/port.hpp"

void Use(kb::port::stm32::OutputPin& pin) {
  kb::iface::GpioInputRef ref{pin};
  static_cast<void>(ref);
}
