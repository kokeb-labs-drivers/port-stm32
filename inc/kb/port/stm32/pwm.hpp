/**
 * @file pwm.hpp
 * @brief `kb::port::stm32::PwmChannel`: one output channel of an STM32 timer in PWM mode 1 (HAL
 * `stm32<family>xx_hal_tim`), which satisfies `kb::iface::PwmOutput`.
 *
 * SetFrequencyHz() picks the prescaler and auto-reload for the finest duty steps the 16-bit
 * auto-reload allows; SetDuty() writes the compare register rounded to nearest. Enable(false) holds
 * the output at its idle level by a compare of 0 (or full), so the pin is driven on every timer;
 * the counter keeps running. Channels of one timer share its frequency (the last SetFrequencyHz()
 * wins). Advanced timers (TIM1, TIM8, TIM20) get their main output enabled by the HAL.
 *
 * | Result | Meaning |
 * | --- | --- |
 * | `Ok` | Done |
 * | `InvalidArgument` | Frequency 0, above timer clock / 2, or below what the prescaler reaches;
 * bad timer, channel or pin | | `NotReady` | EnsureReady() has not succeeded |
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_PWM_HPP
#define KB_PORT_STM32_PWM_HPP

#include <cstdint>

#include "kb/interfaces/pwm.hpp"
#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"

namespace kb::port::stm32 {

/** @brief One PWM output. */
struct PwmConfig {
  uint8_t timer{3U};            ///< TIM1 is 1, ... TIM20 is 20 (where the device has it).
  uint8_t channel{1U};          ///< Timer channel 1 to 4.
  PinId pin{};                  ///< The channel's pin with its alternate function.
  uint32_t frequency_hz{1000U}; ///< Initial frequency.
  bool idle_high{false};        ///< Level while disabled.
  uint32_t timer_clock_hz{0U};  ///< The timer's clock; 0 derives it from the APB clock.
};

/** @brief Prescaler and auto-reload of a timer period. */
struct PwmPeriod {
  uint32_t prescaler{0U}; ///< PSC (divides by prescaler + 1).
  uint32_t reload{0U};    ///< ARR (the period is reload + 1 counts).
};

/**
 * @brief The period for `hz` from a timer clock of `timer_hz` with a 16-bit prescaler and
 * auto-reload: the smallest prescaler, so the finest duty steps. `ok` is false when `hz` is 0,
 * above `timer_hz` / 2 or below what a 16-bit prescaler reaches.
 */
[[nodiscard]] PwmPeriod ComputePwmPeriod(uint32_t timer_hz, uint32_t hz, bool& ok) noexcept;

/** @brief One timer channel; satisfies kb::iface::PwmOutput. */
class PwmChannel {
public:
  /** @brief Keeps `config`. */
  explicit PwmChannel(const PwmConfig& config) noexcept : config_(config) {}
  PwmChannel(const PwmChannel&) = delete;
  PwmChannel& operator=(const PwmChannel&) = delete;
  PwmChannel(PwmChannel&&) = delete;
  PwmChannel& operator=(PwmChannel&&) = delete;
  /** @brief Stops the channel. */
  ~PwmChannel() noexcept;

  /** @brief Timer clock, pin, HAL_TIM_PWM_Init and the channel, started at the idle level. */
  [[nodiscard]] kb::Status EnsureReady() noexcept;
  /** @brief The period; the duty is kept as a fraction. */
  [[nodiscard]] kb::Status SetFrequencyHz(uint32_t hz) noexcept;
  /** @brief High time per period, 0 to 100 %. Applied at once when enabled. */
  [[nodiscard]] kb::Status SetDuty(kb::iface::Duty duty) noexcept;
  /** @brief Starts the duty, or holds the idle level. */
  [[nodiscard]] kb::Status Enable(bool on) noexcept;
  /** @brief The auto-reload (period counts - 1) at the current frequency. */
  [[nodiscard]] uint32_t Reload() const noexcept {
    return period_.reload;
  }
  /** @brief The compare value for `duty` with auto-reload `reload`, rounded to nearest. */
  [[nodiscard]] static constexpr uint32_t CompareFor(kb::iface::Duty duty,
                                                     uint32_t reload) noexcept {
    const uint64_t counts = uint64_t{reload} + 1U;
    return static_cast<uint32_t>(
        ((uint64_t{duty.Permyriad()} * counts) + (kb::iface::Duty::kFull / 2U)) /
        kb::iface::Duty::kFull);
  }
  /** @brief Stops the channel (HAL_TIM_PWM_Stop). */
  [[nodiscard]] kb::Status Release() noexcept;
  /** @brief The last call's status and its vendor code in Detail(). */
  [[nodiscard]] kb::Result<void> LastError() const noexcept {
    return error_.Get();
  }

private:
  [[nodiscard]] kb::Status applyCompare() noexcept;

  PwmConfig config_;
  LastErrorRecord error_;
  kb::iface::Duty duty_;
  PwmPeriod period_;
  uint32_t timer_hz_{0U};
  bool enabled_{false};
  bool ready_{false};
};

static_assert(kb::iface::PwmOutput<PwmChannel>, "PwmChannel satisfies kb::iface::PwmOutput");

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_PWM_HPP
