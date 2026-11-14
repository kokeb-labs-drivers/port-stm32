/**
 * @file i2c.hpp
 * @brief `kb::port::stm32::I2cController`: one STM32 I2C peripheral (HAL `stm32<family>xx_hal_i2c`,
 * blocking calls) that satisfies `kb::iface::I2cBus`, with bus recovery, statistics and a lock.
 *
 * | Result | Meaning |
 * | --- | --- |
 * | `Ok` | Done; `LastFailure()` is `None` |
 * | `NoDevice` | The address was not acknowledged (`AddressNack`) |
 * | `Nack` | The device acknowledged its address, then refused a byte (`DataNack`) |
 * | `Timeout` | The transfer did not end in time, lines idle (`Timeout`) |
 * | `BusError` | Arbitration lost, bus error, or a stuck line (`Bus` or `BusStuck`) |
 * | `Busy` | The controller lock was not had within the timeout; nothing was sent |
 * | `Unsupported` | `WriteRead()` with more than 2 bytes to write (HAL_I2C_Mem_Read limit) |
 * | `InvalidArgument` | Address outside 0x08..0x77, an empty read, or a bad configuration |
 * | `NotReady` | EnsureReady() has not succeeded |
 *
 * The HAL reports a NACK of the address and of a data byte with one bit (`HAL_I2C_ERROR_AF`); the
 * controller tells them apart with one `HAL_I2C_IsDeviceReady` probe. After a failure SDA and SCL
 * are read; when one is low the failure is `BusStuck` and, with `auto_recover`, RecoverBus() runs:
 * the pins are taken as GPIO, SCL is clocked nine times and a STOP is made, and the peripheral is
 * initialised again.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_I2C_HPP
#define KB_PORT_STM32_I2C_HPP

#include <cstdint>
#include <span>

#include "kb/interfaces/i2c.hpp"
#include "kb/interfaces/types.hpp"
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
  kb::Milliseconds timeout{kb::Milliseconds{50U}}; ///< Lock wait and transfer limit, 1 ms or more.
  uint32_t timing{0U};          ///< TIMINGR (I2C v2 families); 0 computes it from the kernel clock.
  uint32_t kernel_clock_hz{0U}; ///< The I2C kernel clock; 0 reads PCLK1 (the reset default).
  bool internal_pullups{false}; ///< Weak internal pull-ups on SCL and SDA (use external ones).
  bool auto_recover{true};      ///< Run RecoverBus() when a stuck line is found.
};

/** @brief Counters of an I2C controller since EnsureReady(). */
struct I2cStats {
  uint32_t transfers{0U};     ///< Transfers started.
  uint32_t failures{0U};      ///< Transfers that failed.
  uint32_t recoveries{0U};    ///< RecoverBus() runs.
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
  /** @brief Writes `tx` (1 or 2 bytes), repeated START, reads `rx` (HAL_I2C_Mem_Read). */
  [[nodiscard]] kb::Status WriteRead(uint8_t address, std::span<const uint8_t> tx,
                                     std::span<uint8_t> rx) noexcept;
  /** @brief START, address, reads `rx` (non-empty), STOP. */
  [[nodiscard]] kb::Status Read(uint8_t address, std::span<uint8_t> rx) noexcept;
  /** @brief Why the last transfer failed; `None` after a success. */
  [[nodiscard]] kb::iface::I2cFailure LastFailure() const noexcept {
    return last_failure_;
  }
  /** @brief Nine SCL clocks and a STOP on the pins as GPIO, then the peripheral again. */
  [[nodiscard]] kb::Status RecoverBus() noexcept;
  /** @brief HAL_I2C_DeInit; EnsureReady() may bring it up again. */
  [[nodiscard]] kb::Status Release() noexcept;
  /** @brief The last call's status and its vendor code in Detail(). */
  [[nodiscard]] kb::Result<void> LastError() const noexcept {
    return error_.Get();
  }
  /** @brief The counters. */
  [[nodiscard]] I2cStats Stats() const noexcept {
    return stats_;
  }
  /** @brief The configuration. */
  [[nodiscard]] const I2cConfig& Config() const noexcept {
    return config_;
  }

private:
  enum class Op : uint8_t { Write, WriteRead, Read };
  [[nodiscard]] kb::Status transfer(Op op, uint8_t address, std::span<const uint8_t> tx,
                                    std::span<uint8_t> rx) noexcept;
  [[nodiscard]] uint32_t run(Op op, uint8_t address, std::span<const uint8_t> tx,
                             std::span<uint8_t> rx) const noexcept;
  [[nodiscard]] kb::Status classify(uint32_t code, uint8_t address) noexcept;
  [[nodiscard]] kb::iface::I2cFailure failureOf(uint32_t code, uint8_t address) const noexcept;
  [[nodiscard]] static kb::Status statusOf(kb::iface::I2cFailure failure) noexcept;
  [[nodiscard]] bool linesIdle() const noexcept;
  [[nodiscard]] kb::Status recoverLocked() noexcept;
  [[nodiscard]] bool configValid() const noexcept;
  [[nodiscard]] uint32_t timingRegister(bool& ok) const noexcept;

  I2cConfig config_;
  kb::rtos::Mutex mutex_;
  LastErrorRecord error_;
  I2cStats stats_;
  kb::iface::I2cFailure last_failure_{kb::iface::I2cFailure::None};
  bool ready_{false};
};

static_assert(kb::iface::I2cBus<I2cController>, "I2cController satisfies kb::iface::I2cBus");

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_I2C_HPP
