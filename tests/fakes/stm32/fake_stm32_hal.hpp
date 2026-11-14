// The test control of the STM32 HAL fake (fake_stm32_hal.cpp): error injection per HAL function,
// call counts and a small model of each peripheral, so the tests drive every branch of the port.
// Test code. SPDX-License-Identifier: MIT
// Copyright (c) 2026 The port-stm32 authors
#ifndef KB_FAKE_STM32_CONTROL_HPP
#define KB_FAKE_STM32_CONTROL_HPP

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "stm32_hal_fake.h"

namespace fake {

/** @brief Everything back to power-on: no injection, pins low, time at 0. */
void Reset();
/** @brief The next `count` calls of `function` return `status` with handle ErrorCode `error_code`.
 */
void Fail(const char* function, HAL_StatusTypeDef status, uint32_t error_code = 0U,
          uint32_t count = 1U);
/** @brief Calls `fn(context)` once at the start of the next call of `function` (re-entrancy tests).
 */
void Hook(const char* function, void (*fn)(void*), void* context);
/** @brief Calls of `function` since Reset(). */
[[nodiscard]] uint32_t Calls(const char* function);
[[nodiscard]] bool ClockEnabled(const char* name);

// --- time ---------------------------------------------------------------------------------------
void SetTickStep(uint32_t step);
void SetCycleStep(uint32_t step);
[[nodiscard]] uint32_t Ticks();
[[nodiscard]] uint64_t CyclesRead();

// --- GPIO (port 0 is A)
// -----------------------------------------------------------------------------
/** @brief The level an input or output pin reads: -1 follows the model, 0 or 1 forces it. */
void ForceLevel(int port, int pin, int level);
[[nodiscard]] int Driven(int port, int pin);
[[nodiscard]] uint32_t ModeOf(int port, int pin);
[[nodiscard]] uint32_t AlternateOf(int port, int pin);
[[nodiscard]] uint32_t PullOf(int port, int pin);
[[nodiscard]] uint32_t Toggles();
[[nodiscard]] bool IrqEnabled(int irq);
[[nodiscard]] uint32_t IrqPriority(int irq);
/** @brief Marks EXTI line `line` pending. */
void ExtiPending(int line);

// --- I2C ----------------------------------------------------------------------------------------
void I2cPresent(uint8_t address, bool present);
void I2cDataNack(bool on);
void I2cReply(std::span<const uint8_t> bytes);
[[nodiscard]] const std::vector<uint8_t>& I2cWritten();
[[nodiscard]] uint16_t I2cMemAddress();
[[nodiscard]] uint16_t I2cMemSize();
[[nodiscard]] const I2C_InitTypeDef& I2cInit();

// --- SPI ----------------------------------------------------------------------------------------
void SpiReply(std::span<const uint8_t> bytes);
[[nodiscard]] const std::vector<uint8_t>& SpiSent();
[[nodiscard]] uint32_t SpiTransfers();
[[nodiscard]] const SPI_InitTypeDef& SpiInit();

// --- UART ---------------------------------------------------------------------------------------
[[nodiscard]] const std::vector<uint8_t>& UartSent();
[[nodiscard]] const UART_InitTypeDef& UartInit();
[[nodiscard]] uint32_t UartInterrupts(int index);

// --- ADC, timers --------------------------------------------------------------------------------
void AdcRaw(uint32_t raw);
[[nodiscard]] uint32_t AdcLastChannel();
[[nodiscard]] uint32_t AdcLastSampling();
[[nodiscard]] const TIM_Base_InitTypeDef& TimInit(int timer);

// --- CAN ----------------------------------------------------------------------------------------
struct Frame {
  uint32_t id{0U};
  bool extended{false};
  bool remote{false};
  uint32_t dlc{0U};
  uint8_t data[8]{};
};
void CanTxFree(uint32_t free);
void CanFeed(const Frame& frame);
[[nodiscard]] const std::vector<Frame>& CanSent();
/** @brief Bus-off on (and, for FDCAN, how many status reads after a restart it lasts). */
void CanBusOff(bool on, uint32_t reads_after_restart = 1U);
void CanCounters(uint32_t tec, uint32_t rec);
[[nodiscard]] uint32_t CanPrescaler();
[[nodiscard]] uint32_t CanMode();

} // namespace fake

#endif // KB_FAKE_STM32_CONTROL_HPP
