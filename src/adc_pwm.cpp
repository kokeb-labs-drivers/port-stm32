// AdcChannel and PwmChannel: argument checks, the shared ADC instance, scaling, the timer period
// and the compare rounding. Platform-free; every HAL call goes through sdk.hpp
// (src/stm32_io_boundary.cpp). SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32
// authors
#include <cstdint>

#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/adc.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/pwm.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "sdk.hpp"

namespace kb::port::stm32 {

namespace {
constexpr uint8_t kMaxTimerChannel = 4U;
constexpr uint64_t kCounterSpan = 65536U; // 16-bit prescaler and auto-reload

[[nodiscard]] bool PinOk(PinId pin) noexcept {
  return pin.Used() && pin.Valid() && sdk::PortExists(pin.port);
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
                       (config_.conversion_timeout.Count() > 0U);
    uint32_t code = valid ? hal::kOk : port_code::kInvalidArgument;
    if ((code == hal::kOk) && config_.pin.Used()) {
      code = sdk::PinAnalog(config_.pin);
    }
    if (code == hal::kOk) {
      code = sdk::AdcOpen(config_.instance);
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
    const uint32_t code =
        sdk::AdcConvert(config_.instance, config_.channel, config_.sampling,
                        config_.conversion_timeout.Count(), config_.lock_timeout.Count(), raw);
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
    const uint32_t code = sdk::AdcClose(config_.instance);
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
    bool ok = sdk::TimerExists(config_.timer) && (config_.channel >= 1U) &&
              (config_.channel <= kMaxTimerChannel) && PinOk(config_.pin);
    timer_hz_ =
        (config_.timer_clock_hz != 0U) ? config_.timer_clock_hz : sdk::TimerClockHz(config_.timer);
    const PwmPeriod period =
        ok ? ComputePwmPeriod(timer_hz_, config_.frequency_hz, ok) : PwmPeriod{};
    const uint32_t code = ok ? sdk::PwmOpen(config_, period) : port_code::kInvalidArgument;
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
  const PwmPeriod period = (ready_ && ok) ? ComputePwmPeriod(timer_hz_, hz, ok) : period_;
  if (!ok) {
    static_cast<void>(error_.Set(status, port_code::kInvalidArgument));
  } else if (!ready_) {
    config_.frequency_hz = hz; // used by EnsureReady()
    status = error_.Clear();
  } else {
    const uint32_t code = sdk::PwmSetPeriod(config_.timer, period);
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
    const uint32_t code = sdk::PwmClose(config_.timer, config_.channel);
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = false;
    enabled_ = false;
  }
  return status;
}

} // namespace kb::port::stm32
