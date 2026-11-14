// CanController, Clock, BusyWait and the log sinks: the CAN bit timing, frame checks, bounded waits
// on the controller's fill levels and bus-off recovery; the 64-bit clock from its 32-bit samples;
// the DWT spin; the log line format. Platform-free; every HAL call goes through sdk.hpp
// (src/stm32_can_boundary.cpp, src/stm32_time_boundary.cpp).
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
// The LogSink contract takes a std::string_view (kb/interfaces/log.hpp).
// misra-lint: ignore KB-CPP-012
#include <string_view>

#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/can.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/log.hpp"
#include "kb/port/stm32/time.hpp"
#include "kb/port/stm32/uart.hpp"
#include "kb/rtos/mutex.hpp"
#include "kb/rtos/time.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"
#include "sdk.hpp"

namespace kb::port::stm32 {

namespace {
constexpr uint32_t kMinQuanta = 8U;
constexpr uint32_t kMaxQuanta = 25U;
constexpr uint32_t kSamplePermille = 875U;
constexpr uint32_t kPermille = 1000U;
constexpr uint32_t kMaxSjw = 4U;
constexpr uint32_t kMinBitrate = 10000U;
constexpr uint32_t kMaxBitrate = 1000000U;

[[nodiscard]] bool Within(uint32_t start, uint32_t timeout) noexcept {
  return kb::utils::ElapsedSince(sdk::TickMs(), start) < timeout;
}

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

CanController::~CanController() noexcept {
  static_cast<void>(Release());
}

kb::Status CanController::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    uint32_t code = hal::kOk;
    if (!sdk::CanExists(config_.controller)) {
      code = port_code::kUnsupported;
    } else if (!PinOk(config_.tx) || !PinOk(config_.rx) || (config_.lock_timeout.Count() == 0U)) {
      code = port_code::kInvalidArgument;
    } else {
      bool ok = false;
      const uint32_t kernel = (config_.kernel_clock_hz != 0U)
                                  ? config_.kernel_clock_hz
                                  : sdk::CanKernelClockHz(config_.controller);
      timing_ = ComputeCanTiming(kernel, config_.bitrate, sdk::CanLimits(), ok);
      static_cast<void>(mutex_.Create());
      code = ok ? sdk::CanOpen(config_, timing_) : port_code::kInvalidArgument;
    }
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    ready_ = status == kb::Status::Ok;
  }
  return status;
}

kb::Status CanController::attempt(bool transmit, const kb::iface::CanFrame& tx,
                                  kb::iface::CanFrame& rx, bool& pending) noexcept {
  pending = false;
  const kb::rtos::LockGuard lock{mutex_, config_.lock_timeout};
  kb::Status status = kb::Status::Busy;
  uint32_t code = port_code::kLockTimeout;
  if (lock.OwnsLock()) {
    uint32_t level = 0U;
    code = transmit ? sdk::CanTxFree(config_.controller, level)
                    : sdk::CanRxCount(config_.controller, level);
    status = MapHalStatus(code & 0xFFU);
    if ((status == kb::Status::Ok) && (level > 0U)) {
      code = transmit ? sdk::CanPut(config_.controller, tx) : sdk::CanGet(config_.controller, rx);
      status = MapHalStatus(code & 0xFFU);
      status = (transmit && (status != kb::Status::Ok)) ? kb::Status::BusError : status;
    } else if (status == kb::Status::Ok) {
      CanStatus s{};
      code = sdk::CanReadStatus(config_.controller, s);
      const bool bus_off = (code == hal::kOk) && transmit && (s.state == CanState::BusOff);
      status = bus_off ? kb::Status::BusError : MapHalStatus(code & 0xFFU);
      pending = status == kb::Status::Ok; // nothing yet: try again until the timeout
      code = bus_off ? hal::kError : code;
    } else {
      // the HAL refused the fill-level read: status and code as they are
    }
  }
  return error_.Set(status, code);
}

kb::Status CanController::exchange(bool transmit, const kb::iface::CanFrame& tx,
                                   kb::iface::CanFrame& rx, kb::Milliseconds timeout) noexcept {
  const uint32_t start = sdk::TickMs();
  bool pending = false;
  kb::Status status = attempt(transmit, tx, rx, pending);
  while (pending && Within(start, timeout.Count())) {
    kb::rtos::Yield();
    status = attempt(transmit, tx, rx, pending);
  }
  if (pending) {
    status = error_.Set(kb::Status::Timeout, hal::kTimeout); // no slot, or no frame, in time
  }
  return status;
}

kb::Status CanController::Send(const kb::iface::CanFrame& frame,
                               kb::Milliseconds timeout) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  if (!CanFrameValid(frame)) {
    static_cast<void>(error_.Set(status, port_code::kInvalidArgument));
  } else if (!ready_) {
    status = error_.Set(kb::Status::NotReady, port_code::kNotReady);
  } else {
    kb::iface::CanFrame unused{};
    status = exchange(true, frame, unused, timeout);
  }
  return status;
}

kb::Status CanController::Receive(kb::iface::CanFrame& frame, kb::Milliseconds timeout) noexcept {
  kb::Status status = kb::Status::NotReady;
  if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else {
    status = exchange(false, kb::iface::CanFrame{}, frame, timeout);
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
  if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else {
    const kb::rtos::LockGuard lock{mutex_, config_.lock_timeout};
    CanStatus s{};
    uint32_t code =
        lock.OwnsLock() ? sdk::CanReadStatus(config_.controller, s) : port_code::kLockTimeout;
    if ((code == hal::kOk) && (s.state == CanState::BusOff)) {
      code = sdk::CanRestart(config_.controller);
      const uint32_t start = sdk::TickMs();
      while ((code == hal::kOk) && (s.state == CanState::BusOff) &&
             Within(start, timeout.Count())) {
        kb::rtos::Yield();
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

// --- Clock, BusyWait -----------------------------------------------------------------------------

kb::Status Clock::Start() noexcept {
  const uint32_t code =
      sdk::ClockTimerExists(config_.timer) ? sdk::ClockStart(config_) : port_code::kInvalidArgument;
  return error_.Set(MapHalStatus(code & 0xFFU), code);
}

kb::Instant Clock::Now() const noexcept {
  uint32_t before = 0U;
  uint32_t counter = 0U;
  uint32_t after = 0U;
  bool pending = false;
  sdk::ClockSample(config_.timer, before, counter, after, pending);
  return kb::Instant{Combine(before, counter, after, pending)};
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract calls a member
void BusyWait::WaitUs(kb::Microseconds us) noexcept {
  constexpr uint64_t kStep = 0x40000000U; // a quarter of the counter: never wraps within a step
  uint64_t left = Cycles(us.Count(), sdk::CoreClockHz());
  while (left > 0U) {
    const auto step = static_cast<uint32_t>(std::min(left, kStep));
    const uint32_t start = sdk::Cycles();
    while ((sdk::Cycles() - start) < step) {
    }
    left -= step;
  }
}

// --- log --------------------------------------------------------------------------------------

std::size_t FormatLogLine(kb::iface::LogLevel level, std::string_view text,
                          std::span<char> out) noexcept {
  constexpr std::size_t kFrame = 4U; // "<letter> " and "\r\n"
  std::size_t n = 0U;
  if (out.size() >= kFrame) {
    char letter = 'D';
    if (level == kb::iface::LogLevel::Error) {
      letter = 'E';
    } else if (level == kb::iface::LogLevel::Warning) {
      letter = 'W';
    } else if (level == kb::iface::LogLevel::Info) {
      letter = 'I';
    } else {
      letter = 'D';
    }
    out[0] = letter;
    out[1] = ' ';
    const std::size_t body = std::min({text.size(), kLogMaxLine, out.size() - kFrame});
    for (std::size_t i = 0U; i < body; ++i) {
      out[2U + i] = text[i];
    }
    out[2U + body] = '\r';
    out[3U + body] = '\n';
    n = kFrame + body;
  }
  return n;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): the contract calls a member
void ItmLogSink::Write(kb::iface::LogLevel level, std::string_view text) noexcept {
  std::array<char, kLogMaxLine + 4U> line{};
  const std::size_t n = FormatLogLine(level, text, line);
  bool sent = sdk::ItmEnabled();
  for (std::size_t i = 0U; sent && (i < n); ++i) {
    sent = sdk::ItmPut(line[i], kItmWaitSpins);
  }
}

void UartLogSink::Write(kb::iface::LogLevel level, std::string_view text) noexcept {
  std::array<char, kLogMaxLine + 4U> line{};
  const std::size_t n = FormatLogLine(level, text, line);
  std::array<uint8_t, kLogMaxLine + 4U> bytes{};
  for (std::size_t i = 0U; i < n; ++i) {
    bytes[i] = std::bit_cast<uint8_t>(line[i]); // the same bits, no numeric conversion
  }
  static_cast<void>(uart_.Write(std::span<const uint8_t>{bytes.data(), n}));
}

} // namespace kb::port::stm32
