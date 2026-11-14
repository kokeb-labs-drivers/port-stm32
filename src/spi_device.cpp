// SpiBus and SpiDevice: argument checks, the prescaler, the bus lock, per-device mode and clock,
// chip select around every frame, chunks of 65535 bytes and re-initialisation after a timeout.
// Platform-free; every HAL call goes through sdk.hpp (src/stm32_bus_boundary.cpp).
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/interfaces/spi.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/spi.hpp"
#include "kb/utils/status.hpp"
#include "sdk.hpp"

namespace kb::port::stm32 {

namespace {
constexpr uint8_t kMaxMode = 3U;
constexpr uint8_t kMaxExponent = 7U;   // divide by 256
constexpr std::size_t kChunk = 65535U; // HAL_SPI_* take a uint16_t size

[[nodiscard]] bool PinOk(PinId pin) noexcept {
  return pin.Used() && pin.Valid() && sdk::PortExists(pin.port);
}
} // namespace

uint8_t SpiPrescalerExponent(uint32_t kernel_hz, uint32_t hz, bool& ok) noexcept {
  uint8_t exponent = 0U;
  while ((exponent < kMaxExponent) && ((kernel_hz >> (exponent + 1U)) > hz)) {
    ++exponent;
  }
  ok = (hz > 0U) && ((kernel_hz >> (exponent + 1U)) <= hz) && (kernel_hz > 0U);
  return exponent;
}

// --- SpiBus --------------------------------------------------------------------------------------

SpiBus::~SpiBus() noexcept {
  static_cast<void>(Release());
}

kb::Status SpiBus::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    const bool valid = sdk::SpiExists(config_.instance) && PinOk(config_.sck) &&
                       (PinOk(config_.mosi) || PinOk(config_.miso)) &&
                       (!config_.mosi.Used() || PinOk(config_.mosi)) &&
                       (!config_.miso.Used() || PinOk(config_.miso));
    if (!valid) {
      status = error_.Set(kb::Status::InvalidArgument, port_code::kInvalidArgument);
    } else {
      static_cast<void>(mutex_.Create());
      const uint32_t code = sdk::SpiOpen(config_);
      status = error_.Set(MapHalStatus(code & 0xFFU), code);
      ready_ = status == kb::Status::Ok;
      kernel_hz_ = (config_.kernel_clock_hz != 0U) ? config_.kernel_clock_hz
                                                   : sdk::SpiKernelClockHz(config_.instance);
      mode_ = 0xFFU;
      prescaler_ = 0xFFU;
    }
  }
  return status;
}

kb::Status SpiBus::Release() noexcept {
  kb::Status status = kb::Status::Ok;
  if (ready_) {
    const uint32_t code = sdk::SpiClose(config_.instance);
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = false;
  }
  return status;
}

uint32_t SpiBus::SelectLocked(uint8_t mode, uint8_t prescaler, bool force) noexcept {
  uint32_t code = hal::kOk;
  if (force || (mode != mode_) || (prescaler != prescaler_)) {
    code = sdk::SpiConfigure(config_.instance, mode, prescaler);
    mode_ = (code == hal::kOk) ? mode : 0xFFU;
    prescaler_ = (code == hal::kOk) ? prescaler : 0xFFU;
  }
  return code;
}

// --- SpiDevice -----------------------------------------------------------------------------------

SpiDevice::~SpiDevice() noexcept {
  static_cast<void>(Release());
}

bool SpiDevice::configValid() const noexcept {
  return PinOk(config_.cs) && (config_.mode <= kMaxMode) && (config_.clock_hz > 0U) &&
         (config_.timeout.Count() > 0U);
}

uint32_t SpiDevice::ActualClockHz() const noexcept {
  return bus_.KernelClockHz() >> (prescaler_ + 1U);
}

uint32_t SpiDevice::claimCs() const noexcept {
  const uint32_t claim = sdk::PinClaim(config_.cs);
  uint32_t code = claim;
  if (claim == hal::kOk) {
    code = sdk::PinOutput(config_.cs, false, Pull::None, Speed::High, true); // released: high
    if (code != hal::kOk) {
      sdk::PinUnclaim(config_.cs);
    }
  }
  return code;
}

kb::Status SpiDevice::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    bool ok = false;
    if (!configValid()) {
      status = error_.Set(kb::Status::InvalidArgument, port_code::kInvalidArgument);
    } else if (bus_.EnsureReady() != kb::Status::Ok) {
      status = error_.Set(kb::Status::NotReady, port_code::kNotReady);
    } else {
      prescaler_ = SpiPrescalerExponent(bus_.KernelClockHz(), config_.clock_hz, ok);
      const uint32_t code = ok ? claimCs() : port_code::kInvalidArgument;
      status = error_.Set(MapHalStatus(code & 0xFFU), code);
      ready_ = status == kb::Status::Ok;
    }
  }
  return status;
}

kb::Status SpiDevice::Release() noexcept {
  if (ready_) {
    sdk::PinReset(config_.cs);
    sdk::PinUnclaim(config_.cs);
    ready_ = false;
  }
  return error_.Clear();
}

uint32_t SpiDevice::frame(std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept {
  const std::size_t total = std::max(tx.size(), rx.size());
  uint32_t code = bus_.SelectLocked(config_.mode, prescaler_, rebuild_);
  rebuild_ = false;
  std::size_t done = 0U;
  if (code == hal::kOk) {
    sdk::PinWrite(config_.cs, false);
    while ((code == hal::kOk) && (done < total)) {
      const std::size_t n = std::min(kChunk, total - done);
      const std::span<const uint8_t> t = tx.empty() ? tx : tx.subspan(done, n);
      const std::span<uint8_t> r = rx.empty() ? rx : rx.subspan(done, n);
      code = sdk::SpiTransfer(bus_.Config().instance, t, r, config_.timeout.Count());
      done += n;
    }
    sdk::PinWrite(config_.cs, true);
  }
  return code;
}

kb::Status SpiDevice::finish(uint32_t code) noexcept {
  const uint32_t hal_status = code & 0xFFU;
  kb::Status status = MapHalStatus(hal_status);
  if (hal_status == hal::kError) {
    status = kb::Status::BusError; // mode fault, overrun, CRC or frame error (ErrorCode)
  }
  if (status != kb::Status::Ok) {
    ++stats_.failures;
  }
  if (status == kb::Status::Timeout) {
    rebuild_ = true; // the HAL aborted the transfer: initialise the peripheral before the next one
    ++stats_.rebuilds;
  }
  return error_.Set(status, code);
}

kb::Status SpiDevice::Transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  if (!kb::iface::SpiSpansValid(tx, rx) || (tx.empty() && rx.empty())) {
    static_cast<void>(error_.Set(status, port_code::kInvalidArgument));
  } else if (!ready_) {
    status = error_.Set(kb::Status::NotReady, port_code::kNotReady);
  } else if (!bus_.TryLockFor(config_.timeout)) {
    ++stats_.lock_timeouts;
    status = error_.Set(kb::Status::Busy, port_code::kLockTimeout);
  } else {
    ++stats_.transfers;
    std::fill(rx.begin(), rx.end(), uint8_t{0U}); // a read-only frame clocks these zeros out
    status = finish(frame(tx, rx));
    bus_.Unlock();
  }
  return status;
}

kb::Status SpiDevice::SetClockHz(uint32_t hz) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  bool ok = (hz > 0U);
  const uint8_t exponent =
      (ready_ && ok) ? SpiPrescalerExponent(bus_.KernelClockHz(), hz, ok) : prescaler_;
  if (!ok) {
    static_cast<void>(error_.Set(status, port_code::kInvalidArgument));
  } else {
    config_.clock_hz = hz; // the next frame selects the new prescaler
    prescaler_ = exponent;
    status = error_.Clear();
  }
  return status;
}

} // namespace kb::port::stm32
