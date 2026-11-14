/**
 * @file adc.hpp
 * @brief `kb::port::stm32::AdcChannel`: one regular channel of an STM32 ADC (HAL
 * `stm32<family>xx_hal_adc`, single conversion, polled), which satisfies `kb::iface::AdcChannel`.
 *
 * The ADC instance is shared by every channel object on it (initialised once, counted); a
 * conversion selects the channel, starts, polls for the end of conversion at most
 * `conversion_timeout` and stops. On G4, H7 and L4 the ADC's offset calibration runs once at
 * EnsureReady()
 * (`HAL_ADCEx_Calibration_Start`); F4 has none. ReadMillivolts() scales the raw value by the
 * reference voltage the board has on VREF+ (`vref_mv`).
 *
 * | Result | Meaning |
 * | --- | --- |
 * | `Ok` | One conversion |
 * | `Timeout` | The conversion did not finish within `conversion_timeout` |
 * | `InvalidArgument` | Instance or channel the device does not have, or vref_mv 0 |
 * | `Busy` | The instance lock was not had in time |
 * | `NotReady` | EnsureReady() has not succeeded |
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_ADC_HPP
#define KB_PORT_STM32_ADC_HPP

#include <cstdint>

#include "kb/interfaces/adc.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"

namespace kb::port::stm32 {

/** @brief Sampling time class; the cycles per class are the family's nearest (adc.md). */
enum class AdcSampling : uint8_t {
  Short = 0,  ///< Low source impedance.
  Medium = 1, ///< Default.
  Long = 2,   ///< High source impedance, internal channels.
};

/** @brief One ADC input. */
struct AdcConfig {
  uint8_t instance{1U}; ///< ADC1 is 1, ... ADC5 is 5.
  uint8_t channel{0U};  ///< Channel number 0 to 19 (ADC_CHANNEL_<n>).
  PinId pin{};          ///< The pin to set analog, or unused (internal channel).
  AdcSampling sampling{AdcSampling::Medium}; ///< Sampling time.
  uint32_t vref_mv{3300U};                   ///< The VREF+ voltage of the board, in millivolts.
  kb::Milliseconds conversion_timeout{kb::Milliseconds{5U}}; ///< End-of-conversion wait.
  kb::Milliseconds lock_timeout{kb::Milliseconds{10U}};      ///< Wait for the shared instance.
};

/** @brief One ADC input; satisfies kb::iface::AdcChannel. */
class AdcChannel {
public:
  /** @brief Keeps `config`. */
  explicit AdcChannel(const AdcConfig& config) noexcept : config_(config) {}
  AdcChannel(const AdcChannel&) = delete;
  AdcChannel& operator=(const AdcChannel&) = delete;
  AdcChannel(AdcChannel&&) = delete;
  AdcChannel& operator=(AdcChannel&&) = delete;
  /** @brief Releases the channel. */
  ~AdcChannel() noexcept;

  /** @brief Opens (or shares) the instance and sets the pin analog; idempotent. */
  [[nodiscard]] kb::Status EnsureReady() noexcept;
  /** @brief One conversion, in counts (0 to FullScale()). Detail(): the vendor code. */
  [[nodiscard]] kb::Result<uint32_t> ReadRaw() noexcept;
  /** @brief One conversion scaled by `vref_mv`, in millivolts. */
  [[nodiscard]] kb::Result<int32_t> ReadMillivolts() noexcept;
  /** @brief The largest count: 4095 at 12 bits (F4, G4, L4), 65535 at 16 bits (H7). */
  [[nodiscard]] uint32_t FullScale() const noexcept {
    return full_scale_;
  }
  /** @brief Frees the instance when no other channel uses it. */
  [[nodiscard]] kb::Status Release() noexcept;
  /** @brief The last call's status and its vendor code in Detail(). */
  [[nodiscard]] kb::Result<void> LastError() const noexcept {
    return error_.Get();
  }
  /** @brief Millivolts of `raw` counts of `full_scale` at `vref_mv`, rounded to nearest. */
  [[nodiscard]] static constexpr int32_t ToMillivolts(uint32_t raw, uint32_t full_scale,
                                                      uint32_t vref_mv) noexcept {
    const uint64_t num = (uint64_t{raw} * vref_mv) + (uint64_t{full_scale} / 2U);
    return static_cast<int32_t>(num / full_scale);
  }

private:
  AdcConfig config_;
  LastErrorRecord error_;
  uint32_t full_scale_{0U};
  bool ready_{false};
};

static_assert(kb::iface::AdcChannel<AdcChannel>, "AdcChannel satisfies kb::iface::AdcChannel");

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_ADC_HPP
