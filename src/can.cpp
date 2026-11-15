// CanController: the CAN bit timing, frame checks, the receive ring fed by the interrupt, the
// bounded waits for a frame or a transmit slot on the port's clock, and bus-off recovery.
// Platform-free; every HAL call goes through sdk.hpp (src/stm32_can_boundary.cpp).
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <algorithm>
// State shared with interrupts needs lock-free std::atomic (as kb-core's SpscRing does).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstdint>

#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/can.hpp"
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/rtos/mutex.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
constexpr uint32_t kMinQuanta = 8U;
constexpr uint32_t kMaxQuanta = 25U;
constexpr uint32_t kSamplePermille = 875U;
constexpr uint32_t kPermille = 1000U;
constexpr uint32_t kMaxSjw = 4U;
constexpr uint32_t kMinBitrate = 10000U;
constexpr uint32_t kMaxBitrate = 1000000U;

[[nodiscard]] bool PinOk(PinId pin) noexcept {
  return pin.Used() && pin.Valid() && sdk::PortExists(pin.port);
}

// Timing of `quanta` per bit at `prescaler`, the sample point nearest 87.5 %.
[[nodiscard]] CanTiming TimingOf(uint32_t quanta, uint32_t prescaler, const CanTimingLimits& limits,
                                 bool& ok) noexcept {
  CanTiming t{};
  const uint32_t sample = ((quanta * kSamplePermille) + (kPermille / 2U)) / kPermille; // 1 + seg1
  t.prescaler = prescaler;
  t.seg1 = sample - 1U;
  t.seg2 = quanta - sample;
  t.sjw = std::min({t.seg2, kMaxSjw, limits.max_sjw});
  ok = (prescaler >= 1U) && (prescaler <= limits.max_prescaler) && (t.seg1 >= 1U) &&
       (t.seg1 <= limits.max_seg1) && (t.seg2 >= 1U) && (t.seg2 <= limits.max_seg2);
  return t;
}
[[nodiscard]] bool Created(const kb::Result<void>& r) noexcept {
  return r.IsOk() || (r.Error() == kb::Status::AlreadyStarted);
}
} // namespace

CanTiming ComputeCanTiming(uint32_t kernel_hz, uint32_t bitrate, const CanTimingLimits& limits,
                           bool& ok) noexcept {
  CanTiming t{};
  ok = false;
  if ((bitrate >= kMinBitrate) && (bitrate <= kMaxBitrate)) {
    for (uint32_t quanta = kMaxQuanta; (quanta >= kMinQuanta) && !ok; --quanta) {
      const uint64_t per_bit = uint64_t{bitrate} * quanta;
      if ((kernel_hz % per_bit) == 0U) {
        t = TimingOf(quanta, static_cast<uint32_t>(kernel_hz / per_bit), limits, ok);
      }
    }
  }
  return t;
}

// --- CanController -------------------------------------------------------------------------------

namespace {
[[nodiscard]] bool CanConfigValid(const CanConfig& config) noexcept {
  return PinOk(config.tx) && PinOk(config.rx) && ConfiguredTimeoutValid(config.lock_timeout) &&
         RtosIrqPriorityValid(config.irq_priority);
}

// The kernel clock: configured, or the controller's from the RCC (0 for a controller it lacks).
[[nodiscard]] uint32_t CanKernelHz(const CanConfig& config) noexcept {
  uint32_t kernel = config.kernel_clock_hz;
  if ((kernel == 0U) && sdk::CanExists(config.controller)) {
    kernel = sdk::CanKernelClockHz(config.controller);
  }
  return kernel;
}

// Frames taken from the hardware FIFO per interrupt at most (the deepest FIFO configured is 16).
constexpr uint32_t kMaxDrain = 32U;
} // namespace

CanController::~CanController() noexcept {
  static_cast<void>(Release());
}

kb::Status CanController::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    uint32_t code = hal::kOk;
    const uint32_t kernel = CanKernelHz(config_);
    if (!sdk::CanExists(config_.controller)) {
      code = port_code::kUnsupported;
    } else if (!CanConfigValid(config_)) {
      code = port_code::kInvalidArgument;
    } else if (kernel == 0U) {
      code = port_code::kNoClock;
    } else if (!Created(tx_mutex_.Create()) || !Created(rx_mutex_.Create()) ||
               (rx_wake_.Create() != kb::Status::Ok) || (tx_wake_.Create() != kb::Status::Ok)) {
      code = port_code::kRtosObject;
    } else if (!HalTickRuns()) {
      code = port_code::kHalTickStopped; // the HAL's INIT acknowledge waits would never end
    } else {
      bool ok = false;
      timing_ = ComputeCanTiming(kernel, config_.bitrate, sdk::CanLimits(), ok);
      rx_lost_.store(0U, std::memory_order_relaxed);
      kb::iface::CanFrame stale{};
      while (rx_ring_.TryPop(stale)) {
        // frames from before a Release(): not delivered after the reopen
      }
      code = ok ? sdk::CanOpen(config_, timing_, *this) : port_code::kInvalidArgument;
    }
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = status == kb::Status::Ok;
  }
  return status;
}

void CanController::OnRxInterrupt(bool lost) noexcept {
  uint32_t flags = lost ? can_flags::kFifoOverrun : 0U;
  uint32_t level = 0U;
  for (uint32_t i = 0U; i < kMaxDrain; ++i) {
    kb::iface::CanFrame frame{};
    const bool any = (sdk::CanRxCount(config_.controller, level) == hal::kOk) && (level > 0U);
    if (!any) {
      break;
    }
    if (sdk::CanGet(config_.controller, frame) != hal::kOk) {
      break;
    }
    if (!rx_ring_.TryPush(frame)) {
      flags |= can_flags::kRingFull;
    }
  }
  if (flags != 0U) {
    static_cast<void>(rx_lost_.fetch_or(flags, std::memory_order_relaxed));
  }
  rx_wake_.Notify();
}

void CanController::OnTxInterrupt() noexcept {
  tx_wake_.Notify();
}

kb::Status CanController::sendLocked(const kb::iface::CanFrame& frame,
                                     kb::Milliseconds timeout) noexcept {
  Deadline deadline{timeout};
  kb::Status status = kb::Status::Timeout;
  uint32_t code = hal::kTimeout;
  bool waiting = true;
  while (waiting) {
    tx_wake_.Arm();
    uint32_t free = 0U;
    code = sdk::CanTxFree(config_.controller, free);
    CanStatus s{};
    if ((code == hal::kOk) && (free > 0U)) {
      code = sdk::CanPut(config_.controller, frame);
      status = (code == hal::kOk) ? kb::Status::Ok : kb::Status::BusError;
      waiting = false;
    } else if (code != hal::kOk) {
      status = MapHalStatus(code & 0xFFU);
      waiting = false;
    } else if ((sdk::CanReadStatus(config_.controller, s) == hal::kOk) &&
               (s.state == CanState::BusOff)) {
      status = kb::Status::BusError; // no slot frees while off the bus
      code = hal::kError;
      waiting = false;
    } else if (!tx_wake_.Pause(deadline)) {
      status = kb::Status::Timeout; // no slot in time
      code = hal::kTimeout;
      waiting = false;
    } else {
      // woken by a transmit interrupt (or a slice ended): look again
    }
  }
  return error_.Set(status, code);
}

kb::Status CanController::receiveLocked(kb::iface::CanFrame& frame,
                                        kb::Milliseconds timeout) noexcept {
  Deadline deadline{timeout};
  kb::Status status = kb::Status::Timeout;
  uint32_t code = hal::kTimeout;
  bool waiting = true;
  while (waiting) {
    rx_wake_.Arm();
    const uint32_t lost = rx_lost_.exchange(0U, std::memory_order_relaxed);
    if (lost != 0U) {
      status = kb::Status::Overflow; // the frames still in the ring stay for the next Receive()
      code = lost;
      waiting = false;
    } else if (rx_ring_.TryPop(frame)) {
      status = kb::Status::Ok;
      code = hal::kOk;
      waiting = false;
    } else if (!rx_wake_.Pause(deadline)) {
      waiting = false; // nothing in time
    } else {
      // woken by a receive interrupt (or a slice ended): look again
    }
  }
  return error_.Set(status, code);
}

kb::Status CanController::Send(const kb::iface::CanFrame& frame,
                               kb::Milliseconds timeout) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  if (!CanFrameValid(frame) || !TimeoutValid(timeout)) {
    static_cast<void>(error_.Set(status, port_code::kInvalidArgument));
  } else if (!ready_) {
    status = error_.Set(kb::Status::NotReady, port_code::kNotReady);
  } else {
    const kb::rtos::LockGuard lock{tx_mutex_, config_.lock_timeout};
    status = lock.OwnsLock() ? sendLocked(frame, timeout)
                             : error_.Set(kb::Status::Busy, port_code::kLockTimeout);
  }
  return status;
}

kb::Status CanController::Receive(kb::iface::CanFrame& frame, kb::Milliseconds timeout) noexcept {
  kb::Status status = kb::Status::NotReady;
  if (!TimeoutValid(timeout)) {
    status = error_.Set(kb::Status::InvalidArgument, port_code::kInvalidArgument);
  } else if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else {
    const kb::rtos::LockGuard lock{rx_mutex_, config_.lock_timeout};
    status = lock.OwnsLock() ? receiveLocked(frame, timeout)
                             : error_.Set(kb::Status::Busy, port_code::kLockTimeout);
  }
  return status;
}

kb::Result<CanStatus> CanController::Status() noexcept {
  kb::Result<CanStatus> result =
      kb::Result<CanStatus>::Failure(kb::Status::NotReady, port_code::kNotReady);
  if (!ready_) {
    static_cast<void>(error_.Set(kb::Status::NotReady, port_code::kNotReady));
  } else {
    CanStatus s{};
    const uint32_t code = sdk::CanReadStatus(config_.controller, s);
    const kb::Status status = error_.Set(MapHalStatus(code & 0xFFU), code);
    result = (status == kb::Status::Ok) ? kb::Result<CanStatus>::Success(s)
                                        : kb::Result<CanStatus>::Failure(status, code);
  }
  return result;
}

kb::Status CanController::Recover(kb::Milliseconds timeout) noexcept {
  kb::Status status = kb::Status::NotReady;
  if (!TimeoutValid(timeout)) {
    status = error_.Set(kb::Status::InvalidArgument, port_code::kInvalidArgument);
  } else if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else {
    const kb::rtos::LockGuard lock{tx_mutex_, config_.lock_timeout};
    CanStatus s{};
    uint32_t code =
        lock.OwnsLock() ? sdk::CanReadStatus(config_.controller, s) : port_code::kLockTimeout;
    if ((code == hal::kOk) && (s.state == CanState::BusOff)) {
      // FDCAN's stop and start wait on the HAL tick: refused when it does not run.
      code = HalTickRuns() ? sdk::CanRestart(config_.controller) : port_code::kHalTickStopped;
      Deadline deadline{timeout};
      // The controller needs 128 x 11 recessive bits: look again after each bounded pause.
      while ((code == hal::kOk) && (s.state == CanState::BusOff) && tx_wake_.Pause(deadline)) {
        code = sdk::CanReadStatus(config_.controller, s);
      }
      code = ((code == hal::kOk) && (s.state == CanState::BusOff)) ? hal::kTimeout : code;
    }
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
  }
  return status;
}

kb::Status CanController::Release() noexcept {
  kb::Status status = kb::Status::Ok;
  if (ready_) {
    const uint32_t code = sdk::CanClose(config_.controller);
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = false;
  }
  return status;
}

} // namespace kb::port::stm32
