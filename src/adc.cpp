// AdcChannel: argument checks, the shared ADC instance (its life, lock and interrupt-ended
// conversion) and the scaling. Platform-free; every HAL call goes through sdk.hpp
// (src/boundary/adc.cpp). SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
// State shared with interrupts needs lock-free std::atomic (as kb-core's SpscRing does).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstdint>

#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/adc.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/rtos/time.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
[[nodiscard]] bool PinOk(PinId pin) noexcept {
  return pin.Used() && pin.Valid() && sdk::PortExists(pin.port);
}

[[nodiscard]] bool Created(const kb::Result<void>& r) noexcept {
  return r.IsOk() || (r.Error() == kb::Status::AlreadyStarted);
}

// The instance's first channel initialises it; others wait (bounded) while it does.
[[nodiscard]] uint32_t AdcShare(uint8_t instance, kb::Milliseconds wait,
                                uint8_t priority) noexcept {
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
      deadline.Slept(1U);
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
      code = sdk::AdcOpen(instance, entry.conversion, priority);
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
                       ConfiguredTimeoutValid(config_.lock_timeout) &&
                       RtosIrqPriorityValid(config_.irq_priority);
    uint32_t code = valid ? sdk::AdcClockCheck() : port_code::kInvalidArgument;
    if ((code == hal::kOk) && config_.pin.Used()) {
      code = sdk::PinAnalog(config_.pin);
    }
    if (code == hal::kOk) {
      code = AdcShare(config_.instance, config_.lock_timeout, config_.irq_priority);
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

} // namespace kb::port::stm32
