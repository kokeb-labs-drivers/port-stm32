// Clock (the 64-bit clock over its 32-bit timer and wrap interrupt, timer ownership), BusyWait
// (the DWT spin), the waits of every port object (Deadline, Completion), the interrupt mask guard,
// the HAL time-base check and the log sinks. Platform-free; every HAL and CMSIS call goes through
// sdk.hpp (src/stm32_io_boundary.cpp).
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <algorithm>
#include <array>
// State shared with interrupts needs lock-free std::atomic (as kb-core's SpscRing does).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
// The LogSink contract takes a std::string_view (kb/interfaces/log.hpp).
// misra-lint: ignore KB-CPP-012
#include <string_view>

#include "clock_read.hpp"
#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/irq.hpp"
#include "kb/port/stm32/log.hpp"
#include "kb/port/stm32/time.hpp"
#include "kb/port/stm32/uart.hpp"
#include "kb/rtos/semaphore.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
constexpr uint64_t kUsPerSecond = 1000000U;
constexpr uint64_t kUsPerMs = 1000U;
constexpr uint32_t kHalTickCheckMs = 2U;
constexpr uint32_t kHalTickCheckPolls = 1000000U;

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
  const uint32_t first = sdk::TickMs();
  const uint64_t budget = (uint64_t{sdk::CoreClockHz()} * kHalTickCheckMs) / kUsPerMs;
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
  if ((timer < kTimers) && sdk::State().clock_on[timer].load(std::memory_order_acquire)) {
    // The flag cleared and the wrap counted with interrupts masked: no reader, not even a higher
    // priority interrupt, ever sees one without the other.
    const IrqGuard guard{};
    if (sdk::ClockWrapPending(timer)) {
      sdk::ClockClearWrap(timer);
      static_cast<void>(sdk::State().wraps[timer].fetch_add(1U, std::memory_order_acq_rel));
    }
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

// --- Deadline, Completion ------------------------------------------------------------------------

Deadline::Deadline(kb::Milliseconds budget) noexcept
    : budget_us_(uint64_t{budget.Count()} * kUsPerMs),
      max_polls_(static_cast<uint32_t>(
          std::min<uint64_t>((uint64_t{budget.Count()} + 1U) * kDeadlinePollsPerMs, UINT32_MAX))),
      clock_timer_(sdk::State().clock_timer.load(std::memory_order_acquire)) {
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

bool Deadline::Expired() noexcept {
  if (!expired_) {
    ++polls_;
    expired_ = (polls_ >= max_polls_) || (ElapsedUs() >= budget_us_);
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
    static_cast<void>(wake_.TakeFor(kb::Milliseconds{std::min(left.Count(), kWaitSliceMs)}));
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

// --- log --------------------------------------------------------------------------------------

std::size_t FormatLogLine(kb::iface::LogLevel level, std::string_view text,
                          std::span<char> out) noexcept {
  constexpr std::size_t kFrame = 4U; // "<letter> " and "\r\n"
  std::size_t n = 0U;
  if (out.size() >= kFrame) {
    char letter = 'D';
    if (level == kb::iface::LogLevel::Error) {
      letter = 'E';
    } else if (level == kb::iface::LogLevel::Warning) {
      letter = 'W';
    } else if (level == kb::iface::LogLevel::Info) {
      letter = 'I';
    } else {
      letter = 'D';
    }
    out[0] = letter;
    out[1] = ' ';
    const std::size_t body = std::min({text.size(), kLogMaxLine, out.size() - kFrame});
    for (std::size_t i = 0U; i < body; ++i) {
      out[2U + i] = text[i];
    }
    out[2U + body] = '\r';
    out[3U + body] = '\n';
    n = kFrame + body;
  }
  return n;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract calls a member
void ItmLogSink::Write(kb::iface::LogLevel level, std::string_view text) noexcept {
  std::array<char, kLogMaxLine + 4U> line{};
  const std::size_t n = FormatLogLine(level, text, line);
  bool sent = sdk::ItmEnabled();
  for (std::size_t i = 0U; sent && (i < n); ++i) {
    uint32_t spins = 0U;
    while (!sdk::ItmPortFree() && (spins < kItmWaitSpins)) {
      ++spins; // a bounded wait for the probe to take the last character
    }
    sent = sdk::ItmPortFree();
    if (sent) {
      sdk::ItmWrite(line[i]);
    }
  }
}

void UartLogSink::Write(kb::iface::LogLevel level, std::string_view text) noexcept {
  std::array<char, kLogMaxLine + 4U> line{};
  const std::size_t n = FormatLogLine(level, text, line);
  std::array<uint8_t, kLogMaxLine + 4U> bytes{};
  for (std::size_t i = 0U; i < n; ++i) {
    bytes[i] = std::bit_cast<uint8_t>(line[i]); // the same bits, no numeric conversion
  }
  static_cast<void>(uart_.Write(std::span<const uint8_t>{bytes.data(), n}));
}

} // namespace kb::port::stm32
