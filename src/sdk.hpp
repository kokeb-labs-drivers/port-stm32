/**
 * @file sdk.hpp
 * @brief The seam between the port's platform-free logic (the src/ files outside the boundary list)
 * and its boundary files (src/stm32_<area>_boundary.cpp): one function per HAL operation, in kb
 * types only. Each returns the vendor code (`VendorCode(HAL status, ErrorCode)`; 0 is success).
 * Private to the port.
 *
 * The boundary files are the only files that include HAL or CMSIS headers
 * (deviations/boundary-files.yml). HAL handles stay in the boundary files, in tables indexed by the
 * instance number.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 The port-stm32 authors
 */
#ifndef KB_PORT_STM32_SRC_SDK_HPP
#define KB_PORT_STM32_SRC_SDK_HPP

#include <cstddef>
#include <cstdint>
#include <span>

#include "kb/interfaces/types.hpp"
#include "kb/port/stm32/adc.hpp"
#include "kb/port/stm32/can.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/gpio.hpp"
#include "kb/port/stm32/i2c.hpp"
#include "kb/port/stm32/pwm.hpp"
#include "kb/port/stm32/spi.hpp"
#include "kb/port/stm32/time.hpp"
#include "kb/port/stm32/uart.hpp"

namespace kb::port::stm32::sdk {

// --- system -----------------------------------------------------------------------------------
/** @brief HAL_GetTick(): milliseconds of the HAL time base. */
[[nodiscard]] uint32_t TickMs() noexcept;
/** @brief SystemCoreClock. */
[[nodiscard]] uint32_t CoreClockHz() noexcept;
/** @brief The DWT cycle counter, enabled on first use. */
[[nodiscard]] uint32_t Cycles() noexcept;

// --- GPIO ---------------------------------------------------------------------------------------
[[nodiscard]] bool PortExists(GpioPort port) noexcept;
/** @brief Claims `pin` for one object (port_code::kOwned when another has it). */
[[nodiscard]] uint32_t PinClaim(PinId pin) noexcept;
void PinUnclaim(PinId pin) noexcept;
[[nodiscard]] uint32_t PinOutput(PinId pin, bool open_drain, Pull pull, Speed speed,
                                 bool high) noexcept;
[[nodiscard]] uint32_t PinInput(PinId pin, Pull pull) noexcept;
/** @brief The pin in its alternate function (open drain for I2C). */
[[nodiscard]] uint32_t PinAlternate(PinId pin, bool open_drain, Pull pull) noexcept;
[[nodiscard]] uint32_t PinAnalog(PinId pin) noexcept;
void PinReset(PinId pin) noexcept;
void PinWrite(PinId pin, bool high) noexcept;
[[nodiscard]] bool PinRead(PinId pin) noexcept;
/** @brief Toggles the output with interrupts masked between the read and the write. */
void PinToggle(PinId pin) noexcept;
/** @brief EXTI on the pin's line (port_code::kOwned when the line is taken by another pin). */
[[nodiscard]] uint32_t ExtiEnable(PinId pin, kb::iface::Edge edge, kb::iface::GpioCallback fn,
                                  void* context, uint8_t priority) noexcept;
[[nodiscard]] uint32_t ExtiDisable(PinId pin) noexcept;

// --- I2C ----------------------------------------------------------------------------------------
[[nodiscard]] bool I2cExists(uint8_t instance) noexcept;
/** @brief True on the I2C v2 peripheral (TIMINGR: G4, H7, L4); false on F4. */
[[nodiscard]] bool I2cHasTimingRegister() noexcept;
[[nodiscard]] uint32_t I2cKernelClockHz(uint8_t instance) noexcept;
[[nodiscard]] uint32_t I2cOpen(const I2cConfig& config, uint32_t timing) noexcept;
[[nodiscard]] uint32_t I2cClose(uint8_t instance) noexcept;
[[nodiscard]] uint32_t I2cTransmit(uint8_t instance, uint8_t address, std::span<const uint8_t> tx,
                                   uint32_t timeout_ms) noexcept;
/** @brief HAL_I2C_Mem_Read: `tx` (1 or 2 bytes) as the memory address, repeated START, `rx`. */
[[nodiscard]] uint32_t I2cMemRead(uint8_t instance, uint8_t address, std::span<const uint8_t> tx,
                                  std::span<uint8_t> rx, uint32_t timeout_ms) noexcept;
[[nodiscard]] uint32_t I2cReceive(uint8_t instance, uint8_t address, std::span<uint8_t> rx,
                                  uint32_t timeout_ms) noexcept;
[[nodiscard]] uint32_t I2cProbe(uint8_t instance, uint8_t address, uint32_t timeout_ms) noexcept;

// --- SPI ----------------------------------------------------------------------------------------
[[nodiscard]] bool SpiExists(uint8_t instance) noexcept;
[[nodiscard]] uint32_t SpiKernelClockHz(uint8_t instance) noexcept;
[[nodiscard]] uint32_t SpiOpen(const SpiBusConfig& config) noexcept;
/** @brief HAL_SPI_Init again in `mode` with prescaler 2^(exponent + 1). */
[[nodiscard]] uint32_t SpiConfigure(uint8_t instance, uint8_t mode, uint8_t exponent) noexcept;
[[nodiscard]] uint32_t SpiClose(uint8_t instance) noexcept;
/** @brief One blocking transfer of at most 65535 bytes; a timeout aborts it (HAL_SPI_Abort). */
[[nodiscard]] uint32_t SpiTransfer(uint8_t instance, std::span<const uint8_t> tx,
                                   std::span<uint8_t> rx, uint32_t timeout_ms) noexcept;

// --- UART ---------------------------------------------------------------------------------------
[[nodiscard]] bool UartExists(uint8_t instance) noexcept;
/** @brief Init, the receive interrupt and the owner that HandleUartIrq() calls. */
[[nodiscard]] uint32_t UartOpen(const UartConfig& config, Uart& owner) noexcept;
[[nodiscard]] uint32_t UartClose(uint8_t instance) noexcept;
[[nodiscard]] uint32_t UartTransmit(uint8_t instance, std::span<const uint8_t> tx,
                                    uint32_t timeout_ms) noexcept;
[[nodiscard]] uint32_t UartSetBaud(uint8_t instance, uint32_t baud) noexcept;
/** @brief The transmission complete flag (TC). */
[[nodiscard]] bool UartTxComplete(uint8_t instance) noexcept;

// --- ADC ----------------------------------------------------------------------------------------
[[nodiscard]] bool AdcExists(uint8_t instance, uint8_t channel) noexcept;
[[nodiscard]] uint8_t AdcBits(uint8_t instance) noexcept;
/** @brief Opens (or shares) the instance; calibrates it on first open where the family can. */
[[nodiscard]] uint32_t AdcOpen(uint8_t instance) noexcept;
[[nodiscard]] uint32_t AdcClose(uint8_t instance) noexcept;
/** @brief One conversion under the instance's lock (port_code::kLockTimeout when not had in time).
 */
[[nodiscard]] uint32_t AdcConvert(uint8_t instance, uint8_t channel, AdcSampling sampling,
                                  uint32_t conversion_ms, uint32_t lock_ms, uint32_t& raw) noexcept;

// --- timers (PWM) -------------------------------------------------------------------------------
[[nodiscard]] bool TimerExists(uint8_t timer) noexcept;
[[nodiscard]] uint32_t TimerClockHz(uint8_t timer) noexcept;
/** @brief The timer (once, shared by its channels) and the channel at compare 0, started. */
[[nodiscard]] uint32_t PwmOpen(const PwmConfig& config, const PwmPeriod& period) noexcept;
[[nodiscard]] uint32_t PwmSetPeriod(uint8_t timer, const PwmPeriod& period) noexcept;
void PwmSetCompare(uint8_t timer, uint8_t channel, uint32_t compare) noexcept;
[[nodiscard]] uint32_t PwmClose(uint8_t timer, uint8_t channel) noexcept;

// --- CAN ----------------------------------------------------------------------------------------
[[nodiscard]] bool CanExists(uint8_t controller) noexcept;
/** @brief The limits of this family's controller (FDCAN or bxCAN). */
[[nodiscard]] CanTimingLimits CanLimits() noexcept;
[[nodiscard]] uint32_t CanKernelClockHz(uint8_t controller) noexcept;
[[nodiscard]] uint32_t CanOpen(const CanConfig& config, const CanTiming& timing) noexcept;
[[nodiscard]] uint32_t CanClose(uint8_t controller) noexcept;
[[nodiscard]] uint32_t CanTxFree(uint8_t controller, uint32_t& free) noexcept;
[[nodiscard]] uint32_t CanRxCount(uint8_t controller, uint32_t& count) noexcept;
[[nodiscard]] uint32_t CanPut(uint8_t controller, const kb::iface::CanFrame& frame) noexcept;
[[nodiscard]] uint32_t CanGet(uint8_t controller, kb::iface::CanFrame& frame) noexcept;
[[nodiscard]] uint32_t CanReadStatus(uint8_t controller, CanStatus& status) noexcept;
/** @brief FDCAN: stop and start (leaves bus-off). bxCAN: nothing (automatic bus-off recovery). */
[[nodiscard]] uint32_t CanRestart(uint8_t controller) noexcept;

// --- clock, ITM
// -----------------------------------------------------------------------------------
[[nodiscard]] bool ClockTimerExists(uint8_t timer) noexcept;
[[nodiscard]] uint32_t ClockStart(const ClockConfig& config) noexcept;
/** @brief The wrap count, the counter and the wrap count again, and the pending-update flag. */
void ClockSample(uint8_t timer, uint32_t& high_before, uint32_t& counter, uint32_t& high_after,
                 bool& wrap_pending) noexcept;
/** @brief True when the ITM and stimulus port 0 are enabled (a probe listens). */
[[nodiscard]] bool ItmEnabled() noexcept;
/** @brief Puts one character when port 0 is free within `spins` polls; false when dropped. */
[[nodiscard]] bool ItmPut(char c, uint32_t spins) noexcept;

} // namespace kb::port::stm32::sdk

#endif // KB_PORT_STM32_SRC_SDK_HPP
