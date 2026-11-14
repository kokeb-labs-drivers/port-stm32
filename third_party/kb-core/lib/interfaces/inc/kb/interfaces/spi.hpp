/**
 * @file spi.hpp
 * @brief The SPI contract: `kb::iface::SpiDevice` (one chip select on a bus, mode and clock fixed
 * by the HAL) and `SpiDeviceRef`; the optional `SpiClockControl`.
 *
 * | Member | Meaning |
 * | --- | --- |
 * | `kb::Status EnsureReady() noexcept` | Brings the device up if it is not; idempotent |
 * | `kb::Status Transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept` | One
 * full-duplex frame with chip select held throughout |
 *
 * Either span may be empty (write only, or read only with zeros clocked out); when both are given
 * they have the same length, otherwise the result is `InvalidArgument` and nothing is clocked.
 * Optional: `kb::Status SetClockHz(uint32_t hz) noexcept` (SpiClockControl).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The kb-core authors
 */
#ifndef KB_INTERFACES_SPI_HPP
#define KB_INTERFACES_SPI_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/utils/status.hpp"

namespace kb::iface {

/** @brief One SPI device: a chip select on a bus with its mode and clock. */
template <typename D>
concept SpiDevice = requires(D& device, std::span<const uint8_t> tx, std::span<uint8_t> rx) {
  { device.EnsureReady() } noexcept -> std::same_as<kb::Status>;
  { device.Transfer(tx, rx) } noexcept -> std::same_as<kb::Status>;
};

/** @brief An SPI device whose clock can change at run time. */
template <typename D>
concept SpiClockControl = SpiDevice<D> && requires(D& device, uint32_t hz) {
  { device.SetClockHz(hz) } noexcept -> std::same_as<kb::Status>;
};

/** @brief True when a Transfer's spans are acceptable: either empty, or both of one length. */
[[nodiscard]] constexpr bool SpiSpansValid(std::span<const uint8_t> tx,
                                           std::span<uint8_t> rx) noexcept {
  return tx.empty() || rx.empty() || (tx.size() == rx.size());
}

/** @brief The function table behind an SpiDeviceRef. */
struct SpiDeviceOps {
  kb::Status (*ensure_ready)(void* self) noexcept; ///< EnsureReady.
  kb::Status (*transfer)(void* self, std::span<const uint8_t> tx,
                         std::span<uint8_t> rx) noexcept; ///< Transfer.
};

namespace detail {
/// The SpiDeviceOps of an implementation type T.
template <SpiDevice T>
inline constexpr SpiDeviceOps kSpiDeviceOps{
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    .ensure_ready = [](void* self) noexcept { return static_cast<T*>(self)->EnsureReady(); },
    .transfer =
        [](void* self, std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept {
          // codeql::misra_deviation_next_line(DEV-KBC-004)
          return static_cast<T*>(self)->Transfer(tx, rx);
        },
};
} // namespace detail

/** @brief A non-owning, run-time chosen reference to any SpiDevice (context pointer and function
 * table). */
class SpiDeviceRef {
public:
  /** @brief Refers to `device`, which must outlive the reference. */
  template <SpiDevice T>
  constexpr explicit SpiDeviceRef(T& device) noexcept
      : self_(&device), ops_(&detail::kSpiDeviceOps<T>) {}
  /** @brief SpiDevice::EnsureReady. */
  [[nodiscard]] kb::Status EnsureReady() noexcept {
    return ops_->ensure_ready(self_);
  }
  /** @brief SpiDevice::Transfer. */
  [[nodiscard]] kb::Status Transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept {
    return ops_->transfer(self_, tx, rx);
  }

private:
  void* self_;
  const SpiDeviceOps* ops_;
};

static_assert(SpiDevice<SpiDeviceRef>, "SpiDeviceRef satisfies the contract it erases");

} // namespace kb::iface

#endif // KB_INTERFACES_SPI_HPP
