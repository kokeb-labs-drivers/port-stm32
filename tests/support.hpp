// Shared by the host tests: the harness, the fake's control and the port's public headers.
// Test code. SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#ifndef KB_PORT_STM32_TEST_SUPPORT_HPP
#define KB_PORT_STM32_TEST_SUPPORT_HPP

#include <array>
#include <cstdint>
#include <span>

#include "fake_stm32_hal.hpp"
#include "kb/port/stm32/port.hpp"
#include "kb_test.hpp"

namespace stm = kb::port::stm32;
using kb::Status;
using stm::GpioPort;
using stm::PinId;

// Every test starts from a fresh fake (pins analog and low, nothing initialised, no injection).
inline void Fresh() {
  fake::Reset();
}

// The vendor code of a HAL status and ErrorCode.
[[nodiscard]] constexpr uint32_t Code(HAL_StatusTypeDef s, uint32_t error_code = 0U) {
  return stm::VendorCode(static_cast<uint32_t>(s), error_code);
}

// True when `r` failed with `s` and vendor code `code`.
template <typename R>
[[nodiscard]] bool FailedWith(const R& r, Status s, uint32_t code) {
  return !r.IsOk() && (r.Error() == s) && (r.Detail() == code);
}

#if defined(KB_FAKE_STM32F4)
inline constexpr bool kF4 = true;
#else
inline constexpr bool kF4 = false;
#endif

#endif
