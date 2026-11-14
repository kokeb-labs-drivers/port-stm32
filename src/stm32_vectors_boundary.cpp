// Boundary file (deviations/boundary-files.yml): the interrupt vectors the port serves, defined
// over the device header's instance macros so each exists only where the device has it. Built
// unless KB_PORT_STM32_VECTORS is OFF; an application that defines these vectors itself turns it
// off and calls the entry points of irq.hpp from its own handlers. SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
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

// NOLINTEND(readability-identifier-naming)
} // extern "C"
