// Pins and instances of the STM32H743xx example (alternate functions from the device datasheet's
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
inline constexpr const char* kDevice = "STM32H743xx";
// I2C1 on PB8 (SCL) and PB9 (SDA), AF4.
inline constexpr uint8_t kI2c = 1U;
inline constexpr PinId kI2cScl{GpioPort::B, 8U, 4U};
inline constexpr PinId kI2cSda{GpioPort::B, 9U, 4U};
// SPI1 on PA5 (SCK), PA6 (MISO), PB5 (MOSI), AF5; chip select PD14.
inline constexpr uint8_t kSpi = 1U;
inline constexpr PinId kSpiSck{GpioPort::A, 5U, 5U};
inline constexpr PinId kSpiMiso{GpioPort::A, 6U, 5U};
inline constexpr PinId kSpiMosi{GpioPort::B, 5U, 5U};
inline constexpr PinId kSpiCs{GpioPort::D, 14U, 0U};
inline constexpr PinId kTmcEnn{GpioPort::E, 2U, 0U};
inline constexpr PinId kBnoInt{GpioPort::E, 3U, 0U};
// USART3 on PD8 (TX) and PD9 (RX), AF7.
inline constexpr uint8_t kUart = 3U;
inline constexpr PinId kUartTx{GpioPort::D, 8U, 7U};
inline constexpr PinId kUartRx{GpioPort::D, 9U, 7U};
// TIM4 channel 1 on PD12, AF2.
inline constexpr uint8_t kPwmTimer = 4U;
inline constexpr uint8_t kPwmChannel = 1U;
inline constexpr PinId kPwmPin{GpioPort::D, 12U, 2U};
// ADC1 channel 15 (INP15) on PA3.
inline constexpr uint8_t kAdc = 1U;
inline constexpr uint8_t kAdcChannel = 15U;
inline constexpr PinId kAdcPin{GpioPort::A, 3U, 0U};
// FDCAN1 on PD1 (TX) and PD0 (RX), AF9.
inline constexpr PinId kCanTx{GpioPort::D, 1U, 9U};
inline constexpr PinId kCanRx{GpioPort::D, 0U, 9U};
} // namespace board

#endif // KB_PORT_STM32_EXAMPLE_BOARD_HPP
