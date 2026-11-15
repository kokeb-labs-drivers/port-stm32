// OutputPin and InputPin: pin claims, level conversion, the EXTI line table and its dispatch from
// the interrupt. Platform-free; every HAL call goes through sdk.hpp (src/boundary/gpio.cpp).
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
// State shared with interrupts needs lock-free std::atomic (as kb-core's SpscRing does).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "kb/interfaces/gpio.hpp"
#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/gpio.hpp"
#include "kb/port/stm32/irq.hpp"
#include "kb/utils/status.hpp"
#include "sdk.hpp"
#include "state.hpp"

namespace kb::port::stm32 {

namespace {
[[nodiscard]] bool PinOk(PinId pin) noexcept {
  return pin.Used() && pin.Valid() && sdk::PortExists(pin.port);
}

[[nodiscard]] uint16_t MaskOf(PinId pin) noexcept {
  return static_cast<uint16_t>(1U << pin.pin);
}

// Claims the pin, then configures it; the claim is given back when configuring fails.
template <typename Fn>
[[nodiscard]] uint32_t ClaimAndConfigure(PinId pin, Fn configure) noexcept {
  uint32_t code = ClaimPin(pin);
  if (code == hal::kOk) {
    code = configure();
    if (code != hal::kOk) {
      UnclaimPin(pin);
    }
  }
  return code;
}
} // namespace

uint32_t ClaimPin(PinId pin) noexcept {
  const IrqGuard guard{};
  uint16_t& claims = sdk::State().pin_claims[static_cast<std::size_t>(pin.port)];
  const bool free = (claims & MaskOf(pin)) == 0U;
  if (free) {
    claims = static_cast<uint16_t>(claims | MaskOf(pin));
  }
  return free ? hal::kOk : port_code::kOwned;
}

void UnclaimPin(PinId pin) noexcept {
  const IrqGuard guard{};
  uint16_t& claims = sdk::State().pin_claims[static_cast<std::size_t>(pin.port)];
  claims = static_cast<uint16_t>(claims & static_cast<uint16_t>(~MaskOf(pin)));
}

void HandleExtiIrq(uint8_t first, uint8_t last) noexcept {
  for (uint8_t line = first; (line <= last) && (line < kExtiLines); ++line) {
    if (sdk::ExtiTakePending(line)) {
      const ExtiLine& entry = sdk::State().exti[line];
      // The task changes an entry only with interrupts masked (atomic stores), so the handler
      // always sees a callback together with its own context.
      void* const context = entry.context.load(std::memory_order_acquire);
      const kb::iface::GpioCallback fn = entry.fn.load(std::memory_order_acquire);
      if (fn != nullptr) {
        fn(context);
      }
    }
  }
}

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
    {
      const IrqGuard guard{}; // the toggle reads and writes the output register
      sdk::PinToggle(config_.pin);
    }
    status = error_.Clear();
  }
  return status;
}

kb::Status OutputPin::Release() noexcept {
  if (ready_) {
    sdk::PinReset(config_.pin);
    UnclaimPin(config_.pin);
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
    ExtiLine& line = sdk::State().exti[config_.pin.pin];
    uint32_t code = hal::kOk;
    {
      const IrqGuard guard{};
      if (line.owned && (line.port != config_.pin.port)) {
        code = port_code::kOwned; // the line belongs to the same pin number on another port
      } else {
        line.owned = true;
        line.port = config_.pin.port;
        line.fn.store(nullptr, std::memory_order_release); // no call while the line is reconfigured
        line.context.store(context, std::memory_order_release);
      }
    }
    if (code == hal::kOk) {
      code = sdk::ExtiConfigure(config_.pin, edge, config_.irq_priority);
      const IrqGuard guard{};
      if (code == hal::kOk) {
        line.fn.store(fn, std::memory_order_release);
      } else {
        line.owned = irq_; // keep a line this pin had before
        line.context.store(nullptr, std::memory_order_release);
      }
    }
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
    uint32_t code = hal::kOk;
    if (irq_) {
      ExtiLine& line = sdk::State().exti[config_.pin.pin];
      {
        const IrqGuard guard{};
        line.fn.store(nullptr, std::memory_order_release); // the callback first, then its context
        line.context.store(nullptr, std::memory_order_release);
        line.owned = false;
      }
      code = sdk::ExtiDeconfigure(config_.pin);
    }
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
    UnclaimPin(config_.pin);
    ready_ = false;
  }
  return status;
}

} // namespace kb::port::stm32
