// Deadline and Completion (completion.hpp): the waits of every port object, bounded on the port's
// own clock. Platform-free; the clock and the cycle counter come through sdk.hpp
// (src/stm32_io_boundary.cpp).
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <algorithm>
// State shared with interrupts needs lock-free std::atomic (as kb-core's SpscRing does).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstdint>

#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/rtos/semaphore.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
// True when kb::rtos takes and sleeps block (FreeRTOS, or the host's std::thread backend): set by
// CMake from KB_RTOS. Under the `none` backend they return at once.
#if defined(KB_PORT_STM32_RTOS_BLOCKS)
constexpr bool kRtosSleeps = true;
#else
constexpr bool kRtosSleeps = false;
#endif
constexpr uint64_t kUsPerSecond = 1000000U;
constexpr uint64_t kUsPerMs = 1000U;
} // namespace

// --- Deadline, Completion ------------------------------------------------------------------------

Deadline::Deadline(kb::Milliseconds budget) noexcept : Deadline(budget, kRtosSleeps) {}

Deadline::Deadline(kb::Milliseconds budget, bool sleeps_block) noexcept
    : budget_us_(uint64_t{budget.Count()} * kUsPerMs),
      max_polls_(static_cast<uint32_t>(
          std::min<uint64_t>((uint64_t{budget.Count()} + 1U) * kDeadlinePollsPerMs, UINT32_MAX))),
      max_slept_ms_(uint64_t{budget.Count()} + kWaitSliceMs),
      clock_timer_(sdk::State().clock_timer.load(std::memory_order_acquire)),
      sleeps_block_(sleeps_block) {
  if (clock_timer_ != 0U) {
    start_us_ = ClockNowUs(clock_timer_);
  } else {
    core_hz_ = sdk::CoreClockHz();
    last_cycles_ = sdk::Cycles();
  }
}

uint64_t Deadline::ElapsedUs() noexcept {
  uint64_t us = 0U;
  if (clock_timer_ != 0U) {
    us = ClockNowUs(clock_timer_) - start_us_;
  } else {
    const uint32_t now = sdk::Cycles();
    cycles_ += now - last_cycles_; // 64-bit cycles first, then microseconds
    last_cycles_ = now;
    us = (core_hz_ != 0U) ? ((cycles_ * kUsPerSecond) / core_hz_) : cycles_;
  }
  return us;
}

void Deadline::Slept(uint32_t ms) noexcept {
  slept_ms_ += ms;
}

bool Deadline::Expired() noexcept {
  if (!expired_) {
    ++polls_;
    // The second bound: slept time on the RTOS tick when sleeps block, else the poll count.
    const bool capped = sleeps_block_ ? (slept_ms_ >= max_slept_ms_) : (polls_ >= max_polls_);
    expired_ = capped || (ElapsedUs() >= budget_us_);
  }
  return expired_;
}

kb::Milliseconds Deadline::Remaining() noexcept {
  uint32_t ms = 0U;
  if (!Expired()) {
    const uint64_t elapsed = ElapsedUs();
    const uint64_t left = (elapsed < budget_us_) ? (budget_us_ - elapsed) : 0U;
    ms = static_cast<uint32_t>((left + (kUsPerMs - 1U)) / kUsPerMs);
  }
  return kb::Milliseconds{ms};
}

kb::Status Completion::Create() noexcept {
  if (!created_) {
    const kb::Result<void> r = wake_.Create(1U, 0U);
    created_ = r.IsOk() || (r.Error() == kb::Status::AlreadyStarted);
  }
  return created_ ? kb::Status::Ok : kb::Status::Fault;
}

void Completion::Arm() noexcept {
  done_.store(false, std::memory_order_release);
  while (wake_.TryTake()) {
    // a wake-up from before this operation
  }
}

void Completion::Complete(uint32_t code, uint32_t value) noexcept {
  code_.store(code, std::memory_order_relaxed);
  value_.store(value, std::memory_order_relaxed);
  done_.store(true, std::memory_order_release);
  static_cast<void>(wake_.GiveFromIsr()); // at most one count: a second give is dropped
}

void Completion::Notify() noexcept {
  static_cast<void>(wake_.GiveFromIsr());
}

bool Completion::Pause(Deadline& deadline) noexcept {
  const kb::Milliseconds left = deadline.Remaining();
  const bool more = left.Count() > 0U;
  if (more) {
    // Blocks under an RTOS until a give or the slice; returns at once under the `none` backend.
    const uint32_t slice = std::min(left.Count(), kWaitSliceMs);
    if (!wake_.TakeFor(kb::Milliseconds{slice})) {
      deadline.Slept(slice); // timed out: the slice passed on the RTOS tick
    }
  }
  return more;
}

bool Completion::Wait(Deadline& deadline) noexcept {
  bool more = true;
  while (!Done() && more) {
    more = Pause(deadline);
  }
  return Done();
}

} // namespace kb::port::stm32
