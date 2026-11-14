// Uart: argument checks, the reader and writer locks, the receive ring fed by the interrupt,
// latched errors, RS-485 direction and the bounded waits. Platform-free; every HAL call goes
// through sdk.hpp (src/stm32_bus_boundary.cpp). SPDX-License-Identifier: MIT Copyright (c) 2026 The
// port-stm32 authors
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/uart.hpp"
#include "kb/rtos/mutex.hpp"
#include "kb/rtos/time.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"
#include "sdk.hpp"

namespace kb::port::stm32 {

namespace {
constexpr uint8_t kMinDataBits = 7U;
constexpr uint8_t kMaxDataBits = 8U;
constexpr uint32_t kMaxBaud = 12500000U;
constexpr std::size_t kMaxChunkBytes = 0xFFFFU;
constexpr uint32_t kOverflowFlags = uart_flags::kOverrun | uart_flags::kRingFull;

[[nodiscard]] kb::Result<std::size_t> Fail(LastErrorRecord& error, kb::Status status,
                                           uint32_t code) noexcept {
  static_cast<void>(error.Set(status, code));
  return kb::Result<std::size_t>::Failure(status, code);
}

[[nodiscard]] bool PinOk(PinId pin) noexcept {
  return pin.Used() && pin.Valid() && sdk::PortExists(pin.port);
}

// True while `timeout` ms have not passed since `start` (HAL_GetTick wraps after 49 days).
[[nodiscard]] bool Within(uint32_t start, uint32_t timeout) noexcept {
  return kb::utils::ElapsedSince(sdk::TickMs(), start) < timeout;
}
} // namespace

Uart::~Uart() noexcept {
  static_cast<void>(Release());
}

bool Uart::configValid() const noexcept {
  return sdk::UartExists(config_.instance) && PinOk(config_.tx) && PinOk(config_.rx) &&
         (!config_.de.Used() || PinOk(config_.de)) && (config_.baud > 0U) &&
         (config_.baud <= kMaxBaud) && (config_.data_bits >= kMinDataBits) &&
         (config_.data_bits <= kMaxDataBits) && (config_.write_timeout.Count() > 0U);
}

kb::Status Uart::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    if (!configValid()) {
      status = error_.Set(kb::Status::InvalidArgument, port_code::kInvalidArgument);
    } else {
      static_cast<void>(tx_mutex_.Create());
      static_cast<void>(rx_mutex_.Create());
      uint32_t code = config_.de.Used()
                          ? sdk::PinOutput(config_.de, false, Pull::None, Speed::Low, false)
                          : hal::kOk;
      if (code == hal::kOk) {
        code = sdk::UartOpen(config_, *this);
      }
      status = error_.Set(MapHalStatus(code & 0xFFU), code);
      ready_ = status == kb::Status::Ok;
      stats_ = UartStats{};
    }
  }
  return status;
}

kb::Status Uart::Release() noexcept {
  kb::Status status = kb::Status::Ok;
  if (ready_) {
    const uint32_t code = sdk::UartClose(config_.instance);
    if (config_.de.Used()) {
      sdk::PinReset(config_.de);
    }
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = false;
  }
  return status;
}

void Uart::OnByte(uint8_t byte) noexcept {
  if (!ring_.TryPush(byte)) {
    OnErrors(uart_flags::kRingFull);
  }
}

void Uart::OnErrors(uint32_t flags) noexcept {
  static_cast<void>(errors_.TryPush(flags)); // a full error queue keeps the four already latched
}

uint32_t Uart::takeErrors() noexcept {
  uint32_t flags = 0U;
  uint32_t one = 0U;
  while (errors_.TryPop(one)) {
    flags |= one;
  }
  return flags;
}

std::size_t Uart::drain(std::span<uint8_t> rx) noexcept {
  std::size_t n = 0U;
  uint8_t byte = 0U;
  while ((n < rx.size()) && ring_.TryPop(byte)) {
    rx[n] = byte;
    ++n;
  }
  return n;
}

kb::Status Uart::writeLocked(std::span<const uint8_t> tx) noexcept {
  if (config_.de.Used()) {
    sdk::PinWrite(config_.de, true);
  }
  // The HAL takes at most 65535 bytes a call; each chunk has the write timeout.
  uint32_t code = hal::kOk;
  for (std::size_t at = 0U; (at < tx.size()) && (code == hal::kOk); at += kMaxChunkBytes) {
    code = sdk::UartTransmit(config_.instance,
                             tx.subspan(at, std::min(kMaxChunkBytes, tx.size() - at)),
                             config_.write_timeout.Count());
  }
  if (config_.de.Used()) {
    // Keep DE until the last stop bit has left, at most one more write timeout.
    const uint32_t start = sdk::TickMs();
    while (!sdk::UartTxComplete(config_.instance) && Within(start, config_.write_timeout.Count())) {
      kb::rtos::Yield();
    }
    code = ((code == hal::kOk) && !sdk::UartTxComplete(config_.instance)) ? hal::kTimeout : code;
    sdk::PinWrite(config_.de, false);
  }
  if (code == hal::kOk) {
    stats_.bytes_written += static_cast<uint32_t>(tx.size());
  }
  return error_.Set(MapHalStatus(code & 0xFFU), code);
}

kb::Status Uart::Write(std::span<const uint8_t> tx) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  if (tx.empty()) {
    static_cast<void>(error_.Set(status, port_code::kInvalidArgument));
  } else if (!ready_) {
    status = error_.Set(kb::Status::NotReady, port_code::kNotReady);
  } else {
    const kb::rtos::LockGuard lock{tx_mutex_, config_.write_timeout};
    if (!lock.OwnsLock()) {
      ++stats_.lock_timeouts;
      status = error_.Set(kb::Status::Busy, port_code::kLockTimeout);
    } else {
      status = writeLocked(tx);
    }
  }
  return status;
}

kb::Result<std::size_t> Uart::readLocked(std::span<uint8_t> rx, kb::Milliseconds timeout) noexcept {
  kb::Result<std::size_t> result = kb::Result<std::size_t>::Failure(kb::Status::Fault);
  const uint32_t flags = takeErrors();
  if (flags != 0U) {
    ++stats_.errors;
    const kb::Status status =
        ((flags & kOverflowFlags) != 0U) ? kb::Status::Overflow : kb::Status::BusError;
    result = Fail(error_, status, flags);
  } else {
    const uint32_t start = sdk::TickMs();
    std::size_t got = drain(rx);
    while ((got == 0U) && Within(start, timeout.Count())) {
      kb::rtos::Yield();
      got = drain(rx);
    }
    if (got == 0U) {
      result = Fail(error_, kb::Status::Timeout, hal::kTimeout);
    } else {
      stats_.bytes_read += static_cast<uint32_t>(got);
      static_cast<void>(error_.Clear());
      result = kb::Result<std::size_t>::Success(got);
    }
  }
  return result;
}

kb::Result<std::size_t> Uart::Read(std::span<uint8_t> rx, kb::Milliseconds timeout) noexcept {
  kb::Result<std::size_t> result = kb::Result<std::size_t>::Failure(kb::Status::Fault);
  if (rx.empty()) {
    result = Fail(error_, kb::Status::InvalidArgument, port_code::kInvalidArgument);
  } else if (!ready_) {
    result = Fail(error_, kb::Status::NotReady, port_code::kNotReady);
  } else {
    const kb::rtos::LockGuard lock{rx_mutex_, timeout};
    if (!lock.OwnsLock()) {
      ++stats_.lock_timeouts;
      result = Fail(error_, kb::Status::Busy, port_code::kLockTimeout);
    } else {
      result = readLocked(rx, timeout);
    }
  }
  return result;
}

kb::Status Uart::FlushInput() noexcept {
  kb::Status status = kb::Status::NotReady;
  if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else {
    const kb::rtos::LockGuard lock{rx_mutex_, config_.write_timeout};
    if (!lock.OwnsLock()) {
      ++stats_.lock_timeouts;
      status = error_.Set(kb::Status::Busy, port_code::kLockTimeout);
    } else {
      uint8_t byte = 0U;
      while (ring_.TryPop(byte)) {
      }
      static_cast<void>(takeErrors()); // dropped with the data
      status = error_.Clear();
    }
  }
  return status;
}

kb::Status Uart::SetBaud(uint32_t baud) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  if ((baud == 0U) || (baud > kMaxBaud)) {
    static_cast<void>(error_.Set(status, port_code::kInvalidArgument));
  } else if (!ready_) {
    config_.baud = baud; // used by EnsureReady()
    status = error_.Clear();
  } else {
    const kb::rtos::LockGuard tx_lock{tx_mutex_, config_.write_timeout};
    const kb::rtos::LockGuard rx_lock{rx_mutex_, config_.write_timeout};
    if (!tx_lock.OwnsLock() || !rx_lock.OwnsLock()) {
      ++stats_.lock_timeouts;
      status = error_.Set(kb::Status::Busy, port_code::kLockTimeout);
    } else {
      const uint32_t code = sdk::UartSetBaud(config_.instance, baud);
      status = error_.Set(MapHalStatus(code & 0xFFU), code);
      if (status == kb::Status::Ok) {
        config_.baud = baud;
      }
    }
  }
  return status;
}

kb::Status Uart::WaitTxDone(kb::Milliseconds timeout) noexcept {
  kb::Status status = kb::Status::NotReady;
  if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else {
    const uint32_t start = sdk::TickMs();
    while (!sdk::UartTxComplete(config_.instance) && Within(start, timeout.Count())) {
      kb::rtos::Yield();
    }
    status = sdk::UartTxComplete(config_.instance) ? error_.Clear()
                                                   : error_.Set(kb::Status::Timeout, hal::kTimeout);
  }
  return status;
}

} // namespace kb::port::stm32
