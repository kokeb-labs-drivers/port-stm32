/**
 * @file sdk.hpp
 * @brief The seam between the port's platform-free logic (the src/ files outside the boundary list)
 * and its boundary files (src/stm32_<area>_boundary.cpp): one function per HAL operation, in kb
 * types only. Each returns the vendor code (`VendorCode(HAL status, ErrorCode)`; 0 is success).
 * Private to the port.
 *
 * The boundary files are the only files that include HAL or CMSIS headers
 * (deviations/boundary-files.yml). HAL handles stay in the boundary files, in tables indexed by the
 * instance number. Nothing here waits: an operation that takes time is started here and completed
 * by its interrupt, through the Completion the owner gave at open (completion.hpp).
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
#include "kb/port/stm32/completion.hpp"
#include "kb/port/stm32/error.hpp"
#include "kb/port/stm32/gpio.hpp"
#include "kb/port/stm32/i2c.hpp"
#include "kb/port/stm32/pwm.hpp"
#include "kb/port/stm32/spi.hpp"
#include "kb/port/stm32/time.hpp"
#include "kb/port/stm32/uart.hpp"
#include "state.hpp"

namespace kb::port::stm32::sdk {

// --- system -----------------------------------------------------------------------------------
/** @brief HAL_GetTick(): only to check that the HAL time base runs (HalTickRuns()). */
[[nodiscard]] uint32_t TickMs() noexcept;
/** @brief SystemCoreClock. */
[[nodiscard]] uint32_t CoreClockHz() noexcept;
/** @brief The DWT cycle counter, enabled on first use. */
[[nodiscard]] uint32_t Cycles() noexcept;
/** @brief Masks interrupts (PRIMASK); returns the PRIMASK before. */
[[nodiscard]] uint32_t IrqMask() noexcept;
/** @brief Restores the PRIMASK IrqMask() returned. */
void IrqRestore(uint32_t saved) noexcept;
/** @brief The port's shared state (state.hpp), one instance. */
[[nodiscard]] PortState& State() noexcept;

// --- GPIO ---------------------------------------------------------------------------------------
[[nodiscard]] bool PortExists(GpioPort port) noexcept;
[[nodiscard]] uint32_t PinOutput(PinId pin, bool open_drain, Pull pull, Speed speed,
                                 bool high) noexcept;
[[nodiscard]] uint32_t PinInput(PinId pin, Pull pull) noexcept;
/** @brief The pin in its alternate function (open drain for I2C). */
[[nodiscard]] uint32_t PinAlternate(PinId pin, bool open_drain, Pull pull) noexcept;
[[nodiscard]] uint32_t PinAnalog(PinId pin) noexcept;
void PinReset(PinId pin) noexcept;
void PinWrite(PinId pin, bool high) noexcept;
[[nodiscard]] bool PinRead(PinId pin) noexcept;
/** @brief HAL_GPIO_TogglePin (a read and a write: the caller masks interrupts around it). */
void PinToggle(PinId pin) noexcept;
/** @brief The pin as an EXTI input on `edge` (its pull kept), its NVIC line enabled. */
[[nodiscard]] uint32_t ExtiConfigure(PinId pin, kb::iface::Edge edge, uint8_t priority) noexcept;
/** @brief The pin back to a plain input with its pull (the EXTI line cleared). */
[[nodiscard]] uint32_t ExtiDeconfigure(PinId pin) noexcept;
/** @brief True when EXTI `line` is pending; clears it. */
[[nodiscard]] bool ExtiTakePending(uint8_t line) noexcept;

// --- I2C ----------------------------------------------------------------------------------------
[[nodiscard]] bool I2cExists(uint8_t instance) noexcept;
/** @brief True on the I2C v2 peripheral (TIMINGR: G4, H7, L4); false on F4. */
[[nodiscard]] bool I2cHasTimingRegister() noexcept;
[[nodiscard]] uint32_t I2cKernelClockHz(uint8_t instance) noexcept;
/** @brief Pins, HAL_I2C_Init, the event and error interrupts; `done` gets every transfer's end. */
[[nodiscard]] uint32_t I2cOpen(const I2cConfig& config, uint32_t timing, Completion& done) noexcept;
[[nodiscard]] uint32_t I2cClose(uint8_t instance) noexcept;
/** @brief Before a transfer: TXDR and RXDR flushed, stale CR2 transfer fields cleared (I2C v2). */
void I2cPrepare(uint8_t instance) noexcept;
/** @brief After a failure: the peripheral's interrupts off, error flags cleared, the data
 * registers flushed, the handle READY and unlocked. No PE toggle. */
void I2cRestore(uint8_t instance) noexcept;
/** @brief The BUSY flag. */
[[nodiscard]] bool I2cBusy(uint8_t instance) noexcept;
/** @brief Resets the peripheral: HAL_I2C_DeInit and HAL_I2C_Init (PE toggled; SWRST on F4). */
[[nodiscard]] uint32_t I2cReset(uint8_t instance) noexcept;
/** @brief Starts START, address, `tx` and STOP (`hold`: no STOP, a read follows). Empty: a probe.
 */
[[nodiscard]] uint32_t I2cStartTransmit(uint8_t instance, uint8_t address,
                                        std::span<const uint8_t> tx, bool hold) noexcept;
/** @brief Starts (repeated, when `after_write`) START, address, `rx` and STOP. */
[[nodiscard]] uint32_t I2cStartReceive(uint8_t instance, uint8_t address, std::span<uint8_t> rx,
                                       bool after_write) noexcept;

// --- SPI ----------------------------------------------------------------------------------------
[[nodiscard]] bool SpiExists(uint8_t instance) noexcept;
[[nodiscard]] uint32_t SpiKernelClockHz(uint8_t instance) noexcept;
/** @brief Pins, HAL_SPI_Init, the interrupt; `done` gets every transfer's end. */
[[nodiscard]] uint32_t SpiOpen(const SpiBusConfig& config, Completion& done) noexcept;
/** @brief HAL_SPI_Init again in `mode` with prescaler 2^(exponent + 1). */
[[nodiscard]] uint32_t SpiConfigure(uint8_t instance, uint8_t mode, uint8_t exponent) noexcept;
[[nodiscard]] uint32_t SpiClose(uint8_t instance) noexcept;
/**
 * @brief Starts one transfer of at most 65535 bytes by interrupt: full duplex when both spans are
 * given, transmit only when `rx` is empty, and for `tx` empty a full-duplex transfer that clocks
 * out `rx` (zeroed by the caller) in place.
 */
[[nodiscard]] uint32_t SpiStart(uint8_t instance, std::span<const uint8_t> tx,
                                std::span<uint8_t> rx) noexcept;
/** @brief After a timeout: HAL_SPI_Abort (a counted wait in the HAL, not the HAL tick). */
[[nodiscard]] uint32_t SpiAbort(uint8_t instance) noexcept;

// --- UART ---------------------------------------------------------------------------------------
[[nodiscard]] bool UartExists(uint8_t instance) noexcept;
/** @brief True when the family's UART has a 7-bit word length (not F4). */
[[nodiscard]] bool UartHasSevenBitWords() noexcept;
/** @brief Init, the receive interrupt and the owner that HandleUartIrq() calls. */
[[nodiscard]] uint32_t UartOpen(const UartConfig& config, Uart& owner) noexcept;
[[nodiscard]] uint32_t UartClose(uint8_t instance) noexcept;
[[nodiscard]] uint32_t UartSetBaud(uint8_t instance, uint32_t baud) noexcept;
/** @brief The transmission complete flag (TC). */
[[nodiscard]] bool UartTxComplete(uint8_t instance) noexcept;
/** @brief Writes one byte to the transmit data register. */
void UartWrite(uint8_t instance, uint8_t byte) noexcept;
/** @brief Enables or disables the transmit-empty (TXE) and transmit-complete (TC) interrupts. */
void UartTxInterrupts(uint8_t instance, bool empty, bool complete) noexcept;

// --- ADC ----------------------------------------------------------------------------------------
[[nodiscard]] bool AdcExists(uint8_t instance, uint8_t channel) noexcept;
[[nodiscard]] uint8_t AdcBits(uint8_t instance) noexcept;
/** @brief The ADC clock this port selects: hal::kOk, port_code::kNoClock (0 Hz) or
 * port_code::kUnsupported (above the family's limit). */
[[nodiscard]] uint32_t AdcClockCheck() noexcept;
/** @brief Clock, HAL_ADC_Init, calibration where the family has it, the interrupt. */
[[nodiscard]] uint32_t AdcOpen(uint8_t instance, Completion& conversion) noexcept;
[[nodiscard]] uint32_t AdcClose(uint8_t instance) noexcept;
/** @brief Configures `channel` and starts one conversion by interrupt (HAL_ADC_Start_IT). */
[[nodiscard]] uint32_t AdcStart(uint8_t instance, uint8_t channel, AdcSampling sampling) noexcept;
/** @brief After a timeout: HAL_ADC_Stop_IT. */
[[nodiscard]] uint32_t AdcStop(uint8_t instance) noexcept;

// --- timers (PWM) -------------------------------------------------------------------------------
[[nodiscard]] bool TimerExists(uint8_t timer) noexcept;
[[nodiscard]] uint32_t TimerClockHz(uint8_t timer) noexcept;
/** @brief HAL_TIM_PWM_Init of `timer` at `period`. */
[[nodiscard]] uint32_t PwmInit(uint8_t timer, const PwmPeriod& period) noexcept;
/** @brief The channel's pin, the channel at compare 0, started. */
[[nodiscard]] uint32_t PwmStart(uint8_t timer, uint8_t channel, PinId pin) noexcept;
[[nodiscard]] uint32_t PwmSetPeriod(uint8_t timer, const PwmPeriod& period) noexcept;
void PwmSetCompare(uint8_t timer, uint8_t channel, uint32_t compare) noexcept;
[[nodiscard]] uint32_t PwmStop(uint8_t timer, uint8_t channel) noexcept;
[[nodiscard]] uint32_t PwmDeInit(uint8_t timer) noexcept;

// --- CAN ----------------------------------------------------------------------------------------
[[nodiscard]] bool CanExists(uint8_t controller) noexcept;
/** @brief The limits of this family's controller (FDCAN or bxCAN). */
[[nodiscard]] CanTimingLimits CanLimits() noexcept;
[[nodiscard]] uint32_t CanKernelClockHz(uint8_t controller) noexcept;
/** @brief Init, filter, start, and the receive, overrun and transmit interrupts into `owner`. */
[[nodiscard]] uint32_t CanOpen(const CanConfig& config, const CanTiming& timing,
                               CanController& owner) noexcept;
[[nodiscard]] uint32_t CanClose(uint8_t controller) noexcept;
[[nodiscard]] uint32_t CanTxFree(uint8_t controller, uint32_t& free) noexcept;
[[nodiscard]] uint32_t CanRxCount(uint8_t controller, uint32_t& count) noexcept;
[[nodiscard]] uint32_t CanPut(uint8_t controller, const kb::iface::CanFrame& frame) noexcept;
[[nodiscard]] uint32_t CanGet(uint8_t controller, kb::iface::CanFrame& frame) noexcept;
[[nodiscard]] uint32_t CanReadStatus(uint8_t controller, CanStatus& status) noexcept;
/** @brief FDCAN: stop and start (leaves bus-off). bxCAN: nothing (automatic bus-off recovery). */
[[nodiscard]] uint32_t CanRestart(uint8_t controller) noexcept;

// --- clock, ITM ---------------------------------------------------------------------------------
[[nodiscard]] bool ClockTimerExists(uint8_t timer) noexcept;
/** @brief The timer at 1 MHz, free running, the update interrupt on, started. */
[[nodiscard]] uint32_t ClockStart(const ClockConfig& config) noexcept;
[[nodiscard]] uint32_t ClockCounter(uint8_t timer) noexcept;
/** @brief The update flag: a wrap the interrupt has not counted yet. */
[[nodiscard]] bool ClockWrapPending(uint8_t timer) noexcept;
void ClockClearWrap(uint8_t timer) noexcept;
/** @brief True when the ITM and stimulus port 0 are enabled (a probe listens). */
[[nodiscard]] bool ItmEnabled() noexcept;
/** @brief True when stimulus port 0 can take a character. */
[[nodiscard]] bool ItmPortFree() noexcept;
/** @brief Puts one character on stimulus port 0. */
void ItmWrite(char c) noexcept;

} // namespace kb::port::stm32::sdk

#endif // KB_PORT_STM32_SRC_SDK_HPP
