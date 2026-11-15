// Boundary file (deviations/boundary-files.yml): the GPIO and EXTI calls of the STM32 HAL behind
// sdk.hpp. Family differences use the CMSIS family macros (STM32F4, STM32G4, STM32H7, STM32L4) and
// the instance macros of the device header, here and in the other boundary files only. Every
// decision is in the platform-free src/gpio.cpp.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <array>
#include <cstddef>
#include <cstdint>

#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/gpio.hpp"
#include "sdk.hpp"
#if defined(KB_PORT_STM32_G4)
#include "stm32g4xx_hal.h"
#elif defined(KB_PORT_STM32_H7)
#include "stm32h7xx_hal.h"
#elif defined(KB_PORT_STM32_F4)
#include "stm32f4xx_hal.h"
#elif defined(KB_PORT_STM32_L4)
#include "stm32l4xx_hal.h"
#else
#error "define one of KB_PORT_STM32_G4, KB_PORT_STM32_H7, KB_PORT_STM32_F4, KB_PORT_STM32_L4"
#endif
#if defined(STM32H7) && defined(CORE_CM4)
// The second core of a dual-core H7 has its own EXTI pending registers (C2PR1) and shares the
// peripherals with the first: not supported (docs/families.md).
#error "port-stm32 supports the Cortex-M7 of STM32H7 only, not CORE_CM4"
#endif

namespace kb::port::stm32::sdk {

namespace {
constexpr std::size_t kPorts = kGpioPorts;

// --- GPIO ----------------------------------------------------------------------------------------
[[nodiscard]] uint32_t ExtiModeOf(kb::iface::Edge edge) noexcept {
  uint32_t mode = GPIO_MODE_IT_RISING_FALLING;
  if (edge == kb::iface::Edge::Rising) {
    mode = GPIO_MODE_IT_RISING;
  } else if (edge == kb::iface::Edge::Falling) {
    mode = GPIO_MODE_IT_FALLING;
  } else {
    mode = GPIO_MODE_IT_RISING_FALLING;
  }
  return mode;
}

[[nodiscard]] GPIO_TypeDef* PortOf(GpioPort port) noexcept {
  // The ports this device has, by index; nullptr where it has none.
  const std::array<GPIO_TypeDef*, kPorts> ports{
      GPIOA, GPIOB, GPIOC,
#if defined(GPIOD)
      GPIOD,
#else
      nullptr,
#endif
#if defined(GPIOE)
      GPIOE,
#else
      nullptr,
#endif
#if defined(GPIOF)
      GPIOF,
#else
      nullptr,
#endif
#if defined(GPIOG)
      GPIOG,
#else
      nullptr,
#endif
#if defined(GPIOH)
      GPIOH,
#else
      nullptr,
#endif
#if defined(GPIOI)
      GPIOI,
#else
      nullptr,
#endif
#if defined(GPIOJ)
      GPIOJ,
#else
      nullptr,
#endif
#if defined(GPIOK)
      GPIOK,
#else
      nullptr,
#endif
  };
  const auto index = static_cast<std::size_t>(port);
  return (index < ports.size()) ? ports[index] : nullptr;
}

// The port clocks, in two halves.
void PortClockLow(GpioPort port) noexcept {
  if (port == GpioPort::A) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
  } else if (port == GpioPort::B) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
  } else if (port == GpioPort::C) {
    __HAL_RCC_GPIOC_CLK_ENABLE();
#if defined(GPIOD)
  } else if (port == GpioPort::D) {
    __HAL_RCC_GPIOD_CLK_ENABLE();
#endif
#if defined(GPIOE)
  } else if (port == GpioPort::E) {
    __HAL_RCC_GPIOE_CLK_ENABLE();
#endif
#if defined(GPIOF)
  } else if (port == GpioPort::F) {
    __HAL_RCC_GPIOF_CLK_ENABLE();
#endif
  } else {
    // a port of the other half, or one the device does not have
  }
}

void PortClockHigh(GpioPort port) noexcept {
  static_cast<void>(port);
#if defined(GPIOG)
  if (port == GpioPort::G) {
    __HAL_RCC_GPIOG_CLK_ENABLE();
  }
#endif
#if defined(GPIOH)
  if (port == GpioPort::H) {
    __HAL_RCC_GPIOH_CLK_ENABLE();
  }
#endif
#if defined(GPIOI)
  if (port == GpioPort::I) {
    __HAL_RCC_GPIOI_CLK_ENABLE();
  }
#endif
#if defined(GPIOJ)
  if (port == GpioPort::J) {
    __HAL_RCC_GPIOJ_CLK_ENABLE();
  }
#endif
#if defined(GPIOK)
  if (port == GpioPort::K) {
    __HAL_RCC_GPIOK_CLK_ENABLE();
  }
#endif
}

void PortClock(GpioPort port) noexcept {
  PortClockLow(port);
  PortClockHigh(port);
}

[[nodiscard]] uint16_t MaskOf(PinId pin) noexcept {
  return static_cast<uint16_t>(1U << pin.pin);
}

[[nodiscard]] uint32_t PullOf(Pull pull) noexcept {
  uint32_t p = GPIO_NOPULL;
  if (pull == Pull::Up) {
    p = GPIO_PULLUP;
  } else if (pull == Pull::Down) {
    p = GPIO_PULLDOWN;
  } else {
    p = GPIO_NOPULL;
  }
  return p;
}

[[nodiscard]] uint32_t SpeedOf(Speed speed) noexcept {
  uint32_t s = GPIO_SPEED_FREQ_LOW;
  if (speed == Speed::Medium) {
    s = GPIO_SPEED_FREQ_MEDIUM;
  } else if (speed == Speed::High) {
    s = GPIO_SPEED_FREQ_HIGH;
  } else if (speed == Speed::VeryHigh) {
    s = GPIO_SPEED_FREQ_VERY_HIGH;
  } else {
    s = GPIO_SPEED_FREQ_LOW;
  }
  return s;
}

[[nodiscard]] IRQn_Type ExtiIrq(uint8_t line) noexcept {
  IRQn_Type irq = EXTI15_10_IRQn;
  if (line == 0U) {
    irq = EXTI0_IRQn;
  } else if (line == 1U) {
    irq = EXTI1_IRQn;
  } else if (line == 2U) {
    irq = EXTI2_IRQn;
  } else if (line == 3U) {
    irq = EXTI3_IRQn;
  } else if (line == 4U) {
    irq = EXTI4_IRQn;
  } else if (line <= 9U) {
    irq = EXTI9_5_IRQn;
  } else {
    irq = EXTI15_10_IRQn;
  }
  return irq;
}
} // namespace

// --- GPIO ----------------------------------------------------------------------------------------
bool PortExists(GpioPort port) noexcept {
  return PortOf(port) != nullptr;
}

uint32_t PinOutput(PinId pin, bool open_drain, Pull pull, Speed speed, bool high) noexcept {
  PortClock(pin.port);
  HAL_GPIO_WritePin(PortOf(pin.port), MaskOf(pin),
                    high ? GPIO_PIN_SET : GPIO_PIN_RESET); // first: no glitch
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = open_drain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT_PP;
  init.Pull = PullOf(pull);
  init.Speed = SpeedOf(speed);
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

uint32_t PinInput(PinId pin, Pull pull) noexcept {
  PortClock(pin.port);
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = GPIO_MODE_INPUT;
  init.Pull = PullOf(pull);
  init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

uint32_t PinAlternate(PinId pin, bool open_drain, Pull pull) noexcept {
  PortClock(pin.port);
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = open_drain ? GPIO_MODE_AF_OD : GPIO_MODE_AF_PP;
  init.Pull = PullOf(pull);
  init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  init.Alternate = pin.af;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

uint32_t PinAnalog(PinId pin) noexcept {
  PortClock(pin.port);
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = GPIO_MODE_ANALOG;
  init.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

void PinReset(PinId pin) noexcept {
  HAL_GPIO_DeInit(PortOf(pin.port), MaskOf(pin));
}

void PinWrite(PinId pin, bool high) noexcept {
  HAL_GPIO_WritePin(PortOf(pin.port), MaskOf(pin), high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool PinRead(PinId pin) noexcept {
  return HAL_GPIO_ReadPin(PortOf(pin.port), MaskOf(pin)) == GPIO_PIN_SET;
}

void PinToggle(PinId pin) noexcept {
  HAL_GPIO_TogglePin(PortOf(pin.port), MaskOf(pin));
}

namespace {
// The pull setting a pin has now (its PUPDR bits); no pull for a port the device lacks (refused
// earlier).
[[nodiscard]] uint32_t PullBitsOf(PinId pin) noexcept {
  const GPIO_TypeDef* const port = PortOf(pin.port);
  return (port != nullptr) ? ((port->PUPDR >> (2U * pin.pin)) & 3U) : GPIO_NOPULL;
}
} // namespace

uint32_t ExtiConfigure(PinId pin, kb::iface::Edge edge, uint8_t priority) noexcept {
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = ExtiModeOf(edge);
  init.Pull = PullBitsOf(pin);
  init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  HAL_NVIC_SetPriority(ExtiIrq(pin.pin), priority, 0U);
  HAL_NVIC_EnableIRQ(ExtiIrq(pin.pin));
  return hal::kOk;
}

uint32_t ExtiDeconfigure(PinId pin) noexcept {
  const uint32_t pull = PullBitsOf(pin);
  HAL_GPIO_DeInit(PortOf(pin.port), MaskOf(pin)); // clears the EXTI line's configuration
  GPIO_InitTypeDef init{};
  init.Pin = MaskOf(pin);
  init.Mode = GPIO_MODE_INPUT;
  init.Pull = pull;
  init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PortOf(pin.port), &init);
  return hal::kOk;
}

bool ExtiTakePending(uint8_t line) noexcept {
  const uint32_t mask = 1U << line;
  const bool pending = __HAL_GPIO_EXTI_GET_IT(mask) != 0U;
  if (pending) {
    __HAL_GPIO_EXTI_CLEAR_IT(mask);
  }
  return pending;
}
} // namespace kb::port::stm32::sdk
