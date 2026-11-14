// The concepts tell an output pin from an input pin at compile time.
// EXPECT: OutputPin is not a GpioInput
// Test code. SPDX-License-Identifier: MIT
#include "kb/port/stm32/port.hpp"

static_assert(kb::iface::GpioInput<kb::port::stm32::OutputPin>, "OutputPin is not a GpioInput");
