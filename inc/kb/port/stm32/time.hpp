/**
 * @file time.hpp
 * @brief `kb::port::stm32::Clock` (satisfies `kb::iface::MonotonicClock`: a 32-bit timer at 1 MHz
 * extended to 64 bits by its update interrupt) and `kb::port::stm32::BusyWait` (satisfies
 * `kb::iface::BusyWait`: the Cortex-M DWT cycle counter).
 *
 * Clock::Start() configures the timer once (TIM2 or TIM5, both 32-bit on every supported family):
 * prescaler for 1 MHz, auto-reload 0xFFFFFFFF, the update interrupt counting wraps (every 71.6
 * minutes; HandleClockIrq(), irq.hpp). The wrap interrupt clears the update flag and counts the
 * wrap with interrupts masked, so no reader ever sees one done without the other. The timer is
 * then owned by the clock: a PwmChannel on it is refused.
 *
 * Now() takes a ClockSample, in this order: the wrap count, the counter, the update flag, the
 * counter again, the wrap count again (time-log.md has the proof). Combine():
 * - wrap counts differ: the wrap interrupt ran inside the sample; the sample is discarded and
 *   taken again (after three such samples, one is taken with interrupts masked, which cannot be);
 * - update flag set: a wrap happened before the flag was read and is not yet counted, and the
 *   second counter read is after it: (count + 1, second counter);
 * - flag clear: no uncounted wrap happened up to the flag read, so none before the first counter
 *   read: (count, first counter).
 * Each result is the true count at one of the sample's counter reads, so successive results never
 * go back. Before Start() Now() returns 0.
 *
 * BusyWait spins on DWT->CYCCNT (enabled on first use) for at least the asked time at
 * SystemCoreClock; long waits are split so the 32-bit counter never wraps within one step.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_TIME_HPP
#define KB_PORT_STM32_TIME_HPP

#include <cstdint>

#include "kb/interfaces/time.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"

namespace kb::port::stm32 {

/** @brief How the clock's timer is set up. */
struct ClockConfig {
  uint8_t timer{2U};        ///< TIM2 or TIM5 (32-bit).
  uint8_t irq_priority{1U}; ///< NVIC priority of the wrap interrupt (high: it must not wait long).
  uint32_t timer_clock_hz{0U}; ///< The timer's clock; 0 derives it from the APB1 clock.
};

/** @brief One reading of the clock's registers, in the order they are read. */
struct ClockSample {
  uint32_t high_before{0U};    ///< The wrap count, first read.
  uint32_t counter_before{0U}; ///< The counter, first read.
  bool wrap_pending{false};    ///< The update flag: a wrap not yet counted by the interrupt.
  uint32_t counter_after{0U};  ///< The counter, read after the flag.
  uint32_t high_after{0U};     ///< The wrap count, read last.
};

/** @brief The 64-bit microsecond clock; satisfies kb::iface::MonotonicClock. */
class Clock {
public:
  /** @brief A clock on `config.timer` (Start() configures it). */
  explicit constexpr Clock(const ClockConfig& config = ClockConfig{}) noexcept : config_(config) {}
  /** @brief Configures and starts the timer once (later calls and other Clock objects: Ok). */
  [[nodiscard]] kb::Status Start() noexcept;
  /** @brief Microseconds since Start(). */
  [[nodiscard]] kb::Instant Now() const noexcept;
  /** @brief The last call's status and its vendor code in Detail(). */
  [[nodiscard]] kb::Result<void> LastError() const noexcept {
    return error_.Get();
  }
  /**
   * @brief The 64-bit count of `sample` into `us`; false when the wrap interrupt ran inside the
   * sample (the wrap counts differ), which the caller answers by sampling again.
   */
  [[nodiscard]] static constexpr bool Combine(const ClockSample& sample, uint64_t& us) noexcept {
    const bool consistent = sample.high_before == sample.high_after;
    if (consistent) {
      const uint64_t high = sample.high_before;
      us = sample.wrap_pending ? (((high + 1U) << 32U) | sample.counter_after)
                               : ((high << 32U) | sample.counter_before);
    }
    return consistent;
  }

private:
  ClockConfig config_;
  LastErrorRecord error_;
};

/** @brief A spin delay on the DWT cycle counter; satisfies kb::iface::BusyWait. */
class BusyWait {
public:
  /** @brief Spins for at least `us` (waits of 1 ms or more should sleep instead). */
  void WaitUs(kb::Microseconds us) noexcept;
  /** @brief The cycles to wait for `us` at `core_hz`, rounded up. */
  [[nodiscard]] static constexpr uint64_t Cycles(uint32_t us, uint32_t core_hz) noexcept {
    constexpr uint64_t kUsPerSecond = 1000000U;
    return ((uint64_t{us} * core_hz) + (kUsPerSecond - 1U)) / kUsPerSecond;
  }
};

static_assert(kb::iface::MonotonicClock<Clock>, "Clock satisfies kb::iface::MonotonicClock");
static_assert(kb::iface::BusyWait<BusyWait>, "BusyWait satisfies kb::iface::BusyWait");

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_TIME_HPP
