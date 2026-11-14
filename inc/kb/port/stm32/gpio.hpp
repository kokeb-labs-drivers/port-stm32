/**
 * @file gpio.hpp
 * @brief `kb::port::stm32::OutputPin` (satisfies `kb::iface::GpioOutput`) and
 * `kb::port::stm32::InputPin` (satisfies `kb::iface::GpioInput` and `GpioInterrupt`), over HAL GPIO
 * and EXTI.
 *
 * Levels are electrical. Set() writes BSRR (atomic); Toggle() reads the output data register and
 * writes BSRR, with interrupts masked between the two, so it is atomic too. One object per pin: two
 * objects on one pin are refused with `Busy`. EXTI has one line per pin number: two InputPins with
 * the same number on different ports cannot both have an interrupt (`Busy`). The interrupt callback
 * runs in interrupt context (HandleExtiIrq(), irq.hpp) and may only use the `...FromIsr` calls of
 * kb::rtos.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_GPIO_HPP
#define KB_PORT_STM32_GPIO_HPP

#include <cstdint>

#include "kb/interfaces/gpio.hpp"
#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"

namespace kb::port::stm32 {

/** @brief Internal pull resistors of a pin. */
enum class Pull : uint8_t {
  None = 0, ///< Floating.
  Up = 1,   ///< Weak pull-up (about 40 kOhm).
  Down = 2, ///< Weak pull-down.
};

/** @brief Output speed (slew rate) of a pin. */
enum class Speed : uint8_t {
  Low = 0,      ///< Lowest edge rate, least noise.
  Medium = 1,   ///< Medium.
  High = 2,     ///< High.
  VeryHigh = 3, ///< Highest.
};

/** @brief An output pin. */
struct OutputConfig {
  PinId pin{};                                     ///< Port and number.
  bool open_drain{false};                          ///< Open drain instead of push-pull.
  Pull pull{Pull::None};                           ///< Pull resistor.
  Speed speed{Speed::Low};                         ///< Edge rate.
  kb::iface::Level initial{kb::iface::Level::Low}; ///< Level driven by EnsureReady().
};

/** @brief An input pin. */
struct InputConfig {
  PinId pin{};              ///< Port and number.
  Pull pull{Pull::None};    ///< Pull resistor.
  uint8_t irq_priority{5U}; ///< NVIC priority of its EXTI interrupt.
};

/** @brief One output pin; satisfies kb::iface::GpioOutput. */
class OutputPin {
public:
  /** @brief Keeps `config`. */
  explicit OutputPin(const OutputConfig& config) noexcept : config_(config) {}
  OutputPin(const OutputPin&) = delete;
  OutputPin& operator=(const OutputPin&) = delete;
  OutputPin(OutputPin&&) = delete;
  OutputPin& operator=(OutputPin&&) = delete;
  /** @brief Gives the pin back (analog, its reset state). */
  ~OutputPin() noexcept;

  /** @brief Port clock, the initial level, then HAL_GPIO_Init as output; idempotent. */
  [[nodiscard]] kb::Status EnsureReady() noexcept;
  /** @brief Drives `level`. */
  [[nodiscard]] kb::Status Set(kb::iface::Level level) noexcept;
  /** @brief Drives the opposite of the driven level. */
  [[nodiscard]] kb::Status Toggle() noexcept;
  /** @brief HAL_GPIO_DeInit of the pin. */
  [[nodiscard]] kb::Status Release() noexcept;
  /** @brief The last call's status and its vendor code in Detail(). */
  [[nodiscard]] kb::Result<void> LastError() const noexcept {
    return error_.Get();
  }

private:
  OutputConfig config_;
  LastErrorRecord error_;
  bool ready_{false};
};

/** @brief One input pin; satisfies kb::iface::GpioInput and GpioInterrupt. */
class InputPin {
public:
  /** @brief Keeps `config`. */
  explicit InputPin(const InputConfig& config) noexcept : config_(config) {}
  InputPin(const InputPin&) = delete;
  InputPin& operator=(const InputPin&) = delete;
  InputPin(InputPin&&) = delete;
  InputPin& operator=(InputPin&&) = delete;
  /** @brief Disables the interrupt and gives the pin back. */
  ~InputPin() noexcept;

  /** @brief Port clock and HAL_GPIO_Init as input; idempotent. */
  [[nodiscard]] kb::Status EnsureReady() noexcept;
  /** @brief The level the pin reads. */
  [[nodiscard]] kb::Status Get(kb::iface::Level& out) noexcept;
  /** @brief Calls `fn(context)` in interrupt context on `edge`; replaces an earlier callback. */
  [[nodiscard]] kb::Status EnableInterrupt(kb::iface::Edge edge, kb::iface::GpioCallback fn,
                                           void* context) noexcept;
  /** @brief Stops calling the callback and frees the EXTI line. */
  [[nodiscard]] kb::Status DisableInterrupt() noexcept;
  /** @brief Disables the interrupt and HAL_GPIO_DeInit of the pin. */
  [[nodiscard]] kb::Status Release() noexcept;
  /** @brief The last call's status and its vendor code in Detail(). */
  [[nodiscard]] kb::Result<void> LastError() const noexcept {
    return error_.Get();
  }

private:
  InputConfig config_;
  LastErrorRecord error_;
  bool ready_{false};
  bool irq_{false};
};

static_assert(kb::iface::GpioOutput<OutputPin>, "OutputPin satisfies kb::iface::GpioOutput");
static_assert(kb::iface::GpioInput<InputPin>, "InputPin satisfies kb::iface::GpioInput");
static_assert(kb::iface::GpioInterrupt<InputPin>, "InputPin satisfies kb::iface::GpioInterrupt");

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_GPIO_HPP
