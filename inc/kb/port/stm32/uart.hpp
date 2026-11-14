/**
 * @file uart.hpp
 * @brief `kb::port::stm32::Uart`: one USART, UART or LPUART (HAL `stm32<family>xx_hal_uart`) that
 * satisfies `kb::iface::UartPort`, `UartBaudControl` and `UartTxDrain`, with optional RS-485
 * direction control on a GPIO.
 *
 * Reception is interrupt driven: the port's handler (HandleUartIrq(), called from the peripheral's
 * interrupt vector, see irq.hpp) moves each byte into a ring of kUartRxBytes and latches overrun,
 * framing, parity and noise errors; it never calls the HAL's interrupt machinery or its callbacks.
 * Transmission is `HAL_UART_Transmit` (polling), bounded by `write_timeout`. Read() waits at most
 * its timeout for the first byte, then takes whatever else has arrived. Reader and writer have
 * separate locks. Waits count `HAL_GetTick()` milliseconds.
 *
 * | Result | Meaning |
 * | --- | --- |
 * | `Ok` | Done (Read: one byte or more) |
 * | `Timeout` | Read: nothing in time. Write: the HAL did not finish in time |
 * | `Overflow` | Read: the ring or the receiver overflowed since the last Read (Detail: flags) |
 * | `BusError` | Read: framing, parity or noise error since the last Read (Detail: flags) |
 * | `Busy` | The reader or writer lock was not had in time |
 * | `InvalidArgument` | Empty span, baud 0, or a bad configuration |
 * | `NotReady` | EnsureReady() has not succeeded |
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_UART_HPP
#define KB_PORT_STM32_UART_HPP

#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/interfaces/uart.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/rtos/mutex.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/ring_buffer.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"

namespace kb::port::stm32 {

/** @brief The receive ring of each Uart, in bytes (a power of two). */
inline constexpr std::size_t kUartRxBytes = 256U;
/** @brief The instance number of LPUART1 (USART and UART instances are 1 to 8). */
inline constexpr uint8_t kLpuart1 = 101U;

/** @brief Parity of a UART frame. */
enum class UartParity : uint8_t {
  None = 0, ///< No parity bit.
  Even = 1, ///< Even parity.
  Odd = 2,  ///< Odd parity.
};

/** @brief Stop bits of a UART frame. */
enum class UartStopBits : uint8_t {
  One = 1, ///< One stop bit.
  Two = 2, ///< Two stop bits.
};

/** @brief The receive errors latched until the next Read() (Read's Detail()). */
namespace uart_flags {
inline constexpr uint32_t kOverrun = 1U << 0U;     ///< The receiver overran (ORE).
inline constexpr uint32_t kRingFull = 1U << 1U;    ///< The port's ring was full; bytes were lost.
inline constexpr uint32_t kFrameError = 1U << 2U;  ///< A stop bit was missing (FE).
inline constexpr uint32_t kParityError = 1U << 3U; ///< A parity bit was wrong (PE).
inline constexpr uint32_t kNoise = 1U << 4U;       ///< Noise was detected (NE).
} // namespace uart_flags

/** @brief How a UART is wired and framed. */
struct UartConfig {
  uint8_t instance{2U};   ///< USART1 to UART8 are 1 to 8, LPUART1 is kLpuart1.
  PinId tx{};             ///< TX with its alternate function.
  PinId rx{};             ///< RX with its alternate function.
  PinId de{};             ///< RS-485 DE driven as a GPIO (high while sending), or unused.
  uint32_t baud{115200U}; ///< Bits per second.
  uint8_t data_bits{8U};  ///< 7 or 8 data bits (9 with parity off is not offered).
  UartParity parity{UartParity::None};       ///< Parity.
  UartStopBits stop_bits{UartStopBits::One}; ///< Stop bits.
  uint8_t irq_priority{5U};                  ///< NVIC priority of the receive interrupt.
  kb::Milliseconds write_timeout{kb::Milliseconds{100U}}; ///< Write() and lock waits.
};

/** @brief Counters of a UART since EnsureReady(). */
struct UartStats {
  uint32_t bytes_written{0U}; ///< Bytes sent.
  uint32_t bytes_read{0U};    ///< Bytes returned by Read().
  uint32_t errors{0U};        ///< Receive errors reported.
  uint32_t lock_timeouts{0U}; ///< Calls refused with Busy.
};

/** @brief One STM32 UART; satisfies UartPort, UartBaudControl and UartTxDrain. */
class Uart {
public:
  /** @brief Keeps `config`. */
  explicit Uart(const UartConfig& config) noexcept : config_(config) {}
  Uart(const Uart&) = delete;
  Uart& operator=(const Uart&) = delete;
  Uart(Uart&&) = delete;
  Uart& operator=(Uart&&) = delete;
  /** @brief De-initialises the peripheral if it is up. */
  ~Uart() noexcept;

  /** @brief Clock, pins, HAL_UART_Init and the receive interrupt once; idempotent. */
  [[nodiscard]] kb::Status EnsureReady() noexcept;
  /** @brief Sends every byte of `tx`, at most `write_timeout` (RS-485: DE high, then low after). */
  [[nodiscard]] kb::Status Write(std::span<const uint8_t> tx) noexcept;
  /** @brief Waits at most `timeout` for the first byte, then takes what has arrived. */
  [[nodiscard]] kb::Result<std::size_t> Read(std::span<uint8_t> rx,
                                             kb::Milliseconds timeout) noexcept;
  /** @brief Drops everything received and not yet read, and the latched errors. */
  [[nodiscard]] kb::Status FlushInput() noexcept;
  /** @brief Changes the baud rate (HAL_UART_Init again, both locks held). */
  [[nodiscard]] kb::Status SetBaud(uint32_t baud) noexcept;
  /** @brief Waits at most `timeout` until the last byte has left the shift register (TC). */
  [[nodiscard]] kb::Status WaitTxDone(kb::Milliseconds timeout) noexcept;
  /** @brief Stops the interrupt and HAL_UART_DeInit. */
  [[nodiscard]] kb::Status Release() noexcept;
  /** @brief The last call's status and its vendor code (or error flags) in Detail(). */
  [[nodiscard]] kb::Result<void> LastError() const noexcept {
    return error_.Get();
  }
  /** @brief The counters. */
  [[nodiscard]] UartStats Stats() const noexcept {
    return stats_;
  }
  /** @brief The configuration (with the current baud rate). */
  [[nodiscard]] const UartConfig& Config() const noexcept {
    return config_;
  }
  /** @brief Interrupt context only (called by HandleUartIrq): one received byte. */
  void OnByte(uint8_t byte) noexcept;
  /** @brief Interrupt context only (called by HandleUartIrq): receive error flags (uart_flags). */
  void OnErrors(uint32_t flags) noexcept;

private:
  [[nodiscard]] bool configValid() const noexcept;
  [[nodiscard]] kb::Status writeLocked(std::span<const uint8_t> tx) noexcept;
  [[nodiscard]] kb::Result<std::size_t> readLocked(std::span<uint8_t> rx,
                                                   kb::Milliseconds timeout) noexcept;
  [[nodiscard]] uint32_t takeErrors() noexcept;
  [[nodiscard]] std::size_t drain(std::span<uint8_t> rx) noexcept;

  UartConfig config_;
  kb::rtos::Mutex tx_mutex_;
  kb::rtos::Mutex rx_mutex_;
  LastErrorRecord error_;
  UartStats stats_;
  kb::utils::SpscRing<uint8_t, kUartRxBytes> ring_;
  kb::utils::SpscRing<uint32_t, 4U> errors_;
  bool ready_{false};
};

static_assert(kb::iface::UartPort<Uart>, "Uart satisfies kb::iface::UartPort");
static_assert(kb::iface::UartBaudControl<Uart>, "Uart satisfies UartBaudControl");
static_assert(kb::iface::UartTxDrain<Uart>, "Uart satisfies UartTxDrain");

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_UART_HPP
