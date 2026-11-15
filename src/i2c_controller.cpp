// I2cController: argument checks, the TIMINGR computation, the controller lock, the
// interrupt-driven transfers and their bounded waits, failure classification, the handle restore
// after a failure and the bit-banged bus recovery. Platform-free; every HAL call goes through
// sdk.hpp (src/stm32_bus_boundary.cpp, src/stm32_io_boundary.cpp). SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/i2c.hpp"
#include "kb/port/stm32/time.hpp"
#include "kb/rtos/mutex.hpp"
#include "kb/utils/status.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
using kb::iface::I2cFailure;
constexpr uint32_t kMinClockHz = 10000U;
constexpr uint32_t kMaxClockHz = 1000000U;         // Fast-mode Plus (I2C v2: G4, H7, L4)
constexpr uint32_t kMaxClockV1Hz = 400000U;        // Fast mode (I2C v1: F4)
constexpr std::size_t kMaxTransferBytes = 0xFFFFU; // the HAL's uint16_t length
constexpr uint32_t kRecoveryClocks = 9U;
constexpr uint32_t kHalfPeriodUs = 5U; // 100 kHz while recovering
// HAL_I2C_ERROR_AF (acknowledge failure): bit 2 of ErrorCode on every family.
constexpr uint32_t kErrorAf = 0x04U;
// HAL_I2C_ERROR_TIMEOUT: bit 5 of ErrorCode on every family.
constexpr uint32_t kErrorTimeout = 0x20U;

[[nodiscard]] bool Created(const kb::Result<void>& r) noexcept {
  return r.IsOk() || (r.Error() == kb::Status::AlreadyStarted);
}

// The I2C specification's timings of one mode, in nanoseconds.
struct ModeTimes {
  uint32_t low_ns;
  uint32_t high_ns;
  uint32_t setup_ns;
  uint32_t rise_ns;
  uint32_t fall_ns;
};
constexpr ModeTimes kStandard{
    .low_ns = 4700U, .high_ns = 4000U, .setup_ns = 250U, .rise_ns = 1000U, .fall_ns = 300U};
constexpr ModeTimes kFast{
    .low_ns = 1300U, .high_ns = 600U, .setup_ns = 100U, .rise_ns = 300U, .fall_ns = 300U};
constexpr ModeTimes kFastPlus{
    .low_ns = 500U, .high_ns = 260U, .setup_ns = 50U, .rise_ns = 120U, .fall_ns = 120U};

// The specification's mode of a bus clock: standard to 100 kHz, fast to 400 kHz, fast-mode plus
// above.
[[nodiscard]] const ModeTimes& ModeOf(uint32_t bus_hz) noexcept {
  constexpr uint32_t kStandardMax = 100000U;
  constexpr uint32_t kFastMax = 400000U;
  const ModeTimes* m = &kFastPlus;
  if (bus_hz <= kStandardMax) {
    m = &kStandard;
  } else if (bus_hz <= kFastMax) {
    m = &kFast;
  } else {
    m = &kFastPlus;
  }
  return *m;
}

[[nodiscard]] uint32_t CeilDiv(uint64_t num, uint64_t den) noexcept {
  return static_cast<uint32_t>((num + den - 1U) / den);
}

// The first failure of a sequence of calls.
[[nodiscard]] uint32_t First(uint32_t so_far, uint32_t next) noexcept {
  return (so_far != hal::kOk) ? so_far : next;
}

[[nodiscard]] bool AddressValid(uint8_t address) noexcept {
  return (address >= kI2cFirstAddress) && (address <= kI2cLastAddress);
}

// Fields for one prescaler; ok is false when they do not fit their widths.
[[nodiscard]] I2cTiming TryPrescaler(uint32_t presc, uint64_t kernel_hz, uint32_t bus_hz,
                                     const ModeTimes& m, bool& ok) noexcept {
  constexpr uint64_t kNsPerSecond = 1000000000U;
  const uint64_t tick_ns_x =
      (presc + 1U) * kNsPerSecond; // one prescaled tick is tick_ns_x / kernel ns
  I2cTiming t{};
  t.presc = presc;
  const uint32_t scldel = CeilDiv((m.rise_ns + m.setup_ns) * kernel_hz, tick_ns_x);
  const uint32_t sdadel = CeilDiv(m.fall_ns * kernel_hz, tick_ns_x);
  const auto period = static_cast<uint32_t>(kernel_hz / ((presc + 1U) * uint64_t{bus_hz}));
  const uint32_t low = CeilDiv(m.low_ns * kernel_hz, tick_ns_x);
  const uint32_t high = CeilDiv(m.high_ns * kernel_hz, tick_ns_x);
  ok = (scldel >= 1U) && (scldel <= 16U) && (sdadel <= 15U) && (period >= (low + high)) &&
       (low >= 1U) && (high >= 1U);
  if (ok) {
    const uint32_t spare = period - low - high; // spread the rest between low and high
    const uint32_t scll = low + (spare / 2U);
    const uint32_t sclh = period - scll;
    ok = (scll >= 1U) && (scll <= 256U) && (sclh >= 1U) && (sclh <= 256U);
    t.scldel = scldel - 1U;
    t.sdadel = sdadel;
    t.scll = scll - 1U;
    t.sclh = sclh - 1U;
  }
  return t;
}
} // namespace

I2cTiming ComputeI2cTiming(uint32_t kernel_hz, uint32_t bus_hz, bool& ok) noexcept {
  constexpr uint32_t kPrescalers = 16U;
  const ModeTimes& m = ModeOf(bus_hz);
  I2cTiming t{};
  ok = false;
  if ((bus_hz >= kMinClockHz) && (bus_hz <= kMaxClockHz) && (kernel_hz >= (bus_hz * 4U))) {
    for (uint32_t presc = 0U; (presc < kPrescalers) && !ok; ++presc) {
      t = TryPrescaler(presc, kernel_hz, bus_hz, m, ok);
    }
  }
  return t;
}

I2cController::~I2cController() noexcept {
  static_cast<void>(Release());
}

bool I2cController::configValid() const noexcept {
  return sdk::I2cExists(config_.instance) && config_.scl.Used() && config_.scl.Valid() &&
         config_.sda.Used() && config_.sda.Valid() && sdk::PortExists(config_.scl.port) &&
         sdk::PortExists(config_.sda.port) && (config_.clock_hz >= kMinClockHz) &&
         (config_.clock_hz <= (sdk::I2cHasTimingRegister() ? kMaxClockHz : kMaxClockV1Hz)) &&
         ConfiguredTimeoutValid(config_.timeout);
}

uint32_t I2cController::timingRegister(bool& ok) const noexcept {
  uint32_t timing = config_.timing;
  ok = true;
  if (sdk::I2cHasTimingRegister() && (timing == 0U)) {
    const uint32_t kernel = (config_.kernel_clock_hz != 0U)
                                ? config_.kernel_clock_hz
                                : sdk::I2cKernelClockHz(config_.instance);
    timing = ComputeI2cTiming(kernel, config_.clock_hz, ok).Register();
  }
  return timing;
}

kb::Status I2cController::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    bool timing_ok = false;
    uint32_t code = hal::kOk;
    if (!configValid()) {
      code = port_code::kInvalidArgument;
    } else if (sdk::I2cKernelClockHz(config_.instance) == 0U) {
      code = port_code::kNoClock; // the peripheral's clock source is not running
    } else {
      const uint32_t timing = timingRegister(timing_ok);
      if (!timing_ok) {
        code = port_code::kInvalidArgument;
      } else if (!Created(mutex_.Create()) || (done_.Create() != kb::Status::Ok)) {
        code = port_code::kRtosObject;
      } else {
        code = sdk::I2cOpen(config_, timing, done_); // never probes a device
      }
    }
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = status == kb::Status::Ok;
    if (ready_) {
      const IrqGuard guard{};
      stats_ = I2cStats{};
    }
    last_failure_.store(I2cFailure::None, std::memory_order_relaxed);
  }
  return status;
}

kb::Status I2cController::Release() noexcept {
  kb::Status status = kb::Status::Ok;
  if (ready_) {
    const uint32_t code = sdk::I2cClose(config_.instance);
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = false;
  }
  return status;
}

I2cStats I2cController::Stats() const noexcept {
  const IrqGuard guard{};
  return stats_;
}

void I2cController::count(uint32_t I2cStats::* field) noexcept {
  const IrqGuard guard{};
  ++(stats_.*field);
}

kb::Status I2cController::Write(uint8_t address, std::span<const uint8_t> tx) noexcept {
  return transfer(tx.empty() ? Op::Probe : Op::Write, address, tx, std::span<uint8_t>{});
}

kb::Status I2cController::WriteRead(uint8_t address, std::span<const uint8_t> tx,
                                    std::span<uint8_t> rx) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  if (tx.empty() || rx.empty()) {
    static_cast<void>(error_.Set(status, port_code::kInvalidArgument));
  } else {
    status = transfer(Op::WriteRead, address, tx, rx);
  }
  return status;
}

kb::Status I2cController::Read(uint8_t address, std::span<uint8_t> rx) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  if (rx.empty()) {
    static_cast<void>(error_.Set(status, port_code::kInvalidArgument));
  } else {
    status = transfer(Op::Read, address, std::span<const uint8_t>{}, rx);
  }
  return status;
}

uint32_t I2cController::await(uint32_t started, Deadline& deadline) noexcept {
  uint32_t code = started;
  if (started == hal::kOk) {
    code = done_.Wait(deadline) ? done_.Code() : VendorCode(hal::kTimeout, kErrorTimeout);
  }
  return code;
}

uint32_t I2cController::run(Op op, uint8_t address, std::span<const uint8_t> tx,
                            std::span<uint8_t> rx) noexcept {
  Deadline deadline{config_.timeout};
  const uint8_t instance = config_.instance;
  sdk::I2cPrepare(instance); // flush TXDR/RXDR, clear stale CR2 transfer fields
  uint32_t code = hal::kOk;
  done_.Arm();
  if (op == Op::Read) {
    code = await(sdk::I2cStartReceive(instance, address, rx, false), deadline);
  } else {
    const bool hold = op == Op::WriteRead; // no STOP: a repeated START and the read follow
    code = await(sdk::I2cStartTransmit(instance, address, tx, hold), deadline);
    if ((code == hal::kOk) && hold) {
      done_.Arm();
      code = await(sdk::I2cStartReceive(instance, address, rx, true), deadline);
    }
  }
  return code;
}

kb::Status I2cController::transfer(Op op, uint8_t address, std::span<const uint8_t> tx,
                                   std::span<uint8_t> rx) noexcept {
  kb::Status status = kb::Status::NotReady;
  if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else if (!AddressValid(address) || (tx.size() > kMaxTransferBytes) ||
             (rx.size() > kMaxTransferBytes)) {
    status = error_.Set(kb::Status::InvalidArgument, port_code::kInvalidArgument);
  } else {
    const kb::rtos::LockGuard lock{mutex_, config_.timeout};
    if (!lock.OwnsLock()) {
      count(&I2cStats::lock_timeouts);
      status = error_.Set(kb::Status::Busy, port_code::kLockTimeout);
    } else {
      count(&I2cStats::transfers);
      status = classify(run(op, address, tx, rx), op, address);
      if (status != kb::Status::Ok) {
        count(&I2cStats::failures);
      }
    }
  }
  return status;
}

bool I2cController::linesIdle() const noexcept {
  const bool sda = sdk::PinRead(config_.sda); // both read, always
  const bool scl = sdk::PinRead(config_.scl);
  return sda && scl;
}

kb::iface::I2cFailure I2cController::failureOf(uint32_t code, Op op, uint8_t address) noexcept {
  const uint32_t hal_status = code & 0xFFU;
  const uint32_t error_code = code >> 8U;
  I2cFailure failure = I2cFailure::Bus;
  if (hal_status == hal::kOk) {
    failure = I2cFailure::None;
  } else if ((error_code & kErrorAf) != 0U) {
    if ((op == Op::Read) || (op == Op::Probe)) {
      failure = I2cFailure::AddressNack; // a read's or a probe's only acknowledge is the address's
    } else if (config_.nack_probe) {
      // One address-only probe tells an address NACK from a data NACK (opt-in: see i2c.hpp).
      restoreLocked();
      Deadline probe_deadline{config_.timeout};
      done_.Arm();
      const uint32_t probe =
          await(sdk::I2cStartTransmit(config_.instance, address, std::span<const uint8_t>{}, false),
                probe_deadline);
      failure = ((probe & 0xFFU) == hal::kOk) ? I2cFailure::DataNack : I2cFailure::AddressNack;
    } else {
      failure = I2cFailure::DataNack; // the address or a data byte: not split without a probe
    }
  } else if (hal_status == hal::kTimeout) {
    failure = linesIdle() ? I2cFailure::Timeout : I2cFailure::BusStuck;
  } else {
    // A bus error, arbitration loss, or HAL_BUSY (the BUSY flag set before the start).
    failure = linesIdle() ? I2cFailure::Bus : I2cFailure::BusStuck;
  }
  return failure;
}

kb::Status I2cController::statusOf(kb::iface::I2cFailure failure) noexcept {
  kb::Status status = kb::Status::BusError;
  switch (failure) {
  case I2cFailure::None:
    status = kb::Status::Ok;
    break;
  case I2cFailure::AddressNack:
    status = kb::Status::NoDevice;
    break;
  case I2cFailure::DataNack:
    status = kb::Status::Nack;
    break;
  case I2cFailure::Timeout:
    status = kb::Status::Timeout;
    break;
  case I2cFailure::Bus:
  case I2cFailure::BusStuck:
  default:
    status = kb::Status::BusError;
    break;
  }
  return status;
}

void I2cController::restoreLocked() noexcept {
  // The handle back to ready, error flags cleared, data registers flushed. The peripheral is reset
  // (PE toggled) only while BUSY stays set: a reset after a plain NACK is never needed.
  sdk::I2cRestore(config_.instance);
  if (sdk::I2cBusy(config_.instance)) {
    count(&I2cStats::recoveries);
    static_cast<void>(sdk::I2cReset(config_.instance));
  }
}

kb::Status I2cController::classify(uint32_t code, Op op, uint8_t address) noexcept {
  I2cFailure failure = failureOf(code, op, address);
  if (failure != I2cFailure::None) {
    restoreLocked();
  }
  if ((failure == I2cFailure::BusStuck) && config_.auto_recover) {
    static_cast<void>(recoverLocked());
    failure = I2cFailure::BusStuck; // the transfer failed even if the bus is free now
  }
  last_failure_.store(failure, std::memory_order_relaxed);
  return error_.Set(statusOf(failure), code);
}

kb::Status I2cController::recoverLocked() noexcept {
  count(&I2cStats::recoveries);
  BusyWait wait{};
  const Pull pull = config_.internal_pullups ? Pull::Up : Pull::None; // the configured pull stays
  uint32_t code = sdk::I2cClose(config_.instance);
  // SCL and SDA as open-drain outputs, released (high): clock SCL until a slave lets SDA go.
  code = First(code, sdk::PinOutput(config_.scl, true, pull, Speed::Low, true));
  code = First(code, sdk::PinOutput(config_.sda, true, pull, Speed::Low, true));
  for (uint32_t i = 0U; i < kRecoveryClocks; ++i) {
    if (!sdk::PinRead(config_.sda)) { // clocks only while a slave holds SDA
      sdk::PinWrite(config_.scl, false);
      wait.WaitUs(kb::Microseconds{kHalfPeriodUs});
      sdk::PinWrite(config_.scl, true);
      wait.WaitUs(kb::Microseconds{kHalfPeriodUs});
    }
  }
  // A STOP: SDA low to high while SCL is high.
  sdk::PinWrite(config_.sda, false);
  wait.WaitUs(kb::Microseconds{kHalfPeriodUs});
  sdk::PinWrite(config_.sda, true);
  wait.WaitUs(kb::Microseconds{kHalfPeriodUs});
  const bool idle = linesIdle();
  bool timing_ok = false;
  const uint32_t timing = timingRegister(timing_ok);
  code = First(code, sdk::I2cOpen(config_, timing, done_));
  kb::Status status = MapHalStatus(code & 0xFFU);
  if ((status == kb::Status::Ok) && !idle) {
    status = kb::Status::BusError; // a device still holds a line low (SCL or SDA)
  }
  last_failure_.store((status == kb::Status::Ok) ? I2cFailure::None : I2cFailure::BusStuck,
                      std::memory_order_relaxed);
  return error_.Set(status, code);
}

kb::Status I2cController::RecoverBus() noexcept {
  kb::Status status = kb::Status::NotReady;
  if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else {
    const kb::rtos::LockGuard lock{mutex_, config_.timeout};
    if (!lock.OwnsLock()) {
      count(&I2cStats::lock_timeouts);
      status = error_.Set(kb::Status::Busy, port_code::kLockTimeout);
    } else {
      status = recoverLocked();
    }
  }
  return status;
}

} // namespace kb::port::stm32
