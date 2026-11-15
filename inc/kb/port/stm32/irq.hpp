/**
 * @file irq.hpp
 * @brief The interrupt entry points of the port. The port's own vector file
 * (src/stm32_vectors_boundary.cpp, on unless KB_PORT_STM32_VECTORS=OFF) defines the USART, UART,
 * LPUART, EXTI, TIM2/TIM5, I2C event and error, SPI, ADC and CAN handlers of the device and calls
 * these; it is linked into the firmware as objects, never from an archive, so its strong
 * definitions always replace the startup file's weak Default_Handler aliases. An application that
 * defines those vectors itself turns the file off and calls these from its handlers.
 *
 * The UART, EXTI and clock entries read the registers themselves; the I2C, SPI, ADC and CAN entries
 * call the HAL's interrupt handler of the open handle, which ends in the HAL callbacks the port
 * defines (HAL_I2C_MasterTxCpltCallback and the others, docs/peripherals/interrupts.md).
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
/** @brief The event (`error` false) or error interrupt of I2C `instance`. */
void HandleI2cIrq(uint8_t instance, bool error) noexcept;
/** @brief The interrupt of SPI `instance`. */
void HandleSpiIrq(uint8_t instance) noexcept;
/** @brief The interrupts of CAN `controller` (FDCAN line 0; bxCAN TX, RX0 and SCE). */
void HandleCanIrq(uint8_t controller) noexcept;
/** @brief The ADC instances `first` to `last` sharing one vector. */
void HandleAdcIrq(uint8_t first, uint8_t last) noexcept;

} // namespace kb::port::stm32

#endif // KB_PORT_STM32_IRQ_HPP
