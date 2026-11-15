// AdcChannel and PwmChannel: argument checks, the shared ADC instance (its life, lock and
// interrupt-ended conversion), scaling, timer ownership (the clock's timer refused, one period per
// shared timer), the timer period and the compare rounding. Platform-free; every HAL call goes
// through sdk.hpp (src/stm32_io_boundary.cpp). SPDX-License-Identifier: MIT Copyright (c) 2026 The
// port-stm32 authors
// State shared with interrupts needs lock-free std::atomic (as kb-core's SpscRing does).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstdint>

#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/adc.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/pwm.hpp"
#include "kb/rtos/time.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
constexpr uint8_t kMaxTimerChannel = 4U;
constexpr uint64_t kCounterSpan = 65536U; // 16-bit prescaler and auto-reload

[[nodiscard]] bool PinOk(PinId pin) noexcept {
  return pin.Used() && pin.Valid() && sdk::PortExists(pin.port);
}

[[nodiscard]] bool Created(const kb::Result<void>& r) noexcept {
  return r.IsOk() || (r.Error() == kb::Status::AlreadyStarted);
}

// The instance's first channel initialises it; others wait (bounded) while it does.
[[nodiscard]] uint32_t AdcShare(uint8_t instance, kb::Milliseconds wait) noexcept {
  AdcEntry& entry = sdk::State().adcs[instance];
  Deadline deadline{wait};
  bool opener = false;
  bool joined = false;
  uint32_t code = port_code::kLockTimeout;
  while (!opener && !joined && !deadline.Expired()) {
    {
      const IrqGuard guard{};
      const auto phase = static_cast<AdcPhase>(entry.phase.load(std::memory_order_relaxed));
      if (phase == AdcPhase::Open) {
        ++entry.users;
        joined = true;
      } else if (phase == AdcPhase::Closed) {
        entry.phase.store(static_cast<uint8_t>(AdcPhase::Opening), std::memory_order_relaxed);
        opener = true;
      } else {
        // another channel is opening or closing the instance
      }
    }
    if (!opener && !joined) {
      kb::rtos::SleepFor(kb::Milliseconds{1U});
    }
  }
  if (joined) {
    code = hal::kOk;
  } else if (opener) {
    if (!Created(entry.lock.Create()) || (entry.conversion.Create() != kb::Status::Ok)) {
      code = port_code::kRtosObject;
    } else if (!HalTickRuns()) {
      code = port_code::kHalTickStopped; // the HAL's enable and calibration waits use it
    } else {
      code = sdk::AdcOpen(instance, entry.conversion);
    }
    const IrqGuard guard{};
    const bool ok = code == hal::kOk;
    entry.users = ok ? 1U : 0U;
    entry.phase.store(static_cast<uint8_t>(ok ? AdcPhase::Open : AdcPhase::Closed),
                      std::memory_order_relaxed);
  } else {
    // the other channel did not finish in time
  }
  return code;
}

// One conversion under the instance's lock, ended by its interrupt.
[[nodiscard]] uint32_t AdcConvert(const AdcConfig& config, uint32_t& raw) noexcept {
  AdcEntry& entry = sdk::State().adcs[config.instance];
  uint32_t code = port_code::kLockTimeout;
  if (entry.lock.TryLockFor(config.lock_timeout)) {
    entry.conversion.Arm();
    code = sdk::AdcStart(config.instance, config.channel, config.sampling);
    if (code == hal::kOk) {
      Deadline deadline{config.conversion_timeout};
      if (entry.conversion.Wait(deadline)) {
        code = entry.conversion.Code();
        raw = entry.conversion.Value();
      } else {
        const uint32_t stop = sdk::AdcStop(config.instance);
        code = VendorCode(hal::kTimeout, stop >> 8U);
      }
    }
    entry.lock.Unlock();
  }
  return code;
}

[[nodiscard]] uint32_t AdcUnshare(uint8_t instance) noexcept {
  AdcEntry& entry = sdk::State().adcs[instance];
  bool last = false;
  {
    const IrqGuard guard{};
    if (entry.users > 0U) {
      --entry.users;
    }
    last = (entry.users == 0U) &&
           (static_cast<AdcPhase>(entry.phase.load(std::memory_order_relaxed)) == AdcPhase::Open);
    if (last) {
      entry.phase.store(static_cast<uint8_t>(AdcPhase::Opening), std::memory_order_relaxed);
    }
  }
  uint32_t code = hal::kOk;
  if (last) {
    code = sdk::AdcClose(instance);
    entry.phase.store(static_cast<uint8_t>(AdcPhase::Closed), std::memory_order_relaxed);
  }
  return code;
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

// --- AdcChannel ----------------------------------------------------------------------------------

AdcChannel::~AdcChannel() noexcept {
  static_cast<void>(Release());
}

kb::Status AdcChannel::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    const bool valid = sdk::AdcExists(config_.instance, config_.channel) &&
                       (config_.vref_mv > 0U) && (!config_.pin.Used() || PinOk(config_.pin)) &&
                       ConfiguredTimeoutValid(config_.conversion_timeout) &&
                       ConfiguredTimeoutValid(config_.lock_timeout);
    uint32_t code = valid ? sdk::AdcClockCheck() : port_code::kInvalidArgument;
    if ((code == hal::kOk) && config_.pin.Used()) {
      code = sdk::PinAnalog(config_.pin);
    }
    if (code == hal::kOk) {
      code = AdcShare(config_.instance, config_.lock_timeout);
    }
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = status == kb::Status::Ok;
    full_scale_ = ready_ ? ((uint32_t{1U} << sdk::AdcBits(config_.instance)) - 1U) : 0U;
  }
  return status;
}

kb::Result<uint32_t> AdcChannel::ReadRaw() noexcept {
  kb::Result<uint32_t> result =
      kb::Result<uint32_t>::Failure(kb::Status::NotReady, port_code::kNotReady);
  if (!ready_) {
    static_cast<void>(error_.Set(kb::Status::NotReady, port_code::kNotReady));
  } else {
    uint32_t raw = 0U;
    const uint32_t code = AdcConvert(config_, raw);
    const kb::Status status = error_.Set(MapHalStatus(code & 0xFFU), code);
    result = (status == kb::Status::Ok) ? kb::Result<uint32_t>::Success(raw)
                                        : kb::Result<uint32_t>::Failure(status, code);
  }
  return result;
}

kb::Result<int32_t> AdcChannel::ReadMillivolts() noexcept {
  const kb::Result<uint32_t> raw = ReadRaw();
  return raw.IsOk()
             ? kb::Result<int32_t>::Success(ToMillivolts(raw.Value(), full_scale_, config_.vref_mv))
             : kb::Result<int32_t>::Failure(raw.Error(), raw.Detail());
}

kb::Status AdcChannel::Release() noexcept {
  kb::Status status = kb::Status::Ok;
  if (ready_) {
    const uint32_t code = AdcUnshare(config_.instance);
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = false;
  }
  return status;
}

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
