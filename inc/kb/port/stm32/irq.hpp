/**
 * @file irq.hpp
 * @brief The interrupt entry points of the port. The port's own vector file
 * (src/stm32_vectors_boundary.cpp, on unless KB_PORT_STM32_VECTORS=OFF) defines the USART, UART,
 * LPUART, EXTI and TIM2/TIM5 handlers of the device and calls these; an application that defines
 * those vectors itself turns the file off and calls them from its handlers. None of them calls a
 * HAL interrupt handler or a HAL callback.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_IRQ_HPP
#define KB_PORT_STM32_IRQ_HPP

#include <cstdint>

namespace kb::port::stm32 {

/** @brief The receive interrupt of UART `instance` (1 to 8, or kLpuart1). */
void HandleUartIrq(uint8_t instance) noexcept;
/** @brief The EXTI lines `first` to `last` (0 to 15): calls the callback of each pending line. */
void HandleExtiIrq(uint8_t first, uint8_t last) noexcept;
/** @brief The update interrupt of the clock's timer (TIM2 or TIM5). */
void HandleClockIrq(uint8_t timer) noexcept;

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_IRQ_HPP
