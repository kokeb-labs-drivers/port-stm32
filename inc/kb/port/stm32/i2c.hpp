/**
 * @file i2c.hpp
 * @brief `kb::port::stm32::I2cController`: one STM32 I2C peripheral (HAL `stm32<family>xx_hal_i2c`,
 * interrupt-driven sequential transfers) that satisfies `kb::iface::I2cBus`, with bus recovery,
 * statistics and a lock.
 *
 * | Result | Meaning |
 * | --- | --- |
 * | `Ok` | Done; `LastFailure()` is `None` |
 * | `NoDevice` | The address was not acknowledged (`AddressNack`; a Read, or a probe) |
 * | `Nack` | A write was not acknowledged (`DataNack`): the address or a data byte, unsplit unless
 * `nack_probe` | | `Timeout` | The transfer did not end in time, lines idle (`Timeout`) | |
 * `BusError` | Arbitration lost, bus error, BUSY with idle lines, or a stuck line (`Bus` or
 * `BusStuck`) | | `Busy` | The controller lock was not had within the timeout; nothing was sent |
 * | `InvalidArgument` | Address outside 0x08..0x77, an empty read, a timeout above kMaxTimeout, a
 * bad configuration | | `NotReady` | EnsureReady() has not succeeded, or the I2C kernel clock reads
 * 0 |
 *
 * Every transfer is started with the HAL's interrupt API (`HAL_I2C_Master_Seq_Transmit_IT`,
 * `HAL_I2C_Master_Seq_Receive_IT`) and ended by the HAL's completion or error callback, which
 * completes the controller's Completion; the calling task waits on it for at most `timeout` on the
 * port's clock (completion.hpp), asleep under an RTOS. WriteRead() is a write of any length without
 * STOP, a repeated START and the read. Before every transfer the transmit and receive data
 * registers are flushed and the stale transfer fields of CR2 cleared (I2C v2).
 *
 * After any failure the HAL handle is put back to ready and the error flags are cleared; only when
 * the BUSY flag stays set is the peripheral reset (PE toggled by re-initialisation; SWRST on F4),
 * never after a plain NACK. When a line reads low the failure is `BusStuck` and, with
 * `auto_recover`, RecoverBus() runs: the pins are taken as GPIO, SCL is clocked nine times and a
 * STOP is made, and the peripheral is initialised again.
 *
 * The HAL reports a NACK of the address and of a data byte with one bit (`HAL_I2C_ERROR_AF`). Only
 * with `nack_probe` set does the controller tell them apart, with one address-only probe after the
 * NACK. It is off by default: some devices (secure elements) NACK every poll while busy, and an
 * address-only frame can make them hold the bus. EnsureReady() never probes.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_I2C_HPP
#define KB_PORT_STM32_I2C_HPP

// State shared with interrupts needs lock-free std::atomic (as kb-core's SpscRing does).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstdint>
#include <span>

#include "kb/interfaces/i2c.hpp"
#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/rtos/mutex.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"

namespace kb::port::stm32 {

/** @brief How an I2C peripheral is wired and clocked. */
struct I2cConfig {
  uint8_t instance{1U};       ///< I2C1 is 1, ... I2C4 is 4 (where the device has it).
  PinId scl{};                ///< SCL with its alternate function.
  PinId sda{};                ///< SDA with its alternate function.
  uint32_t clock_hz{100000U}; ///< 10 kHz to 1 MHz (Fast-mode Plus needs FM+ pins).
  /// Lock wait, and the limit of one transfer, 1 ms to kMaxTimeout.
  kb::Milliseconds timeout{kb::Milliseconds{50U}};
  /// TIMINGR (I2C v2 families); 0 computes it from the kernel clock. Real wiring can need a slower
  /// timing than the nominal one: this override stays next to the computed value.
  uint32_t timing{0U};
  uint32_t kernel_clock_hz{0U}; ///< The I2C kernel clock; 0 reads it from the RCC.
  bool internal_pullups{false}; ///< Weak internal pull-ups on SCL and SDA (use external ones).
  bool auto_recover{true};      ///< Run RecoverBus() when a stuck line is found.
  bool nack_probe{false};       ///< After a write NACK, probe the address to split address/data.
  uint8_t irq_priority{5U};     ///< NVIC priority of the event and error interrupts.
};

/** @brief Counters of an I2C controller since EnsureReady(). */
struct I2cStats {
  uint32_t transfers{0U};     ///< Transfers started.
  uint32_t failures{0U};      ///< Transfers that failed.
  uint32_t recoveries{0U};    ///< RecoverBus() runs and peripheral resets.
  uint32_t scl_held{0U};      ///< Recoveries that found SCL held low (not freed by clocking).
  uint32_t lock_timeouts{0U}; ///< Calls refused with Busy.
};

/** @brief The lowest and highest 7-bit address a transfer accepts. */
inline constexpr uint8_t kI2cFirstAddress = 0x08U;
inline constexpr uint8_t kI2cLastAddress = 0x77U; ///< See kI2cFirstAddress.

/** @brief I2C timing register fields for the I2C v2 peripheral (G4, H7, L4). */
struct I2cTiming {
  uint32_t presc{0U};  ///< PRESC.
  uint32_t scldel{0U}; ///< SCLDEL.
  uint32_t sdadel{0U}; ///< SDADEL.
  uint32_t sclh{0U};   ///< SCLH.
  uint32_t scll{0U};   ///< SCLL.
  /** @brief The TIMINGR value. */
  [[nodiscard]] constexpr uint32_t Register() const noexcept {
    return (presc << 28U) | (scldel << 20U) | (sdadel << 16U) | (sclh << 8U) | scll;
  }
};

/**
 * @brief TIMINGR for `bus_hz` from a kernel clock of `kernel_hz`, meeting the I2C specification's
 * minimum low and high times, data setup time and the rise times of its mode (standard to 100 kHz,
 * fast to 400 kHz, fast-mode plus to 1 MHz). `ok` is false when no prescaler fits.
 */
[[nodiscard]] I2cTiming ComputeI2cTiming(uint32_t kernel_hz, uint32_t bus_hz, bool& ok) noexcept;

/** @brief One STM32 I2C peripheral; satisfies kb::iface::I2cBus. */
class I2cController {
public:
  /** @brief Keeps `config`; nothing is touched until EnsureReady(). */
  explicit I2cController(const I2cConfig& config) noexcept : config_(config) {}
  I2cController(const I2cController&) = delete;
  I2cController& operator=(const I2cController&) = delete;
  I2cController(I2cController&&) = delete;
  I2cController& operator=(I2cController&&) = delete;
  /** @brief Releases the peripheral if it is up. */
  ~I2cController() noexcept;

  /** @brief Clock, pins and HAL_I2C_Init once; idempotent. Call once before the object is shared.
   */
  [[nodiscard]] kb::Status EnsureReady() noexcept;
  /** @brief START, address, `tx`, STOP. An empty `tx` probes the address. */
  [[nodiscard]] kb::Status Write(uint8_t address, std::span<const uint8_t> tx) noexcept;
  /** @brief Writes `tx` (no STOP), repeated START, reads `rx`, STOP. */
  [[nodiscard]] kb::Status WriteRead(uint8_t address, std::span<const uint8_t> tx,
                                     std::span<uint8_t> rx) noexcept;
  /** @brief START, address, reads `rx` (non-empty), STOP. */
  [[nodiscard]] kb::Status Read(uint8_t address, std::span<uint8_t> rx) noexcept;
  /** @brief Why the last transfer failed; `None` after a success. */
  [[nodiscard]] kb::iface::I2cFailure LastFailure() const noexcept {
    return last_failure_.load(std::memory_order_relaxed);
  }
  /** @brief Nine SCL clocks and a STOP on the pins as GPIO, then the peripheral again. */
  [[nodiscard]] kb::Status RecoverBus() noexcept;
  /** @brief HAL_I2C_DeInit; EnsureReady() may bring it up again. */
  [[nodiscard]] kb::Status Release() noexcept;
  /** @brief The last call's status and its vendor code in Detail(). */
  [[nodiscard]] kb::Result<void> LastError() const noexcept {
    return error_.Get();
  }
  /** @brief The counters (a consistent copy). */
  [[nodiscard]] I2cStats Stats() const noexcept;
  /** @brief The configuration. */
  [[nodiscard]] const I2cConfig& Config() const noexcept {
    return config_;
  }

private:
  enum class Op : uint8_t { Probe, Write, WriteRead, Read };
  [[nodiscard]] kb::Status transfer(Op op, uint8_t address, std::span<const uint8_t> tx,
                                    std::span<uint8_t> rx) noexcept;
  [[nodiscard]] uint32_t run(Op op, uint8_t address, std::span<const uint8_t> tx,
                             std::span<uint8_t> rx) noexcept;
  [[nodiscard]] uint32_t await(uint32_t started, Deadline& deadline) noexcept;
  [[nodiscard]] kb::Status classify(uint32_t code, Op op, uint8_t address) noexcept;
  [[nodiscard]] kb::iface::I2cFailure failureOf(uint32_t code, Op op, uint8_t address) noexcept;
  void restoreLocked() noexcept;
  void count(uint32_t I2cStats::* field) noexcept;
  [[nodiscard]] static kb::Status statusOf(kb::iface::I2cFailure failure) noexcept;
  [[nodiscard]] bool linesIdle() const noexcept;
  [[nodiscard]] kb::Status recoverLocked() noexcept;
  [[nodiscard]] bool configValid() const noexcept;
  [[nodiscard]] uint32_t timingRegister(bool& ok) const noexcept;

  I2cConfig config_;
  kb::rtos::Mutex mutex_;
  Completion done_; ///< Completed by the HAL's transfer callbacks.
  LastErrorRecord error_;
  I2cStats stats_;
  std::atomic<kb::iface::I2cFailure> last_failure_{kb::iface::I2cFailure::None};
  std::atomic<bool> ready_{false}; ///< Cleared under the lock by a failed recovery.
};

static_assert(kb::iface::I2cBus<I2cController>, "I2cController satisfies kb::iface::I2cBus");

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_I2C_HPP
