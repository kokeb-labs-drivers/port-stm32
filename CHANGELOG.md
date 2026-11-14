# Changelog

All notable changes of port-stm32. The format follows Keep a Changelog; versions follow Semantic Versioning (before
1.0 a minor version may break).

## [Unreleased]

## [0.1.0-rc.1]

First release candidate: kb-core's contracts over the STM32Cube HAL for the STM32 G4, H7, F4 and L4 families.

### Added

- `I2cController` (`I2cBus`): TIMINGR computed from the kernel clock (I2C v2) or ClockSpeed (F4), address and data
  NACK told apart, stuck-bus detection and nine-clock recovery on the pins as GPIO, one owner per instance,
  statistics, a bounded lock.
- `SpiBus` and `SpiDevice` (`SpiDevice`, `SpiClockControl`): GPIO chip selects, per-device mode and clock, frames
  of any length in 65535-byte chunks with chip select held, abort and re-initialisation after a HAL timeout.
- `Uart` (`UartPort`, `UartBaudControl`, `UartTxDrain`): the port's own receive interrupt into a 256-byte ring,
  latched receive errors, a bounded polling writer, RS-485 direction control, LPUART1, separate reader and writer
  locks.
- `OutputPin` and `InputPin` (`GpioOutput`, `GpioInput`, `GpioInterrupt`) with pin claims and EXTI line ownership.
- `AdcChannel` (`AdcChannel`): shared instances, calibration on G4, H7 and L4, three sampling classes.
- `PwmChannel` (`PwmOutput`): the finest duty steps the 16-bit auto-reload allows, idle level while disabled.
- `CanController` (`CanBus`): FDCAN in classic mode (G4, H7) and bxCAN (F4, L4), exact bit timing near 87.5 %,
  loopback and listen-only modes, bus-off recovery, polls that do not hold off each other.
- `Clock` (`MonotonicClock`, TIM2 or TIM5 extended to 64 bits), `BusyWait` (DWT), `ItmLogSink` and `UartLogSink`
  (`LogSink`); the interrupt entry points and an optional vector file.
- The HAL status and `ErrorCode` to `kb::Status` mapping with the vendor code kept in `Detail()`.
- The STM32Cube HAL, CMSIS device and CMSIS-Core fetched at pinned tags and commits, checked after the fetch.
- Host tests over fakes of every HAL call in two family variants, compile-fail tests, header checks; example
  firmware binding the pcal9555, tmc5160 and bno08x drivers for one device of each family.
