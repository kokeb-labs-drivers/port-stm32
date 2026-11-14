/**
 * @file time.hpp
 * @brief The clock and delay contracts: `MonotonicClock` (the one 64-bit microsecond clock above
 * the ports) and `BusyWait` (short delays below one millisecond), with `MonotonicClockRef` and
 * `BusyWaitRef`.
 *
 * | Concept | Member | Meaning |
 * | --- | --- | --- |
 * | `MonotonicClock` | `kb::Instant Now() const noexcept` | Microseconds since an arbitrary start;
 * never goes back, never wraps in practice | | `BusyWait` | `void WaitUs(kb::Microseconds us)
 * noexcept` | Spins for at least `us`; for waits below 1 ms (longer ones use kb::rtos::SleepFor) |
 *
 * Driver hooks with a 32-bit clock narrow `Now()` with kb::utils::NarrowUs / NarrowMs and compare
 * by unsigned elapsed time (kb::utils::ElapsedSince).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The kb-core authors
 */
#ifndef KB_INTERFACES_TIME_HPP
#define KB_INTERFACES_TIME_HPP

#include <concepts>

#include "kb/utils/time.hpp"

namespace kb::iface {

/** @brief The 64-bit monotonic microsecond clock. */
template <typename C>
concept MonotonicClock = requires(const C& clock) {
  { clock.Now() } noexcept -> std::same_as<kb::Instant>;
};

/** @brief A short spin delay. */
template <typename D>
concept BusyWait = requires(D& delay, kb::Microseconds us) {
  { delay.WaitUs(us) } noexcept -> std::same_as<void>;
};

/** @brief The function table behind a MonotonicClockRef. */
struct MonotonicClockOps {
  kb::Instant (*now)(const void* self) noexcept; ///< Now.
};

/** @brief The function table behind a BusyWaitRef. */
struct BusyWaitOps {
  void (*wait_us)(void* self, kb::Microseconds us) noexcept; ///< WaitUs.
};

namespace detail {
/// The MonotonicClockOps of an implementation type T.
template <MonotonicClock T>
inline constexpr MonotonicClockOps kMonotonicClockOps{
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    [](const void* self) noexcept { return static_cast<const T*>(self)->Now(); },
};
/// The BusyWaitOps of an implementation type T.
template <BusyWait T>
inline constexpr BusyWaitOps kBusyWaitOps{
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    [](void* self, kb::Microseconds us) noexcept { static_cast<T*>(self)->WaitUs(us); },
};
} // namespace detail

/** @brief A non-owning, run-time chosen reference to any MonotonicClock. */
class MonotonicClockRef {
public:
  /** @brief Refers to `clock`, which must outlive the reference. */
  template <MonotonicClock T>
  constexpr explicit MonotonicClockRef(const T& clock) noexcept
      : self_(&clock), ops_(&detail::kMonotonicClockOps<T>) {}
  /** @brief MonotonicClock::Now. */
  [[nodiscard]] kb::Instant Now() const noexcept {
    return ops_->now(self_);
  }

private:
  const void* self_;
  const MonotonicClockOps* ops_;
};

/** @brief A non-owning, run-time chosen reference to any BusyWait. */
class BusyWaitRef {
public:
  /** @brief Refers to `delay`, which must outlive the reference. */
  template <BusyWait T>
  constexpr explicit BusyWaitRef(T& delay) noexcept
      : self_(&delay), ops_(&detail::kBusyWaitOps<T>) {}
  /** @brief BusyWait::WaitUs. */
  void WaitUs(kb::Microseconds us) noexcept {
    ops_->wait_us(self_, us);
  }

private:
  void* self_;
  const BusyWaitOps* ops_;
};

static_assert(MonotonicClock<MonotonicClockRef>,
              "MonotonicClockRef satisfies the contract it erases");
static_assert(BusyWait<BusyWaitRef>, "BusyWaitRef satisfies the contract it erases");

} // namespace kb::iface

#endif // KB_INTERFACES_TIME_HPP
