/**
 * @file mutex.hpp
 * @brief kb::rtos::Mutex with a bounded try-lock, and the RAII LockGuard that holds it for one
 * scope.
 *
 * A mutex is constructed inert (constexpr, no RTOS call) and becomes usable with `Create()`, called
 * after the scheduler runs. Every lock is bounded: `TryLockFor(timeout)` waits at most `timeout`
 * (at least that long when it fails) and says whether it got the lock. Locking before `Create()`
 * fails.
 *
 * @code
 * kb::rtos::Mutex bus_lock{};
 * if (!bus_lock.Create().IsOk()) { ... }
 * {
 *   kb::rtos::LockGuard guard{bus_lock, kb::Milliseconds{5U}};
 *   if (!guard.OwnsLock()) { return kb::Status::Busy; }
 *   ... // exclusive access; unlocked at the end of the scope
 * }
 * @endcode
 *
 * Not callable from an interrupt handler.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The kb-core authors
 */
#ifndef KB_RTOS_MUTEX_HPP
#define KB_RTOS_MUTEX_HPP

#include "kb/rtos/backend.hpp"
#include "kb/rtos/time.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/time.hpp"

namespace kb::rtos {

/** @brief A non-recursive mutex (priority inheriting where the RTOS has it), created explicitly. */
class Mutex {
public:
  constexpr Mutex() noexcept = default;
  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
  Mutex(Mutex&&) = delete;
  Mutex& operator=(Mutex&&) = delete;
  ~Mutex() = default;

  /** @brief Creates the RTOS object. `AlreadyStarted` on a second call. */
  [[nodiscard]] kb::Result<void> Create() noexcept {
    kb::Status status = kb::Status::AlreadyStarted;
    if (!created_) {
      status = backend::MutexCreate(storage_);
      created_ = status == kb::Status::Ok;
    }
    return kb::Result<void>::FromStatus(status);
  }

  /** @brief Takes the mutex if it is free now. */
  [[nodiscard]] bool TryLock() noexcept {
    bool done = false;
    if (created_) {
      done = backend::MutexLock(storage_, 0U);
    }
    return done;
  }

  /** @brief Takes the mutex, waiting at most `timeout`; false (after at least `timeout`) when it
   * stayed held. */
  [[nodiscard]] bool TryLockFor(Milliseconds timeout) noexcept {
    bool done = false;
    if (created_) {
      done = backend::MutexLock(storage_, ToTicksAtLeast(timeout));
    }
    return done;
  }

  /** @brief Gives the mutex back. Precondition: the caller holds it. */
  void Unlock() noexcept {
    if (created_) {
      backend::MutexUnlock(storage_);
    }
  }

  /** @brief True after a successful Create(). */
  [[nodiscard]] bool IsCreated() const noexcept {
    return created_;
  }

private:
  detail::MutexStorage storage_{};
  bool created_{false};
};

/** @brief Holds a Mutex for one scope, if it could be taken within the timeout. */
class LockGuard {
public:
  /** @brief Tries to take `mutex` within `timeout`; check OwnsLock(). */
  LockGuard(Mutex& mutex, Milliseconds timeout) noexcept
      : mutex_(mutex), owns_(mutex.TryLockFor(timeout)) {}
  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;
  LockGuard(LockGuard&&) = delete;
  LockGuard& operator=(LockGuard&&) = delete;
  /** @brief Unlocks when the lock was taken. */
  ~LockGuard() {
    if (owns_) {
      mutex_.Unlock();
    }
  }

  /** @brief True when the guard holds the mutex. */
  [[nodiscard]] bool OwnsLock() const noexcept {
    return owns_;
  }

private:
  Mutex& mutex_;
  bool owns_;
};

} // namespace kb::rtos

#endif // KB_RTOS_MUTEX_HPP
