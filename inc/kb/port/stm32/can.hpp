/**
 * @file can.hpp
 * @brief `kb::port::stm32::CanController`: one CAN controller, FDCAN in classic mode on G4 and H7
 * (HAL `stm32<family>xx_hal_fdcan`) or bxCAN on F4 and L4 (HAL `stm32<family>xx_hal_can`), which
 * satisfies `kb::iface::CanBus`. Classic CAN 2.0A/B frames; every frame is accepted into RX FIFO 0.
 *
 * Send() waits at most its timeout for a free transmit slot (FDCAN TX FIFO, bxCAN mailbox), then
 * queues the frame. Receive() waits at most its timeout for a frame in RX FIFO 0. Both poll the
 * hardware fill levels; each poll takes the controller's lock (at most `lock_timeout`) only for the
 * check and the HAL call, so a Receive() that waits does not hold off a Send(). Nothing runs in an
 * interrupt. The bit timing comes from ComputeCanTiming() at a sample point near 87.5 %. bxCAN
 * recovers from bus-off by itself (automatic bus-off management); FDCAN is restarted by Recover().
 *
 * | Result | Meaning |
 * | --- | --- |
 * | `Ok` | Queued (Send) or taken (Receive) |
 * | `Timeout` | No transmit slot or no frame within the timeout |
 * | `BusError` | Bus-off. Detail: the vendor code |
 * | `InvalidArgument` | dlc above 8, an identifier too wide, a bit rate the clock cannot make |
 * | `Unsupported` | The device has no CAN controller of this number |
 * | `Busy` | The controller lock was not had in time |
 * | `NotReady` | EnsureReady() has not succeeded |
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_CAN_HPP
#define KB_PORT_STM32_CAN_HPP

#include <cstdint>

#include "kb/interfaces/can.hpp"
#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/rtos/mutex.hpp"
#include "kb/utils/result.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"

namespace kb::port::stm32 {

/** @brief Operating mode of a CAN controller. */
enum class CanMode : uint8_t {
  Normal = 0,     ///< Sends, receives and acknowledges.
  Loopback = 1,   ///< Internal loopback: frames sent are received, the bus is not driven.
  ListenOnly = 2, ///< Bus monitoring: receives only, never drives the bus.
};

/** @brief The state of a CAN controller. */
enum class CanState : uint8_t {
  Stopped = 0,      ///< Not on the bus.
  ErrorActive = 1,  ///< On the bus, error counters below 128.
  ErrorPassive = 2, ///< On the bus, an error counter at 128 or above.
  BusOff = 3,       ///< Off the bus.
};

/** @brief Error counters and state of a CAN controller. */
struct CanStatus {
  CanState state{CanState::Stopped}; ///< State.
  uint32_t tx_error_counter{0U};     ///< TEC.
  uint32_t rx_error_counter{0U};     ///< REC.
  uint32_t last_error_code{0U};      ///< The controller's last error code field (LEC).
};

/** @brief Bit timing of a CAN controller in time quanta. */
struct CanTiming {
  uint32_t prescaler{0U}; ///< Kernel clock divider (time quantum = prescaler / kernel clock).
  uint32_t seg1{0U};      ///< Propagation and phase segment 1, in quanta.
  uint32_t seg2{0U};      ///< Phase segment 2, in quanta.
  uint32_t sjw{0U};       ///< Synchronisation jump width, in quanta.
};

/** @brief The limits of a controller's bit timing fields (bxCAN or FDCAN nominal). */
struct CanTimingLimits {
  uint32_t max_prescaler; ///< Largest prescaler.
  uint32_t max_seg1;      ///< Largest seg1.
  uint32_t max_seg2;      ///< Largest seg2.
  uint32_t max_sjw;       ///< Largest SJW.
};

/** @brief bxCAN's limits (F4, L4). */
inline constexpr CanTimingLimits kBxCanLimits{
    .max_prescaler = 1024U, .max_seg1 = 16U, .max_seg2 = 8U, .max_sjw = 4U};
/** @brief FDCAN's nominal limits (G4, H7). */
inline constexpr CanTimingLimits kFdCanLimits{
    .max_prescaler = 512U, .max_seg1 = 256U, .max_seg2 = 128U, .max_sjw = 128U};

/**
 * @brief Bit timing for `bitrate` from `kernel_hz`: the most quanta per bit (8 to 25) that divide
 * the clock exactly within `limits`, the sample point nearest 87.5 %, SJW = min(seg2, 4). `ok` is
 * false when no exact division exists.
 */
[[nodiscard]] CanTiming ComputeCanTiming(uint32_t kernel_hz, uint32_t bitrate,
                                         const CanTimingLimits& limits, bool& ok) noexcept;

/** @brief How a CAN controller is wired and timed. */
struct CanConfig {
  uint8_t controller{1U};        ///< FDCAN1/CAN1 is 1, ... 3.
  PinId tx{};                    ///< TX with its alternate function.
  PinId rx{};                    ///< RX with its alternate function.
  uint32_t bitrate{500000U};     ///< Bits per second, 10k to 1M.
  CanMode mode{CanMode::Normal}; ///< Mode.
  uint32_t kernel_clock_hz{0U};  ///< The CAN kernel clock; 0 reads it from the RCC.
  kb::Milliseconds lock_timeout{kb::Milliseconds{10U}}; ///< Wait for the controller lock.
};

/** @brief One CAN controller; satisfies kb::iface::CanBus. */
class CanController {
public:
  /** @brief Keeps `config`. */
  explicit CanController(const CanConfig& config) noexcept : config_(config) {}
  CanController(const CanController&) = delete;
  CanController& operator=(const CanController&) = delete;
  CanController(CanController&&) = delete;
  CanController& operator=(CanController&&) = delete;
  /** @brief Stops the controller if it is up. */
  ~CanController() noexcept;

  /** @brief Clock, pins, init, accept-all filter and start, once; idempotent. */
  [[nodiscard]] kb::Status EnsureReady() noexcept;
  /** @brief Queues `frame`, waiting at most `timeout` for a slot. */
  [[nodiscard]] kb::Status Send(const kb::iface::CanFrame& frame,
                                kb::Milliseconds timeout) noexcept;
  /** @brief Takes the oldest received frame, waiting at most `timeout`. */
  [[nodiscard]] kb::Status Receive(kb::iface::CanFrame& frame, kb::Milliseconds timeout) noexcept;
  /** @brief State and error counters. */
  [[nodiscard]] kb::Result<CanStatus> Status() noexcept;
  /** @brief Back on the bus after bus-off (FDCAN: stop and start; bxCAN: wait for its recovery). */
  [[nodiscard]] kb::Status Recover(kb::Milliseconds timeout) noexcept;
  /** @brief Stops and de-initialises the controller. */
  [[nodiscard]] kb::Status Release() noexcept;
  /** @brief The last call's status and its vendor code in Detail(). */
  [[nodiscard]] kb::Result<void> LastError() const noexcept {
    return error_.Get();
  }
  /** @brief The bit timing in use (after EnsureReady()). */
  [[nodiscard]] const CanTiming& Timing() const noexcept {
    return timing_;
  }

private:
  [[nodiscard]] kb::Status attempt(bool transmit, const kb::iface::CanFrame& tx,
                                   kb::iface::CanFrame& rx, bool& pending) noexcept;
  [[nodiscard]] kb::Status exchange(bool transmit, const kb::iface::CanFrame& tx,
                                    kb::iface::CanFrame& rx, kb::Milliseconds timeout) noexcept;

  CanConfig config_;
  kb::rtos::Mutex mutex_;
  LastErrorRecord error_;
  CanTiming timing_;
  bool ready_{false};
};

/** @brief True when `frame` is a frame CAN 2.0 can carry (dlc, identifier width). */
[[nodiscard]] constexpr bool CanFrameValid(const kb::iface::CanFrame& frame) noexcept {
  const uint32_t limit = frame.extended ? 0x1FFFFFFFU : 0x7FFU;
  return (frame.dlc <= kb::iface::kCanMaxDlc) && (frame.id <= limit);
}

static_assert(kb::iface::CanBus<CanController>, "CanController satisfies kb::iface::CanBus");

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_CAN_HPP
