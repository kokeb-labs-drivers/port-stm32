/**
 * @file error.hpp
 * @brief The STM32 HAL results this port reports, their mapping to kb::Status, and the pin naming
 * shared by every peripheral.
 *
 * Every failed HAL call is reported as a kb::Status; the vendor code is kept in `Detail()` of a
 * kb::Result, and in `LastError().Detail()` of the object for calls that return a bare kb::Status.
 * The vendor code packs the `HAL_StatusTypeDef` in bits 0 to 7 and the handle's `ErrorCode` (the
 * peripheral's `HAL_<PPP>_ERROR_*` bits) from bit 8: `VendorCode(status, error_code)`.
 *
 * | HAL_StatusTypeDef | Value | kb::Status (general) |
 * | --- | --- | --- |
 * | HAL_OK | 0 | Ok |
 * | HAL_ERROR | 1 | Fault (refined per peripheral from ErrorCode) |
 * | HAL_BUSY | 2 | Busy |
 * | HAL_TIMEOUT | 3 | Timeout |
 *
 * The values are checked against the HAL by a static_assert in src/boundary/error.cpp.
 *
 * Every timeout the port takes (configuration or per call) is at most kMaxTimeout; a longer one is
 * refused with InvalidArgument, so no wait can become the HAL's "forever" (HAL_MAX_DELAY).
 *
 * LastErrorRecord is safe to write and read from several tasks: each Set() and Get() runs with
 * interrupts masked for a few instructions, so a record never tears and is never a data race. It
 * holds the most recent call on the object by any task (docs/thread-safety.md).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_ERROR_HPP
#define KB_PORT_STM32_ERROR_HPP

#include <cstdint>

#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"

namespace kb::port::stm32 {

/** @brief The HAL_StatusTypeDef values (checked against the HAL). */
namespace hal {
inline constexpr uint32_t kOk = 0U;      ///< HAL_OK.
inline constexpr uint32_t kError = 1U;   ///< HAL_ERROR.
inline constexpr uint32_t kBusy = 2U;    ///< HAL_BUSY.
inline constexpr uint32_t kTimeout = 3U; ///< HAL_TIMEOUT.
} // namespace hal

/** @brief Port-side codes in the HAL status byte, for refusals made before any HAL call. */
namespace port_code {
inline constexpr uint32_t kInvalidArgument = 0xF1U; ///< The port refused an argument.
inline constexpr uint32_t kNotReady = 0xF2U;        ///< The object is not started.
inline constexpr uint32_t kLockTimeout = 0xF3U;     ///< The object's lock was not had in time.
inline constexpr uint32_t kUnsupported = 0xF4U;     ///< Not available on this family or instance.
inline constexpr uint32_t kOwned = 0xF5U; ///< The instance or pin is owned by another object.
/** @brief The request conflicts with how another object set up shared hardware (a timer's period).
 */
inline constexpr uint32_t kConflict = 0xF6U;
inline constexpr uint32_t kRtosObject = 0xF7U; ///< A kb::rtos object could not be created.
/** @brief The HAL time base (HAL_GetTick) does not advance; HAL initialisation would not time out.
 */
inline constexpr uint32_t kHalTickStopped = 0xF8U;

/** @brief A peripheral's kernel clock reads 0 Hz: its clock source is not running. */
inline constexpr uint32_t kNoClock = 0xF9U;
/** @brief A DMA stream ended in a transfer or direct-mode error; Detail's upper bits are its flags.
 */
inline constexpr uint32_t kDma = 0xFAU;
/** @brief A DMA stop could not be confirmed: the memory is quarantined until Recover(). */
inline constexpr uint32_t kQuarantined = 0xFBU;
/** @brief A DMA buffer lies outside its region, or the region's MPU or cache set-up differs. */
inline constexpr uint32_t kPlacement = 0xFCU;
/** @brief A frame larger than the DMA staging buffer of the bus. */
inline constexpr uint32_t kTooLarge = 0xFDU;
/** @brief An NVIC priority written from the board's assignment did not read back. */
inline constexpr uint32_t kPriority = 0xFEU;
} // namespace port_code

/**
 * @brief The longest timeout the port accepts, in a configuration or a call (2 s). Long enough for
 * a secure element that stretches SCL for several hundred milliseconds; short enough that a failing
 * bus cannot hold callers for long. A caller that must wait longer loops.
 */
inline constexpr kb::Milliseconds kMaxTimeout{2000U};

/**
 * @brief The lowest NVIC priority number an interrupt that calls the RTOS may have: under FreeRTOS
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (unshifted). Set by CMake
 * (KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY); 0 when nothing limits it (bare metal).
 */
#if defined(KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY)
inline constexpr uint8_t kMinRtosIrqPriority = KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY;
#else
inline constexpr uint8_t kMinRtosIrqPriority = 0U;
#endif
/** @brief The largest NVIC priority number (4 priority bits on every supported family). */
inline constexpr uint8_t kLowestIrqPriority = 15U;

/** @brief True for an NVIC priority an interrupt that gives a kb::rtos semaphore may have. */
[[nodiscard]] constexpr bool RtosIrqPriorityValid(uint8_t priority) noexcept {
  return (priority >= kMinRtosIrqPriority) && (priority <= kLowestIrqPriority);
}

/** @brief True when `timeout` is at most kMaxTimeout (zero included). */
[[nodiscard]] constexpr bool TimeoutValid(kb::Milliseconds timeout) noexcept {
  return timeout.Count() <= kMaxTimeout.Count();
}

/** @brief True when `timeout` is at least 1 ms and at most kMaxTimeout (configured waits). */
[[nodiscard]] constexpr bool ConfiguredTimeoutValid(kb::Milliseconds timeout) noexcept {
  return (timeout.Count() > 0U) && TimeoutValid(timeout);
}

/** @brief The vendor code: the HAL status in bits 0 to 7, the handle's ErrorCode from bit 8. */
[[nodiscard]] constexpr uint32_t VendorCode(uint32_t status, uint32_t error_code) noexcept {
  return (status & 0xFFU) | (error_code << 8U);
}

/** @brief The general mapping of a HAL_StatusTypeDef (or a port code) to kb::Status. */
[[nodiscard]] constexpr kb::Status MapHalStatus(uint32_t status) noexcept {
  kb::Status out = kb::Status::Fault;
  switch (status) {
  case hal::kOk:
    out = kb::Status::Ok;
    break;
  case hal::kBusy:
  case port_code::kLockTimeout:
  case port_code::kOwned:
    out = kb::Status::Busy;
    break;
  case hal::kTimeout:
    out = kb::Status::Timeout;
    break;
  case port_code::kInvalidArgument:
  case port_code::kConflict:
    out = kb::Status::InvalidArgument;
    break;
  case port_code::kNotReady:
  case port_code::kHalTickStopped:
  case port_code::kNoClock:
  case port_code::kPlacement:
  case port_code::kPriority:
    out = kb::Status::NotReady;
    break;
  case port_code::kTooLarge:
    out = kb::Status::OutOfRange;
    break;
  case port_code::kUnsupported:
    out = kb::Status::Unsupported;
    break;
  default:
    out = kb::Status::Fault;
    break;
  }
  return out;
}

/**
 * @brief The outcome of the last call of a port object: the status and the vendor code. Set() and
 * Get() mask interrupts for their few instructions, so concurrent calls from several tasks never
 * tear a record (src/error.cpp).
 */
class LastErrorRecord {
public:
  constexpr LastErrorRecord() noexcept = default;
  /** @brief Records `status` with vendor code `code` and returns `status`. */
  kb::Status Set(kb::Status status, uint32_t code) noexcept;
  /** @brief Records a success. */
  kb::Status Clear() noexcept {
    return Set(kb::Status::Ok, 0U);
  }
  /** @brief The record as a kb::Result<void>: `Error()` and `Detail()` (the vendor code). */
  [[nodiscard]] kb::Result<void> Get() const noexcept;

private:
  kb::Status status_{kb::Status::Ok};
  uint32_t detail_{0U};
};

/** @brief A GPIO port letter as a number: A is 0, B is 1, ... K is 10. */
enum class GpioPort : uint8_t { A = 0, B, C, D, E, F, G, H, I, J, K };

/** @brief One pin: port, number 0 to 15 and, for peripheral pins, the alternate function 0 to 15.
 */
struct PinId {
  GpioPort port{GpioPort::A}; ///< Port letter.
  uint8_t pin{0xFFU};         ///< Pin number 0 to 15; 0xFF means "no pin".
  uint8_t af{0U};             ///< Alternate function number (peripheral pins only).
  /** @brief True when a pin is named. */
  [[nodiscard]] constexpr bool Used() const noexcept {
    return pin != 0xFFU;
  }
  /** @brief True when the pin number and the alternate function are in range. */
  [[nodiscard]] constexpr bool Valid() const noexcept {
    return (pin <= 15U) && (af <= 15U);
  }
};

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_ERROR_HPP
