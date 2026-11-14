/**
 * @file uart.hpp
 * @brief The UART contract: `kb::iface::UartPort` and `UartPortRef`; the optional `UartBaudControl`
 * and `UartTxDrain`.
 *
 * | Member | Meaning |
 * | --- | --- |
 * | `kb::Status EnsureReady() noexcept` | Brings the port up if it is not; idempotent |
 * | `kb::Status Write(std::span<const uint8_t> data) noexcept` | Queues every byte for sending
 * (bounded by the port's timeout) | | `kb::Result<std::size_t> Read(std::span<uint8_t> out,
 * kb::Milliseconds timeout) noexcept` | Bytes received into `out`, waiting at most `timeout` for
 * the first; a count below `out.size()` is not a failure | | `kb::Status FlushInput() noexcept` |
 * Drops everything received and not yet read |
 *
 * Optional: `kb::Status SetBaud(uint32_t baud) noexcept`, `kb::Status WaitTxDone(kb::Milliseconds)
 * noexcept`.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The kb-core authors
 */
#ifndef KB_INTERFACES_UART_HPP
#define KB_INTERFACES_UART_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"

namespace kb::iface {

/** @brief A UART (or RS-485, or single-wire) port. */
template <typename U>
concept UartPort =
    requires(U& port, std::span<const uint8_t> tx, std::span<uint8_t> rx, kb::Milliseconds t) {
      { port.EnsureReady() } noexcept -> std::same_as<kb::Status>;
      { port.Write(tx) } noexcept -> std::same_as<kb::Status>;
      { port.Read(rx, t) } noexcept -> std::same_as<kb::Result<std::size_t>>;
      { port.FlushInput() } noexcept -> std::same_as<kb::Status>;
    };

/** @brief A UART whose baud rate can change at run time. */
template <typename U>
concept UartBaudControl = UartPort<U> && requires(U& port, uint32_t baud) {
  { port.SetBaud(baud) } noexcept -> std::same_as<kb::Status>;
};

/** @brief A UART that can wait until its last byte has left the wire (RS-485 direction switching).
 */
template <typename U>
concept UartTxDrain = UartPort<U> && requires(U& port, kb::Milliseconds t) {
  { port.WaitTxDone(t) } noexcept -> std::same_as<kb::Status>;
};

/** @brief The function table behind a UartPortRef. */
struct UartPortOps {
  kb::Status (*ensure_ready)(void* self) noexcept;                       ///< EnsureReady.
  kb::Status (*write)(void* self, std::span<const uint8_t> tx) noexcept; ///< Write.
  kb::Result<std::size_t> (*read)(void* self, std::span<uint8_t> rx,
                                  kb::Milliseconds t) noexcept; ///< Read.
  kb::Status (*flush_input)(void* self) noexcept;               ///< FlushInput.
};

namespace detail {
/// The UartPortOps of an implementation type T.
template <UartPort T>
inline constexpr UartPortOps kUartPortOps{
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    .ensure_ready = [](void* self) noexcept { return static_cast<T*>(self)->EnsureReady(); },
    .write = [](void* self,
                // codeql::misra_deviation_next_line(DEV-KBC-004)
                std::span<const uint8_t> tx) noexcept { return static_cast<T*>(self)->Write(tx); },
    .read = [](void* self, std::span<uint8_t> rx,
               // codeql::misra_deviation_next_line(DEV-KBC-004)
               kb::Milliseconds t) noexcept { return static_cast<T*>(self)->Read(rx, t); },
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    .flush_input = [](void* self) noexcept { return static_cast<T*>(self)->FlushInput(); },
};
} // namespace detail

/** @brief A non-owning, run-time chosen reference to any UartPort. */
class UartPortRef {
public:
  /** @brief Refers to `port`, which must outlive the reference. */
  template <UartPort T>
  constexpr explicit UartPortRef(T& port) noexcept : self_(&port), ops_(&detail::kUartPortOps<T>) {}
  /** @brief UartPort::EnsureReady. */
  [[nodiscard]] kb::Status EnsureReady() noexcept {
    return ops_->ensure_ready(self_);
  }
  /** @brief UartPort::Write. */
  [[nodiscard]] kb::Status Write(std::span<const uint8_t> tx) noexcept {
    return ops_->write(self_, tx);
  }
  /** @brief UartPort::Read. */
  [[nodiscard]] kb::Result<std::size_t> Read(std::span<uint8_t> rx,
                                             kb::Milliseconds timeout) noexcept {
    return ops_->read(self_, rx, timeout);
  }
  /** @brief UartPort::FlushInput. */
  [[nodiscard]] kb::Status FlushInput() noexcept {
    return ops_->flush_input(self_);
  }

private:
  void* self_;
  const UartPortOps* ops_;
};

static_assert(UartPort<UartPortRef>, "UartPortRef satisfies the contract it erases");

} // namespace kb::iface

#endif // KB_INTERFACES_UART_HPP
