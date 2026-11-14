/**
 * @file can.hpp
 * @brief The CAN contract: `kb::iface::CanBus` (a controller on one bus) and `CanBusRef`.
 *
 * | Member | Meaning |
 * | --- | --- |
 * | `kb::Status EnsureReady() noexcept` | Brings the controller onto the bus if it is not;
 * idempotent | | `kb::Status Send(const CanFrame& frame, kb::Milliseconds timeout) noexcept` |
 * Queues the frame, waiting at most `timeout` for a transmit slot | | `kb::Status Receive(CanFrame&
 * frame, kb::Milliseconds timeout) noexcept` | Takes the oldest received frame, waiting at most
 * `timeout` |
 *
 * Results include `Timeout`, `BusError` (bus-off, error passive) and `InvalidArgument` (dlc above
 * 8).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The kb-core authors
 */
#ifndef KB_INTERFACES_CAN_HPP
#define KB_INTERFACES_CAN_HPP

#include <concepts>

#include "kb/interfaces/types.hpp"
#include "kb/utils/status.hpp"
#include "kb/utils/time.hpp"

namespace kb::iface {

/** @brief A CAN controller on one bus. */
template <typename C>
concept CanBus = requires(C& can, const CanFrame& tx, CanFrame& rx, kb::Milliseconds t) {
  { can.EnsureReady() } noexcept -> std::same_as<kb::Status>;
  { can.Send(tx, t) } noexcept -> std::same_as<kb::Status>;
  { can.Receive(rx, t) } noexcept -> std::same_as<kb::Status>;
};

/** @brief The function table behind a CanBusRef. */
struct CanBusOps {
  kb::Status (*ensure_ready)(void* self) noexcept; ///< EnsureReady.
  kb::Status (*send)(void* self, const CanFrame& frame, kb::Milliseconds t) noexcept; ///< Send.
  kb::Status (*receive)(void* self, CanFrame& frame, kb::Milliseconds t) noexcept;    ///< Receive.
};

namespace detail {
/// The CanBusOps of an implementation type T.
template <CanBus T>
inline constexpr CanBusOps kCanBusOps{
    // codeql::misra_deviation_next_line(DEV-KBC-004)
    .ensure_ready = [](void* self) noexcept { return static_cast<T*>(self)->EnsureReady(); },
    .send = [](void* self, const CanFrame& frame,
               // codeql::misra_deviation_next_line(DEV-KBC-004)
               kb::Milliseconds t) noexcept { return static_cast<T*>(self)->Send(frame, t); },
    .receive = [](void* self, CanFrame& frame,
                  // codeql::misra_deviation_next_line(DEV-KBC-004)
                  kb::Milliseconds t) noexcept { return static_cast<T*>(self)->Receive(frame, t); },
};
} // namespace detail

/** @brief A non-owning, run-time chosen reference to any CanBus. */
class CanBusRef {
public:
  /** @brief Refers to `can`, which must outlive the reference. */
  template <CanBus T>
  constexpr explicit CanBusRef(T& can) noexcept : self_(&can), ops_(&detail::kCanBusOps<T>) {}
  /** @brief CanBus::EnsureReady. */
  [[nodiscard]] kb::Status EnsureReady() noexcept {
    return ops_->ensure_ready(self_);
  }
  /** @brief CanBus::Send. */
  [[nodiscard]] kb::Status Send(const CanFrame& frame, kb::Milliseconds timeout) noexcept {
    return ops_->send(self_, frame, timeout);
  }
  /** @brief CanBus::Receive. */
  [[nodiscard]] kb::Status Receive(CanFrame& frame, kb::Milliseconds timeout) noexcept {
    return ops_->receive(self_, frame, timeout);
  }

private:
  void* self_;
  const CanBusOps* ops_;
};

static_assert(CanBus<CanBusRef>, "CanBusRef satisfies the contract it erases");

} // namespace kb::iface

#endif // KB_INTERFACES_CAN_HPP
