/**
 * @file state.hpp
 * @brief The port's shared state: pin claims, EXTI lines, timer ownership, the clock's wrap
 * counters and the shared ADC instances. The types and every rule over them are platform-free
 * (src/gpio.cpp, src/adc.cpp, src/pwm.cpp, src/time.cpp); the one instance lives in a boundary file
 * (sdk::State(), DEV-001), next to the HAL handles it goes with. Private to the port.
 *
 * Entries shared with an interrupt (EXTI callbacks, wrap counters) are std::atomic and are changed
 * with interrupts masked (IrqGuard), so an interrupt sees either the old entry or the new one.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_SRC_STATE_HPP
#define KB_PORT_STM32_SRC_STATE_HPP

#include <array>
// State shared with interrupts needs lock-free std::atomic (as kb-core's SpscRing does).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "kb/interfaces/gpio.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/pwm.hpp"
#include "kb/rtos/mutex.hpp"

namespace kb::port::stm32 {

inline constexpr std::size_t kGpioPorts = 11U; ///< A to K.
inline constexpr std::size_t kExtiLines = 16U; ///< EXTI0 to EXTI15.
inline constexpr std::size_t kTimers = 21U;    ///< TIM0 (unused) to TIM20.
inline constexpr std::size_t kAdcs = 6U;       ///< ADC0 (unused) to ADC5.

/** @brief One EXTI line: the callback, its context and the port of the pin that owns it. */
struct ExtiLine {
  std::atomic<kb::iface::GpioCallback> fn{nullptr};
  std::atomic<void*> context{nullptr};
  GpioPort port{GpioPort::A}; ///< Task side only, changed with interrupts masked.
  bool owned{false};          ///< Task side only, changed with interrupts masked.
};

/** @brief Who uses a timer. */
enum class TimerUse : uint8_t {
  None = 0,  ///< Free.
  Clock = 1, ///< The monotonic clock (free-running, wrap interrupt).
  Pwm = 2,   ///< PWM channels, all at one period.
};

/** @brief One timer's owner, its PWM channel count and the period they share. */
struct TimerEntry {
  TimerUse use{TimerUse::None};
  uint32_t users{0U};
  PwmPeriod period{};
};

/** @brief Where a shared ADC instance is in its life. */
enum class AdcPhase : uint8_t {
  Closed = 0,  ///< Not initialised.
  Opening = 1, ///< One channel is initialising it; others wait.
  Open = 2,    ///< Initialised; `users` channels share it.
};

/** @brief One ADC instance shared by its channels. */
struct AdcEntry {
  std::atomic<uint8_t> phase{0U};
  uint32_t users{0U};
  kb::rtos::Mutex lock;  ///< One conversion at a time.
  Completion conversion; ///< Completed by the end-of-conversion interrupt.
};

/** @brief Everything the port shares between its objects and its interrupts. */
struct PortState {
  std::array<uint16_t, kGpioPorts> pin_claims{};
  std::array<ExtiLine, kExtiLines> exti{};
  std::array<TimerEntry, kTimers> timers{};
  std::array<std::atomic<uint32_t>, kTimers> wraps{};
  std::array<std::atomic<bool>, kTimers> clock_on{}; ///< The timer runs a started Clock.
  std::atomic<uint8_t> clock_timer{0U};              ///< The first started clock's timer; 0 before.
  std::array<AdcEntry, kAdcs> adcs{};
};

/** @brief Interrupts masked for the guard's life (PRIMASK saved and restored; nests). */
class IrqGuard {
public:
  IrqGuard() noexcept;
  ~IrqGuard() noexcept;
  IrqGuard(const IrqGuard&) = delete;
  IrqGuard& operator=(const IrqGuard&) = delete;
  IrqGuard(IrqGuard&&) = delete;
  IrqGuard& operator=(IrqGuard&&) = delete;

private:
  uint32_t saved_;
};

/** @brief True when the HAL time base advances within 2 ms (measured on the DWT). */
[[nodiscard]] bool HalTickRuns() noexcept;

/** @brief Claims `pin` for one object (port_code::kOwned when another has it). */
[[nodiscard]] uint32_t ClaimPin(PinId pin) noexcept;
/** @brief Gives a claim back. */
void UnclaimPin(PinId pin) noexcept;

/** @brief The 64-bit count of the clock on `timer` (0 when it has not started). */
[[nodiscard]] uint64_t ClockNowUs(uint8_t timer) noexcept;

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_SRC_STATE_HPP
