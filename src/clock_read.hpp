/**
 * @file clock_read.hpp
 * @brief The read rule of the 64-bit clock over any sample source, so the host tests drive it with
 * a model of the timer over every interleaving of the reads and the wrap interrupt
 * (tests/test_clock_read.cpp). Private to the port; platform-free.
 *
 * `Source` provides `ClockSample Sample()` (the five reads in the order of ClockSample, interrupts
 * enabled) and `ClockSample SampleMasked()` (the same with interrupts masked, so the wrap interrupt
 * cannot run inside it).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_SRC_CLOCK_READ_HPP
#define KB_PORT_STM32_SRC_CLOCK_READ_HPP

#include <cstdint>

#include "kb/port/stm32/time.hpp"

namespace kb::port::stm32 {

/** @brief Unmasked samples taken before one is taken with interrupts masked. */
inline constexpr uint32_t kClockReadTries = 3U;

/** @brief The 64-bit count from `source` (Clock::Combine, sampled again when inconsistent). */
template <typename Source>
[[nodiscard]] uint64_t ReadClock(Source& source) noexcept {
  uint64_t us = 0U;
  bool ok = false;
  for (uint32_t i = 0U; (i < kClockReadTries) && !ok; ++i) {
    ok = Clock::Combine(source.Sample(), us);
  }
  if (!ok) {
    // The wrap interrupt ran inside each sample: impossible unless the reader was held off for a
    // wrap period each time. With interrupts masked the interrupt cannot run inside the sample.
    static_cast<void>(Clock::Combine(source.SampleMasked(), us));
  }
  return us;
}

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_SRC_CLOCK_READ_HPP
