// Clock (the 64-bit clock over its 32-bit timer and wrap interrupt, timer ownership), BusyWait
// (the DWT spin), the interrupt mask guard and the HAL time-base check. Platform-free; every HAL
// and CMSIS call goes through sdk.hpp (src/boundary/time.cpp, src/boundary/timer.cpp).
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <algorithm>
// State shared with interrupts needs lock-free std::atomic (as kb-core's SpscRing does).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstdint>

#include "clock_read.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/irq.hpp"
#include "kb/port/stm32/time.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
constexpr uint64_t kUsPerMs = 1000U;
constexpr uint32_t kHalTickCheckMs = 25U; // more than two periods of a 100 Hz tick
constexpr uint32_t kHalTickCheckPolls = 10000000U;

// The clock's registers and wrap count, read in the order of ClockSample.
class TimerSource {
public:
  TimerSource(uint8_t timer, const std::atomic<uint32_t>& wraps) noexcept
      : timer_(timer), wraps_(wraps) {}
  [[nodiscard]] ClockSample Sample() const noexcept {
    ClockSample s{};
    s.high_before = wraps_.load(std::memory_order_acquire);
    s.counter_before = sdk::ClockCounter(timer_);
    s.wrap_pending = sdk::ClockWrapPending(timer_);
    s.counter_after = sdk::ClockCounter(timer_);
    s.high_after = wraps_.load(std::memory_order_acquire);
    return s;
  }
  [[nodiscard]] ClockSample SampleMasked() const noexcept {
    const IrqGuard guard{};
    return Sample();
  }

private:
  uint8_t timer_;
  const std::atomic<uint32_t>& wraps_;
};
} // namespace

// --- interrupt mask, HAL time base, clock reads
// ----------------------------------------------------

IrqGuard::IrqGuard() noexcept : saved_(sdk::IrqMask()) {}

IrqGuard::~IrqGuard() noexcept {
  sdk::IrqRestore(saved_);
}

bool HalTickRuns() noexcept {
  // Two HAL tick periods (uwTickFreq: 1 ms by default, 10 or 100 ms with HAL_TICK_FREQ_100HZ or
  // _10HZ), and at least kHalTickCheckMs, so a time base advanced from a 100 Hz RTOS tick hook is
  // seen running too. The wait ends at the first change.
  const uint32_t first = sdk::TickMs();
  const uint32_t period_ms = sdk::TickPeriodMs();
  const uint64_t window_ms = std::max<uint64_t>((2U * uint64_t{period_ms}) + 1U, kHalTickCheckMs);
  const uint64_t budget = (uint64_t{sdk::CoreClockHz()} * window_ms) / kUsPerMs;
  uint64_t elapsed = 0U;
  uint32_t last = sdk::Cycles();
  bool runs = false;
  for (uint32_t i = 0U; (i < kHalTickCheckPolls) && !runs && (elapsed <= budget); ++i) {
    runs = sdk::TickMs() != first;
    const uint32_t now = sdk::Cycles();
    elapsed += now - last; // extended to 64 bits before any division
    last = now;
  }
  return runs;
}

uint64_t ClockNowUs(uint8_t timer) noexcept {
  uint64_t us = 0U;
  if ((timer < kTimers) && sdk::State().clock_on[timer].load(std::memory_order_acquire)) {
    const TimerSource source{timer, sdk::State().wraps[timer]};
    us = ReadClock(source);
  }
  return us;
}

void HandleClockIrq(uint8_t timer) noexcept {
  if ((timer < kTimers) && !sdk::State().clock_on[timer].load(std::memory_order_acquire)) {
    sdk::ClockIrqOff(timer); // not the port's clock: its update interrupt would re-enter forever
  } else if (timer < kTimers) {
    // The flag cleared and the wrap counted with interrupts masked: no reader, not even a higher
    // priority interrupt, ever sees one without the other.
    const IrqGuard guard{};
    if (sdk::ClockWrapPending(timer)) {
      sdk::ClockClearWrap(timer);
      static_cast<void>(sdk::State().wraps[timer].fetch_add(1U, std::memory_order_acq_rel));
    }
  } else {
    // not a timer
  }
}

// --- Clock, BusyWait -----------------------------------------------------------------------------

kb::Status Clock::Start() noexcept {
  uint32_t code = hal::kOk;
  const uint8_t timer = config_.timer;
  bool start = false;
  if (!sdk::ClockTimerExists(timer)) {
    code = port_code::kInvalidArgument;
  } else {
    const IrqGuard guard{};
    TimerEntry& entry = sdk::State().timers[timer];
    if (entry.use == TimerUse::Pwm) {
      code = port_code::kOwned; // a PWM channel runs on this timer
    } else if (entry.use == TimerUse::None) {
      entry.use = TimerUse::Clock; // claimed; started below
      start = true;
    } else {
      // already the clock: Ok
    }
  }
  if (start) {
    const uint32_t hz =
        (config_.timer_clock_hz != 0U) ? config_.timer_clock_hz : sdk::TimerClockHz(timer);
    PortState& state = sdk::State();
    state.wraps[timer].store(0U, std::memory_order_relaxed);
    code = (hz != 0U)
               ? sdk::ClockStart(ClockConfig{
                     .timer = timer, .irq_priority = config_.irq_priority, .timer_clock_hz = hz})
               : port_code::kNoClock; // the counter is zeroed: it may run on from a loader
    if (code == hal::kOk) {
      state.clock_on[timer].store(true, std::memory_order_release);
      uint8_t none = 0U;
      static_cast<void>(state.clock_timer.compare_exchange_strong(none, timer));
    } else {
      const IrqGuard guard{};
      state.timers[timer].use = TimerUse::None;
    }
  }
  return error_.Set(MapHalStatus(code & 0xFFU), code);
}

kb::Instant Clock::Now() const noexcept {
  return kb::Instant{ClockNowUs(config_.timer)};
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract calls a member
void BusyWait::WaitUs(kb::Microseconds us) noexcept {
  constexpr uint64_t kStep = 0x40000000U; // a quarter of the counter: never wraps within a step
  uint64_t left = Cycles(us.Count(), sdk::CoreClockHz());
  while (left > 0U) {
    const auto step = static_cast<uint32_t>(std::min(left, kStep));
    const uint32_t start = sdk::Cycles();
    while ((sdk::Cycles() - start) < step) {
    }
    left -= step;
  }
}

} // namespace kb::port::stm32
