# port-stm32

kb-core's contracts (`kb::iface`) implemented over the **STM32Cube HAL** for the **STM32 G4, H7, F4 and L4**
families: one C++20 library, namespace `kb::port::stm32`, that lets any driver written against the kb contracts
run on an STM32 without a line of HAL code in the driver.

## The MCU at a glance

| | |
| --- | --- |
| Families | STM32G4 (Cortex-M4F, 170 MHz), STM32H7 (Cortex-M7F, up to 550 MHz), STM32F4 (Cortex-M4F, up to 180 MHz), STM32L4 (Cortex-M4F, 80 MHz) ([Families](families.md)) |
| SDK | STM32Cube HAL and CMSIS device files of each family, fetched unchanged at pinned tags and commits ([SOUP](soup.md)) |
| HAL modules | `hal_i2c`, `hal_spi`, `hal_uart`, `hal_gpio`, `hal_cortex` (NVIC), `hal_adc`, `hal_tim`, `hal_fdcan` (G4, H7) or `hal_can` (F4, L4), `hal_rcc`; CMSIS-Core for DWT and ITM |
| RTOS | Bare metal (`KB_RTOS=NONE`) or FreeRTOS (`KB_RTOS=FREERTOS`), through `kb::rtos` |
| Language | ISO C++20, no heap, no exceptions, no RTTI, no virtual functions |
| Level | High (kb-core ADR-0011): bounded building blocks, never a product's safety layer |

## Peripherals and contracts

| Peripheral | kb contract | Type | Page |
| --- | --- | --- | --- |
| I2C1 to I2C4 | `I2cBus` | `I2cController` | [I2C](peripherals/i2c.md) |
| SPI1 to SPI6 | `SpiDevice`, `SpiClockControl` | `SpiBus` + `SpiDevice` | [SPI](peripherals/spi.md) |
| USART, UART, LPUART1 | `UartPort`, `UartBaudControl`, `UartTxDrain` | `Uart` | [UART](peripherals/uart.md) |
| GPIO, EXTI | `GpioOutput` / `GpioInput`, `GpioInterrupt` | `OutputPin` / `InputPin` | [GPIO and EXTI](peripherals/gpio.md) |
| ADC1 to ADC5 | `AdcChannel` | `AdcChannel` | [ADC](peripherals/adc.md) |
| TIM (PWM mode 1) | `PwmOutput` | `PwmChannel` | [PWM](peripherals/pwm.md) |
| FDCAN (G4, H7), bxCAN (F4, L4) | `CanBus` | `CanController` | [CAN](peripherals/can.md) |
| TIM2/TIM5, DWT, ITM | `MonotonicClock`, `BusyWait`, `LogSink` | `Clock`, `BusyWait`, `ItmLogSink`, `UartLogSink` | [Clock, delay and log](peripherals/time-log.md) |

Every type satisfies its concepts (`static_assert` beside its definition) and converts to the contract's
type-erased `*Ref` (`I2cBusRef`, `SpiDeviceRef`, ...), proven at compile time in `kb/port/stm32/port.hpp` and at
run time in the host tests.

What the port does **not** implement, and why:

| Not implemented | Why |
| --- | --- |
| `WriteRead()` with more than 2 bytes to write | The HAL's repeated-START read (`HAL_I2C_Mem_Read`) takes a 1- or 2-byte register address; longer writes return `Unsupported` (use `Write()` then `Read()` when the device allows a STOP between them) |
| DMA and interrupt-driven I2C, SPI and UART transmit | Not yet kb-core contracts (asynchronous I2C/SPI are planned, M4/M5); the blocking calls are bounded by their timeouts |
| CAN FD frames, CAN filters | `CanFrame` is classic CAN; FDCAN runs in classic mode and accepts every frame into RX FIFO 0 |
| LPTIM, low-power modes, I3C, OCTOSPI, USB | kb-core has no contract for them |
| `PulseTx` | Not yet a kb-core contract |
| UART9, USART10 (H7) | Not served by the vector file or the instance table yet; USART1 to UART8 and LPUART1 are |
| PWM complementary outputs (CHxN), input capture, encoder mode | Beyond `PwmOutput`; no kb-core contract |
| Families other than G4, H7, F4, L4 | Not built or tested; the CMake device check refuses them |

## Rules that hold everywhere

- **Every wait is bounded**: every HAL call gets the configured timeout, every lock is taken with
  `TryLockFor(timeout)`, every poll counts `HAL_GetTick()` or a fixed number of spins.
- **Errors are values**: every call returns `kb::Status` or `kb::Result<T>`; the HAL status and the handle's
  `ErrorCode` are kept as the vendor code (`Detail()`, and `LastError()` on every object). See
  [Error mapping](error-mapping.md).
- **Bus recovery**: I2C stuck-bus detection and nine-clock recovery on the pins as GPIO; SPI aborted and
  initialised again after a HAL timeout; FDCAN restarted after bus-off, bxCAN by its automatic bus-off management.
- **Thread safety**: a lock per peripheral, bounded; see [Thread safety](thread-safety.md).
- **MISRA boundary**: only the boundary files `src/stm32_*_boundary.cpp` include HAL or CMSIS headers
  ([MISRA boundary](deviations.md)).

## Start

[Getting started](getting-started.md) adds the port to a CMake firmware project; the [examples](examples.md) bind
the pcal9555, tmc5160 and bno08x drivers through it on one device of each family.
