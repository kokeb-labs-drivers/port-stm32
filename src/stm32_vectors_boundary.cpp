// Boundary file (deviations/boundary-files.yml): the interrupt vectors the port serves, defined
// over the device header's instance macros so each exists only where the device has it. Built
// unless KB_PORT_STM32_VECTORS is OFF, as an object library linked into the firmware directly (a
// weak reference from the startup file never pulls a member out of an archive);
// tools/check_vectors.cmake checks every family's firmware for them. An application that defines
// these vectors itself turns it off and calls the entry points of irq.hpp from its own handlers.
// SPDX-License-Identifier: MIT Copyright (c) 2026 The port-stm32 authors
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

void EXTI0_IRQHandler(void) {
  port::HandleExtiIrq(0U, 0U);
}
void EXTI1_IRQHandler(void) {
  port::HandleExtiIrq(1U, 1U);
}
void EXTI2_IRQHandler(void) {
  port::HandleExtiIrq(2U, 2U);
}
void EXTI3_IRQHandler(void) {
  port::HandleExtiIrq(3U, 3U);
}
void EXTI4_IRQHandler(void) {
  port::HandleExtiIrq(4U, 4U);
}
void EXTI9_5_IRQHandler(void) {
  port::HandleExtiIrq(5U, 9U);
}
void EXTI15_10_IRQHandler(void) {
  port::HandleExtiIrq(10U, 15U);
}
#if defined(TIM2)
void TIM2_IRQHandler(void) {
  port::HandleClockIrq(2U);
}
#endif
#if defined(TIM5)
void TIM5_IRQHandler(void) {
  port::HandleClockIrq(5U);
}
#endif
#if defined(USART1)
void USART1_IRQHandler(void) {
  port::HandleUartIrq(1U);
}
#endif
#if defined(USART2)
void USART2_IRQHandler(void) {
  port::HandleUartIrq(2U);
}
#endif
#if defined(USART3)
void USART3_IRQHandler(void) {
  port::HandleUartIrq(3U);
}
#endif
#if defined(UART4)
void UART4_IRQHandler(void) {
  port::HandleUartIrq(4U);
}
#endif
#if defined(UART5)
void UART5_IRQHandler(void) {
  port::HandleUartIrq(5U);
}
#endif
#if defined(USART6)
void USART6_IRQHandler(void) {
  port::HandleUartIrq(6U);
}
#endif
#if defined(UART7)
void UART7_IRQHandler(void) {
  port::HandleUartIrq(7U);
}
#endif
#if defined(UART8)
void UART8_IRQHandler(void) {
  port::HandleUartIrq(8U);
}
#endif
#if defined(LPUART1)
void LPUART1_IRQHandler(void) {
  port::HandleUartIrq(port::kLpuart1);
}
#endif

// I2C event and error interrupts.
void I2C1_EV_IRQHandler(void) {
  port::HandleI2cIrq(1U, false);
}
void I2C1_ER_IRQHandler(void) {
  port::HandleI2cIrq(1U, true);
}
#if defined(I2C2)
void I2C2_EV_IRQHandler(void) {
  port::HandleI2cIrq(2U, false);
}
void I2C2_ER_IRQHandler(void) {
  port::HandleI2cIrq(2U, true);
}
#endif
#if defined(I2C3)
void I2C3_EV_IRQHandler(void) {
  port::HandleI2cIrq(3U, false);
}
void I2C3_ER_IRQHandler(void) {
  port::HandleI2cIrq(3U, true);
}
#endif
#if defined(I2C4)
void I2C4_EV_IRQHandler(void) {
  port::HandleI2cIrq(4U, false);
}
void I2C4_ER_IRQHandler(void) {
  port::HandleI2cIrq(4U, true);
}
#endif

// SPI interrupts.
void SPI1_IRQHandler(void) {
  port::HandleSpiIrq(1U);
}
#if defined(SPI2)
void SPI2_IRQHandler(void) {
  port::HandleSpiIrq(2U);
}
#endif
#if defined(SPI3)
void SPI3_IRQHandler(void) {
  port::HandleSpiIrq(3U);
}
#endif
#if defined(SPI4)
void SPI4_IRQHandler(void) {
  port::HandleSpiIrq(4U);
}
#endif
#if defined(SPI5)
void SPI5_IRQHandler(void) {
  port::HandleSpiIrq(5U);
}
#endif
#if defined(SPI6)
void SPI6_IRQHandler(void) {
  port::HandleSpiIrq(6U);
}
#endif

// ADC interrupts: the vector names differ by family.
#if defined(STM32F4)
void ADC_IRQHandler(void) {
  port::HandleAdcIrq(1U, 3U);
}
#elif defined(STM32H7)
void ADC_IRQHandler(void) {
  port::HandleAdcIrq(1U, 2U);
}
#if defined(ADC3)
void ADC3_IRQHandler(void) {
  port::HandleAdcIrq(3U, 3U);
}
#endif
#else
#if defined(ADC2)
void ADC1_2_IRQHandler(void) {
  port::HandleAdcIrq(1U, 2U);
}
#else
void ADC1_IRQHandler(void) {
  port::HandleAdcIrq(1U, 1U);
}
#endif
#if defined(ADC3)
void ADC3_IRQHandler(void) {
  port::HandleAdcIrq(3U, 3U);
}
#endif
#if defined(ADC4)
void ADC4_IRQHandler(void) {
  port::HandleAdcIrq(4U, 4U);
}
#endif
#if defined(ADC5)
void ADC5_IRQHandler(void) {
  port::HandleAdcIrq(5U, 5U);
}
#endif
#endif

// CAN interrupts: FDCAN line 0 (G4, H7) or the bxCAN TX, RX0 and SCE lines (F4, L4).
#if defined(FDCAN1)
void FDCAN1_IT0_IRQHandler(void) {
  port::HandleCanIrq(1U);
}
#endif
#if defined(FDCAN2)
void FDCAN2_IT0_IRQHandler(void) {
  port::HandleCanIrq(2U);
}
#endif
#if defined(FDCAN3)
void FDCAN3_IT0_IRQHandler(void) {
  port::HandleCanIrq(3U);
}
#endif
#if defined(CAN1)
void CAN1_TX_IRQHandler(void) {
  port::HandleCanIrq(1U);
}
void CAN1_RX0_IRQHandler(void) {
  port::HandleCanIrq(1U);
}
void CAN1_SCE_IRQHandler(void) {
  port::HandleCanIrq(1U);
}
#endif
#if defined(CAN2)
void CAN2_TX_IRQHandler(void) {
  port::HandleCanIrq(2U);
}
void CAN2_RX0_IRQHandler(void) {
  port::HandleCanIrq(2U);
}
void CAN2_SCE_IRQHandler(void) {
  port::HandleCanIrq(2U);
}
#endif

// NOLINTEND(readability-identifier-naming)
} // extern "C"
