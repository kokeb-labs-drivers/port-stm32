/**
 * @file i2c.hpp
 * @brief The I2C contract: `kb::iface::I2cBus` (a controller addressed per transfer) and its
 * type-erased `I2cBusRef`.
 *
 * | Member | Meaning |
 * | --- | --- |
 * | `kb::Status EnsureReady() noexcept` | Brings the controller up if it is not; idempotent; `Ok`
 * when usable | | `kb::Status Write(uint8_t address, std::span<const uint8_t> data) noexcept` | One
 * write transfer (START, address, data, STOP) | | `kb::Status WriteRead(uint8_t address,
 * std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept` | Write then read with a repeated
 * START | | `kb::Status Read(uint8_t address, std::span<uint8_t> rx) noexcept` | One read transfer
 * | | `I2cFailure LastFailure() const noexcept` | Why the last transfer failed (`None` after a
 * success) |
 *
 * Addresses are 7-bit (0x08 to 0x77). Every call is bounded by the controller's timeout. Results:
 * `Ok`, `NoDevice` (address NACK), `Nack` (data NACK), `BusError`, `Timeout`, `InvalidArgument`,
 * `NotReady`. A driver port adapts this with a few lines (docs/port-contracts.md).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The kb-core authors
 */
#ifndef KB_INTERFACES_I2C_HPP
#define KB_INTERFACES_I2C_HPP

#include <concepts>
#include <cstdint>
#include <span>

#include "kb/interfaces/types.hpp"
#include "kb/utils/status.hpp"

namespace kb::iface {

/** @brief An I2C controller that addresses a device per transfer. */
template <typename B>
concept I2cBus = requires(B& bus, const B& cbus, uint8_t address, std::span<const uint8_t> tx,
                          std::span<uint8_t> rx) {
  { bus.EnsureReady() } noexcept -> std::same_as<kb::Status>;
  { bus.Write(address, tx) } noexcept -> std::same_as<kb::Status>;
  { bus.WriteRead(address, tx, rx) } noexcept -> std::same_as<kb::Status>;
  { bus.Read(address, rx) } noexcept -> std::same_as<kb::Status>;
  { cbus.LastFailure() } noexcept -> std::same_as<I2cFailure>;
};

/** @brief The function table behind an I2cBusRef. */
struct I2cBusOps {
  kb::Status (*ensure_ready)(void* self) noexcept; ///< EnsureReady.
  kb::Status (*write)(void* self, uint8_t address,
                      std::span<const uint8_t> tx) noexcept; ///< Write.
  kb::Status (*write_read)(void* self, uint8_t address, std::span<const uint8_t> tx,
                           std::span<uint8_t> rx) noexcept;                        ///< WriteRead.
  kb::Status (*read)(void* self, uint8_t address, std::span<uint8_t> rx) noexcept; ///< Read.
  I2cFailure (*last_failure)(const void* self) noexcept;                           ///< LastFailure.
};

namespace detail {
/// The I2cBusOps of an implementation type T.
template <I2cBus T>
inline constexpr I2cBusOps kI2cBusOps{
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    .ensure_ready = [](void* self) noexcept { return static_cast<T*>(self)->EnsureReady(); },
    .write =
        [](void* self, uint8_t address, std::span<const uint8_t> tx) noexcept {
          // codeql::misra_deviation_next_line(DEV-KBC-004)
          return static_cast<T*>(self)->Write(address, tx);
        },
    .write_read =
        [](void* self, uint8_t address, std::span<const uint8_t> tx,
           std::span<uint8_t> rx) noexcept {
          // codeql::misra_deviation_next_line(DEV-KBC-004)
          return static_cast<T*>(self)->WriteRead(address, tx, rx);
        },
    .read = [](void* self, uint8_t address,
               // codeql::misra_deviation_next_line(DEV-KBC-004)
               std::span<uint8_t> rx) noexcept { return static_cast<T*>(self)->Read(address, rx); },
    .last_failure =
        // codeql::misra_deviation_next_line(DEV-KBC-004)
    [](const void* self) noexcept { return static_cast<const T*>(self)->LastFailure(); },
};
} // namespace detail

/**
 * @brief A non-owning reference to any I2cBus, chosen at run time (a context pointer and a constant
 * function table; no virtual function, no heap). The referenced bus must outlive the reference.
 */
class I2cBusRef {
public:
  /** @brief Refers to `bus`. */
  template <I2cBus T>
  constexpr explicit I2cBusRef(T& bus) noexcept : self_(&bus), ops_(&detail::kI2cBusOps<T>) {}
  /** @brief I2cBus::EnsureReady. */
  [[nodiscard]] kb::Status EnsureReady() noexcept {
    return ops_->ensure_ready(self_);
  }
  /** @brief I2cBus::Write. */
  [[nodiscard]] kb::Status Write(uint8_t address, std::span<const uint8_t> tx) noexcept {
    return ops_->write(self_, address, tx);
  }
  /** @brief I2cBus::WriteRead. */
  [[nodiscard]] kb::Status WriteRead(uint8_t address, std::span<const uint8_t> tx,
                                     std::span<uint8_t> rx) noexcept {
    return ops_->write_read(self_, address, tx, rx);
  }
  /** @brief I2cBus::Read. */
  [[nodiscard]] kb::Status Read(uint8_t address, std::span<uint8_t> rx) noexcept {
    return ops_->read(self_, address, rx);
  }
  /** @brief I2cBus::LastFailure. */
  [[nodiscard]] I2cFailure LastFailure() const noexcept {
    return ops_->last_failure(self_);
  }

private:
  void* self_;
  const I2cBusOps* ops_;
};

static_assert(I2cBus<I2cBusRef>, "I2cBusRef satisfies the contract it erases");

} // namespace kb::iface

#endif // KB_INTERFACES_I2C_HPP
