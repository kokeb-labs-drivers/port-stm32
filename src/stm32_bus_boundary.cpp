// Boundary file (deviations/boundary-files.yml): the I2C, SPI and UART calls of the STM32 HAL
// behind sdk.hpp, and the UART receive interrupt. Each function converts its arguments, calls the
// HAL and returns VendorCode(HAL status, the handle's ErrorCode); the handles live in the tables
// below. SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

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

[[nodiscard]] uint16_t Size16(std::size_t n) noexcept {
  return static_cast<uint16_t>(n);
}

// --- I2C -----------------------------------------------------------------------------------------
std::array<I2C_HandleTypeDef, kI2cs>& I2cs() noexcept {
  // codeql::misra_deviation_next_line(DEV-KBP-STM32-002)
  static std::array<I2C_HandleTypeDef, kI2cs> handles{};
  return handles;
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
std::array<SPI_HandleTypeDef, kSpis>& Spis() noexcept {
  // codeql::misra_deviation_next_line(DEV-KBP-STM32-002)
  static std::array<SPI_HandleTypeDef, kSpis> handles{};
  return handles;
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
  // codeql::misra_deviation_next_line(DEV-KBP-STM32-002)
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
  static_cast<void>(instance); // every instance runs from PCLK1 by default
  return HAL_RCC_GetPCLK1Freq();
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
} // namespace

uint32_t I2cOpen(const I2cConfig& config, uint32_t timing) noexcept {
  I2C_HandleTypeDef& h = I2cs()[config.instance];
  // The handle's Instance is the owner mark: set while one I2cController has the peripheral open.
  return (h.Instance == nullptr) ? I2cInitHandle(h, config, timing) : port_code::kOwned;
}

uint32_t I2cClose(uint8_t instance) noexcept {
  I2C_HandleTypeDef& h = I2cs()[instance];
  const HAL_StatusTypeDef s = HAL_I2C_DeInit(&h);
  const uint32_t code = Code(s, h.ErrorCode);
  h.Instance = nullptr;
  return code;
}

uint32_t I2cTransmit(uint8_t instance, uint8_t address, std::span<const uint8_t> tx,
                     uint32_t timeout_ms) noexcept {
  I2C_HandleTypeDef& h = I2cs()[instance];
  // The HAL's transmit takes a non-const pointer but only reads through it.
  auto* data = const_cast<uint8_t*>(tx.data()); // NOLINT(cppcoreguidelines-pro-type-const-cast)
  const HAL_StatusTypeDef s =
      HAL_I2C_Master_Transmit(&h, Address8(address), data, Size16(tx.size()), timeout_ms);
  return Code(s, h.ErrorCode);
}

uint32_t I2cMemRead(uint8_t instance, uint8_t address, std::span<const uint8_t> tx,
                    std::span<uint8_t> rx, uint32_t timeout_ms) noexcept {
  I2C_HandleTypeDef& h = I2cs()[instance];
  const bool two = tx.size() == 2U;
  const uint16_t mem =
      two ? static_cast<uint16_t>((uint32_t{tx[0]} << 8U) | uint32_t{tx[1]}) : uint16_t{tx[0]};
  const HAL_StatusTypeDef s = HAL_I2C_Mem_Read(&h, Address8(address), mem,
                                               two ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT,
                                               rx.data(), Size16(rx.size()), timeout_ms);
  return Code(s, h.ErrorCode);
}

uint32_t I2cReceive(uint8_t instance, uint8_t address, std::span<uint8_t> rx,
                    uint32_t timeout_ms) noexcept {
  I2C_HandleTypeDef& h = I2cs()[instance];
  const HAL_StatusTypeDef s =
      HAL_I2C_Master_Receive(&h, Address8(address), rx.data(), Size16(rx.size()), timeout_ms);
  return Code(s, h.ErrorCode);
}

uint32_t I2cProbe(uint8_t instance, uint8_t address, uint32_t timeout_ms) noexcept {
  I2C_HandleTypeDef& h = I2cs()[instance];
  const HAL_StatusTypeDef s = HAL_I2C_IsDeviceReady(&h, Address8(address), 1U, timeout_ms);
  return Code(s, h.ErrorCode);
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

uint32_t SpiOpen(const SpiBusConfig& config) noexcept {
  SPI_HandleTypeDef& h = Spis()[config.instance];
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
    }
  }
  return code;
}

uint32_t SpiConfigure(uint8_t instance, uint8_t mode, uint8_t exponent) noexcept {
  SPI_HandleTypeDef& h = Spis()[instance];
  HAL_StatusTypeDef s = HAL_SPI_DeInit(&h);
  if (s == HAL_OK) {
    SpiInitFields(h, mode, exponent);
    s = HAL_SPI_Init(&h);
  }
  return Code(s, h.ErrorCode);
}

uint32_t SpiClose(uint8_t instance) noexcept {
  SPI_HandleTypeDef& h = Spis()[instance];
  const HAL_StatusTypeDef s = HAL_SPI_DeInit(&h);
  const uint32_t code = Code(s, h.ErrorCode);
  h.Instance = nullptr;
  return code;
}

uint32_t SpiTransfer(uint8_t instance, std::span<const uint8_t> tx, std::span<uint8_t> rx,
                     uint32_t timeout_ms) noexcept {
  SPI_HandleTypeDef& h = Spis()[instance];
  HAL_StatusTypeDef s = HAL_OK;
  if (tx.empty()) {
    s = HAL_SPI_Receive(&h, rx.data(), Size16(rx.size()), timeout_ms); // clocks out the zeroed rx
  } else if (rx.empty()) {
    s = HAL_SPI_Transmit(&h, tx.data(), Size16(tx.size()), timeout_ms);
  } else {
    s = HAL_SPI_TransmitReceive(&h, tx.data(), rx.data(), Size16(tx.size()), timeout_ms);
  }
  // A timeout leaves the peripheral mid-frame: abort it (the logic initialises it again before the
  // next frame).
  if ((s == HAL_TIMEOUT) && (HAL_SPI_Abort(&h) != HAL_OK)) {
    h.ErrorCode |= HAL_SPI_ERROR_ABORT;
  }
  return Code(s, h.ErrorCode);
}

// --- UART ----------------------------------------------------------------------------------------
bool UartExists(uint8_t instance) noexcept {
  return (UartSlotOf(instance) < kUarts) && (UartOf(instance) != nullptr);
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
    const HAL_StatusTypeDef s = HAL_UART_Init(&slot.handle);
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

uint32_t UartTransmit(uint8_t instance, std::span<const uint8_t> tx, uint32_t timeout_ms) noexcept {
  UART_HandleTypeDef& h = Uarts()[UartSlotOf(instance)].handle;
  const HAL_StatusTypeDef s = HAL_UART_Transmit(&h, tx.data(), Size16(tx.size()), timeout_ms);
  return Code(s, h.ErrorCode);
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

} // namespace kb::port::stm32::sdk

namespace kb::port::stm32 {

void HandleUartIrq(uint8_t instance) noexcept {
  const std::size_t index = sdk::UartSlotOf(instance);
  if (index < sdk::kUarts) {
    sdk::UartSlot& slot = sdk::Uarts()[index];
    USART_TypeDef* u = slot.handle.Instance;
    if ((slot.owner != nullptr) && (u != nullptr)) {
#if defined(STM32F4)
      // F4: reading SR then DR clears the error flags; a byte with an error is dropped.
      const uint32_t status = u->SR;
      const auto byte = static_cast<uint8_t>(u->DR & 0xFFU);
      const uint32_t flags = sdk::ErrorFlags(status);
      const bool data = (status & USART_SR_RXNE) != 0U;
#else
      const uint32_t status = u->ISR;
      const uint32_t flags = sdk::ErrorFlags(status);
      u->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_PECF | USART_ICR_NECF;
#if defined(USART_ISR_RXNE_RXFNE)
      const bool data = (status & USART_ISR_RXNE_RXFNE) != 0U;
#else
      const bool data = (status & USART_ISR_RXNE) != 0U;
#endif
      const uint8_t byte = data ? static_cast<uint8_t>(u->RDR & 0xFFU) : 0U;
#endif
      if (flags != 0U) {
        slot.owner->OnErrors(flags);
      }
      const uint32_t corrupt = uart_flags::kFrameError | uart_flags::kParityError;
      if (data && ((flags & corrupt) == 0U)) {
        slot.owner->OnByte(byte);
      }
    }
  }
}

} // namespace kb::port::stm32
