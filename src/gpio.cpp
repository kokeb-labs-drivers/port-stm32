// OutputPin and InputPin: pin claims, level conversion and the EXTI hook. Platform-free; every HAL
// call goes through sdk.hpp (src/stm32_io_boundary.cpp).
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#include <cstdint>

#include "kb/interfaces/gpio.hpp"
#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/gpio.hpp"
#include "kb/utils/status.hpp"
#include "sdk.hpp"

namespace kb::port::stm32 {

namespace {
[[nodiscard]] bool PinOk(PinId pin) noexcept {
  return pin.Used() && pin.Valid() && sdk::PortExists(pin.port);
}

// Claims the pin, then configures it; the claim is given back when configuring fails.
template <typename Fn>
[[nodiscard]] uint32_t ClaimAndConfigure(PinId pin, Fn configure) noexcept {
  uint32_t code = sdk::PinClaim(pin);
  if (code == hal::kOk) {
    code = configure();
    if (code != hal::kOk) {
      sdk::PinUnclaim(pin);
    }
  }
  return code;
}
} // namespace

// --- OutputPin -----------------------------------------------------------------------------------

OutputPin::~OutputPin() noexcept {
  static_cast<void>(Release());
}

kb::Status OutputPin::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    if (!PinOk(config_.pin)) {
      status = error_.Set(kb::Status::InvalidArgument, port_code::kInvalidArgument);
    } else {
      const OutputConfig& c = config_;
      const uint32_t code = ClaimAndConfigure(c.pin, [&c]() noexcept {
        return sdk::PinOutput(c.pin, c.open_drain, c.pull, c.speed,
                              c.initial == kb::iface::Level::High);
      });
      status = error_.Set(MapHalStatus(code & 0xFFU), code);
      ready_ = status == kb::Status::Ok;
    }
  }
  return status;
}

kb::Status OutputPin::Set(kb::iface::Level level) noexcept {
  kb::Status status = kb::Status::NotReady;
  if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else {
    sdk::PinWrite(config_.pin, level == kb::iface::Level::High);
    status = error_.Clear();
  }
  return status;
}

kb::Status OutputPin::Toggle() noexcept {
  kb::Status status = kb::Status::NotReady;
  if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else {
    sdk::PinToggle(config_.pin);
    status = error_.Clear();
  }
  return status;
}

kb::Status OutputPin::Release() noexcept {
  if (ready_) {
    sdk::PinReset(config_.pin);
    sdk::PinUnclaim(config_.pin);
    ready_ = false;
  }
  return error_.Clear();
}

// --- InputPin ------------------------------------------------------------------------------------

InputPin::~InputPin() noexcept {
  static_cast<void>(Release());
}

kb::Status InputPin::EnsureReady() noexcept {
  kb::Status status = kb::Status::Ok;
  if (!ready_) {
    if (!PinOk(config_.pin)) {
      status = error_.Set(kb::Status::InvalidArgument, port_code::kInvalidArgument);
    } else {
      const InputConfig& c = config_;
      const uint32_t code =
          ClaimAndConfigure(c.pin, [&c]() noexcept { return sdk::PinInput(c.pin, c.pull); });
      status = error_.Set(MapHalStatus(code & 0xFFU), code);
      ready_ = status == kb::Status::Ok;
    }
  }
  return status;
}

kb::Status InputPin::Get(kb::iface::Level& out) noexcept {
  kb::Status status = kb::Status::NotReady;
  if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else {
    out = sdk::PinRead(config_.pin) ? kb::iface::Level::High : kb::iface::Level::Low;
    status = error_.Clear();
  }
  return status;
}

kb::Status InputPin::EnableInterrupt(kb::iface::Edge edge, kb::iface::GpioCallback fn,
                                     void* context) noexcept {
  kb::Status status = kb::Status::InvalidArgument;
  if (fn == nullptr) {
    static_cast<void>(error_.Set(status, port_code::kInvalidArgument));
  } else if (!ready_) {
    status = error_.Set(kb::Status::NotReady, port_code::kNotReady);
  } else {
    const uint32_t code = sdk::ExtiEnable(config_.pin, edge, fn, context, config_.irq_priority);
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    irq_ = irq_ || (status == kb::Status::Ok);
  }
  return status;
}

kb::Status InputPin::DisableInterrupt() noexcept {
  kb::Status status = kb::Status::NotReady;
  if (!ready_) {
    static_cast<void>(error_.Set(status, port_code::kNotReady));
  } else {
    const uint32_t code = irq_ ? sdk::ExtiDisable(config_.pin) : hal::kOk;
    status = error_.Set(MapHalStatus(code & 0xFFU), code);
    irq_ = false;
  }
  return status;
}

kb::Status InputPin::Release() noexcept {
  kb::Status status = kb::Status::Ok;
  if (ready_) {
    status = DisableInterrupt();
    sdk::PinReset(config_.pin);
    sdk::PinUnclaim(config_.pin);
    ready_ = false;
  }
  return status;
}

} // namespace kb::port::stm32
