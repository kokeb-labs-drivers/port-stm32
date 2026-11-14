/**
 * @file gpio.hpp
 * @brief The pin contracts: `GpioOutput`, `GpioInput`, `GpioInterrupt`, and `GpioOutputRef`,
 * `GpioInputRef`.
 *
 * | Concept | Members |
 * | --- | --- |
 * | `GpioOutput` | `kb::Status Set(Level) noexcept`, `kb::Status Toggle() noexcept` |
 * | `GpioInput` | `kb::Status Get(Level& out) noexcept` |
 * | `GpioInterrupt` | `GpioInput` plus `kb::Status EnableInterrupt(Edge, GpioCallback, void*
 * context) noexcept`, `kb::Status DisableInterrupt() noexcept` |
 *
 * Levels are electrical; whether low means "asserted" is the HAL's configuration. An interrupt
 * callback runs in interrupt context: it may only use `...FromIsr` calls of kb::rtos.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The kb-core authors
 */
#ifndef KB_INTERFACES_GPIO_HPP
#define KB_INTERFACES_GPIO_HPP

#include <concepts>

#include "kb/interfaces/types.hpp"
#include "kb/utils/status.hpp"

namespace kb::iface {

/** @brief A GPIO interrupt callback: a plain function with a context pointer, run in interrupt
 * context. */
using GpioCallback = void (*)(void* context) noexcept;

/** @brief A pin this code drives. */
template <typename P>
concept GpioOutput = requires(P& pin, Level level) {
  { pin.Set(level) } noexcept -> std::same_as<kb::Status>;
  { pin.Toggle() } noexcept -> std::same_as<kb::Status>;
};

/** @brief A pin this code reads. */
template <typename P>
concept GpioInput = requires(P& pin, Level& out) {
  { pin.Get(out) } noexcept -> std::same_as<kb::Status>;
};

/** @brief An input pin that can call a function on an edge. */
template <typename P>
concept GpioInterrupt =
    GpioInput<P> && requires(P& pin, Edge edge, GpioCallback fn, void* context) {
      { pin.EnableInterrupt(edge, fn, context) } noexcept -> std::same_as<kb::Status>;
      { pin.DisableInterrupt() } noexcept -> std::same_as<kb::Status>;
    };

/** @brief The function table behind a GpioOutputRef. */
struct GpioOutputOps {
  kb::Status (*set)(void* self, Level level) noexcept; ///< Set.
  kb::Status (*toggle)(void* self) noexcept;           ///< Toggle.
};

/** @brief The function table behind a GpioInputRef. */
struct GpioInputOps {
  kb::Status (*get)(void* self, Level& out) noexcept; ///< Get.
};

namespace detail {
/// The GpioOutputOps of an implementation type T.
template <GpioOutput T>
inline constexpr GpioOutputOps kGpioOutputOps{
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    .set = [](void* self, Level level) noexcept { return static_cast<T*>(self)->Set(level); },
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    .toggle = [](void* self) noexcept { return static_cast<T*>(self)->Toggle(); },
};
/// The GpioInputOps of an implementation type T.
template <GpioInput T>
inline constexpr GpioInputOps kGpioInputOps{
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    .get = [](void* self, Level& out) noexcept { return static_cast<T*>(self)->Get(out); },
};
} // namespace detail

/**
 * @brief A non-owning, run-time chosen reference to any GpioOutput: one board can mix an MCU pin
 * and an I/O-expander pin behind the same type.
 */
class GpioOutputRef {
public:
  /** @brief Refers to `pin`, which must outlive the reference. */
  template <GpioOutput T>
  constexpr explicit GpioOutputRef(T& pin) noexcept
      : self_(&pin), ops_(&detail::kGpioOutputOps<T>) {}
  /** @brief GpioOutput::Set. */
  [[nodiscard]] kb::Status Set(Level level) noexcept {
    return ops_->set(self_, level);
  }
  /** @brief GpioOutput::Toggle. */
  [[nodiscard]] kb::Status Toggle() noexcept {
    return ops_->toggle(self_);
  }

private:
  void* self_;
  const GpioOutputOps* ops_;
};

/** @brief A non-owning, run-time chosen reference to any GpioInput. */
class GpioInputRef {
public:
  /** @brief Refers to `pin`, which must outlive the reference. */
  template <GpioInput T>
  constexpr explicit GpioInputRef(T& pin) noexcept : self_(&pin), ops_(&detail::kGpioInputOps<T>) {}
  /** @brief GpioInput::Get. */
  [[nodiscard]] kb::Status Get(Level& out) noexcept {
    return ops_->get(self_, out);
  }

private:
  void* self_;
  const GpioInputOps* ops_;
};

static_assert(GpioOutput<GpioOutputRef>, "GpioOutputRef satisfies the contract it erases");
static_assert(GpioInput<GpioInputRef>, "GpioInputRef satisfies the contract it erases");

} // namespace kb::iface

#endif // KB_INTERFACES_GPIO_HPP
