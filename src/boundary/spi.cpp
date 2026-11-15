// Boundary file (deviations/boundary-files.yml): the SPI calls of the STM32 HAL behind sdk.hpp, the
// HAL's SPI completion callbacks and the SPI interrupt entry point. Each function converts its
// arguments, calls the HAL and returns VendorCode(HAL status, the handle's ErrorCode); the handles
// live in the table below. Every decision is in the platform-free src/spi.cpp.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <array>
// Slot pointers shared with the interrupts are std::atomic (release before the NVIC enable).
// misra-lint: ignore KB-CPP-012
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/gpio.hpp"
#include "kb/port/stm32/irq.hpp"
#include "kb/port/stm32/spi.hpp"
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
constexpr std::size_t kSpis = 7U;

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

// --- SPI -----------------------------------------------------------------------------------------
struct SpiSlot {
  SPI_HandleTypeDef handle{};
  std::atomic<Completion*> done{nullptr}; ///< The bus's completion while it is open.
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
} // namespace

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

bool SpiHasFifo() noexcept {
#if defined(STM32F4)
  return false;
#else
  return true;
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
      slot.done.store(&done, std::memory_order_release);
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
  slot.done.store(nullptr, std::memory_order_release);
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

} // namespace kb::port::stm32::sdk

namespace kb::port::stm32 {

void HandleSpiIrq(uint8_t instance) noexcept {
  if (instance < sdk::kSpis) {
    sdk::SpiSlot& slot = sdk::Spis()[instance];
    if (slot.done.load(std::memory_order_acquire) == nullptr) {
      HAL_NVIC_DisableIRQ(sdk::SpiIrq(instance)); // not the port's: no interrupt storm
    } else {
      HAL_SPI_IRQHandler(&slot.handle);
    }
  }
}

} // namespace kb::port::stm32

// The HAL's SPI callbacks (weak in the HAL, defined here): the end of the transfer the port
// started, into the owner's Completion. C linkage and global names by definition.
namespace {
void SpiDone(const SPI_HandleTypeDef* h, uint32_t code) noexcept {
  kb::port::stm32::sdk::SpiSlot* const slot = kb::port::stm32::sdk::SpiSlotOf(h);
  kb::port::stm32::Completion* const done =
      (slot != nullptr) ? slot->done.load(std::memory_order_acquire) : nullptr;
  if (done != nullptr) {
    done->Complete(code);
  }
}
[[nodiscard]] uint32_t Failed(uint32_t error_code) noexcept {
  return kb::port::stm32::VendorCode(static_cast<uint32_t>(HAL_ERROR), error_code);
}
} // namespace

extern "C" {
// NOLINTBEGIN(readability-identifier-naming): the HAL fixes these names
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
