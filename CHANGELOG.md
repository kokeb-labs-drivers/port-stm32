# Changelog

All notable changes of port-stm32. The format follows Keep a Changelog; versions follow Semantic Versioning (before
1.0 a minor version may break).

## [Unreleased]

### Added

- Figures: a matrix of the bus contracts the port implements (native, adapted, planned, not available) on the home
  page, and a sequence of one transaction through the port on the I2C, SPI, UART and CAN pages, drawn by figkit from
  models in `docs/model/` (light and dark SVG, readable on a phone).
- CI path filter: a pull request that changes only documentation runs only the docs, handbook and DCO checks; the
  gate allows the code jobs to be skipped only then. The docs publish only from main and `v*` tags.
- DMA (`docs/dma.md`), on kb-core's `kb::dma` vocabulary: `dma.hpp` (masters and region tags of each
  family, `RequireStream` / `RequireStreamReaches`, `DmaStream` over DMA1/DMA2 streams and channels with the NVIC
  priority written from the board's assignment and read back, every flag serviced by its own clear bit on every
  interrupt entry, a stop bounded in core cycles that proves EN reads 0, quarantine and `Recover()`; `DmaTransfer`
  over a leased `kb::dma::Buffer`; the cache hooks and the start-time placement and MPU check), `spi_dma.hpp`
  (`SpiDmaLink`: SPI by DMA through a staging pair per bus, H7 order with TSIZE, frames from `min_bytes` at the full
  SCK, `Capabilities()`, `Health()`, `Limits()` and `Recover()` on `SpiBus`, `SpiLimitsOf`) and `adc_stream.hpp`
  (`AdcDmaStream`: a timer-triggered scan into a `kb::dma::Stream` of two frames, OVRMOD 0 with the overrun counted in
  the drain, the interrupt the only drainer, seqlock publication, supervision, idempotent start, the kernel clock
  read and never selected, DMA only when the conversion count read back equals the frame, else the one-shot fallback,
  reported). The DMA vectors of each family dispatch to the started stream.
- kb-core's DMA conformance suite T1 to T18 runs on the port (its DMA boundary linked against kb-core's host
  DMA-and-cache model, `tests/dma_model/`), in the AXI (`Maintained`) and D2 (`NonCacheable`) regions; unit and
  compile-fail tests of the DMA layer.
- The DMA bench kit: `tests/target/dma/dma_bench.cpp` (built for the four families by the example project), the
  procedure for an STM32H7 board with its wiring figures, the evidence form (`docs/assurance/bench/`).
- `src/boundary/atomic64.c` (targets only): the 64-bit atomic load and store the Arm GNU toolchain calls for
  `std::atomic<uint64_t>` on a Cortex-M, which `kb::dma::Stream` uses.

### Removed

- The `drivers/` submodules (pcal9555, tmc5160, bno08x), `drivers/drivers.lock.yml`, the pin-hygiene workflow and
  `driver_adapters.hpp`. The example firmware is now `port_selftest` (the port's self test only); chip drivers are
  bound through the port in kb-core's examples. A port stands alone and the repository root stays small.

### Changed

- kb-core moves to its republished main with the `kb::dma` part; `tools/kb_core_snapshot.py` copies the paths
  kb-core's `tools/layout/layout.py vendor` names instead of a list of its own.
- Port codes 0xFA to 0xFE (DMA error, quarantined, placement, frame too large, priority read-back); `MapHalStatus`
  maps placement and priority to `NotReady` and a frame too large to `OutOfRange`.
- An `AdcChannel` on an instance an ADC stream owns is refused (`Busy`).
- The interrupt vectors include the DMA streams or channels of the device (`KB_PORT_STM32_VECTORS_EXCLUDE` keeps one
  for the application).
- The FreeRTOS example configuration enables recursive mutexes and the stack high-water mark, which kb-core's
  FreeRTOS backend now uses.
- Source layout: every public header has a source file of the same name (`src/adc.cpp` for `adc.hpp`, and so
  on; `irq.hpp` and `version.hpp` are header-only), and the HAL boundary is one file per HAL component in
  `src/boundary/` (`i2c.cpp`, `spi.cpp`, `uart.cpp`, `gpio.cpp`, `adc.cpp`, `timer.cpp`, `can.cpp`, `time.cpp`,
  `log.cpp`, `error.cpp`, `state.cpp`, `vectors.cpp`). The combined files (`adc_pwm.cpp`, `stm32_*_boundary.cpp`)
  are gone; `time.cpp` is split into `time.cpp`, `completion.cpp` and `log.cpp`, `i2c_controller.cpp` is `i2c.cpp`
  and `spi_device.cpp` is `spi.cpp`. `tools/check_layout.py` (preflight and ctest) fails on a header without its
  source or a file that matches no header. No behaviour change; the MISRA records name the new files.

### Known limits

- The SPI interrupt ceilings are estimates; a bench measurement per family is not yet done. A long critical section
  or a higher-priority interrupt can still overrun an F4 frame at the ceiling (`BusError`).
- A shared ADC vector with an application instance on it must be excluded (`KB_PORT_STM32_VECTORS_EXCLUDE`); the port
  does not clear the application instance's flags.
- No CodeQL MISRA scan of the current code yet (the pull request's CI produces it).

### Fixed

- The UART transmit enable is preceded by a signal fence, so the frame stores cannot move past it (with LTO too).
- The blocking-wait bound is enabled for any spelling of `KB_RTOS` and for an installed kb-core's FreeRTOS or host
  backend; a FreeRTOS build takes `KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY` from its FreeRTOSConfig.h, and one that leaves
  it at 0 fails the configure.
- The SPI clock actually used is recorded (`Stats().actual_clock_hz`) and lowered selections counted (`clamps`).
- I2C recovery waits up to 25 ms for SCL to rise before it counts SCL as held and skips the clocks.
- The TIM2/TIM5 vector switches its NVIC line off when no `Clock` runs on that timer.

- SPI interrupt transfers are limited to a per-family SCK ceiling (core / 64 on F4, core / 40 with a FIFO), so the
  per-byte interrupt cannot overrun the receiver; `ActualClockHz()` reports the SCK used. An abort makes every
  device's next frame re-initialise the bus.
- A wait under FreeRTOS ends by the slept RTOS time even if the port's clock stops (the poll cap alone allowed hours).
- A vector the port serves for an instance it does not own switches its NVIC line off instead of re-entering;
  `KB_PORT_STM32_VECTORS_EXCLUDE` leaves named vectors to the application.
- The HAL time-base check accepts a 100 Hz HAL tick; `SetBaud()` and the FDCAN restart check it too.
- RTOS-calling interrupt priorities are checked against `KB_PORT_STM32_MIN_RTOS_IRQ_PRIORITY`; the ADC interrupt
  priority is configurable (`AdcConfig::irq_priority`).
- H7: the ADC uses the asynchronous kernel clock with a divider that meets 36 MHz when HCLK / 4 is above it.
- I2C: a failed re-initialisation in recovery leaves the controller not ready (it dereferenced a null instance);
  a recovery that finds SCL held low sends no clocks and is counted (`scl_held`).
- The UART transmit frame and the interrupt slot pointers are atomics with release/acquire order.
- CAN frames received before a `Release()` are not delivered after the reopen.

- The interrupt vectors are linked for certain: the vector file is an object library added to every firmware that
  links the port. Before, it sat in the static archive, a weak reference from the startup file never pulled it in,
  and every USART, EXTI and TIM2/TIM5 interrupt ran `Default_Handler`. The example firmware checks with `nm` after
  each link that every vector and HAL callback of the port is a strong symbol (`tools/check_vectors.cmake`).
- `Clock::Now()` never goes back: a new sample order (wrap count, counter, update flag, counter, wrap count), a
  sample in which the wrap interrupt ran is taken again, and the interrupt clears the flag and counts the wrap with
  interrupts masked. The earlier rule stepped forward by 2^32 us and back when the interrupt ran between its reads.
  `Clock::Start()` zeroes the counter (it keeps running across a jump from a loader).
- No wait reads `HAL_GetTick()` or spins at the caller's priority: I2C, SPI and ADC use the HAL's interrupt APIs,
  the UART transmits from its own interrupt, CAN receives into a ring from its RX FIFO interrupt; every wait is a
  `Completion` on a `kb::rtos` semaphore, bounded on the port's clock (the DWT before a `Clock` runs) and by a poll
  cap, with the operation stopped and the peripheral restored on a timeout. `Uart`, `CanController` and
  `AdcChannel` refuse `EnsureReady()` when the HAL time base does not run (the HAL's own init waits use it).
- UART 7-bit frames: the received word is masked to the data bits (7E1 and 7O1 returned the parity bit in bit 7);
  7N1 is refused on F4 (it silently became 8N1).
- PWM on the clock's timer is refused, and the channels of one timer must share its frequency.
- Every timeout is at most 2 s (`kMaxTimeout`); `HAL_MAX_DELAY` and longer values are refused.
- CAN frames are no longer lost behind a 3-deep hardware FIFO; a FIFO overrun or a full ring is `Overflow`.
- I2C: a BUSY flag that stays set with idle lines resets the peripheral (on every family); the handle is restored
  and the error flags cleared after every failure, PE toggled only while BUSY stays set; TXDR/RXDR flushed and the
  CR2 transfer fields cleared before every transfer.
- `LastError()`, `LastFailure()` and `Stats()` are free of data races (interrupt-masked records, atomics); the
  documentation states that they hold the most recent call by any task.
- The ADC instance's channel count and the EXTI line table change with interrupts masked (two tasks no longer
  initialise an instance twice, a handler no longer sees a new callback with an old context).
- The logic that sat in the boundary files (pin claims, EXTI dispatch, timer ownership, the ADC sharing, the clock
  wrap count, the UART interrupt's decisions, the ITM wait) moved to the platform-free files.
- Mutex and semaphore creation failures fail `EnsureReady()` (`Fault`, port code `kRtosObject`).
- A kernel clock that reads 0 Hz is refused (`NotReady`, `kNoClock`); the H7 I2C4 kernel clock is read from D3;
  TIMPRE is applied to the timer clock; the H7 ADC clock is refused above 36 MHz; F4 ADC channel 19 is refused.
- UART `Release()` keeps the RS-485 DE pin driven low; I2C recovery keeps the configured pull-ups.
- H7 read-only SPI frames are full duplex (the HAL's receive-only call switched to simplex receive).
- clang-tidy's configuration is valid for clang-tidy 18 (the host job ran nothing), and the pre-flight fails on a
  configuration error.

### Changed

- `I2cController::WriteRead()` writes any number of bytes before the repeated START (was at most 2).
- A write NACK is `Nack` (`DataNack`) unless `I2cConfig::nack_probe` is set; no address-only probe is sent by default
  and never during `EnsureReady()`.
- `Uart::write_timeout` bounds the whole write (was per 65535-byte chunk); `Uart::OnByte()`/`OnErrors()` became
  `OnInterrupt(UartEvent)`.
- `Clock::Combine()` takes a `ClockSample` and says whether the sample is consistent.
- New config fields: `I2cConfig::irq_priority`, `I2cConfig::nack_probe`, `SpiBusConfig::irq_priority`,
  `CanConfig::irq_priority`. New interrupt entry points: `HandleI2cIrq`, `HandleSpiIrq`, `HandleAdcIrq`,
  `HandleCanIrq`. The port defines the HAL completion callbacks of I2C, SPI, ADC and CAN.
- Dual-core STM32H7 parts and `CORE_CM4` builds are refused.
- kb-core copy re-pinned to kb-core `main` (`efd67da`); the snapshot tool copies the whole `cmake/` folder and refuses
  a commit that is not on `origin/main`.

### Added

- `completion.hpp` (`Deadline`, `Completion`), `docs/waits.md`, `docs/integration.md` (CubeMX, priorities, RTOS
  start-up, the H7 cache and DMA rules, dual core), `docs/kb-core-dependency.md`, `docs/misra-review.md`.
- The example under FreeRTOS (`-DEXAMPLE_RTOS=FREERTOS`, FreeRTOS-Kernel V11.1.0 pinned) and its CI build for G4.

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
