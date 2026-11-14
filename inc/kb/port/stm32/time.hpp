/**
 * @file time.hpp
 * @brief `kb::port::stm32::Clock` (satisfies `kb::iface::MonotonicClock`: a 32-bit timer at 1 MHz
 * extended to 64 bits by its update interrupt) and `kb::port::stm32::BusyWait` (satisfies
 * `kb::iface::BusyWait`: the Cortex-M DWT cycle counter).
 *
 * Clock::Start() configures the timer once (TIM2 or TIM5, both 32-bit on every supported family):
 * prescaler for 1 MHz, auto-reload 0xFFFFFFFF, the update interrupt counting wraps (every 71.6
 * minutes; HandleClockIrq(), irq.hpp). Now() reads the high word, the counter and the high word
 * again and corrects for a wrap whose interrupt is still pending, so it never goes back, even
 * inside a critical section. Before Start() Now() returns 0.
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
  /** @brief The 64-bit count from a high word read before and after the counter (the wrap rule). */
  [[nodiscard]] static constexpr uint64_t Combine(uint32_t high_before, uint32_t counter,
                                                  uint32_t high_after, bool wrap_pending) noexcept {
    constexpr uint32_t kHalf = 0x80000000U;
    uint64_t high = high_after;
    if ((high_before == high_after) && wrap_pending && (counter < kHalf)) {
      high += 1U; // the counter wrapped and its interrupt has not run yet
    }
    return (high << 32U) | counter;
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
