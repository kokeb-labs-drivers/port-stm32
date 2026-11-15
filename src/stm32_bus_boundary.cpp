// Boundary file (deviations/boundary-files.yml): the I2C, SPI and UART calls of the STM32 HAL
// behind sdk.hpp, the HAL's I2C and SPI completion callbacks, the I2C and SPI interrupt entry
// points, and the UART interrupt's register reads. Each function converts its arguments, calls the
// HAL and returns VendorCode(HAL status, the handle's ErrorCode); the handles live in the tables
// below. Every decision is in the platform-free files. SPDX-License-Identifier: MIT Copyright (c)
// 2026 The port-stm32 authors
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/i2c.hpp"
#include "kb/port/stm32/irq.hpp"
#include "kb/port/stm32/spi.hpp"
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
constexpr std::size_t kI2cs = 5U;
constexpr std::size_t kSpis = 7U;
constexpr std::size_t kUarts = 12U; // slots 1 to 10 (1 to 8 served), slot 11 for LPUART1
constexpr std::size_t kLpuartSlot = 11U;

[[nodiscard]] uint32_t Code(HAL_StatusTypeDef status, uint32_t error_code) noexcept {
  return VendorCode(static_cast<uint32_t>(status), error_code);
}

// The vendor code of an interrupt API's start: its ErrorCode only when the start itself failed (the
// interrupt may already have ended the transfer, and set ErrorCode, before the call returns).
[[nodiscard]] uint32_t Started(HAL_StatusTypeDef status, uint32_t error_code) noexcept {
  return (status == HAL_OK) ? VendorCode(0U, 0U) : Code(status, error_code);
}

[[nodiscard]] uint16_t Size16(std::size_t n) noexcept {
  return static_cast<uint16_t>(n);
}

// --- I2C -----------------------------------------------------------------------------------------
struct I2cSlot {
  I2C_HandleTypeDef handle{};
  Completion* done{nullptr}; ///< The owner's completion while it is open.
  uint8_t probe{0U};         ///< The (unsent) buffer of an address-only probe.
};

std::array<I2cSlot, kI2cs>& I2cs() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<I2cSlot, kI2cs> slots{};
  return slots;
}

[[nodiscard]] I2cSlot* I2cSlotOf(const I2C_HandleTypeDef* h) noexcept {
  I2cSlot* found = nullptr;
  for (I2cSlot& slot : I2cs()) {
    if (&slot.handle == h) {
      found = &slot;
    }
  }
  return found;
}

// The event and error interrupt numbers of an instance.
void I2cIrqs(uint8_t instance, IRQn_Type& event, IRQn_Type& error) noexcept {
  event = I2C1_EV_IRQn;
  error = I2C1_ER_IRQn;
#if defined(I2C2)
  if (instance == 2U) {
    event = I2C2_EV_IRQn;
    error = I2C2_ER_IRQn;
  }
#endif
#if defined(I2C3)
  if (instance == 3U) {
    event = I2C3_EV_IRQn;
    error = I2C3_ER_IRQn;
  }
#endif
#if defined(I2C4)
  if (instance == 4U) {
    event = I2C4_EV_IRQn;
    error = I2C4_ER_IRQn;
  }
#endif
  static_cast<void>(instance);
}

void I2cIrqsOn(uint8_t instance, uint8_t priority, bool on) noexcept {
  IRQn_Type event = I2C1_EV_IRQn;
  IRQn_Type error = I2C1_ER_IRQn;
  I2cIrqs(instance, event, error);
  if (on) {
    HAL_NVIC_SetPriority(event, priority, 0U);
    HAL_NVIC_SetPriority(error, priority, 0U);
    HAL_NVIC_EnableIRQ(event);
    HAL_NVIC_EnableIRQ(error);
  } else {
    HAL_NVIC_DisableIRQ(event);
    HAL_NVIC_DisableIRQ(error);
  }
}

[[nodiscard]] I2C_TypeDef* I2cOf(uint8_t instance) noexcept {
  I2C_TypeDef* i = nullptr;
  if (instance == 1U) {
    i = I2C1;
#if defined(I2C2)
  } else if (instance == 2U) {
    i = I2C2;
#endif
#if defined(I2C3)
  } else if (instance == 3U) {
    i = I2C3;
#endif
#if defined(I2C4)
  } else if (instance == 4U) {
    i = I2C4;
#endif
  } else {
    i = nullptr;
  }
  return i;
}

void I2cClock(uint8_t instance) noexcept {
  if (instance == 1U) {
    __HAL_RCC_I2C1_CLK_ENABLE();
#if defined(I2C2)
  } else if (instance == 2U) {
    __HAL_RCC_I2C2_CLK_ENABLE();
#endif
#if defined(I2C3)
  } else if (instance == 3U) {
    __HAL_RCC_I2C3_CLK_ENABLE();
#endif
#if defined(I2C4)
  } else if (instance == 4U) {
    __HAL_RCC_I2C4_CLK_ENABLE();
#endif
  } else {
    // no other instance
  }
}

[[nodiscard]] uint16_t Address8(uint8_t address) noexcept {
  return static_cast<uint16_t>(static_cast<uint16_t>(address) << 1U);
}

// --- SPI -----------------------------------------------------------------------------------------
struct SpiSlot {
  SPI_HandleTypeDef handle{};
  Completion* done{nullptr}; ///< The bus's completion while it is open.
};

std::array<SpiSlot, kSpis>& Spis() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<SpiSlot, kSpis> slots{};
  return slots;
}

[[nodiscard]] SpiSlot* SpiSlotOf(const SPI_HandleTypeDef* h) noexcept {
  SpiSlot* found = nullptr;
  for (SpiSlot& slot : Spis()) {
    if (&slot.handle == h) {
      found = &slot;
    }
  }
  return found;
}

[[nodiscard]] IRQn_Type SpiIrq(uint8_t instance) noexcept {
  IRQn_Type irq = SPI1_IRQn;
#if defined(SPI2)
  if (instance == 2U) {
    irq = SPI2_IRQn;
  }
#endif
#if defined(SPI3)
  if (instance == 3U) {
    irq = SPI3_IRQn;
  }
#endif
#if defined(SPI4)
  if (instance == 4U) {
    irq = SPI4_IRQn;
  }
#endif
#if defined(SPI5)
  if (instance == 5U) {
    irq = SPI5_IRQn;
  }
#endif
#if defined(SPI6)
  if (instance == 6U) {
    irq = SPI6_IRQn;
  }
#endif
  static_cast<void>(instance);
  return irq;
}

[[nodiscard]] SPI_TypeDef* SpiOf(uint8_t instance) noexcept {
  SPI_TypeDef* s = nullptr;
  if (instance == 1U) {
    s = SPI1;
#if defined(SPI2)
  } else if (instance == 2U) {
    s = SPI2;
#endif
#if defined(SPI3)
  } else if (instance == 3U) {
    s = SPI3;
#endif
#if defined(SPI4)
  } else if (instance == 4U) {
    s = SPI4;
#endif
#if defined(SPI5)
  } else if (instance == 5U) {
    s = SPI5;
#endif
#if defined(SPI6)
  } else if (instance == 6U) {
    s = SPI6;
#endif
  } else {
    s = nullptr;
  }
  return s;
}

void SpiClock(uint8_t instance) noexcept {
  if (instance == 1U) {
    __HAL_RCC_SPI1_CLK_ENABLE();
#if defined(SPI2)
  } else if (instance == 2U) {
    __HAL_RCC_SPI2_CLK_ENABLE();
#endif
#if defined(SPI3)
  } else if (instance == 3U) {
    __HAL_RCC_SPI3_CLK_ENABLE();
#endif
#if defined(SPI4)
  } else if (instance == 4U) {
    __HAL_RCC_SPI4_CLK_ENABLE();
#endif
#if defined(SPI5)
  } else if (instance == 5U) {
    __HAL_RCC_SPI5_CLK_ENABLE();
#endif
#if defined(SPI6)
  } else if (instance == 6U) {
    __HAL_RCC_SPI6_CLK_ENABLE();
#endif
  } else {
    // no other instance
  }
}

[[nodiscard]] uint32_t PrescalerOf(uint8_t exponent) noexcept {
  constexpr std::array<uint32_t, 8> kPrescalers{
      SPI_BAUDRATEPRESCALER_2,   SPI_BAUDRATEPRESCALER_4,  SPI_BAUDRATEPRESCALER_8,
      SPI_BAUDRATEPRESCALER_16,  SPI_BAUDRATEPRESCALER_32, SPI_BAUDRATEPRESCALER_64,
      SPI_BAUDRATEPRESCALER_128, SPI_BAUDRATEPRESCALER_256};
  return kPrescalers[exponent];
}

void SpiInitFields(SPI_HandleTypeDef& h, uint8_t mode, uint8_t exponent) noexcept {
  h.Init.Mode = SPI_MODE_MASTER;
  h.Init.Direction = SPI_DIRECTION_2LINES;
  h.Init.DataSize = SPI_DATASIZE_8BIT;
  h.Init.CLKPolarity = ((mode & 2U) != 0U) ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
  h.Init.CLKPhase = ((mode & 1U) != 0U) ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
  h.Init.NSS = SPI_NSS_SOFT;
  h.Init.BaudRatePrescaler = PrescalerOf(exponent);
  h.Init.FirstBit = SPI_FIRSTBIT_MSB;
  h.Init.TIMode = SPI_TIMODE_DISABLE;
  h.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  h.Init.CRCPolynomial = 7U;
#if defined(STM32H7)
  h.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  h.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  h.Init.MasterKeepIOState =
      SPI_MASTER_KEEP_IO_STATE_ENABLE; // SCK keeps its idle level between frames
#elif !defined(STM32F4)
  h.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  h.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
#endif
}

// --- UART ----------------------------------------------------------------------------------------
struct UartSlot {
  UART_HandleTypeDef handle{};
  Uart* owner{nullptr};
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

// --- I2C -----------------------------------------------------------------------------------------
bool I2cExists(uint8_t instance) noexcept {
  return (instance < kI2cs) && (I2cOf(instance) != nullptr);
}

bool I2cHasTimingRegister() noexcept {
#if defined(STM32F4)
  return false;
#else
  return true;
#endif
}

uint32_t I2cKernelClockHz(uint8_t instance) noexcept {
#if defined(STM32H7) && defined(I2C4)
  // I2C4 is in D3: its kernel clock is selected in D3CCIPR (rcc_pclk4 at reset), not PCLK1.
  return (instance == 4U) ? HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_I2C4) : HAL_RCC_GetPCLK1Freq();
#else
  static_cast<void>(instance); // every other instance runs from PCLK1 at reset
  return HAL_RCC_GetPCLK1Freq();
#endif
}

namespace {
// One HAL_I2C_Init of a handle the caller owns.
[[nodiscard]] uint32_t I2cInitHandle(I2C_HandleTypeDef& h, const I2cConfig& config,
                                     uint32_t timing) noexcept {
  const Pull pull = config.internal_pullups ? Pull::Up : Pull::None;
  static_cast<void>(PinAlternate(config.scl, true, pull));
  static_cast<void>(PinAlternate(config.sda, true, pull));
  I2cClock(config.instance);
  h.Instance = I2cOf(config.instance);
#if defined(STM32F4)
  static_cast<void>(timing);
  h.Init.ClockSpeed = config.clock_hz;
  h.Init.DutyCycle = I2C_DUTYCYCLE_2;
#else
  h.Init.Timing = timing;
  h.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
#endif
  h.Init.OwnAddress1 = 0U;
  h.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  h.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  h.Init.OwnAddress2 = 0U;
  h.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  h.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  HAL_StatusTypeDef s = HAL_I2C_Init(&h);
#if !defined(STM32F4)
  if (s == HAL_OK) {
    s = HAL_I2CEx_ConfigAnalogFilter(&h, I2C_ANALOGFILTER_ENABLE);
  }
#endif
  if (s != HAL_OK) {
    h.Instance = nullptr; // not opened: free for the next EnsureReady()
  }
  return Code(s, h.ErrorCode);
}

// The interrupt priority each open I2C instance was given.
std::array<uint8_t, kI2cs>& I2cPriorities() noexcept {
  // codeql::misra_deviation_next_line(DEV-001)
  static std::array<uint8_t, kI2cs> priorities{};
  return priorities;
}
} // namespace

uint32_t I2cOpen(const I2cConfig& config, uint32_t timing, Completion& done) noexcept {
  I2cSlot& slot = I2cs()[config.instance];
  // The handle's Instance is the owner mark: set while one I2cController has the peripheral open.
  uint32_t code = port_code::kOwned;
  if (slot.handle.Instance == nullptr) {
    code = I2cInitHandle(slot.handle, config, timing);
    if (code == hal::kOk) {
      slot.done = &done;
      I2cPriorities()[config.instance] = config.irq_priority;
      I2cIrqsOn(config.instance, config.irq_priority, true);
    }
  }
  return code;
}

uint32_t I2cClose(uint8_t instance) noexcept {
  I2cSlot& slot = I2cs()[instance];
  I2cIrqsOn(instance, 0U, false);
  slot.done = nullptr;
  const HAL_StatusTypeDef s = HAL_I2C_DeInit(&slot.handle);
  const uint32_t code = Code(s, slot.handle.ErrorCode);
  slot.handle.Instance = nullptr;
  return code;
}

void I2cPrepare(uint8_t instance) noexcept {
#if defined(STM32F4)
  static_cast<void>(instance); // I2C v1 has no TXDR/RXDR or CR2 transfer fields
#else
  I2C_TypeDef* const r = I2cs()[instance].handle.Instance;
  if ((r->ISR & I2C_ISR_TXIS) != 0U) {
    r->TXDR = 0x00U; // a latched TXIS: a dummy byte clears it, the flush below drops it
  }
  if ((r->ISR & I2C_ISR_TXE) == 0U) {
    r->ISR |= I2C_ISR_TXE; // flush the transmit data register
  }
  if ((r->ISR & I2C_ISR_RXNE) != 0U) {
    static_cast<void>(r->RXDR);
  }
  constexpr uint32_t kTransferFields = I2C_CR2_NBYTES | I2C_CR2_RELOAD | I2C_CR2_AUTOEND |
                                       I2C_CR2_START | I2C_CR2_STOP | I2C_CR2_RD_WRN;
  r->CR2 &= ~kTransferFields;
#endif
}

void I2cRestore(uint8_t instance) noexcept {
  I2C_HandleTypeDef& h = I2cs()[instance].handle;
#if defined(STM32F4)
  __HAL_I2C_DISABLE_IT(&h, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR);
  __HAL_I2C_CLEAR_FLAG(&h, I2C_FLAG_AF | I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_OVR);
#else
  __HAL_I2C_DISABLE_IT(&h, I2C_IT_ERRI | I2C_IT_TCI | I2C_IT_STOPI | I2C_IT_NACKI | I2C_IT_ADDRI |
                               I2C_IT_RXI | I2C_IT_TXI);
  __HAL_I2C_CLEAR_FLAG(&h,
                       I2C_FLAG_AF | I2C_FLAG_STOPF | I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_OVR);
#endif
  I2cPrepare(instance);
  h.State = HAL_I2C_STATE_READY;
  h.Mode = HAL_I2C_MODE_NONE;
  h.PreviousState = 0U;
  __HAL_UNLOCK(&h);
}

bool I2cBusy(uint8_t instance) noexcept {
  return __HAL_I2C_GET_FLAG(&I2cs()[instance].handle, I2C_FLAG_BUSY) != RESET;
}

uint32_t I2cReset(uint8_t instance) noexcept {
  I2C_HandleTypeDef& h = I2cs()[instance].handle;
  I2cIrqsOn(instance, 0U, false);
  HAL_StatusTypeDef s = HAL_I2C_DeInit(&h); // PE cleared (SWRST on F4 at the next init)
  if (s == HAL_OK) {
    s = HAL_I2C_Init(&h); // the Init fields and Instance are kept by DeInit
  }
#if !defined(STM32F4)
  if (s == HAL_OK) {
    s = HAL_I2CEx_ConfigAnalogFilter(&h, I2C_ANALOGFILTER_ENABLE);
  }
#endif
  I2cIrqsOn(instance, I2cPriorities()[instance], true);
  return Code(s, h.ErrorCode);
}

uint32_t I2cStartTransmit(uint8_t instance, uint8_t address, std::span<const uint8_t> tx,
                          bool hold) noexcept {
  I2cSlot& slot = I2cs()[instance];
  HAL_StatusTypeDef s = HAL_OK;
  if (tx.empty()) {
    s = HAL_I2C_Master_Transmit_IT(&slot.handle, Address8(address), &slot.probe, 0U);
  } else {
    // The HAL's transmit takes a non-const pointer but only reads through it.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): DEV-002, the HAL lacks the const
    auto* data = const_cast<uint8_t*>(tx.data());
    s = HAL_I2C_Master_Seq_Transmit_IT(&slot.handle, Address8(address), data, Size16(tx.size()),
                                       hold ? I2C_FIRST_FRAME : I2C_FIRST_AND_LAST_FRAME);
  }
  return Started(s, slot.handle.ErrorCode);
}

uint32_t I2cStartReceive(uint8_t instance, uint8_t address, std::span<uint8_t> rx,
                         bool after_write) noexcept {
  I2cSlot& slot = I2cs()[instance];
  const HAL_StatusTypeDef s =
      HAL_I2C_Master_Seq_Receive_IT(&slot.handle, Address8(address), rx.data(), Size16(rx.size()),
                                    after_write ? I2C_LAST_FRAME : I2C_FIRST_AND_LAST_FRAME);
  return Started(s, slot.handle.ErrorCode);
}

// --- SPI -----------------------------------------------------------------------------------------
bool SpiExists(uint8_t instance) noexcept {
  return (instance < kSpis) && (SpiOf(instance) != nullptr);
}

uint32_t SpiKernelClockHz(uint8_t instance) noexcept {
#if defined(STM32H7)
  uint32_t hz = 0U;
  if (instance <= 3U) {
    hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI123);
  } else if (instance <= 5U) {
    hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI45);
  } else {
    hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI6);
  }
  return hz;
#else
  // SPI2 and SPI3 are on APB1, the others on APB2.
  return ((instance == 2U) || (instance == 3U)) ? HAL_RCC_GetPCLK1Freq() : HAL_RCC_GetPCLK2Freq();
#endif
}

uint32_t SpiOpen(const SpiBusConfig& config, Completion& done) noexcept {
  SpiSlot& slot = Spis()[config.instance];
  SPI_HandleTypeDef& h = slot.handle;
  uint32_t code = port_code::kOwned; // the handle's Instance is the owner mark, as for I2C
  if (h.Instance == nullptr) {
    static_cast<void>(PinAlternate(config.sck, false, Pull::None));
    if (config.mosi.Used()) {
      static_cast<void>(PinAlternate(config.mosi, false, Pull::None));
    }
    if (config.miso.Used()) {
      static_cast<void>(PinAlternate(config.miso, false, Pull::None));
    }
    SpiClock(config.instance);
    h.Instance = SpiOf(config.instance);
    SpiInitFields(h, 0U, 7U);
    const HAL_StatusTypeDef s = HAL_SPI_Init(&h);
    code = Code(s, h.ErrorCode);
    if (s != HAL_OK) {
      h.Instance = nullptr;
    } else {
      slot.done = &done;
      HAL_NVIC_SetPriority(SpiIrq(config.instance), config.irq_priority, 0U);
      HAL_NVIC_EnableIRQ(SpiIrq(config.instance));
    }
  }
  return code;
}

uint32_t SpiConfigure(uint8_t instance, uint8_t mode, uint8_t exponent) noexcept {
  SPI_HandleTypeDef& h = Spis()[instance].handle;
  HAL_StatusTypeDef s = HAL_SPI_DeInit(&h);
  if (s == HAL_OK) {
    SpiInitFields(h, mode, exponent);
    s = HAL_SPI_Init(&h); // leaves the peripheral disabled until the next transfer
  }
  return Code(s, h.ErrorCode);
}

uint32_t SpiClose(uint8_t instance) noexcept {
  SpiSlot& slot = Spis()[instance];
  HAL_NVIC_DisableIRQ(SpiIrq(instance));
  slot.done = nullptr;
  const HAL_StatusTypeDef s = HAL_SPI_DeInit(&slot.handle);
  const uint32_t code = Code(s, slot.handle.ErrorCode);
  slot.handle.Instance = nullptr;
  return code;
}

uint32_t SpiStart(uint8_t instance, std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept {
  SPI_HandleTypeDef& h = Spis()[instance].handle;
  HAL_StatusTypeDef s = HAL_OK;
  if (rx.empty()) {
    s = HAL_SPI_Transmit_IT(&h, tx.data(), Size16(tx.size()));
  } else {
    // A read-only frame clocks the zeroed receive buffer out in place: full duplex on every family
    // (the H7 receive-only call would switch the peripheral to simplex receive).
    const uint8_t* const out = tx.empty() ? rx.data() : tx.data();
    s = HAL_SPI_TransmitReceive_IT(&h, out, rx.data(), Size16(rx.size()));
  }
  return Started(s, h.ErrorCode);
}

uint32_t SpiAbort(uint8_t instance) noexcept {
  SPI_HandleTypeDef& h = Spis()[instance].handle;
  const HAL_StatusTypeDef s = HAL_SPI_Abort(&h);
  if (s != HAL_OK) {
    h.ErrorCode |= HAL_SPI_ERROR_ABORT;
  }
  return Code(s, h.ErrorCode);
}

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
  if (slot.owner == nullptr) {
    static_cast<void>(PinAlternate(config.tx, false, Pull::None));
    static_cast<void>(PinAlternate(config.rx, false, Pull::Up));
    UartClock(config.instance);
    slot.handle.Instance = UartOf(config.instance);
    UartInitFields(slot.handle, config);
    const HAL_StatusTypeDef s = HAL_UART_Init(&slot.handle); // the FIFO stays off (HAL default)
    code = Code(s, slot.handle.ErrorCode);
    if (s == HAL_OK) {
      slot.owner = &owner;
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
  slot.owner = nullptr;
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
    if ((slot.owner != nullptr) && (u != nullptr)) {
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
      slot.owner->OnInterrupt(event);
    }
  }
}

void HandleI2cIrq(uint8_t instance, bool error) noexcept {
  if (instance < sdk::kI2cs) {
    sdk::I2cSlot& slot = sdk::I2cs()[instance];
    if (slot.done != nullptr) {
      if (error) {
        HAL_I2C_ER_IRQHandler(&slot.handle);
      } else {
        HAL_I2C_EV_IRQHandler(&slot.handle);
      }
    }
  }
}

void HandleSpiIrq(uint8_t instance) noexcept {
  if (instance < sdk::kSpis) {
    sdk::SpiSlot& slot = sdk::Spis()[instance];
    if (slot.done != nullptr) {
      HAL_SPI_IRQHandler(&slot.handle);
    }
  }
}

} // namespace kb::port::stm32

// The HAL's I2C and SPI callbacks (weak in the HAL, defined here): the end of the transfer the port
// started, into the owner's Completion. C linkage and global names by definition.
namespace {
void I2cDone(const I2C_HandleTypeDef* h, uint32_t code) noexcept {
  kb::port::stm32::sdk::I2cSlot* const slot = kb::port::stm32::sdk::I2cSlotOf(h);
  if ((slot != nullptr) && (slot->done != nullptr)) {
    slot->done->Complete(code);
  }
}
void SpiDone(const SPI_HandleTypeDef* h, uint32_t code) noexcept {
  kb::port::stm32::sdk::SpiSlot* const slot = kb::port::stm32::sdk::SpiSlotOf(h);
  if ((slot != nullptr) && (slot->done != nullptr)) {
    slot->done->Complete(code);
  }
}
[[nodiscard]] uint32_t Failed(uint32_t error_code) noexcept {
  return kb::port::stm32::VendorCode(static_cast<uint32_t>(HAL_ERROR), error_code);
}
} // namespace

extern "C" {
// NOLINTBEGIN(readability-identifier-naming): the HAL fixes these names
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef* hi2c) {
  I2cDone(hi2c, kb::port::stm32::hal::kOk);
}
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef* hi2c) {
  I2cDone(hi2c, kb::port::stm32::hal::kOk);
}
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* hi2c) {
  I2cDone(hi2c, Failed(hi2c->ErrorCode));
}
void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef* hi2c) {
  I2cDone(hi2c, Failed(hi2c->ErrorCode));
}
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef* hspi) {
  SpiDone(hspi, kb::port::stm32::hal::kOk);
}
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef* hspi) {
  SpiDone(hspi, kb::port::stm32::hal::kOk);
}
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef* hspi) {
  SpiDone(hspi, Failed(hspi->ErrorCode));
}
// NOLINTEND(readability-identifier-naming)
} // extern "C"
