// Boundary file (deviations/boundary-files.yml): the SPI side of a DMA frame behind sdk.hpp, by
// register: the data register addresses, the DMA request enables, the enable and start bits in the
// family's order (H7: TSIZE with SPE clear, RXDMAEN, then after the streams TXDMAEN, SPE, CSTART;
// F4, G4, L4: RXDMAEN, then TXDMAEN, SPE), the error flags and the end of the frame. The HAL handle
// of the bus (src/boundary/spi.cpp) stays READY: the HAL is not in the DMA path. Every decision is
// in the platform-free src/spi_dma.cpp. SPDX-License-Identifier: MIT Copyright (c) 2026 The
// port-stm32 authors
#include <bit>
#include <cstddef>
#include <cstdint>

#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/spi_dma.hpp"
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
[[nodiscard]] SPI_TypeDef* SpiRegs(uint8_t instance) noexcept {
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

[[nodiscard]] std::uintptr_t AddressOfRegister(const volatile uint32_t& reg) noexcept {
  // The DMA's peripheral address: the register's bus address (32 bits on every family).
  const volatile uint32_t* const p = &reg;
  return std::bit_cast<std::uintptr_t>(p);
}
} // namespace

std::uintptr_t SpiDmaTxRegister(uint8_t instance) noexcept {
  const SPI_TypeDef* const s = SpiRegs(instance);
#if defined(STM32H7)
  return (s != nullptr) ? AddressOfRegister(s->TXDR) : 0U;
#else
  return (s != nullptr) ? AddressOfRegister(s->DR) : 0U;
#endif
}

std::uintptr_t SpiDmaRxRegister(uint8_t instance) noexcept {
  const SPI_TypeDef* const s = SpiRegs(instance);
#if defined(STM32H7)
  return (s != nullptr) ? AddressOfRegister(s->RXDR) : 0U;
#else
  return (s != nullptr) ? AddressOfRegister(s->DR) : 0U;
#endif
}

uint32_t SpiDmaPrepare(uint8_t instance, uint32_t bytes) noexcept {
  SPI_TypeDef* const s = SpiRegs(instance);
  uint32_t code = port_code::kInvalidArgument;
  if ((s != nullptr) && (bytes > 0U) && (bytes <= 0xFFFFU)) {
#if defined(STM32H7)
    // TSIZE may only be written with SPE clear; the stale end-of-transfer and error flags go first.
    s->CR1 &= ~SPI_CR1_SPE;
    s->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_UDRC | SPI_IFCR_OVRC | SPI_IFCR_CRCEC |
              SPI_IFCR_TIFREC | SPI_IFCR_MODFC | SPI_IFCR_SUSPC;
    s->CR2 = (s->CR2 & ~SPI_CR2_TSIZE) | (bytes & SPI_CR2_TSIZE);
    s->CFG1 |= SPI_CFG1_RXDMAEN;
#else
    // A stale overrun clears with a read of DR then of SR (RM0090, RM0440, RM0351).
    static_cast<void>(s->DR);
    static_cast<void>(s->SR);
    s->CR2 |= SPI_CR2_RXDMAEN;
#endif
    code = hal::kOk;
  }
  return code;
}

void SpiDmaGo(uint8_t instance) noexcept {
  SPI_TypeDef* const s = SpiRegs(instance);
  if (s != nullptr) {
#if defined(STM32H7)
    s->CFG1 |= SPI_CFG1_TXDMAEN;
    s->CR1 |= SPI_CR1_SPE;
    s->CR1 |= SPI_CR1_CSTART;
#else
    s->CR2 |= SPI_CR2_TXDMAEN;
    s->CR1 |= SPI_CR1_SPE;
#endif
  }
}

uint32_t SpiDmaErrors(uint8_t instance) noexcept {
  const SPI_TypeDef* const s = SpiRegs(instance);
  uint32_t errors = 0U;
  if (s != nullptr) {
    const uint32_t sr = s->SR;
    errors |= ((sr & SPI_SR_OVR) != 0U) ? kSpiDmaOverrun : 0U;
    errors |= ((sr & SPI_SR_MODF) != 0U) ? kSpiDmaModeFault : 0U;
#if defined(STM32H7)
    errors |= ((sr & SPI_SR_UDR) != 0U) ? kSpiDmaUnderrun : 0U;
    errors |= ((sr & SPI_SR_CRCE) != 0U) ? kSpiDmaCrc : 0U;
#else
    errors |= ((sr & SPI_SR_CRCERR) != 0U) ? kSpiDmaCrc : 0U;
#endif
  }
  return errors;
}

bool SpiDmaIdle(uint8_t instance) noexcept {
  const SPI_TypeDef* const s = SpiRegs(instance);
  bool idle = true;
  if (s != nullptr) {
#if defined(STM32H7)
    idle = (s->SR & SPI_SR_EOT) != 0U;
#else
    idle = (s->SR & SPI_SR_BSY) == 0U;
#endif
  }
  return idle;
}

void SpiDmaFinish(uint8_t instance) noexcept {
  SPI_TypeDef* const s = SpiRegs(instance);
  if (s != nullptr) {
#if defined(STM32H7)
    s->CFG1 &= ~(SPI_CFG1_TXDMAEN | SPI_CFG1_RXDMAEN);
    s->CR1 &= ~SPI_CR1_SPE; // TSIZE of the next frame, and the HAL's interrupt path, need SPE clear
    s->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_UDRC | SPI_IFCR_OVRC | SPI_IFCR_CRCEC |
              SPI_IFCR_TIFREC | SPI_IFCR_MODFC | SPI_IFCR_SUSPC;
#else
    s->CR2 &= ~(SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN);
    static_cast<void>(s->DR);
    static_cast<void>(s->SR);
#endif
  }
}

} // namespace kb::port::stm32::sdk
