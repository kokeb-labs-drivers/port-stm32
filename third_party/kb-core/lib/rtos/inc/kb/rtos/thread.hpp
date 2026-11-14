/**
 * @file thread.hpp
 * @brief kb::rtos::Thread<Derived, StackBytes>: the CRTP thread base (ADR-0006 register) with a
 * static stack, a fixed-rate run loop, overrun counting, stop handling and step statistics.
 *
 * A derived thread supplies three hooks, public or reachable by friendship:
 *
 * | Hook | Called | Returns |
 * | --- | --- | --- |
 * | `bool Setup() noexcept` | once, on the new thread | false fails the start: the thread ends
 * `Failed` | | `bool Step() noexcept` | every period (or back to back with a zero period) | false
 * ends the loop | | `void Cleanup() noexcept` | once, last, also after a failed Setup | - |
 *
 * @code
 * class Blinker : public kb::rtos::Thread<Blinker, 2048U> {
 * public:
 *   bool Setup() noexcept { return true; }
 *   bool Step() noexcept { led_.Toggle(); return true; }
 *   void Cleanup() noexcept {}
 * };
 * Blinker blinker{};                                     // static, constexpr-constructed, no RTOS
 * call kb::rtos::ThreadConfig cfg{}; cfg.name = "blink"; cfg.period = kb::Milliseconds{500U}; if
 * (!blinker.Start(cfg).IsOk()) { ... }                // after the scheduler runs
 * @endcode
 *
 * The constructor never touches the RTOS; `Start()` creates the thread and fails closed
 * (`AlreadyStarted`, `InvalidArgument`, or the backend's status). `RequestStop()` ends the loop
 * after the current step; `Join(timeout)` waits for the end. No virtual function is involved: hooks
 * are called through `Derived`.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The kb-core authors
 */
#ifndef KB_RTOS_THREAD_HPP
#define KB_RTOS_THREAD_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "kb/rtos/backend.hpp"
#include "kb/rtos/time.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/time.hpp"

namespace kb::rtos {

/** @brief Where a thread is in its life. */
enum class ThreadState : uint8_t {
  Idle = 0,     ///< Constructed, not started.
  Starting = 1, ///< Created; Setup() has not returned yet.
  Running = 2,  ///< In the step loop.
  Stopped =
      3,      ///< The loop ended (Step() returned false or a stop was requested) and Cleanup() ran.
  Failed = 4, ///< Setup() returned false; Cleanup() ran.
};

/** @brief How a thread starts. */
struct ThreadConfig {
  const char* name{"kb"}; ///< Name for the RTOS's diagnostics.
  uint32_t priority{1U};  ///< Backend priority; higher runs first.
  Milliseconds period{
      0U}; ///< Step period on a fixed grid; 0 runs steps back to back (yielding between).
};

/** @brief Run-time statistics of a thread, read with Thread::Stats(). */
struct ThreadStats {
  uint32_t steps{0U};       ///< Steps completed.
  uint32_t overruns{0U};    ///< Periods whose wake-up had already passed.
  uint32_t max_step_us{0U}; ///< Longest step, microseconds (saturates).
};

/** @brief The hooks a Thread's Derived class must provide. */
template <typename D>
concept ThreadBody = requires(D& d) {
  { d.Setup() } noexcept -> std::same_as<bool>;
  { d.Step() } noexcept -> std::same_as<bool>;
  { d.Cleanup() } noexcept;
};

/**
 * @brief The CRTP thread base.
 * @tparam Derived The thread class (ThreadBody).
 * @tparam StackBytes Static stack size in bytes; at least 1024.
 */
template <typename Derived, std::size_t StackBytes>
  requires(StackBytes >= 1024U)
class Thread : private detail::ThreadCore {
public:
  // Public, so a derived thread can be brace-initialised as an aggregate (`Blinker blinker{};`).
  constexpr Thread() noexcept : detail::ThreadCore{&Thread::runCore} {}
  /** @brief Releases the backend's hold on an ended thread (joins it on the host backend). */
  ~Thread() {
    if (State() != ThreadState::Idle) {
      backend::ThreadRelease(storage_);
    }
  }
  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;
  Thread(Thread&&) = delete;
  Thread& operator=(Thread&&) = delete;

  /** @brief Creates the thread with `config`. Fails closed; a second call gives AlreadyStarted. */
  [[nodiscard]] kb::Result<void> Start(const ThreadConfig& config) noexcept {
    static_assert(ThreadBody<Derived>,
                  "Derived must provide Setup, Step and Cleanup, each noexcept");
    kb::Status status = kb::Status::AlreadyStarted;
    if (state_.load(std::memory_order_acquire) != static_cast<uint8_t>(ThreadState::Idle)) {
      status = kb::Status::AlreadyStarted;
    } else if (config.name == nullptr) {
      status = kb::Status::InvalidArgument;
    } else {
      period_ = config.period;
      stop_.store(false, std::memory_order_release);
      state_.store(static_cast<uint8_t>(ThreadState::Starting), std::memory_order_release);
      detail::ThreadParams params{};
      params.core = this;
      params.stack = stack_.data();
      params.stack_bytes = StackBytes;
      params.name = config.name;
      params.priority = config.priority;
      status = backend::ThreadCreate(storage_, params);
      if (status != kb::Status::Ok) {
        state_.store(static_cast<uint8_t>(ThreadState::Idle), std::memory_order_release);
      }
    }
    return kb::Result<void>::FromStatus(status);
  }

  /** @brief Asks the loop to end after the current step. */
  void RequestStop() noexcept {
    stop_.store(true, std::memory_order_release);
  }

  /** @brief True once a stop was requested. */
  [[nodiscard]] bool StopRequested() const noexcept {
    return stop_.load(std::memory_order_acquire);
  }

  /** @brief The current state. */
  [[nodiscard]] ThreadState State() const noexcept {
    return static_cast<ThreadState>(state_.load(std::memory_order_acquire));
  }

  /** @brief A snapshot of the statistics. */
  [[nodiscard]] ThreadStats Stats() const noexcept {
    ThreadStats s{};
    s.steps = steps_.load(std::memory_order_relaxed);
    s.overruns = overruns_.load(std::memory_order_relaxed);
    s.max_step_us = max_step_us_.load(std::memory_order_relaxed);
    return s;
  }

  /**
   * @brief Waits at most `timeout` for the thread to end (Stopped or Failed), polling once a
   * millisecond.
   * @return True when it ended.
   */
  [[nodiscard]] bool Join(Milliseconds timeout) noexcept {
    uint32_t waited = 0U;
    bool ended = isEnded();
    while ((!ended) && (waited < timeout.Count())) {
      SleepFor(Milliseconds{1U});
      ++waited;
      ended = isEnded();
    }
    return ended;
  }

private:
  [[nodiscard]] bool isEnded() const noexcept {
    const ThreadState s = State();
    return (s == ThreadState::Stopped) || (s == ThreadState::Failed);
  }

  // The backend calls this with the ThreadCore that Start() passed: this object's own private base.
  static void runCore(detail::ThreadCore& core) noexcept {
    static_cast<Thread&>(core).run();
  }

  void run() noexcept {
    auto& body = static_cast<Derived&>(*this);
    ThreadState end = ThreadState::Failed;
    if (body.Setup()) {
      state_.store(static_cast<uint8_t>(ThreadState::Running), std::memory_order_release);
      loop(body);
      end = ThreadState::Stopped;
    }
    body.Cleanup();
    state_.store(static_cast<uint8_t>(end), std::memory_order_release);
    backend::ThreadExit(storage_);
  }

  void loop(Derived& body) noexcept {
    Periodic grid{};
    grid.Reset();
    bool more = true;
    while (more && (!StopRequested())) {
      const kb::Instant before = backend::Now();
      more = body.Step();
      record(kb::utils::ElapsedUs(backend::Now(), before));
      if (more && (!StopRequested())) {
        pace(grid);
      }
    }
  }

  void pace(Periodic& grid) noexcept {
    if (period_.Count() == 0U) {
      backend::Yield();
    } else if (!grid.Wait(period_)) {
      static_cast<void>(overruns_.fetch_add(1U, std::memory_order_relaxed));
    } else {
      // On time: nothing to record.
    }
  }

  void record(uint64_t step_us) noexcept {
    constexpr uint64_t kMax = std::numeric_limits<uint32_t>::max();
    const auto clipped = static_cast<uint32_t>(std::min(step_us, kMax));
    if (clipped > max_step_us_.load(std::memory_order_relaxed)) {
      max_step_us_.store(clipped, std::memory_order_relaxed);
    }
    static_cast<void>(steps_.fetch_add(1U, std::memory_order_relaxed));
  }

  detail::ThreadStorage storage_{};
  alignas(detail::kStackAlign) std::array<std::byte, StackBytes> stack_{};
  Milliseconds period_;
  std::atomic<uint8_t> state_{static_cast<uint8_t>(ThreadState::Idle)};
  std::atomic<bool> stop_{false};
  std::atomic<uint32_t> steps_{0U};
  std::atomic<uint32_t> overruns_{0U};
  std::atomic<uint32_t> max_step_us_{0U};
};

} // namespace kb::rtos

#endif // KB_RTOS_THREAD_HPP
