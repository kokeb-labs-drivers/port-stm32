/**
 * @file completion.hpp
 * @brief How every wait of the port is bounded: `Deadline` (a budget measured on the port's own
 * clock, never on HAL_GetTick) and `Completion` (an operation finished by an interrupt, waited for
 * on a kb::rtos semaphore).
 *
 * A Deadline measures elapsed time on the port's Clock (TIM2 or TIM5, time.hpp) once one has been
 * started, and on the Cortex-M DWT cycle counter before that. It also counts its polls and expires
 * after (budget in ms + 1) x kDeadlinePollsPerMs of them, so a wait ends even if both time sources
 * were wrong.
 *
 * A Completion is armed by the task before it starts an interrupt-driven operation, completed
 * from the interrupt (or the HAL callback) with the vendor code, and waited for with Wait(): each
 * pass takes the semaphore for at most min(remaining, kWaitSliceMs). Under an RTOS (FreeRTOS) the
 * task blocks until the interrupt gives the semaphore or the slice ends, so it uses no CPU while it
 * waits. Under kb-core's `none` backend (no scheduler) a take cannot block and returns at once:
 * the wait becomes a poll of the completion flag, still bounded by the Deadline and its poll cap.
 *
 * The flags are std::atomic: the interrupt writes the code and the value, then sets `done` with
 * release order; the task reads `done` with acquire order, then the code.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_COMPLETION_HPP
#define KB_PORT_STM32_COMPLETION_HPP

// State shared with interrupts needs lock-free std::atomic (as kb-core's SpscRing does).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstdint>

#include "kb/rtos/semaphore.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"

namespace kb::port::stm32 {

/** @brief The longest single semaphore take of a wait, so the port clock is checked again. */
inline constexpr uint32_t kWaitSliceMs = 10U;
/** @brief The poll cap per millisecond of budget (far above what any part polls in 1 ms). */
inline constexpr uint32_t kDeadlinePollsPerMs = 20000U;

/** @brief A time budget started at construction, measured on the port's clock. */
class Deadline {
public:
  /** @brief Starts a budget of `budget` now. */
  explicit Deadline(kb::Milliseconds budget) noexcept;
  /** @brief True once the budget (or the poll cap) is used up; counts one poll. */
  [[nodiscard]] bool Expired() noexcept;
  /** @brief The time left, rounded up to whole milliseconds; 0 once expired. */
  [[nodiscard]] kb::Milliseconds Remaining() noexcept;
  /** @brief Microseconds elapsed since the start. */
  [[nodiscard]] uint64_t ElapsedUs() noexcept;

private:
  uint64_t budget_us_;
  uint64_t start_us_{0U};
  uint64_t cycles_{0U};
  uint32_t last_cycles_{0U};
  uint32_t core_hz_{0U};
  uint32_t polls_{0U};
  uint32_t max_polls_;
  uint8_t clock_timer_{0U};
  bool expired_{false};
};

/** @brief One interrupt-driven operation at a time: armed, completed from the interrupt, waited. */
class Completion {
public:
  constexpr Completion() noexcept = default;
  Completion(const Completion&) = delete;
  Completion& operator=(const Completion&) = delete;
  Completion(Completion&&) = delete;
  Completion& operator=(Completion&&) = delete;
  ~Completion() = default;

  /** @brief Creates the semaphore (binary, empty); Ok when it already exists. Task context. */
  [[nodiscard]] kb::Status Create() noexcept;
  /** @brief Task context, before starting the operation: not done, stale wake-ups dropped. */
  void Arm() noexcept;
  /** @brief Interrupt context: the operation ended with vendor code `code` and `value`. */
  void Complete(uint32_t code, uint32_t value = 0U) noexcept;
  /** @brief Interrupt context: wakes a waiting task without ending the operation (data arrived). */
  void Notify() noexcept;
  /** @brief True once Complete() has run since Arm(). */
  [[nodiscard]] bool Done() const noexcept {
    return done_.load(std::memory_order_acquire);
  }
  /** @brief The code given to Complete(). */
  [[nodiscard]] uint32_t Code() const noexcept {
    return code_.load(std::memory_order_relaxed);
  }
  /** @brief The value given to Complete(). */
  [[nodiscard]] uint32_t Value() const noexcept {
    return value_.load(std::memory_order_relaxed);
  }
  /** @brief Task context: waits until Done() or `deadline` expires; true when done. */
  [[nodiscard]] bool Wait(Deadline& deadline) noexcept;
  /**
   * @brief Task context: one bounded pause until Notify(), Complete() or the slice ends; false (no
   * pause) once `deadline` has expired. For waits whose condition the caller checks itself.
   */
  [[nodiscard]] bool Pause(Deadline& deadline) noexcept;

private:
  kb::rtos::Semaphore wake_;
  std::atomic<bool> done_{false};
  std::atomic<uint32_t> code_{0U};
  std::atomic<uint32_t> value_{0U};
  bool created_{false};
};

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_COMPLETION_HPP
