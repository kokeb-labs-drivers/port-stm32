// PwmChannel: argument checks, timer ownership (the clock's timer refused, one period per shared
// timer), the timer period and the compare rounding. Platform-free; every HAL call goes through
// sdk.hpp (src/stm32_io_boundary.cpp). SPDX-License-Identifier: MIT Copyright (c) 2026 The
// port-stm32 authors
#include <cstdint>

#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/pwm.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
constexpr uint8_t kMaxTimerChannel = 4U;
constexpr uint64_t kCounterSpan = 65536U; // 16-bit prescaler and auto-reload

[[nodiscard]] bool PinOk(PinId pin) noexcept {
  return pin.Used() && pin.Valid() && sdk::PortExists(pin.port);
}

// Takes a channel of `timer` for PWM at `period`: refused on the clock's timer, and at another
// period than the timer's other channels run. `first` is true for the timer's first channel.
[[nodiscard]] uint32_t TimerTake(uint8_t timer, const PwmPeriod& period, bool& first) noexcept {
  const IrqGuard guard{};
  TimerEntry& entry = sdk::State().timers[timer];
  uint32_t code = hal::kOk;
  first = false;
  if (entry.use == TimerUse::Clock) {
    code = port_code::kOwned;
  } else if ((entry.use == TimerUse::Pwm) && ((entry.period.prescaler != period.prescaler) ||
                                              (entry.period.reload != period.reload))) {
    code = port_code::kConflict; // one period per timer: its channels share PSC and ARR
  } else {
    first = entry.use == TimerUse::None;
    entry.use = TimerUse::Pwm;
    entry.period = period;
    ++entry.users;
  }
  return code;
}

// The timer clock of a channel: configured, or the timer's from the RCC.
[[nodiscard]] uint32_t PwmTimerHz(const PwmConfig& config) noexcept {
  return (config.timer_clock_hz != 0U) ? config.timer_clock_hz : sdk::TimerClockHz(config.timer);
}

// Gives a channel back; true when it was the timer's last.
[[nodiscard]] bool TimerGive(uint8_t timer) noexcept {
  const IrqGuard guard{};
  TimerEntry& entry = sdk::State().timers[timer];
  if (entry.users > 0U) {
    --entry.users;
  }
  const bool last = entry.users == 0U;
  if (last) {
    entry.use = TimerUse::None;
  }
  return last;
}

// Takes the timer channel at `period`: the timer initialised by its first channel, the channel
// started; everything given back on a failure.
[[nodiscard]] uint32_t PwmOpen(const PwmConfig& config, const PwmPeriod& period) noexcept {
  bool first = false;
  uint32_t code = TimerTake(config.timer, period, first);
  if (code == hal::kOk) {
    const uint32_t init = first ? sdk::PwmInit(config.timer, period) : hal::kOk;
    code = (init == hal::kOk) ? sdk::PwmStart(config.timer, config.channel, config.pin) : init;
    if ((code != hal::kOk) && TimerGive(config.timer) && (init == hal::kOk)) {
      static_cast<void>(sdk::PwmDeInit(config.timer)); // the timer's last channel failed
    }
  }
  return code;
}
} // namespace

// --- PwmChannel ----------------------------------------------------------------------------------

PwmPeriod ComputePwmPeriod(uint32_t timer_hz, uint32_t hz, bool& ok) noexcept {
  PwmPeriod p{};
  ok = (hz > 0U) && (hz <= (timer_hz / 2U));
  if (ok) {
    const uint64_t total = timer_hz / uint64_t{hz}; // counts per period at prescaler 1
    // total < 2^32, so the prescaler is at most 2^16: always within the 16-bit PSC register.
    const uint64_t prescaler = (total + kCounterSpan - 1U) / kCounterSpan;
    const uint64_t reload = ((timer_hz / prescaler) + (hz / 2U)) / hz; // rounded to nearest
    p.prescaler = static_cast<uint32_t>(prescaler - 1U);
    p.reload = static_cast<uint32_t>(((reload > kCounterSpan) ? kCounterSpan : reload) - 1U);
    ok = p.reload >= 1U;
  }
  return p;
}

PwmChannel::~PwmChannel() noexcept {
  static_cast<void>(Release());
}

kb::Status PwmChannel::applyCompare() noexcept {
  const kb::iface::Duty idle =
      config_.idle_high ? kb::iface::Duty{kb::iface::Duty::kFull} : kb::iface::Duty{0U};
  const kb::iface::Duty level = enabled_ ? duty_ : idle;
  sdk::PwmSetCompare(config_.timer, config_.channel, CompareFor(level, period_.reload));
  return error_.Clear();
}

kb::Status PwmChannel::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    const bool valid = sdk::TimerExists(config_.timer) && (config_.channel >= 1U) &&
                       (config_.channel <= kMaxTimerChannel) && PinOk(config_.pin);
    timer_hz_ = valid ? PwmTimerHz(config_) : 0U;
    bool ok = valid && (timer_hz_ != 0U);
    PwmPeriod period{};
    if (ok) {
      period = ComputePwmPeriod(timer_hz_, config_.frequency_hz, ok);
    }
    uint32_t code = port_code::kInvalidArgument;
    if (valid && (timer_hz_ == 0U)) {
      code = port_code::kNoClock;
    } else if (ok) {
      code = PwmOpen(config_, period);
    } else {
      // refused: the configuration or the frequency
    }
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = status == kb::Status::Ok;
    if (ready_) {
      period_ = period;
      enabled_ = false;
      status = applyCompare();
    }
  }
  return status;
}

kb::Status PwmChannel::SetFrequencyHz(uint32_t hz) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  bool ok = hz > 0U;
  PwmPeriod period = period_;
  if (ready_ && ok) {
    period = ComputePwmPeriod(timer_hz_, hz, ok);
  }
  if (!ok) {
    static_cast<void>(error_.Set(status, port_code::kInvalidArgument));
  } else if (!ready_) {
    config_.frequency_hz = hz; // used by EnsureReady()
    status = error_.Clear();
  } else {
    uint32_t code = hal::kOk;
    {
      const IrqGuard guard{};
      TimerEntry& entry = sdk::State().timers[config_.timer];
      if (entry.users > 1U) {
        code = port_code::kConflict; // the timer's other channels run at its period
      } else {
        entry.period = period;
      }
    }
    code = (code == hal::kOk) ? sdk::PwmSetPeriod(config_.timer, period) : code;
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    if (status == kb::Status::Ok) {
      config_.frequency_hz = hz;
      period_ = period;
      status = applyCompare();
    }
  }
  return status;
}

kb::Status PwmChannel::SetDuty(kb::iface::Duty duty) noexcept {
  kb::Status status = kb::Status::NotReady;
  if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else {
    duty_ = duty;
    status = applyCompare();
  }
  return status;
}

kb::Status PwmChannel::Enable(bool on) noexcept {
  kb::Status status = kb::Status::NotReady;
  if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else {
    enabled_ = on;
    status = applyCompare();
  }
  return status;
}

kb::Status PwmChannel::Release() noexcept {
  kb::Status status = kb::Status::Ok;
  if (ready_) {
    uint32_t code = sdk::PwmStop(config_.timer, config_.channel);
    if (TimerGive(config_.timer)) {
      const uint32_t deinit = sdk::PwmDeInit(config_.timer);
      code = (code == hal::kOk) ? deinit : code;
    }
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = false;
    enabled_ = false;
  }
  return status;
}

} // namespace kb::port::stm32
