// Boundary file (deviations/boundary-files.yml): the UART calls of the STM32 HAL behind sdk.hpp and
// the UART interrupt entry point with its register reads. Each function converts its arguments,
// calls the HAL and returns VendorCode(HAL status, the handle's ErrorCode); the handles live in the
// table below. Every decision is in the platform-free src/uart.cpp.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <array>
// Slot pointers shared with the interrupts are std::atomic (release before the NVIC enable).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/gpio.hpp"
#include "kb/port/stm32/irq.hpp"
#include "kb/port/stm32/uart.hpp"
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

namespace kb::port::stm32::sdk {

namespace {
constexpr std::size_t kUarts = 12U; // slots 1 to 10 (1 to 8 served), slot 11 for LPUART1
constexpr std::size_t kLpuartSlot = 11U;

[[nodiscard]] uint32_t Code(HAL_StatusTypeDef status, uint32_t error_code) noexcept {
  return VendorCode(static_cast<uint32_t>(status), error_code);
}

// --- UART ----------------------------------------------------------------------------------------
struct UartSlot {
  UART_HandleTypeDef handle{};
  std::atomic<Uart*> owner{nullptr};
};

std::array<UartSlot, kUarts>& Uarts() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<UartSlot, kUarts> slots{};
  return slots;
}

[[nodiscard]] std::size_t UartSlotOf(uint8_t instance) noexcept {
  return (instance == kLpuart1) ? kLpuartSlot : static_cast<std::size_t>(instance);
}

[[nodiscard]] USART_TypeDef* UartOf(uint8_t instance) noexcept {
  USART_TypeDef* u = nullptr;
  switch (instance) {
#if defined(USART1)
  case 1U:
    u = USART1;
    break;
#endif
#if defined(USART2)
  case 2U:
    u = USART2;
    break;
#endif
#if defined(USART3)
  case 3U:
    u = USART3;
    break;
#endif
#if defined(UART4)
  case 4U:
    u = UART4;
    break;
#endif
#if defined(UART5)
  case 5U:
    u = UART5;
    break;
#endif
#if defined(USART6)
  case 6U:
    u = USART6;
    break;
#endif
#if defined(UART7)
  case 7U:
    u = UART7;
    break;
#endif
#if defined(UART8)
  case 8U:
    u = UART8;
    break;
#endif
#if defined(LPUART1)
  case kLpuart1:
    u = LPUART1;
    break;
#endif
  default:
    u = nullptr;
    break;
  }
  return u;
}

[[nodiscard]] IRQn_Type UartIrq(uint8_t instance) noexcept {
  IRQn_Type irq = USART1_IRQn;
  switch (instance) {
#if defined(USART2)
  case 2U:
    irq = USART2_IRQn;
    break;
#endif
#if defined(USART3)
  case 3U:
    irq = USART3_IRQn;
    break;
#endif
#if defined(UART4)
  case 4U:
    irq = UART4_IRQn;
    break;
#endif
#if defined(UART5)
  case 5U:
    irq = UART5_IRQn;
    break;
#endif
#if defined(USART6)
  case 6U:
    irq = USART6_IRQn;
    break;
#endif
#if defined(UART7)
  case 7U:
    irq = UART7_IRQn;
    break;
#endif
#if defined(UART8)
  case 8U:
    irq = UART8_IRQn;
    break;
#endif
#if defined(LPUART1)
  case kLpuart1:
    irq = LPUART1_IRQn;
    break;
#endif
  default:
    irq = USART1_IRQn;
    break;
  }
  return irq;
}

void UartClock(uint8_t instance) noexcept {
  switch (instance) {
#if defined(USART1)
  case 1U:
    __HAL_RCC_USART1_CLK_ENABLE();
    break;
#endif
#if defined(USART2)
  case 2U:
    __HAL_RCC_USART2_CLK_ENABLE();
    break;
#endif
#if defined(USART3)
  case 3U:
    __HAL_RCC_USART3_CLK_ENABLE();
    break;
#endif
#if defined(UART4)
  case 4U:
    __HAL_RCC_UART4_CLK_ENABLE();
    break;
#endif
#if defined(UART5)
  case 5U:
    __HAL_RCC_UART5_CLK_ENABLE();
    break;
#endif
#if defined(USART6)
  case 6U:
    __HAL_RCC_USART6_CLK_ENABLE();
    break;
#endif
#if defined(UART7)
  case 7U:
    __HAL_RCC_UART7_CLK_ENABLE();
    break;
#endif
#if defined(UART8)
  case 8U:
    __HAL_RCC_UART8_CLK_ENABLE();
    break;
#endif
#if defined(LPUART1)
  case kLpuart1:
    __HAL_RCC_LPUART1_CLK_ENABLE();
    break;
#endif
  default:
    break;
  }
}

// The HAL's word length counts the parity bit.
[[nodiscard]] uint32_t WordLengthOf(const UartConfig& config) noexcept {
  const uint32_t bits =
      uint32_t{config.data_bits} + ((config.parity == UartParity::None) ? 0U : 1U);
  uint32_t w = UART_WORDLENGTH_8B;
  if (bits >= 9U) {
    w = UART_WORDLENGTH_9B;
#if defined(UART_WORDLENGTH_7B)
  } else if (bits <= 7U) {
    w = UART_WORDLENGTH_7B;
#endif
  } else {
    w = UART_WORDLENGTH_8B;
  }
  return w;
}

[[nodiscard]] uint32_t ParityOf(UartParity parity) noexcept {
  uint32_t p = UART_PARITY_NONE;
  if (parity == UartParity::Even) {
    p = UART_PARITY_EVEN;
  } else if (parity == UartParity::Odd) {
    p = UART_PARITY_ODD;
  } else {
    p = UART_PARITY_NONE;
  }
  return p;
}

void UartInitFields(UART_HandleTypeDef& h, const UartConfig& config) noexcept {
  h.Init.BaudRate = config.baud;
  h.Init.WordLength = WordLengthOf(config);
  h.Init.StopBits = (config.stop_bits == UartStopBits::Two) ? UART_STOPBITS_2 : UART_STOPBITS_1;
  h.Init.Parity = ParityOf(config.parity);
  h.Init.Mode = UART_MODE_TX_RX;
  h.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  h.Init.OverSampling = UART_OVERSAMPLING_16;
#if !defined(STM32F4)
  h.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
#if defined(USART_PRESC_PRESCALER)
  h.Init.ClockPrescaler = UART_PRESCALER_DIV1;
#endif
  h.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
#endif
}

void UartRxOn(UART_HandleTypeDef& h) noexcept {
  __HAL_UART_ENABLE_IT(&h, UART_IT_RXNE);
  __HAL_UART_ENABLE_IT(&h, UART_IT_PE);
  __HAL_UART_ENABLE_IT(&h, UART_IT_ERR);
}

[[nodiscard]] bool UartItOn(UART_HandleTypeDef& h, uint32_t it) noexcept {
  return __HAL_UART_GET_IT_SOURCE(&h, it) != 0U;
}

// The receive errors of a status word as uart_flags.
[[nodiscard]] uint32_t ErrorFlags(uint32_t status) noexcept {
#if defined(STM32F4)
  constexpr uint32_t kOre = USART_SR_ORE;
  constexpr uint32_t kFe = USART_SR_FE;
  constexpr uint32_t kPe = USART_SR_PE;
  constexpr uint32_t kNe = USART_SR_NE;
#else
  constexpr uint32_t kOre = USART_ISR_ORE;
  constexpr uint32_t kFe = USART_ISR_FE;
  constexpr uint32_t kPe = USART_ISR_PE;
  constexpr uint32_t kNe = USART_ISR_NE;
#endif
  uint32_t flags = 0U;
  flags |= ((status & kOre) != 0U) ? uart_flags::kOverrun : 0U;
  flags |= ((status & kFe) != 0U) ? uart_flags::kFrameError : 0U;
  flags |= ((status & kPe) != 0U) ? uart_flags::kParityError : 0U;
  flags |= ((status & kNe) != 0U) ? uart_flags::kNoise : 0U;
  return flags;
}
} // namespace

// --- UART ----------------------------------------------------------------------------------------
bool UartExists(uint8_t instance) noexcept {
  return (UartSlotOf(instance) < kUarts) && (UartOf(instance) != nullptr);
}

bool UartHasSevenBitWords() noexcept {
#if defined(UART_WORDLENGTH_7B)
  return true;
#else
  return false;
#endif
}

uint32_t UartOpen(const UartConfig& config, Uart& owner) noexcept {
  UartSlot& slot = Uarts()[UartSlotOf(config.instance)];
  uint32_t code = port_code::kOwned;
  if (slot.owner.load(std::memory_order_acquire) == nullptr) {
    static_cast<void>(PinAlternate(config.tx, false, Pull::None));
    static_cast<void>(PinAlternate(config.rx, false, Pull::Up));
    UartClock(config.instance);
    slot.handle.Instance = UartOf(config.instance);
    UartInitFields(slot.handle, config);
    const HAL_StatusTypeDef s = HAL_UART_Init(&slot.handle); // the FIFO stays off (HAL default)
    code = Code(s, slot.handle.ErrorCode);
    if (s == HAL_OK) {
      slot.owner.store(&owner, std::memory_order_release);
      UartRxOn(slot.handle);
      HAL_NVIC_SetPriority(UartIrq(config.instance), config.irq_priority, 0U);
      HAL_NVIC_EnableIRQ(UartIrq(config.instance));
    }
  }
  return code;
}

uint32_t UartClose(uint8_t instance) noexcept {
  UartSlot& slot = Uarts()[UartSlotOf(instance)];
  HAL_NVIC_DisableIRQ(UartIrq(instance));
  slot.owner.store(nullptr, std::memory_order_release);
  const HAL_StatusTypeDef s = HAL_UART_DeInit(&slot.handle);
  return Code(s, slot.handle.ErrorCode);
}

uint32_t UartSetBaud(uint8_t instance, uint32_t baud) noexcept {
  UART_HandleTypeDef& h = Uarts()[UartSlotOf(instance)].handle;
  h.Init.BaudRate = baud;
  const HAL_StatusTypeDef s = HAL_UART_Init(&h); // disables the peripheral, sets BRR, enables it
  if (s == HAL_OK) {
    UartRxOn(h);
  }
  return Code(s, h.ErrorCode);
}

bool UartTxComplete(uint8_t instance) noexcept {
  UART_HandleTypeDef& h = Uarts()[UartSlotOf(instance)].handle;
  return __HAL_UART_GET_FLAG(&h, UART_FLAG_TC) != 0U;
}

void UartWrite(uint8_t instance, uint8_t byte) noexcept {
  USART_TypeDef* const u = Uarts()[UartSlotOf(instance)].handle.Instance;
#if defined(STM32F4)
  u->DR = byte;
#else
  u->TDR = byte;
#endif
}

void UartTxInterrupts(uint8_t instance, bool empty, bool complete) noexcept {
  UART_HandleTypeDef& h = Uarts()[UartSlotOf(instance)].handle;
  if (empty) {
    __HAL_UART_ENABLE_IT(&h, UART_IT_TXE);
  } else {
    __HAL_UART_DISABLE_IT(&h, UART_IT_TXE);
  }
  if (complete) {
    __HAL_UART_ENABLE_IT(&h, UART_IT_TC);
  } else {
    __HAL_UART_DISABLE_IT(&h, UART_IT_TC);
  }
}

} // namespace kb::port::stm32::sdk

namespace kb::port::stm32 {

void HandleUartIrq(uint8_t instance) noexcept {
  const std::size_t index = sdk::UartSlotOf(instance);
  if (index < sdk::kUarts) {
    sdk::UartSlot& slot = sdk::Uarts()[index];
    USART_TypeDef* const u = slot.handle.Instance;
    Uart* const owner = slot.owner.load(std::memory_order_acquire);
    if (owner == nullptr) {
      // Not the port's: its interrupt would re-enter forever with nothing to clear it.
      HAL_NVIC_DisableIRQ(sdk::UartIrq(instance));
    } else if (u != nullptr) {
      UartEvent event{};
#if defined(STM32F4)
      // F4: reading SR then DR clears the error flags, so DR is read whenever either is set.
      const uint32_t status = u->SR;
      event.errors = sdk::ErrorFlags(status);
      event.received = (status & USART_SR_RXNE) != 0U;
      if (event.received || (event.errors != 0U)) {
        event.data = static_cast<uint16_t>(u->DR & 0x1FFU);
      }
#else
      const uint32_t status = u->ISR;
      event.errors = sdk::ErrorFlags(status);
      u->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_PECF | USART_ICR_NECF; // always all
      event.received = (status & UART_FLAG_RXNE) != 0U;
      event.data = event.received ? static_cast<uint16_t>(u->RDR & 0x1FFU) : uint16_t{0U};
#endif
      event.tx_empty = ((status & UART_FLAG_TXE) != 0U) && sdk::UartItOn(slot.handle, UART_IT_TXE);
      event.tx_complete = ((status & UART_FLAG_TC) != 0U) && sdk::UartItOn(slot.handle, UART_IT_TC);
      owner->OnInterrupt(event);
    } else {
      // an owner without a handle does not exist (UartOpen sets the handle first)
    }
  }
}

} // namespace kb::port::stm32
