// Uart: argument checks, the reader and writer locks, the interrupt's decisions (the receive ring,
// the data mask, latched errors, the transmit byte by byte), RS-485 direction and the bounded
// waits on the port's clock. Platform-free; every HAL call goes through sdk.hpp
// (src/stm32_bus_boundary.cpp). SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32
// authors
// State shared with interrupts needs lock-free std::atomic (as kb-core's SpscRing does).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/uart.hpp"
#include "kb/rtos/mutex.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
constexpr uint8_t kMinDataBits = 7U;
constexpr uint8_t kMaxDataBits = 8U;
constexpr uint32_t kMaxBaud = 12500000U;
constexpr uint32_t kOverflowFlags = uart_flags::kOverrun | uart_flags::kRingFull;
constexpr uint32_t kCorruptFlags = uart_flags::kFrameError | uart_flags::kParityError;

[[nodiscard]] kb::Result<std::size_t> Fail(LastErrorRecord& error, kb::Status status,
                                           uint32_t code) noexcept {
  static_cast<void>(error.Set(status, code));
  return kb::Result<std::size_t>::Failure(status, code);
}

[[nodiscard]] bool PinOk(PinId pin) noexcept {
  return pin.Used() && pin.Valid() && sdk::PortExists(pin.port);
}

[[nodiscard]] bool Created(const kb::Result<void>& r) noexcept {
  return r.IsOk() || (r.Error() == kb::Status::AlreadyStarted);
}
} // namespace

Uart::~Uart() noexcept {
  static_cast<void>(Release());
}

bool Uart::configValid() const noexcept {
  return sdk::UartExists(config_.instance) && PinOk(config_.tx) && PinOk(config_.rx) &&
         (!config_.de.Used() || PinOk(config_.de)) && (config_.baud > 0U) &&
         (config_.baud <= kMaxBaud) && (config_.data_bits >= kMinDataBits) &&
         (config_.data_bits <= kMaxDataBits) && ConfiguredTimeoutValid(config_.write_timeout);
}

kb::Status Uart::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    const bool seven_none =
        (config_.data_bits == kMinDataBits) && (config_.parity == UartParity::None);
    uint32_t code = hal::kOk;
    if (!configValid()) {
      code = port_code::kInvalidArgument;
    } else if (seven_none && !sdk::UartHasSevenBitWords()) {
      code = port_code::kUnsupported; // F4: 7N1 would silently become 8N1
    } else if (!Created(tx_mutex_.Create()) || !Created(rx_mutex_.Create()) ||
               (rx_wake_.Create() != kb::Status::Ok) || (tx_done_.Create() != kb::Status::Ok)) {
      code = port_code::kRtosObject;
    } else if (!HalTickRuns()) {
      code = port_code::kHalTickStopped; // HAL_UART_Init's own waits would never end
    } else {
      rx_mask_ = static_cast<uint16_t>((1U << config_.data_bits) - 1U);
      code = config_.de.Used() ? sdk::PinOutput(config_.de, false, Pull::None, Speed::Low, false)
                               : hal::kOk;
      if (code == hal::kOk) {
        code = sdk::UartOpen(config_, *this);
      }
    }
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = status == kb::Status::Ok;
    if (ready_) {
      const IrqGuard guard{};
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
      sdk::PinWrite(config_.de, false); // kept driven low: a floating DE could enable the driver
    }
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = false;
  }
  return status;
}

UartStats Uart::Stats() const noexcept {
  const IrqGuard guard{};
  return stats_;
}

void Uart::count(uint32_t UartStats::* field, uint32_t n) noexcept {
  const IrqGuard guard{};
  stats_.*field += n;
}

// --- interrupt context ---------------------------------------------------------------------------

void Uart::onByte(uint8_t byte) noexcept {
  if (!ring_.TryPush(byte)) {
    onErrors(uart_flags::kRingFull);
  }
}

void Uart::onErrors(uint32_t flags) noexcept {
  static_cast<void>(errors_.TryPush(flags)); // a full error queue keeps the four already latched
}

void Uart::onTxEmpty() noexcept {
  const std::size_t next = tx_next_.load(std::memory_order_relaxed);
  if (next < tx_data_.size()) {
    sdk::UartWrite(config_.instance, tx_data_[next]);
    tx_next_.store(next + 1U, std::memory_order_relaxed);
  }
  if ((next + 1U) >= tx_data_.size()) {
    // The last byte is in the data register: end now, or when it has left the shift register.
    const bool wait_complete = tx_wait_complete_.load(std::memory_order_relaxed);
    sdk::UartTxInterrupts(config_.instance, false, wait_complete);
    if (!wait_complete) {
      tx_done_.Complete(hal::kOk);
    }
  }
}

void Uart::OnInterrupt(const UartEvent& event) noexcept {
  if (event.errors != 0U) {
    onErrors(event.errors);
  }
  if (event.received && ((event.errors & kCorruptFlags) == 0U)) {
    // 7E1 and 7O1 frames carry the parity bit in bit 7 of the word: masked to the data bits.
    onByte(static_cast<uint8_t>(event.data & rx_mask_));
  }
  if (event.received || (event.errors != 0U)) {
    rx_wake_.Notify();
  }
  if (event.tx_empty) {
    onTxEmpty();
  }
  if (event.tx_complete) {
    sdk::UartTxInterrupts(config_.instance, false, false);
    tx_done_.Complete(hal::kOk);
  }
}

// --- task context --------------------------------------------------------------------------------

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
  bool more = !rx.empty();
  while (more) {
    more = ring_.TryPop(byte);
    if (more) {
      rx[n] = byte;
      ++n;
      more = n < rx.size();
    }
  }
  return n;
}

kb::Status Uart::sendLocked(bool wait_complete, kb::Milliseconds timeout) noexcept {
  tx_wait_complete_.store(wait_complete, std::memory_order_relaxed);
  tx_next_.store(0U, std::memory_order_relaxed);
  tx_done_.Arm();
  // Data first, then the interrupt: transmit-empty fires at once and sends byte by byte. An empty
  // tx_data_ with wait_complete only waits for transmit-complete (WaitTxDone()).
  sdk::UartTxInterrupts(config_.instance, !tx_data_.empty(), tx_data_.empty() && wait_complete);
  Deadline deadline{timeout};
  const bool done = tx_done_.Wait(deadline);
  if (!done) {
    sdk::UartTxInterrupts(config_.instance, false, false); // stop: the rest is not sent
  }
  tx_data_ = std::span<const uint8_t>{};
  return done ? kb::Status::Ok : kb::Status::Timeout;
}

kb::Status Uart::writeLocked(std::span<const uint8_t> tx) noexcept {
  const bool de = config_.de.Used();
  if (de) {
    sdk::PinWrite(config_.de, true);
  }
  tx_data_ = tx;
  const kb::Status status = sendLocked(de, config_.write_timeout);
  if (de) {
    sdk::PinWrite(config_.de, false);
  }
  if (status == kb::Status::Ok) {
    count(&UartStats::bytes_written, static_cast<uint32_t>(tx.size()));
  }
  return error_.Set(status, (status == kb::Status::Ok) ? hal::kOk : hal::kTimeout);
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
      count(&UartStats::lock_timeouts);
      status = error_.Set(kb::Status::Busy, port_code::kLockTimeout);
    } else {
      status = writeLocked(tx);
    }
  }
  return status;
}

kb::Result<std::size_t> Uart::readLocked(std::span<uint8_t> rx, kb::Milliseconds timeout) noexcept {
  kb::Result<std::size_t> result = kb::Result<std::size_t>::Failure(kb::Status::Timeout);
  Deadline deadline{timeout};
  bool waiting = true;
  while (waiting) {
    rx_wake_.Arm();
    const uint32_t flags = takeErrors();
    const std::size_t got = (flags == 0U) ? drain(rx) : 0U;
    if (flags != 0U) {
      count(&UartStats::errors);
      const kb::Status status =
          ((flags & kOverflowFlags) != 0U) ? kb::Status::Overflow : kb::Status::BusError;
      result = Fail(error_, status, flags);
      waiting = false;
    } else if (got > 0U) {
      count(&UartStats::bytes_read, static_cast<uint32_t>(got));
      static_cast<void>(error_.Clear());
      result = kb::Result<std::size_t>::Success(got);
      waiting = false;
    } else if (!rx_wake_.Pause(deadline)) {
      result = Fail(error_, kb::Status::Timeout, hal::kTimeout);
      waiting = false;
    } else {
      // woken (or a slice ended): look again
    }
  }
  return result;
}

kb::Result<std::size_t> Uart::Read(std::span<uint8_t> rx, kb::Milliseconds timeout) noexcept {
  kb::Result<std::size_t> result = kb::Result<std::size_t>::Failure(kb::Status::Fault);
  if (rx.empty() || !TimeoutValid(timeout)) {
    result = Fail(error_, kb::Status::InvalidArgument, port_code::kInvalidArgument);
  } else if (!ready_) {
    result = Fail(error_, kb::Status::NotReady, port_code::kNotReady);
  } else {
    const kb::rtos::LockGuard lock{rx_mutex_, timeout};
    if (!lock.OwnsLock()) {
      count(&UartStats::lock_timeouts);
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
      count(&UartStats::lock_timeouts);
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
      count(&UartStats::lock_timeouts);
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
  if (!TimeoutValid(timeout)) {
    status = error_.Set(kb::Status::InvalidArgument, port_code::kInvalidArgument);
  } else if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else if (sdk::UartTxComplete(config_.instance)) {
    status = error_.Clear();
  } else {
    const kb::rtos::LockGuard lock{tx_mutex_, timeout};
    if (!lock.OwnsLock()) {
      count(&UartStats::lock_timeouts);
      status = error_.Set(kb::Status::Busy, port_code::kLockTimeout);
    } else {
      tx_data_ = std::span<const uint8_t>{};
      status = sendLocked(true, timeout); // the transmit-complete interrupt ends it
      status = error_.Set(status, (status == kb::Status::Ok) ? hal::kOk : hal::kTimeout);
    }
  }
  return status;
}

} // namespace kb::port::stm32
