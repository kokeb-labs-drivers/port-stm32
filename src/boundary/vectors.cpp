// Boundary file (deviations/boundary-files.yml): the interrupt vectors the port serves, defined
// over the device header's instance macros so each exists only where the device has it. Built
// unless KB_PORT_STM32_VECTORS is OFF, as an object library linked into the firmware directly (a
// weak reference from the startup file never pulls a member out of an archive);
// tools/check_vectors.cmake checks every family's firmware for them. An application that defines
// these vectors itself turns the file off, or names the vectors it keeps in
// KB_PORT_STM32_VECTORS_EXCLUDE (each becomes KB_PORT_STM32_NO_<vector>), and calls the entry
// points of irq.hpp from its own handlers for the port's instances. The DMA vectors dispatch to the
// started DmaStream of the stream (src/dma.cpp); a stream the port does not own has its line
// disabled. SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
#include <cstdint>

#include "kb/port/stm32/irq.hpp"
#include "kb/port/stm32/uart.hpp"
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

namespace port = kb::port::stm32;

// The vector table names these handlers; they have C linkage and global names by definition.
extern "C" {
// NOLINTBEGIN(readability-identifier-naming): the vector table fixes these names

#if !defined(KB_PORT_STM32_NO_EXTI0_IRQHandler)
void EXTI0_IRQHandler(void) {
  port::HandleExtiIrq(0U, 0U);
}
#endif
#if !defined(KB_PORT_STM32_NO_EXTI1_IRQHandler)
void EXTI1_IRQHandler(void) {
  port::HandleExtiIrq(1U, 1U);
}
#endif
#if !defined(KB_PORT_STM32_NO_EXTI2_IRQHandler)
void EXTI2_IRQHandler(void) {
  port::HandleExtiIrq(2U, 2U);
}
#endif
#if !defined(KB_PORT_STM32_NO_EXTI3_IRQHandler)
void EXTI3_IRQHandler(void) {
  port::HandleExtiIrq(3U, 3U);
}
#endif
#if !defined(KB_PORT_STM32_NO_EXTI4_IRQHandler)
void EXTI4_IRQHandler(void) {
  port::HandleExtiIrq(4U, 4U);
}
#endif
#if !defined(KB_PORT_STM32_NO_EXTI9_5_IRQHandler)
void EXTI9_5_IRQHandler(void) {
  port::HandleExtiIrq(5U, 9U);
}
#endif
#if !defined(KB_PORT_STM32_NO_EXTI15_10_IRQHandler)
void EXTI15_10_IRQHandler(void) {
  port::HandleExtiIrq(10U, 15U);
}
#endif
#if defined(TIM2)
#if !defined(KB_PORT_STM32_NO_TIM2_IRQHandler)
void TIM2_IRQHandler(void) {
  port::HandleClockIrq(2U);
}
#endif
#endif
#if defined(TIM5)
#if !defined(KB_PORT_STM32_NO_TIM5_IRQHandler)
void TIM5_IRQHandler(void) {
  port::HandleClockIrq(5U);
}
#endif
#endif
#if defined(USART1)
#if !defined(KB_PORT_STM32_NO_USART1_IRQHandler)
void USART1_IRQHandler(void) {
  port::HandleUartIrq(1U);
}
#endif
#endif
#if defined(USART2)
#if !defined(KB_PORT_STM32_NO_USART2_IRQHandler)
void USART2_IRQHandler(void) {
  port::HandleUartIrq(2U);
}
#endif
#endif
#if defined(USART3)
#if !defined(KB_PORT_STM32_NO_USART3_IRQHandler)
void USART3_IRQHandler(void) {
  port::HandleUartIrq(3U);
}
#endif
#endif
#if defined(UART4)
#if !defined(KB_PORT_STM32_NO_UART4_IRQHandler)
void UART4_IRQHandler(void) {
  port::HandleUartIrq(4U);
}
#endif
#endif
#if defined(UART5)
#if !defined(KB_PORT_STM32_NO_UART5_IRQHandler)
void UART5_IRQHandler(void) {
  port::HandleUartIrq(5U);
}
#endif
#endif
#if defined(USART6)
#if !defined(KB_PORT_STM32_NO_USART6_IRQHandler)
void USART6_IRQHandler(void) {
  port::HandleUartIrq(6U);
}
#endif
#endif
#if defined(UART7)
#if !defined(KB_PORT_STM32_NO_UART7_IRQHandler)
void UART7_IRQHandler(void) {
  port::HandleUartIrq(7U);
}
#endif
#endif
#if defined(UART8)
#if !defined(KB_PORT_STM32_NO_UART8_IRQHandler)
void UART8_IRQHandler(void) {
  port::HandleUartIrq(8U);
}
#endif
#endif
#if defined(LPUART1)
#if !defined(KB_PORT_STM32_NO_LPUART1_IRQHandler)
void LPUART1_IRQHandler(void) {
  port::HandleUartIrq(port::kLpuart1);
}
#endif
#endif

// I2C event and error interrupts.
#if !defined(KB_PORT_STM32_NO_I2C1_EV_IRQHandler)
void I2C1_EV_IRQHandler(void) {
  port::HandleI2cIrq(1U, false);
}
#endif
#if !defined(KB_PORT_STM32_NO_I2C1_ER_IRQHandler)
void I2C1_ER_IRQHandler(void) {
  port::HandleI2cIrq(1U, true);
}
#endif
#if defined(I2C2)
#if !defined(KB_PORT_STM32_NO_I2C2_EV_IRQHandler)
void I2C2_EV_IRQHandler(void) {
  port::HandleI2cIrq(2U, false);
}
#endif
#if !defined(KB_PORT_STM32_NO_I2C2_ER_IRQHandler)
void I2C2_ER_IRQHandler(void) {
  port::HandleI2cIrq(2U, true);
}
#endif
#endif
#if defined(I2C3)
#if !defined(KB_PORT_STM32_NO_I2C3_EV_IRQHandler)
void I2C3_EV_IRQHandler(void) {
  port::HandleI2cIrq(3U, false);
}
#endif
#if !defined(KB_PORT_STM32_NO_I2C3_ER_IRQHandler)
void I2C3_ER_IRQHandler(void) {
  port::HandleI2cIrq(3U, true);
}
#endif
#endif
#if defined(I2C4)
#if !defined(KB_PORT_STM32_NO_I2C4_EV_IRQHandler)
void I2C4_EV_IRQHandler(void) {
  port::HandleI2cIrq(4U, false);
}
#endif
#if !defined(KB_PORT_STM32_NO_I2C4_ER_IRQHandler)
void I2C4_ER_IRQHandler(void) {
  port::HandleI2cIrq(4U, true);
}
#endif
#endif

// SPI interrupts.
#if !defined(KB_PORT_STM32_NO_SPI1_IRQHandler)
void SPI1_IRQHandler(void) {
  port::HandleSpiIrq(1U);
}
#endif
#if defined(SPI2)
#if !defined(KB_PORT_STM32_NO_SPI2_IRQHandler)
void SPI2_IRQHandler(void) {
  port::HandleSpiIrq(2U);
}
#endif
#endif
#if defined(SPI3)
#if !defined(KB_PORT_STM32_NO_SPI3_IRQHandler)
void SPI3_IRQHandler(void) {
  port::HandleSpiIrq(3U);
}
#endif
#endif
#if defined(SPI4)
#if !defined(KB_PORT_STM32_NO_SPI4_IRQHandler)
void SPI4_IRQHandler(void) {
  port::HandleSpiIrq(4U);
}
#endif
#endif
#if defined(SPI5)
#if !defined(KB_PORT_STM32_NO_SPI5_IRQHandler)
void SPI5_IRQHandler(void) {
  port::HandleSpiIrq(5U);
}
#endif
#endif
#if defined(SPI6)
#if !defined(KB_PORT_STM32_NO_SPI6_IRQHandler)
void SPI6_IRQHandler(void) {
  port::HandleSpiIrq(6U);
}
#endif
#endif

// ADC interrupts: the vector names differ by family.
#if defined(STM32F4)
#if !defined(KB_PORT_STM32_NO_ADC_IRQHandler)
void ADC_IRQHandler(void) {
  port::HandleAdcIrq(1U, 3U);
}
#endif
#elif defined(STM32H7)
#if !defined(KB_PORT_STM32_NO_ADC_IRQHandler)
void ADC_IRQHandler(void) {
  port::HandleAdcIrq(1U, 2U);
}
#endif
#if defined(ADC3)
#if !defined(KB_PORT_STM32_NO_ADC3_IRQHandler)
void ADC3_IRQHandler(void) {
  port::HandleAdcIrq(3U, 3U);
}
#endif
#endif
#else
#if defined(ADC2)
#if !defined(KB_PORT_STM32_NO_ADC1_2_IRQHandler)
void ADC1_2_IRQHandler(void) {
  port::HandleAdcIrq(1U, 2U);
}
#endif
#else
#if !defined(KB_PORT_STM32_NO_ADC1_IRQHandler)
void ADC1_IRQHandler(void) {
  port::HandleAdcIrq(1U, 1U);
}
#endif
#endif
#if defined(ADC3)
#if !defined(KB_PORT_STM32_NO_ADC3_IRQHandler)
void ADC3_IRQHandler(void) {
  port::HandleAdcIrq(3U, 3U);
}
#endif
#endif
#if defined(ADC4)
#if !defined(KB_PORT_STM32_NO_ADC4_IRQHandler)
void ADC4_IRQHandler(void) {
  port::HandleAdcIrq(4U, 4U);
}
#endif
#endif
#if defined(ADC5)
#if !defined(KB_PORT_STM32_NO_ADC5_IRQHandler)
void ADC5_IRQHandler(void) {
  port::HandleAdcIrq(5U, 5U);
}
#endif
#endif
#endif

// CAN interrupts: FDCAN line 0 (G4, H7) or the bxCAN TX, RX0 and SCE lines (F4, L4).
#if defined(FDCAN1)
#if !defined(KB_PORT_STM32_NO_FDCAN1_IT0_IRQHandler)
void FDCAN1_IT0_IRQHandler(void) {
  port::HandleCanIrq(1U);
}
#endif
#endif
#if defined(FDCAN2)
#if !defined(KB_PORT_STM32_NO_FDCAN2_IT0_IRQHandler)
void FDCAN2_IT0_IRQHandler(void) {
  port::HandleCanIrq(2U);
}
#endif
#endif
#if defined(FDCAN3)
#if !defined(KB_PORT_STM32_NO_FDCAN3_IT0_IRQHandler)
void FDCAN3_IT0_IRQHandler(void) {
  port::HandleCanIrq(3U);
}
#endif
#endif
#if defined(CAN1)
#if !defined(KB_PORT_STM32_NO_CAN1_TX_IRQHandler)
void CAN1_TX_IRQHandler(void) {
  port::HandleCanIrq(1U);
}
#endif
#if !defined(KB_PORT_STM32_NO_CAN1_RX0_IRQHandler)
void CAN1_RX0_IRQHandler(void) {
  port::HandleCanIrq(1U);
}
#endif
#if !defined(KB_PORT_STM32_NO_CAN1_SCE_IRQHandler)
void CAN1_SCE_IRQHandler(void) {
  port::HandleCanIrq(1U);
}
#endif
#endif
#if defined(CAN2)
#if !defined(KB_PORT_STM32_NO_CAN2_TX_IRQHandler)
void CAN2_TX_IRQHandler(void) {
  port::HandleCanIrq(2U);
}
#endif
#if !defined(KB_PORT_STM32_NO_CAN2_RX0_IRQHandler)
void CAN2_RX0_IRQHandler(void) {
  port::HandleCanIrq(2U);
}
#endif
#if !defined(KB_PORT_STM32_NO_CAN2_SCE_IRQHandler)
void CAN2_SCE_IRQHandler(void) {
  port::HandleCanIrq(2U);
}
#endif
#endif

// DMA interrupts (dma.hpp): F4 and H7 streams 0 to 7, G4 and L4 channels 1 to 8, as the device has
// them.
#if defined(DMA1_Stream0)
#if !defined(KB_PORT_STM32_NO_DMA1_Stream0_IRQHandler)
void DMA1_Stream0_IRQHandler(void) {
  port::HandleDmaIrq(1U, 0U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA1_Stream1_IRQHandler)
void DMA1_Stream1_IRQHandler(void) {
  port::HandleDmaIrq(1U, 1U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA1_Stream2_IRQHandler)
void DMA1_Stream2_IRQHandler(void) {
  port::HandleDmaIrq(1U, 2U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA1_Stream3_IRQHandler)
void DMA1_Stream3_IRQHandler(void) {
  port::HandleDmaIrq(1U, 3U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA1_Stream4_IRQHandler)
void DMA1_Stream4_IRQHandler(void) {
  port::HandleDmaIrq(1U, 4U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA1_Stream5_IRQHandler)
void DMA1_Stream5_IRQHandler(void) {
  port::HandleDmaIrq(1U, 5U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA1_Stream6_IRQHandler)
void DMA1_Stream6_IRQHandler(void) {
  port::HandleDmaIrq(1U, 6U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA1_Stream7_IRQHandler)
void DMA1_Stream7_IRQHandler(void) {
  port::HandleDmaIrq(1U, 7U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA2_Stream0_IRQHandler)
void DMA2_Stream0_IRQHandler(void) {
  port::HandleDmaIrq(2U, 0U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA2_Stream1_IRQHandler)
void DMA2_Stream1_IRQHandler(void) {
  port::HandleDmaIrq(2U, 1U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA2_Stream2_IRQHandler)
void DMA2_Stream2_IRQHandler(void) {
  port::HandleDmaIrq(2U, 2U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA2_Stream3_IRQHandler)
void DMA2_Stream3_IRQHandler(void) {
  port::HandleDmaIrq(2U, 3U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA2_Stream4_IRQHandler)
void DMA2_Stream4_IRQHandler(void) {
  port::HandleDmaIrq(2U, 4U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA2_Stream5_IRQHandler)
void DMA2_Stream5_IRQHandler(void) {
  port::HandleDmaIrq(2U, 5U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA2_Stream6_IRQHandler)
void DMA2_Stream6_IRQHandler(void) {
  port::HandleDmaIrq(2U, 6U);
}
#endif
#if !defined(KB_PORT_STM32_NO_DMA2_Stream7_IRQHandler)
void DMA2_Stream7_IRQHandler(void) {
  port::HandleDmaIrq(2U, 7U);
}
#endif
#elif defined(DMA1_Channel1)
#if defined(DMA1_Channel1)
#if !defined(KB_PORT_STM32_NO_DMA1_Channel1_IRQHandler)
void DMA1_Channel1_IRQHandler(void) {
  port::HandleDmaIrq(1U, 1U);
}
#endif
#endif
#if defined(DMA1_Channel2)
#if !defined(KB_PORT_STM32_NO_DMA1_Channel2_IRQHandler)
void DMA1_Channel2_IRQHandler(void) {
  port::HandleDmaIrq(1U, 2U);
}
#endif
#endif
#if defined(DMA1_Channel3)
#if !defined(KB_PORT_STM32_NO_DMA1_Channel3_IRQHandler)
void DMA1_Channel3_IRQHandler(void) {
  port::HandleDmaIrq(1U, 3U);
}
#endif
#endif
#if defined(DMA1_Channel4)
#if !defined(KB_PORT_STM32_NO_DMA1_Channel4_IRQHandler)
void DMA1_Channel4_IRQHandler(void) {
  port::HandleDmaIrq(1U, 4U);
}
#endif
#endif
#if defined(DMA1_Channel5)
#if !defined(KB_PORT_STM32_NO_DMA1_Channel5_IRQHandler)
void DMA1_Channel5_IRQHandler(void) {
  port::HandleDmaIrq(1U, 5U);
}
#endif
#endif
#if defined(DMA1_Channel6)
#if !defined(KB_PORT_STM32_NO_DMA1_Channel6_IRQHandler)
void DMA1_Channel6_IRQHandler(void) {
  port::HandleDmaIrq(1U, 6U);
}
#endif
#endif
#if defined(DMA1_Channel7)
#if !defined(KB_PORT_STM32_NO_DMA1_Channel7_IRQHandler)
void DMA1_Channel7_IRQHandler(void) {
  port::HandleDmaIrq(1U, 7U);
}
#endif
#endif
#if defined(DMA1_Channel8)
#if !defined(KB_PORT_STM32_NO_DMA1_Channel8_IRQHandler)
void DMA1_Channel8_IRQHandler(void) {
  port::HandleDmaIrq(1U, 8U);
}
#endif
#endif
#if defined(DMA2_Channel1)
#if !defined(KB_PORT_STM32_NO_DMA2_Channel1_IRQHandler)
void DMA2_Channel1_IRQHandler(void) {
  port::HandleDmaIrq(2U, 1U);
}
#endif
#endif
#if defined(DMA2_Channel2)
#if !defined(KB_PORT_STM32_NO_DMA2_Channel2_IRQHandler)
void DMA2_Channel2_IRQHandler(void) {
  port::HandleDmaIrq(2U, 2U);
}
#endif
#endif
#if defined(DMA2_Channel3)
#if !defined(KB_PORT_STM32_NO_DMA2_Channel3_IRQHandler)
void DMA2_Channel3_IRQHandler(void) {
  port::HandleDmaIrq(2U, 3U);
}
#endif
#endif
#if defined(DMA2_Channel4)
#if !defined(KB_PORT_STM32_NO_DMA2_Channel4_IRQHandler)
void DMA2_Channel4_IRQHandler(void) {
  port::HandleDmaIrq(2U, 4U);
}
#endif
#endif
#if defined(DMA2_Channel5)
#if !defined(KB_PORT_STM32_NO_DMA2_Channel5_IRQHandler)
void DMA2_Channel5_IRQHandler(void) {
  port::HandleDmaIrq(2U, 5U);
}
#endif
#endif
#if defined(DMA2_Channel6)
#if !defined(KB_PORT_STM32_NO_DMA2_Channel6_IRQHandler)
void DMA2_Channel6_IRQHandler(void) {
  port::HandleDmaIrq(2U, 6U);
}
#endif
#endif
#if defined(DMA2_Channel7)
#if !defined(KB_PORT_STM32_NO_DMA2_Channel7_IRQHandler)
void DMA2_Channel7_IRQHandler(void) {
  port::HandleDmaIrq(2U, 7U);
}
#endif
#endif
#if defined(DMA2_Channel8)
#if !defined(KB_PORT_STM32_NO_DMA2_Channel8_IRQHandler)
void DMA2_Channel8_IRQHandler(void) {
  port::HandleDmaIrq(2U, 8U);
}
#endif
#endif
#endif

// NOLINTEND(readability-identifier-naming)
} // extern "C"
