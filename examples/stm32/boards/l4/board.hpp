// Pins and instances of the STM32L476xx example (alternate functions from the device datasheet's
// alternate function table). Example code.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#ifndef KB_PORT_STM32_EXAMPLE_BOARD_HPP
#define KB_PORT_STM32_EXAMPLE_BOARD_HPP

#include <cstdint>

#include "kb/port/stm32/error.hpp"

namespace board {
using kb::port::stm32::GpioPort;
using kb::port::stm32::PinId;
inline constexpr const char* kDevice = "STM32L476xx";
// I2C1 on PB8 (SCL) and PB9 (SDA), AF4.
inline constexpr uint8_t kI2c = 1U;
inline constexpr PinId kI2cScl{GpioPort::B, 8U, 4U};
inline constexpr PinId kI2cSda{GpioPort::B, 9U, 4U};
// SPI1 on PA5 (SCK), PA6 (MISO), PA7 (MOSI), AF5; chip select PB6.
inline constexpr uint8_t kSpi = 1U;
inline constexpr PinId kSpiSck{GpioPort::A, 5U, 5U};
inline constexpr PinId kSpiMiso{GpioPort::A, 6U, 5U};
inline constexpr PinId kSpiMosi{GpioPort::A, 7U, 5U};
inline constexpr PinId kSpiCs{GpioPort::B, 6U, 0U};
inline constexpr PinId kTmcEnn{GpioPort::C, 7U, 0U};
inline constexpr PinId kBnoInt{GpioPort::C, 8U, 0U};
// USART2 on PA2 (TX) and PA3 (RX), AF7.
inline constexpr uint8_t kUart = 2U;
inline constexpr PinId kUartTx{GpioPort::A, 2U, 7U};
inline constexpr PinId kUartRx{GpioPort::A, 3U, 7U};
// TIM3 channel 1 on PB4, AF2.
inline constexpr uint8_t kPwmTimer = 3U;
inline constexpr uint8_t kPwmChannel = 1U;
inline constexpr PinId kPwmPin{GpioPort::B, 4U, 2U};
// ADC1 channel 5 on PA0.
inline constexpr uint8_t kAdc = 1U;
inline constexpr uint8_t kAdcChannel = 5U;
inline constexpr PinId kAdcPin{GpioPort::A, 0U, 0U};
// CAN1 on PA12 (TX) and PA11 (RX), AF9.
inline constexpr PinId kCanTx{GpioPort::A, 12U, 9U};
inline constexpr PinId kCanRx{GpioPort::A, 11U, 9U};
} // namespace board

#endif // KB_PORT_STM32_EXAMPLE_BOARD_HPP
