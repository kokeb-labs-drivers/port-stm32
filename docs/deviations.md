# MISRA boundary and deviations

The port is scanned with the CodeQL MISRA C++:2023 pack and the KB-CPP20 profile pack at level high
(`ci-misra.yml`, kb-core ADR-0011), on a traced host build and a traced Cortex-M build of `inc/` and `src/` (the
port over the HAL fakes, both family variants: `kb_port_stm32_scan`).

## The boundary

STM32Cube HAL and CMSIS headers are included only by the files of `src/boundary/` (`deviations/boundary-files.yml`):
one file per HAL component, named after the public header it serves, next to the logic file `src/<name>.cpp` of the
same name (`tools/check_layout.py` fails on a header without its source, on a combined file and on a file that
matches no header):

| File | HAL areas |
| --- | --- |
| `src/boundary/i2c.cpp` | `hal_i2c`; the I2C HAL callbacks and interrupt entry |
| `src/boundary/spi.cpp` | `hal_spi`; the SPI HAL callbacks and interrupt entry |
| `src/boundary/uart.cpp` | `hal_uart`; the UART interrupt's register reads and entry |
| `src/boundary/gpio.cpp` | `hal_gpio` and EXTI, `hal_rcc` (the port clocks) |
| `src/boundary/adc.cpp` | `hal_adc`, its callbacks and interrupt entry |
| `src/boundary/timer.cpp` | `hal_tim`: the PWM channels and the clock's counter share one table of handles |
| `src/boundary/can.cpp` | `hal_fdcan` (G4, H7) or `hal_can` (F4, L4), their callbacks and interrupt entry |
| `src/boundary/time.cpp` | `hal_cortex` (PRIMASK), the HAL tick, `SystemCoreClock`, DWT |
| `src/boundary/log.cpp` | ITM stimulus port 0 |
| `src/boundary/error.cpp` | The `HAL_StatusTypeDef` value check against `error.hpp` |
| `src/boundary/state.cpp` | The one instance of the shared state (includes no HAL header) |
| `src/boundary/vectors.cpp` | The device header's instance macros, for the interrupt vectors |

Each boundary function converts its arguments, makes one HAL call (or a fixed short sequence) and returns the
vendor code; a callback or an interrupt entry finds the owner of the handle and passes the event on. Every decision
is in the platform-free files, which include no HAL header and have **no deviation**: what a code means, retries,
recovery and the BUSY reset, locking, timing arithmetic and the waits (`src/time.cpp`), the UART's byte handling,
the CAN receive ring, pin claims and the EXTI table (`src/gpio.cpp`), timer ownership and the shared ADC instances
(`src/adc.cpp`, `src/pwm.cpp`), the clock's read rule (`src/clock_read.hpp`) and its wrap count. The layering check (`ci-layering.yml`)
enforces the boundary, and its poison build compiles the platform-free code with every HAL header replaced by one
that stops the build.

## Records

| Id | Guideline | Where | Why | How the risk is held |
| --- | --- | --- | --- | --- |
| DEV-001 | Rule 6.7.1 (local variables with static storage) | the I2C, SPI, UART, ADC, timer, CAN and shared-state boundary files | The HAL keeps each peripheral's state in a handle that outlives each call and is reached from the interrupt handlers and HAL callbacks, and no HAL type may reach a public header: the handles live in one function-local static table per peripheral, and the one instance of the platform-free shared state (`src/state.hpp`) in the same form | One accessor per table in an anonymous namespace; entries written by the owner's `EnsureReady()`, cleared by `Release()`; a second owner refused with `Busy`; entries shared with an interrupt are atomic and changed only with interrupts masked (`IrqGuard`); every accessor exercised by the host tests |
| DEV-002 | Rule 8.2.3 (casts that remove const) | the I2C boundary file, one call | `HAL_I2C_Master_Seq_Transmit_IT` takes `uint8_t*` for data it only reads; the contract hands the port `std::span<const uint8_t>`, and a write cannot be split into copies | One call site; the pinned HAL only reads through the pointer; the tests check the bytes reach the bus and the caller's buffer is unchanged |
| DEV-003 | Rule 10.2.3 (unscoped enum without a fixed underlying type) | the I2C boundary file, two stores in `I2cRestore` | `I2C_HandleTypeDef::State` and `::Mode` are volatile fields of the HAL's unscoped enumerations; the pinned HAL has no call that returns a handle to ready without a re-initialisation | Only the field's own enumerators are stored (no conversion); bus lock held and the instance's interrupts disabled; the tests start a transfer from ready after a NACK, a timeout and a recovery |

The records are in `deviations/deviations.yml`. A marker `// codeql::misra_deviation_next_line(DEV-001)` stands
on the line before each finding it covers. A clang-tidy suppression of a check the MISRA matrix relies on names its
record inside the comment, never as its first or last word (a bare id there would be read as a CodeQL marker):
`// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): DEV-002, the HAL lacks the const`. A marker that covers
nothing, an unknown id or a lapsed record (`review_by` passed) fails the scan.

## Findings avoided by design

| Guideline | What the HAL would have needed | What the port does instead |
| --- | --- | --- |
| Rule 8.2.5 (`reinterpret_cast`) | `HAL_UART_Transmit` takes `uint8_t*` for log text | `UartLogSink` converts each character with `std::bit_cast` into a byte buffer |
| Rule 24.5.2 (`memcpy`) | Copying frames and text with `memcpy` | `std::array` assignment and element loops |
| Rule 8.2.6 (`void*` to object pointer) | HAL handles in public headers | Handles stay in the boundary files' tables; the HAL's `void*` callbacks are not used |
| Rule 10.2.3 (`HAL_StatusTypeDef` cast to `void`) | Discarding the result of `HAL_SPI_Abort`, `HAL_ADC_Stop`, `HAL_FDCAN_Stop`, `HAL_CAN_Stop`, `HAL_CAN_ResetError` | Each result is used: folded into the reported status or `ErrorCode`, or compared with `HAL_OK` (the CAN error callback) |
| Rule 10.2.3 (`IRQn_Type` stored through a reference) | An out-parameter pair for the I2C event and error vectors | `I2cEventIrq()` and `I2cErrorIrq()` return each vector by value |
| Rule 8.2.6 in the HAL callbacks | Recovering the owner from the handle through a `void*` user field | Each callback finds its owner by comparing the handle's address with the port's table |

## Advisory findings left

| Guideline | Where | Why it stays |
| --- | --- | --- |
| Rule 6.5.1 (external linkage not declared in a header) | The interrupt vectors in `src/boundary/vectors.cpp`; the HAL callbacks the port defines (I2C, SPI, ADC, CAN) | The vector names are fixed by the startup file's table, which declares them weak; the callback names and signatures are the HAL's (declared in its headers); the entry points are declared in `irq.hpp` |
| Rule 10.1.1 (parameter could be const) | The HAL callbacks | Their signatures are the HAL's |

Advisory findings are reported and do not block.

## Review of the findings marked "to review"

[MISRA review of "to review" findings](misra-review.md) records the review of the findings the scan marks
"to review" (guidelines whose CodeQL query has known false positives): each class, its verdict and the reason.
