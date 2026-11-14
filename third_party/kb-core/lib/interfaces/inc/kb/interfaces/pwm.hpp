/**
 * @file pwm.hpp
 * @brief The PWM contract: `kb::iface::PwmOutput` (one channel) and `PwmOutputRef`.
 *
 * | Member | Meaning |
 * | --- | --- |
 * | `kb::Status SetFrequencyHz(uint32_t hz) noexcept` | The period; `InvalidArgument` outside the
 * channel's range | | `kb::Status SetDuty(Duty duty) noexcept` | High time per period, 0 to 100 %
 * in steps of 0.01 % | | `kb::Status Enable(bool on) noexcept` | Starts or stops the output
 * (stopped: the idle level) |
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The kb-core authors
 */
#ifndef KB_INTERFACES_PWM_HPP
#define KB_INTERFACES_PWM_HPP

#include <concepts>
#include <cstdint>

#include "kb/interfaces/types.hpp"
#include "kb/utils/status.hpp"

namespace kb::iface {

/** @brief One PWM output channel. */
template <typename P>
concept PwmOutput = requires(P& pwm, uint32_t hz, Duty duty, bool on) {
  { pwm.SetFrequencyHz(hz) } noexcept -> std::same_as<kb::Status>;
  { pwm.SetDuty(duty) } noexcept -> std::same_as<kb::Status>;
  { pwm.Enable(on) } noexcept -> std::same_as<kb::Status>;
};

/** @brief The function table behind a PwmOutputRef. */
struct PwmOutputOps {
  kb::Status (*set_frequency_hz)(void* self, uint32_t hz) noexcept; ///< SetFrequencyHz.
  kb::Status (*set_duty)(void* self, Duty duty) noexcept;           ///< SetDuty.
  kb::Status (*enable)(void* self, bool on) noexcept;               ///< Enable.
};

namespace detail {
/// The PwmOutputOps of an implementation type T.
template <PwmOutput T>
inline constexpr PwmOutputOps kPwmOutputOps{
    .set_frequency_hz =
        // codeql::misra_deviation_next_line(DEV-KBC-004)
    [](void* self, uint32_t hz) noexcept { return static_cast<T*>(self)->SetFrequencyHz(hz); },
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    .set_duty = [](void* self, Duty duty) noexcept { return static_cast<T*>(self)->SetDuty(duty); },
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    .enable = [](void* self, bool on) noexcept { return static_cast<T*>(self)->Enable(on); },
};
} // namespace detail

/** @brief A non-owning, run-time chosen reference to any PwmOutput. */
class PwmOutputRef {
public:
  /** @brief Refers to `pwm`, which must outlive the reference. */
  template <PwmOutput T>
  constexpr explicit PwmOutputRef(T& pwm) noexcept : self_(&pwm), ops_(&detail::kPwmOutputOps<T>) {}
  /** @brief PwmOutput::SetFrequencyHz. */
  [[nodiscard]] kb::Status SetFrequencyHz(uint32_t hz) noexcept {
    return ops_->set_frequency_hz(self_, hz);
  }
  /** @brief PwmOutput::SetDuty. */
  [[nodiscard]] kb::Status SetDuty(Duty duty) noexcept {
    return ops_->set_duty(self_, duty);
  }
  /** @brief PwmOutput::Enable. */
  [[nodiscard]] kb::Status Enable(bool on) noexcept {
    return ops_->enable(self_, on);
  }

private:
  void* self_;
  const PwmOutputOps* ops_;
};

static_assert(PwmOutput<PwmOutputRef>, "PwmOutputRef satisfies the contract it erases");

} // namespace kb::iface

#endif // KB_INTERFACES_PWM_HPP
