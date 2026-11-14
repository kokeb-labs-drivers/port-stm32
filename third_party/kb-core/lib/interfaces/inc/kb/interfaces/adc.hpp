/**
 * @file adc.hpp
 * @brief The ADC contract: `kb::iface::AdcChannel` (one converted input) and `AdcChannelRef`.
 *
 * | Member | Meaning |
 * | --- | --- |
 * | `kb::Result<uint32_t> ReadRaw() noexcept` | One conversion, in counts |
 * | `kb::Result<int32_t> ReadMillivolts() noexcept` | One conversion, calibrated by the port, in
 * millivolts | | `uint32_t FullScale() const noexcept` | The largest raw count (4095 for 12 bits) |
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The kb-core authors
 */
#ifndef KB_INTERFACES_ADC_HPP
#define KB_INTERFACES_ADC_HPP

#include <concepts>
#include <cstdint>

#include "kb/utils/result.hpp"

namespace kb::iface {

/** @brief One ADC input. */
template <typename A>
concept AdcChannel = requires(A& adc, const A& cadc) {
  { adc.ReadRaw() } noexcept -> std::same_as<kb::Result<uint32_t>>;
  { adc.ReadMillivolts() } noexcept -> std::same_as<kb::Result<int32_t>>;
  { cadc.FullScale() } noexcept -> std::same_as<uint32_t>;
};

/** @brief The function table behind an AdcChannelRef. */
struct AdcChannelOps {
  kb::Result<uint32_t> (*read_raw)(void* self) noexcept;       ///< ReadRaw.
  kb::Result<int32_t> (*read_millivolts)(void* self) noexcept; ///< ReadMillivolts.
  uint32_t (*full_scale)(const void* self) noexcept;           ///< FullScale.
};

namespace detail {
/// The AdcChannelOps of an implementation type T.
template <AdcChannel T>
inline constexpr AdcChannelOps kAdcChannelOps{
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    .read_raw = [](void* self) noexcept { return static_cast<T*>(self)->ReadRaw(); },
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    .read_millivolts = [](void* self) noexcept { return static_cast<T*>(self)->ReadMillivolts(); },
    .full_scale =
        // codeql::misra_deviation_next_line(DEV-KBC-004)
    [](const void* self) noexcept { return static_cast<const T*>(self)->FullScale(); },
};
} // namespace detail

/** @brief A non-owning, run-time chosen reference to any AdcChannel. */
class AdcChannelRef {
public:
  /** @brief Refers to `adc`, which must outlive the reference. */
  template <AdcChannel T>
  constexpr explicit AdcChannelRef(T& adc) noexcept
      : self_(&adc), ops_(&detail::kAdcChannelOps<T>) {}
  /** @brief AdcChannel::ReadRaw. */
  [[nodiscard]] kb::Result<uint32_t> ReadRaw() noexcept {
    return ops_->read_raw(self_);
  }
  /** @brief AdcChannel::ReadMillivolts. */
  [[nodiscard]] kb::Result<int32_t> ReadMillivolts() noexcept {
    return ops_->read_millivolts(self_);
  }
  /** @brief AdcChannel::FullScale. */
  [[nodiscard]] uint32_t FullScale() const noexcept {
    return ops_->full_scale(self_);
  }

private:
  void* self_;
  const AdcChannelOps* ops_;
};

static_assert(AdcChannel<AdcChannelRef>, "AdcChannelRef satisfies the contract it erases");

} // namespace kb::iface

#endif // KB_INTERFACES_ADC_HPP
